# Adapter Sidecar Protocol Contract (v1)

This document defines the adapter-sidecar IPC contract between `phi-core` and
adapter runtimes built on `phi-adapter-sdk`.

Scope:
- Unix socket/frame protocol between core and adapter sidecar
- `IpcCommand` payloads and direction
- lifecycle, topology, logging, stream, and result semantics

Non-scope:
- external transport wire protocol (`phi-transport-api/PROTOCOLL.md`)
- UI behavior

## 1. Framing

Transport:
- local socket IPC
- canonical command enum: `phicore::adapter::v1::IpcCommand`
- canonical frame type enum: `phicore::adapter::v1::MessageType`

Message classes:
- `Request`
  - core -> adapter for `Sync*` and `Cmd*`
- `Response`
  - adapter -> core for `ResultCmd` / `ResultAction`
- `Event`
  - adapter -> core for unsolicited runtime/topology/log/stream events

Correlation:
- `cmdId`
  - assigned by phi-core
  - required for all `Cmd*` / `Result*` flows
- `correlationId`
  - frame-level transport correlation
  - present on sync/config/bootstrap style requests
- `externalId`
  - strict target routing key
  - `""` = factory scope
  - non-empty = one concrete adapter instance

Hard rules:
- `Sync*` commands do not produce `Result*`
- `Cmd*` commands always complete via exactly one correlated `ResultCmd` or `ResultAction`
- target routing is resolved only by `externalId`
- top-level `adapterId` and `scope` are not routing keys in sidecar IPC

## 2. IPC Commands

### 2.1 Core -> Adapter

| Command | Hex | Type | Scope | Required payload fields | Optional payload fields |
| --- | --- | --- | --- | --- | --- |
| `SyncAdapterBootstrap` | `0x0101` | `Request` | factory | `adapterId:int`, `adapter:object` | `cmdId:uint64`, `externalId:string`, `pluginType:string`, `staticConfig:json` |
| `SyncAdapterConfigChanged` | `0x0102` | `Request` | factory or instance | `adapterId:int`, `adapter:object` | `cmdId:uint64`, `externalId:string`, `pluginType:string`, `staticConfig:json` |
| `SyncAdapterInstanceRemoved` | `0x0103` | `Request` | instance | `adapterId:int`, `pluginType:string`, `externalId:string` | `cmdId:uint64` |
| `CmdChannelInvoke` | `0x0201` | `Request` | instance | `cmdId:uint64`, `externalId:string`, `deviceExternalId:string`, `channelExternalId:string`, `value:any-json` | none |
| `CmdAdapterActionInvoke` | `0x0202` | `Request` | factory or instance | `cmdId:uint64`, `externalId:string`, `actionId:string` | `params:object` |
| `CmdDeviceNameUpdate` | `0x0203` | `Request` | instance | `cmdId:uint64`, `externalId:string`, `deviceExternalId:string`, `name:string` | none |
| `CmdDeviceEffectInvoke` | `0x0204` | `Request` | instance | `cmdId:uint64`, `externalId:string`, `deviceExternalId:string` | `effect:int`, `effectId:string`, `params:object` |
| `CmdSceneInvoke` | `0x0205` | `Request` | instance | `cmdId:uint64`, `externalId:string`, `sceneExternalId:string`, `action:string` | `groupExternalId:string` |
| `CmdAdaptersStreamStart` | `0x0206` | `Request` | instance | `cmdId:uint64`, `externalId:string`, `kind:string` | `params:object` |
| `CmdAdaptersStreamStop` | `0x0207` | `Request` | instance | `cmdId:uint64`, `externalId:string`, `streamId:string` | none |

Notes:
- `SyncAdapterBootstrap` and `SyncAdapterConfigChanged` are normalized by the SDK into typed
  `BootstrapRequest` / `ConfigChangedRequest`.
- `staticConfig` is raw JSON text.
- `CmdAdaptersStreamStart` / `CmdAdaptersStreamStop` are internal adapter-sidecar IPC only.
  External transport clients use `cmd.stream.start` / `cmd.stream.stop` against phi-core.

