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

This will create:
- `build/collector.ko` - Main collector module
- `build/rmid_allocator_test_module.ko` - RMID allocator test module
- `build/procfs_test_module.ko` - procfs test module
- `build/sync_timer_test_module.ko` - Sync timer test module
- `build/sync_timer_benchmark_module.ko` - Sync timer benchmark module

## Components

### Sync Timer Module

The sync timer module provides synchronized high-resolution timers across all CPUs. Key features:

- Synchronized timer ticks at regular intervals on all CPUs
- Configurable interval with nanosecond precision
- Automatic alignment to interval boundaries
- Comprehensively benchmarked

Usage example:

```c
#include "sync_timer.h"

struct sync_timer timer;

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
    // Handle timer tick
    return sync_timer_restart(timer, &timer);
}

// Initialize timers
sync_timer_init(&timer, timer_callback, NSEC_PER_MSEC);  // 1ms interval

// Clean up
sync_timer_destroy(&timer);
```

### RMID Allocator

The RMID allocator manages Resource Monitoring IDs for Intel RDT. Features:

- Dynamic RMID allocation and freeing
- Minimum free time enforcement
- Per-RMID tracking of process info
- Tracepoint integration

### Procfs Interface

The procfs module provides a simple interface for triggering data dumps:

- Write-only procfs entries
- Configurable dump callback
- Simple command parsing

## Testing

The module includes several test components:

### Unit Tests

1. **RMID Allocator Tests** (`rmid_allocator_test_module.ko`)
   - Initialization and cleanup
   - RMID allocation and exhaustion
   - RMID freeing and minimum free time enforcement
   - RMID info retrieval and status checking

2. **Procfs Tests** (`procfs_test_module.ko`)
   - Entry creation and removal
   - Command parsing
   - Callback invocation

3. **Sync Timer Tests** (`sync_timer_test.ko`)
   - Timer initialization and cleanup
   - Synchronized tick verification
   - Missed tick detection
   - Cross-CPU synchronization

To run all unit tests:

```bash
make test
```

### Integration Tests

The main module includes integration tests that verify:
1. Proper loading and initialization
2. RDT capability detection
3. RMID allocation to processes
4. Memory monitoring data collection
5. Timer synchronization

To run integration tests:

```bash
./test_module.sh
```

### Timer Benchmarks

The sync timer module includes comprehensive benchmarking capabilities to measure timer precision under various system loads:

1. **Basic Benchmarking**
   ```bash
   cd module
   ./benchmark_sync_timer.sh
   ```
   This outputs JSON-formatted results including:
   - Total samples collected
   - Minimum/maximum/mean timer deltas
   - Standard deviation of timer deltas
   - Number of missed ticks

2. **Stress Testing**
   ```bash
   cd module
   ./benchmark_sync_timer_stress.sh
   ```
   This runs benchmarks under various stress conditions:
   - CPU and scheduler stress (matrix multiplication)
   - Memory and cache contention (75% memory usage)
   - Interrupt generation (1000Hz timer interrupts)
   - I/O and system call pressure
   - Lock contention (bus locks and mutexes)

   Results are output in CSV format with columns:
   - Test name
   - Sample count
   - Min/max/mean timer deltas (ns)
   - Standard deviation (ns)
   - Missed ticks

3. **CI/CD Integration**
   The GitHub Actions workflow includes benchmark support:
   - Triggered manually with `run-benchmarks` input
   - Runs full stress test suite
   - Uploads results as artifacts
   - Configurable instance type for testing

### CI/CD Testing

The module is automatically tested on push to main branch and on pull requests using GitHub Actions. The workflow:
- Runs in a privileged Ubuntu 22.04 container
- Installs necessary build tools and kernel headers
- Builds all modules
- Runs unit tests for each component
- Runs integration tests
- Optionally runs timer benchmarks
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