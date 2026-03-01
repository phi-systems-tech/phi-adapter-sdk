#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "phi/adapter/v1/contract.h"

namespace phicore::adapter::sdk {

class SidecarRuntime;

/**
 * @brief Bootstrap payload sent by phi-core right after IPC connect.
 */
struct BootstrapRequest {
    /// Database adapter id (`adapters.id`) in phi-core.
    int adapterId = 0;
    /// Request-side command id from bootstrap envelope (`cmdId`).
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Transport correlation id from frame header.
    phicore::adapter::v1::CorrelationId correlationId = 0;
    /// Effective adapter instance configuration (host/user/meta/flags/...).
    phicore::adapter::v1::Adapter adapter;
    /// Static adapter config JSON (`AdapterStaticInfo::config`) as raw JSON text.
    phicore::adapter::v1::JsonText staticConfigJson;
};

/**
 * @brief Typed payload for `cmd.channel.invoke`.
 */
struct ChannelInvokeRequest {
    /// Command id assigned by phi-core.
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Adapter-domain device id.
    phicore::adapter::v1::ExternalId deviceExternalId;
    /// Adapter-domain channel id.
    phicore::adapter::v1::ExternalId channelExternalId;
    /// Parsed scalar value when possible.
    phicore::adapter::v1::ScalarValue value;
    /// Original JSON value token for non-scalar/custom payloads.
    phicore::adapter::v1::JsonText valueJson;
    /// `true` when `value` could be parsed into `ScalarValue`.
    bool hasScalarValue = false;
};

/**
 * @brief Typed payload for `cmd.adapter.action.invoke`.
 */
struct AdapterActionInvokeRequest {
    /// Command id assigned by phi-core.
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Action identifier from adapter capabilities.
    phicore::adapter::v1::Utf8String actionId;
    /// Raw JSON object for action params.
    phicore::adapter::v1::JsonText paramsJson;
};

/**
 * @brief Typed payload for `cmd.device.name.update`.
 */
struct DeviceNameUpdateRequest {
    /// Command id assigned by phi-core.
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Adapter-domain device id.
    phicore::adapter::v1::ExternalId deviceExternalId;
    /// New user-facing name.
    phicore::adapter::v1::Utf8String name;
};

/**
 * @brief Typed payload for `cmd.device.effect.invoke`.
 */
struct DeviceEffectInvokeRequest {
    /// Command id assigned by phi-core.
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Adapter-domain device id.
    phicore::adapter::v1::ExternalId deviceExternalId;
    /// Canonical effect enum, when provided by caller.
    phicore::adapter::v1::DeviceEffect effect = phicore::adapter::v1::DeviceEffect::None;
    /// Vendor effect identifier, when provided by caller.
    phicore::adapter::v1::Utf8String effectId;
    /// Raw JSON object for effect params.
    phicore::adapter::v1::JsonText paramsJson;
};

/**
 * @brief Typed payload for `cmd.scene.invoke`.
 */
struct SceneInvokeRequest {
    /// Command id assigned by phi-core.
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Adapter-domain scene id.
    phicore::adapter::v1::ExternalId sceneExternalId;
    /// Optional adapter-domain group id for scoped scene execution.
    phicore::adapter::v1::ExternalId groupExternalId;
    /// Scene action text (`activate`, `deactivate`, ...).
    phicore::adapter::v1::Utf8String action;
};

/**
 * @brief Fallback payload for unsupported/unknown request methods.
 */
struct UnknownRequest {
    /// Command id assigned by phi-core (0 when untracked).
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Raw request method name.
    phicore::adapter::v1::Utf8String method;
    /// Raw request payload as JSON text.
    phicore::adapter::v1::JsonText payloadJson;
};

/**
 * @brief First-class static adapter descriptor exchanged with phi-core.
 *
 * This descriptor replaces static meta transport for adapter identity,
 * capabilities and config layout.
 */
struct AdapterDescriptor {
    /// Adapter plugin type (e.g. `onkyo-pioneer`).
    phicore::adapter::v1::Utf8String pluginType;
    /// User-facing adapter name.
    phicore::adapter::v1::Utf8String displayName;
    /// User-facing adapter description.
    phicore::adapter::v1::Utf8String description;
    /// Adapter API version label.
    phicore::adapter::v1::Utf8String apiVersion;
    /// Inline icon SVG markup.
    phicore::adapter::v1::Utf8String iconSvg;
    /// Optional image payload (base64 text).
    phicore::adapter::v1::Utf8String imageBase64;
    /// Default device timeout in milliseconds.
    int timeoutMs = 0;
    /// Maximum allowed instances (`0` => unlimited).
    int maxInstances = 0;
    /// Adapter capabilities.
    phicore::adapter::v1::AdapterCapabilities capabilities;
    /// Adapter config schema as JSON object text (UTF-8), expected object shape.
    phicore::adapter::v1::JsonText configSchemaJson;
};

/**
 * @brief Callback set used by SidecarDispatcher.
 *
 * Any callback may be left empty. For request handlers without callback, the
 * dispatcher returns a default `NotImplemented` response.
 */
struct SidecarHandlers {
    /// Called when phi-core connects to the sidecar socket.
    std::function<void()> onConnected;
    /// Called when phi-core disconnects from the sidecar socket.
    std::function<void()> onDisconnected;
    /// Called on malformed request payloads/protocol decode failures.
    std::function<void(const phicore::adapter::v1::Utf8String &)> onProtocolError;

