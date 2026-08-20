#include "phi/adapter/sdk/sidecar.h"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace phicore::adapter::sdk {

namespace {

std::atomic_bool g_stopRequested{false};

void handleStopSignal(int)
{
    g_stopRequested.store(true, std::memory_order_relaxed);
}

// Back-off after a failing poll so a persistent failure cannot turn the loop
// into a busy spin.
constexpr auto kPollFailureBackoff = std::chrono::milliseconds(200);

// Grace for a worker that missed its stop deadline. It may be milliseconds from
// returning, and reaping it here turns an abrupt exit back into a normal one.
constexpr auto kAbandonedThreadGrace = std::chrono::milliseconds(250);

} // namespace

int runSidecarMain(SidecarHost &host, const SidecarMainOptions &options)
{
    phicore::adapter::v1::Utf8String error;
    if (!host.start(&error)) {
        // Pre-bootstrap failure: structured logging is not available yet.
        std::cerr << "[sidecar][startFailure][host] " << error << std::endl;
        return 1;
    }

    if (options.installSignalHandlers) {
        g_stopRequested.store(false, std::memory_order_relaxed);
        std::signal(SIGINT, handleStopSignal);
        std::signal(SIGTERM, handleStopSignal);
    }

    for (;;) {
        if (options.installSignalHandlers && g_stopRequested.load(std::memory_order_relaxed))
            break;
        if (options.keepRunning && !options.keepRunning())
            break;

        phicore::adapter::v1::Utf8String pollError;
        if (!host.pollOnce(options.pollTimeout, &pollError)) {
            std::cerr << "[sidecar][pollFailure][host] " << pollError << std::endl;
            std::this_thread::sleep_for(kPollFailureBackoff);
        }

        if (options.onIteration)
            options.onIteration();
    }

    host.stop();

    // A worker that missed its stop budget is still executing adapter code
    // (F-35). Returning from main would run static destructors underneath it,
    // so once it is clear the thread is not coming back, leave without touching
    // them: the stuck thread cannot then fault against a half-destroyed process
    // after shutdown was already reported, and it cannot deadlock exit by
    // holding an allocator or stdio lock.
    const std::size_t stuck = reapAbandonedExecutionThreads(kAbandonedThreadGrace);
    if (stuck > 0) {
        std::cerr << "[sidecar][abandonedThreads] " << stuck
                  << " execution thread(s) did not stop within the shutdown budget; "
                     "exiting without static destructors" << std::endl;
        std::cerr.flush();
        std::cout.flush();
        // Shutdown itself completed and was reported, so the exit status stays
        // clean; the defect is on the stderr line above.
        std::_Exit(0);
    }

    return 0;
}

} // namespace phicore::adapter::sdk
