// The loop execution backend: the thing that replaces the Qt one.
//
// Four properties, and the last two are the ones that made this worth writing
// rather than trusting. A backend that cannot arm a timer sends adapters back
// to QtCore, and a backend whose stop() waits forever on stuck adapter code
// turns one wedged instance into a wedged sidecar.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/sdk/loop_execution_backend.h"
#include "phi/runtime/loop.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
namespace sdk = phicore::adapter::sdk;

/// Blocks the calling test until `predicate` holds or the deadline passes.
/// Returns whether it held - a test that reports a timeout as a failure says
/// more than one that hangs.
template <typename Predicate>
bool waitFor(std::mutex &mutex,
             std::condition_variable &cv,
             Predicate predicate,
             std::chrono::milliseconds budget = 2000ms)
{
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, budget, predicate);
}

void testTasksRunOnOneThreadInOrder()
{
    auto backend = sdk::createLoopExecutionBackend("phi-test-order");
    phicore::adapter::v1::Utf8String error;
    PHI_CHECK_MSG(backend->start(&error), "start failed: %s", error.c_str());

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<int> seen;
    std::vector<std::thread::id> threads;

    for (int i = 0; i < 8; ++i) {
        const bool queued = backend->execute([i, &mutex, &cv, &seen, &threads]() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                seen.push_back(i);
                threads.push_back(std::this_thread::get_id());
            }
            cv.notify_all();
        });
        PHI_CHECK(queued);
    }

    PHI_CHECK(waitFor(mutex, cv, [&seen]() { return seen.size() == 8; }));

    std::lock_guard<std::mutex> lock(mutex);
    for (std::size_t i = 0; i < seen.size(); ++i)
        PHI_CHECK_MSG(seen[i] == static_cast<int>(i), "task %d ran at position %zu",
                      seen[i], i);
    // One thread, not a pool: adapter state touched only by callbacks needs no
    // locking, and every adapter written against the Qt backend assumes it.
    for (const std::thread::id &id : threads)
        PHI_CHECK(id == threads.front());
    PHI_CHECK(threads.front() != std::this_thread::get_id());

    PHI_CHECK(backend->stop(2000ms, &error));
}

void testACallbackCanArmATimerOnItsOwnLoop()
{
    // The whole reason this backend exists. Under the default backend there is
    // no loop to find here, and an adapter that wants to poll has to bring one.
    auto backend = sdk::createLoopExecutionBackend("phi-test-timer");
    phicore::adapter::v1::Utf8String error;
    PHI_CHECK(backend->start(&error));

    std::mutex mutex;
    std::condition_variable cv;
    int ticks = 0;
    phi::runtime::Timer timer;

    PHI_CHECK(backend->execute([&]() {
        phi::runtime::Loop *loop = phi::runtime::Loop::current();
        PHI_CHECK_MSG(loop != nullptr, "no loop on the backend thread");
        if (!loop)
            return;
        timer = loop->timerEvery(20ms, [&]() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++ticks;
            }
            cv.notify_all();
        });
    }));

    PHI_CHECK(waitFor(mutex, cv, [&ticks]() { return ticks >= 3; }));

    // The timer is owned by the loop's thread, so it is dropped there too.
    PHI_CHECK(backend->execute([&timer]() { timer.reset(); }));
    PHI_CHECK(backend->stop(2000ms, &error));
}

void testStopComesBackOnTimeEvenWithAWedgedCallback()
{
    // F-33/F-35 in one test: adapter code that will not return must not be able
    // to hold the sidecar's teardown open, and the thread must be kept rather
    // than detached or killed.
    auto backend = sdk::createLoopExecutionBackend("phi-test-wedge");
    phicore::adapter::v1::Utf8String error;
    PHI_CHECK(backend->start(&error));

    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    std::atomic_bool release{false};

    PHI_CHECK(backend->execute([&]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            entered = true;
        }
        cv.notify_all();
        while (!release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(5ms);
    }));
    PHI_CHECK(waitFor(mutex, cv, [&entered]() { return entered; }));

    const auto before = std::chrono::steady_clock::now();
    const bool stopped = backend->stop(200ms, &error);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - before);

    PHI_CHECK_MSG(!stopped, "stop() claimed a clean stop while a callback was still running");
    PHI_CHECK_MSG(!error.empty(), "a timed-out stop must say so");
    // The budget, not the callback, decides how long teardown takes.
    PHI_CHECK_MSG(elapsed < 1000ms, "stop() waited %lldms for a wedged callback",
                  static_cast<long long>(elapsed.count()));

    release.store(true, std::memory_order_release);
    // The worker still owns state the registry keeps alive; give it its moment
    // so the process does not end underneath it.
    std::this_thread::sleep_for(100ms);
}

void testWorkIsRefusedRatherThanLostAfterStop()
{
    auto backend = sdk::createLoopExecutionBackend("phi-test-refuse");
    phicore::adapter::v1::Utf8String error;
    PHI_CHECK(backend->start(&error));
    PHI_CHECK(backend->stop(2000ms, &error));

    std::atomic_bool ran{false};
    phicore::adapter::v1::Utf8String executeError;
    const bool queued = backend->execute([&ran]() { ran.store(true); }, &executeError);
    PHI_CHECK_MSG(!queued, "a stopped backend accepted work");
    PHI_CHECK(!executeError.empty());
    std::this_thread::sleep_for(50ms);
    PHI_CHECK(!ran.load());

    // An empty task is a caller bug, not something to schedule.
    PHI_CHECK(!backend->execute({}, &executeError));
}

} // namespace

int main()
{
    testTasksRunOnOneThreadInOrder();
    testACallbackCanArmATimerOnItsOwnLoop();
    testStopComesBackOnTimeEvenWithAWedgedCallback();
    testWorkIsRefusedRatherThanLostAfterStop();
    return phi::testing::report("sdk_loop_execution_backend_tests");
}
