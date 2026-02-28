#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
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

class ExampleAdapter final : public phicore::adapter::sdk::AdapterSidecar
{
public:
    void onConnected() override
    {
        std::cerr << "core connected" << std::endl;
    }

    void onDisconnected() override
    {
        std::cerr << "core disconnected" << std::endl;
    }

    void onProtocolError(const phicore::adapter::v1::Utf8String &message) override
    {
        std::cerr << "protocol error: " << message << std::endl;
    }

    void onBootstrap(const phicore::adapter::sdk::BootstrapRequest &request) override
    {
        AdapterSidecar::onBootstrap(request);
        std::cerr << "bootstrap adapterId=" << request.adapterId
                  << " extId=" << request.adapter.externalId
                  << " plugin=" << request.adapter.pluginType << std::endl;
    }

    phicore::adapter::v1::CmdResponse onChannelInvoke(
        const phicore::adapter::sdk::ChannelInvokeRequest &request) override
    {
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
    }

    phicore::adapter::v1::ActionResponse onAdapterActionInvoke(
        const phicore::adapter::sdk::AdapterActionInvokeRequest &request) override
    {
        phicore::adapter::v1::ActionResponse response;
        response.id = request.cmdId;
        response.status = phicore::adapter::v1::CmdStatus::Success;
        response.resultType = phicore::adapter::v1::ActionResultType::String;
        response.resultValue = phicore::adapter::v1::Utf8String("ok");
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "adapter action invoke actionId=" << request.actionId << std::endl;
        return response;
    }
};

class ExampleFactory final : public phicore::adapter::sdk::AdapterFactory
{
public:
    phicore::adapter::v1::Utf8String pluginType() const override
    {
        return "example";
    }

    std::unique_ptr<phicore::adapter::sdk::AdapterSidecar> create() const override
    {
        return std::make_unique<ExampleAdapter>();
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const char *envSocketPath = std::getenv("PHI_ADAPTER_SOCKET_PATH");
    const phicore::adapter::v1::Utf8String socketPath = (argc > 1)
        ? argv[1]
        : (envSocketPath ? envSocketPath : phicore::adapter::v1::Utf8String("/tmp/phi-adapter-example.sock"));

    ExampleFactory factory;
    phicore::adapter::sdk::SidecarHost host(socketPath, factory);

    phicore::adapter::v1::Utf8String error;
    if (!host.start(&error)) {
        std::cerr << "host start failed: " << error << std::endl;
        return 1;
    }

    std::cerr << "phi adapter sidecar host example listening on " << socketPath << std::endl;

    while (g_running.load()) {
        if (!host.pollOnce(std::chrono::milliseconds(250), &error)) {
            std::cerr << "host poll failed: " << error << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    host.stop();
    return 0;
}
