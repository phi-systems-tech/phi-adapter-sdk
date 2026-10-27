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
    device.setName("Living Room Light \"Süd\"")
        .setDeviceClass(v1::DeviceClass::Light)
        .setFlags(v1::DeviceFlag::Wireless | v1::DeviceFlag::Battery)
        .setExternalId("dev-9")
        .setManufacturer("Acme")
        .setFirmware("1.2.3")
        .setModel("LTG-002")
        .setMetaJson("{\"vendorId\":7}")
        .setIpv4(" 192.168.18.23 ")
        .setIpv6("FE80:0000::0001")
        .setHost("smartbox.local")
        .setMac("30-C6-F7-18-C7-BF")
        .setBleAddress("30c6f718c7c1");
    v1::DeviceEffectDescriptor effect;
    effect.setEffect(v1::DeviceEffect::ColorLoop)
        .setId("vendor-loop")
        .setLabel("Color Loop")
        .setDescription("Cycles colors")
        .setRequiresParams(false);
    device.addEffect(effect);
    return device;
}

v1::Channel fixtureChannel()
{
    v1::Channel channel;
    channel.setName("Brightness")
        .setExternalId("ch-2")
        .setKind(v1::ChannelKind::Brightness)
        .setDataType(v1::ChannelDataType::Int)
        .setFlags(v1::kChannelFlagDefaultWrite)
        .setUnit("%")
        .setRange(0.0, 100.0, 1.0)
        .setMetaJson("{}")
        .addChoice("auto", "Automatic")
        .setLastValue(static_cast<std::int64_t>(75), kFixedTsMs);
    return channel;
}

v1::Room fixtureRoom()
{
    v1::Room room;
    room.setExternalId("room-1")
        .setName("Living Room")
        .setZone("Ground")
        .setRoomClass(v1::RoomClass::LivingRoom)
        .setDeviceExternalIds({"dev-9", "dev-10"})
        .setMetaJson("{}");
    return room;
}

v1::Group fixtureGroup()
{
    v1::Group group;
    group.setExternalId("grp-1")
        .setName("All Lights")
        .setZone("Ground")
        .setDeviceExternalIds({"dev-9"})
        .setMetaJson("{}");
    return group;
}

v1::Scene fixtureScene()
{
    v1::Scene scene;
    scene.setExternalId("scene-1")
        .setName("Relax")
        .setDescription("Evening scene")
        .setScope("group", "grp-1")
        .setAvatarColor("#ff8800")
        .setPresetTag("relax")
        .setState(v1::SceneState::Inactive)
        .setFlags(v1::SceneFlag::OriginAdapter | v1::SceneFlag::SupportsDynamic)
        .setMetaJson("{}");
    return scene;
}

