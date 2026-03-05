# phi-adapter-sdk

Linux-first SDK for phi adapter sidecars.

## Targets

- `phi::adapter-contract`
  - Header-only contract (`phicore::adapter::v1`)
  - Domain types for adapter integration: schema, device, channel, room/group/scene, discovery
  - Stable protocol primitives (`CmdId`, `ExternalId`, frame header, message type)
  - Central enum ↔ string helpers in `phi/adapter/v1/enum_names.h`
- `phi::adapter-sdk`
  - Linux runtime helpers (UDS + epoll transport)
  - Typed dispatcher (`SidecarDispatcher`)
  - C++ sidecar model (`AdapterFactory`, `AdapterInstance`, `SidecarHost`)
  - Shared runtime library (`libphi_adapter_sdk.so`)

## Scope

- Runtime transport is Linux-only (`epoll`, Unix Domain Sockets)
- No Qt dependency
- No Boost dependency
- `externalId` is the canonical adapter-domain identifier in v1 contract types
- Contract text type is `phicore::adapter::v1::Utf8String` (`std::string` alias)
- All contract text fields are UTF-8 by contract
- C++ API is the primary SDK surface for v1
- Enum string conversion (`enum_names.h`) is strict v1 canonical naming (no legacy aliases)

## SDK Type Aliases

Recommended adapter-side alias:

```cpp
namespace phi = phicore::adapter::sdk;
```

With that alias, common contract types are available as:

- `phi::Utf8String`, `phi::JsonText`, `phi::ExternalId`, `phi::CmdId`
- `phi::CmdResponse`, `phi::ActionResponse`, `phi::CmdStatus`, `phi::ActionResultType`
- `phi::Adapter`, `phi::Device`, `phi::Channel`, `phi::Room`, `phi::Group`, `phi::Scene`

## STRICT V1 POLICY: NO FALLBACKS, NO BACKWARD COMPATIBILITY

- Do not implement legacy aliases for schema keys, action ids, channel ids, or enum names.
- Do not add implicit key mapping (`port` -> `iscpPort`, etc.) in adapter handlers.
- Treat missing required keys as `InvalidArgument`.
- Keep adapter schema and handler keys identical and explicit.
- Any rename/removal of public schema/action/channel keys is a breaking v1 change and must be migrated explicitly.
- This applies across the full stack: adapter sidecars, phi-core, phi-ui, and automation runtime/editor.

## Value Normalization And Comparison (v1)

- Source of truth for channel value type is `Channel.dataType`.
- Adapters MUST normalize outbound values (`channelStateUpdated`, `channel.lastValue`) to that type.
- For `ChannelDataType::Bool`, adapters MUST emit canonical booleans (`true`/`false`) or numeric `0/1`.
- String aliases such as `"on"`, `"off"`, `"yes"`, `"no"` are adapter-internal input forms and SHOULD be
  normalized before emitting to core.

Comparison semantics (core/runtime):

- Bool compare is intentionally lenient:
  - non-zero integer == `true`, zero == `false`
  - case-insensitive string aliases map to bool (`"true"/"false"`, `"on"/"off"`, `"yes"/"no"`, `"1"/"0"`)
- Numeric compare remains numeric (int/float coercion as needed by compare operation).
- Enum compare remains value-based (integer identity).
- String compare should not apply implicit trim as global default.

## Coalescing, Dedupe, ACK And Result (v1)

- `cmd.channel.invoke` ACK is transport-level acceptance only (request accepted by core pipeline).
- ACK MUST NOT imply that the value was persisted, emitted, or changed.
- Every accepted command SHOULD still produce a final command result.
- Coalesced/superseded or deduped commands MUST NOT be treated as transport errors.
  - Recommended result: `Success` with explicit reason metadata (`deduped`, `coalesced_superseded`).

Core-side dedupe behavior:

- If incoming state equals the last known channel value (by central type-aware compare), core does not fan out
  a `channel.stateChanged` event and does not write history samples.
- Equal-value updates are not considered failures.
- History is append-only for changed reportable values; unchanged values are intentionally skipped.

## Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

## Runtime Linking (.so)