    /// Called on `sync.adapter.bootstrap`.
    std::function<void(const BootstrapRequest &)> onBootstrap;
    /// Called on `cmd.channel.invoke`.
    std::function<phicore::adapter::v1::CmdResponse(const ChannelInvokeRequest &)> onChannelInvoke;
    /// Called on `cmd.adapter.action.invoke`.
    std::function<phicore::adapter::v1::ActionResponse(const AdapterActionInvokeRequest &)> onAdapterActionInvoke;
    /// Called on `cmd.device.name.update`.
    std::function<phicore::adapter::v1::CmdResponse(const DeviceNameUpdateRequest &)> onDeviceNameUpdate;
    /// Called on `cmd.device.effect.invoke`.
    std::function<phicore::adapter::v1::CmdResponse(const DeviceEffectInvokeRequest &)> onDeviceEffectInvoke;
    /// Called on `cmd.scene.invoke`.
    std::function<phicore::adapter::v1::CmdResponse(const SceneInvokeRequest &)> onSceneInvoke;
    /// Called when no typed handler exists for a request method.
    std::function<void(const UnknownRequest &)> onUnknownRequest;
};

/**
 * @brief High-level typed IPC helper for adapter sidecars.
 *
 * Wraps the internal IPC runtime and provides:
 * - typed inbound request decoding
 * - default response behavior for missing handlers
 * - typed outbound event/result helpers
 */
class SidecarDispatcher
{
public:
    ~SidecarDispatcher();

    /**
     * @brief Create dispatcher bound to a Unix domain socket path.
     * @param socketPath Filesystem path used by sidecar server socket.
     */
    explicit SidecarDispatcher(phicore::adapter::v1::Utf8String socketPath);

    /**
     * @brief Replace active callback set.
     * @param handlers Callback container.
     */
    void setHandlers(SidecarHandlers handlers);

