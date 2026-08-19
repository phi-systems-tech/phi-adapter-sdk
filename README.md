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

## Adapter or Transport? (plane boundary)

An adapter is **southbound**: phi is the client of a device or device network,
consuming state and issuing device commands. If phi is instead the **endpoint** a
client or controller talks to - exposing phi's own devices, rooms and scenes and
accepting commands about them - that is a *transport*, and it belongs in
`phi-transport-api`.

The protocol name does not decide it, the direction does. The same protocol can
appear on both planes:

| Case | Plane |
| --- | --- |
| Subscribe to `zigbee2mqtt/...` to learn about devices | adapter (`phi-adapter-z2m`) |
| Publish phi's devices on `phi/...` and accept commands there | transport |
| Talk to `otbr-agent` to reach Thread devices | adapter |
| Appear as a Matter bridge that a controller commissions | transport |

Practical difference beyond the direction: adapters run **out of process** over a
versioned wire protocol, so they may be written in any language and keep working
across phi-core releases. Transports are in-process Qt plugins built against one
release. If you have a choice, the adapter plane is the more stable place to be.

## Scope

- Runtime transport is Linux-only (`epoll`, Unix Domain Sockets)
- No Qt dependency in this repository or package set
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

- SDK ABI/API changes are intentional in v1 cleanup; compatibility shims are not provided.
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

## Button Event Normalization Policy (v1)

For all adapters exposing `ChannelKind::ButtonEvent`:

- Emit canonical `ButtonEventCode` integer values only (no raw protocol strings).
- `Single` press behavior:
  - emit only `ShortPressRelease` after the multi-press window closes.
- Multi-press behavior:
  - emit only the aggregated event (`DoublePress`, `TriplePress`, `QuadruplePress`, `QuintuplePress`).
  - do not emit additional `ShortPressRelease` events for the same multi-press sequence.
- Long-press behavior:
  - if the underlying protocol has no explicit long-start event, adapters may synthesize `LongPress`.
  - while held, emit `Repeat` events.
  - on release, emit `LongPressRelease`.
- Avoid duplicate triggers:
  - dedupe identical raw actions within a short debounce window.
  - prefer deterministic state transitions for automations over verbose raw event mirroring.

Rationale:

- Automations stay adapter-agnostic and deterministic.
- Single-click automations are not spuriously triggered before a double/triple click is recognized.
- Hold interactions behave consistently even when vendor protocols differ.

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
cmake -S . -B ../build/phi-adapter-sdk/release-ninja -G Ninja
cmake --build ../build/phi-adapter-sdk/release-ninja --parallel
```

Qt event-loop helper:

- The Qt backend lives in the separate `phi-adapter-sdk-qt` repository.
- Install `phi-adapter-sdk-qt` / `phi-adapter-sdk-qt-dev` when an adapter needs Qt-based instance execution.

## Runtime Linking (.so)

- `phi::adapter-sdk` is shipped as `libphi_adapter_sdk.so`.
- Sidecar executables must be able to resolve this library at runtime.
- Supported deployment patterns:
  - install library into a system loader path (`/usr/lib/<multiarch-triplet>`, `/usr/local/lib`, ...)
  - ship library with adapter bundle and configure `RPATH` (for example `$ORIGIN/../../../`)

For Debian packages, install shared libraries and CMake package files under multiarch paths
(for example `usr/lib/aarch64-linux-gnu/...`) to avoid mixed/duplicate runtime library resolution.

For the package split:

- `phi-adapter-sdk` stays `Architecture: any` because it ships `libphi_adapter_sdk.so`
- `phi-adapter-sdk-dev` can be `Architecture: all` only if it contains only
  headers and arch-neutral build metadata

Recommended Debian install layout:

- runtime library: `usr/lib/<multiarch-triplet>/libphi_adapter_sdk.so*`
- headers: `usr/include/...`
- CMake package config for the `-dev` package: `usr/lib/cmake/phi-adapter-sdk`

Do not keep the `-dev` package on `usr/lib/<multiarch-triplet>/cmake/...` if the
goal is an architecture-independent `all` package.

## Unix Socket Permissions

- The SDK sidecar transport creates its unix domain socket with mode `0660` (owner/group read-write).
- Recommended runtime ownership is `phi:phi` for `/var/lib/phi` and `/var/lib/phi/ipc`.
- This keeps adapter IPC available to the service user/group without exposing write access to others.

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
  - `timeoutMs()` is adapter/device default timeout metadata (descriptor), not sidecar command timeout ownership
- `descriptor()` (default build from first-class overrides)
- `onBootstrap(...)`
- `onFactoryConfigChanged(...)`
- `onFactoryActionInvoke(...)`
- `createInstanceExecutionBackend(externalId)` (optional override for custom threading/event loop)
- `createFactoryExecutionBackend()` (optional; `nullptr` by default, see below)
- `onFactoryStopping()`
- `createInstance(...)`, `destroyInstance(...)`

Qt helper usage example (from `phi-adapter-sdk-qt`):

```cpp
#include "phi/adapter/sdk/qt/instance_execution_backend_qt.h"

