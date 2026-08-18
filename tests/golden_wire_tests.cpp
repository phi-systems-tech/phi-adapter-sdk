// Golden-wire contract tests for the sidecar IPC.
//
// Outbound (adapter -> core): every dispatcher send* payload is serialized
// from fixed inputs and compared byte-exactly against a checked-in golden
// file (tests/golden/out/<name>.json). Any change to the wire envelope fails
// loudly here first — intentional changes are made by updating the goldens:
//
//     PHI_GOLDEN_UPDATE=1 ./sdk_golden_wire_tests   # regenerate out/ files
//
// Inbound (core -> adapter): tests/golden/in/<name>.json are hand-authored
// canonical request frames exactly as phi-core sends them. They are decoded
// through the dispatcher and the resulting typed payloads are asserted, so
// parser drift against the documented request shapes is caught mechanically.
#include "phi/adapter/sdk/sidecar.h"
#include "phi/adapter/v1/ipc_command.h"
#include "test_support.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;
using phitest::TestClient;
using Clock = std::chrono::steady_clock;

#ifndef PHI_GOLDEN_DIR
#error "PHI_GOLDEN_DIR must be defined by the build system"
#endif

namespace {

constexpr std::int64_t kFixedTsMs = 1755500000000;

bool readFileText(const std::string &path, std::string *out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    *out = buffer.str();
    // Tolerate a single trailing newline from editors/POSIX text files.
    if (!out->empty() && out->back() == '\n')
        out->pop_back();
    return true;
}

bool writeFileText(const std::string &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << text << '\n';
    return out.good();
}

// ---------------------------------------------------------------------------
// Fixed fixtures
// ---------------------------------------------------------------------------

v1::Device fixtureDevice()
{
    v1::Device device;
    device.name = "Living Room Light \"Süd\"";
    device.deviceClass = v1::DeviceClass::Light;
    device.flags = v1::DeviceFlag::Wireless | v1::DeviceFlag::Battery;
    device.externalId = "dev-9";
    device.manufacturer = "Acme";
    device.firmware = "1.2.3";
    device.model = "LTG-002";
    device.metaJson = "{\"vendorId\":7}";
    v1::DeviceEffectDescriptor effect;
    effect.effect = v1::DeviceEffect::ColorLoop;
    effect.id = "vendor-loop";
    effect.label = "Color Loop";
    effect.description = "Cycles colors";
    effect.requiresParams = false;
    device.effects.push_back(effect);
    return device;
}

v1::Channel fixtureChannel()
{
    v1::Channel channel;
    channel.name = "Brightness";
    channel.externalId = "ch-2";
    channel.kind = v1::ChannelKind::Brightness;
    channel.dataType = v1::ChannelDataType::Int;
    channel.flags = v1::kChannelFlagDefaultWrite;
    channel.unit = "%";
    channel.minValue = 0.0;
    channel.maxValue = 100.0;
    channel.stepValue = 1.0;
    channel.metaJson = "{}";
    v1::AdapterConfigOption choice;
    choice.value = "auto";
    choice.label = "Automatic";
    channel.choices.push_back(choice);
    channel.lastValue = static_cast<std::int64_t>(75);
    channel.lastUpdateMs = kFixedTsMs;
    channel.hasValue = true;
    return channel;
}

v1::Room fixtureRoom()
{
    v1::Room room;
    room.externalId = "room-1";
    room.name = "Living Room";
    room.zone = "Ground";
    room.deviceExternalIds = {"dev-9", "dev-10"};
    room.metaJson = "{}";
    return room;
}

v1::Group fixtureGroup()
{
    v1::Group group;
    group.externalId = "grp-1";
    group.name = "All Lights";
    group.zone = "Ground";
    group.deviceExternalIds = {"dev-9"};
    group.metaJson = "{}";
    return group;
}

v1::Scene fixtureScene()
{
    v1::Scene scene;
    scene.externalId = "scene-1";
    scene.name = "Relax";
    scene.description = "Evening scene";
    scene.scopeExternalId = "grp-1";
    scene.scopeType = "group";
    scene.avatarColor = "#ff8800";
    scene.presetTag = "relax";
    scene.state = v1::SceneState::Inactive;
    scene.flags = v1::SceneFlag::OriginAdapter | v1::SceneFlag::SupportsDynamic;
    scene.metaJson = "{}";
    return scene;
}

sdk::AdapterDescriptor fixtureDescriptor()
{
    sdk::AdapterDescriptor descriptor;
    descriptor.pluginType = "demo";
    descriptor.displayName = "Demo Adapter";
    descriptor.description = "Contract fixture";
    descriptor.apiVersion = "1.0";
    descriptor.iconSvg = "<svg/>";
    descriptor.timeoutMs = 10000;
    descriptor.maxInstances = 2;
    descriptor.capabilities.required = v1::AdapterRequirement::Host;
    descriptor.capabilities.optional = v1::AdapterRequirement::Port;
    descriptor.capabilities.flags = v1::AdapterFlag::SupportsDiscovery;
    v1::AdapterActionDescriptor action;
    action.id = "probe";
    action.label = "Probe";
    action.description = "Connectivity probe";
    action.hasForm = false;
    action.danger = false;
    action.cooldownMs = 0;
    descriptor.capabilities.factoryActions.push_back(action);
    descriptor.configSchemaJson = "{\"fields\":[]}";
    return descriptor;
}

// ---------------------------------------------------------------------------
// Test driver
// ---------------------------------------------------------------------------

struct OutboundCase {
    const char *name;
    v1::MessageType expectedType;
    std::function<bool(sdk::SidecarDispatcher &)> send;
};

struct InboundCapture {
    std::optional<sdk::BootstrapRequest> bootstrap;
    std::optional<sdk::ConfigChangedRequest> configChanged;
    std::optional<sdk::InstanceRemovedRequest> instanceRemoved;
    std::optional<sdk::ChannelInvokeRequest> channelInvoke;
    std::optional<sdk::AdapterActionInvokeRequest> actionInvoke;
    std::optional<sdk::DeviceNameUpdateRequest> nameUpdate;
    std::optional<sdk::DeviceEffectInvokeRequest> effectInvoke;
    std::optional<sdk::SceneInvokeRequest> sceneInvoke;
    std::optional<sdk::AdaptersStreamStartRequest> streamStart;
    std::optional<sdk::AdaptersStreamStopRequest> streamStop;

