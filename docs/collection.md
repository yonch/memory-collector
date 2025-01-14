# Memory Collector Telemetry Strategy

## Overview

The Memory Collector project aims to provide deep visibility into memory subsystem interference patterns in Kubernetes environments. To achieve this, we plan to build an initial collector that captures high-resolution telemetry data from individual machines. 

**This "firehose" telemetry will enable us to build a big data dataset for offline analysis, allowing us to identify patterns and develop algorithms for real-time interference detection.**

## Telemetry Collection

The collector will monitor and record the following metrics for each process at 1 millisecond granularity:

- Process ID
- Core ID 
- Core frequency during the measured interval
- Cycles 
- Instructions
- Last level cache misses

**Collecting this data at high frequency across all processes will provide a detailed view of resource usage and contention over time.**

In addition to these per-process metrics, we will also collect cache occupancy measurements using Intel RDT's Cache Monitoring Technology (CMT) or an equivalent mechanism. This data will be collected per process at the same 1 millisecond granularity.

**Monitoring cache usage per process is necessary because caches maintain state across context switches and are shared by all threads of a process.**

## Data Format

For the initial version, telemetry will be written to CSV files to simplify data collection and analysis. Each row will represent a single measurement interval for a specific process.

**We will generate two datasets:**

1. Per-process, per-core measurements (process ID, core ID, frequency, cycles, instructions, LLC misses)
2. Per-process cache occupancy measurements

**While these datasets will be separate, they will be synchronized and aligned by timestamp to enable joint analysis.**

## Implementation Stages

To manage complexity, we will implement telemetry collection in two stages:

1. Collect per-process, per-core measurements (process ID, core ID, frequency, cycles, instructions, LLC misses)
2. Add per-process cache occupancy measurements using Intel RDT or an equivalent mechanism

**This staged approach allows us to validate the core telemetry pipeline before adding the complexity of cache monitoring.**

For the cache monitoring stage, we will need to assign each process a unique identifier (e.g., CLOS for Intel RDT) to track its cache usage. This will require additional system-level coordination and metadata management.

## Analysis and Algorithm Development

By collecting high-resolution telemetry from multiple clusters, both real-world deployments and benchmark environments, we aim to build a representative dataset capturing a wide range of interference scenarios.

**Analyzing this data offline using big data techniques will help us identify common interference patterns, resource usage signatures, and relevant metrics for detecting contention.**

These insights will inform the development of algorithms for real-time interference detection in future collector versions. Starting with a thorough understanding of low-level behavior is key to building effective higher-level detection and mitigation strategies.

## Summary

Our telemetry strategy prioritizes high-resolution, low-level data collection to build a foundation for understanding memory subsystem interference. By focusing on simplicity and data quality in the initial collector, we can enable rapid iteration and validation of detection algorithms.

**The key aspects of our approach are:**

- Collect per-process, per-core metrics at 1 millisecond granularity
- Collect per-process cache occupancy metrics at 1 millisecond granularity
- Generate synchronized datasets for joint analysis
- Implement in stages to manage complexity

**This approach aligns with our overall goal of providing meaningful visibility into a critical yet often overlooked performance factor in Kubernetes environments.**