std::unique_ptr<phi::InstanceExecutionBackend> createInstanceExecutionBackend(
    const phi::ExternalId &externalId) override
{
    (void)externalId;
    return phicore::adapter::sdk::qt::createInstanceExecutionBackend();
}
```

#### Factory Execution Backend (v1)

Factory hooks run on `HostThread` by default. Any blocking call there - a device probe, a
synchronous HTTP request, a nested `QEventLoop` - stalls IPC for the whole sidecar, including
every instance. A factory that blocks MUST return a backend:

```cpp
// Moves onBootstrap, onFactoryConfigChanged, onFactoryActionInvoke, onConnected,
// onDisconnected and onProtocolError onto a dedicated thread with an event loop.
std::unique_ptr<phi::InstanceExecutionBackend> createFactoryExecutionBackend() override
{
    return phicore::adapter::sdk::qt::createFactoryExecutionBackend();
}

// Objects with thread affinity must be created lazily, inside a hook, so they belong
// to the backend thread - not in the factory constructor, which runs on the main thread.
HttpClient &ensureHttp()
{
    if (!m_http) {
        m_network = std::make_unique<QNetworkAccessManager>();
        m_http = std::make_unique<HttpClient>(m_network.get());
    }
    return *m_http;
}

// ... and destroyed here, while that thread is still alive.
void onFactoryStopping() override
{
    m_http.reset();
    m_network.reset();
}
```

Rules that come with it:

- The six hooks above are serialized against each other on that backend, so factory state used
  only by them needs no locking.
- `createInstance(...)`, `destroyInstance(...)` and the descriptor accessors stay on
  `HostThread` by design - blocking on the factory thread there would reintroduce exactly the
  stall this backend removes. State shared between them and the six hooks is the adapter's
  responsibility. `bootstrap()` and the cached factory config are safe: the SDK caches them on
  `HostThread` before scheduling the hook.
- The bootstrap descriptor reply is emitted from the same task as `onBootstrap(...)`, so a
  factory that derives schema data from the static config still answers with it.
- Returning `nullptr` (the default) keeps every hook inline on `HostThread`; existing adapters
  are unaffected.

Instance methods (v1 SDK contract):

- lifecycle: `start()`, `stop()`, `restart()`
- runtime: `onConfigChanged(...)`
- command handlers are queued/asynchronous: `onChannelInvoke(...)`, `onAdapterActionInvoke(...)`,
  `onDeviceNameUpdate(...)`, `onDeviceEffectInvoke(...)`, `onSceneInvoke(...)`,
  `onAdaptersStreamStart(...)`, `onAdaptersStreamStop(...)`
- outbound events: `send*` helpers from SDK base class
- command/action completion is explicit via `sendResult(...)` helpers

Logging API (v1 SDK contract):

- `log(...)` is a public method on factory and instance classes for adapter implementers.
- Signature is `log(level, category, message, params, ctx, fieldsJson, tsMs, error)`.
- SDK enriches and forwards logs to core automatically; adapters should only provide semantic
  message/context fields.
- Mandatory normalized fields are SDK-managed: `tsMs`, `level`, `category`, `message`, `plugin`,
  `externalId` (empty for factory scope).
- `ctx` is translation context (for translation engines), not source/module context.
- `params` are placeholder replacements for `%1`, `%2`, ... in `message`.
- Source/module information belongs into structured `fieldsJson` (for example `{"source":"poll"}`).
- Optional fields may include `adapterId` and structured metadata.
- Reserved source-location fields for debug/trace logs: `file`, `line`, `func`.
- `fieldsJson` is for structured diagnostic context, not for duplicating top-level log fields.
- Do not put these top-level fields into `fieldsJson`:
  - `level`
  - `category`
  - `message`
  - `ctx`
  - `params`
  - `plugin`
  - `externalId`
  - `tsMs`
- Prefer canonical `fieldsJson` keys where applicable:
  - `source`, `file`, `line`, `func`
  - `deviceId`, `channelId`, `groupId`, `sceneId`, `actionId`, `streamId`, `requestId`
  - `endpoint`, `host`, `port`
  - `attempt`, `durationMs`, `timeoutMs`
  - `statusCode`, `errorCode`, `provider`
- `fieldsJson` keys should use lowerCamelCase ASCII naming.
- Keep `fieldsJson` flat and machine-readable; avoid large blobs and duplicated human text.
- Use SDK macros for automatic source-location enrichment:
  - `PHI_LOG_DEBUG(target, category, message, params, ctx)`
  - `PHI_LOG_TRACE(target, category, message, params, ctx)`
  - `PHI_LOG_WITH_SOURCE(target, level, category, message, params, ctx)`
- `params` in macros is a `ScalarList` expression, e.g.
  `phi::ScalarList{"bridge-1", 3000}`.
- SDK log forwarding is gated by adapter flags from config:
  - when `AdapterFlagEnableLogs` is absent, `log(...)` is suppressed for
    `Trace`/`Debug`/`Info`/`Warn`
  - `Error` is always forwarded to core, independent of log flag state
  - when `AdapterFlagEnableLogs` is set, SDK applies `adapter.meta.logging` filter:
    - `logging.minLevel`: one of `trace|debug|info|warn|error` (default: `debug`)
    - `logging.categories`: string array (default: `["all"]`)
    - supported public categories: `internal`, `lifecycle`, `discovery`, `network`, `protocol`,
      `device`, `config`, `performance`, `security`, `database`
    - `["all"]` enables all categories
- `sendError(...)` is the primary adapter incident path toward phi-core:
  - it is intended for core-visible adapter errors
  - it may be consumed by automation/notification/error-handling flows
- adapter code must not emit an additional `log(...)` for the same primary incident
  handled via `sendError(...)`
- `log(...)` is the structured diagnostics/telemetry channel and must not replace
  `sendError(...)` for primary incidents
- `sendError(...)` uses the same structured socket log model as `log(...)`
- SDK signatures are normalized as:
  - `log(level, category, message, params, ctx, fieldsJson, tsMs, error)`
  - `sendError(category, message, params, ctx, fieldsJson, tsMs, error)`
- `sendError(...)` always emits with:
  - `level = Error`
  - same public base category enum as adapter code passed in
  - incident flag set in the wire `category:uint8`
- host does not duplicate `sendError(...)`; incident interpretation happens in core
- Canonical categories:
  `Internal`, `Lifecycle`, `Discovery`, `Network`, `Protocol`, `Device`, `Config`,
  `Performance`, `Security`, `Database`.
- Wire encoding:
  - `level` is transmitted as `uint8`
  - `1=Trace`, `2=Debug`, `3=Info`, `4=Warn`, `5=Error`
  - `category` is transmitted as `uint8`
  - lower 7 bits contain the base public category
  - bit `0x80` marks an incident emitted through `sendError(...)`
  - plain `log(...)` never sets `0x80`
- Central SDK policy applies to all adapters (rate limiting/size limits/UTF-8 normalization/
  redaction); no adapter-specific fallback logging paths.

Logging contract summary:

- Adapter implementations use `log(...)` as the canonical runtime logging path.
- Adapter implementations do not use `std::cerr`, `fprintf(stderr, ...)`, `qWarning()`,
  `qDebug()`, or similar stderr-style output for normal runtime logging.
- `stderr` is reserved for host/runtime failures where structured logging is not yet available
  or no longer reliable:
  - pre-bootstrap startup failures
  - dispatcher/socket unavailable conditions
  - fatal or otherwise unrecoverable host/runtime failures
  - failed emission of a primary incident submitted through `sendError(...)`
  - rate-limited host summaries for queue/backpressure or repeated diagnostic log send failures
- SDK applies the effective log filter before IPC emission for performance reasons.
- Core provides the effective logging configuration; SDK enforces it locally.
- `Error` logs are never suppressed by normal enable/min-level/category filtering.
- Host/runtime owns logging for:
  - sidecar process start/stop
  - core <-> sidecar socket connect/disconnect
  - bootstrap/config dispatch on host layer
  - host protocol/dispatch/send failures
  - queue growth, backpressure, and send-queue pressure summaries
  - host-created / host-destroyed instance lifecycle
- Adapter code owns logging for:
  - external integration connect/disconnect
  - semantic config normalization/validation
  - discovery execution/results/failures for devices/resources within the adapter domain
  - command/action execution decisions and failures
  - persistent external communication failures
  - domain state transitions
- Adapter code must not duplicate host-owned incidents.
- If `sendError(...)` itself cannot be emitted, the host must write a fallback line to `stderr`
  so the lost incident is still visible to phi-core through process stderr capture.
- If normal `log(...)` emission fails repeatedly, the host should summarize those failures via
  rate-limited `stderr` diagnostics instead of writing one line per failed log frame.
- Host diagnostics about dispatcher/socket/backpressure/send-path health should not rely on
  `EventLog`, because the degraded upstream path is often exactly the component that is failing.

Logging best practices:

- `Trace`
  - channel state changes
  - poll cycles
  - repeated inbound device/service events
  - fine-grained protocol chatter
  - retry loop iterations
- `Debug`
  - config normalization
  - discovery matching within the adapter domain
  - command/action dispatch decisions
  - retry/backoff decisions
  - non-trivial internal state transitions
- `Info`
  - successful external target connect/disconnect
  - adapter-domain startup/initialization completed
  - discovery/resync completed summary within the adapter domain
- `Warn`
  - recoverable network/protocol issues
  - malformed external data handled gracefully
  - partial update failures
  - degraded but still running behavior
- `Error`
  - command/action failures
  - persistent connection failures
  - invalid configuration preventing operation
  - unrecoverable external API/protocol failures
  - failed event/result submission

High-frequency rules:

- high-frequency paths must not log above `Trace` by default
- especially:
  - channel state updates
  - polling loops
  - repeated event traffic
- do not log secrets, tokens, or passwords in `message`, `params`, or `fields`

Cmd/Action Results (NVI, mandatory):

- Host dispatch uses private NVI entry points for command/action processing.
- SDK runtime uses asynchronous queued processing for `Cmd*`/`Action*`:
  - host accepts request and enqueues it to the target instance execution context
  - adapter runtime completes later via explicit `sendResult(...)`
  - host sends correlated `Result*` back to core on the host send path
- A “quasi-sync fast-path” is allowed for immediate completions:
  - handler may compute result immediately and call `sendResult(...)` without additional wait
  - result still traverses the same host-owned queue/send path
  - no direct worker-thread IPC writes and no blocking remote I/O in handler fast-path
- Return-value based command/action completion is non-compliant for the v1 contract model.
- Each accepted `Cmd*`/`Action*` request produces exactly one correlated `Result*`.
- `cmdId` correlation is host-managed and always echoed.
- SDK/host normalizes responses (required fields, `status`, `tsMs`, kind-specific payload).
- Error mapping is centralized in SDK/host; adapter hooks must not block on remote I/O.
- Adapter code must not emit raw `Result*` frames directly.
- SDK does **not** own command timeout/drop policy.
  - timeout, retry, and stale-result handling are owned by phi-core adapter manager
  - sidecar SDK only dispatches inbound commands and forwards adapter-produced `Result*`

Result dispatch flow (normative):

1. `HostThread` receives `Cmd*`/`Action*` and routes by `externalId`.
2. Target instance execution context processes request.
3. Instance publishes completion via `sendResult(...)` (cmd/action variant).
4. SDK enqueues completion to host result queue (thread-safe).
5. `HostThread` drains queue and emits correlated `Result*` IPC frame to phi-core.

Outbound send path (v1 runtime behavior):

- Enqueuing outbound work (results, events, logs) wakes a `HostThread` that is
  blocked inside `pollOnce(...)` via an internal wake descriptor. Outbound
  latency does not depend on the poll timeout; long poll timeouts are safe.
- The outbound send queue is bounded (`4096` frames). On overflow the oldest
  log frame is shed first, then the oldest event frame. `Result*`/response
  frames are never shed and may exceed the cap.
- Queue drops are counted and reported via rate-limited `stderr` host
  diagnostics (`[sidecar][queueOverflow][host]`,
  `[sidecar][sendQueueDropped][host]`).
- Writing one frame to a connected peer is bounded (5s). A peer that does not
  drain the socket within that window is treated as dead: the connection is
  closed, remaining queued frames are dropped with a summary diagnostic, and
  `onDisconnected` fires on the next poll.

## Main Loop

Adapters do not hand-roll the poll loop:

- Qt-free adapters call `runSidecarMain(host, options)` (`phi/adapter/sdk/sidecar.h`),
  which starts the host, polls, and stops it. Because queued outbound work
  interrupts the poll through the internal wake descriptor, the poll timeout
  only bounds idle wakeups - it does not add latency.
- Qt adapters use `phi::qt::SidecarDriver` from `phi-adapter-sdk-qt`, which
  watches `SidecarHost::pollDescriptor()` with a `QSocketNotifier`. There is no
  polling interval at all: idle costs nothing, and the Qt event loop is never
  blocked by a poll, so adapter timers and network operations run on time.

Both replace the two older hand-written patterns (a blocking `pollOnce(250ms)`
loop that starved the Qt event loop, and a 16 ms `QTimer` that woke up 60 times
per second).

## Color Conversions

`phi/adapter/v1/color.h` carries the canonical, **Qt-free** color contract: the
`Color` type (gamma-encoded sRGB, components in `[0, 1]`) plus conversions to and
from HSV, linear RGB, XYZ and xy chromaticity, and the Kelvin/mired helpers.

Translating the canonical value into a device's native color space is an adapter
responsibility, so these conversions must not require Qt. Adapters using the Qt
wrapper get the same functions through `phi/adapter/qt/color.h`
(`phi-adapter-sdk-qt`), which only adds the Qt meta-type registration - there is
exactly one `Color` type, not two layout-compatible ones.

Device-specific behavior (for example clamping into a particular bridge gamut)
stays in the adapter.

## Socket Failure Semantics

Writes use `send(..., MSG_NOSIGNAL)`: a peer that closed the socket surfaces as
a write error, never as a `SIGPIPE` that would terminate the sidecar. The SDK
does not change the process-wide signal disposition on its own.

## Tests

`tests/` carries a ctest suite (run automatically by `dh_auto_test` during
package builds; skipped when the SDK is consumed via `add_subdirectory`):

- `sdk_runtime_tests`: outbound wakeup latency, write deadline against a
  stalled peer, send-queue cap/shed accounting, stop() interrupting a poll.
- `sdk_protocol_tests`: raw-frame behavior from the core side of the socket
  (typed request decode, result/event envelopes, default responses,
  disconnect on invalid frame headers).
- `sdk_golden_wire_tests`: golden-wire contract tests. Every outbound
  `send*` payload is compared **byte-exactly** against checked-in fixtures in
  `tests/golden/out/`; `tests/golden/in/` holds canonical core request frames
  that are decoded and asserted. Any wire envelope change fails here first.
  Intentional contract changes regenerate the outbound fixtures with
  `PHI_GOLDEN_UPDATE=1 ./sdk_golden_wire_tests` — review the diff and update
  `PROTOCOLL.md` (and phi-core) in the same change.

Shutdown budget (v1, mandatory):

- `phi::sdk::kShutdownBudget` is the total time a sidecar has to shut down. It is
  derived from phi-core, which sends `SIGTERM` and kills the process 3 s later.
- `SidecarHost::stop(budget)` treats it as one deadline and divides it: the instances
  share what is left after a reserve for the factory backend, and each instance splits
  its slice between the cooperative `stop()` and the join that follows.
- Adapter consequence: **a blocking wait in a teardown path must be cancellable or
  shorter than the instance's slice.** With two instances the slice is well under a
  second, so a 3 s HTTP timeout or a 1 s socket connect cannot be waited out.
- `AdapterInstance::stopRequested()` / `AdapterFactory::stopRequested()` is how a
  blocking path learns about the shutdown. The host sets it from its own thread the
  moment teardown begins - before `stop()` / `onFactoryStopping()` is even scheduled -
  so it is observable while the execution thread sits in a socket wait and cannot run
  queued work. Poll it between waits:

```cpp
// Chunked instead of one long wait, so the loop can give up early.
while (waitedMs < timeoutMs) {
    if (stopRequested())
        return false;
    const int slice = std::min(100, timeoutMs - waitedMs);
    if (socket.waitForReadyRead(slice))
        break;
    waitedMs += slice;
}
```

  For nested `QEventLoop` helpers, run a short poll timer that quits the loop and
  aborts the request when `stopRequested()` turns true.
- If an execution backend does not stop within its slice, the host forces it and then
  **leaks that instance on purpose**: a thread may still be running inside it, and
  `destroyInstance()` would be a use-after-free. The leak is reported as a protocol
  error - treat it as a bug in the adapter's teardown path, not as normal operation.

Concurrency model (v1, mandatory):

- `HostThread` is the sidecar main thread that runs `SidecarHost::pollOnce(...)`.
- IPC read/write and frame dispatch run on `HostThread`.
- Exactly one runtime process exists per `pluginType`.
- One runtime process hosts factory scope and all instance scopes for that `pluginType`.
- Each adapter instance (`externalId`) runs in its own execution context.
- Default SDK execution context is a dedicated worker thread per instance.
- Factory may override `createInstanceExecutionBackend(externalId)` to provide a custom backend
  (for example Qt event-loop execution).
- Factory-scope hooks run on `HostThread` unless the factory overrides
  `createFactoryExecutionBackend()`; a factory that performs blocking work MUST override it.
- Periodic instance work MUST be driven from the instance's own execution context (a timer created
  in `start()`), never from a host-thread timer calling into instances - that races with every
  queued instance callback.
- `createInstance(externalId)` creates runtime object; SDK owns execution lifecycle.
- `SyncAdapterInstanceRemoved` stops execution context and destroys the instance.
- `send*`, `log`, `sendError`, and `sendResult` are thread-safe enqueue APIs.
- IPC write/dispatch to core MUST be serialized through one host-owned send path.
- Worker threads MUST not emit IPC frames directly.
- Worker threads enqueue events/results; `HostThread` drains queues and sends frames.
- `send*` success means "enqueued for host send path", not transport-level delivery ACK.
- Per-instance outbound ordering is FIFO and deterministic.
- Backpressure policy:
  - sidecar SDK provides central host-serialized outbound queue
  - adapters should avoid flooding low-value logs/events from worker contexts
  - coalescing/dedupe/rate-limit enforcement is owned by phi-core
- Timeout policy:
  - SDK does not synthesize timeout `Result*` responses
  - phi-core is the single owner of request timeout and late-result policy

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
../build/phi-adapter-sdk/release-ninja/phi_adapter_sidecar_example /tmp/phi-adapter-example.sock
```

