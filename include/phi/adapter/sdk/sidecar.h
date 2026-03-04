#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>

#include "phi/adapter/v1/contract.h"

namespace phicore::adapter::sdk {

class SidecarRuntime;

// SDK-facing aliases for v1 contract types.
using CmdId = phicore::adapter::v1::CmdId;
using CorrelationId = phicore::adapter::v1::CorrelationId;
using ExternalId = phicore::adapter::v1::ExternalId;
using Utf8String = phicore::adapter::v1::Utf8String;
using JsonText = phicore::adapter::v1::JsonText;
using ScalarValue = phicore::adapter::v1::ScalarValue;
using ScalarList = phicore::adapter::v1::ScalarList;
using CmdStatus = phicore::adapter::v1::CmdStatus;
using ActionResultType = phicore::adapter::v1::ActionResultType;
using IpcCommand = phicore::adapter::v1::IpcCommand;
using CmdResponse = phicore::adapter::v1::CmdResponse;
using ActionResponse = phicore::adapter::v1::ActionResponse;
using Adapter = phicore::adapter::v1::Adapter;
using AdapterCapabilities = phicore::adapter::v1::AdapterCapabilities;
using Device = phicore::adapter::v1::Device;
using Channel = phicore::adapter::v1::Channel;
using ChannelList = phicore::adapter::v1::ChannelList;
using Room = phicore::adapter::v1::Room;
using Group = phicore::adapter::v1::Group;
using Scene = phicore::adapter::v1::Scene;

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
    /// Adapter identity payload.
    phicore::adapter::v1::Adapter adapter;
    /// Static adapter config JSON (`<pluginType>-config.json`) as raw JSON text.
    /// This is provided during bootstrap so factory scope is immediately functional.
    phicore::adapter::v1::JsonText staticConfigJson;
};

/**
 * @brief Runtime adapter configuration update payload.
 *
 * Sent by phi-core after bootstrap and whenever effective runtime config changes.
 *
 * Scope is resolved by `adapter.externalId`:
 * - `adapter.externalId == ""` -> factory scope
 * - `adapter.externalId != ""` -> instance scope
 */
struct ConfigChangedRequest {
    /// Database adapter id (`adapters.id`) in phi-core.
    int adapterId = 0;
    /// Request-side command id from envelope (`cmdId`).
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Transport correlation id from frame header.
    phicore::adapter::v1::CorrelationId correlationId = 0;
    /// Effective adapter instance configuration snapshot.
    phicore::adapter::v1::Adapter adapter;
    /// Reserved for future static-config delta transport.
    /// In v1, phi-core sends static config only in `BootstrapRequest::staticConfigJson`.
    phicore::adapter::v1::JsonText staticConfigJson;
};

/**
 * @brief Instance removal payload (`sync.adapter.instance.removed`).
 */
struct InstanceRemovedRequest {
    /// Database adapter id (`adapters.id`) in phi-core.
    int adapterId = 0;
    /// Request-side command id from envelope (`cmdId`).
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Transport correlation id from frame header.
    phicore::adapter::v1::CorrelationId correlationId = 0;
    /// Adapter plugin type for runtime routing.
    phicore::adapter::v1::Utf8String pluginType;
    /// Target adapter instance external id.
    phicore::adapter::v1::ExternalId externalId;
};

/**
 * @brief Typed payload for `cmd.channel.invoke`.
 */
struct ChannelInvokeRequest {
    /// Command id assigned by phi-core.
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Target adapter instance external id (`externalId`).
    phicore::adapter::v1::ExternalId externalId;
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
    /// Target external id (`""` => factory scope, non-empty => instance scope).
    phicore::adapter::v1::ExternalId externalId;
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
    /// Target adapter instance external id (`externalId`).
    phicore::adapter::v1::ExternalId externalId;
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
    /// Target adapter instance external id (`externalId`).
    phicore::adapter::v1::ExternalId externalId;
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
    /// Target adapter instance external id (`externalId`).
    phicore::adapter::v1::ExternalId externalId;
    /// Adapter-domain scene id.
    phicore::adapter::v1::ExternalId sceneExternalId;
    /// Optional adapter-domain group id for scoped scene execution.
    phicore::adapter::v1::ExternalId groupExternalId;
    /// Scene action text (`activate`, `deactivate`, ...).
    phicore::adapter::v1::Utf8String action;
};

/**
 * @brief Fallback payload for unsupported/unknown request commands.
 */
