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

This will create `build/collector.ko` and `build/rmid_allocator_test_module.ko`.

## Testing

The module includes several test components:

### Unit Tests

The RMID allocator has a dedicated test module (`rmid_allocator_test_module.ko`) that verifies:
- Initialization and cleanup
- RMID allocation and exhaustion
- RMID freeing and minimum free time enforcement
- RMID info retrieval and status checking

To run the unit tests:

```bash
make test
```

The test script:
1. Builds the test module
2. Loads it using `insmod`
3. Collects and parses test results from kernel logs
4. Provides a summary of passed/failed tests
5. Unloads the module

### Integration Tests

The main module includes integration tests that verify:
1. Proper loading and initialization
2. RDT capability detection
3. RMID allocation to processes
4. Memory monitoring data collection

To run integration tests:

```bash
./test_module.sh
```

### CI/CD Testing

The module is automatically tested on push to main branch and on pull requests using GitHub Actions. The workflow:
- Runs in a privileged Ubuntu 22.04 container
- Installs necessary build tools and kernel headers
- Builds both modules
- Runs unit tests via `test_rmid_allocator.sh`
- Runs integration tests via `test_module.sh`
- Checks kernel logs for errors and warnings
- Fails if any tests fail or if critical errors are found

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