## Adapter IPC Command Model (v1)

Naming rules:

- `Sync*`: core -> adapter, no response.
- `Cmd*`: core -> adapter, always followed by `Result*`.
- `Event*`: adapter -> core, unsolicited runtime/topology events.
- `Result*`: adapter -> core, correlated response to one `Cmd*`.

Canonical enum: `phicore::adapter::v1::IpcCommand` in
`phi/adapter/v1/ipc_command.h`.
Canonical sidecar IPC payload contract is defined in:
- `phi-adapter-sdk/PROTOCOLL.md`

Core -> Adapter (`Sync*` / `Cmd*`):

- `SyncAdapterBootstrap` (`0x0101`)
- `SyncAdapterConfigChanged` (`0x0102`)
- `SyncAdapterInstanceRemoved` (`0x0103`)
- `CmdChannelInvoke` (`0x0201`)
- `CmdAdapterActionInvoke` (`0x0202`)
- `CmdDeviceNameUpdate` (`0x0203`)
- `CmdDeviceEffectInvoke` (`0x0204`)
- `CmdSceneInvoke` (`0x0205`)
- `CmdAdaptersStreamStart` (`0x0206`)
- `CmdAdaptersStreamStop` (`0x0207`)

Adapter -> Core (`Response*` / `Event*`):

