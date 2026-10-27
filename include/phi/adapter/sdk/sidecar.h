#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "phi/adapter/v1/contract.h"
#include "phi/adapter/v1/sdk_version.h"
#include "phi/adapter/sdk/requests.h"
#include "phi/adapter/sdk/schema_json.h"

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

// The request classes live in requests.h.


/**
 * @brief First-class static adapter descriptor exchanged with phi-core.
 *
 * This descriptor replaces static meta transport for adapter identity,
 * capabilities and config layout.
 */
class AdapterDescriptor
{
public:
    AdapterDescriptor();
    AdapterDescriptor(const AdapterDescriptor &other);
    AdapterDescriptor(AdapterDescriptor &&other) noexcept;
    AdapterDescriptor &operator=(const AdapterDescriptor &other);
    AdapterDescriptor &operator=(AdapterDescriptor &&other) noexcept;
    ~AdapterDescriptor();

    /// Adapter plugin type (e.g. `onkyo-pioneer`).
    const phicore::adapter::v1::Utf8String &pluginType() const noexcept;
    AdapterDescriptor &setPluginType(phicore::adapter::v1::Utf8String pluginType);
    /// User-facing adapter name.
    const phicore::adapter::v1::Utf8String &displayName() const noexcept;
    AdapterDescriptor &setDisplayName(phicore::adapter::v1::Utf8String displayName);
    /// User-facing adapter description.
    const phicore::adapter::v1::Utf8String &description() const noexcept;
    AdapterDescriptor &setDescription(phicore::adapter::v1::Utf8String description);
    /// Adapter API version label, the adapter's own.
    const phicore::adapter::v1::Utf8String &apiVersion() const noexcept;
    AdapterDescriptor &setApiVersion(phicore::adapter::v1::Utf8String apiVersion);
    /// The SDK version the adapter was compiled against. Filled by the
    /// dispatcher when the descriptor goes out, never by the adapter: it is
    /// PHI_ADAPTER_SDK_VERSION as the inline constructors captured it.
    const phicore::adapter::v1::Utf8String &sdkVersion() const noexcept;
    AdapterDescriptor &setSdkVersion(phicore::adapter::v1::Utf8String sdkVersion);
    /// Inline icon SVG markup.
    const phicore::adapter::v1::Utf8String &iconSvg() const noexcept;
    AdapterDescriptor &setIconSvg(phicore::adapter::v1::Utf8String iconSvg);
    /// Optional image payload (base64 text).
    const phicore::adapter::v1::Utf8String &imageBase64() const noexcept;
    AdapterDescriptor &setImageBase64(phicore::adapter::v1::Utf8String imageBase64);
    /// Default device timeout in milliseconds.
    int timeoutMs() const noexcept;
    AdapterDescriptor &setTimeoutMs(int timeoutMs);
    /// Maximum allowed instances (`0` => unlimited).
    int maxInstances() const noexcept;
    AdapterDescriptor &setMaxInstances(int maxInstances);
    const phicore::adapter::v1::AdapterCapabilities &capabilities() const noexcept;
    phicore::adapter::v1::AdapterCapabilities &capabilities();
    AdapterDescriptor &setCapabilities(phicore::adapter::v1::AdapterCapabilities capabilities);
    /// The config forms; null for an adapter without settings.
    const phicore::adapter::v1::AdapterConfigSchema *configSchema() const noexcept;
    AdapterDescriptor &setConfigSchema(phicore::adapter::v1::AdapterConfigSchema schema);
    AdapterDescriptor &clearConfigSchema();

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

// Shared log vocabulary.
//
// These values are the phi log numbering: identical on the adapter IPC wire
// (PROTOCOLL.md, "Level wire encoding"), in phi-core, and in
// phicore::transport::LogLevel on the transport plane. There is exactly one
// numbering, so a value never needs translating when it crosses a plane - it was
// two divergent numberings (0-based here, 1-based there) that made every adapter
// log level arrive one step off in core (F-36/F-39).
//
// The static_asserts below are the guard: renumbering fails to compile rather
// than silently shifting severities.
enum class LogLevel : std::uint8_t {
    Trace = 1,
    Debug = 2,
    Info = 3,
    Warn = 4,
    Error = 5,
};

static_assert(static_cast<std::uint8_t>(LogLevel::Trace) == 1, "log vocabulary: Trace is 1");
static_assert(static_cast<std::uint8_t>(LogLevel::Debug) == 2, "log vocabulary: Debug is 2");
static_assert(static_cast<std::uint8_t>(LogLevel::Info) == 3, "log vocabulary: Info is 3");
static_assert(static_cast<std::uint8_t>(LogLevel::Warn) == 4, "log vocabulary: Warn is 4");
static_assert(static_cast<std::uint8_t>(LogLevel::Error) == 5, "log vocabulary: Error is 5");

// Categories 0..63 are the shared range; 64..127 are reserved for core-local
// extensions and are not available to adapters. Bit 0x80 is the incident flag and
// is set by sendError(...) only.
enum class LogCategory : std::uint8_t {
    Internal = 0,
    Lifecycle = 1,
    Discovery = 2,
    Network = 3,
    Protocol = 4,
    Device = 5,
    Config = 6,
    Performance = 7,
    Security = 8,
    Database = 9,
};

static_assert(static_cast<std::uint8_t>(LogCategory::Internal) == 0, "log vocabulary: Internal is 0");
static_assert(static_cast<std::uint8_t>(LogCategory::Database) == 9, "log vocabulary: Database is 9");
static_assert(static_cast<std::uint8_t>(LogCategory::Database) < 64,
              "log vocabulary: adapter categories live in the shared 0..63 range");

/**
 * @brief Internal: cached log-forwarding filter for one target.
 *
 * Rebuilt whenever the effective adapter config changes so the hot log path
 * never parses the meta JSON per call. Info, Warn and Error always pass.
 */


/// Whether this log reaches core. Info and above always do - what an operator
/// has to see does not hang on a switch - while Trace and Debug wait for
/// `AdapterOption::EnableLogs` and pass `logging.minLevel` / `logging.categories`.
/// Declared here for the test that pins that rule.
// The log filter (LogFilterCache, shouldForwardLog) is the library's own, in src/logfilter.h.

// One log line as the library sends it. Adapters log through the log(...)
// helpers and never build this themselves, so it stays plain.
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

// The dispatcher's wiring. No adapter sees this struct - SidecarHost fills it
// inside the library and the tests fill it directly - so its layout is nobody's
// contract and it stays plain.
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
    /// Called on `sync.device.removed`.
    std::function<void(const DeviceRemovedRequest &)> onDeviceRemoved;
    /// Called on `cmd.channel.invoke`.
    std::function<void(const ChannelInvokeRequest &)> onChannelInvoke;
    /// Called on `cmd.adapter.action.invoke`.
    std::function<void(const AdapterActionInvokeRequest &)> onAdapterActionInvoke;
    /// Called on `cmd.device.name.update`.
    std::function<void(const DeviceNameUpdateRequest &)> onDeviceNameUpdate;
    std::function<void(const EntityNameUpdateRequest &)> onEntityNameUpdate;
    /// Called on `cmd.device.effect.invoke`.
    std::function<void(const DeviceEffectInvokeRequest &)> onDeviceEffectInvoke;
    /// Called on `cmd.mediaPlayer.invoke`.
    std::function<void(const MediaPlayerInvokeRequest &)> onMediaPlayerInvoke;
    /// Called when core asks where a camera's picture comes from.
    std::function<void(const StreamSourceRequest &)> onStreamSourceRequest;
    std::function<void(const PairingStartRequest &)> onPairingStart;
    std::function<void(const PairingStopRequest &)> onPairingStop;
    std::function<void(const PairCandidateRequest &)> onPairCandidate;
    std::function<void(const SetupCodeRequest &)> onSetupCode;
    /// Called on `cmd.scene.invoke`.
    std::function<void(const SceneInvokeRequest &)> onSceneInvoke;
    /// Called on adapter-sidecar stream start IPC.
    std::function<void(const AdaptersStreamStartRequest &)> onAdaptersStreamStart;
    /// Called on adapter-sidecar stream stop IPC.
    std::function<void(const AdaptersStreamStopRequest &)> onAdaptersStreamStop;
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
 *
 * Outbound `send*` calls are enqueue operations. Actual transport writes are
 * serialized on the host poll thread; enqueuing wakes a blocking poll so
 * outbound frames do not wait for the poll timeout.
 *
 * The send queue is bounded. On overflow the oldest log frame (then the
 * oldest event frame) is shed; result/response frames are never shed.
 * Drops are counted and reported via rate-limited host stderr diagnostics.
 */
class SidecarDispatcher
{
public:
    ~SidecarDispatcher();

