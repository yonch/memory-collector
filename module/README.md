# Memory Collector Kernel Module

This kernel module provides low-level memory subsystem monitoring capabilities for the Memory Collector project.

## Building the Module

### Prerequisites

- Linux kernel headers for your running kernel
- Build tools (gcc, make)

On Ubuntu/Debian:
```bash
sudo apt-get install linux-headers-$(uname -r) build-essential
```

On RHEL/Fedora:
```bash
sudo dnf install kernel-devel kernel-headers gcc make
```

### Manual Build

To build the module directly:

```bash
make
```

This will create `memory_collector.ko` which can be loaded with:

```bash
sudo insmod memory_collector.ko
```

To unload:
```bash
sudo rmmod memory_collector
```

### DKMS Installation

For automatic rebuilding when the kernel updates:

1. Install DKMS:
```bash
# Ubuntu/Debian
sudo apt-get install dkms

# RHEL/Fedora
sudo dnf install dkms
```

2. Install the module through DKMS:
```bash
sudo dkms add .
sudo dkms install memory-collector/1.0
```

The module will now automatically rebuild when your kernel is updated.

To uninstall:
```bash
sudo dkms remove memory-collector/1.0 --all
```

## Verification

After loading the module, verify it's running:

```bash
lsmod | grep memory_collector
```

Check kernel logs for module status:
```bash
dmesg | tail
``` 