- `ResponseFactoryDescriptor` (`0x1001`)
- `EventFactoryDescriptorUpdated` (`0x1002`)
- `EventAdapterMetaUpdated` (`0x1003`)
- `EventConnectionStateChanged` (`0x1004`)
- `EventLog` (`0x1005`)
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
- `EventStreamOpen` (`0x1601`)
- `EventStreamData` (`0x1602`)
- `EventStreamError` (`0x1603`)
- `EventStreamEnd` (`0x1604`)

Adapter -> Core (`Result*`):

- `ResultCmd` (`0x2001`)
- `ResultAction` (`0x2002`)

For v1 lifecycle and topology synchronization, completion is signaled via
`ResultCmd`/`ResultAction`; `EventFullSyncCompleted` is not part of the contract.

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
  - state and topology events; full sync completion is reported through command/action results, not events
- Runtime model is strict v1:
  - exactly one sidecar process per `pluginType`
  - one process hosts factory and all instances for that `pluginType`
  - instance execution may be threaded; IPC routing stays strict by `externalId`

UI metadata rule (v1):

- The instance metadata inspect button is a core/UI-owned generic feature.
- It is backed by adapter metadata payload (`metaAdapter` / runtime metadata), not by
  adapter action descriptors in `capabilities()`.
- Do not add a synthetic `metadata` action to `factoryActions`/`instanceActions`.
- `capabilities().*Actions` are reserved for adapter domain operations (for example
  `probe`, `resync`, discovery/maintenance commands), not UI inspect helpers.

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

