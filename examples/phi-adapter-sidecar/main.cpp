#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "phi/adapter/sdk/runtime.h"
#include "phi/adapter/v1/types.h"

namespace {

std::atomic_bool g_running{true};

void handleSignal(int)
{
    g_running.store(false);
}

std::vector<std::byte> toBytes(const std::string &text)
{
    const auto *raw = reinterpret_cast<const std::byte *>(text.data());
    return {raw, raw + text.size()};
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const std::string socketPath = (argc > 1) ? argv[1] : std::string("/tmp/phi-adapter-example.sock");

    phicore::adapter::sdk::SidecarRuntime runtime(socketPath);
    runtime.setCallbacks({
        .onConnected = []() {
            std::cerr << "client connected" << std::endl;
        },
        .onDisconnected = []() {
            std::cerr << "client disconnected" << std::endl;
        },
        .onFrame = [&](const phicore::adapter::v1::FrameHeader &header, std::span<const std::byte> payload) {
            const std::string text(reinterpret_cast<const char *>(payload.data()), payload.size());
            std::cerr << "rx type=" << static_cast<int>(header.type)
                      << " corr=" << header.correlationId
                      << " payload=" << text << std::endl;

            const std::string ack = R"({"event":"adapter.heartbeat","ok":true})";
            const auto ackBytes = toBytes(ack);
            std::string sendError;
            if (!runtime.send(phicore::adapter::v1::MessageType::Event,
                              header.correlationId,
                              ackBytes,
                              &sendError)) {
                std::cerr << "send failed: " << sendError << std::endl;
            }
        },
    });

    std::string error;
    if (!runtime.start(&error)) {
        std::cerr << "runtime start failed: " << error << std::endl;
        return 1;
    }

    std::cerr << "phi adapter sidecar example listening on " << socketPath << std::endl;

    while (g_running.load()) {
        if (!runtime.pollOnce(std::chrono::milliseconds(250), &error)) {
            std::cerr << "runtime poll failed: " << error << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    runtime.stop();
    return 0;
}
