// The entities behind their d-pointers: one pointer wide, deep on copy, empty
// but alive after a move, and the value accessors keep hasValue honest.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/v1/channel.h"
#include "phi/adapter/v1/device.h"
#include "phi/adapter/v1/group.h"
#include "phi/adapter/v1/room.h"
#include "phi/adapter/v1/scene.h"

#include <utility>

namespace v1 = phicore::adapter::v1;

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
    PHI_CHECK(channel.choices().size() == 2 && channel.choices()[1].value == "b" && channel.choices()[1].label == "B");

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

} // namespace

int main()
{
    testOnePointerWide();
    testDefaultsReadAsEmpty();
    testSettersChainAndRead();
    testTheValueKeepsHasValueHonest();
    testCopiesAreDeep();
    testAMovedFromObjectIsEmptyAndAlive();
    return phi::testing::report("sdk_entity_tests");
}
