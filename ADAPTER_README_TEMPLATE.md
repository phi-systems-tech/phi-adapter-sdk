# phi-adapter-<name>

## Overview

Short user-facing summary of what this adapter does.

## Supported Devices / Systems

- List supported vendors, product families, or platforms.

## Cloud Functionality

- Cloud required: `yes` / `optional` / `no`
- Briefly explain if/why data leaves the local network.

## Known Issues

- List confirmed limitations in user terms.

## License

See `LICENSE`.

---

## Developer Documentation

### Purpose

Describe adapter scope and technical integration boundaries.

### Features

- Discovery support
- Read/write behavior
- Event handling

### Runtime Requirements

- phi-core version requirements
- Network/service prerequisites
- Sidecar runtime requirements (Linux amd64/aarch64)

### Build Requirements

- Build tools and required libraries
- C++20 compiler
- `phi-adapter-sdk`

### Configuration

- Config files
- Required and optional fields
- Document `factory` vs `instance` schema sections
- Document static discovery config `<plugin>-config.json` (`discovery` array) and supported keys
  (`kind`, `mdnsServiceType`, `ssdpSt`, `defaultPort`, `hints`)
- State explicitly that v1 discovery config is strict (no aliases/fallback keys)
- Document layout hints (`layout` / `field.ui`) and action placement rules
- Document action-result form patch behavior (`formValuesJson`, `fieldChoicesJson`, `reloadLayout`)
- Minimal example

### Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

Notes:

- By default, this project expects `phi-adapter-sdk` at `../phi-adapter-sdk`.
- Alternatively install `phi-adapter-sdk` and set `CMAKE_PREFIX_PATH`.

### Installation

- Output sidecar executable name (for `ipc.command`)
- Deployment location (`/opt/phi/plugins/adapters/`)

### SDK Integration

- Use `AdapterInstance` as the adapter instance base class.
- Provide an `AdapterFactory` (`pluginType()`, `createInstance()`).
- Run using `SidecarHost`.
- Recommended alias in adapter code: `namespace phi = phicore::adapter::sdk;`.
- Treat `Cmd*`/`Action*` handling as asynchronous:
  - complete via `sendResult(...)` (never by direct IPC writes)
  - worker/instance threads enqueue results; host/main thread sends IPC frames to core
  - "quasi sync" fast-path is allowed only if result is immediately available, but still
    through `sendResult(...)` and the same host queue/send path
- Follow naming conventions:
  - inbound handlers: `on*`
  - outbound IPC calls: `send*`

### Troubleshooting

- Common error -> cause -> fix

### Maintainers

- Phi Systems Tech team

### Issue Tracker

- `https://github.com/phi-systems-tech/phi-adapter-<name>/issues`

### Releases / Changelog

- Releases: `https://github.com/phi-systems-tech/phi-adapter-<name>/releases`
- Tags: `https://github.com/phi-systems-tech/phi-adapter-<name>/tags`
