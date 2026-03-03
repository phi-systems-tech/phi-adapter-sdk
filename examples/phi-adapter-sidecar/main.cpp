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
namespace sdk = phicore::adapter::sdk;

void handleSignal(int)
{
    g_running.store(false);
}

class ExampleAdapter final : public sdk::AdapterInstance
{
protected:
    bool start() override
    {
        std::cerr << "instance start extId=" << externalId() << std::endl;
        return true;
    }

    void stop() override
    {
        std::cerr << "instance stop extId=" << externalId() << std::endl;
    }

    void onConnected() override
    {
        std::cerr << "core connected" << std::endl;
    }

    void onDisconnected() override
    {
        std::cerr << "core disconnected" << std::endl;
    }

    void onProtocolError(const sdk::Utf8String &message) override
    {
        std::cerr << "protocol error: " << message << std::endl;
    }

    void onConfigChanged(const sdk::ConfigChangedRequest &request) override
    {
        std::cerr << "config.changed adapterId=" << request.adapterId
                  << " extId=" << request.adapter.externalId
                  << " ip=" << request.adapter.ip
                  << " port=" << request.adapter.port << std::endl;
    }

    sdk::CmdResponse onChannelInvoke(const sdk::ChannelInvokeRequest &request) override
    {
        sdk::CmdResponse response;
        response.id = request.cmdId;
        response.status = sdk::CmdStatus::Success;
        response.finalValue = request.value;
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "channel invoke extId=" << request.externalId
                  << " device=" << request.deviceExternalId
                  << " channel=" << request.channelExternalId << std::endl;
        return response;
    }

    sdk::ActionResponse onAdapterActionInvoke(const sdk::AdapterActionInvokeRequest &request) override
    {
        sdk::ActionResponse response;
        response.id = request.cmdId;
        response.status = sdk::CmdStatus::Success;
        if (request.actionId == "browseHosts") {
            response.resultType = sdk::ActionResultType::None;
            response.formValuesJson =
                R"json({"trackedMacs":["1c:90:ff:0b:58:77","26:d2:aa:57:79:46"]})json";
            response.fieldChoicesJson =
                R"json({"trackedMacs":[{"value":"1c:90:ff:0b:58:77","label":"Zigbee (192.168.1.77)"},{"value":"26:d2:aa:57:79:46","label":"Phone (192.168.1.76)"},{"value":"cc:8c:bf:76:0c:54","label":"Heater (192.168.1.26)"}]})json";
            response.reloadLayout = false;
        } else {
            response.resultType = sdk::ActionResultType::String;
            response.resultValue = sdk::Utf8String("ok");
        }
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "adapter action invoke extId=" << request.externalId
                  << " actionId=" << request.actionId << std::endl;
        return response;
    }
};

class ExampleFactory final : public sdk::AdapterFactory
{
protected:
    sdk::Utf8String pluginType() const override
    {
        return "example";
    }

    sdk::Utf8String displayName() const override
    {
        return "Example";
    }

    std::unique_ptr<sdk::AdapterInstance> createInstance(const sdk::ExternalId &externalId) override
    {
        std::cerr << "create instance for externalId=" << externalId << std::endl;
        return std::make_unique<ExampleAdapter>();
    }

    sdk::ActionResponse onFactoryActionInvoke(const sdk::AdapterActionInvokeRequest &request) override
    {
        sdk::ActionResponse response;
        response.id = request.cmdId;
        response.status = sdk::CmdStatus::Success;
        response.resultType = sdk::ActionResultType::String;
        response.resultValue = sdk::Utf8String("factory-ok");
        return response;
    }

    void onBootstrap(const sdk::BootstrapRequest &request) override
    {
        std::cerr << "factory bootstrap adapterId=" << request.adapterId
                  << " extId='" << request.adapter.externalId
                  << "' plugin=" << request.adapter.pluginType << std::endl;
    }
};

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const char *envSocketPath = std::getenv("PHI_ADAPTER_SOCKET_PATH");
    const sdk::Utf8String socketPath = (argc > 1)
        ? argv[1]
        : (envSocketPath ? envSocketPath : sdk::Utf8String("/tmp/phi-adapter-example.sock"));

    ExampleFactory factory;
    sdk::SidecarHost host(socketPath, factory);

    sdk::Utf8String error;
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
