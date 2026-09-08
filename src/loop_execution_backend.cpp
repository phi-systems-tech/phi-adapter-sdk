#include "phi/adapter/sdk/loop_execution_backend.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <pthread.h>

#include "phi/runtime/epollloop.h"
#include "phi/runtime/loop.h"

#include "abandoned_threads.h"

namespace phicore::adapter::sdk {

namespace {

using detail::AbandonedThreadRegistry;

/// 15 bytes plus a terminator is what the kernel keeps of a thread name.
constexpr std::size_t kThreadNameLimit = 15;

void nameThisThread(const std::string &name)
{
    if (name.empty())
        return;
    ::pthread_setname_np(::pthread_self(), name.substr(0, kThreadNameLimit).c_str());
}

class LoopExecutionBackend final : public InstanceExecutionBackend
{
public:
    explicit LoopExecutionBackend(std::string threadName)
        : m_threadName(std::move(threadName))
    {
    }

    ~LoopExecutionBackend() override
    {
        phicore::adapter::v1::Utf8String ignoreError;
        stop(kShutdownBudget, &ignoreError);
    }

    bool start(phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (!m_state)
            m_state = std::make_shared<State>();
        if (m_thread.joinable()) {
            if (error)
                *error = "Execution backend thread still active";
            return false;
        }
        if (m_state->started)
            return true;

        auto state = m_state;
        {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->stopRequested = false;
            state->workerExited = false;
            state->loop = nullptr;
        }
        try {
            m_thread = std::thread([state, name = m_threadName]() { run(state, name); });
        } catch (const std::exception &ex) {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->workerExited = true;
            if (error)
                *error = std::string("Failed to create execution thread: ") + ex.what();
            return false;
        }

        // The loop is built on its own thread, so start() cannot return until
        // it exists: an execute() immediately after start() would otherwise
        // find nothing to post to.
        std::unique_lock<std::mutex> stateLock(state->mutex);
        state->readyCv.wait(stateLock, [&state]() {
            return state->loop != nullptr || state->workerExited;
        });
        if (!state->loop) {
            stateLock.unlock();
            m_thread.join();
            if (error)
                *error = "Execution backend loop failed to start";
            return false;
        }
        state->started = true;
        return true;
    }

    bool execute(std::function<void()> task,
                 phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        if (!task) {
            if (error)
                *error = "Execution task is empty";
            return false;
        }
        auto state = m_state;
        if (!state) {
            if (error)
                *error = "Execution backend not initialized";
            return false;
        }
        // Held across the post so the loop cannot be torn down between the
        // check and the call; post() itself is the loop's thread-safe entry.
        std::lock_guard<std::mutex> stateLock(state->mutex);
        if (!state->started || !state->loop) {
            if (error)
                *error = "Execution backend not started";
            return false;
        }
        if (state->stopRequested) {
            if (error)
                *error = "Execution backend is stopping";
            return false;
        }
        state->loop->post(std::move(task));
        return true;
    }

    bool stop(std::chrono::milliseconds timeout,
              phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::shared_ptr<State> state;
        std::thread thread;
        {
            std::lock_guard<std::mutex> lock(m_lifecycleMutex);
            state = m_state;
            if (!state)
                return true;
            if (!state->started && !m_thread.joinable())
                return true;
            {
                std::lock_guard<std::mutex> stateLock(state->mutex);
                state->stopRequested = true;
                // Null only in the window before the worker published its loop;
                // run() re-reads the flag there and never enters run().
                if (state->loop)
                    state->loop->stop();
            }
            thread = std::move(m_thread);
        }

        if (!thread.joinable()) {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->started = false;
            state->stopRequested = false;
            state->workerExited = true;
            return true;
        }

        const auto effectiveTimeout = timeout < std::chrono::milliseconds::zero()
            ? std::chrono::milliseconds::zero()
            : timeout;
        bool exited = false;
        {
            std::unique_lock<std::mutex> stateLock(state->mutex);
            exited = state->exitCv.wait_for(stateLock, effectiveTimeout, [&state]() {
                return state->workerExited;
            });
        }

        if (!exited) {
            // Parked inside adapter code. Hand the thread over rather than
            // detach it (F-35): the predicate holds its own reference to
            // `state`, so the worker keeps a valid State even though this
            // backend moves on to a fresh one.
            AbandonedThreadRegistry::instance().adopt(std::move(thread), [state]() {
                std::lock_guard<std::mutex> stateLock(state->mutex);
                return state->workerExited;
            });
            {
                std::lock_guard<std::mutex> lock(m_lifecycleMutex);
                if (m_state == state)
                    m_state = std::make_shared<State>();
            }
            if (error)
                *error = "Timed out waiting for execution backend stop";
            return false;
        }

        thread.join();
        {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->started = false;
            state->stopRequested = false;
        }
        return true;
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable readyCv;
        std::condition_variable exitCv;
        /// Valid only between the worker publishing it and the worker dropping
        /// it, both under `mutex`; nobody outside the worker's thread may do
        /// anything with it but post() and stop().
        phi::runtime::Loop *loop = nullptr;
        bool started = false;
        bool stopRequested = false;
        bool workerExited = true;
    };

    static void run(const std::shared_ptr<State> &state, const std::string &name)
    {
        nameThisThread(name);
        {
            phi::runtime::EpollLoop loop;
            bool alreadyStopped = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->loop = &loop;
                alreadyStopped = state->stopRequested;
            }
            state->readyCv.notify_all();

            if (!alreadyStopped)
                loop.run();

            // Published before the loop is destroyed, so an execute() racing
            // the teardown finds a null and reports it instead of posting into
            // a dying loop.
            std::lock_guard<std::mutex> lock(state->mutex);
            state->loop = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->workerExited = true;
        }
        state->exitCv.notify_all();
    }

    std::string m_threadName;
    std::mutex m_lifecycleMutex;
    std::shared_ptr<State> m_state = std::make_shared<State>();
    std::thread m_thread;
};

} // namespace

std::unique_ptr<InstanceExecutionBackend> createLoopExecutionBackend(std::string threadName)
{
    return std::make_unique<LoopExecutionBackend>(std::move(threadName));
}

} // namespace phicore::adapter::sdk
