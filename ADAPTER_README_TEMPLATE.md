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

### Build Requirements

- Build tools and required libraries
- Qt modules used

### Configuration

- Config files
- Required and optional fields
- Document `factory` vs `instance` schema sections
- Document layout hints (`layout` / `field.ui`) and action placement rules
- Minimal example

### Build

```bash
cmake -S . -B build
cmake --build build --parallel
```

Notes:

- By default, this project expects `phi-adapter-api` at `../phi-adapter-api`.
- Alternatively install `phi-adapter-api-dev` and set `CMAKE_PREFIX_PATH`.

### Installation

- Output shared library name
- Deployment location (`/opt/phi/plugins/adapters/`)

### Troubleshooting

- Common error -> cause -> fix

### Maintainers

- Phi Systems Tech team

### Issue Tracker

- `https://github.com/phi-systems-tech/phi-adapter-<name>/issues`

### Releases / Changelog

- Releases: `https://github.com/phi-systems-tech/phi-adapter-<name>/releases`
- Tags: `https://github.com/phi-systems-tech/phi-adapter-<name>/tags`
