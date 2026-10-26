// What core asks an adapter, one class per request, each behind a d-pointer
// like the v1 entities (see phi/adapter/v1/channel.h for the rule). The
// library fills them from the wire and hands them to the handlers; an adapter
// reads. A request that gains a field later gains a getter, and an adapter
// built before it keeps running. The setters are the library's and the tests'.
//
// They stay separate classes on purpose: each handler gets exactly the fields
// its request can have, and a new kind of request is a new class and a new
// handler setter, which the d-pointer makes free.
#pragma once

#include "phi/adapter/v1/contract.h"

#include <cstdint>
#include <memory>

namespace phicore::adapter::sdk {

#define PHI_SDK_REQUEST_MEMBERS(Class)                                                                  \
public:                                                                                                 \
    Class();                                                                                            \
    Class(const Class &other);                                                                          \
    Class(Class &&other) noexcept;                                                                      \
    Class &operator=(const Class &other);                                                               \
    Class &operator=(Class &&other) noexcept;                                                           \
    ~Class();                                                                                           \
                                                                                                        \
private:                                                                                                \
    struct Private;                                                                                     \
    const Private &p() const noexcept;                                                                  \
    Private &m();                                                                                       \
    std::unique_ptr<Private> d;                                                                         \
                                                                                                        \
public:

/// The first message of a factory runtime: who it is in core and its static config.
class BootstrapRequest
{
    PHI_SDK_REQUEST_MEMBERS(BootstrapRequest)
    /// Database adapter id (`adapters.id`) in phi-core.
    int adapterId() const noexcept;
    BootstrapRequest &setAdapterId(int adapterId);
    /// Request-side command id from the bootstrap envelope.
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    BootstrapRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    /// Transport correlation id from the frame header.
    phicore::adapter::v1::CorrelationId correlationId() const noexcept;
    BootstrapRequest &setCorrelationId(phicore::adapter::v1::CorrelationId correlationId);
    /// The adapter record as core knows it.
    const phicore::adapter::v1::Adapter &adapter() const noexcept;
    phicore::adapter::v1::Adapter &adapter();
    BootstrapRequest &setAdapter(phicore::adapter::v1::Adapter adapter);
    /// Static adapter config (`<pluginType>-config.json`) as raw JSON text,
    /// here so the factory scope works from the first message on.
    const phicore::adapter::v1::JsonText &staticConfigJson() const noexcept;
    BootstrapRequest &setStaticConfigJson(phicore::adapter::v1::JsonText json);
};

/// An instance's configuration, at creation and on every change.
class ConfigChangedRequest
{
    PHI_SDK_REQUEST_MEMBERS(ConfigChangedRequest)
    int adapterId() const noexcept;
    ConfigChangedRequest &setAdapterId(int adapterId);
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    ConfigChangedRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    phicore::adapter::v1::CorrelationId correlationId() const noexcept;
    ConfigChangedRequest &setCorrelationId(phicore::adapter::v1::CorrelationId correlationId);
    /// The effective instance configuration.
    const phicore::adapter::v1::Adapter &adapter() const noexcept;
    phicore::adapter::v1::Adapter &adapter();
    ConfigChangedRequest &setAdapter(phicore::adapter::v1::Adapter adapter);
    /// Reserved for a static-config delta; in v1 core sends the static config
    /// only with the bootstrap.
    const phicore::adapter::v1::JsonText &staticConfigJson() const noexcept;
    ConfigChangedRequest &setStaticConfigJson(phicore::adapter::v1::JsonText json);
};

/// An instance is gone from core.
class InstanceRemovedRequest
{
    PHI_SDK_REQUEST_MEMBERS(InstanceRemovedRequest)
    int adapterId() const noexcept;
    InstanceRemovedRequest &setAdapterId(int adapterId);
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    InstanceRemovedRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    phicore::adapter::v1::CorrelationId correlationId() const noexcept;
    InstanceRemovedRequest &setCorrelationId(phicore::adapter::v1::CorrelationId correlationId);
    const phicore::adapter::v1::Utf8String &pluginType() const noexcept;
    InstanceRemovedRequest &setPluginType(phicore::adapter::v1::Utf8String pluginType);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    InstanceRemovedRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
};

/// A write to a channel.
class ChannelInvokeRequest
{
    PHI_SDK_REQUEST_MEMBERS(ChannelInvokeRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    ChannelInvokeRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    /// The instance the channel belongs to.
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    ChannelInvokeRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &deviceExternalId() const noexcept;
    ChannelInvokeRequest &setDeviceExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &channelExternalId() const noexcept;
    ChannelInvokeRequest &setChannelExternalId(phicore::adapter::v1::ExternalId externalId);
    /// The value as a scalar, when it parsed as one (hasScalarValue).
    const phicore::adapter::v1::ScalarValue &value() const noexcept;
    bool hasScalarValue() const noexcept;
    ChannelInvokeRequest &setValue(phicore::adapter::v1::ScalarValue value);
    /// The value's JSON token as it arrived, for what is not a scalar.
    const phicore::adapter::v1::JsonText &valueJson() const noexcept;
    ChannelInvokeRequest &setValueJson(phicore::adapter::v1::JsonText json);
};

/// An action from the adapter's descriptor, on the factory (empty externalId)
/// or on an instance.
class AdapterActionInvokeRequest
{
    PHI_SDK_REQUEST_MEMBERS(AdapterActionInvokeRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    AdapterActionInvokeRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    AdapterActionInvokeRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &actionId() const noexcept;
    AdapterActionInvokeRequest &setActionId(phicore::adapter::v1::Utf8String actionId);
    /// The params as a raw JSON object.
    const phicore::adapter::v1::JsonText &paramsJson() const noexcept;
    AdapterActionInvokeRequest &setParamsJson(phicore::adapter::v1::JsonText json);
};

/// A device is to carry a new name.
class DeviceNameUpdateRequest
{
    PHI_SDK_REQUEST_MEMBERS(DeviceNameUpdateRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    DeviceNameUpdateRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    DeviceNameUpdateRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &deviceExternalId() const noexcept;
    DeviceNameUpdateRequest &setDeviceExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &name() const noexcept;
    DeviceNameUpdateRequest &setName(phicore::adapter::v1::Utf8String name);
};

/// A room, group or scene is to carry a new name.
class EntityNameUpdateRequest
{
    PHI_SDK_REQUEST_MEMBERS(EntityNameUpdateRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    EntityNameUpdateRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    EntityNameUpdateRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    /// Which of the three.
    phicore::adapter::v1::NamedEntityKind kind() const noexcept;
    EntityNameUpdateRequest &setKind(phicore::adapter::v1::NamedEntityKind kind);
    const phicore::adapter::v1::ExternalId &entityExternalId() const noexcept;
    EntityNameUpdateRequest &setEntityExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &name() const noexcept;
    EntityNameUpdateRequest &setName(phicore::adapter::v1::Utf8String name);
};

/// A device effect, by canonical enum or by the vendor's id.
class DeviceEffectInvokeRequest
{
    PHI_SDK_REQUEST_MEMBERS(DeviceEffectInvokeRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    DeviceEffectInvokeRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    DeviceEffectInvokeRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &deviceExternalId() const noexcept;
    DeviceEffectInvokeRequest &setDeviceExternalId(phicore::adapter::v1::ExternalId externalId);
    phicore::adapter::v1::DeviceEffect effect() const noexcept;
    DeviceEffectInvokeRequest &setEffect(phicore::adapter::v1::DeviceEffect effect);
    const phicore::adapter::v1::Utf8String &effectId() const noexcept;
    DeviceEffectInvokeRequest &setEffectId(phicore::adapter::v1::Utf8String effectId);
    const phicore::adapter::v1::JsonText &paramsJson() const noexcept;
    DeviceEffectInvokeRequest &setParamsJson(phicore::adapter::v1::JsonText json);
};

/// Where a camera's picture is: a stream or one still frame.
class StreamSourceRequest
{
    PHI_SDK_REQUEST_MEMBERS(StreamSourceRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    StreamSourceRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    StreamSourceRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &deviceExternalId() const noexcept;
    StreamSourceRequest &setDeviceExternalId(phicore::adapter::v1::ExternalId externalId);
    /// `live` or `snapshot`.
    const phicore::adapter::v1::Utf8String &kind() const noexcept;
    StreamSourceRequest &setKind(phicore::adapter::v1::Utf8String kind);
    /// `main` (full resolution) or `sub` (the camera's smaller stream).
    const phicore::adapter::v1::Utf8String &profile() const noexcept;
    StreamSourceRequest &setProfile(phicore::adapter::v1::Utf8String profile);
};

/// Open the network for joining, or start looking for candidates, for a while.
/// Core sends it to every instance that pairs when the household taps "Add".
class PairingStartRequest
{
    PHI_SDK_REQUEST_MEMBERS(PairingStartRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    PairingStartRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    PairingStartRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    /// How long the window stays open; the adapter closes it by itself after.
    int durationS() const noexcept;
    PairingStartRequest &setDurationS(int durationS);
};

/// Close the window early.
class PairingStopRequest
{
    PHI_SDK_REQUEST_MEMBERS(PairingStopRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    PairingStopRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    PairingStopRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
};

/// Pair one candidate the adapter reported. The Wi-Fi credentials are the IoT
/// WLAN's, for devices that need them (Matter over Wi-Fi, Improv); they are
/// secrets: never log them, never echo them in a response.
class PairCandidateRequest
{
    PHI_SDK_REQUEST_MEMBERS(PairCandidateRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    PairCandidateRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    PairCandidateRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    /// Core's handle for this pairing; every PairingProgress for it names it.
    const phicore::adapter::v1::Utf8String &pairingId() const noexcept;
    PairCandidateRequest &setPairingId(phicore::adapter::v1::Utf8String pairingId);
    const phicore::adapter::v1::Utf8String &candidateId() const noexcept;
    PairCandidateRequest &setCandidateId(phicore::adapter::v1::Utf8String candidateId);
    /// The code or PIN, when the candidate needs one.
    const phicore::adapter::v1::Utf8String &setupCode() const noexcept;
    PairCandidateRequest &setSetupCode(phicore::adapter::v1::Utf8String setupCode);
    /// The name the household gave it; put it on the device where the device
    /// keeps one (the rename rule), else leave it to core.
    const phicore::adapter::v1::Utf8String &name() const noexcept;
    PairCandidateRequest &setName(phicore::adapter::v1::Utf8String name);
    const phicore::adapter::v1::Utf8String &wifiSsid() const noexcept;
    PairCandidateRequest &setWifiSsid(phicore::adapter::v1::Utf8String wifiSsid);
    const phicore::adapter::v1::Utf8String &wifiPassphrase() const noexcept;
    PairCandidateRequest &setWifiPassphrase(phicore::adapter::v1::Utf8String wifiPassphrase);
};

/// Pair the device behind a setup code, with no candidate seen first.
class SetupCodeRequest
{
    PHI_SDK_REQUEST_MEMBERS(SetupCodeRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    SetupCodeRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    SetupCodeRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &pairingId() const noexcept;
    SetupCodeRequest &setPairingId(phicore::adapter::v1::Utf8String pairingId);
    /// Which scheme core recognised the code as.
    phicore::adapter::v1::SetupCodeScheme scheme() const noexcept;
    SetupCodeRequest &setScheme(phicore::adapter::v1::SetupCodeScheme scheme);
    const phicore::adapter::v1::Utf8String &code() const noexcept;
    SetupCodeRequest &setCode(phicore::adapter::v1::Utf8String code);
    const phicore::adapter::v1::Utf8String &name() const noexcept;
    SetupCodeRequest &setName(phicore::adapter::v1::Utf8String name);
    const phicore::adapter::v1::Utf8String &wifiSsid() const noexcept;
    SetupCodeRequest &setWifiSsid(phicore::adapter::v1::Utf8String wifiSsid);
    const phicore::adapter::v1::Utf8String &wifiPassphrase() const noexcept;
    SetupCodeRequest &setWifiPassphrase(phicore::adapter::v1::Utf8String wifiPassphrase);
};

/// A media player command, PlayMedia with what to play.
class MediaPlayerInvokeRequest
{
    PHI_SDK_REQUEST_MEMBERS(MediaPlayerInvokeRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    MediaPlayerInvokeRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    MediaPlayerInvokeRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &deviceExternalId() const noexcept;
    MediaPlayerInvokeRequest &setDeviceExternalId(phicore::adapter::v1::ExternalId externalId);
    phicore::adapter::v1::MediaPlayerAction action() const noexcept;
    MediaPlayerInvokeRequest &setAction(phicore::adapter::v1::MediaPlayerAction action);
    /// An adapter-native id or a directly playable URL.
    const phicore::adapter::v1::Utf8String &mediaId() const noexcept;
    MediaPlayerInvokeRequest &setMediaId(phicore::adapter::v1::Utf8String mediaId);
    /// `music`, `movie`, `episode`, `announcement`...
    const phicore::adapter::v1::Utf8String &mediaType() const noexcept;
    MediaPlayerInvokeRequest &setMediaType(phicore::adapter::v1::Utf8String mediaType);
    /// The MIME type when mediaId is a byte stream.
    const phicore::adapter::v1::Utf8String &mimeType() const noexcept;
    MediaPlayerInvokeRequest &setMimeType(phicore::adapter::v1::Utf8String mimeType);
    /// A seek, or the start offset of a PlayMedia.
    std::int64_t positionMs() const noexcept;
    MediaPlayerInvokeRequest &setPositionMs(std::int64_t positionMs);
    phicore::adapter::v1::MediaPlayerEnqueue enqueue() const noexcept;
    MediaPlayerInvokeRequest &setEnqueue(phicore::adapter::v1::MediaPlayerEnqueue enqueue);
    /// Display metadata; never a credential.
    const phicore::adapter::v1::JsonText &metadataJson() const noexcept;
    MediaPlayerInvokeRequest &setMetadataJson(phicore::adapter::v1::JsonText json);
};

/// A scene is to run (`activate`, `deactivate`, ...).
class SceneInvokeRequest
{
    PHI_SDK_REQUEST_MEMBERS(SceneInvokeRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    SceneInvokeRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    SceneInvokeRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::ExternalId &sceneExternalId() const noexcept;
    SceneInvokeRequest &setSceneExternalId(phicore::adapter::v1::ExternalId externalId);
    /// The group to run it on, when scoped.
    const phicore::adapter::v1::ExternalId &groupExternalId() const noexcept;
    SceneInvokeRequest &setGroupExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &action() const noexcept;
    SceneInvokeRequest &setAction(phicore::adapter::v1::Utf8String action);
};

/// Core opens a stream (`adapter.log`, `camera.live`, ...).
class AdaptersStreamStartRequest
{
    PHI_SDK_REQUEST_MEMBERS(AdaptersStreamStartRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    AdaptersStreamStartRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    AdaptersStreamStartRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &streamId() const noexcept;
    AdaptersStreamStartRequest &setStreamId(phicore::adapter::v1::Utf8String streamId);
    const phicore::adapter::v1::Utf8String &kind() const noexcept;
    AdaptersStreamStartRequest &setKind(phicore::adapter::v1::Utf8String kind);
    const phicore::adapter::v1::JsonText &paramsJson() const noexcept;
    AdaptersStreamStartRequest &setParamsJson(phicore::adapter::v1::JsonText json);
};

/// Core closes a stream.
class AdaptersStreamStopRequest
{
    PHI_SDK_REQUEST_MEMBERS(AdaptersStreamStopRequest)
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    AdaptersStreamStopRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    AdaptersStreamStopRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    const phicore::adapter::v1::Utf8String &streamId() const noexcept;
    AdaptersStreamStopRequest &setStreamId(phicore::adapter::v1::Utf8String streamId);
};

/// A command this library does not know: the raw number and payload, for an
/// adapter that wants to answer it anyway. The default answer is NotSupported.
class UnknownRequest
{
    PHI_SDK_REQUEST_MEMBERS(UnknownRequest)
    /// 0 when the command was not tracked.
    phicore::adapter::v1::CmdId cmdId() const noexcept;
    UnknownRequest &setCmdId(phicore::adapter::v1::CmdId cmdId);
    const phicore::adapter::v1::ExternalId &externalId() const noexcept;
    UnknownRequest &setExternalId(phicore::adapter::v1::ExternalId externalId);
    /// The wire `command` as it arrived.
    std::uint16_t command() const noexcept;
    UnknownRequest &setCommand(std::uint16_t command);
    const phicore::adapter::v1::JsonText &payloadJson() const noexcept;
    UnknownRequest &setPayloadJson(phicore::adapter::v1::JsonText json);
};

#undef PHI_SDK_REQUEST_MEMBERS

} // namespace phicore::adapter::sdk