sdk::AdapterDescriptor fixtureDescriptor()
{
    sdk::AdapterDescriptor descriptor;
    descriptor.setPluginType("demo")
        .setDisplayName("Demo Adapter")
        .setDescription("Contract fixture")
        .setApiVersion("1.0")
        .setIconSvg("<svg/>")
        .setTimeoutMs(10000)
        .setMaxInstances(2);
    descriptor.capabilities()
        .setRequired(v1::AdapterRequirement::Host)
        .setOptional(v1::AdapterRequirement::Port)
        .setFlags(v1::AdapterCapability::SupportsDiscovery)
        .setTopologies(v1::Topology::CloudStandIn | v1::Topology::CloudOnly)
        .setStandInRedirect(v1::StandInRedirect::Gateway)
        .setPairingStyles(v1::PairingStyle::Candidates | v1::PairingStyle::SetupCode)
        .setSetupCodeSchemes(v1::SetupCodeScheme::MatterQr)
        .addFactoryAction(v1::AdapterActionDescriptor("probe", "Probe", "Connectivity probe")
                              .setPlacement(v1::AdapterActionPlacement::Device)
                              .setTimeoutMs(5000));
    v1::AdapterActionDescriptor pair("pair", "Pair");
    pair.setHasForm(true).setLoadFormOnOpen(true).setSubmitLabel("Pair now").setResultField("deviceId");
    pair.formLayout().setWidth(v1::AdapterConfigSize::Narrow);
    pair.setConfirm({"Pair device?", "This puts the bridge into pairing mode.", "Pair", "Cancel"});
    descriptor.capabilities().addInstanceAction(pair);

    v1::AdapterConfigSchema schema;
    schema.factory().setTitle("Bridge");
    schema.factory().layout().setColumns(2).setLabelWidth(v1::AdapterConfigSize::Narrow);
    v1::AdapterConfigField host("host", v1::AdapterConfigFieldType::Hostname, "Host");
    host.setFlags(v1::AdapterConfigFieldFlag::Required).addAction("probe", "Probe");
    schema.factory().addField(host);
    v1::AdapterConfigField port("port", v1::AdapterConfigFieldType::Port, "Port");
    port.setDefaultValue(std::int64_t{443});
    port.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    schema.factory().addField(port);
    v1::AdapterConfigField device("deviceId", v1::AdapterConfigFieldType::Select, "Device");
    device.setChoicesFrom("devices").setParentActionId("pair");
    device.layout()
        .setCells(2)
        .setNewRow(true)
        .setLabelPosition(v1::AdapterConfigLabelPosition::Top)
        .setActionPosition(v1::AdapterConfigActionPosition::Below);
    schema.instance().addField(device);
    descriptor.setConfigSchema(schema);
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
    std::optional<sdk::DeviceRemovedRequest> deviceRemoved;
    std::optional<sdk::ChannelInvokeRequest> channelInvoke;
    std::optional<sdk::AdapterActionInvokeRequest> actionInvoke;
    std::optional<sdk::DeviceNameUpdateRequest> nameUpdate;
    std::optional<sdk::EntityNameUpdateRequest> entityNameUpdate;
    std::optional<sdk::DeviceEffectInvokeRequest> effectInvoke;
    std::optional<sdk::MediaPlayerInvokeRequest> mediaPlayerInvoke;
    std::optional<sdk::SceneInvokeRequest> sceneInvoke;
    std::optional<sdk::AdaptersStreamStartRequest> streamStart;
    std::optional<sdk::AdaptersStreamStopRequest> streamStop;
    std::optional<sdk::PairingStartRequest> pairingStart;
    std::optional<sdk::PairCandidateRequest> pairCandidate;
    std::optional<sdk::SetupCodeRequest> setupCode;

    void clear() { *this = InboundCapture{}; }
    bool any() const
    {
        return bootstrap || configChanged || instanceRemoved || deviceRemoved || channelInvoke || actionInvoke
            || nameUpdate || entityNameUpdate || effectInvoke || mediaPlayerInvoke || sceneInvoke || streamStart
            || streamStop || pairingStart || pairCandidate || setupCode;
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
             const v1::CmdResponse r = v1::CmdResponse::success(11, static_cast<std::int64_t>(1)).setTsMs(kFixedTsMs);
             return d.sendCmdResult(r, nullptr);
         }},
        {"cmd_result_error", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             const v1::CmdResponse r = v1::CmdResponse::failure(12, v1::CmdStatus::InvalidArgument,
                                                                "Missing required key '%1'",
                                                                {v1::Utf8String("iscpPort")}, "adapter.cmd")
                                           .setTsMs(kFixedTsMs);
             return d.sendCmdResult(r, nullptr);
         }},
        {"action_result", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             v1::ActionResponse r = v1::ActionResponse::success(13);
             r.setResult(true, v1::ActionResultType::Boolean)
                 .addFormValue("host", v1::ScalarValue{v1::Utf8String("bridge.local")})
                 .addFormValue("devices", v1::ScalarList{v1::Utf8String("a"), v1::Utf8String("b")})
                 .addFormValue("ip", v1::AdapterConfigPerChoiceValues{{"a", v1::Utf8String("10.0.0.2")}})
                 .addFieldChoices("port", {{"80", "HTTP"}})
                 .setReloadLayout(true)
                 .setTsMs(kFixedTsMs);
             return d.sendActionResult(r, nullptr);
         }},
        {"action_result_display", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             const v1::ActionResponse r
                 = v1::ActionResponse::success(14)
                       .setDisplay({"Scan this on your phone", "123456", "otpauth://pair/demo?code=123456"})
                       .setTsMs(kFixedTsMs);
             return d.sendActionResult(r, nullptr);
         }},
        {"action_result_run", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             v1::AdapterRunHandle run;
             run.setRunId("run-1")
                 .setStreamKind("adapter.log")
                 .setStreamParams({{"tail", static_cast<std::int64_t>(100)}})
                 .setAbortActionId("abort")
                 .setAbortParams({{"runId", v1::Utf8String("run-1")}})
                 .setBatch(true);
             const v1::ActionResponse r = v1::ActionResponse::success(15).setRun(run).setTsMs(kFixedTsMs);
             return d.sendActionResult(r, nullptr);
         }},
        {"action_result_data", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             const v1::ActionResponse r
                 = v1::ActionResponse::success(16).setData("{\"count\":3,\"items\":[\"a\",\"b\"]}").setTsMs(kFixedTsMs);
             return d.sendActionResult(r, nullptr);
         }},
        {"action_result_data_blank_is_null", v1::MessageType::Response, [](sdk::SidecarDispatcher &d) {
             const v1::ActionResponse r
                 = v1::ActionResponse::success(17).setResultType(v1::ActionResultType::Data).setTsMs(kFixedTsMs);
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
             const v1::AdapterFormValues patch = {
                 {"bridgeModel", v1::ScalarValue{v1::Utf8String("BSB002")}},
             };
             return d.sendAdapterMetaUpdated("inst-1", patch, nullptr);
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
        {"topology_state", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendTopologyState("inst-1", false, "The box is not routed through phi", nullptr);
         }},
        {"device_candidate_updated", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             v1::DeviceCandidate candidate;
             candidate.setId("matter-3840")
                 .setName("Smart Plug")
                 .setDeviceClass(v1::DeviceClass::Plug)
                 .setManufacturer("Acme")
                 .setModel("SP-1")
                 .setRadio(v1::Radio::Ble)
                 .setRssi(-58)
                 .setRoomExternalId("room-4")
                 .setNeeds(v1::PairingNeed::SetupCode)
                 .setSetupCodeScheme(v1::SetupCodeScheme::MatterQr)
                 .setInstruction("Scan the code on the plug")
                 .setExpiresInMs(30000)
                 .setMac("D8-C8-0C-02-22-44")
                 .setIpv4("192.168.18.70");
             return d.sendDeviceCandidateUpdated("inst-1", candidate, nullptr);
         }},
        {"device_candidate_removed", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendDeviceCandidateRemoved("inst-1", "matter-3840", nullptr);
         }},
        {"pairing_state", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendPairingState("inst-1", true, 117, nullptr);
         }},
        {"pairing_progress", v1::MessageType::Event, [](sdk::SidecarDispatcher &d) {
             return d.sendPairingProgress("inst-1", "pair-7", v1::PairingStage::Done, 100, "Commissioned",
                                          "node-12", nullptr);
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
             CHECK(c.bootstrap->adapterId() == 0);
             CHECK(c.bootstrap->cmdId() == 7);
             CHECK(c.bootstrap->adapter().pluginType() == "demo");
             CHECK(c.bootstrap->adapter().externalId().empty());
             CHECK(phitest::contains(c.bootstrap->staticConfigJson(), "_hue._tcp"));
         }},
        {"sync_adapter_config_changed", 8, [](const InboundCapture &c) {
             REQUIRE(c.configChanged.has_value());
             CHECK(c.configChanged->adapterId() == 42);
             CHECK(c.configChanged->adapter().externalId() == "inst-1");
             CHECK(c.configChanged->adapter().pluginType() == "demo");
             CHECK(c.configChanged->adapter().name() == "Bridge");
             CHECK(c.configChanged->adapter().host() == "bridge.local");
             CHECK(c.configChanged->adapter().ip() == "192.168.1.10");
             CHECK(c.configChanged->adapter().port() == 443);
             CHECK(c.configChanged->adapter().user() == "user");
             CHECK(c.configChanged->adapter().password() == "secret");
             CHECK(c.configChanged->adapter().token() == "tok");
             CHECK(phitest::contains(c.configChanged->adapter().metaJson(), "\"logging\""));
             CHECK(v1::hasFlag(c.configChanged->adapter().options(), v1::AdapterOption::UseTls));
         }},
        {"sync_adapter_instance_removed", 9, [](const InboundCapture &c) {
             REQUIRE(c.instanceRemoved.has_value());
             CHECK(c.instanceRemoved->adapterId() == 42);
             CHECK(c.instanceRemoved->pluginType() == "demo");
             CHECK(c.instanceRemoved->externalId() == "inst-1");
         }},
        {"sync_device_removed", 10, [](const InboundCapture &c) {
             REQUIRE(c.deviceRemoved.has_value());
             CHECK(c.deviceRemoved->externalId() == "inst-1");
             CHECK(c.deviceRemoved->deviceExternalId() == "d8:c8:0c:02:22:44");
         }},
        {"cmd_channel_invoke", 11, [](const InboundCapture &c) {
             REQUIRE(c.channelInvoke.has_value());
             CHECK(c.channelInvoke->cmdId() == 11);
             CHECK(c.channelInvoke->externalId() == "inst-1");
             CHECK(c.channelInvoke->deviceExternalId() == "dev-9");
             CHECK(c.channelInvoke->channelExternalId() == "ch-2");
             CHECK(c.channelInvoke->hasScalarValue());
             const auto *value = std::get_if<std::int64_t>(&c.channelInvoke->value());
             CHECK(value && *value == 75);
         }},
        {"cmd_adapter_action_invoke", 12, [](const InboundCapture &c) {
             REQUIRE(c.actionInvoke.has_value());
             CHECK(c.actionInvoke->cmdId() == 12);
             CHECK(c.actionInvoke->externalId().empty()); // factory scope
             CHECK(c.actionInvoke->actionId() == "probe");
             CHECK(phitest::contains(c.actionInvoke->paramsJson(), "\"host\""));
         }},
        {"cmd_device_name_update", 13, [](const InboundCapture &c) {
             REQUIRE(c.nameUpdate.has_value());
             CHECK(c.nameUpdate->cmdId() == 13);
             CHECK(c.nameUpdate->externalId() == "inst-1");
             CHECK(c.nameUpdate->deviceExternalId() == "dev-9");
             CHECK(c.nameUpdate->name() == "Kitchen Light");
         }},
        {"cmd_entity_name_update", 18, [](const InboundCapture &c) {
             REQUIRE(c.entityNameUpdate.has_value());
             CHECK(c.entityNameUpdate->cmdId() == 18);
             CHECK(c.entityNameUpdate->externalId() == "inst-1");
             CHECK(c.entityNameUpdate->kind() == v1::NamedEntityKind::Room);
             CHECK(c.entityNameUpdate->entityExternalId() == "room-4");
             CHECK(c.entityNameUpdate->name() == "Wohnzimmer");
         }},
        {"cmd_device_effect_invoke", 14, [](const InboundCapture &c) {
             REQUIRE(c.effectInvoke.has_value());
             CHECK(c.effectInvoke->cmdId() == 14);
             CHECK(c.effectInvoke->deviceExternalId() == "dev-9");
             CHECK(c.effectInvoke->effect() == v1::DeviceEffect::ColorLoop);
             CHECK(c.effectInvoke->effectId() == "vendor-loop");
         }},
        {"cmd_media_player_invoke", 19, [](const InboundCapture &c) {
             REQUIRE(c.mediaPlayerInvoke.has_value());
             CHECK(c.mediaPlayerInvoke->cmdId() == 19);
             CHECK(c.mediaPlayerInvoke->externalId() == "inst-1");
             CHECK(c.mediaPlayerInvoke->deviceExternalId() == "player-2");
             CHECK(c.mediaPlayerInvoke->action() == v1::MediaPlayerAction::PlayMedia);
             CHECK(c.mediaPlayerInvoke->mediaId() == "plex://server/library/metadata/42");
             CHECK(c.mediaPlayerInvoke->mediaType() == "movie");
             CHECK(c.mediaPlayerInvoke->mimeType() == "video/mp4");
             CHECK(c.mediaPlayerInvoke->positionMs() == 12000);
             CHECK(c.mediaPlayerInvoke->enqueue() == v1::MediaPlayerEnqueue::Replace);
             CHECK(phitest::contains(c.mediaPlayerInvoke->metadataJson(), "Example Movie"));
         }},
        {"cmd_scene_invoke", 15, [](const InboundCapture &c) {
             REQUIRE(c.sceneInvoke.has_value());
             CHECK(c.sceneInvoke->cmdId() == 15);
             CHECK(c.sceneInvoke->sceneExternalId() == "scene-1");
             CHECK(c.sceneInvoke->groupExternalId() == "grp-1");
             CHECK(c.sceneInvoke->action() == "activate");
         }},
        {"cmd_adapters_stream_start", 16, [](const InboundCapture &c) {
             REQUIRE(c.streamStart.has_value());
             CHECK(c.streamStart->cmdId() == 16);
             CHECK(c.streamStart->externalId() == "inst-1");
             CHECK(c.streamStart->streamId() == "stream-5");
             CHECK(c.streamStart->kind() == "adapter.log");
             CHECK(phitest::contains(c.streamStart->paramsJson(), "\"tail\""));
         }},
        {"cmd_pairing_start", 20, [](const InboundCapture &c) {
             REQUIRE(c.pairingStart.has_value());
             CHECK(c.pairingStart->cmdId() == 20);
             CHECK(c.pairingStart->externalId() == "inst-1");
             CHECK(c.pairingStart->durationS() == 180);
         }},
        {"cmd_pair_candidate", 21, [](const InboundCapture &c) {
             REQUIRE(c.pairCandidate.has_value());
             CHECK(c.pairCandidate->cmdId() == 21);
             CHECK(c.pairCandidate->pairingId() == "pair-7");
             CHECK(c.pairCandidate->candidateId() == "matter-3840");
             CHECK(c.pairCandidate->setupCode() == "MT:Y.K9042C00KA0648G00");
             CHECK(c.pairCandidate->name() == "Desk Lamp");
             CHECK(c.pairCandidate->wifiSsid() == "phimagic-18C7BF");
             CHECK(c.pairCandidate->wifiPassphrase() == "not-a-real-one");
         }},
        {"cmd_setup_code", 22, [](const InboundCapture &c) {
             REQUIRE(c.setupCode.has_value());
             CHECK(c.setupCode->pairingId() == "pair-8");
             CHECK(c.setupCode->scheme() == v1::SetupCodeScheme::MatterManual);
             CHECK(c.setupCode->code() == "34970112332");
         }},
        {"cmd_adapters_stream_stop", 17, [](const InboundCapture &c) {
             REQUIRE(c.streamStop.has_value());
             CHECK(c.streamStop->cmdId() == 17);
             CHECK(c.streamStop->externalId() == "inst-1");
             CHECK(c.streamStop->streamId() == "stream-5");
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
    // Pinned: the descriptor fixture carries the built-against version, and the
    // fixture must not change with every release.
    sdk::SidecarDispatcher dispatcher(path, "0.0.0-golden");
    TestClient client;
    bool connected = false;
    InboundCapture capture;

    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onBootstrap = [&capture](const sdk::BootstrapRequest &r) { capture.bootstrap = r; };
    handlers.onConfigChanged = [&capture](const sdk::ConfigChangedRequest &r) { capture.configChanged = r; };
    handlers.onInstanceRemoved = [&capture](const sdk::InstanceRemovedRequest &r) { capture.instanceRemoved = r; };
    handlers.onDeviceRemoved = [&capture](const sdk::DeviceRemovedRequest &r) { capture.deviceRemoved = r; };
    handlers.onChannelInvoke = [&capture](const sdk::ChannelInvokeRequest &r) { capture.channelInvoke = r; };
    handlers.onAdapterActionInvoke = [&capture](const sdk::AdapterActionInvokeRequest &r) { capture.actionInvoke = r; };
    handlers.onDeviceNameUpdate = [&capture](const sdk::DeviceNameUpdateRequest &r) { capture.nameUpdate = r; };
    handlers.onEntityNameUpdate = [&capture](const sdk::EntityNameUpdateRequest &r) { capture.entityNameUpdate = r; };
    handlers.onDeviceEffectInvoke = [&capture](const sdk::DeviceEffectInvokeRequest &r) { capture.effectInvoke = r; };
    handlers.onMediaPlayerInvoke = [&capture](const sdk::MediaPlayerInvokeRequest &r) { capture.mediaPlayerInvoke = r; };
    handlers.onSceneInvoke = [&capture](const sdk::SceneInvokeRequest &r) { capture.sceneInvoke = r; };
    handlers.onAdaptersStreamStart = [&capture](const sdk::AdaptersStreamStartRequest &r) { capture.streamStart = r; };
    handlers.onAdaptersStreamStop = [&capture](const sdk::AdaptersStreamStopRequest &r) { capture.streamStop = r; };
    handlers.onPairingStart = [&capture](const sdk::PairingStartRequest &r) { capture.pairingStart = r; };
    handlers.onPairCandidate = [&capture](const sdk::PairCandidateRequest &r) { capture.pairCandidate = r; };
    handlers.onSetupCode = [&capture](const sdk::SetupCodeRequest &r) { capture.setupCode = r; };
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

    // What must never reach the wire: an address the setters could not
    // normalise, and a descriptor without a topology.
    {
        v1::Utf8String refused;
        v1::Device bad = fixtureDevice();
        bad.setMac("30:c6:f7");
        CHECK(!dispatcher.sendDeviceUpdated("inst-1", bad, {}, &refused));
        CHECK(phitest::contains(refused, "not a MAC address"));
        sdk::AdapterDescriptor silent = fixtureDescriptor();
        silent.capabilities().setTopologies(v1::Topology::None);
        CHECK(!dispatcher.sendAdapterDescriptorUpdated("", silent, &refused));
        CHECK(phitest::contains(refused, "no topology"));
        sdk::AdapterDescriptor unrouted = fixtureDescriptor();
        unrouted.capabilities().setStandInRedirect(v1::StandInRedirect::None);
        CHECK(!dispatcher.sendAdapterDescriptorUpdated("", unrouted, &refused));
    }

    dispatcher.stop();

    if (phitest::g_failures == 0) {
        std::printf("golden_wire_tests: all passed%s\n", updateMode ? " (update mode)" : "");
        return 0;
    }
    std::printf("golden_wire_tests: %d failure(s)\n", phitest::g_failures);
    return 1;
}
