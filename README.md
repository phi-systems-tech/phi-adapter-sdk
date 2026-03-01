# phi-adapter-sdk

Linux-first SDK for phi adapter sidecars.

## Targets

- `phi::adapter-contract`
  - Header-only contract (`phicore::adapter::v1`)
  - Domain types for adapter integration: schema, device, channel, room/group/scene, discovery
  - Stable protocol primitives (`CmdId`, `ExternalId`, frame header, message type)
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

## Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

## Recommended C++ Model

`AdapterSidecar` is the polymorphic base class for sidecar adapters.
`AdapterFactory` creates adapter instances.
`SidecarHost` wires IPC transport and handler dispatch.
`AdapterFactory::pluginType()` is used as fallback if bootstrap payload has no plugin type.

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