- `phi::adapter-sdk` is shipped as `libphi_adapter_sdk.so`.
- Sidecar executables must be able to resolve this library at runtime.
- Supported deployment patterns:
  - install library into a system loader path (`/usr/lib`, `/usr/local/lib`, ...)
  - ship library with adapter bundle and configure `RPATH` (for example `$ORIGIN/../../../`)

## Recommended C++ Model

`AdapterFactory` is the plugin-level runtime base class.
`AdapterInstance` is the per-adapter-instance runtime base class.
`SidecarHost` wires IPC transport and handler dispatch.
`AdapterFactory::pluginType()` must match the adapter plugin type used by phi-core.

### Runtime Class Contract (v1, normative)

- `SidecarHost` is the only dispatcher/lifecycle owner and MUST be declared `friend` for
  both factory and instance runtime classes.
- Host-triggered entry points are `private` and SDK-owned (NVI): adapter code must not call
  them directly.
- Private host entry points run invariant checks, common logging/enrichment, and then call the
  adapter's virtual hooks.
- Adapter implementers override natural hook names (`start()`, `stop()`, `restart()`, ...)
  and MUST NOT implement or depend on `do*` naming.
- Factory plane is strict `externalId == ""`; instance plane is strict `externalId != ""`.
- Exactly one sidecar runtime process exists per adapter `pluginType`.
- A sidecar runtime may host multiple adapter instances; each instance should run in its own
  worker execution context/thread while IPC transport stays host-serialized.

Factory methods (v1 SDK contract):

- `pluginType()`
- `displayName()`, `description()`, `apiVersion()`, `iconSvg()`, `imageBase64()`
- `timeoutMs()`, `maxInstances()`, `capabilities()`, `configSchemaJson()`
- `descriptor()` (default build from first-class overrides)
- `onBootstrap(...)`
- `onFactoryConfigChanged(...)`
- `onFactoryActionInvoke(...)`
- `createInstanceExecutionBackend(externalId)` (optional override for custom threading/event loop)
- `createInstance(...)`, `destroyInstance(...)`

Instance methods (v1 SDK contract):

- lifecycle: `start()`, `stop()`, `restart()`
- runtime: `onConfigChanged(...)`
- command handlers are queued/asynchronous: `onChannelInvoke(...)`, `onAdapterActionInvoke(...)`,
  `onDeviceNameUpdate(...)`, `onDeviceEffectInvoke(...)`, `onSceneInvoke(...)`
- outbound events: `send*` helpers from SDK base class
- command/action completion is explicit via `sendResult(...)` helpers

Logging API (v1 SDK contract):

- `log(...)` is a public method on factory and instance classes for adapter implementers.
- Signature is `log(level, category, message, ...)`.
- SDK enriches and forwards logs to core automatically; adapters should only provide semantic
  message/context fields.
- Mandatory normalized fields are SDK-managed: `tsMs`, `level`, `category`, `message`, `pluginType`,
  `externalId` (empty for factory scope).
- `ctx` is translation context (for translation engines), not source/module context.
- `params` are placeholder replacements for `%1`, `%2`, ... in `message`.
- Source/module information belongs into structured `fieldsJson` (for example `{"source":"poll"}`).
- Optional fields may include `adapterId` and structured metadata.
- Reserved source-location fields for debug/trace logs: `file`, `line`, `func`.
- Use SDK macros for automatic source-location enrichment:
  - `PHI_LOG_DEBUG(target, category, message, ctx, params)`
  - `PHI_LOG_TRACE(target, category, message, ctx, params)`
  - `PHI_LOG_WITH_SOURCE(target, level, category, message, ctx, params)`
- `params` in macros is a `ScalarList` expression, e.g.
  `phi::ScalarList{"bridge-1", 3000}`.
- SDK log forwarding is gated by adapter flags from config:
  - when `AdapterFlagEnableLogs` is absent, `log(...)` is suppressed for
    `Trace`/`Debug`/`Info`/`Warn`
  - `Error` is always forwarded to core, independent of log flag state
  - when `AdapterFlagEnableLogs` is set, SDK applies `adapter.meta.logging` filter:
    - `logging.minLevel`: one of `trace|debug|info|warn|error` (default: `debug`)
    - `logging.categories`: string array (default: `["all"]`)
    - supported categories: `event`, `lifecycle`, `discovery`, `network`, `protocol`,
      `deviceState`, `config`, `performance`, `security`, `internal`
    - `["all"]` enables all categories