    void clear() { *this = InboundCapture{}; }
    bool any() const
    {
        return bootstrap || configChanged || instanceRemoved || channelInvoke || actionInvoke
            || nameUpdate || effectInvoke || sceneInvoke || streamStart || streamStop;
    }
};

struct InboundCase {
    const char *name;
    std::uint64_t correlationId;
    std::function<void(const InboundCapture &)> verify;
};

void runOutboundCases(sdk::SidecarDispatcher &dispatcher, TestClient &client, bool updateMode)
{
    const std::string goldenDir = std::string(PHI_GOLDEN_DIR) + "/out/";
    const std::vector<OutboundCase> cases = {
        {"cmd_result", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             v1::CmdResponse r;
             r.id = 11;
             r.status = v1::CmdStatus::Success;
             r.finalValue = static_cast<std::int64_t>(1);
             r.tsMs = kFixedTsMs;
             return d.sendCmdResult(r, nullptr);
         }},
        {"cmd_result_error", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             v1::CmdResponse r;
             r.id = 12;
             r.status = v1::CmdStatus::InvalidArgument;
             r.error = "Missing required key '%1'";
             r.errorParams = {v1::Utf8String("iscpPort")};
             r.errorContext = "adapter.cmd";
             r.tsMs = kFixedTsMs;
             return d.sendCmdResult(r, nullptr);
         }},
        {"action_result", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             v1::ActionResponse r;
             r.id = 13;
             r.status = v1::CmdStatus::Success;
             r.resultType = v1::ActionResultType::Boolean;
             r.resultValue = true;
             r.formValuesJson = "{\"host\":\"bridge.local\"}";
             r.fieldChoicesJson = "{\"port\":[{\"value\":\"80\",\"label\":\"HTTP\"}]}";
             r.reloadLayout = true;
             r.tsMs = kFixedTsMs;
             return d.sendActionResult(r, nullptr);
         }},
        {"connection_state_changed", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendConnectionStateChanged("inst-1", true, nullptr);
         }},
        {"log_event", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             sdk::LogEntry entry;
             entry.level = sdk::LogLevel::Info;
             entry.category = sdk::LogCategory::Network;
             entry.message = "Connected to %1";
             entry.params = {v1::Utf8String("bridge.local")};
             entry.ctx = "demo.connect";
             entry.fieldsJson = "{\"attempt\":1}";
             entry.tsMs = kFixedTsMs;
             return d.sendLog("inst-1", "demo", entry, nullptr);
         }},
        {"error_incident", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendError("inst-1", "demo", sdk::LogCategory::Network,
                                "Connection lost", {}, "demo.disconnect", "{}", kFixedTsMs,
                                nullptr);
         }},
        {"adapter_meta_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendAdapterMetaUpdated("inst-1", "{\"bridgeModel\":\"BSB002\"}", nullptr);
         }},
        {"factory_descriptor_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendAdapterDescriptorUpdated("", fixtureDescriptor(), nullptr);
         }},
        {"channel_state_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendChannelStateUpdated("inst-1", "dev-9", "ch-2",
                                              static_cast<std::int64_t>(75), kFixedTsMs, nullptr);
         }},
        {"channel_color_state_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendChannelColorStateUpdated("inst-1", "dev-9", "ch-3", 1.0, 0.5, 0.25,
                                                   kFixedTsMs, nullptr);
         }},
        {"device_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendDeviceUpdated("inst-1", fixtureDevice(), {fixtureChannel()}, nullptr);
         }},
        {"device_removed", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendDeviceRemoved("inst-1", "dev-9", nullptr);
         }},
        {"channel_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendChannelUpdated("inst-1", "dev-9", fixtureChannel(), nullptr);
         }},
        {"room_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendRoomUpdated("inst-1", fixtureRoom(), nullptr);
         }},
        {"room_removed", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendRoomRemoved("inst-1", "room-1", nullptr);
         }},
        {"group_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendGroupUpdated("inst-1", fixtureGroup(), nullptr);
         }},
        {"group_removed", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendGroupRemoved("inst-1", "grp-1", nullptr);
         }},
        {"scene_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendSceneUpdated("inst-1", fixtureScene(), nullptr);
         }},
        {"scene_removed", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendSceneRemoved("inst-1", "scene-1", nullptr);
         }},
        {"stream_open", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendStreamOpen("inst-1", "stream-5", "cmd.stream.start", "adapter.log",
                                     "application/json", "", "{\"tail\":100}", nullptr);
         }},
        {"stream_data", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendStreamData("inst-1", "stream-5", "cmd.stream.start", 3,
                                     "{\"line\":\"hello\"}", kFixedTsMs, nullptr);
         }},
        {"stream_error", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendStreamError("inst-1", "stream-5", "cmd.stream.start",
                                      "Source vanished", "sourceGone", {}, "demo.stream",
                                      nullptr);
         }},
        {"stream_end", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendStreamEnd("inst-1", "stream-5", "cmd.stream.start", "completed",
                                    nullptr);
         }},
    };

    int updated = 0;
    for (const OutboundCase &testCase : cases) {
        CHECK_MSG(testCase.send(dispatcher), "%s: send failed", testCase.name);
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr); // flush

        v1::FrameHeader header{};
        std::string payload;
        if (!client.readFrame(2000, &header, &payload)) {
            CHECK_MSG(false, "%s: no frame received", testCase.name);
            continue;
        }
        CHECK_MSG(v1::messageType(header) == testCase.expectedType,
                  "%s: frame type %d", testCase.name, static_cast<int>(header.type));

        const std::string goldenPath = goldenDir + testCase.name + ".json";
        if (updateMode) {
            if (writeFileText(goldenPath, payload))
                ++updated;
            else
                CHECK_MSG(false, "%s: cannot write golden file", testCase.name);
            continue;
        }

        std::string expected;
        if (!readFileText(goldenPath, &expected)) {
            CHECK_MSG(false, "%s: missing golden file %s (run with PHI_GOLDEN_UPDATE=1)",
                      testCase.name, goldenPath.c_str());
            continue;
        }
        if (payload != expected) {
            CHECK_MSG(false, "%s: wire payload drifted from golden", testCase.name);
            std::printf("  expected: %s\n  actual:   %s\n", expected.c_str(), payload.c_str());
        }
    }
    if (updateMode)
        std::printf("golden update: wrote %d outbound fixtures to %sout/\n", updated,
                    (std::string(PHI_GOLDEN_DIR) + "/").c_str());
}