    /**
     * @brief Start IPC listener.
     * @param error Optional error output on failure.
     * @return `true` on success.
     */
    bool start(phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Stop IPC listener and close current connection.
     */
    void stop();

    /**
     * @brief Run one event loop step.
     * @param timeout Poll timeout.
     * @param error Optional error output on failure.
     * @return `true` on success.
     */
    bool pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send command response (`kind=cmdResult`).
     */
    bool sendCmdResult(const phicore::adapter::v1::CmdResponse &response, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send action response (`kind=actionResult`).
     */
    bool sendActionResult(const phicore::adapter::v1::ActionResponse &response, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter connectivity state (`kind=connectionStateChanged`).
     */
    bool sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter error event (`kind=error`).
     */
    bool sendError(const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter meta patch (`kind=adapterMetaUpdated`).
     * @param metaPatchJson JSON object text for dynamic runtime metadata only.
     *
     * Static adapter identity/capabilities/schema belong to descriptor transport
     * (`adapterDescriptor` / `adapterDescriptorUpdated`).
     */
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish runtime descriptor update (`kind=adapterDescriptorUpdated`).
     * @param descriptor First-class adapter descriptor payload.
     */
    bool sendAdapterDescriptorUpdated(const AdapterDescriptor &descriptor,
                                      phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish channel state update (`kind=channelStateUpdated`).
     * @param tsMs Timestamp in ms since epoch (`0` => now).
     */
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish full device snapshot (`kind=deviceUpdated`).
     */
    bool sendDeviceUpdated(const phicore::adapter::v1::Device &device,
                           const phicore::adapter::v1::ChannelList &channels,
                           phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish device removal (`kind=deviceRemoved`).
     */
    bool sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                           phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish channel metadata update (`kind=channelUpdated`).
     */
    bool sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                            const phicore::adapter::v1::Channel &channel,
                            phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish room upsert (`kind=roomUpdated`).
     */
    bool sendRoomUpdated(const phicore::adapter::v1::Room &room, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish room removal (`kind=roomRemoved`).
     */
    bool sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish group upsert (`kind=groupUpdated`).
     */
    bool sendGroupUpdated(const phicore::adapter::v1::Group &group, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish group removal (`kind=groupRemoved`).
     */
    bool sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter scene snapshot (`kind=scenesUpdated`).
     */
    bool sendScenesUpdated(const phicore::adapter::v1::SceneList &scenes, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Signal completion of a full sync cycle (`kind=fullSyncCompleted`).
     */
    bool sendFullSyncCompleted(phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    /**
     * @brief Send bootstrap descriptor response (`kind=adapterDescriptor`).
     *
     * Internal helper used by `SidecarHost` during bootstrap flow.
     */
    bool sendAdapterDescriptor(const AdapterDescriptor &descriptor,
                               phicore::adapter::v1::CorrelationId correlationId,
                               phicore::adapter::v1::Utf8String *error);

    bool handleRequestFrame(const phicore::adapter::v1::FrameHeader &header,
                            std::span<const std::byte> payload);
    bool sendJson(phicore::adapter::v1::MessageType type,
                  phicore::adapter::v1::CorrelationId correlationId,
                  std::string_view json,
                  phicore::adapter::v1::Utf8String *error);

    std::unique_ptr<SidecarRuntime> m_runtime;
    SidecarHandlers m_handlers;
};

/**
 * @brief Polymorphic base class for C++ adapter sidecars.
 *
 * The host (`SidecarHost`) wires IPC frames to these virtual handlers.
 * Outbound IPC helpers use the `send*` naming convention.
 */
class AdapterSidecar
{
public:
    virtual ~AdapterSidecar() = default;

    /**
     * @brief Returns last bootstrap payload.
     */
    const BootstrapRequest &bootstrap() const;

    /**
     * @brief Returns whether bootstrap payload was received.
     */
    bool hasBootstrap() const;

    /**
     * @brief Database adapter id (`adapters.id`) after bootstrap.
     */
    int adapterId() const;

    /**
     * @brief Effective plugin type after bootstrap.
     */
    const phicore::adapter::v1::Utf8String &pluginType() const;

    /**
     * @brief Effective adapter external id after bootstrap.
     */
    const phicore::adapter::v1::ExternalId &externalId() const;

protected:
    /**
     * @brief Called when phi-core connects to this sidecar socket.
     */
    virtual void onConnected();

    /**
     * @brief Called when phi-core disconnects from this sidecar socket.
     */
    virtual void onDisconnected();

    /**
     * @brief Called on protocol decode/validation errors.
     */
    virtual void onProtocolError(const phicore::adapter::v1::Utf8String &message);

    /**
     * @brief Called after bootstrap payload arrived from phi-core.
     */
    virtual void onBootstrap(const BootstrapRequest &request);

    /**
     * @brief Handle `cmd.channel.invoke`.
     */
    virtual phicore::adapter::v1::CmdResponse onChannelInvoke(const ChannelInvokeRequest &request);

    /**
     * @brief Handle `cmd.adapter.action.invoke`.
     */
    virtual phicore::adapter::v1::ActionResponse onAdapterActionInvoke(const AdapterActionInvokeRequest &request);

    /**
     * @brief Handle `cmd.device.name.update`.
     */
    virtual phicore::adapter::v1::CmdResponse onDeviceNameUpdate(const DeviceNameUpdateRequest &request);

    /**
     * @brief Handle `cmd.device.effect.invoke`.
     */
    virtual phicore::adapter::v1::CmdResponse onDeviceEffectInvoke(const DeviceEffectInvokeRequest &request);

    /**
     * @brief Handle `cmd.scene.invoke`.
     */
    virtual phicore::adapter::v1::CmdResponse onSceneInvoke(const SceneInvokeRequest &request);

    /**
     * @brief Called for unsupported request methods.
     */
    virtual void onUnknownRequest(const UnknownRequest &request);

    /**
     * @brief Returns adapter display name for bootstrap descriptor.
     */
    virtual phicore::adapter::v1::Utf8String displayName() const;

    /**
     * @brief Returns adapter description for bootstrap descriptor.
     */
    virtual phicore::adapter::v1::Utf8String description() const;

    /**
     * @brief Returns adapter API version for bootstrap descriptor.
     */
    virtual phicore::adapter::v1::Utf8String apiVersion() const;

    /**
     * @brief Returns inline adapter icon SVG for bootstrap descriptor.
     */
    virtual phicore::adapter::v1::Utf8String iconSvg() const;

    /**
     * @brief Returns optional adapter image payload for bootstrap descriptor.
     */
    virtual phicore::adapter::v1::Utf8String imageBase64() const;

    /**
     * @brief Returns default device timeout in milliseconds.
     */
    virtual int timeoutMs() const;

    /**
     * @brief Returns maximum supported adapter instances (`0` => unlimited).
     */
    virtual int maxInstances() const;

    /**
     * @brief Returns adapter capabilities.
     */
    virtual phicore::adapter::v1::AdapterCapabilities capabilities() const;

    /**
     * @brief Returns adapter config schema as JSON object text.
     *
     * This value is serialized into descriptor field `configSchema`.
     */
    virtual phicore::adapter::v1::JsonText configSchemaJson() const;

    /**
     * @brief Build first-class adapter descriptor from virtual overrides.
     */
    virtual AdapterDescriptor descriptor() const;
    /**
     * @brief Send command response (`kind=cmdResult`).
     */
    bool sendCmdResult(const phicore::adapter::v1::CmdResponse &response, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send action response (`kind=actionResult`).
     */
    bool sendActionResult(const phicore::adapter::v1::ActionResponse &response, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send adapter connectivity event.
     */
    bool sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send adapter error event (`kind=error`).
     */
    bool sendError(const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send adapter meta patch (`kind=adapterMetaUpdated`).
     */
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send descriptor update event (`kind=adapterDescriptorUpdated`).
     */
    bool sendAdapterDescriptorUpdated(const AdapterDescriptor &descriptor,
                                      phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send channel state update (`kind=channelStateUpdated`).
     */
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send full device snapshot (`kind=deviceUpdated`).
     */
    bool sendDeviceUpdated(const phicore::adapter::v1::Device &device,
                           const phicore::adapter::v1::ChannelList &channels,
                           phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send device removal (`kind=deviceRemoved`).
     */
    bool sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                           phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send channel metadata update (`kind=channelUpdated`).
     */
    bool sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                            const phicore::adapter::v1::Channel &channel,
                            phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send room upsert (`kind=roomUpdated`).
     */
    bool sendRoomUpdated(const phicore::adapter::v1::Room &room, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send room removal (`kind=roomRemoved`).
     */
    bool sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId,
                         phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send group upsert (`kind=groupUpdated`).
     */
    bool sendGroupUpdated(const phicore::adapter::v1::Group &group, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send group removal (`kind=groupRemoved`).
     */
    bool sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId,
                          phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send scene snapshot (`kind=scenesUpdated`).
     */
    bool sendScenesUpdated(const phicore::adapter::v1::SceneList &scenes, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send full sync completion (`kind=fullSyncCompleted`).
     */
    bool sendFullSyncCompleted(phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    void bindDispatcher(SidecarDispatcher *dispatcher);
    void cacheBootstrap(const BootstrapRequest &request);

    SidecarDispatcher *m_dispatcher = nullptr;
    BootstrapRequest m_bootstrap;
    bool m_hasBootstrap = false;
};

/**
 * @brief Factory interface for sidecar adapter instances.
 */
class AdapterFactory
{
public:
    virtual ~AdapterFactory() = default;

    /**
     * @brief Adapter plugin type handled by this factory.
     */
    virtual phicore::adapter::v1::Utf8String pluginType() const = 0;

    /**
     * @brief Create a new adapter sidecar instance.
     */
    virtual std::unique_ptr<AdapterSidecar> create() const = 0;
};

/**
 * @brief High-level sidecar host that wires IPC transport and adapter class.
 */
class SidecarHost
{
public:
    /**
     * @brief Construct host with a concrete adapter sidecar instance.
     */
    SidecarHost(phicore::adapter::v1::Utf8String socketPath, std::unique_ptr<AdapterSidecar> adapter);

    /**
     * @brief Construct host from factory.
     */
    SidecarHost(phicore::adapter::v1::Utf8String socketPath, const AdapterFactory &factory);

    /**
     * @brief Start IPC host.
     */
    bool start(phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Stop IPC host.
     */
    void stop();

    /**
     * @brief Poll IPC once.
     */
    bool pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Returns hosted adapter instance.
     */
    AdapterSidecar *adapter();

    /**
     * @brief Returns hosted adapter instance (const).
     */
    const AdapterSidecar *adapter() const;

    /**
     * @brief Returns underlying dispatcher.
     */
    SidecarDispatcher *dispatcher();

    /**
     * @brief Returns underlying dispatcher (const).
     */
    const SidecarDispatcher *dispatcher() const;

private:
    void wireHandlers();

    SidecarDispatcher m_dispatcher;
    std::unique_ptr<AdapterSidecar> m_adapter;
    phicore::adapter::v1::Utf8String m_factoryPluginType;
};

} // namespace phicore::adapter::sdk
