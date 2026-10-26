// The request classes behind their d-pointers. Same rules as entities.cpp.

#include "phi/adapter/sdk/requests.h"

#include <type_traits>
#include <utility>

namespace phicore::adapter::sdk {

namespace v1 = phicore::adapter::v1;

#define PHI_SDK_REQUEST_IMPL(Class)                                                                     \
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

#define PHI_SDK_FIELD(Class, Type, name, Setter)                                                        \
    Type Class::name() const noexcept { return p().name; }                                              \
    Class &Class::Setter(std::remove_cvref_t<Type> value)                                               \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

#define PHI_SDK_ADAPTER_PART(Class)                                                                     \
    const v1::Adapter &Class::adapter() const noexcept { return p().adapter; }                          \
    v1::Adapter &Class::adapter() { return m().adapter; }                                               \
    Class &Class::setAdapter(v1::Adapter adapter)                                                       \
    {                                                                                                   \
        m().adapter = std::move(adapter);                                                               \
        return *this;                                                                                   \
    }

// --- BootstrapRequest --------------------------------------------------------

struct BootstrapRequest::Private {
    int adapterId = 0;
    v1::CmdId cmdId = 0;
    v1::CorrelationId correlationId = 0;
    v1::Adapter adapter;
    v1::JsonText staticConfigJson;
};
PHI_SDK_REQUEST_IMPL(BootstrapRequest)
PHI_SDK_FIELD(BootstrapRequest, int, adapterId, setAdapterId)
PHI_SDK_FIELD(BootstrapRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(BootstrapRequest, v1::CorrelationId, correlationId, setCorrelationId)
PHI_SDK_ADAPTER_PART(BootstrapRequest)
PHI_SDK_FIELD(BootstrapRequest, const v1::JsonText &, staticConfigJson, setStaticConfigJson)

// --- ConfigChangedRequest ----------------------------------------------------

struct ConfigChangedRequest::Private {
    int adapterId = 0;
    v1::CmdId cmdId = 0;
    v1::CorrelationId correlationId = 0;
    v1::Adapter adapter;
    v1::JsonText staticConfigJson;
};
PHI_SDK_REQUEST_IMPL(ConfigChangedRequest)
PHI_SDK_FIELD(ConfigChangedRequest, int, adapterId, setAdapterId)
PHI_SDK_FIELD(ConfigChangedRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(ConfigChangedRequest, v1::CorrelationId, correlationId, setCorrelationId)
PHI_SDK_ADAPTER_PART(ConfigChangedRequest)
PHI_SDK_FIELD(ConfigChangedRequest, const v1::JsonText &, staticConfigJson, setStaticConfigJson)

// --- InstanceRemovedRequest --------------------------------------------------

struct InstanceRemovedRequest::Private {
    int adapterId = 0;
    v1::CmdId cmdId = 0;
    v1::CorrelationId correlationId = 0;
    v1::Utf8String pluginType;
    v1::ExternalId externalId;
};
PHI_SDK_REQUEST_IMPL(InstanceRemovedRequest)
PHI_SDK_FIELD(InstanceRemovedRequest, int, adapterId, setAdapterId)
PHI_SDK_FIELD(InstanceRemovedRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(InstanceRemovedRequest, v1::CorrelationId, correlationId, setCorrelationId)
PHI_SDK_FIELD(InstanceRemovedRequest, const v1::Utf8String &, pluginType, setPluginType)
PHI_SDK_FIELD(InstanceRemovedRequest, const v1::ExternalId &, externalId, setExternalId)

// --- ChannelInvokeRequest ----------------------------------------------------

struct ChannelInvokeRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::ExternalId deviceExternalId;
    v1::ExternalId channelExternalId;
    v1::ScalarValue value;
    v1::JsonText valueJson;
    bool hasScalarValue = false;
};
PHI_SDK_REQUEST_IMPL(ChannelInvokeRequest)
PHI_SDK_FIELD(ChannelInvokeRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(ChannelInvokeRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(ChannelInvokeRequest, const v1::ExternalId &, deviceExternalId, setDeviceExternalId)
PHI_SDK_FIELD(ChannelInvokeRequest, const v1::ExternalId &, channelExternalId, setChannelExternalId)
PHI_SDK_FIELD(ChannelInvokeRequest, const v1::JsonText &, valueJson, setValueJson)

const v1::ScalarValue &ChannelInvokeRequest::value() const noexcept { return p().value; }
bool ChannelInvokeRequest::hasScalarValue() const noexcept { return p().hasScalarValue; }
ChannelInvokeRequest &ChannelInvokeRequest::setValue(v1::ScalarValue value)
{
    Private &priv = m();
    priv.value = std::move(value);
    priv.hasScalarValue = true;
    return *this;
}

// --- AdapterActionInvokeRequest ----------------------------------------------

struct AdapterActionInvokeRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::Utf8String actionId;
    v1::JsonText paramsJson;
};
PHI_SDK_REQUEST_IMPL(AdapterActionInvokeRequest)
PHI_SDK_FIELD(AdapterActionInvokeRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(AdapterActionInvokeRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(AdapterActionInvokeRequest, const v1::Utf8String &, actionId, setActionId)
PHI_SDK_FIELD(AdapterActionInvokeRequest, const v1::JsonText &, paramsJson, setParamsJson)

// --- DeviceNameUpdateRequest -------------------------------------------------

struct DeviceNameUpdateRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::ExternalId deviceExternalId;
    v1::Utf8String name;
};
PHI_SDK_REQUEST_IMPL(DeviceNameUpdateRequest)
PHI_SDK_FIELD(DeviceNameUpdateRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(DeviceNameUpdateRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(DeviceNameUpdateRequest, const v1::ExternalId &, deviceExternalId, setDeviceExternalId)
PHI_SDK_FIELD(DeviceNameUpdateRequest, const v1::Utf8String &, name, setName)

// --- EntityNameUpdateRequest -------------------------------------------------

struct EntityNameUpdateRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::NamedEntityKind kind = v1::NamedEntityKind::Room;
    v1::ExternalId entityExternalId;
    v1::Utf8String name;
};
PHI_SDK_REQUEST_IMPL(EntityNameUpdateRequest)
PHI_SDK_FIELD(EntityNameUpdateRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(EntityNameUpdateRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(EntityNameUpdateRequest, v1::NamedEntityKind, kind, setKind)
PHI_SDK_FIELD(EntityNameUpdateRequest, const v1::ExternalId &, entityExternalId, setEntityExternalId)
PHI_SDK_FIELD(EntityNameUpdateRequest, const v1::Utf8String &, name, setName)

// --- DeviceEffectInvokeRequest -----------------------------------------------

struct DeviceEffectInvokeRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::ExternalId deviceExternalId;
    v1::DeviceEffect effect = v1::DeviceEffect::None;
    v1::Utf8String effectId;
    v1::JsonText paramsJson;
};
PHI_SDK_REQUEST_IMPL(DeviceEffectInvokeRequest)
PHI_SDK_FIELD(DeviceEffectInvokeRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(DeviceEffectInvokeRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(DeviceEffectInvokeRequest, const v1::ExternalId &, deviceExternalId, setDeviceExternalId)
PHI_SDK_FIELD(DeviceEffectInvokeRequest, v1::DeviceEffect, effect, setEffect)
PHI_SDK_FIELD(DeviceEffectInvokeRequest, const v1::Utf8String &, effectId, setEffectId)
PHI_SDK_FIELD(DeviceEffectInvokeRequest, const v1::JsonText &, paramsJson, setParamsJson)

// --- StreamSourceRequest -----------------------------------------------------

struct StreamSourceRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::ExternalId deviceExternalId;
    v1::Utf8String kind;
    v1::Utf8String profile;
};
PHI_SDK_REQUEST_IMPL(StreamSourceRequest)
PHI_SDK_FIELD(StreamSourceRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(StreamSourceRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(StreamSourceRequest, const v1::ExternalId &, deviceExternalId, setDeviceExternalId)
PHI_SDK_FIELD(StreamSourceRequest, const v1::Utf8String &, kind, setKind)
PHI_SDK_FIELD(StreamSourceRequest, const v1::Utf8String &, profile, setProfile)

// --- Pairing -----------------------------------------------------------------

struct PairingStartRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    int durationS = 0;
};
PHI_SDK_REQUEST_IMPL(PairingStartRequest)
PHI_SDK_FIELD(PairingStartRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(PairingStartRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(PairingStartRequest, int, durationS, setDurationS)

struct PairingStopRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
};
PHI_SDK_REQUEST_IMPL(PairingStopRequest)
PHI_SDK_FIELD(PairingStopRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(PairingStopRequest, const v1::ExternalId &, externalId, setExternalId)

struct PairCandidateRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::Utf8String pairingId;
    v1::Utf8String candidateId;
    v1::Utf8String setupCode;
    v1::Utf8String name;
    v1::Utf8String wifiSsid;
    v1::Utf8String wifiPassphrase;
};
PHI_SDK_REQUEST_IMPL(PairCandidateRequest)
PHI_SDK_FIELD(PairCandidateRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(PairCandidateRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(PairCandidateRequest, const v1::Utf8String &, pairingId, setPairingId)
PHI_SDK_FIELD(PairCandidateRequest, const v1::Utf8String &, candidateId, setCandidateId)
PHI_SDK_FIELD(PairCandidateRequest, const v1::Utf8String &, setupCode, setSetupCode)
PHI_SDK_FIELD(PairCandidateRequest, const v1::Utf8String &, name, setName)
PHI_SDK_FIELD(PairCandidateRequest, const v1::Utf8String &, wifiSsid, setWifiSsid)
PHI_SDK_FIELD(PairCandidateRequest, const v1::Utf8String &, wifiPassphrase, setWifiPassphrase)

struct SetupCodeRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::Utf8String pairingId;
    v1::SetupCodeScheme scheme = v1::SetupCodeScheme::None;
    v1::Utf8String code;
    v1::Utf8String name;
    v1::Utf8String wifiSsid;
    v1::Utf8String wifiPassphrase;
};
PHI_SDK_REQUEST_IMPL(SetupCodeRequest)
PHI_SDK_FIELD(SetupCodeRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(SetupCodeRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(SetupCodeRequest, const v1::Utf8String &, pairingId, setPairingId)
PHI_SDK_FIELD(SetupCodeRequest, v1::SetupCodeScheme, scheme, setScheme)
PHI_SDK_FIELD(SetupCodeRequest, const v1::Utf8String &, code, setCode)
PHI_SDK_FIELD(SetupCodeRequest, const v1::Utf8String &, name, setName)
PHI_SDK_FIELD(SetupCodeRequest, const v1::Utf8String &, wifiSsid, setWifiSsid)
PHI_SDK_FIELD(SetupCodeRequest, const v1::Utf8String &, wifiPassphrase, setWifiPassphrase)

// --- MediaPlayerInvokeRequest ------------------------------------------------

struct MediaPlayerInvokeRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::ExternalId deviceExternalId;
    v1::MediaPlayerAction action = v1::MediaPlayerAction::Unknown;
    v1::Utf8String mediaId;
    v1::Utf8String mediaType;
    v1::Utf8String mimeType;
    std::int64_t positionMs = 0;
    v1::MediaPlayerEnqueue enqueue = v1::MediaPlayerEnqueue::Replace;
    v1::JsonText metadataJson;
};
PHI_SDK_REQUEST_IMPL(MediaPlayerInvokeRequest)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, const v1::ExternalId &, deviceExternalId, setDeviceExternalId)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, v1::MediaPlayerAction, action, setAction)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, const v1::Utf8String &, mediaId, setMediaId)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, const v1::Utf8String &, mediaType, setMediaType)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, const v1::Utf8String &, mimeType, setMimeType)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, std::int64_t, positionMs, setPositionMs)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, v1::MediaPlayerEnqueue, enqueue, setEnqueue)
PHI_SDK_FIELD(MediaPlayerInvokeRequest, const v1::JsonText &, metadataJson, setMetadataJson)

// --- SceneInvokeRequest ------------------------------------------------------

struct SceneInvokeRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::ExternalId sceneExternalId;
    v1::ExternalId groupExternalId;
    v1::Utf8String action;
};
PHI_SDK_REQUEST_IMPL(SceneInvokeRequest)
PHI_SDK_FIELD(SceneInvokeRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(SceneInvokeRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(SceneInvokeRequest, const v1::ExternalId &, sceneExternalId, setSceneExternalId)
PHI_SDK_FIELD(SceneInvokeRequest, const v1::ExternalId &, groupExternalId, setGroupExternalId)
PHI_SDK_FIELD(SceneInvokeRequest, const v1::Utf8String &, action, setAction)

// --- AdaptersStreamStartRequest ----------------------------------------------

struct AdaptersStreamStartRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::Utf8String streamId;
    v1::Utf8String kind;
    v1::JsonText paramsJson;
};
PHI_SDK_REQUEST_IMPL(AdaptersStreamStartRequest)
PHI_SDK_FIELD(AdaptersStreamStartRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(AdaptersStreamStartRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(AdaptersStreamStartRequest, const v1::Utf8String &, streamId, setStreamId)
PHI_SDK_FIELD(AdaptersStreamStartRequest, const v1::Utf8String &, kind, setKind)
PHI_SDK_FIELD(AdaptersStreamStartRequest, const v1::JsonText &, paramsJson, setParamsJson)

// --- AdaptersStreamStopRequest -----------------------------------------------

struct AdaptersStreamStopRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    v1::Utf8String streamId;
};
PHI_SDK_REQUEST_IMPL(AdaptersStreamStopRequest)
PHI_SDK_FIELD(AdaptersStreamStopRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(AdaptersStreamStopRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(AdaptersStreamStopRequest, const v1::Utf8String &, streamId, setStreamId)

// --- UnknownRequest ----------------------------------------------------------

struct UnknownRequest::Private {
    v1::CmdId cmdId = 0;
    v1::ExternalId externalId;
    std::uint16_t command = 0;
    v1::JsonText payloadJson;
};
PHI_SDK_REQUEST_IMPL(UnknownRequest)
PHI_SDK_FIELD(UnknownRequest, v1::CmdId, cmdId, setCmdId)
PHI_SDK_FIELD(UnknownRequest, const v1::ExternalId &, externalId, setExternalId)
PHI_SDK_FIELD(UnknownRequest, std::uint16_t, command, setCommand)
PHI_SDK_FIELD(UnknownRequest, const v1::JsonText &, payloadJson, setPayloadJson)

#undef PHI_SDK_ADAPTER_PART
#undef PHI_SDK_FIELD
#undef PHI_SDK_REQUEST_IMPL

} // namespace phicore::adapter::sdk
