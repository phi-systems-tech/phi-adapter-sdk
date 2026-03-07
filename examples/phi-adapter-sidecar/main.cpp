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
namespace phi = phicore::adapter::sdk;

void handleSignal(int)
{
    g_running.store(false);
}

class ExampleAdapter final : public phi::AdapterInstance
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
        PHI_LOG_DEBUG(*this,
                      phi::LogCategory::Lifecycle,
                      "Core connected for %1",
                      "adapter.example.lifecycle.connected",
                      phi::ScalarList{externalId()});
    }

    void onDisconnected() override
    {
        std::cerr << "core disconnected" << std::endl;
    }

    void onProtocolError(const phi::Utf8String &message) override
    {
        std::cerr << "protocol error: " << message << std::endl;
        phi::Utf8String sendErr;
        // `ctx` is translation context, params replace `%1`, `%2`, ...
        // source/module context belongs to structured log fields (SDK mirrors event.error -> event.log).
        sendError("Protocol error for %1: %2",
                  {externalId(), message},
                  "adapter.example.protocol.error",
                  &sendErr);
        if (!sendErr.empty())
            std::cerr << "failed to send adapter error: " << sendErr << std::endl;
    }

    void onConfigChanged(const phi::ConfigChangedRequest &request) override
    {
        std::cerr << "config.changed adapterId=" << request.adapterId
                  << " extId=" << request.adapter.externalId
                  << " ip=" << request.adapter.ip
                  << " port=" << request.adapter.port << std::endl;
    }

    void onChannelInvoke(const phi::ChannelInvokeRequest &request) override
    {
        phi::CmdResponse response;
        response.id = request.cmdId;
        response.status = phi::CmdStatus::Success;
        response.finalValue = request.value;
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "channel invoke extId=" << request.externalId
                  << " device=" << request.deviceExternalId
                  << " channel=" << request.channelExternalId << std::endl;
        phi::Utf8String err;
        if (!sendResult(response, &err)) {
            std::cerr << "failed to send channel result: " << err << std::endl;
        }
    }

    void onAdapterActionInvoke(const phi::AdapterActionInvokeRequest &request) override
    {
        phi::ActionResponse response;
        response.id = request.cmdId;
        response.status = phi::CmdStatus::Success;
        if (request.actionId == "browseHosts") {
            response.resultType = phi::ActionResultType::None;
            response.formValuesJson =
                R"json({"trackedMacs":["1c:90:ff:0b:58:77","26:d2:aa:57:79:46"]})json";
            response.fieldChoicesJson =
                R"json({"trackedMacs":[{"value":"1c:90:ff:0b:58:77","label":"Zigbee (192.168.1.77)"},{"value":"26:d2:aa:57:79:46","label":"Phone (192.168.1.76)"},{"value":"cc:8c:bf:76:0c:54","label":"Heater (192.168.1.26)"}]})json";
            response.reloadLayout = false;
        } else {
            response.resultType = phi::ActionResultType::String;
            response.resultValue = phi::Utf8String("ok");
        }
        response.tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        std::cerr << "adapter action invoke extId=" << request.externalId
                  << " actionId=" << request.actionId << std::endl;
        phi::Utf8String err;
        if (!sendResult(response, &err)) {
            std::cerr << "failed to send action result: " << err << std::endl;
        }
    }
};

class ExampleFactory final : public phi::AdapterFactory
{
protected:
    phi::Utf8String pluginType() const override
    {
        return "example";
    }

    phi::Utf8String displayName() const override
    {
        return "Example";
    }

    std::unique_ptr<phi::AdapterInstance> createInstance(const phi::ExternalId &externalId) override
    {
        std::cerr << "create instance for externalId=" << externalId << std::endl;
        return std::make_unique<ExampleAdapter>();
    }

    void onFactoryActionInvoke(const phi::AdapterActionInvokeRequest &request) override
    {
        phi::ActionResponse response;
        response.id = request.cmdId;
        response.status = phi::CmdStatus::Success;
        response.resultType = phi::ActionResultType::String;
        response.resultValue = phi::Utf8String("factory-ok");
        phi::Utf8String err;
        if (!sendResult(response, &err)) {
            std::cerr << "failed to send factory action result: " << err << std::endl;
        }
    }

    void onBootstrap(const phi::BootstrapRequest &request) override
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
    const phi::Utf8String socketPath = (argc > 1)
        ? argv[1]
        : (envSocketPath ? envSocketPath : phi::Utf8String("/tmp/phi-adapter-example.sock"));

    ExampleFactory factory;
    phi::SidecarHost host(socketPath, factory);

    phi::Utf8String error;
    if (!host.start(&error)) {
        std::cerr << "host start failed: " << error << std::endl;
        return 1;
    }

    std::cerr << "phi adapter sidecar host example listening on " << socketPath << std::endl;
    constexpr std::chrono::milliseconds kHostPollTimeout{16};

    while (g_running.load()) {
        if (!host.pollOnce(kHostPollTimeout, &error)) {
            std::cerr << "host poll failed: " << error << std::endl;
            std::this_thread::sleep_for(kHostPollTimeout);
        }
    }

    host.stop();
    return 0;
}