- `sendError(...)` always emits:
  - `EventError` (primary incident event)
  - mirrored `EventLog` with `level=Error` and `category=Event`
  - mirrored log metadata fields: `{"source":"event.error"}`
- Canonical categories:
  `Event`, `Lifecycle`, `Discovery`, `Network`, `Protocol`, `DeviceState`, `Config`,
  `Performance`, `Security`, `Internal`.
- Central SDK policy applies to all adapters (rate limiting/size limits/UTF-8 normalization/
  redaction); no adapter-specific fallback logging paths.

Cmd/Action Results (NVI, mandatory):

- Host dispatch uses private NVI entry points for command/action processing.
- SDK runtime uses asynchronous queued processing for `Cmd*`/`Action*`:
  - host accepts request and enqueues it to the target instance execution context
  - adapter runtime completes later via explicit `sendResult(...)`
  - host sends correlated `Result*` back to core on the host send path
- Direct "fast-path" completion is allowed only as a degenerate async case:
  - handler may compute result immediately and call `sendResult(...)` without additional wait
  - result still traverses the same host-owned queue/send path
  - no direct worker-thread IPC writes and no blocking remote I/O in handler fast-path
- Return-value based command/action completion is non-compliant for the v1 contract model.
- Each accepted `Cmd*`/`Action*` request produces exactly one correlated `Result*`.
- `cmdId` correlation is host-managed and always echoed.
- SDK/host normalizes responses (required fields, `status`, `tsMs`, kind-specific payload).
- Error mapping is centralized in SDK/host; adapter hooks must not block on remote I/O.
- Adapter code must not emit raw `Result*` frames directly.

Result dispatch flow (normative):

1. `HostThread` receives `Cmd*`/`Action*` and routes by `externalId`.
2. Target instance execution context processes request.
3. Instance publishes completion via `sendResult(...)` (cmd/action variant).
4. SDK enqueues completion to host result queue (thread-safe).
5. `HostThread` drains queue and emits correlated `Result*` IPC frame to phi-core.

Concurrency model (v1, mandatory):

- `HostThread` is the sidecar main thread that runs `SidecarHost::pollOnce(...)`.
- IPC read/write and frame dispatch run on `HostThread`.
- Exactly one runtime process exists per `pluginType`.
- One runtime process hosts factory scope and all instance scopes for that `pluginType`.
- Each adapter instance (`externalId`) runs in its own execution context.
- Default SDK execution context is a dedicated worker thread per instance.
- Factory may override `createInstanceExecutionBackend(externalId)` to provide a custom backend
  (for example Qt event-loop execution).
- `createInstance(externalId)` creates runtime object; SDK owns execution lifecycle.
- `SyncAdapterInstanceRemoved` stops execution context and destroys the instance.
- `send*`, `log`, `sendError`, and `sendResult` are thread-safe enqueue APIs.
- IPC write/dispatch to core MUST be serialized through one host-owned send path.
- Worker threads MUST not emit IPC frames directly.
- Worker threads enqueue events/results; `HostThread` drains queues and sends frames.
- Per-instance outbound ordering is FIFO and deterministic.
- Backpressure policy:
  - queues are bounded per instance
  - drop first: low-priority logs (`Trace`/`Debug`)
  - never drop: `EventError`
  - coalescing of rapid state updates is SDK-controlled
- Timeout policy:
  - pending command timeout is tracked by host
  - default timeout source is `AdapterFactory::timeoutMs()` (plugin-level)
  - timeout returns correlated `Result*` with timeout status
  - late worker results after timeout are dropped with debug log

Optional Qt event loop model (v1, allowed):

- Adapter builders may run a Qt event loop via custom `InstanceExecutionBackend`.
- Dispatcher remains independent in host runtime; instance execution may use Qt or non-Qt internals.
- Command/action handlers should enqueue work to the instance loop and return immediately.
- Immediate completion is allowed when no asynchronous wait is needed; even then, completion
  must be emitted via `sendResult(...)` and host queue dispatch.
- Completion must still happen via `sendResult(...)` (thread-safe), never by direct IPC writes.

