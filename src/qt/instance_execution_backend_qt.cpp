#include "phi/adapter/sdk/qt/instance_execution_backend_qt.h"

#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include <QMetaObject>
#include <QObject>
#include <QThread>

namespace phicore::adapter::sdk::qt {

namespace {

class QtInstanceExecutionBackend final : public phicore::adapter::sdk::InstanceExecutionBackend
{
public:
    QtInstanceExecutionBackend()
        : m_impl(std::make_unique<Impl>())
    {
    }

    ~QtInstanceExecutionBackend() override
    {
        phicore::adapter::v1::Utf8String ignoreError;
        stop(std::chrono::seconds(3), &ignoreError);
    }

    bool start(phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->started)
            return true;

        auto thread = std::make_unique<QThread>();
        auto target = std::make_unique<QObject>();
        target->moveToThread(thread.get());
        thread->start();
        if (!thread->isRunning()) {
            if (error)
                *error = "Qt execution thread failed to start";
            return false;
        }

        m_impl->thread = std::move(thread);
        m_impl->target = std::move(target);
        m_impl->started = true;
        return true;
    }

    bool execute(std::function<void()> task, phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        if (!task) {
            if (error)
                *error = "Execution task is empty";
            return false;
        }

        QObject *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (!m_impl->started || !m_impl->thread || !m_impl->target || !m_impl->thread->isRunning()) {
                if (error)
                    *error = "Qt execution backend not started";
                return false;
            }
            target = m_impl->target.get();
        }

        auto sharedTask = std::make_shared<std::function<void()>>(std::move(task));
        const bool queued = QMetaObject::invokeMethod(
            target,
            [sharedTask]() {
                try {
                    (*sharedTask)();
                } catch (...) {
                    // Keep backend alive if adapter work throws.
                }
            },
            Qt::QueuedConnection);
        if (!queued && error)
            *error = "Failed to enqueue task on Qt execution backend";
        return queued;
    }

    bool stop(std::chrono::milliseconds timeout,
              phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::unique_ptr<QThread> thread;
        std::unique_ptr<QObject> target;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (!m_impl->started)
                return true;
            m_impl->started = false;
            thread = std::move(m_impl->thread);
            target = std::move(m_impl->target);
        }

        if (target) {
            QObject *rawTarget = target.release();
            if (!QMetaObject::invokeMethod(rawTarget, "deleteLater", Qt::QueuedConnection))
                delete rawTarget;
        }

        if (!thread)
            return true;
        thread->quit();
        const auto clampedTimeout = timeout < std::chrono::milliseconds::zero()
            ? std::chrono::milliseconds::zero()
            : timeout;
        const auto timeoutCount = clampedTimeout.count();
        const int waitMs = timeoutCount > static_cast<long long>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(timeoutCount);
        if (!thread->wait(waitMs)) {
            if (error)
                *error = "Timed out waiting for Qt execution backend stop";
            thread->terminate();
            thread->wait(1000);
            return false;
        }
        return true;
    }

private:
    struct Impl {
        std::mutex mutex;
        std::unique_ptr<QThread> thread;
        std::unique_ptr<QObject> target;
        bool started = false;
    };

    std::unique_ptr<Impl> m_impl;
};

} // namespace

std::unique_ptr<phicore::adapter::sdk::InstanceExecutionBackend> createInstanceExecutionBackend()
{
    return std::make_unique<QtInstanceExecutionBackend>();
}

} // namespace phicore::adapter::sdk::qt