void runInboundCases(sdk::SidecarDispatcher &dispatcher, TestClient &client,
                     InboundCapture &capture)
{
    const std::string goldenDir = std::string(PHI_GOLDEN_DIR) + "/in/";
    const std::vector<InboundCase> cases = {
        {"sync_adapter_bootstrap", 7, [](const InboundCapture &c) {
             REQUIRE(c.bootstrap.has_value());
             CHECK(c.bootstrap->adapterId == 0);
             CHECK(c.bootstrap->cmdId == 7);
             CHECK(c.bootstrap->adapter.pluginType == "demo");
             CHECK(c.bootstrap->adapter.externalId.empty());
             CHECK(phitest::contains(c.bootstrap->staticConfigJson, "_hue._tcp"));
         }},
        {"sync_adapter_config_changed", 8, [](const InboundCapture &c) {
             REQUIRE(c.configChanged.has_value());
             CHECK(c.configChanged->adapterId == 42);
             CHECK(c.configChanged->adapter.externalId == "inst-1");
             CHECK(c.configChanged->adapter.pluginType == "demo");
             CHECK(c.configChanged->adapter.name == "Bridge");
             CHECK(c.configChanged->adapter.host == "bridge.local");
             CHECK(c.configChanged->adapter.ip == "192.168.1.10");
             CHECK(c.configChanged->adapter.port == 443);
             CHECK(c.configChanged->adapter.user == "user");
             CHECK(c.configChanged->adapter.password == "secret");
             CHECK(c.configChanged->adapter.token == "tok");
             CHECK(phitest::contains(c.configChanged->adapter.metaJson, "\"logging\""));
             CHECK(v1::hasFlag(c.configChanged->adapter.flags, v1::AdapterFlag::UseTls));
         }},
        {"sync_adapter_instance_removed", 9, [](const InboundCapture &c) {
             REQUIRE(c.instanceRemoved.has_value());
             CHECK(c.instanceRemoved->adapterId == 42);
             CHECK(c.instanceRemoved->pluginType == "demo");
             CHECK(c.instanceRemoved->externalId == "inst-1");
         }},
        {"cmd_channel_invoke", 11, [](const InboundCapture &c) {
             REQUIRE(c.channelInvoke.has_value());
             CHECK(c.channelInvoke->cmdId == 11);
             CHECK(c.channelInvoke->externalId == "inst-1");
             CHECK(c.channelInvoke->deviceExternalId == "dev-9");
             CHECK(c.channelInvoke->channelExternalId == "ch-2");
             CHECK(c.channelInvoke->hasScalarValue);
             const auto *value = std::get_if<std::int64_t>(&c.channelInvoke->value);
             CHECK(value && *value == 75);
         }},
        {"cmd_adapter_action_invoke", 12, [](const InboundCapture &c) {
             REQUIRE(c.actionInvoke.has_value());
             CHECK(c.actionInvoke->cmdId == 12);
             CHECK(c.actionInvoke->externalId.empty()); // factory scope
             CHECK(c.actionInvoke->actionId == "probe");
             CHECK(phitest::contains(c.actionInvoke->paramsJson, "\"host\""));
         }},
        {"cmd_device_name_update", 13, [](const InboundCapture &c) {
             REQUIRE(c.nameUpdate.has_value());
             CHECK(c.nameUpdate->cmdId == 13);
             CHECK(c.nameUpdate->externalId == "inst-1");
             CHECK(c.nameUpdate->deviceExternalId == "dev-9");
             CHECK(c.nameUpdate->name == "Kitchen Light");
         }},
        {"cmd_device_effect_invoke", 14, [](const InboundCapture &c) {
             REQUIRE(c.effectInvoke.has_value());
             CHECK(c.effectInvoke->cmdId == 14);
             CHECK(c.effectInvoke->deviceExternalId == "dev-9");
             CHECK(c.effectInvoke->effect == v1::DeviceEffect::ColorLoop);
             CHECK(c.effectInvoke->effectId == "vendor-loop");
         }},
        {"cmd_scene_invoke", 15, [](const InboundCapture &c) {
             REQUIRE(c.sceneInvoke.has_value());
             CHECK(c.sceneInvoke->cmdId == 15);
             CHECK(c.sceneInvoke->sceneExternalId == "scene-1");
             CHECK(c.sceneInvoke->groupExternalId == "grp-1");
             CHECK(c.sceneInvoke->action == "activate");
         }},
        {"cmd_adapters_stream_start", 16, [](const InboundCapture &c) {
             REQUIRE(c.streamStart.has_value());
             CHECK(c.streamStart->cmdId == 16);
             CHECK(c.streamStart->externalId == "inst-1");
             CHECK(c.streamStart->streamId == "stream-5");
             CHECK(c.streamStart->kind == "adapter.log");
             CHECK(phitest::contains(c.streamStart->paramsJson, "\"tail\""));
         }},
        {"cmd_adapters_stream_stop", 17, [](const InboundCapture &c) {
             REQUIRE(c.streamStop.has_value());
             CHECK(c.streamStop->cmdId == 17);
             CHECK(c.streamStop->externalId == "inst-1");
             CHECK(c.streamStop->streamId == "stream-5");
         }},
    };

    for (const InboundCase &testCase : cases) {
        std::string request;
        if (!readFileText(goldenDir + testCase.name + ".json", &request)) {
            CHECK_MSG(false, "%s: missing inbound fixture", testCase.name);
            continue;
        }
        capture.clear();
        CHECK(client.sendFrame(v1::MessageType::Request, testCase.correlationId, request));
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        while (!capture.any() && Clock::now() < deadline)
            dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
        if (!capture.any()) {
            CHECK_MSG(false, "%s: request was not decoded/dispatched", testCase.name);
            continue;
        }
        testCase.verify(capture);
    }
}

} // namespace