### Reload / Shutdown Responsibilities (Adapter Builder Checklist)

What the SDK/host does for you:

- Owns IPC transport lifecycle (`SidecarHost`, dispatch, frame I/O).
- Routes factory/instance lifecycle messages and enforces `externalId` scope.
- Stops execution backend and destroys instances on `SyncAdapterInstanceRemoved`.
- Stops the factory execution backend on shutdown, after calling `onFactoryStopping()` on it.
- Keeps command/result correlation and host-thread send serialization.

What adapter code must provide:

- `start()` must initialize quickly and fail fast on invalid config/dependencies.
- `stop()` must be idempotent and graceful:
  - stop timers/event subscriptions/reconnect loops
  - cancel inflight network/device operations
  - release sockets/file descriptors/device sessions
- `restart()` should be equivalent to `stop()` + `start()` with clean state boundaries.
- Do not block host thread work in lifecycle hooks; use instance execution context for long work.
- Periodic work (polling) belongs to a timer the instance creates in `start()` and destroys in
  `stop()`, so it runs on the instance's own execution context.
- If an adapter uses an event-loop backend, create, stop and destroy event-loop owned resources
  inside that backend context. This includes `QObject` trees, `QTimer`, sockets, reconnect timers
  and protocol/session managers.
- Never let the host thread delete `QObject` instances that own active timers or sockets. Stop
  timers and sockets first, then destroy the object in its affinity thread.
