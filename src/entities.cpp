// The entity classes behind their d-pointers: Channel, Device and its effects,
// Room, Group, Scene. Every field is reached through an exported accessor, so
// an adapter never sees the layout and a later field is a later accessor.
//
// Copies are deep. A moved-from object has no Private and reads as empty
// through a shared null instance; the first setter gives it one again.

#include "phi/adapter/v1/candidate.h"
#include "phi/adapter/v1/channel.h"
#include "phi/adapter/v1/device.h"
#include "phi/adapter/v1/group.h"
#include "phi/adapter/v1/room.h"
#include "phi/adapter/v1/scene.h"

#include "net_address.h"

#include <type_traits>
#include <utility>

namespace phicore::adapter::v1 {

// The six classes share the same special members; one macro writes them so
// they cannot drift apart. Each class defines its own Private first.
#define PHI_V1_ENTITY_MEMBERS(Class)                                                                    \
    Class::Class() : d(std::make_unique<Private>()) {}                                                  \
    Class::Class(const Class &other) : d(other.d ? std::make_unique<Private>(*other.d) : nullptr) {}    \
    Class::Class(Class &&other) noexcept : d(std::move(other.d)) {}                                     \
    Class &Class::operator=(const Class &other)                                                         \
    {                                                                                                   \
        if (this != &other)                                                                             \
            d = other.d ? std::make_unique<Private>(*other.d) : nullptr;                                \
        return *this;                                                                                   \
    }                                                                                                   \
    Class &Class::operator=(Class &&other) noexcept                                                     \
    {                                                                                                   \
        d = std::move(other.d);                                                                         \
        return *this;                                                                                   \
    }                                                                                                   \
    Class::~Class() = default;                                                                          \
    const Class::Private &Class::p() const noexcept                                                     \
    {                                                                                                   \
        static const Private empty;                                                                     \
        return d ? *d : empty;                                                                          \
    }                                                                                                   \
    Class::Private &Class::m()                                                                          \
    {                                                                                                   \
        if (!d)                                                                                         \
            d = std::make_unique<Private>();                                                            \
        return *d;                                                                                      \
    }

#define PHI_V1_FIELD(Class, Type, name, Setter)                                                         \
    Type Class::name() const noexcept { return p().name; }                                              \
    Class &Class::Setter(std::remove_cvref_t<Type> value)                                               \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

// --- Channel -----------------------------------------------------------------

struct Channel::Private {
    Utf8String name;
    ExternalId externalId;
    ChannelKind kind = ChannelKind::Unknown;
    ChannelDataType dataType = ChannelDataType::Unknown;
    ChannelFlags flags = ChannelFlag::None;
    Utf8String unit;
    double minValue = 0.0;
    double maxValue = 0.0;
    double stepValue = 0.0;
    JsonText metaJson;
    AdapterConfigOptionList choices;
    ScalarValue lastValue;
    std::int64_t lastUpdateMs = 0;
    bool hasValue = false;
};

PHI_V1_ENTITY_MEMBERS(Channel)
PHI_V1_FIELD(Channel, const Utf8String &, name, setName)
PHI_V1_FIELD(Channel, const ExternalId &, externalId, setExternalId)
PHI_V1_FIELD(Channel, ChannelKind, kind, setKind)
PHI_V1_FIELD(Channel, ChannelDataType, dataType, setDataType)
PHI_V1_FIELD(Channel, ChannelFlags, flags, setFlags)
PHI_V1_FIELD(Channel, const Utf8String &, unit, setUnit)
PHI_V1_FIELD(Channel, double, minValue, setMinValue)
PHI_V1_FIELD(Channel, double, maxValue, setMaxValue)
PHI_V1_FIELD(Channel, double, stepValue, setStepValue)
PHI_V1_FIELD(Channel, const JsonText &, metaJson, setMetaJson)
PHI_V1_FIELD(Channel, const AdapterConfigOptionList &, choices, setChoices)

Channel::Channel(ExternalId externalId, Utf8String name, ChannelKind kind, ChannelDataType dataType, ChannelFlags flags)
    : d(std::make_unique<Private>())
{
    d->externalId = std::move(externalId);
    d->name = std::move(name);
    d->kind = kind;
    d->dataType = dataType;
    d->flags = flags;
}

Channel &Channel::addFlags(ChannelFlags flags)
{
    Private &priv = m();
    priv.flags = priv.flags | flags;
    return *this;
}

Channel &Channel::setRange(double minValue, double maxValue, double stepValue)
{
    Private &priv = m();
    priv.minValue = minValue;
    priv.maxValue = maxValue;
    priv.stepValue = stepValue;
    return *this;
}

Channel &Channel::addChoice(Utf8String value, Utf8String label)
{
    m().choices.emplace_back(std::move(value), std::move(label));
    return *this;
}

const ScalarValue &Channel::lastValue() const noexcept { return p().lastValue; }
std::int64_t Channel::lastUpdateMs() const noexcept { return p().lastUpdateMs; }
bool Channel::hasValue() const noexcept { return p().hasValue; }

Channel &Channel::setLastValue(ScalarValue value, std::int64_t updateMs)
{
    Private &priv = m();
    priv.lastValue = std::move(value);
    priv.lastUpdateMs = updateMs;
    priv.hasValue = true;
    return *this;
}

Channel &Channel::clearValue()
{
    Private &priv = m();
    priv.lastValue = ScalarValue{};
    priv.lastUpdateMs = 0;
    priv.hasValue = false;
    return *this;
}

// --- DeviceEffectDescriptor --------------------------------------------------

struct DeviceEffectDescriptor::Private {
    DeviceEffect effect = DeviceEffect::None;
    Utf8String id;
    Utf8String label;
    Utf8String description;
    bool requiresParams = false;
    JsonText metaJson;
};

PHI_V1_ENTITY_MEMBERS(DeviceEffectDescriptor)
PHI_V1_FIELD(DeviceEffectDescriptor, DeviceEffect, effect, setEffect)
PHI_V1_FIELD(DeviceEffectDescriptor, const Utf8String &, id, setId)
PHI_V1_FIELD(DeviceEffectDescriptor, const Utf8String &, label, setLabel)
PHI_V1_FIELD(DeviceEffectDescriptor, const Utf8String &, description, setDescription)
PHI_V1_FIELD(DeviceEffectDescriptor, bool, requiresParams, setRequiresParams)
PHI_V1_FIELD(DeviceEffectDescriptor, const JsonText &, metaJson, setMetaJson)

// --- Device ------------------------------------------------------------------

struct Device::Private {
    Utf8String name;
    DeviceClass deviceClass = DeviceClass::Unknown;
    DeviceFlags flags = DeviceFlag::None;
    ExternalId externalId;
    Utf8String manufacturer;
    Utf8String firmware;
    Utf8String model;
    JsonText metaJson;
    Utf8String ipv4;
    Utf8String ipv6;
    Utf8String host;
    Utf8String mac;
    Utf8String bleAddress;
    MediaPlayerFeatures mediaPlayerFeatures = MediaPlayerFeature::None;
    CameraFeatures cameraFeatures = CameraFeature::None;
    DeviceEffectDescriptorList effects;
};

PHI_V1_ENTITY_MEMBERS(Device)
PHI_V1_FIELD(Device, const Utf8String &, name, setName)
PHI_V1_FIELD(Device, DeviceClass, deviceClass, setDeviceClass)
PHI_V1_FIELD(Device, DeviceFlags, flags, setFlags)
PHI_V1_FIELD(Device, const ExternalId &, externalId, setExternalId)
PHI_V1_FIELD(Device, const Utf8String &, manufacturer, setManufacturer)
PHI_V1_FIELD(Device, const Utf8String &, firmware, setFirmware)
PHI_V1_FIELD(Device, const Utf8String &, model, setModel)
PHI_V1_FIELD(Device, const JsonText &, metaJson, setMetaJson)
PHI_V1_FIELD(Device, MediaPlayerFeatures, mediaPlayerFeatures, setMediaPlayerFeatures)
PHI_V1_FIELD(Device, CameraFeatures, cameraFeatures, setCameraFeatures)
PHI_V1_FIELD(Device, const DeviceEffectDescriptorList &, effects, setEffects)

const Utf8String &Device::ipv4() const noexcept { return p().ipv4; }
Device &Device::setIpv4(Utf8String ipv4)
{
    m().ipv4 = detail::normalizedIp(ipv4, AF_INET);
    return *this;
}
const Utf8String &Device::ipv6() const noexcept { return p().ipv6; }
Device &Device::setIpv6(Utf8String ipv6)
{
    m().ipv6 = detail::normalizedIp(ipv6, AF_INET6);
    return *this;
}
const Utf8String &Device::host() const noexcept { return p().host; }
Device &Device::setHost(Utf8String host)
{
    m().host = detail::trimmedAscii(host);
    return *this;
}
const Utf8String &Device::mac() const noexcept { return p().mac; }
Device &Device::setMac(Utf8String mac)
{
    m().mac = detail::normalizedMac(mac);
    return *this;
}
const Utf8String &Device::bleAddress() const noexcept { return p().bleAddress; }
Device &Device::setBleAddress(Utf8String bleAddress)
{
    m().bleAddress = detail::normalizedMac(bleAddress);
    return *this;
}

Device &Device::addEffect(DeviceEffectDescriptor effect)
{
    m().effects.push_back(std::move(effect));
    return *this;
}

Device &Device::addFlags(DeviceFlags flags)
{
    Private &priv = m();
    priv.flags = priv.flags | flags;
    return *this;
}

Device &Device::addMediaPlayerFeatures(MediaPlayerFeatures features)
{
    Private &priv = m();
    priv.mediaPlayerFeatures = priv.mediaPlayerFeatures | features;
    return *this;
}

Device &Device::addCameraFeatures(CameraFeatures features)
{
    Private &priv = m();
    priv.cameraFeatures = priv.cameraFeatures | features;
    return *this;
}

// --- DeviceCandidate ---------------------------------------------------------

struct DeviceCandidate::Private {
    Utf8String id;
    Utf8String name;
    DeviceClass deviceClass = DeviceClass::Unknown;
    Utf8String manufacturer;
    Utf8String model;
    Radio radio = Radio::Unknown;
    int rssi = 0;
    ExternalId roomExternalId;
    PairingNeed needs = PairingNeed::None;
    SetupCodeScheme setupCodeScheme = SetupCodeScheme::None;
    Utf8String instruction;
    std::int64_t expiresInMs = 0;
    JsonText metaJson;
};

PHI_V1_ENTITY_MEMBERS(DeviceCandidate)
PHI_V1_FIELD(DeviceCandidate, const Utf8String &, id, setId)
PHI_V1_FIELD(DeviceCandidate, const Utf8String &, name, setName)
PHI_V1_FIELD(DeviceCandidate, DeviceClass, deviceClass, setDeviceClass)
PHI_V1_FIELD(DeviceCandidate, const Utf8String &, manufacturer, setManufacturer)
PHI_V1_FIELD(DeviceCandidate, const Utf8String &, model, setModel)
PHI_V1_FIELD(DeviceCandidate, Radio, radio, setRadio)
PHI_V1_FIELD(DeviceCandidate, int, rssi, setRssi)
PHI_V1_FIELD(DeviceCandidate, const ExternalId &, roomExternalId, setRoomExternalId)
PHI_V1_FIELD(DeviceCandidate, PairingNeed, needs, setNeeds)
PHI_V1_FIELD(DeviceCandidate, SetupCodeScheme, setupCodeScheme, setSetupCodeScheme)
PHI_V1_FIELD(DeviceCandidate, const Utf8String &, instruction, setInstruction)
PHI_V1_FIELD(DeviceCandidate, std::int64_t, expiresInMs, setExpiresInMs)
PHI_V1_FIELD(DeviceCandidate, const JsonText &, metaJson, setMetaJson)

// --- Room --------------------------------------------------------------------

struct Room::Private {
    ExternalId externalId;
    Utf8String name;
    Utf8String zone;
    RoomClass roomClass = RoomClass::Unknown;
    std::vector<ExternalId> deviceExternalIds;
    JsonText metaJson;
};

PHI_V1_ENTITY_MEMBERS(Room)
PHI_V1_FIELD(Room, const ExternalId &, externalId, setExternalId)
PHI_V1_FIELD(Room, const Utf8String &, name, setName)
PHI_V1_FIELD(Room, const Utf8String &, zone, setZone)
PHI_V1_FIELD(Room, RoomClass, roomClass, setRoomClass)
PHI_V1_FIELD(Room, const std::vector<ExternalId> &, deviceExternalIds, setDeviceExternalIds)
PHI_V1_FIELD(Room, const JsonText &, metaJson, setMetaJson)

Room &Room::addDeviceExternalId(ExternalId id)
{
    m().deviceExternalIds.push_back(std::move(id));
    return *this;
}

// --- Group -------------------------------------------------------------------

struct Group::Private {
    ExternalId externalId;
    Utf8String name;
    Utf8String zone;
    std::vector<ExternalId> deviceExternalIds;
    JsonText metaJson;
};

PHI_V1_ENTITY_MEMBERS(Group)
PHI_V1_FIELD(Group, const ExternalId &, externalId, setExternalId)
PHI_V1_FIELD(Group, const Utf8String &, name, setName)
PHI_V1_FIELD(Group, const Utf8String &, zone, setZone)
PHI_V1_FIELD(Group, const std::vector<ExternalId> &, deviceExternalIds, setDeviceExternalIds)
PHI_V1_FIELD(Group, const JsonText &, metaJson, setMetaJson)

Group &Group::addDeviceExternalId(ExternalId id)
{
    m().deviceExternalIds.push_back(std::move(id));
    return *this;
}

// --- Scene -------------------------------------------------------------------

struct Scene::Private {
    ExternalId externalId;
    Utf8String name;
    Utf8String description;
    ExternalId scopeExternalId;
    Utf8String scopeType;
    Utf8String avatarColor;
    Utf8String image;
    Utf8String presetTag;
    SceneState state = SceneState::Unknown;
    SceneFlags flags = SceneFlag::None;
    JsonText metaJson;
};

PHI_V1_ENTITY_MEMBERS(Scene)
PHI_V1_FIELD(Scene, const ExternalId &, externalId, setExternalId)
PHI_V1_FIELD(Scene, const Utf8String &, name, setName)
PHI_V1_FIELD(Scene, const Utf8String &, description, setDescription)
PHI_V1_FIELD(Scene, const ExternalId &, scopeExternalId, setScopeExternalId)
PHI_V1_FIELD(Scene, const Utf8String &, scopeType, setScopeType)
PHI_V1_FIELD(Scene, const Utf8String &, avatarColor, setAvatarColor)
PHI_V1_FIELD(Scene, const Utf8String &, image, setImage)
PHI_V1_FIELD(Scene, const Utf8String &, presetTag, setPresetTag)
PHI_V1_FIELD(Scene, SceneState, state, setState)
PHI_V1_FIELD(Scene, SceneFlags, flags, setFlags)
PHI_V1_FIELD(Scene, const JsonText &, metaJson, setMetaJson)

Scene &Scene::addFlags(SceneFlags flags)
{
    Private &priv = m();
    priv.flags = priv.flags | flags;
    return *this;
}

Scene &Scene::setScope(Utf8String scopeType, ExternalId scopeExternalId)
{
    Private &priv = m();
    priv.scopeType = std::move(scopeType);
    priv.scopeExternalId = std::move(scopeExternalId);
    return *this;
}

#undef PHI_V1_FIELD
#undef PHI_V1_ENTITY_MEMBERS

} // namespace phicore::adapter::v1
