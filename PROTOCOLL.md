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
| `EventError` | `0x1005` | `Event` | factory or instance | `externalId:string`, `message:string`, `ctx:string`, `params:array` | none |
| `EventLog` | `0x1006` | `Event` | factory or instance | `externalId:string`, `pluginType:string`, `level:string`, `category:string`, `message:string`, `ctx:string`, `params:array`, `fields:object`, `tsMs:int64` | none |
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
- SDK applies the effective log filter before IPC emission
- phi-core provides the effective logging configuration; SDK enforces it locally
- `Error` logs are never suppressed by normal enable/min-level/category filtering

Normalized `EventLog` fields:
- `tsMs`
- `level`
- `category`
- `message`
- `ctx`
- `params`
- `pluginType`
- `externalId`
- optional `fields`

Field rules:
- `ctx` is translation context, not module/source naming
- source/module details belong into structured `fields`
- source-location fields such as `file`, `line`, `func` are reserved for debug/trace enrichment

Canonical categories:
- `event`
- `lifecycle`
- `discovery`
- `network`
- `protocol`
- `deviceState`
- `config`
- `performance`
- `security`
- `internal`

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