    /**
     * @brief Create dispatcher bound to a Unix domain socket path.
     * @param socketPath Filesystem path used by sidecar server socket.
     *
     * Inline on purpose: PHI_ADAPTER_SDK_VERSION is read where the adapter is
     * compiled, so the library learns what the adapter was built against.
     */
    explicit SidecarDispatcher(phicore::adapter::v1::Utf8String socketPath)
        : SidecarDispatcher(std::move(socketPath), PHI_ADAPTER_SDK_VERSION)
    {
    }
    /// The same, for a host that is not the adapter itself (tests pin a fixed string).
    SidecarDispatcher(phicore::adapter::v1::Utf8String socketPath, const char *builtAgainstSdkVersion);

    /// What the adapter was compiled against; goes out as the descriptor's `sdkVersion`.
    const char *builtAgainstSdkVersion() const noexcept;

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
     * @brief Descriptor for integrating the sidecar into a foreign event loop.
     *
     * Readable whenever `pollOnce(...)` has work to do - inbound frames as
     * well as outbound work queued from other threads (the wake descriptor is
     * part of the same poll set). Returns `-1` while stopped.
     */
    int pollDescriptor() const noexcept;

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

    /// Whether the instance's chosen topology works (`command=EventTopologyState`).
    bool sendTopologyState(const phicore::adapter::v1::ExternalId &externalId,
                           bool works,
                           const phicore::adapter::v1::Utf8String &reason,
                           phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendDeviceCandidateUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                    const phicore::adapter::v1::DeviceCandidate &candidate,
                                    phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendDeviceCandidateRemoved(const phicore::adapter::v1::ExternalId &externalId,
                                    const phicore::adapter::v1::Utf8String &candidateId,
                                    phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendPairingState(const phicore::adapter::v1::ExternalId &externalId,
                          bool open,
                          int remainingS,
                          phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendPairingProgress(const phicore::adapter::v1::ExternalId &externalId,
                             const phicore::adapter::v1::Utf8String &pairingId,
                             phicore::adapter::v1::PairingStage stage,
                             int percent,
                             const phicore::adapter::v1::Utf8String &message,
                             const phicore::adapter::v1::ExternalId &deviceExternalId,
                             phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter incident/error payload.
     *
     * `sendError(...)` uses the same structured socket log model as `sendLog(...)`,
     * but always with `level=Error` and an incident marker encoded in the wire
     * `category:uint8`. Core may treat such frames as automation-relevant adapter
     * incidents. `params` replace `%1`, `%2`, ... in `message`; `ctx` is the
     * translation context.
     */
    bool sendError(const phicore::adapter::v1::ExternalId &externalId,
                   const phicore::adapter::v1::Utf8String &plugin,
                   LogCategory category = LogCategory::Internal,
                   const phicore::adapter::v1::Utf8String &message = {},
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   const phicore::adapter::v1::JsonText &fieldsJson = {},
                   std::int64_t tsMs = 0,
                   phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish structured adapter log (`command=EventLog`).
     */
    bool sendLog(const phicore::adapter::v1::ExternalId &externalId,
                 const phicore::adapter::v1::Utf8String &plugin,
                 const LogEntry &entry,
                 phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Publish adapter meta patch (`command=EventAdapterMetaUpdated`).
     * @param patch Settings values and facts a client shows; a null value
     *        removes the key. What only the adapter itself reads back belongs
     *        in its state directory, not here.
     *
     * Static adapter identity/capabilities/schema belong to descriptor transport
     * (`ResponseFactoryDescriptor` / `EventFactoryDescriptorUpdated`).
     */
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                const phicore::adapter::v1::AdapterFormValues &patch,
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
    bool sendChannelObjectStateUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                       const phicore::adapter::v1::ExternalId &deviceExternalId,
                                       const phicore::adapter::v1::ExternalId &channelExternalId,
                                       const phicore::adapter::v1::ChannelValueFields &fields,
                                       std::int64_t tsMs = 0,
                                       phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendChannelColorStateUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                      const phicore::adapter::v1::ExternalId &deviceExternalId,
                                      const phicore::adapter::v1::ExternalId &channelExternalId,
                                      double r,
                                      double g,
                                      double b,
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
    bool sendStreamOpen(const phicore::adapter::v1::ExternalId &externalId,
                        const phicore::adapter::v1::Utf8String &streamId,
                        const phicore::adapter::v1::Utf8String &cmd,
                        const phicore::adapter::v1::Utf8String &kind,
                        const phicore::adapter::v1::Utf8String &contentType = "application/json",
                        const phicore::adapter::v1::Utf8String &contentEncoding = {},
                        const phicore::adapter::v1::JsonText &metaJson = "{}",
                        phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendStreamData(const phicore::adapter::v1::ExternalId &externalId,
                        const phicore::adapter::v1::Utf8String &streamId,
                        const phicore::adapter::v1::Utf8String &cmd,
                        std::int64_t seq,
                        const phicore::adapter::v1::JsonText &payloadJson,
                        std::int64_t tsMs = 0,
                        phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendStreamError(const phicore::adapter::v1::ExternalId &externalId,
                         const phicore::adapter::v1::Utf8String &streamId,
                         const phicore::adapter::v1::Utf8String &cmd,
                         const phicore::adapter::v1::Utf8String &message,
                         const phicore::adapter::v1::Utf8String &code = {},
                         const phicore::adapter::v1::ScalarList &params = {},
                         const phicore::adapter::v1::Utf8String &ctx = {},
                         phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendStreamEnd(const phicore::adapter::v1::ExternalId &externalId,
                       const phicore::adapter::v1::Utf8String &streamId,
                       const phicore::adapter::v1::Utf8String &cmd,
                       const phicore::adapter::v1::Utf8String &reason,
                       phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    struct OutboundFrame {
        phicore::adapter::v1::MessageType type = phicore::adapter::v1::MessageType::Event;
        phicore::adapter::v1::CorrelationId correlationId = 0;
        bool isLogFrame = false;
        bool isIncident = false;
        phicore::adapter::v1::Utf8String plugin;
        phicore::adapter::v1::ExternalId externalId;
        phicore::adapter::v1::Utf8String message;
        std::string payload;
    };

    /**
     * @brief Send bootstrap descriptor response (`command=ResponseFactoryDescriptor`).
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
    bool queueOutboundFrame(OutboundFrame frame, phicore::adapter::v1::Utf8String *error = nullptr);
    bool flushSendQueue(phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Interrupt a blocking pollOnce() from any thread.
     *
     * Used after enqueuing outbound work so the poll thread flushes promptly
     * instead of waiting out the poll timeout.
     */
    void wakeup() noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// The vtable lives in the adapter, so any change here breaks every adapter
// built before it. Before v1 that is allowed (the owner, 2026-10-26: all
// adapters are rebuilt with every SDK that changes it); from v1 on this set
// freezes, and anything new arrives as a registration instead of a slot.
/**
 * @brief Optional instance execution backend (thread/event-loop abstraction).
 *
 * SDK core remains Qt-free. Adapters may provide custom backends (for example
 * Qt-based event-loop execution) without changing sidecar host code.
 */
class InstanceExecutionBackend
{
public:
    virtual ~InstanceExecutionBackend() = default;

    virtual bool start(phicore::adapter::v1::Utf8String *error = nullptr) = 0;
    virtual bool execute(std::function<void()> task,
                         phicore::adapter::v1::Utf8String *error = nullptr) = 0;
    /**
     * @brief Stop execution backend with a bounded wait.
     *
     * @param timeout Maximum time to wait for graceful stop.
     * @param error Optional error output.
     * @return `true` if backend stopped gracefully within timeout.
     *         `false` if stop timed out or failed.
     */
    virtual bool stop(std::chrono::milliseconds timeout,
                      phicore::adapter::v1::Utf8String *error = nullptr) = 0;
};

class AdapterInstance;

// The virtuals below are the adapter's vtable: changing them breaks every
// adapter built before. Allowed before v1 (see InstanceExecutionBackend);
// from v1 on a new request from core is a new request class and a handler
// setter, which an adapter built before it never registers.
/**
 * @brief Factory base class for adapter sidecar runtimes.
 *
 * Host dispatch enters through private NVI methods (`host*`), then forwards
 * to protected virtual hooks with natural names.
 */
class AdapterFactory
{
public:
    AdapterFactory();
    virtual ~AdapterFactory();

    AdapterFactory(const AdapterFactory &) = delete;
    AdapterFactory &operator=(const AdapterFactory &) = delete;

    /**
     * @brief Whether the host has asked this factory to shut down.
     *
     * Set from the host thread the moment teardown begins - before `onFactoryStopping()` is
     * scheduled - so it is observable even while this thread is parked in a
     * blocking wait and cannot run queued work. Thread-safe.
     *
     * Adapter code that performs blocking I/O MUST poll this between waits and
     * abandon its work when it turns true; otherwise the cooperative shutdown
     * path cannot win against its own timeouts (see phi::sdk::kShutdownBudget).
     */
    bool stopRequested() const noexcept;

    /// Last bootstrap payload (factory plane).
    const BootstrapRequest &bootstrap() const;
    /// Whether bootstrap payload has been received.
    bool hasBootstrap() const;

    /// Structured log helper for adapter implementers.
    /// `params` replace `%1`, `%2`, ... in `message`; `ctx` is the translation context.
    bool log(LogLevel level,
             LogCategory category,
             const phicore::adapter::v1::Utf8String &message,
             const phicore::adapter::v1::ScalarList &params = {},
             const phicore::adapter::v1::Utf8String &ctx = {},
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
    virtual std::optional<phicore::adapter::v1::AdapterConfigSchema> configSchema() const;
    virtual AdapterDescriptor descriptor() const;
    virtual std::unique_ptr<InstanceExecutionBackend> createInstanceExecutionBackend(
        const phicore::adapter::v1::ExternalId &externalId);

    /**
     * @brief Optional execution backend for factory-scope callbacks.
     *
     * Returning `nullptr` (the default) keeps `onBootstrap`,
     * `onFactoryConfigChanged`, `onFactoryActionInvoke`, `onConnected`,
     * `onDisconnected` and `onProtocolError` on the host poll thread. Any
     * blocking work in those hooks - a device probe, a synchronous HTTP call -
     * then stalls IPC for every instance of this sidecar.
     *
     * Returning a backend moves all six hooks onto that backend, so the poll
     * loop stays responsive while a factory probe runs. Consequences an adapter
     * must know about:
     *  - The six hooks above then run on the backend thread, one at a time, and
     *    never concurrently with each other. Factory state touched only by them
     *    needs no locking.
     *  - `createInstance`, `destroyInstance` and the descriptor accessors
     *    (`pluginType`, `descriptor`, ...) stay on the host thread by design:
     *    routing them through the backend would make instance creation wait out
     *    exactly the blocking probe this backend exists to get out of the way.
     *    Factory state shared between them and the six hooks must be
     *    synchronized by the adapter (or copied, as `bootstrap()` is).
     *  - Objects with thread affinity (`QNetworkAccessManager`, sockets, timers)
     *    must be created lazily inside a hook, not in the factory constructor,
     *    which still runs on the main/host thread.
     */
    virtual std::unique_ptr<InstanceExecutionBackend> createFactoryExecutionBackend();

    virtual std::unique_ptr<AdapterInstance> createInstance(const phicore::adapter::v1::ExternalId &externalId) = 0;
    virtual void destroyInstance(std::unique_ptr<AdapterInstance> instance);

    /**
     * @brief Last factory-scope callback before the sidecar shuts down.
     *
     * Runs on the factory execution backend (inline on the host thread when
     * there is none) while that backend's thread is still alive. Counterpart to
     * the lazy creation of thread-affine objects inside factory hooks: destroy
     * them here, not in the factory destructor, which runs on the main thread.
     */
    virtual void onFactoryStopping();

    virtual void onFactoryActionInvoke(const AdapterActionInvokeRequest &request);
    virtual void onFactoryConfigChanged(const ConfigChangedRequest &request);
    virtual void onConnected();
    virtual void onDisconnected();
    virtual void onProtocolError(const phicore::adapter::v1::Utf8String &message);
    virtual void onBootstrap(const BootstrapRequest &request);

    // Note: connection-state and adapter-meta events are instance-scoped in
    // the v1 contract (core rejects them without an externalId); they are
    // available on AdapterInstance only. Factory-scope runtime updates go
    // through sendFactoryDescriptorUpdated(...).
    bool sendError(LogCategory category,
                   const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   const phicore::adapter::v1::JsonText &fieldsJson = {},
                   std::int64_t tsMs = 0,
                   phicore::adapter::v1::Utf8String *error = nullptr);
    AdapterDescriptor factoryDescriptor() const;
    bool sendFactoryDescriptorUpdated(phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendFactoryDescriptorUpdated(const AdapterDescriptor &descriptor,
                                      phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendResult(const phicore::adapter::v1::ActionResponse &response,
                    phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    void bindDispatcher(SidecarDispatcher *dispatcher);
    void bindResultSubmitter(std::function<void(const phicore::adapter::v1::ActionResponse &)> actionSubmitter);
    void cacheBootstrap(const BootstrapRequest &request);
    void cacheFactoryConfig(const ConfigChangedRequest &request);

    phicore::adapter::v1::Utf8String hostPluginType() const;
    AdapterDescriptor hostDescriptor() const;
    std::unique_ptr<InstanceExecutionBackend> hostCreateInstanceExecutionBackend(
        const phicore::adapter::v1::ExternalId &externalId);
    std::unique_ptr<InstanceExecutionBackend> hostCreateFactoryExecutionBackend();
    std::unique_ptr<AdapterInstance> hostCreateInstance(const phicore::adapter::v1::ExternalId &externalId);
    void hostDestroyInstance(std::unique_ptr<AdapterInstance> instance);
    void hostOnFactoryActionInvoke(const AdapterActionInvokeRequest &request);
    void hostRequestStop();
    void hostOnFactoryStopping();
    void hostOnConnected();
    void hostOnDisconnected();
    void hostOnProtocolError(const phicore::adapter::v1::Utf8String &message);
    // The host caches bootstrap/config on its own thread (see cacheBootstrap,
    // cacheFactoryConfig) and only the notification below may run on a factory
    // execution backend, so host-thread hooks such as createInstance keep
    // reading a stable bootstrap().
    void hostOnBootstrap(const BootstrapRequest &request);
    void hostOnFactoryConfigChanged(const ConfigChangedRequest &request);

    // State is hidden so the SDK can evolve without breaking the ABI of
    // adapter subclasses (see also the LogFilterCache addition in 0.2.0).
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Like AdapterFactory: new virtuals are allowed before v1 (0.28.0 added the
// pairing hooks), frozen from v1 on.
/**
 * @brief Adapter instance base class.
 *
 * Host dispatch enters through private NVI methods (`host*`) and forwards
 * to protected virtual hooks (`start`, `onConfigChanged`, `onChannelInvoke`, ...).
 */
class AdapterInstance
{
public:
    AdapterInstance();
    virtual ~AdapterInstance();

    AdapterInstance(const AdapterInstance &) = delete;
    AdapterInstance &operator=(const AdapterInstance &) = delete;

    /**
     * @brief Whether the host has asked this instance to shut down.
     *
     * Set from the host thread the moment teardown begins - before `stop()` is
     * scheduled - so it is observable even while this thread is parked in a
     * blocking wait and cannot run queued work. Thread-safe.
     *
     * Adapter code that performs blocking I/O MUST poll this between waits and
     * abandon its work when it turns true; otherwise the cooperative shutdown
     * path cannot win against its own timeouts (see phi::sdk::kShutdownBudget).
     */
    bool stopRequested() const noexcept;

    int adapterId() const;
    const phicore::adapter::v1::Utf8String &pluginType() const;
    const phicore::adapter::v1::ExternalId &externalId() const;
    const ConfigChangedRequest &config() const;
    bool hasConfig() const;

    /**
     * @brief The directory this instance keeps what only it reads back.
     *
     * `<tenant data>/adapters/<pluginType>/<externalId>`, from
     * `PHI_ADAPTER_STATE_DIR` when set, else beside the IPC socket core handed
     * out. Created on the first write. Device caches, discovered hosts and the
     * like live here rather than in the adapter meta, which is for what a
     * client shows.
     */
    phicore::adapter::v1::Utf8String stateDirectory() const;
    /// The content of a file in the state directory; nothing when it does not exist.
    std::optional<std::string> readStateFile(std::string_view name) const;
    /// Replaces a file in the state directory atomically (mode 0640).
    bool writeStateFile(std::string_view name,
                        std::string_view content,
                        phicore::adapter::v1::Utf8String *error = nullptr) const;

    /// Structured log helper for adapter implementers.
    /// `params` replace `%1`, `%2`, ... in `message`; `ctx` is the translation context.
    bool log(LogLevel level,
             LogCategory category,
             const phicore::adapter::v1::Utf8String &message,
             const phicore::adapter::v1::ScalarList &params = {},
             const phicore::adapter::v1::Utf8String &ctx = {},
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
    virtual void onChannelInvoke(const ChannelInvokeRequest &request);
    virtual void onAdapterActionInvoke(const AdapterActionInvokeRequest &request);
    virtual void onDeviceNameUpdate(const DeviceNameUpdateRequest &request);
    virtual void onEntityNameUpdate(const EntityNameUpdateRequest &request);
    virtual void onDeviceEffectInvoke(const DeviceEffectInvokeRequest &request);
    virtual void onMediaPlayerInvoke(const MediaPlayerInvokeRequest &request);
    /// Answer with sendResult(ActionResponse) of type Data; see StreamSourceRequest.
    /// The default answers NotImplemented: the instance has no camera.
    virtual void onStreamSourceRequest(const StreamSourceRequest &request);
    virtual void onSceneInvoke(const SceneInvokeRequest &request);
    /// Pairing (see PairingStyle). The defaults answer NotImplemented, which is
    /// right for a plugin that declares no pairing style. Answer each with
    /// sendResult(CmdResponse) as soon as the window is open or the pairing
    /// has started; report the window with sendPairingState and the pairing
    /// with sendPairingProgress under the request's pairingId.
    /// The household deleted one of this instance's devices. Default: nothing.
    /// An adapter that announces devices from its own list (a router's tracked
    /// clients) takes the device off that list here.
    virtual void onDeviceRemoved(const DeviceRemovedRequest &request);
    virtual void onPairingStart(const PairingStartRequest &request);
    virtual void onPairingStop(const PairingStopRequest &request);
    virtual void onPairCandidate(const PairCandidateRequest &request);
    virtual void onSetupCode(const SetupCodeRequest &request);
    virtual void onAdaptersStreamStart(const AdaptersStreamStartRequest &request);
    virtual void onAdaptersStreamStop(const AdaptersStreamStopRequest &request);
    virtual void onUnknownRequest(const UnknownRequest &request);

    bool sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error = nullptr);
    /// Whether the topology the household chose works, after start and on
    /// every change: false with a reason in English ("The box is not routed
    /// through phi") when a local choice has no redirect. Never fall back into
    /// the vendor's cloud instead.
    bool sendTopologyState(bool works,
                           const phicore::adapter::v1::Utf8String &reason = {},
                           phicore::adapter::v1::Utf8String *error = nullptr);
    /// A device seen but not paired, reported again whenever it changes or to
    /// keep it alive (DeviceCandidate::expiresInMs).
    bool sendDeviceCandidateUpdated(const phicore::adapter::v1::DeviceCandidate &candidate,
                                    phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendDeviceCandidateRemoved(const phicore::adapter::v1::Utf8String &candidateId,
                                    phicore::adapter::v1::Utf8String *error = nullptr);
    /// The join window: open with the seconds left, or closed.
    bool sendPairingState(bool open, int remainingS, phicore::adapter::v1::Utf8String *error = nullptr);
    /// How a pairing goes. Done names the new device, whose sendDeviceUpdated
    /// should come first; Failed carries the reason in `message`.
    bool sendPairingProgress(const phicore::adapter::v1::Utf8String &pairingId,
                             phicore::adapter::v1::PairingStage stage,
                             int percent,
                             const phicore::adapter::v1::Utf8String &message = {},
                             const phicore::adapter::v1::ExternalId &deviceExternalId = {},
                             phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendError(LogCategory category,
                   const phicore::adapter::v1::Utf8String &message,
                   const phicore::adapter::v1::ScalarList &params = {},
                   const phicore::adapter::v1::Utf8String &ctx = {},
                   const phicore::adapter::v1::JsonText &fieldsJson = {},
                   std::int64_t tsMs = 0,
                   phicore::adapter::v1::Utf8String *error = nullptr);
    /// Settings values and facts a client shows; a null value removes the key.
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::AdapterFormValues &patch,
                                phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 phicore::adapter::v1::Utf8String *error = nullptr);
    /**
     * @brief A composite channel state: named scalars, one object on the wire.
     *
     * For a `ChannelDataType::Json` channel. The fields are serialized here,
     * so a caller cannot produce a document that breaks the envelope, and the
     * kind's `channelProjectionField()` decides which of them the history and
     * the automation conditions see - the adapter does not choose that.
     *
     * A firmware update is the case this exists for: a status and two versions,
     * where reporting only the status loses the two a person wants to read and
     * reporting only a version loses the answer.
     */
    bool sendChannelObjectStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                       const phicore::adapter::v1::ExternalId &channelExternalId,
                                       const phicore::adapter::v1::ChannelValueFields &fields,
                                       std::int64_t tsMs = 0,
                                       phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendChannelColorStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                      const phicore::adapter::v1::ExternalId &channelExternalId,
                                      double r,
                                      double g,
                                      double b,
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
    bool sendStreamOpen(const phicore::adapter::v1::Utf8String &streamId,
                        const phicore::adapter::v1::Utf8String &cmd,
                        const phicore::adapter::v1::Utf8String &kind,
                        const phicore::adapter::v1::Utf8String &contentType = "application/json",
                        const phicore::adapter::v1::Utf8String &contentEncoding = {},
                        const phicore::adapter::v1::JsonText &metaJson = "{}",
                        phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendStreamData(const phicore::adapter::v1::Utf8String &streamId,
                        const phicore::adapter::v1::Utf8String &cmd,
                        std::int64_t seq,
                        const phicore::adapter::v1::JsonText &payloadJson,
                        std::int64_t tsMs = 0,
                        phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendStreamError(const phicore::adapter::v1::Utf8String &streamId,
                         const phicore::adapter::v1::Utf8String &cmd,
                         const phicore::adapter::v1::Utf8String &message,
                         const phicore::adapter::v1::Utf8String &code = {},
                         const phicore::adapter::v1::ScalarList &params = {},
                         const phicore::adapter::v1::Utf8String &ctx = {},
                         phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendStreamEnd(const phicore::adapter::v1::Utf8String &streamId,
                       const phicore::adapter::v1::Utf8String &cmd,
                       const phicore::adapter::v1::Utf8String &reason,
                       phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendResult(const phicore::adapter::v1::CmdResponse &response,
                    phicore::adapter::v1::Utf8String *error = nullptr);
    bool sendResult(const phicore::adapter::v1::ActionResponse &response,
                    phicore::adapter::v1::Utf8String *error = nullptr);

private:
    friend class SidecarHost;

    void bindDispatcher(SidecarDispatcher *dispatcher);
    void bindResultSubmitters(std::function<void(const phicore::adapter::v1::CmdResponse &)> cmdSubmitter,
                              std::function<void(const phicore::adapter::v1::ActionResponse &)> actionSubmitter);
    void bindContext(int adapterId,
                     phicore::adapter::v1::Utf8String pluginType,
                     phicore::adapter::v1::ExternalId externalId);
    void cacheConfig(const ConfigChangedRequest &request);

    bool hostStart();
    void hostRequestStop();
    void hostStop();
    bool hostRestart();
    void hostOnConnected();
    void hostOnDisconnected();
    void hostOnProtocolError(const phicore::adapter::v1::Utf8String &message);
    void hostOnConfigChanged(const ConfigChangedRequest &request);
    void hostOnChannelInvoke(const ChannelInvokeRequest &request);
    void hostOnAdapterActionInvoke(const AdapterActionInvokeRequest &request);
    void hostOnDeviceNameUpdate(const DeviceNameUpdateRequest &request);
    void hostOnEntityNameUpdate(const EntityNameUpdateRequest &request);
    void hostOnDeviceEffectInvoke(const DeviceEffectInvokeRequest &request);
    void hostOnMediaPlayerInvoke(const MediaPlayerInvokeRequest &request);
    void hostOnStreamSourceRequest(const StreamSourceRequest &request);
    void hostOnSceneInvoke(const SceneInvokeRequest &request);
    void hostOnDeviceRemoved(const DeviceRemovedRequest &request);
    void hostOnPairingStart(const PairingStartRequest &request);
    void hostOnPairingStop(const PairingStopRequest &request);
    void hostOnPairCandidate(const PairCandidateRequest &request);
    void hostOnSetupCode(const SetupCodeRequest &request);
    void hostOnAdaptersStreamStart(const AdaptersStreamStartRequest &request);
    void hostOnAdaptersStreamStop(const AdaptersStreamStopRequest &request);
    void hostOnUnknownRequest(const UnknownRequest &request);

    // State is hidden so the SDK can evolve without breaking the ABI of
    // adapter subclasses.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Total time budget for a sidecar shutdown.
 *
 * phi-core sends `SIGTERM` and kills the process 3 s later, so everything the
 * sidecar does on the way out has to fit in here: the Qt adapters notice the
 * signal within one 250 ms shutdown-timer tick, then `SidecarHost::stop()` gets
 * this budget to stop the instances, join their execution backends and stop the
 * factory backend - which leaves ~750 ms of headroom for process exit.
 * `SidecarHost::stop()` treats it as one deadline and divides it across that
 * work instead of giving each step its own independent timeout.
 *
 * Consequence for adapter code: a blocking wait inside `stop()` that is longer
 * than the instance's slice of this budget cannot be honoured. The cooperative
 * path only wins if every wait in a teardown path is cancellable, or short
 * enough to finish inside the slice.
 */
constexpr auto kShutdownBudget = std::chrono::milliseconds(2000);

/**
 * @brief High-level sidecar host that wires IPC transport and adapter class.
 */
class SidecarHost
{
public:
    /// Inline on purpose, like SidecarDispatcher's: the adapter's
    /// PHI_ADAPTER_SDK_VERSION is captured here and travels in the descriptor.
    explicit SidecarHost(phicore::adapter::v1::Utf8String socketPath, std::unique_ptr<AdapterFactory> factory)
        : SidecarHost(std::move(socketPath), std::move(factory), PHI_ADAPTER_SDK_VERSION)
    {
    }
    SidecarHost(phicore::adapter::v1::Utf8String socketPath, AdapterFactory &factory)
        : SidecarHost(std::move(socketPath), factory, PHI_ADAPTER_SDK_VERSION)
    {
    }
    SidecarHost(phicore::adapter::v1::Utf8String socketPath, std::unique_ptr<AdapterFactory> factory,
                const char *builtAgainstSdkVersion);
    SidecarHost(phicore::adapter::v1::Utf8String socketPath, AdapterFactory &factory,
                const char *builtAgainstSdkVersion);
    ~SidecarHost();

    /**
     * @brief Start IPC host.
     */
    bool start(phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Stop IPC host within `budget`.
     *
     * The budget is one deadline for the whole teardown: it is divided across
     * the instances (so N instances do not multiply it), their execution
     * backends and the factory backend. Steps that overrun their slice are
     * reported as protocol errors and forced, which is what `kShutdownBudget`
     * exists to keep out of the normal path.
     */
    void stop(std::chrono::milliseconds budget = kShutdownBudget);

    /**
     * @brief Poll IPC once.
     */
    bool pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error = nullptr);

    /**
     * @brief Descriptor for integrating the sidecar into a foreign event loop.
     *
     * See `SidecarDispatcher::pollDescriptor()`. An adapter on a foreign loop
     * watches it for readability instead of polling on a timer -
     * `createLoopExecutionBackend()` does exactly that on a phi::runtime loop.
     */
    int pollDescriptor() const noexcept;

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
    struct DeferredCmdResult { phicore::adapter::v1::CmdResponse response; };
    struct DeferredActionResult { phicore::adapter::v1::ActionResponse response; };
    using DeferredResult = std::variant<DeferredCmdResult, DeferredActionResult>;

    struct InstanceRuntime;
    struct Impl;

    static phicore::adapter::v1::CmdResponse normalizeCmdResponse(const phicore::adapter::v1::CmdResponse &response);
    static phicore::adapter::v1::ActionResponse normalizeActionResponse(const phicore::adapter::v1::ActionResponse &response);
    AdapterInstance *ensureInstance(const ConfigChangedRequest &request);
    bool createInstanceRuntime(const ConfigChangedRequest &request,
                               phicore::adapter::v1::Utf8String *error = nullptr);
    AdapterInstance *findInstance(const phicore::adapter::v1::ExternalId &externalId);
    const AdapterInstance *findInstance(const phicore::adapter::v1::ExternalId &externalId) const;
    InstanceRuntime *findRuntime(const phicore::adapter::v1::ExternalId &externalId);
    const InstanceRuntime *findRuntime(const phicore::adapter::v1::ExternalId &externalId) const;
    bool executeOnRuntime(const phicore::adapter::v1::ExternalId &externalId,
                          std::function<void()> task,
                          phicore::adapter::v1::Utf8String *error = nullptr);
    void executeOnAllRuntimes(const std::function<void(AdapterInstance &)> &fn);
    // Runs `task` on the factory execution backend, or inline on the calling
    // thread when the factory did not provide one.
    bool executeOnFactory(std::function<void()> task,
                          phicore::adapter::v1::Utf8String *error = nullptr);
    void reportProtocolError(phicore::adapter::v1::Utf8String message);
    void shutdownFactoryExecution(std::chrono::milliseconds budget);
    void queueDeferredResult(DeferredResult result);
    void drainDeferredResults();
    void stopAndDestroyInstance(const phicore::adapter::v1::ExternalId &externalId,
                                std::chrono::milliseconds budget);
    void stopAndDestroyInstances(std::chrono::steady_clock::time_point deadline);
    void wireHandlers();

    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Options for `runSidecarMain(...)`.
 */
/// Options for `runSidecarMain(...)`. Behind a d-pointer (see
/// phi/adapter/v1/channel.h for the rule); two adapters drive the loop with it.
class SidecarMainOptions
{
public:
    SidecarMainOptions();
    SidecarMainOptions(const SidecarMainOptions &other);
    SidecarMainOptions(SidecarMainOptions &&other) noexcept;
    SidecarMainOptions &operator=(const SidecarMainOptions &other);
    SidecarMainOptions &operator=(SidecarMainOptions &&other) noexcept;
    ~SidecarMainOptions();

    /// Poll timeout per iteration, 1 s by default. Outbound work interrupts a
    /// blocking poll through the wake descriptor, so a long timeout adds no
    /// latency; it only bounds how often an idle sidecar wakes up.
    std::chrono::milliseconds pollTimeout() const noexcept;
    SidecarMainOptions &setPollTimeout(std::chrono::milliseconds timeout);
    /// Loop predicate; the loop exits as soon as it returns false.
    const std::function<bool()> &keepRunning() const noexcept;
    SidecarMainOptions &setKeepRunning(std::function<bool()> keepRunning);
    /// A per-iteration hook, for example a foreign event-loop pump.
    const std::function<void()> &onIteration() const noexcept;
    SidecarMainOptions &setOnIteration(std::function<void()> onIteration);
    /// Install SIGINT/SIGTERM handlers that stop the loop; on by default
    /// because this helper is meant to *be* the process main loop. Off when
    /// the host owns signal handling and drives the loop through keepRunning.
    bool installSignalHandlers() const noexcept;
    SidecarMainOptions &setInstallSignalHandlers(bool install);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/**
 * @brief Reap execution-backend threads that missed their stop deadline.
 *
 * When a backend's worker does not return within its stop budget it is still
 * running adapter code, and a foreign call stack cannot be unwound safely. The
 * SDK keeps such a thread rather than detaching it, so the process can account
 * for it; this call waits up to `grace` for them to finish and joins any that
 * do.
 *
 * @return the number still running.
 *
 * A non-zero result means the process must **not** run static destructors: the
 * thread would then execute against a half-destroyed process, and the crash
 * would land after shutdown was already reported. `runSidecarMain()` handles
 * that; a hand-written main loop has to do the same.
 */
std::size_t reapAbandonedExecutionThreads(
    std::chrono::milliseconds grace = std::chrono::milliseconds::zero());

/**
 * @brief Run the canonical sidecar main loop: start, poll, stop.
 *
 * Replaces the hand-written loop each adapter used to carry.
 * @return `0` after a clean shutdown, `1` when the host failed to start.
 */
int runSidecarMain(SidecarHost &host, const SidecarMainOptions &options = {});

/**
 * @brief The version of the shared library itself.
 *
 * PHI_ADAPTER_SDK_VERSION is the adapter's number, fixed when it was compiled;
 * this is the library's, fixed when the library was. The two differ on a box
 * that updated the SDK without rebuilding the adapter, which is the case the
 * binary boundary exists for.
 */
const char *sdkLibraryVersion() noexcept;

} // namespace phicore::adapter::sdk

#ifndef PHI_LOG_WITH_SOURCE
#define PHI_LOG_WITH_SOURCE(target, level, category, message, params, ctx)                                     \
    (target).log((level),                                                                                      \
                 (category),                                                                                   \
                 (message),                                                                                    \
                 (params),                                                                                     \
                 (ctx),                                                                                        \
                 ::phicore::adapter::sdk::makeSourceLocationFieldsJson(__FILE__, __LINE__, __func__))
#endif

#ifndef PHI_LOG_DEBUG
#define PHI_LOG_DEBUG(target, category, message, params, ctx)                                                  \
    PHI_LOG_WITH_SOURCE((target),                                                                              \
                        ::phicore::adapter::sdk::LogLevel::Debug,                                              \
                        (category),                                                                            \
                        (message),                                                                             \
                        (params),                                                                              \
                        (ctx))
#endif

#ifndef PHI_LOG_TRACE
#define PHI_LOG_TRACE(target, category, message, params, ctx)                                                  \
    PHI_LOG_WITH_SOURCE((target),                                                                              \
                        ::phicore::adapter::sdk::LogLevel::Trace,                                              \
                        (category),                                                                            \
                        (message),                                                                             \
                        (params),                                                                              \
                        (ctx))
#endif
