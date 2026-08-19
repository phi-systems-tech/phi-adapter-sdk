// Wire-protocol contract tests for the sidecar dispatcher, driven through a
// raw frame client (the phi-core side of the socket):
// - request decode into typed payloads (bootstrap, channel invoke variants)
// - result/event serialization shapes
// - default response for unknown commands
// - disconnect on invalid frame headers
#include "phi/adapter/sdk/sidecar.h"
#include "phi/adapter/v1/ipc_command.h"
#include "test_support.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;
using phitest::TestClient;
using phitest::contains;
using Clock = std::chrono::steady_clock;

namespace {

std::string cmd(v1::IpcCommand command)
{
    return std::to_string(v1::toUint16(command));
}

void testBootstrapDecode()
{
    const std::string path = phitest::uniqueSocketPath("boot");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    std::optional<sdk::BootstrapRequest> seen;

    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onBootstrap = [&seen](const sdk::BootstrapRequest &r) { seen = r; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    const std::string request = "{\"command\":" + cmd(v1::IpcCommand::SyncAdapterBootstrap)
        + ",\"cmdId\":7,\"payload\":{"
          "\"adapterId\":42,"
          "\"pluginType\":\"demo\","
          "\"externalId\":\"\","
          "\"staticConfig\":{\"discovery\":[{\"kind\":\"mdns\",\"service\":\"_hue._tcp\"}]}"
          "}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 7, request));

    const auto bootDeadline = Clock::now() + std::chrono::seconds(3);
    while (!seen && Clock::now() < bootDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(seen.has_value());

    CHECK(seen->adapterId == 42);
    CHECK(seen->cmdId == 7);
    CHECK(seen->correlationId == 7);
    CHECK(seen->adapter.pluginType == "demo");
    CHECK(seen->adapter.externalId.empty());
    CHECK_MSG(contains(seen->staticConfigJson, "_hue._tcp"),
              "staticConfigJson=%s", seen->staticConfigJson.c_str());

    dispatcher.stop();
}

void testChannelInvokeDecodeAndResult()
{
    const std::string path = phitest::uniqueSocketPath("invoke");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    std::optional<sdk::ChannelInvokeRequest> seen;

    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onChannelInvoke = [&seen](const sdk::ChannelInvokeRequest &r) { seen = r; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    auto poll = [&dispatcher](const std::function<bool()> &pred) {
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        while (!pred() && Clock::now() < deadline)
            dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
        return pred();
    };
    REQUIRE(poll([&connected]() { return connected; }));

    // Scalar double value.
    std::string request = "{\"command\":" + cmd(v1::IpcCommand::CmdChannelInvoke)
        + ",\"cmdId\":11,\"payload\":{"
          "\"externalId\":\"inst-1\","
          "\"deviceExternalId\":\"dev-9\","
          "\"channelExternalId\":\"ch-2\","
          "\"value\":42.5}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 11, request));
    REQUIRE(poll([&seen]() { return seen.has_value(); }));
    CHECK(seen->cmdId == 11);
    CHECK(seen->externalId == "inst-1");
    CHECK(seen->deviceExternalId == "dev-9");
    CHECK(seen->channelExternalId == "ch-2");
    CHECK(seen->hasScalarValue);
    if (const double *d = std::get_if<double>(&seen->value))
        CHECK(*d > 42.4 && *d < 42.6);
    else
        CHECK_MSG(false, "value is not double");

    // Bool value.
    seen.reset();
    request = "{\"command\":" + cmd(v1::IpcCommand::CmdChannelInvoke)
        + ",\"cmdId\":12,\"payload\":{\"externalId\":\"inst-1\","
          "\"deviceExternalId\":\"dev-9\",\"channelExternalId\":\"ch-2\",\"value\":true}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 12, request));
    REQUIRE(poll([&seen]() { return seen.has_value(); }));
    CHECK(seen->hasScalarValue);
    if (const bool *b = std::get_if<bool>(&seen->value))
        CHECK(*b == true);
    else
        CHECK_MSG(false, "value is not bool");

    // String value with a supported escape.
    seen.reset();
    request = "{\"command\":" + cmd(v1::IpcCommand::CmdChannelInvoke)
        + ",\"cmdId\":13,\"payload\":{\"externalId\":\"inst-1\","
          "\"deviceExternalId\":\"dev-9\",\"channelExternalId\":\"ch-2\",\"value\":\"a\\\"b\"}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 13, request));
    REQUIRE(poll([&seen]() { return seen.has_value(); }));
    CHECK(seen->hasScalarValue);
    if (const auto *s = std::get_if<v1::Utf8String>(&seen->value))
        CHECK(*s == "a\"b");
    else
        CHECK_MSG(false, "value is not string");

    // Non-scalar object value: raw JSON token preserved, no scalar flag.
    seen.reset();
    request = "{\"command\":" + cmd(v1::IpcCommand::CmdChannelInvoke)
        + ",\"cmdId\":14,\"payload\":{\"externalId\":\"inst-1\","
          "\"deviceExternalId\":\"dev-9\",\"channelExternalId\":\"ch-2\","
          "\"value\":{\"r\":1,\"g\":0,\"b\":0}}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 14, request));
    REQUIRE(poll([&seen]() { return seen.has_value(); }));
    CHECK(!seen->hasScalarValue);
    CHECK_MSG(contains(seen->valueJson, "\"r\":1"), "valueJson=%s", seen->valueJson.c_str());

    // Result serialization back to the client.
    v1::CmdResponse response;
    response.id = 14;
    response.status = v1::CmdStatus::Success;
    response.finalValue = static_cast<std::int64_t>(1);
    CHECK(dispatcher.sendCmdResult(response, nullptr));
    dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr); // flush

    v1::FrameHeader header{};
    std::string payload;
    REQUIRE(client.readFrame(2000, &header, &payload));
    CHECK(v1::isValidFrameHeader(header));
    CHECK(v1::messageType(header) == v1::MessageType::Response);
    CHECK(header.correlationId == 14);
    CHECK_MSG(contains(payload, "\"command\":" + cmd(v1::IpcCommand::ResultCmd)),
              "payload=%s", payload.c_str());
    CHECK(contains(payload, "\"cmdId\":14"));
    CHECK(contains(payload, "\"status\":0"));
    CHECK(contains(payload, "\"finalValue\":1"));
    CHECK(contains(payload, "\"tsMs\":"));

    dispatcher.stop();
}

void testUnknownCommandDefaultResponse()
{
    const std::string path = phitest::uniqueSocketPath("unknown");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    // Cmd-range command id that is not defined by the v1 contract.
    const std::string request =
        "{\"command\":752,\"cmdId\":21,\"payload\":{\"externalId\":\"inst-1\"}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 21, request));

    // The dispatcher must answer with a correlated default ResultCmd.
    bool gotResult = false;
    const auto resultDeadline = Clock::now() + std::chrono::seconds(3);
    v1::FrameHeader header{};
    std::string payload;
    while (!gotResult && Clock::now() < resultDeadline) {
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
        if (client.readFrame(10, &header, &payload))
            gotResult = true;
    }
    REQUIRE(gotResult);
    CHECK(header.correlationId == 21);
    CHECK_MSG(contains(payload, "\"command\":" + cmd(v1::IpcCommand::ResultCmd)),
              "payload=%s", payload.c_str());
    CHECK(contains(payload, "\"cmdId\":21"));
    CHECK_MSG(!contains(payload, "\"status\":0"), "unknown command must not succeed: %s",
              payload.c_str());

    dispatcher.stop();
}

void testEventEnvelopeShape()
{
    const std::string path = phitest::uniqueSocketPath("event");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    CHECK(dispatcher.sendChannelStateUpdated("inst-1", "dev-9", "ch-2",
                                             static_cast<std::int64_t>(75), 0, nullptr));
    dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr); // flush

    v1::FrameHeader header{};
    std::string payload;
    REQUIRE(client.readFrame(2000, &header, &payload));
    CHECK(v1::isValidFrameHeader(header));
    CHECK(v1::messageType(header) == v1::MessageType::Event);
    CHECK_MSG(contains(payload, "\"command\":" + cmd(v1::IpcCommand::EventChannelStateUpdated)),
              "payload=%s", payload.c_str());
    CHECK(contains(payload, "\"externalId\":\"inst-1\""));
    CHECK(contains(payload, "\"deviceExternalId\":\"dev-9\""));
    CHECK(contains(payload, "\"channelExternalId\":\"ch-2\""));
    CHECK(contains(payload, "\"value\":75"));

    dispatcher.stop();
}

void testUnicodeEscapeDecoding()
{
    const std::string path = phitest::uniqueSocketPath("unicode");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    std::optional<sdk::DeviceNameUpdateRequest> seen;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onDeviceNameUpdate = [&seen](const sdk::DeviceNameUpdateRequest &r) { seen = r; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    auto poll = [&dispatcher](const std::function<bool()> &pred) {
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        while (!pred() && Clock::now() < deadline)
            dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
        return pred();
    };
    REQUIRE(poll([&connected]() { return connected; }));

    // BMP escape: U+00FC.
    std::string request = "{\"command\":" + cmd(v1::IpcCommand::CmdDeviceNameUpdate)
        + ",\"cmdId\":31,\"payload\":{\"externalId\":\"inst-1\",\"deviceExternalId\":\"dev-9\","
          "\"name\":\"B\\u00fcro\"}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 31, request));
    REQUIRE(poll([&seen]() { return seen.has_value(); }));
    const std::string expectedBmp = std::string("B") + "\xc3\xbc" + "ro";
    CHECK_MSG(seen->name == expectedBmp, "name=%s", seen->name.c_str());

    // Surrogate pair: U+1F600.
    seen.reset();
    request = "{\"command\":" + cmd(v1::IpcCommand::CmdDeviceNameUpdate)
        + ",\"cmdId\":32,\"payload\":{\"externalId\":\"inst-1\",\"deviceExternalId\":\"dev-9\","
          "\"name\":\"\\ud83d\\ude00\"}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 32, request));
    REQUIRE(poll([&seen]() { return seen.has_value(); }));
    const std::string expectedEmoji = "\xf0\x9f\x98\x80";
    CHECK_MSG(seen->name == expectedEmoji, "name bytes=%zu", seen->name.size());

    dispatcher.stop();
}

void testFrameTypeCommandMismatchRejected()
{
    const std::string path = phitest::uniqueSocketPath("typemix");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    bool protocolError = false;
    bool dispatched = false;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onProtocolError = [&protocolError](const v1::Utf8String &) { protocolError = true; };
    handlers.onChannelInvoke = [&dispatched](const sdk::ChannelInvokeRequest &) { dispatched = true; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    // An adapter->core event command must not arrive inside a Request frame.
    const std::string request = "{\"command\":" + cmd(v1::IpcCommand::EventChannelStateUpdated)
        + ",\"cmdId\":41,\"payload\":{\"externalId\":\"inst-1\"}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 41, request));

    const auto errDeadline = Clock::now() + std::chrono::seconds(2);
    while (!protocolError && Clock::now() < errDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    CHECK_MSG(protocolError, "frame type/command mismatch was not reported");
    CHECK(!dispatched);

    dispatcher.stop();
}

void testClientReplacementFiresHooks()
{
    const std::string path = phitest::uniqueSocketPath("replace");
    sdk::SidecarDispatcher dispatcher(path);
    int connects = 0;
    int disconnects = 0;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connects]() { ++connects; };
    handlers.onDisconnected = [&disconnects]() { ++disconnects; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));

    TestClient first;
    REQUIRE(first.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (connects == 0 && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connects == 1);
    CHECK(disconnects == 0);

    // Reconnect before the old socket's death was observed: the adapter must
    // still see a full disconnect -> connect cycle for the new session.
    TestClient second;
    REQUIRE(second.connectTo(path));
    const auto cycleDeadline = Clock::now() + std::chrono::seconds(3);
    while (connects < 2 && Clock::now() < cycleDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    CHECK_MSG(connects == 2, "connects=%d", connects);
    CHECK_MSG(disconnects == 1, "disconnects=%d", disconnects);

    dispatcher.stop();
}

void testBatchedFramesAndEscapedKeys()
{
    const std::string path = phitest::uniqueSocketPath("batch");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    std::vector<v1::CmdId> seenOrder;
    std::vector<v1::Utf8String> seenDevices;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onDeviceNameUpdate = [&](const sdk::DeviceNameUpdateRequest &r) {
        seenOrder.push_back(r.cmdId);
        seenDevices.push_back(r.deviceExternalId);
    };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    auto poll = [&dispatcher](const std::function<bool()> &pred) {
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        while (!pred() && Clock::now() < deadline)
            dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
        return pred();
    };
    REQUIRE(poll([&connected]() { return connected; }));

    // Three frames in ONE write: the receive buffer must hand out all of them
    // in order from a single read batch (read cursor instead of erase-front).
    std::string wire;
    for (int n = 1; n <= 3; ++n) {
        const std::string body = "{\"command\":" + cmd(v1::IpcCommand::CmdDeviceNameUpdate)
            + ",\"cmdId\":" + std::to_string(50 + n)
            + ",\"payload\":{\"externalId\":\"inst-1\",\"deviceExternalId\":\"dev-"
            + std::to_string(n) + "\",\"name\":\"n\"}}";
        v1::FrameHeader header;
        header.type = static_cast<std::uint8_t>(v1::MessageType::Request);
        header.correlationId = static_cast<std::uint64_t>(50 + n);
        header.payloadSize = static_cast<std::uint32_t>(body.size());
        wire.append(reinterpret_cast<const char *>(&header), sizeof(header));
        wire.append(body);
    }
    REQUIRE(client.sendRaw(wire.data(), wire.size()));
    REQUIRE(poll([&seenOrder]() { return seenOrder.size() >= 3; }));
    CHECK(seenOrder.size() == 3);
    if (seenOrder.size() == 3) {
        CHECK(seenOrder[0] == 51 && seenOrder[1] == 52 && seenOrder[2] == 53);
        CHECK(seenDevices[0] == "dev-1" && seenDevices[2] == "dev-3");
    }

    // A member key written with an escape must still match (owned-key path).
    seenOrder.clear();
    seenDevices.clear();
    const std::string escaped = "{\"command\":" + cmd(v1::IpcCommand::CmdDeviceNameUpdate)
        + ",\"cmdId\":60,\"payload\":{\"externalId\":\"inst-1\","
          "\"device\\u0045xternalId\":\"dev-9\",\"name\":\"n\"}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 60, escaped));
    REQUIRE(poll([&seenOrder]() { return !seenOrder.empty(); }));
    CHECK_MSG(!seenDevices.empty() && seenDevices[0] == "dev-9",
              "escaped key did not resolve: %s",
              seenDevices.empty() ? "(none)" : seenDevices[0].c_str());

    dispatcher.stop();
}

void testHandlerReentrancyIsSafe()
{
    // A handler that pumps its thread's event loop (nested QEventLoop in
    // blocking HTTP/socket helpers) re-enters pollOnce on the poll thread.
    // Frames are dispatched while the runtime lock is held, so without the
    // re-entrancy guard this deadlocks the sidecar.
    const std::string path = phitest::uniqueSocketPath("reentry");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    bool handlerEntered = false;
    bool nestedReturned = false;

    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onChannelInvoke = [&](const sdk::ChannelInvokeRequest &) {
        handlerEntered = true;
        dispatcher.pollOnce(std::chrono::milliseconds(0), nullptr);
        nestedReturned = true;
    };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    const std::string request = "{\"command\":" + cmd(v1::IpcCommand::CmdChannelInvoke)
        + ",\"cmdId\":70,\"payload\":{\"externalId\":\"inst-1\",\"deviceExternalId\":\"dev-9\","
          "\"channelExternalId\":\"ch-2\",\"value\":1}}";
    REQUIRE(client.sendFrame(v1::MessageType::Request, 70, request));

    const auto dispatchDeadline = Clock::now() + std::chrono::seconds(3);
    while (!nestedReturned && Clock::now() < dispatchDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);

    CHECK(handlerEntered);
    CHECK_MSG(nestedReturned, "nested pollOnce did not return (deadlock)");

    dispatcher.stop();
}

void testOversizeFrameLimits()
{
    const std::string path = phitest::uniqueSocketPath("oversize");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    bool disconnected = false;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onDisconnected = [&disconnected]() { disconnected = true; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    // Outbound: senders must refuse frames above kMaxPayloadSize locally.
    const v1::Utf8String big(v1::kMaxPayloadSize + 1024, 'x');
    v1::Utf8String sendErr;
    CHECK(!dispatcher.sendAdapterMetaUpdated("inst-1", "{\"blob\":\"" + big + "\"}", &sendErr));
    CHECK_MSG(contains(sendErr, "kMaxPayloadSize"), "err=%s", sendErr.c_str());

    // A frame at the limit is still fine (no disconnect, no error).
    v1::Utf8String okErr;
    CHECK(dispatcher.sendAdapterMetaUpdated("inst-1", "{}", &okErr));
    dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    v1::FrameHeader okHeader{};
    std::string okPayload;
    CHECK(client.readFrame(2000, &okHeader, &okPayload));

    // Inbound: a declared payloadSize above the limit is a protocol violation.
    v1::FrameHeader header;
    header.type = static_cast<std::uint8_t>(v1::MessageType::Request);
    header.correlationId = 99;
    header.payloadSize = v1::kMaxPayloadSize + 1;
    CHECK(client.sendRaw(&header, sizeof(header)));

    const auto dropDeadline = Clock::now() + std::chrono::seconds(3);
    while (!disconnected && Clock::now() < dropDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    CHECK_MSG(disconnected, "server did not drop client on oversize declared payload");
    CHECK(client.waitForEof(2000));

    dispatcher.stop();
}

void testInvalidFrameHeaderDisconnects()
{
    const std::string path = phitest::uniqueSocketPath("badmagic");
    sdk::SidecarDispatcher dispatcher(path);
    TestClient client;
    bool connected = false;
    bool disconnected = false;
    sdk::SidecarHandlers handlers;
    handlers.onConnected = [&connected]() { connected = true; };
    handlers.onDisconnected = [&disconnected]() { disconnected = true; };
    dispatcher.setHandlers(std::move(handlers));
    v1::Utf8String err;
    REQUIRE(dispatcher.start(&err));
    REQUIRE(client.connectTo(path));
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!connected && Clock::now() < deadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    REQUIRE(connected);

    const char garbage[32] = {'X', 'X', 'X', 'X', 1, 0, 3, 0};
    REQUIRE(client.sendRaw(garbage, sizeof(garbage)));

    const auto dropDeadline = Clock::now() + std::chrono::seconds(3);
    while (!disconnected && Clock::now() < dropDeadline)
        dispatcher.pollOnce(std::chrono::milliseconds(10), nullptr);
    CHECK_MSG(disconnected, "server did not drop client on invalid magic");
    CHECK(client.waitForEof(2000));

    dispatcher.stop();
}

} // namespace

int main()
{
    testBootstrapDecode();
    testChannelInvokeDecodeAndResult();
    testUnknownCommandDefaultResponse();
    testEventEnvelopeShape();
    testOversizeFrameLimits();
    testInvalidFrameHeaderDisconnects();
    testUnicodeEscapeDecoding();
    testFrameTypeCommandMismatchRejected();
    testClientReplacementFiresHooks();
    testBatchedFramesAndEscapedKeys();
    testHandlerReentrancyIsSafe();

    if (phitest::g_failures == 0) {
        std::printf("protocol_tests: all passed\n");
        return 0;
    }
    std::printf("protocol_tests: %d failure(s)\n", phitest::g_failures);
    return 1;
}
