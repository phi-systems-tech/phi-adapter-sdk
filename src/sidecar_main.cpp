#include "phi/adapter/sdk/sidecar.h"

#include <atomic>
#include <csignal>
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
    return 0;
}

} // namespace phicore::adapter::sdk
