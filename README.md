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

Factory methods (v1 SDK contract):

- `pluginType()`
- `displayName()`, `description()`, `apiVersion()`, `iconSvg()`, `imageBase64()`
- `timeoutMs()`, `maxInstances()`, `capabilities()`, `configSchemaJson()`
- `descriptor()` (default build from first-class overrides)
- `onBootstrap(...)`
- `onFactoryActionInvoke(...)`
- `createInstance(...)`, `destroyInstance(...)`

Instance methods (v1 SDK contract):

- lifecycle: `start()`, `stop()`, `restart()`
- runtime: `onConfigChanged(...)`
- command handlers: `onChannelInvoke(...)`, `onAdapterActionInvoke(...)`,
  `onDeviceNameUpdate(...)`, `onDeviceEffectInvoke(...)`, `onSceneInvoke(...)`
- outbound events: `send*` helpers from SDK base class

Logging API (v1 SDK contract):

- `log(...)` is a public method on factory and instance classes for adapter implementers.
- SDK enriches and forwards logs to core automatically; adapters should only provide semantic
  message/context fields.
- Mandatory normalized fields are SDK-managed: `tsMs`, `level`, `message`, `pluginType`,
  `externalId` (empty for factory scope).
- Optional fields may include `adapterId`, `ctx`, `params`, and structured metadata.
- Central SDK policy applies to all adapters (rate limiting/size limits/UTF-8 normalization/
  redaction); no adapter-specific fallback logging paths.

Cmd/Action Results (NVI, mandatory):

- Host dispatch uses private NVI entry points for command/action processing.
- Adapter command/action hooks are synchronous functions from host perspective.
- Each accepted `Cmd*` request produces exactly one correlated `Result*`.
- `cmdId` correlation is host-managed and always echoed.
- SDK/host normalizes responses (required fields, `status`, `tsMs`, kind-specific payload).
- Error mapping is centralized in SDK/host; adapter hooks return domain results.
- Adapter code must not emit raw `Result*` frames directly.

Concurrency model (v1, mandatory):

- Background work (polling, network callbacks, event streams) may run on worker threads.
- IPC write/dispatch to core MUST be serialized through one host-owned send path.
- Worker threads MUST not emit IPC frames directly.
- Worker threads should enqueue domain updates; host thread drains and sends `Event*`.
- This guarantees deterministic ordering, one-result-per-command behavior, and safe correlation.

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
- One runtime process may host factory and multiple instances; target resolution still stays strict by `externalId`.

## Bootstrap Descriptor

On `sync.adapter.bootstrap`, `SidecarHost` automatically responds with `kind=factoryDescriptor`.
The payload is built from `AdapterFactory::descriptor()` (default implementation aggregates the
first-class override methods listed above).
`factoryDescriptor` is host-managed and not intended to be sent manually by adapter code.
The descriptor payload is complete (name/description/apiVersion/icon/image/capabilities/schema/...),
not a partial field patch.

## Runtime Config Updates (v1)

- `sync.adapter.bootstrap` is factory-plane handshake (`externalId == ""`).
- Effective runtime configuration is delivered via `sync.adapter.config.changed`.
- `sync.adapter.config.changed` is instance-plane (`externalId != ""`).
- phi-core sends an initial `config.changed` right after bootstrap.
- Subsequent `config.changed` messages are sent whenever runtime config changes
  (for example host re-resolve to a new DHCP IP).
- Sidecars should consume network endpoints from `config().adapter.ip` and must not
  perform adapter-local DNS resolution.

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
- The adapter must implement `onAdapterActionInvoke(...)` for that action id.
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

1. phi-core sends `sync.adapter.bootstrap`.
2. SDK host responds with `kind=factoryDescriptor` (includes `configSchema`).
3. phi-core sends `sync.adapter.config.changed`.
4. phi-core persists descriptor fields and exposes schema to UI/settings.
5. Optional runtime static updates are sent via `kind=factoryDescriptorUpdated`.

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
return resp;
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