### 2.2 Adapter -> Core Responses / Events

| Command | Hex | Type | Scope | Required payload fields | Optional payload fields |
| --- | --- | --- | --- | --- | --- |
| `ResponseFactoryDescriptor` | `0x1001` | `Response` | factory | `externalId:string`, `descriptor:object` | none |
| `EventFactoryDescriptorUpdated` | `0x1002` | `Event` | factory | `externalId:string`, `descriptor:object` | none |
| `EventAdapterMetaUpdated` | `0x1003` | `Event` | factory or instance | `externalId:string`, `metaPatch:object` | none |
| `EventConnectionStateChanged` | `0x1004` | `Event` | instance | `externalId:string`, `connected:bool` | none |
| `EventLog` | `0x1005` | `Event` | factory or instance | `externalId:string`, `plugin:string`, `level:string`, `category:uint8`, `message:string`, `ctx:string`, `params:array`, `fields:object`, `tsMs:int64` | none |
| `EventDeviceUpdated` | `0x1101` | `Event` | instance | `externalId:string`, `device:object`, `channels:array` | none |
| `EventDeviceRemoved` | `0x1102` | `Event` | instance | `externalId:string`, `deviceExternalId:string` | none |
| `EventChannelUpdated` | `0x1201` | `Event` | instance | `externalId:string`, `deviceExternalId:string`, `channel:object` | none |
| `EventChannelStateUpdated` | `0x1202` | `Event` | instance | `externalId:string`, `deviceExternalId:string`, `channelExternalId:string`, `value:any-json`, `tsMs:int64` | none |
| `EventRoomUpdated` | `0x1301` | `Event` | instance | `externalId:string`, `room:object` | none |
| `EventRoomRemoved` | `0x1302` | `Event` | instance | `externalId:string`, `roomExternalId:string` | none |
| `EventGroupUpdated` | `0x1401` | `Event` | instance | `externalId:string`, `group:object` | none |
| `EventGroupRemoved` | `0x1402` | `Event` | instance | `externalId:string`, `groupExternalId:string` | none |
| `EventSceneUpdated` | `0x1501` | `Event` | instance | `externalId:string`, `scene:object` | none |
| `EventSceneRemoved` | `0x1502` | `Event` | instance | `externalId:string`, `sceneExternalId:string` | none |
| `EventStreamOpen` | `0x1601` | `Event` | instance | `externalId:string`, `streamId:string`, `cmd:string`, `kind:string`, `contentType:string`, `meta:object` | `contentEncoding:string` |
| `EventStreamData` | `0x1602` | `Event` | instance | `externalId:string`, `streamId:string`, `cmd:string`, `seq:int64`, `tsMs:int64`, `payload:object` | none |
| `EventStreamError` | `0x1603` | `Event` | instance | `externalId:string`, `streamId:string`, `cmd:string`, `error:object` | none |
| `EventStreamEnd` | `0x1604` | `Event` | instance | `externalId:string`, `streamId:string`, `cmd:string`, `reason:string` | none |

Notes:
- `ResponseFactoryDescriptor` is host-managed bootstrap descriptor emission returned as a correlated `Response` frame.
- `EventFactoryDescriptorUpdated` is used when static descriptor data changes at runtime.
- topology events are instance-scoped only.

### 2.3 Adapter -> Core Results

| Command | Hex | Type | Scope | Required payload fields | Optional payload fields |
| --- | --- | --- | --- | --- | --- |
| `ResultCmd` | `0x2001` | `Response` | instance | `cmdId:uint64`, `status:int`, `error:string`, `errorCtx:string`, `errorParams:array`, `finalValue:any-scalar-or-null`, `tsMs:int64` | none |
| `ResultAction` | `0x2002` | `Response` | factory or instance | `cmdId:uint64`, `status:int`, `error:string`, `errorCtx:string`, `errorParams:array`, `resultType:int`, `resultValue:any-scalar-or-null`, `tsMs:int64` | `formValues:object`, `fieldChoices:object`, `reloadLayout:bool` |

