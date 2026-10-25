// The entities behind their d-pointers: one pointer wide, deep on copy, empty
// but alive after a move, and the value accessors keep hasValue honest.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/v1/channel.h"
#include "phi/adapter/v1/device.h"
#include "phi/adapter/v1/group.h"
#include "phi/adapter/v1/response.h"
#include "phi/adapter/v1/room.h"
#include "phi/adapter/v1/scene.h"
#include "phi/adapter/sdk/requests.h"
#include "phi/adapter/sdk/sidecar.h"
#include "phi/adapter/net/http_client.h"
#include "phi/adapter/net/http_fetch.h"

#include <chrono>
#include <utility>

namespace v1 = phicore::adapter::v1;
namespace sdk = phicore::adapter::sdk;
namespace net = phicore::adapter::net;

namespace {

void testOnePointerWide()
{
    // The promise the boundary rests on: a later field never changes the size.
    PHI_CHECK(sizeof(v1::Channel) == sizeof(void *));
    PHI_CHECK(sizeof(v1::Device) == sizeof(void *));
    PHI_CHECK(sizeof(v1::DeviceEffectDescriptor) == sizeof(void *));
    PHI_CHECK(sizeof(v1::Room) == sizeof(void *));
    PHI_CHECK(sizeof(v1::Group) == sizeof(void *));
    PHI_CHECK(sizeof(v1::Scene) == sizeof(void *));
}

void testDefaultsReadAsEmpty()
{
    const v1::Channel channel;
    PHI_CHECK(channel.name().empty() && channel.kind() == v1::ChannelKind::Unknown);
    PHI_CHECK(channel.dataType() == v1::ChannelDataType::Unknown && channel.flags() == v1::ChannelFlag::None);
    PHI_CHECK(channel.minValue() == 0.0 && channel.maxValue() == 0.0 && channel.stepValue() == 0.0);
    PHI_CHECK(!channel.hasValue() && channel.lastUpdateMs() == 0 && channel.choices().empty());

    const v1::Device device;
    PHI_CHECK(device.deviceClass() == v1::DeviceClass::Unknown && device.effects().empty());
    PHI_CHECK(device.mediaPlayerFeatures() == v1::MediaPlayerFeature::None);
    const v1::Room room;
    PHI_CHECK(room.roomClass() == v1::RoomClass::Unknown && room.deviceExternalIds().empty());
    const v1::Scene scene;
    PHI_CHECK(scene.state() == v1::SceneState::Unknown && scene.flags() == v1::SceneFlag::None);
}

void testSettersChainAndRead()
{
    v1::Channel channel;
    channel.setExternalId("ch-1").setName("Level").setKind(v1::ChannelKind::Brightness)
        .setDataType(v1::ChannelDataType::Int).setUnit("%").setRange(0, 100, 5).addChoice("a", "A").addChoice("b", "B");
    PHI_CHECK(channel.externalId() == "ch-1" && channel.name() == "Level");
    PHI_CHECK(channel.kind() == v1::ChannelKind::Brightness && channel.unit() == "%");
    PHI_CHECK(channel.minValue() == 0 && channel.maxValue() == 100 && channel.stepValue() == 5);
    PHI_CHECK(channel.choices().size() == 2 && channel.choices()[1].value() == "b" && channel.choices()[1].label() == "B");

    v1::Room room;
    room.setName("Kitchen").addDeviceExternalId("d1").addDeviceExternalId("d2");
    PHI_CHECK(room.deviceExternalIds().size() == 2 && room.deviceExternalIds()[1] == "d2");
    room.setDeviceExternalIds({"x"});
    PHI_CHECK(room.deviceExternalIds().size() == 1 && room.deviceExternalIds()[0] == "x");

    v1::Scene scene;
    scene.setScope("room", "r-1");
    PHI_CHECK(scene.scopeType() == "room" && scene.scopeExternalId() == "r-1");

    v1::Device device;
    device.addEffect(v1::DeviceEffectDescriptor().setEffect(v1::DeviceEffect::Sparkle).setId("blink"));
    PHI_CHECK(device.effects().size() == 1 && device.effects()[0].id() == "blink");
    PHI_CHECK(device.effects()[0].effect() == v1::DeviceEffect::Sparkle);
}

void testTheValueKeepsHasValueHonest()
{
    v1::Channel channel;
    channel.setLastValue(static_cast<std::int64_t>(42), 1000);
    PHI_CHECK(channel.hasValue() && channel.lastUpdateMs() == 1000);
    PHI_CHECK(std::get<std::int64_t>(channel.lastValue()) == 42);
    channel.clearValue();
    PHI_CHECK(!channel.hasValue() && channel.lastUpdateMs() == 0);
    PHI_CHECK(std::holds_alternative<std::monostate>(channel.lastValue()));
}

void testCopiesAreDeep()
{
    v1::Group a;
    a.setName("one").addDeviceExternalId("d1");
    v1::Group b = a;
    b.setName("two").addDeviceExternalId("d2");
    PHI_CHECK(a.name() == "one" && a.deviceExternalIds().size() == 1);
    PHI_CHECK(b.name() == "two" && b.deviceExternalIds().size() == 2);

    v1::Group c;
    c = b;
    c.setName("three");
    PHI_CHECK(b.name() == "two" && c.name() == "three");
    c = c; // self-assignment keeps what it had
    PHI_CHECK(c.name() == "three");
}

void testAMovedFromObjectIsEmptyAndAlive()
{
    v1::Device a;
    a.setName("source").setExternalId("dev-1");
    v1::Device b = std::move(a);
    PHI_CHECK(b.name() == "source" && b.externalId() == "dev-1");
    PHI_CHECK(a.name().empty() && a.externalId().empty()); // NOLINT(bugprone-use-after-move): the point
    PHI_CHECK(a.deviceClass() == v1::DeviceClass::Unknown && a.effects().empty());
    a.setName("again");
    PHI_CHECK(a.name() == "again" && b.name() == "source");

    v1::Device c;
    c = std::move(b);
    PHI_CHECK(c.name() == "source" && b.name().empty()); // NOLINT(bugprone-use-after-move)
}

void testHttpAndTlsTypesFollowTheSameRules()
{
    using net::HttpClient;
    PHI_CHECK(sizeof(HttpClient::Call) == sizeof(void *) && sizeof(HttpClient::Result) == sizeof(void *));
    PHI_CHECK(sizeof(net::Fetch) == sizeof(void *) && sizeof(net::Credentials) == sizeof(void *));
    PHI_CHECK(sizeof(v1::TlsSettings) == sizeof(void *) && sizeof(sdk::SidecarMainOptions) == sizeof(void *));

    HttpClient::Call call("http://bridge.local/api", "PUT");
    call.setBody("{\"on\":true}", "application/json").addHeader("Accept", "application/json").setTimeout(std::chrono::seconds(2));
    call.tls().setEnabled(true).setVerifyHostname(false);
    PHI_CHECK(call.url() == "http://bridge.local/api" && call.method() == "PUT" && call.body() == "{\"on\":true}");
    PHI_CHECK(call.headers().size() == 2 && call.headers()[0].first == "Content-Type");
    PHI_CHECK(call.timeout() == std::chrono::milliseconds(2000));
    PHI_CHECK(call.tls().enabled() && !call.tls().verifyHostname() && call.tls().caFile().empty());
    PHI_CHECK(call.credentials().empty());
    call.setCredentials({"admin", "secret"});
    PHI_CHECK(!call.credentials().empty() && call.credentials().user() == "admin");
    // A second Content-Type replaces the first rather than joining it.
    call.setBody("x", "text/plain");
    PHI_CHECK(call.headers().size() == 2 && call.headers().back().second == "text/plain");

    const HttpClient::Call defaults;
    PHI_CHECK(defaults.method() == "GET" && defaults.timeout() == std::chrono::milliseconds(5000) && !defaults.tls().enabled());
    PHI_CHECK(defaults.tls().verifyHostname());

    HttpClient::Result result;
    result.setOk(true).setStatus(200).setHeaders({{"Content-Type", "text/html"}, {"X-Two", "2"}});
    PHI_CHECK(result.ok() && result.status() == 200 && result.header("content-type") == "text/html");
    PHI_CHECK(result.header("missing").empty() && !result.unauthorized());

    const net::Fetch fetch("https://example.org/");
    PHI_CHECK(fetch.maxRedirects() == 5 && fetch.timeout() == std::chrono::milliseconds(10000));

    sdk::SidecarMainOptions options;
    PHI_CHECK(options.installSignalHandlers() && options.pollTimeout() == std::chrono::milliseconds(1000) && !options.keepRunning());
    options.setKeepRunning([] { return false; }).setInstallSignalHandlers(false);
    PHI_CHECK(options.keepRunning() && !options.keepRunning()() && !options.installSignalHandlers());
}

void testRequestsFollowTheSameRules()
{
    PHI_CHECK(sizeof(sdk::BootstrapRequest) == sizeof(void *) && sizeof(sdk::ChannelInvokeRequest) == sizeof(void *));
    PHI_CHECK(sizeof(sdk::MediaPlayerInvokeRequest) == sizeof(void *) && sizeof(sdk::UnknownRequest) == sizeof(void *));

    sdk::ChannelInvokeRequest request;
    PHI_CHECK(!request.hasScalarValue() && request.cmdId() == 0);
    request.setCmdId(5).setExternalId("inst").setValue(static_cast<std::int64_t>(3));
    PHI_CHECK(request.hasScalarValue() && std::get<std::int64_t>(request.value()) == 3 && request.externalId() == "inst");

    sdk::BootstrapRequest bootstrap;
    bootstrap.adapter().setPluginType("demo").setExternalId("inst-1");
    const sdk::BootstrapRequest copy = bootstrap;
    bootstrap.adapter().setExternalId("inst-2");
    PHI_CHECK(copy.adapter().externalId() == "inst-1" && bootstrap.adapter().externalId() == "inst-2");
    PHI_CHECK(copy.adapter().pluginType() == "demo");
}

void testFormAndAnswerTypesFollowTheSameRules()
{
    PHI_CHECK(sizeof(v1::Adapter) == sizeof(void *) && sizeof(v1::AdapterConfigField) == sizeof(void *));
    PHI_CHECK(sizeof(v1::AdapterConfigSchema) == sizeof(void *) && sizeof(v1::AdapterCapabilities) == sizeof(void *));
    PHI_CHECK(sizeof(v1::ActionResponse) == sizeof(void *) && sizeof(v1::CmdResponse) == sizeof(void *));

    // Nested parts are written in place through the mutable reader.
    v1::AdapterConfigSchema schema;
    schema.factory().setTitle("Bridge").layout().setColumns(2);
    schema.factory().addField(v1::AdapterConfigField("host", v1::AdapterConfigFieldType::Hostname, "Host"));
    schema.factory().fields()[0].layout().setCells(2);
    PHI_CHECK(schema.factory().title() == "Bridge" && schema.factory().layout().columns() == 2);
    PHI_CHECK(schema.factory().fields().size() == 1 && schema.factory().fields()[0].layout().cells() == 2);
    PHI_CHECK(schema.instance().fields().empty());

    // The short constructors, and list-initialisation through them.
    v1::AdapterConfigField field("k", v1::AdapterConfigFieldType::Select, "L");
    field.setOptions({{"a", "A"}, {"b"}}).setActions({{"probe", "Probe"}}).setMinValue(1.5).setStep(std::nullopt);
    PHI_CHECK(field.options().size() == 2 && field.options()[1].value() == "b" && field.options()[1].label().empty());
    PHI_CHECK(field.actions().size() == 1 && field.actions()[0].id() == "probe");
    PHI_CHECK(field.minValue() && *field.minValue() == 1.5 && !field.step());

    // The answers: makers and typed setters that keep resultType in step.
    const v1::CmdResponse ok = v1::CmdResponse::success(7, static_cast<std::int64_t>(1));
    PHI_CHECK(ok.id() == 7 && ok.status() == v1::CmdStatus::Success && std::get<std::int64_t>(ok.finalValue()) == 1);
    const v1::CmdResponse bad = v1::CmdResponse::failure(8, v1::CmdStatus::InvalidArgument, "Missing '%1'", {"host"}, "cmd");
    PHI_CHECK(bad.status() == v1::CmdStatus::InvalidArgument && bad.error() == "Missing '%1'");
    PHI_CHECK(bad.errorParams().size() == 1 && bad.errorContext() == "cmd");

    v1::ActionResponse answer = v1::ActionResponse::success(9);
    PHI_CHECK(answer.resultType() == v1::ActionResultType::None);
    answer.setData("{\"n\":1}");
    PHI_CHECK(answer.resultType() == v1::ActionResultType::Data && answer.dataJson() == "{\"n\":1}");
    answer.setDisplay({"Scan", "123"});
    PHI_CHECK(answer.resultType() == v1::ActionResultType::Display && answer.display().code() == "123");
    answer.setResult(true, v1::ActionResultType::Boolean).addFormValue("host", v1::ScalarValue{v1::Utf8String("x")});
    PHI_CHECK(answer.resultType() == v1::ActionResultType::Boolean && answer.formValues().size() == 1);
    PHI_CHECK(answer.formValues()[0].key() == "host");

    // The record core sends down, read through getters after a move.
    v1::Adapter record;
    record.setExternalId("inst-1").setPort(443).setOptions(v1::AdapterOption::EnableLogs);
    const v1::Adapter moved = std::move(record);
    PHI_CHECK(moved.externalId() == "inst-1" && moved.port() == 443);
    PHI_CHECK(v1::hasFlag(moved.options(), v1::AdapterOption::EnableLogs));
    PHI_CHECK(record.externalId().empty() && record.port() == 0); // NOLINT(bugprone-use-after-move)
}

} // namespace

int main()
{
    testHttpAndTlsTypesFollowTheSameRules();
    testRequestsFollowTheSameRules();
    testFormAndAnswerTypesFollowTheSameRules();
    testOnePointerWide();
    testDefaultsReadAsEmpty();
    testSettersChainAndRead();
    testTheValueKeepsHasValueHonest();
    testCopiesAreDeep();
    testAMovedFromObjectIsEmptyAndAlive();
    return phi::testing::report("sdk_entity_tests");
}
