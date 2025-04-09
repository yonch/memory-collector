# Noisy Neighbor Collector

A Kubernetes-native collector for monitoring noisy neighbor issues, currently focusing on memory subsystem interference between pods. This project is under active development and we welcome contributors to help build this critical observability component.

## Overview

The Noisy Neighbor Collector helps SREs identify and quantify performance degradation caused by memory subsystem interference ("noisy neighbors") by collecting metrics about:

- Memory bandwidth utilization
- Last Level Cache (LLC) usage 
- CPU performance counters related to memory access

This data helps operators:
- **Identify when pods are experiencing memory subsystem interference**
- **Quantify the performance impact of noisy neighbors**  
- **Determine which pods are causing interference and which are affected**
- **Build confidence before deploying memory interference mitigation solutions**

## Why This Matters

**Memory noisy neighbors directly impact application latency, especially at the tail (P95/P99).** For example, Google [published](https://dl.acm.org/doi/10.1145/2749469.2749475) 5x-14x increases in P95/P99 latency due to memory subsystem interference.

Collecting noisy neighbor metrics and reducing tail latency delivers two key benefits:

1. **Better response times for users**, improving customer experience and key business metrics. Latency has a [direct impact](https://www.gigaspaces.com/blog/amazon-found-every-100ms-of-latency-cost-them-1-in-sales/) on revenue.

2. **More efficient infrastructure through reduced autoscaling.** Many scaling decisions are based on P95/P99 metrics. By reducing spikes caused by noisy neighbors, you can run at higher utilization without breaching latency SLOs.

Common sources of interference include:
- Garbage collection
- Video streaming/transcoding
- Large transactions (e.g. scanning many database records)
- Analytics workloads

## Acting on Noisy Neighbor Data

The collector's data shows:

- How much time each pod is exposed to noisy neighbors
- How much time each pod is acting as a noisy neighbor

This enables SREs to:

- Identify problematic pods and work with their owners to optimize them  
- Make informed decisions about pod scheduling to isolate sensitive workloads
- Validate the impact of optimizations or isolation on tail latency

In the future, we plan to use this data to automatically mitigate noisy neighbors by throttling offending threads (e.g., garbage collection) using hardware mechanisms like Intel RDT to prevent them from disrupting latency-sensitive workloads.

## Current Capabilities

When running on bare metal with access to Intel RDT:
- Measures memory bandwidth and cache allocation per process

When access to perf counters is available:
- Measures cycles, instructions, and LLC cache misses per process  

Other capabilities:
- Collects measurements at 1ms intervals
- Writes data to Parquet files for analysis

## Roadmap

We're actively working on:

**Container Metadata**  
- Capturing container and pod metadata for processes
- Exploring Node Resource Interface (NRI) for metadata access

**eBPF Migration** 
- Eliminating kernel module requirement
- Pure eBPF implementation for wider compatibility

**Noisy Neighbor Detection**  
- Identifying noisy neighbors from raw 1ms measurements
- Aggregating metrics over longer intervals to determine:
  - Which pods are noisy vs. sensitive 
  - % time each pod is noisy or impacted by noise
- Quantifying cycles wasted due to noise exposure

## Installation

Current requirements:
- Kernel module (eBPF-only in progress) 
- Minimum kernel version: 5.3 (for eBPF CO-RE) 
- Tested with kernel 6.8.0 on Ubuntu 22.04, 24.04

See the [GitHub Actions workflow](.github/workflows/test-ebpf-collector.yaml) for build steps.

## Get Involved 

We welcome contributions! Here's how you can help:

- **Code**: Check our [good first issues](../../issues?q=is:open+is:issue+label:"good+first+issue") and [documentation](https://unvariance.github.io/collector/)
- **Use Cases**: Share interference scenarios, test in your environment  
- **Discussion**: Open GitHub issues or reach out - [@Jonathan Perry](https://cloud-native.slack.com/team/U019KBNGKFT) on [CNCF Slack](https://slack.cncf.io/)
- **Schedule a Chat**: https://yonch.com/collector

## Learn More

- Kubecon NA 2024: Love thy (noisy) neighbor [(video)](https://www.youtube.com/watch?v=VsYp_Z1PvOc) [(slides)](https://static.sched.com/hosted_files/kccncna2024/93/Slides_Kubecon%20NA%2724_%20Love%20thy%20%28Noisy%29%20Neighbor.pdf) [(notes)](https://static.sched.com/hosted_files/kccncna2024/50/Transcript_and_Slides__Love_thy_%28Noisy%29_Neighbor.pdf)

## Background

This project builds on research and technologies from:
- Google's CPI² system 
- Meta's Resource Control
- Alibaba Cloud's Alita
- MIT's Caladan  

## License

Code is licensed under [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0). Documentation is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).