Rules:
- every accepted `Cmd*` request produces exactly one correlated result
- command-like handlers use `ResultCmd`
- action handlers use `ResultAction`
- return-value based completion is not part of v1; completion is explicit via `sendResult(...)`

## 3. Logging Contract

Logging for adapter runtimes is part of the sidecar contract.

Required behavior:
- adapter runtime logs use the SDK `log(...)` API and are emitted as `EventLog`
- adapter implementations do not use `std::cerr`, `fprintf(stderr, ...)`, `qWarning()`,
  `qDebug()`, or similar stderr-style output as their normal runtime logging channel
- host/runtime code may use `stderr` only for:
  - pre-bootstrap startup failures before structured logging is available
  - dispatcher/socket unavailable conditions
  - fatal or otherwise unrecoverable host/runtime failures
  - failed emission of a primary incident submitted through `sendError(...)`
  - rate-limited host summaries for queue/backpressure or dropped/failed diagnostic log emission
- SDK applies the effective log filter before IPC emission
- phi-core provides the effective logging configuration; SDK enforces it locally
- `Error` logs are never suppressed by normal enable/min-level/category filtering

Incident vs. diagnostics:
- socket-level structured logging uses a single `EventLog` payload model
- `sendError(...)` is a convenience API for primary adapter incidents and emits the same
  structured log model as `log(...)`
- SDK signatures are normalized as:
  - `log(level, category, message, params, ctx, fieldsJson, tsMs, error)`
  - `sendError(category, message, params, ctx, fieldsJson, tsMs, error)`
- `log(...)` is the structured diagnostics/telemetry/operator-visibility channel
- `sendError(...)` is used for adapter incidents that must be visible as core adapter errors
  and may be consumed by automation/notification/error-handling flows
- when a failure is a primary adapter incident, adapter code MUST use `sendError(...)`
  and MUST NOT emit an additional `log(...)` for the same incident
- host does not mirror `sendError(...)`; core may interpret incident-marked `EventLog`
  frames as automation-relevant adapter errors

Ownership:
- host/runtime code owns logging for host-managed lifecycle and protocol concerns:
  - sidecar process start/stop
  - core <-> sidecar socket connect/disconnect
  - bootstrap/config dispatch on host layer
  - protocol framing/decode failures on host layer
  - outbound IPC send failures
  - queue growth, backpressure, and send-queue pressure summaries
  - missing handler / default-not-implemented on host layer
  - instance created/destroyed by host runtime
- adapter implementations own logging for adapter-domain/runtime concerns:
  - external integration connect/disconnect
  - semantic config normalization/validation
  - discovery execution/results/failures for devices/resources within the adapter domain
  - command/action execution decisions and failures
  - persistent external communication failures
  - domain state transitions
- adapter implementations MUST NOT duplicate host-owned incidents
- when `sendError(...)` emission itself fails, host/runtime MUST emit a fallback `stderr` line
  describing the lost incident
- when normal diagnostic `log(...)` emission fails repeatedly, host/runtime SHOULD emit
  rate-limited `stderr` summaries instead of one line per failed log frame
- host/runtime diagnostics about dispatcher/socket/backpressure/send-path health MUST NOT depend
  on `EventLog` delivery, because the degraded upstream path is often the failing component

Required level usage:
- `Trace` MUST be used for:
  - channel state updates
  - polling cycles
  - repeated inbound event traffic
  - fine-grained protocol chatter
  - retry loop iterations
- `Debug` MUST be used for:
  - config normalization decisions
  - discovery matching decisions within the adapter domain
  - command/action dispatch decisions
  - backoff/retry decisions
  - non-trivial internal state transitions
- `Info` MUST be used for:
  - successful external target connect
  - successful external target disconnect
  - adapter-domain startup/initialization completed
  - discovery completed summary within the adapter domain
  - resync/snapshot completed summary
- `Warn` MUST be used for:
  - recoverable external communication issues
  - malformed external data handled gracefully
  - partial update/application failures
  - degraded but still running behavior