- Do not start synchronous device polling from lifecycle teardown. Shutdown must cancel pending
  work and return boundedly.

Timeout model:

- SDK does not invent timeout responses.
- Request timeout, stale-result policy, and reload orchestration are owned by `phi-core`.
- Adapter implementations should still use bounded internal waits and cancellation to avoid
  prolonged shutdown/reload windows.

Threading checklist for adapter authors:

- Construct long-lived runtime helpers after the execution backend is active, not in the factory
  or host thread.
- Parent timers/sockets/session managers to a runtime object that lives in the execution backend.
- Route all control, polling and push handling through one serialized device/session owner.
- Keep protocol detection out of the hot poll path; persist learned protocol capabilities per
  physical endpoint where possible.
- Treat reload/restart tests as mandatory: `phi-cli adapter restart --external-id <plugin>` should
  not emit `QObject::killTimer`, `QObject::~QObject` timer warnings, backend stop timeouts or
  adapter process crashes.

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
- Do not model generic UI metadata inspection as an adapter action; metadata inspection
  remains a UI/core concern and is not part of adapter capabilities.

## Stream Kinds (v1)

- Long-running transport streams are defined in `phi-transport-api/PROTOCOLL.md`
  (`cmd.stream.start|stop`, `stream.open|data|error|end`).
