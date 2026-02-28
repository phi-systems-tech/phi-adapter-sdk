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

## Scope

- Runtime transport is Linux-only (`epoll`, Unix Domain Sockets)
- No Qt dependency
- No Boost dependency
- `externalId` is the canonical adapter-domain identifier in v1 contract types

## Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

## Example

`phi_adapter_sidecar_example` starts a sidecar runtime and echoes a simple heartbeat event.

```bash
./build/phi_adapter_sidecar_example /tmp/phi-adapter-example.sock
```
