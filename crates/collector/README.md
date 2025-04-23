# Task Collector

A small Rust eBPF program to track task (process) lifecycle events in Linux.

## Features

- Tracks new process creation and termination
- Uses the BPF Task Storage API to efficiently track tasks
- Based on libbpf-rs for BPF integration
- Outputs events in real-time with timestamps

## Requirements

- Linux kernel 5.10+ (for BPF Task Storage support)
- Rust toolchain
- clang/llvm (for BPF compilation)
- Root privileges (to load BPF programs)

## Building

```bash
cargo build --release
```

## Running

Since loading BPF programs requires elevated privileges, you need to run the program as root:

```bash
sudo ./target/release/collector
```

### Options

- `--verbose` or `-v`: Enable verbose debug output
- `--duration <SECONDS>` or `-d <SECONDS>`: Run for a specific duration (default: run indefinitely)

Example with a 30-second duration:
```bash
sudo ./target/release/collector -d 30
```

## Output Format

The program outputs events with the following format:

```
HH:MM:SS.mmm EVENT_TYPE: details
```

For example:
```
12:34:56.789 TASK_NEW: pid=1234   comm=bash          
12:35:01.123 TASK_EXIT: pid=1234   
```

## Technical Details

This program uses two eBPF tracepoints:
1. `sched_switch` - to detect and track new task metadata
2. `sched_process_free` - to detect when tasks exit

The BPF Task Storage API is used to efficiently track which tasks have already been reported. 