#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "phi/adapter/sdk/sidecar.h"
#include "phi/adapter/v1/types.h"

namespace {

std::atomic_bool g_running{true};

void handleSignal(int)
{
    g_running.store(false);
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const char *envSocketPath = std::getenv("PHI_ADAPTER_SOCKET_PATH");
    const std::string socketPath = (argc > 1)
        ? argv[1]
        : (envSocketPath ? envSocketPath : std::string("/tmp/phi-adapter-example.sock"));

    phicore::adapter::sdk::SidecarDispatcher dispatcher(socketPath);
    phicore::adapter::sdk::SidecarHandlers handlers;
    handlers.onConnected = []() {
        std::cerr << "core connected" << std::endl;
    };
    handlers.onDisconnected = []() {
        std::cerr << "core disconnected" << std::endl;
    };
    handlers.onProtocolError = [](const std::string &message) {
        std::cerr << "protocol error: " << message << std::endl;
    };
    handlers.onBootstrap = [](const phicore::adapter::sdk::BootstrapRequest &request) {
        std::cerr << "bootstrap adapterId=" << request.adapterId
                  << " extId=" << request.adapter.externalId
                  << " plugin=" << request.adapter.pluginType << std::endl;
    };
    handlers.onChannelInvoke = [](const phicore::adapter::sdk::ChannelInvokeRequest &request) {
        phicore::adapter::v1::CmdResponse response;
        response.id = request.cmdId;
        response.status = phicore::adapter::v1::CmdStatus::Success;
        response.finalValue = request.value;
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "channel invoke device=" << request.deviceExternalId
                  << " channel=" << request.channelExternalId << std::endl;
        return response;
    };
    handlers.onAdapterActionInvoke = [](const phicore::adapter::sdk::AdapterActionInvokeRequest &request) {
        phicore::adapter::v1::ActionResponse response;
        response.id = request.cmdId;
        response.status = phicore::adapter::v1::CmdStatus::Success;
        response.resultType = phicore::adapter::v1::ActionResultType::String;
        response.resultValue = std::string("ok");
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "adapter action invoke actionId=" << request.actionId << std::endl;
        return response;
    };
    dispatcher.setHandlers(std::move(handlers));

    std::string error;
    if (!dispatcher.start(&error)) {
        std::cerr << "dispatcher start failed: " << error << std::endl;
        return 1;
    }

    std::cerr << "phi adapter sidecar dispatcher example listening on " << socketPath << std::endl;

    while (g_running.load()) {
        if (!dispatcher.pollOnce(std::chrono::milliseconds(250), &error)) {
            std::cerr << "dispatcher poll failed: " << error << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    dispatcher.stop();
    return 0;
}