### Naming Rules

- Inbound request handlers:
  - factory scope: `onBootstrap`, `onFactoryActionInvoke`
  - instance scope: `onConfigChanged`, `onChannelInvoke`, `onAdapterActionInvoke`, ...
- Outbound IPC calls: `send*` (`sendDeviceUpdated`, `sendChannelStateUpdated`, `sendError`, ...)
- Static descriptor overrides: `displayName()`, `description()`, `iconSvg()`, `imageBase64()`,
  `apiVersion()`, `timeoutMs()`, `maxInstances()`, `capabilities()`, `configSchemaJson()`

### Minimal Structure

```cpp
namespace phi = phicore::adapter::sdk;

class MyInstance final : public phi::AdapterInstance {
protected:
    bool start() override {
        return true;
    }
    void onConfigChanged(const phi::ConfigChangedRequest &request) override {
        (void)request;
    }
};

class MyFactory final : public phi::AdapterFactory {
protected:
    phi::Utf8String pluginType() const override { return "my-plugin"; }
    std::unique_ptr<phi::AdapterInstance> createInstance(
        const phi::ExternalId &externalId) override {
        (void)externalId;
        return std::make_unique<MyInstance>();
    }
};

MyFactory factory;
phi::SidecarHost host(socketPath, factory);
host.start();
while (running) {
    host.pollOnce(std::chrono::milliseconds(250));
}
host.stop();
```

## Example Binary

`phi_adapter_sidecar_example` demonstrates `AdapterFactory` + `AdapterInstance` + `SidecarHost`.

```bash
./build/phi_adapter_sidecar_example /tmp/phi-adapter-example.sock
```

## Adapter IPC Command Model (v1)

Naming rules:

- `Sync*`: core -> adapter, no response.
- `Cmd*`: core -> adapter, always followed by `Result*`.
- `Event*`: adapter -> core, unsolicited runtime/topology events.
- `Result*`: adapter -> core, correlated response to one `Cmd*`.

Canonical enum: `phicore::adapter::v1::IpcCommand` in
`phi/adapter/v1/ipc_command.h`.
Canonical payload contract for every `IpcCommand` is defined in
`phi-transport-api/PROTOCOLL.md` section `6.4`.

Core -> Adapter (`Sync*` / `Cmd*`):

- `SyncAdapterBootstrap` (`0x0101`)
- `SyncAdapterConfigChanged` (`0x0102`)
- `SyncAdapterInstanceRemoved` (`0x0103`)
- `CmdChannelInvoke` (`0x0201`)
- `CmdAdapterActionInvoke` (`0x0202`)
- `CmdDeviceNameUpdate` (`0x0203`)
- `CmdDeviceEffectInvoke` (`0x0204`)
- `CmdSceneInvoke` (`0x0205`)

Adapter -> Core (`Event*`):

- `EventFactoryDescriptor` (`0x1001`)
- `EventFactoryDescriptorUpdated` (`0x1002`)
- `EventAdapterMetaUpdated` (`0x1003`)
- `EventConnectionStateChanged` (`0x1004`)
- `EventError` (`0x1005`)
- `EventLog` (`0x1006`)
- `EventDeviceUpdated` (`0x1101`)
- `EventDeviceRemoved` (`0x1102`)
- `EventChannelUpdated` (`0x1201`)
- `EventChannelStateUpdated` (`0x1202`)
- `EventRoomUpdated` (`0x1301`)
- `EventRoomRemoved` (`0x1302`)
- `EventGroupUpdated` (`0x1401`)
- `EventGroupRemoved` (`0x1402`)
- `EventSceneUpdated` (`0x1501`)
- `EventSceneRemoved` (`0x1502`)
- `EventFullSyncCompleted` (`0x1FFF`)

Adapter -> Core (`Result*`):

- `ResultCmd` (`0x2001`)
- `ResultAction` (`0x2002`)

## Target Resolution (v1, strict)

- IPC target routing is resolved only by `externalId`.
- `externalId == ""` targets factory scope.
- `externalId != ""` targets one concrete instance scope.
- Do not use `adapterId` or `scope` in sidecar IPC payloads.
- Unknown/non-existent `externalId` must fail explicitly (`NotFound`/`InvalidArgument`).