- Reserved stream kinds in v1:
  - `adapter.discover`
  - `network.discover`
  - `raw.discover`
  - `adapter.log`
  - `camera.live`
- `kind` is a wire string token (not numeric enum on wire).
- Current public stream kinds implemented in core/UI/CLI:
  - `adapter.discover`
  - `network.discover`
- Reserved but not implemented as public stream flows yet:
  - `raw.discover`
  - `adapter.log`
  - `camera.live`
- `target` rules for `cmd.stream.start`:
  - `adapter.discover`: no `target`
  - `network.discover`: no `target`
  - adapter-bound stream kinds: `target.adapterId > 0` is required
- Top-level `adapterId` is not part of the `cmd.stream.start` transport contract.
- Discovery payload naming rules:
  - `adapter.discover`
    - `plugin` = adapter type
    - `provider` = discovery origin (`mdns`, `ssdp`, `manual`, ...)
    - `externalId` = external candidate id
    - `service` = provider-specific service identifier
  - `network.discover`
    - no `plugin`
    - `provider` = discovery origin
    - `externalId` = external finding id
    - `service` = provider-specific service identifier
  - public discovery payloads do not use:
    - `pluginType`
    - `discoveredExternalId`
    - `serviceType`
  - `meta` is for extra provider data only and must not duplicate top-level
    fields such as `plugin`, `provider`, `externalId`, `ip`, `port`, or `service`
