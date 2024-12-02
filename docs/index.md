# Memory Collector

A Kubernetes-native collector for monitoring memory subsystem interference between pods. This project is under active development and we welcome contributors to help build this critical observability component.

## Overview

Memory Collector helps Kubernetes operators identify and quantify performance degradation caused by memory subsystem interference ("noisy neighbors") by collecting metrics about:

- Memory bandwidth utilization
- Last Level Cache (LLC) usage
- CPU performance counters related to memory access

This data helps operators:
- Identify when pods are experiencing memory subsystem interference
- Quantify the performance impact of noisy neighbors
- Build confidence before deploying memory interference mitigation solutions

## Why This Matters

Memory subsystem interference can cause:
- 25%+ increase in cycles per instruction (CPI)
- 4x-13x increase in tail latency
- Reduced application performance even with CPU and memory limits

Common sources of interference include:
- Garbage collection
- Big data analytics
- Security scanning
- Video streaming/transcoding
- Container image decompression

## Development Status & Contributing

The project is in active development across several areas:

### Core Metrics Collection
- Implementing collection for Intel RDT and AMD QoS
- Collecting hardware performance counters: cycles, instructions, cache misses
- Defining Prometheus metrics

### Kubernetes Integration
- Helm chart, DaemonSet implementation
- Prometheus integration

### Testing & Documentation
- Architecture documentation
- Benchmark suite with example workloads
- Integration testing framework

## Get Involved

We welcome contributions! Here's how you can help:

- **Code**: Check our [Good First Issues](../../issues?q=is:issue+is:open+label:"good+first+issue") and [Development Guide](docs/development.md)
- **Use Cases**: Share interference scenarios, test in your environment
- **Discussion**: Open GitHub Issues or email yonch@yonch.com
- **Schedule a chat**: https://yonch.com/collector

## Project Background

This project builds on research and implementation from:
- Google's CPI² system
- Meta's Resource Control implementation
- Alibaba Cloud's Alita system
- MIT's Caladan project

## License

### Code
Licensed under the Apache License, Version 2.0

### Documentation
Documentation is licensed under a [Creative Commons Attribution 4.0 International License](http://creativecommons.org/licenses/by/4.0/).