## Factory And Instance Planes (v1)

- Factory plane (`externalId == ""`):
  - plugin-level descriptor/capabilities/schema and factory actions
  - no device/channel topology events
- Instance plane (`externalId != ""`):
  - device/channel/room/group/scene runtime and command handling
  - state, topology and sync-completion events
- Runtime model is strict v1:
  - exactly one sidecar process per `pluginType`
  - one process hosts factory and all instances for that `pluginType`
  - instance execution may be threaded; IPC routing stays strict by `externalId`

## Bootstrap Descriptor

On `sync.adapter.bootstrap`, `SidecarHost` automatically responds with `kind=factoryDescriptor`.
The payload is built from `AdapterFactory::descriptor()` (default implementation aggregates the
first-class override methods listed above).
`factoryDescriptor` is host-managed and not intended to be sent manually by adapter code.
The descriptor payload is complete (name/description/apiVersion/icon/image/capabilities/schema/...),
not a partial field patch.

## Runtime Config Updates (v1)

- `sync.adapter.bootstrap` is factory-plane handshake (`externalId == ""`) and includes
  `staticConfig` (`<pluginType>-config.json`) from phi-core.
- Factory code must be fully functional right after bootstrap using this `staticConfig`.
- Effective runtime configuration is delivered via `sync.adapter.config.changed`.
- `sync.adapter.config.changed` does not carry static adapter config in v1.
- `sync.adapter.config.changed` is dual-scope:
  - `externalId == ""`: factory scope (`onFactoryConfigChanged(...)`)
  - `externalId != ""`: instance scope (`onConfigChanged(...)`)
- phi-core may send an initial factory `config.changed` right after bootstrap
  (for runtime policy fields like logging).
- Subsequent `config.changed` messages are sent whenever runtime config changes
  (for example host re-resolve to a new DHCP IP).
- Adapter runtimes must not read `<pluginType>-config.json` directly from disk.
  Static config source-of-truth is the bootstrap/config payload from phi-core.
- Changes in `<pluginType>-config.json` require adapter process restart/re-bootstrap to take effect.
- Sidecars should consume runtime network endpoints from `config().adapter.ip`.
- Adapter-local DNS resolution is forbidden for runtime I/O paths (polling, channel invoke,
  event streams, reconnect loops).
- Exception: explicit factory probe/test actions (for example `id="probe"`) may resolve a
  user-provided host value to validate connectivity before apply.

## Runtime Binary Replacement (v1)

- Adapter binaries are replaceable independently of `phi-core` runtime.
- `start`/`stop`/`restart` are instance lifecycle operations; plugin generation activation is
  controlled by core plugin reload flow (`cmd.adapter.reload`).
- Sidecars must expose one strict v1 descriptor per generation (`configSchema`,
  `capabilities`, discovery data) without legacy key aliases.
- New/old adapter generations may coexist during controlled rolling restarts.
- On contract incompatibility, fail fast (reject start/reload) instead of fallback behavior.

## Discovery Queries From Static Adapter Config (v1)

- Discovery provider queries are defined in static adapter config `<pluginType>-config.json`.
- `phi-core` parses the top-level `discovery` array from that file and dispatches queries to
  discovery providers (`mdns`, `ssdp`, ...).
- This static discovery config is the single source of truth for discovery behavior.
- Runtime adapter values delivered via `sync.adapter.config.changed` are separate and do not
  replace static discovery query definitions.

Supported discovery object fields:

- `kind`: discovery backend kind (`"mdns"`, `"ssdp"`, `"netscan"`, `"manual"`).
- `mdnsServiceType`: required for `kind="mdns"`.
- `ssdpSt`: required for `kind="ssdp"`.
- `defaultPort`: optional default port used when provider response has no usable port.
- `hints`: optional JSON object passed through to core discovery logic.
  - Example: `portOverride`, `portOverrideOnlyIfDiscoveredPortIn`.

Strict v1 rules:

- No key aliases, no implicit remapping, no legacy fallback names.
- Adapter README/schema/action keys and static discovery keys must be explicit and stable.
- Renaming/removing public discovery keys is a breaking v1 change.

Minimal static discovery config example:

```json
{
  "discovery": [
    {
      "kind": "mdns",
      "mdnsServiceType": "_example._tcp",
      "defaultPort": 12345,
      "hints": {
        "portOverride": 12345,
        "portOverrideOnlyIfDiscoveredPortIn": [80]
      }
    }
  ]
}
```

## Factory Actions (v1)

- There is no default UI/core fallback for adapter factory actions.
- If an adapter needs `Test connection`, it must expose action `id="probe"` in
  `capabilities().factoryActions`.
- The adapter must implement `onFactoryActionInvoke(...)` for that action id.
- Factory target is selected by empty `externalId`.
- Instance actions use non-empty `externalId`.
- Keep factory/instance actions in descriptor+schema, not in legacy capability fallbacks.

## Action Result Form Patch (v1)

To avoid form state loss on async action+reload flows, `ActionResponse` supports optional
structured form patch fields in addition to `resultType/resultValue`:

- `formValuesJson`: JSON object with field values to apply to the open action form
  (example: `{"trackedMacs":["aa:bb:...","cc:dd:..."]}`).
- `fieldChoicesJson`: JSON object mapping field keys to choice arrays
  (example: `{"trackedMacs":[{"value":"...","label":"..."}]}`).
- `reloadLayout`: optional boolean hint; when `true`, UI/core may re-request action layout.

Rules:

- Keep schema static; patch values/choices dynamically through action result.
- For actions that mutate selectable lists (`probe`, `browse`, discovery-style actions),
  return both `formValuesJson` and `fieldChoicesJson` in one response.
- Do not encode these patches into scalar `resultValue`; use structured patch fields.
- This pattern is generic and must work for any adapter action form, not only settings dialogs.

## Schema Handling (v1)

- Adapter config schema is part of the first-class descriptor field `configSchema`.
- Implement schema via `AdapterFactory::configSchemaJson()` as UTF-8 JSON object text.
- Return an object (`{...}`), not arrays/scalars.
- Keep schema keys stable across releases; treat key renames/removals as breaking changes.
- Use `sendFactoryDescriptorUpdated()` when static descriptor data changes at runtime.
  This sends the full current `factoryDescriptor()` (built from `descriptor()`).
- Do not send static schema/icon/description/displayName through `sendAdapterMetaUpdated(...)`.
- Use `sendAdapterMetaUpdated(...)` only for dynamic runtime metadata.

### Bootstrap Flow

1. phi-core sends `sync.adapter.bootstrap` with `staticConfig`.
2. SDK host responds with `kind=factoryDescriptor` (includes `configSchema`).
3. Optional: phi-core sends `sync.adapter.config.changed` (factory scope) for runtime policy/config.
   `staticConfig` updates are not part of this message in v1.
4. phi-core persists descriptor fields and exposes schema to UI/settings.
5. Optional runtime descriptor updates are sent via `kind=factoryDescriptorUpdated`.

### Action Form Patch Example

`browseHosts` action returning updated choices and retained selection:

```cpp
phicore::adapter::v1::ActionResponse resp;
resp.id = request.cmdId;
resp.status = phicore::adapter::v1::CmdStatus::Success;
resp.resultType = phicore::adapter::v1::ActionResultType::None;
resp.formValuesJson =
    R"json({"trackedMacs":["1c:90:ff:0b:58:77","26:d2:aa:57:79:46"]})json";
resp.fieldChoicesJson =
    R"json({"trackedMacs":[{"value":"1c:90:ff:0b:58:77","label":"Zigbee (192.168.1.77)"},{"value":"26:d2:aa:57:79:46","label":"Phone (192.168.1.76)"}]})json";
resp.reloadLayout = false;
phicore::adapter::v1::Utf8String err;
sendResult(resp, &err);
```

### Minimal Schema Example

```cpp
phicore::adapter::v1::JsonText configSchemaJson() const override {
    return R"json({
      "type": "object",
      "properties": {
        "host": { "type": "string", "title": "Host" },
        "port": { "type": "integer", "title": "Port", "minimum": 1, "maximum": 65535 },
        "forcedPort": { "type": "integer", "title": "Forced Port", "minimum": 1, "maximum": 65535 }
      }
    })json";
}
```
