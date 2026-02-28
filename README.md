# phi-adapter-sdk

Linux-first SDK for phi adapter sidecars.

## Targets

- `phi::adapter-contract`
  - Header-only contract (`phicore::adapter::v1`)
  - Domain types for adapter integration: schema, device, channel, room/group/scene, discovery
  - Stable protocol primitives (`CmdId`, `ExternalId`, frame header, message type)
- `phi::adapter-sdk`
  - Linux runtime helpers (UDS + epoll transport)
  - Minimal sidecar runtime wrapper (`SidecarRuntime`)
  - Typed sidecar dispatcher (`SidecarDispatcher`) for core IPC requests/events

## Scope

- Runtime transport is Linux-only (`epoll`, Unix Domain Sockets)
- No Qt dependency
- No Boost dependency
- `externalId` is the canonical adapter-domain identifier in v1 contract types
- Contract text type is `phicore::adapter::v1::Utf8String` (`std::string` alias)
- All contract text fields are UTF-8 by contract

## Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

## Example

`phi_adapter_sidecar_example` starts a typed sidecar dispatcher.
It handles the core request methods and emits typed command/action responses.

```bash
./build/phi_adapter_sidecar_example /tmp/phi-adapter-example.sock
```

## IPC Methods (Typed Inbound)

- `sync.adapter.bootstrap`
- `cmd.channel.invoke`
- `cmd.adapter.action.invoke`
- `cmd.device.name.update`
- `cmd.device.effect.invoke`
- `cmd.scene.invoke`

## IPC Events (Typed Outbound)

- `connectionStateChanged`, `error`, `adapterMetaUpdated`
- `deviceUpdated`, `deviceRemoved`, `channelUpdated`, `channelStateUpdated`
- `roomUpdated`, `roomRemoved`, `groupUpdated`, `groupRemoved`
- `scenesUpdated`, `fullSyncCompleted`