int main()
{
    const bool updateMode = std::getenv("PHI_GOLDEN_UPDATE") != nullptr;

    const std::string path = phitest::uniqueSocketPath("golden");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    InboundCapture capture;

    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onBootstrap = [&capture](const sdk::BootstrapRequest &r) { capture.bootstrap = r; };
    handlers.onConfigChanged = [&capture](const sdk::ConfigChangedRequest &r) { capture.configChanged = r; };
    handlers.onInstanceRemoved = [&capture](const sdk::InstanceRemovedRequest &r) { capture.instanceRemoved = r; };
    handlers.onChannelInvoke = [&capture](const sdk::ChannelInvokeRequest &r) { capture.channelInvoke = r; };
    handlers.onAdapterActionInvoke = [&capture](const sdk::AdapterActionInvokeRequest &r) { capture.actionInvoke = r; };
    handlers.onDeviceNameUpdate = [&capture](const sdk::DeviceNameUpdateRequest &r) { capture.nameUpdate = r; };
    handlers.onDeviceEffectInvoke = [&capture](const sdk::DeviceEffectInvokeRequest &r) { capture.effectInvoke = r; };
    handlers.onSceneInvoke = [&capture](const sdk::SceneInvokeRequest &r) { capture.sceneInvoke = r; };
    handlers.onAdaptersStreamStart = [&capture](const sdk::AdaptersStreamStartRequest &r) { capture.streamStart = r; };
    handlers.onAdaptersStreamStop = [&capture](const sdk::AdaptersStreamStopRequest &r) { capture.streamStop = r; };
    dispatcher.setHandlers(std::move(handlers));

    v1::Utf8String err;
    if (!dispatcher.start(&err)) {
        std::printf("FATAL: dispatcher start failed: %s\n", err.c_str());
        return 1;
    }
    if (!client.connectTo(path)) {
        std::printf("FATAL: client connect failed\n");
        return 1;
    }
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    if (!connected) {
        std::printf("FATAL: client was not accepted\n");
        return 1;
    }

    runOutboundCases(dispatcher, client, updateMode);
    runInboundCases(dispatcher, client, capture);

    dispatcher.stop();

    if (phitest::g_failures == 0) {
        std::printf("golden_wire_tests: all passed%s\n", updateMode ? " (update mode)" : "");
        return 0;
    }
    std::printf("golden_wire_tests: %d failure(s)\n", phitest::g_failures);
    return 1;
}
