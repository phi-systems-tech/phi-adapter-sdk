#pragma once

#include <chrono>
#include <functional>
#include <string>

#include "phi/adapter/sdk/runtime.h"
#include "phi/adapter/v1/contract.h"

namespace phicore::adapter::sdk {

/**
 * @brief Bootstrap payload sent by phi-core right after IPC connect.
 */
struct BootstrapRequest {
    /// Database adapter id (`adapters.id`) in phi-core.
    int adapterId = 0;
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
    std::function<void(const std::string &)> onProtocolError;

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
 * Wraps `SidecarRuntime` and provides:
 * - typed inbound request decoding
 * - default response behavior for missing handlers
 * - typed outbound event/result helpers
 */
class SidecarDispatcher
{
public:
    /**
     * @brief Create dispatcher bound to a Unix domain socket path.
     * @param socketPath Filesystem path used by sidecar server socket.
     */
    explicit SidecarDispatcher(std::string socketPath);

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
    bool start(std::string *error = nullptr);

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
    bool pollOnce(std::chrono::milliseconds timeout, std::string *error = nullptr);

    /**
     * @brief Send command response (`kind=cmdResult`).
     */
    bool sendCmdResult(const phicore::adapter::v1::CmdResponse &response, std::string *error = nullptr);

    /**
     * @brief Send action response (`kind=actionResult`).
     */
    bool sendActionResult(const phicore::adapter::v1::ActionResponse &response, std::string *error = nullptr);

    /**
     * @brief Publish adapter connectivity state (`kind=connectionStateChanged`).
     */
    bool sendConnectionStateChanged(bool connected, std::string *error = nullptr);

    /**
     * @brief Publish adapter error event (`kind=error`).
     */
    bool sendAdapterError(const std::string &message,
                          const phicore::adapter::v1::ScalarList &params = {},
                          const std::string &ctx = {},
                          std::string *error = nullptr);

    /**
     * @brief Publish adapter meta patch (`kind=adapterMetaUpdated`).
     * @param metaPatchJson JSON object text.
     */
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                std::string *error = nullptr);

    /**
     * @brief Publish channel state update (`kind=channelStateUpdated`).
     * @param tsMs Timestamp in ms since epoch (`0` => now).
     */
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 std::string *error = nullptr);

    /**
     * @brief Publish full device snapshot (`kind=deviceUpdated`).
     */
    bool sendDeviceUpdated(const phicore::adapter::v1::Device &device,
                           const phicore::adapter::v1::ChannelList &channels,
                           std::string *error = nullptr);

    /**
     * @brief Publish device removal (`kind=deviceRemoved`).
     */
    bool sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                           std::string *error = nullptr);

    /**
     * @brief Publish channel metadata update (`kind=channelUpdated`).
     */
    bool sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                            const phicore::adapter::v1::Channel &channel,
                            std::string *error = nullptr);

    /**
     * @brief Publish room upsert (`kind=roomUpdated`).
     */
    bool sendRoomUpdated(const phicore::adapter::v1::Room &room, std::string *error = nullptr);

    /**
     * @brief Publish room removal (`kind=roomRemoved`).
     */
    bool sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId, std::string *error = nullptr);

    /**
     * @brief Publish group upsert (`kind=groupUpdated`).
     */
    bool sendGroupUpdated(const phicore::adapter::v1::Group &group, std::string *error = nullptr);

    /**
     * @brief Publish group removal (`kind=groupRemoved`).
     */
    bool sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId, std::string *error = nullptr);

    /**
     * @brief Publish adapter scene snapshot (`kind=scenesUpdated`).
     */
    bool sendScenesUpdated(const phicore::adapter::v1::SceneList &scenes, std::string *error = nullptr);

    /**
     * @brief Signal completion of a full sync cycle (`kind=fullSyncCompleted`).
     */
    bool sendFullSyncCompleted(std::string *error = nullptr);

private:
    bool handleRequestFrame(const phicore::adapter::v1::FrameHeader &header,
                            std::span<const std::byte> payload);
    bool sendJson(phicore::adapter::v1::MessageType type,
                  phicore::adapter::v1::CorrelationId correlationId,
                  std::string_view json,
                  std::string *error);

    SidecarRuntime m_runtime;
    SidecarHandlers m_handlers;
};

} // namespace phicore::adapter::sdk