- Adding a new reserved `kind` requires mirrored documentation updates in:
  - `phi-transport-api/PROTOCOLL.md` (transport wire contract)
  - `phi-adapter-sdk/PROTOCOLL.md` (adapter sidecar contract)
  - `phi-adapter-sdk/README.md` (adapter SDK guidance / best practices)
- Adapter sidecar IPC remains adapter-scoped internally:
  - phi-core resolves `target.adapterId`
  - then forwards the request to the addressed adapter instance as adapter-sidecar
    stream start/stop IPC
- Adapter actions remain one-shot `ResultAction` flows.
  Stream sessions are transport/core stream contract flows and must not be modeled
  as pseudo-streaming action result loops.

## Action Result Form Patch (v1)

To avoid form state loss on async action+reload flows, `ActionResponse` supports optional
structured form patch fields in addition to `resultType/resultValue`:

- `formValuesJson`: JSON object with field values to apply to the open action form
  (example: `{"trackedMacs":["aa:bb:...","cc:dd:..."]}`).
- `fieldChoicesJson`: JSON object mapping field keys to choice arrays
  (example: `{"trackedMacs":[{"value":"...","label":"..."}]}`).
- `resultValueJson`: optional JSON value serialized upstream as the normal
  `resultValue` field. Use this for structured action results such as
  run metadata objects.
- `reloadLayout`: optional boolean hint; when `true`, UI/core may re-request action layout.

Rules:

- Keep schema static; patch values/choices dynamically through action result.
- For actions that mutate selectable lists (`probe`, `browse`, discovery-style actions),
  return both `formValuesJson` and `fieldChoicesJson` in one response.
- Do not encode these patches into scalar `resultValue`; use structured patch fields.
- Scalar `resultValue` remains valid for primitive action results.
- Structured action results must use `resultValueJson`; they are exposed upstream
  as the normal `resultValue` JSON value.
- This pattern is generic and must work for any adapter action form, not only settings dialogs.

## Long-Running Action Runs (v1)

Long-running adapter-owned runs are still started through a normal one-shot
`ActionResponse`.

Contract for adapter authors:

- If an action starts an observable long-running run, `resultValueJson` should
  contain a structured object exposed upstream as `resultValue`.
- Recommended canonical keys in that object:
  - `runId`
  - `streamKind`
  - `streamParams`
  - optional `abortActionId`
  - optional `abortParams`
  - `batch`
- Recommended generic stream kind:
  - `adapter.run`
- `streamParams` should contain the minimum data needed for a later
  `cmd.stream.start`, typically:
  - `runId`
  - optional scenario-/mode-specific fields
- Live progress is not streamed through the action response itself.
- Clients are expected to attach explicitly through `cmd.stream.start` using the
  returned `streamKind` and `streamParams`.
- `cmd.stream.stop` stops only the observation stream.
- Aborting the underlying run is a separate domain operation.
- if a dedicated abort action exists, `abortActionId` and `abortParams` should
  be returned so clients do not rely on adapter-specific conventions.

Current architecture note:

- `adapter.run` is intended as a generic adapter-owned observable run kind.
- phi-core transport/core routing must forward `kind = "adapter.run"` to the
  addressed adapter instance in the same way as other adapter-owned stream kinds.
- This should be understood as part of an adapter-owned kind family (`adapter.*`).
- A broad fallback of arbitrary non-discovery stream kinds to adapter routing is
  only transitional behavior and should not be relied on as the long-term model.

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

### Structured Run Metadata Example

Long-running actions should return attachment metadata through `resultValueJson`:

```cpp
phicore::adapter::v1::ActionResponse resp;
resp.id = request.cmdId;
resp.status = phicore::adapter::v1::CmdStatus::Success;
resp.resultType = phicore::adapter::v1::ActionResultType::None;
resp.resultValueJson =
    R"json({
      "runId": "run-42",
      "streamKind": "adapter.run",
      "streamParams": {
        "runId": "run-42",
        "mode": "ws",
        "scenario": "handshake"
      },
      "batch": false
    })json";
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