- `Error` MUST be used for:
  - command/action execution failures
  - persistent connection failures
  - invalid configuration preventing operation
  - unrecoverable external API/protocol failures
  - failed event/result submission from adapter code

Prohibitions:
- high-frequency paths MUST NOT log above `Trace` by default
- adapter logs MUST NOT include secrets, tokens, or passwords in `message`, `params`, or `fields`
- `ctx` MUST NOT be used for module/source naming

Normalized `EventLog` fields:
- `tsMs`
- `level`
- `category`
- `message`
- `ctx`
- `params`
- `plugin`
- `externalId`
- optional `fields`

Category wire encoding:
- adapter code uses `LogCategory` enum values
- on the socket, `category` is encoded as `uint8`
- lower 7 bits hold the base public category value
- bit `0x80` marks an incident emitted through `sendError(...)`
- normal `log(...)` frames MUST NOT set the incident bit
- `sendError(...)` MUST set the incident bit and MUST use `level=Error`
- core decodes:
  - `baseCategory = category & 0x7f`
  - `isIncident = (category & 0x80) != 0`
- category text names are documentation-only and not transmitted over the adapter IPC wire

Field rules:
- `ctx` is translation context, not module/source naming
- source/module details belong into structured `fields`
- source-location fields such as `file`, `line`, `func` are reserved for debug/trace enrichment
- `fields` is reserved for structured diagnostic context and MUST NOT duplicate top-level fields
- forbidden keys in `fields`:
  - `level`
  - `category`
  - `message`
  - `ctx`
  - `params`
  - `plugin`
  - `externalId`
  - `tsMs`
- canonical `fields` keys:
  - `source`
  - `file`
  - `line`
  - `func`
  - `deviceId`
  - `channelId`
  - `groupId`
  - `sceneId`
  - `actionId`
  - `streamId`
  - `requestId`
  - `endpoint`
  - `host`
  - `port`
  - `attempt`
  - `durationMs`
  - `timeoutMs`
  - `statusCode`
  - `errorCode`
  - `provider`
- `fields` key naming MUST use lowerCamelCase ASCII identifiers
- `fields` values SHOULD be flat and machine-readable:
  - string
  - number
  - boolean
  - small arrays of those types
- `fields` MUST NOT contain:
  - secrets, tokens, or passwords
  - large payload blobs
  - full remote protocol responses
  - free-form message text duplicated from `message`

Canonical categories:
- `lifecycle`
- `discovery`
- `network`
- `protocol`
- `device`
- `config`
- `performance`
- `security`
- `internal`

Reserved bits:
- `0x80`
  - incident marker set by `sendError(...)`

## 4. Target Resolution

Strict routing:
- `externalId == ""`
  - factory scope
- `externalId != ""`
  - one concrete adapter instance

Rules:
- do not use `adapterId` or `scope` as sidecar routing keys
- unknown/non-existent `externalId` must fail explicitly
- topology and runtime events are instance-scoped
- factory scope is for descriptor/capabilities/schema/bootstrap/factory actions only

## 5. Streams

Sidecar stream IPC is adapter-scoped and internal to the core <-> sidecar boundary.

Rules:
- external clients start streams through phi-core transport topics `cmd.stream.start` /
  `cmd.stream.stop`
- phi-core resolves transport `target.adapterId` first
- phi-core then forwards adapter-sidecar IPC as `CmdAdaptersStreamStart` /
  `CmdAdaptersStreamStop`
- adapter stream lifecycle is:
  - `EventStreamOpen`
  - `EventStreamData` (`0..n`)
  - `EventStreamEnd`
- on failure:
  - `EventStreamError`
  - then `EventStreamEnd`

## 6. References

Canonical source files:
- `include/phi/adapter/v1/ipc_command.h`
- `include/phi/adapter/v1/contract.h`
- `include/phi/adapter/sdk/sidecar.h`

Related documents:
- `phi-transport-api/PROTOCOLL.md`
  - external transport/client wire contract
- `phi-adapter-sdk/README.md`
  - SDK usage guidance and best practices
