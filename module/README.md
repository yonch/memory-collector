# Memory Collector Kernel Module

This kernel module provides low-level memory subsystem monitoring capabilities for the Memory Collector project.

## Development Environment

The recommended way to build and test the module is using the provided dev container, which ensures consistent build environments across different systems.

### Prerequisites

- Docker
- VS Code with Dev Containers extension (optional)

### Using Dev Container

The dev container provides all necessary build tools and kernel headers. It is configured for both ARM64 and AMD64 architectures (compilation-only on ARM64) and includes:

- Build essentials (gcc, make)
- Linux kernel headers (6.8.0-52-generic)
- eBPF development tools (golang, llvm, clang, libbpf-dev)
- Testing tools (trace-cmd)

To start the dev container:

```bash
docker build -f Dockerfile.devcontainer -t memory-collector-dev .
docker run -it --privileged memory-collector-dev
```

## Building the Module

Inside the dev container or on a compatible system:

```bash
cd module
make
```

This will create `build/collector.ko`.

## Testing

The module includes automated tests that verify its functionality:

```bash
./test_module.sh
```

The test script:
1. Loads the module using `insmod`
2. Verifies proper loading
3. Collects trace data using `trace-cmd`
4. Validates the collected samples
5. Unloads the module

### CI/CD Testing

The module is automatically tested on push to main branch using GitHub Actions. The workflow:
- Runs on bare metal EC2 instances (default: m7i.xlarge)
- Supports optional RDT testing on m7i.metal-24xl instances
- Performs multiple load/unload cycles
- Validates trace data collection
- Checks RDT capabilities when available

## Manual Testing

To load the module:
```bash
sudo insmod build/collector.ko
```

Verify it's loaded:
```bash
lsmod | grep collector
dmesg | grep "Memory Collector"
```

To unload:
```bash
sudo rmmod collector
```

## System Requirements

- Linux kernel 6.8.0 or compatible
- For RDT features: Intel CPU with Resource Director Technology support
- trace-cmd for data collection 