struct UnknownRequest {
    /// Command id assigned by phi-core (0 when untracked).
    phicore::adapter::v1::CmdId cmdId = 0;
    /// Target external id (if present in request payload).
    phicore::adapter::v1::ExternalId externalId;
    /// Raw request command id (wire `command` as uint16).
    std::uint16_t command = 0;
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

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
};

enum class LogCategory : std::uint8_t {
    Event = 0,
    Lifecycle = 1,
    Discovery = 2,
    Network = 3,
    Protocol = 4,
    DeviceState = 5,
    Config = 6,
    Performance = 7,
    Security = 8,
    Internal = 9,
};

struct LogEntry {
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCategory::Internal;
    /// Human-readable message text (UTF-8).
    phicore::adapter::v1::Utf8String message;
    /// Translation context key used by translation engines (not source/module context).
    phicore::adapter::v1::Utf8String ctx;
    /// Placeholder replacements for `%1`, `%2`, ... in `message`.
    phicore::adapter::v1::ScalarList params;
    phicore::adapter::v1::JsonText fieldsJson;
    std::int64_t tsMs = 0;
};

/**
 * @brief Build canonical source-location fields JSON for debug/trace logs.
 *
 * Output shape:
 * `{"file":"<basename>","line":<line>,"func":"<function>"}`
 */
