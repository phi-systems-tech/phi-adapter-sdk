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
  - C++ sidecar model (`AdapterSidecar`, `AdapterFactory`, `SidecarHost`)

## Scope

- Runtime transport is Linux-only (`epoll`, Unix Domain Sockets)
- No Qt dependency
- No Boost dependency
- `externalId` is the canonical adapter-domain identifier in v1 contract types
- Contract text type is `phicore::adapter::v1::Utf8String` (`std::string` alias)
- All contract text fields are UTF-8 by contract
- C++ API is the primary SDK surface for v1
- Enum string conversion (`enum_names.h`) is strict v1 canonical naming (no legacy aliases)

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

`AdapterSidecar` is the polymorphic base class for sidecar adapters.
`AdapterFactory` creates adapter instances for `SidecarHost`.
`SidecarHost` wires IPC transport and handler dispatch.
`AdapterFactory::pluginType()` must match the adapter plugin type used by phi-core.

### Naming Rules

- Inbound request handlers: `on*` (`onBootstrap`, `onChannelInvoke`, ...)
- Outbound IPC calls: `send*` (`sendDeviceUpdated`, `sendChannelStateUpdated`, `sendError`, ...)
- Static descriptor overrides: `displayName()`, `description()`, `iconSvg()`, `imageBase64()`,
  `apiVersion()`, `timeoutMs()`, `maxInstances()`, `capabilities()`, `configSchemaJson()`

### Minimal Structure

```cpp
class MyAdapter final : public phicore::adapter::sdk::AdapterSidecar {
public:
    void onBootstrap(const BootstrapRequest &request) override {
        AdapterSidecar::onBootstrap(request); // caches adapterId/pluginType/externalId
    }
};

class MyFactory final : public phicore::adapter::sdk::AdapterFactory {
public:
    phicore::adapter::v1::Utf8String pluginType() const override { return "my-plugin"; }
    std::unique_ptr<phicore::adapter::sdk::AdapterSidecar> create() const override {
        return std::make_unique<MyAdapter>();
    }
};

MyFactory factory;
phicore::adapter::sdk::SidecarHost host(socketPath, factory);
host.start();
while (running) {
    host.pollOnce(std::chrono::milliseconds(250));
}
host.stop();
```

## Example Binary

`phi_adapter_sidecar_example` demonstrates `AdapterFactory` + `AdapterSidecar` + `SidecarHost`.

```bash
./build/phi_adapter_sidecar_example /tmp/phi-adapter-example.sock
```

## IPC Methods (typed inbound)

- `sync.adapter.bootstrap`
- `sync.adapter.config.changed`
- `cmd.channel.invoke`
- `cmd.adapter.action.invoke`
- `cmd.device.name.update`
- `cmd.device.effect.invoke`
- `cmd.scene.invoke`

## IPC Events (typed outbound)

- `connectionStateChanged`, `error`, `adapterMetaUpdated`, `adapterDescriptorUpdated`
- `deviceUpdated`, `deviceRemoved`, `channelUpdated`, `channelStateUpdated`
- `roomUpdated`, `roomRemoved`, `groupUpdated`, `groupRemoved`
- `scenesUpdated`, `fullSyncCompleted`

## Bootstrap Descriptor

On `sync.adapter.bootstrap`, `SidecarHost` automatically responds with `kind=adapterDescriptor`.
The payload is built from `AdapterSidecar::descriptor()` (default implementation aggregates the
first-class override methods listed above).
`adapterDescriptor` is host-managed and not intended to be sent manually by adapter code.

## Runtime Config Updates (v1)

- `sync.adapter.bootstrap` carries adapter identity/session data.
- Effective runtime configuration is delivered via `sync.adapter.config.changed`.
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
- `cmd.adapter.action.invoke` payload may contain `factoryAdapter` values
  (`host`/`ip`/`port`/`meta`) for pre-create actions.
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
- Implement schema via `AdapterSidecar::configSchemaJson()` as UTF-8 JSON object text.
- Return an object (`{...}`), not arrays/scalars.
- Keep schema keys stable across releases; treat key renames/removals as breaking changes.
- Use `sendAdapterDescriptorUpdated(...)` when static descriptor data changes at runtime.
- Do not send static schema/icon/description/displayName through `sendAdapterMetaUpdated(...)`.
- Use `sendAdapterMetaUpdated(...)` only for dynamic runtime metadata.

### Bootstrap Flow

1. phi-core sends `sync.adapter.bootstrap`.
2. SDK host responds with `kind=adapterDescriptor` (includes `configSchema`).
3. phi-core sends `sync.adapter.config.changed`.
4. phi-core persists descriptor fields and exposes schema to UI/settings.
5. Optional runtime static updates are sent via `kind=adapterDescriptorUpdated`.

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
