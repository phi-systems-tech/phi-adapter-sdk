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

class ExampleAdapter final : public phicore::adapter::sdk::AdapterInstance
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

    void onProtocolError(const phicore::adapter::v1::Utf8String &message) override
    {
        std::cerr << "protocol error: " << message << std::endl;
    }

    void onConfigChanged(const phicore::adapter::sdk::ConfigChangedRequest &request) override
    {
        std::cerr << "config.changed adapterId=" << request.adapterId
                  << " extId=" << request.adapter.externalId
                  << " ip=" << request.adapter.ip
                  << " port=" << request.adapter.port << std::endl;
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
        std::cerr << "channel invoke extId=" << request.externalId
                  << " device=" << request.deviceExternalId
                  << " channel=" << request.channelExternalId << std::endl;
        return response;
    }

    phicore::adapter::v1::ActionResponse onAdapterActionInvoke(
        const phicore::adapter::sdk::AdapterActionInvokeRequest &request) override
    {
        phicore::adapter::v1::ActionResponse response;
        response.id = request.cmdId;
        response.status = phicore::adapter::v1::CmdStatus::Success;
        if (request.actionId == "browseHosts") {
            response.resultType = phicore::adapter::v1::ActionResultType::None;
            response.formValuesJson =
                R"json({"trackedMacs":["1c:90:ff:0b:58:77","26:d2:aa:57:79:46"]})json";
            response.fieldChoicesJson =
                R"json({"trackedMacs":[{"value":"1c:90:ff:0b:58:77","label":"Zigbee (192.168.1.77)"},{"value":"26:d2:aa:57:79:46","label":"Phone (192.168.1.76)"},{"value":"cc:8c:bf:76:0c:54","label":"Heater (192.168.1.26)"}]})json";
            response.reloadLayout = false;
        } else {
            response.resultType = phicore::adapter::v1::ActionResultType::String;
            response.resultValue = phicore::adapter::v1::Utf8String("ok");
        }
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "adapter action invoke extId=" << request.externalId
                  << " actionId=" << request.actionId << std::endl;
        return response;
    }
};

class ExampleFactory final : public phicore::adapter::sdk::AdapterFactory
{
protected:
    phicore::adapter::v1::Utf8String pluginType() const override
    {
        return "example";
    }

    phicore::adapter::v1::Utf8String displayName() const override
    {
        return "Example";
    }

    std::unique_ptr<phicore::adapter::sdk::AdapterInstance> createInstance(
        const phicore::adapter::v1::ExternalId &externalId) override
    {
        std::cerr << "create instance for externalId=" << externalId << std::endl;
        return std::make_unique<ExampleAdapter>();
    }

    phicore::adapter::v1::ActionResponse onFactoryActionInvoke(
        const phicore::adapter::sdk::AdapterActionInvokeRequest &request) override
    {
        phicore::adapter::v1::ActionResponse response;
        response.id = request.cmdId;
        response.status = phicore::adapter::v1::CmdStatus::Success;
        response.resultType = phicore::adapter::v1::ActionResultType::String;
        response.resultValue = phicore::adapter::v1::Utf8String("factory-ok");
        return response;
    }

    void onBootstrap(const phicore::adapter::sdk::BootstrapRequest &request) override
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