phicore::adapter::v1::JsonText makeSourceLocationFieldsJson(const char *file,
                                                            int line,
                                                            const char *functionName);

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
    /// Called on `sync.adapter.config.changed` (factory if `externalId==""`, instance otherwise).
    std::function<void(const ConfigChangedRequest &)> onConfigChanged;
    /// Called on `sync.adapter.instance.removed`.
    std::function<void(const InstanceRemovedRequest &)> onInstanceRemoved;
    /// Called on `cmd.channel.invoke`.
    std::function<void(const ChannelInvokeRequest &)> onChannelInvoke;
    /// Called on `cmd.adapter.action.invoke`.
    std::function<void(const AdapterActionInvokeRequest &)> onAdapterActionInvoke;
    /// Called on `cmd.device.name.update`.
    std::function<void(const DeviceNameUpdateRequest &)> onDeviceNameUpdate;
    /// Called on `cmd.device.effect.invoke`.
    std::function<void(const DeviceEffectInvokeRequest &)> onDeviceEffectInvoke;
    /// Called on `cmd.scene.invoke`.
    std::function<void(const SceneInvokeRequest &)> onSceneInvoke;
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
     * @brief Send command response (`command=ResultCmd`).
     */
    bool sendCmdResult(const phicore::adapter::v1::CmdResponse &response, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Send action response (`command=ResultAction`).
     */
    bool sendActionResult(const phicore::adapter::v1::ActionResponse &response, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter connectivity state (`command=EventConnectionStateChanged`).
     */
    bool sendConnectionStateChanged(const phicore::adapter::v1::ExternalId &externalId,
                                    bool connected,
                                    phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter error event (`command=EventError`).
     *
     * Adapter wrapper APIs (`AdapterFactory::sendError`, `AdapterInstance::sendError`)
     * additionally emit a mirrored `EventLog` with `level=Error`,
     * `category=Event`, and `fields={"source":"event.error"}`.
     * `ctx` is translation context; `params` replace `%1`, `%2`, ... in `message`.
     */
    bool sendError(const phicore::adapter::v1::ExternalId &externalId,
                   const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish structured adapter log (`command=EventLog`).
     */
    bool sendLog(const phicore::adapter::v1::ExternalId &externalId,
                 const phicore::adapter::v1::Utf8String &pluginType,
                 const LogEntry &entry,
                 phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter meta patch (`command=EventAdapterMetaUpdated`).
     * @param metaPatchJson JSON object text for dynamic runtime metadata only.
     *
     * Static adapter identity/capabilities/schema belong to descriptor transport
     * (`EventFactoryDescriptor` / `EventFactoryDescriptorUpdated`).
     */
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                const phicore::adapter::v1::JsonText &metaPatchJson,
                                phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish runtime descriptor update (`command=EventFactoryDescriptorUpdated`).
     * @param descriptor First-class adapter descriptor payload.
     */
    bool sendAdapterDescriptorUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                      const AdapterDescriptor &descriptor,
                                      phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish channel state update (`command=EventChannelStateUpdated`).
     * @param tsMs Timestamp in ms since epoch (`0` => now).
     */
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                 const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish full device snapshot (`command=EventDeviceUpdated`).
     */
    bool sendDeviceUpdated(const phicore::adapter::v1::ExternalId &externalId,
                           const phicore::adapter::v1::Device &device,
                           const phicore::adapter::v1::ChannelList &channels,
                           phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish device removal (`command=EventDeviceRemoved`).
     */
    bool sendDeviceRemoved(const phicore::adapter::v1::ExternalId &externalId,
                           const phicore::adapter::v1::ExternalId &deviceExternalId,
                           phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish channel metadata update (`command=EventChannelUpdated`).
     */
    bool sendChannelUpdated(const phicore::adapter::v1::ExternalId &externalId,
                            const phicore::adapter::v1::ExternalId &deviceExternalId,
                            const phicore::adapter::v1::Channel &channel,
                            phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish room upsert (`command=EventRoomUpdated`).
     */
    bool sendRoomUpdated(const phicore::adapter::v1::ExternalId &externalId,
                         const phicore::adapter::v1::Room &room,
                         phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish room removal (`command=EventRoomRemoved`).
     */
    bool sendRoomRemoved(const phicore::adapter::v1::ExternalId &externalId,
                         const phicore::adapter::v1::ExternalId &roomExternalId,
                         phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish group upsert (`command=EventGroupUpdated`).
     */
    bool sendGroupUpdated(const phicore::adapter::v1::ExternalId &externalId,
                          const phicore::adapter::v1::Group &group,
                          phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish group removal (`command=EventGroupRemoved`).
     */
    bool sendGroupRemoved(const phicore::adapter::v1::ExternalId &externalId,
                          const phicore::adapter::v1::ExternalId &groupExternalId,
                          phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter scene update (`command=EventSceneUpdated`).
     */
    bool sendSceneUpdated(const phicore::adapter::v1::ExternalId &externalId,
                          const phicore::adapter::v1::Scene &scene,
                          phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish scene removal (`command=EventSceneRemoved`).
     */
    bool sendSceneRemoved(const phicore::adapter::v1::ExternalId &externalId,
                          const phicore::adapter::v1::ExternalId &sceneExternalId,
                          phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Signal completion of a full sync cycle (`command=EventFullSyncCompleted`).
     */
    bool sendFullSyncCompleted(const phicore::adapter::v1::ExternalId &externalId,
                               phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    /**
     * @brief Send bootstrap descriptor response (`command=EventFactoryDescriptor`).
     *
     * Internal helper used by `SidecarHost` during bootstrap flow.
     */
    bool sendAdapterDescriptor(const phicore::adapter::v1::ExternalId &externalId,
                               const AdapterDescriptor &descriptor,
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
    std::recursive_mutex m_runtimeMutex;
};

class AdapterInstance;

/**
 * @brief Factory base class for adapter sidecar runtimes.
 *
 * Host dispatch enters through private NVI methods (`host*`), then forwards
 * to protected virtual hooks with natural names.
 */
class AdapterFactory
{
public:
    virtual ~AdapterFactory() = default;

    /// Last bootstrap payload (factory plane).
    const BootstrapRequest &bootstrap() const;
    /// Whether bootstrap payload has been received.
    bool hasBootstrap() const;

    /// Structured log helper for adapter implementers.
    /// `ctx` is translation context; `params` replace `%1`, `%2`, ... in `message`.
    bool log(LogLevel level,
             LogCategory category,
             const phicore::adapter::v1::Utf8String &message,
             const phicore::adapter::v1::Utf8String &ctx = {},
             const phicore::adapter::v1::ScalarList &params = {},
             const phicore::adapter::v1::JsonText &fieldsJson = {},
             std::int64_t tsMs = 0,
             phicore::adapter::v1::Utf8String *error = nullptr);

protected:
    virtual phicore::adapter::v1::Utf8String pluginType() const = 0;
    virtual phicore::adapter::v1::Utf8String displayName() const;
    virtual phicore::adapter::v1::Utf8String description() const;
    virtual phicore::adapter::v1::Utf8String apiVersion() const;
    virtual phicore::adapter::v1::Utf8String iconSvg() const;
    virtual phicore::adapter::v1::Utf8String imageBase64() const;
    virtual int timeoutMs() const;
    virtual int maxInstances() const;
    virtual phicore::adapter::v1::AdapterCapabilities capabilities() const;
    virtual phicore::adapter::v1::JsonText configSchemaJson() const;
    virtual AdapterDescriptor descriptor() const;

    virtual std::unique_ptr<AdapterInstance> createInstance(const phicore::adapter::v1::ExternalId &externalId) = 0;
    virtual void destroyInstance(std::unique_ptr<AdapterInstance> instance);

    virtual phicore::adapter::v1::ActionResponse onFactoryActionInvoke(const AdapterActionInvokeRequest &request);
    virtual void onFactoryConfigChanged(const ConfigChangedRequest &request);
    virtual void onConnected();
    virtual void onDisconnected();
    virtual void onProtocolError(const phicore::adapter::v1::Utf8String &message);
    virtual void onBootstrap(const BootstrapRequest &request);

    bool sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendError(const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                phicore::adapter::v1::Utf8String *error = nullptr);
    AdapterDescriptor factoryDescriptor() const;
    bool sendFactoryDescriptorUpdated(phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendFactoryDescriptorUpdated(const AdapterDescriptor &descriptor,
                                      phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    void bindDispatcher(SidecarDispatcher *dispatcher);
    void cacheBootstrap(const BootstrapRequest &request);
    void cacheFactoryConfig(const ConfigChangedRequest &request);

    phicore::adapter::v1::Utf8String hostPluginType() const;
    AdapterDescriptor hostDescriptor() const;
    std::unique_ptr<AdapterInstance> hostCreateInstance(const phicore::adapter::v1::ExternalId &externalId);
    void hostDestroyInstance(std::unique_ptr<AdapterInstance> instance);
    phicore::adapter::v1::ActionResponse hostOnFactoryActionInvoke(const AdapterActionInvokeRequest &request);
    void hostOnConnected();
    void hostOnDisconnected();
    void hostOnProtocolError(const phicore::adapter::v1::Utf8String &message);
    void hostOnBootstrap(const BootstrapRequest &request);
    void hostOnFactoryConfigChanged(const ConfigChangedRequest &request);

    SidecarDispatcher *m_dispatcher = nullptr;
    BootstrapRequest m_bootstrap;
    bool m_hasBootstrap = false;
    ConfigChangedRequest m_factoryConfig;
    bool m_hasFactoryConfig = false;
};

/**
 * @brief Adapter instance base class.
 *
 * Host dispatch enters through private NVI methods (`host*`) and forwards
 * to protected virtual hooks (`start`, `onConfigChanged`, `onChannelInvoke`, ...).
 */
class AdapterInstance
{
public:
    virtual ~AdapterInstance() = default;

    int adapterId() const;
    const phicore::adapter::v1::Utf8String &pluginType() const;
    const phicore::adapter::v1::ExternalId &externalId() const;
    const ConfigChangedRequest &config() const;
    bool hasConfig() const;

    /// Structured log helper for adapter implementers.
    /// `ctx` is translation context; `params` replace `%1`, `%2`, ... in `message`.
    bool log(LogLevel level,
             LogCategory category,
             const phicore::adapter::v1::Utf8String &message,
             const phicore::adapter::v1::Utf8String &ctx = {},
             const phicore::adapter::v1::ScalarList &params = {},
             const phicore::adapter::v1::JsonText &fieldsJson = {},
             std::int64_t tsMs = 0,
             phicore::adapter::v1::Utf8String *error = nullptr);

protected:
    virtual bool start();
    virtual void stop();
    virtual bool restart();

    virtual void onConnected();
    virtual void onDisconnected();
    virtual void onProtocolError(const phicore::adapter::v1::Utf8String &message);
    virtual void onConfigChanged(const ConfigChangedRequest &request);
    virtual phicore::adapter::v1::CmdResponse onChannelInvoke(const ChannelInvokeRequest &request);
    virtual phicore::adapter::v1::ActionResponse onAdapterActionInvoke(const AdapterActionInvokeRequest &request);
    virtual phicore::adapter::v1::CmdResponse onDeviceNameUpdate(const DeviceNameUpdateRequest &request);
    virtual phicore::adapter::v1::CmdResponse onDeviceEffectInvoke(const DeviceEffectInvokeRequest &request);
    virtual phicore::adapter::v1::CmdResponse onSceneInvoke(const SceneInvokeRequest &request);
    virtual void onUnknownRequest(const UnknownRequest &request);

    bool sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendError(const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendDeviceUpdated(const phicore::adapter::v1::Device &device,
                           const phicore::adapter::v1::ChannelList &channels,
                           phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                           phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                            const phicore::adapter::v1::Channel &channel,
                            phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendRoomUpdated(const phicore::adapter::v1::Room &room, phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId,
                         phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendGroupUpdated(const phicore::adapter::v1::Group &group, phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId,
                          phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendSceneUpdated(const phicore::adapter::v1::Scene &scene, phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendSceneRemoved(const phicore::adapter::v1::ExternalId &sceneExternalId,
                          phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendFullSyncCompleted(phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    void bindDispatcher(SidecarDispatcher *dispatcher);
    void bindContext(int adapterId,
                     phicore::adapter::v1::Utf8String pluginType,
                     phicore::adapter::v1::ExternalId externalId);
    void cacheConfig(const ConfigChangedRequest &request);

    bool hostStart();
    void hostStop();
    bool hostRestart();
    void hostOnConnected();
    void hostOnDisconnected();
    void hostOnProtocolError(const phicore::adapter::v1::Utf8String &message);
    void hostOnConfigChanged(const ConfigChangedRequest &request);
    phicore::adapter::v1::CmdResponse hostOnChannelInvoke(const ChannelInvokeRequest &request);
    phicore::adapter::v1::ActionResponse hostOnAdapterActionInvoke(const AdapterActionInvokeRequest &request);
    phicore::adapter::v1::CmdResponse hostOnDeviceNameUpdate(const DeviceNameUpdateRequest &request);
    phicore::adapter::v1::CmdResponse hostOnDeviceEffectInvoke(const DeviceEffectInvokeRequest &request);
    phicore::adapter::v1::CmdResponse hostOnSceneInvoke(const SceneInvokeRequest &request);
    void hostOnUnknownRequest(const UnknownRequest &request);

    SidecarDispatcher *m_dispatcher = nullptr;
    int m_adapterId = 0;
    phicore::adapter::v1::Utf8String m_pluginType;
    phicore::adapter::v1::ExternalId m_externalId;
    ConfigChangedRequest m_config;
    bool m_hasConfig = false;
};

/**
 * @brief High-level sidecar host that wires IPC transport and adapter class.
 */
class SidecarHost
{
public:
    explicit SidecarHost(phicore::adapter::v1::Utf8String socketPath, std::unique_ptr<AdapterFactory> factory);
    SidecarHost(phicore::adapter::v1::Utf8String socketPath, AdapterFactory &factory);
    ~SidecarHost();

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

    AdapterFactory *factory();
    const AdapterFactory *factory() const;
    AdapterInstance *instance(const phicore::adapter::v1::ExternalId &externalId);
    const AdapterInstance *instance(const phicore::adapter::v1::ExternalId &externalId) const;

    /**
     * @brief Returns underlying dispatcher.
     */
    SidecarDispatcher *dispatcher();

    /**
     * @brief Returns underlying dispatcher (const).
     */
    const SidecarDispatcher *dispatcher() const;

private:
    struct WorkerTaskConnected {};
    struct WorkerTaskDisconnected {};
    struct WorkerTaskProtocolError { phicore::adapter::v1::Utf8String message; };
    struct WorkerTaskConfigChanged { ConfigChangedRequest request; };
    struct WorkerTaskUnknown { UnknownRequest request; };
    struct WorkerTaskChannelInvoke { ChannelInvokeRequest request; };
    struct WorkerTaskAdapterActionInvoke { AdapterActionInvokeRequest request; };
    struct WorkerTaskDeviceNameUpdate { DeviceNameUpdateRequest request; };
    struct WorkerTaskDeviceEffectInvoke { DeviceEffectInvokeRequest request; };
    struct WorkerTaskSceneInvoke { SceneInvokeRequest request; };

    using WorkerTask = std::variant<WorkerTaskConnected,
                                    WorkerTaskDisconnected,
                                    WorkerTaskProtocolError,
                                    WorkerTaskConfigChanged,
                                    WorkerTaskUnknown,
                                    WorkerTaskChannelInvoke,
                                    WorkerTaskAdapterActionInvoke,
                                    WorkerTaskDeviceNameUpdate,
                                    WorkerTaskDeviceEffectInvoke,
                                    WorkerTaskSceneInvoke>;

    struct DeferredCmdResult { phicore::adapter::v1::CmdResponse response; };
    struct DeferredActionResult { phicore::adapter::v1::ActionResponse response; };
    using DeferredResult = std::variant<DeferredCmdResult, DeferredActionResult>;

    struct InstanceWorker {
        phicore::adapter::v1::ExternalId externalId;
        std::unique_ptr<AdapterInstance> instance;
        std::thread thread;
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<WorkerTask> tasks;
        bool stopRequested = false;
        bool started = false;
        bool startOk = false;
    };

    enum class PendingKind : std::uint8_t {
        Cmd = 0,
        Action = 1,
    };

    struct PendingCommand {
        PendingKind kind = PendingKind::Cmd;
        phicore::adapter::v1::ExternalId externalId;
        std::int64_t deadlineMs = 0;
    };

    static phicore::adapter::v1::CmdResponse normalizeCmdResponse(phicore::adapter::v1::CmdId cmdId,
                                                                  const phicore::adapter::v1::CmdResponse &response);
    static phicore::adapter::v1::ActionResponse normalizeActionResponse(phicore::adapter::v1::CmdId cmdId,
                                                                        const phicore::adapter::v1::ActionResponse &response);
    AdapterInstance *ensureInstance(const ConfigChangedRequest &request);
    bool createInstanceWorker(const ConfigChangedRequest &request,
                              phicore::adapter::v1::Utf8String *error = nullptr);
    AdapterInstance *findInstance(const phicore::adapter::v1::ExternalId &externalId);
    const AdapterInstance *findInstance(const phicore::adapter::v1::ExternalId &externalId) const;
    InstanceWorker *findWorker(const phicore::adapter::v1::ExternalId &externalId);
    const InstanceWorker *findWorker(const phicore::adapter::v1::ExternalId &externalId) const;
    void workerMain(InstanceWorker *worker);
    bool enqueueWorkerTask(const phicore::adapter::v1::ExternalId &externalId, WorkerTask task);
    void enqueueWorkerTaskBroadcast(const WorkerTask &task);
    void queueDeferredResult(DeferredResult result);
    void drainDeferredResults();
    void completePendingTimeouts();
    int commandTimeoutMs(phicore::adapter::v1::Utf8String *error = nullptr) const;
    bool trackPending(phicore::adapter::v1::CmdId cmdId,
                      PendingKind kind,
                      const phicore::adapter::v1::ExternalId &externalId,
                      phicore::adapter::v1::Utf8String *error = nullptr);
    void clearPendingForInstance(const phicore::adapter::v1::ExternalId &externalId,
                                 const phicore::adapter::v1::Utf8String &reason);
    void scheduleInstanceRemoval(const phicore::adapter::v1::ExternalId &externalId);
    void drainDeferredInstanceRemovals();
    void stopAndDestroyInstance(const phicore::adapter::v1::ExternalId &externalId);
    void stopAndDestroyInstances();
    void wireHandlers();

    SidecarDispatcher m_dispatcher;
    std::unique_ptr<AdapterFactory> m_ownedFactory;
    AdapterFactory *m_factory = nullptr;
    std::unordered_map<phicore::adapter::v1::ExternalId, std::unique_ptr<InstanceWorker>> m_instances;
    std::deque<std::unique_ptr<InstanceWorker>> m_instancesPendingRemoval;
    std::unordered_map<phicore::adapter::v1::CmdId, PendingCommand> m_pendingCommands;
    std::mutex m_resultMutex;
    std::deque<DeferredResult> m_resultQueue;
};

} // namespace phicore::adapter::sdk

#ifndef PHI_LOG_WITH_SOURCE
#define PHI_LOG_WITH_SOURCE(target, level, category, message, ctx, ...)                                        \
    (target).log((level),                                                                                      \
                 (category),                                                                                   \
                 (message),                                                                                    \
                 (ctx),                                                                                        \
                 __VA_ARGS__,                                                                                  \
                 ::phicore::adapter::sdk::makeSourceLocationFieldsJson(__FILE__, __LINE__, __func__))
#endif

#ifndef PHI_LOG_DEBUG
#define PHI_LOG_DEBUG(target, category, message, ctx, ...)                                                     \
    PHI_LOG_WITH_SOURCE((target),                                                                              \
                        ::phicore::adapter::sdk::LogLevel::Debug,                                              \
                        (category),                                                                            \
                        (message),                                                                             \
                        (ctx),                                                                                 \
                        __VA_ARGS__)
#endif

#ifndef PHI_LOG_TRACE
#define PHI_LOG_TRACE(target, category, message, ctx, ...)                                                     \
    PHI_LOG_WITH_SOURCE((target),                                                                              \
                        ::phicore::adapter::sdk::LogLevel::Trace,                                              \
                        (category),                                                                            \
                        (message),                                                                             \
                        (ctx),                                                                                 \
                        __VA_ARGS__)
#endif
