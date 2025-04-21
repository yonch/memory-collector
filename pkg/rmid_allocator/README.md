# RMID Allocator

This package provides an eBPF-based implementation of a Resource Monitoring ID (RMID) allocator. It manages RMIDs used to track memory usage of processes, with features including:

- RMID allocation and deallocation
- Minimum free time enforcement (2ms by default)

## Implementation Details

The RMID allocator is implemented as a C/BPF library with the following components:

- `rmid_allocator.bpf.h`: Header file containing type definitions and function declarations
- `rmid_allocator.bpf.c`: Implementation of the RMID allocator functions

The allocator uses a struct-based approach:
- `rmid_allocator`: Struct containing all allocator state including:
  - Free list management
  - Allocation tracking
  - Configuration parameters

## Usage

1. Include the header file in your BPF program:

```c
#include "rmid_allocator.bpf.h"
```

2. Initialize the allocator:

```c
struct rmid_allocator allocator;
const __u32 max_rmid = 4;
const __u64 min_free_time_ns = 2000000; // 2ms

if (!rmid_init(&allocator, max_rmid, min_free_time_ns)) {
    // Handle initialization error
}
```

3. Allocate an RMID:

```c
__u64 timestamp = bpf_ktime_get_ns();
__u32 rmid = rmid_alloc(&allocator, timestamp);
if (rmid == 0) {
    // Handle allocation failure (no free RMIDs or minimum free time not met)
}
```

4. Free an RMID:

```c
__u64 timestamp = bpf_ktime_get_ns();
rmid_free(&allocator, rmid, timestamp);
```

5. Check if an RMID is allocated:

```c
if (rmid_is_allocated(&allocator, rmid)) {
    // RMID is allocated
} else {
    // RMID is not allocated
}
```

## Building

To use the RMID allocator in your BPF program, add `rmid_allocator.bpf.c` to your bpf2go's `//go:generate` clause

## Testing

The package contains test code for the RMID allocator:

- Basic RMID allocation and deallocation
- Minimum free time enforcement
- RMID exhaustion
- Invalid RMID handling

## Synchronization

The RMID allocator implementation does not handle synchronization internally. It is the responsibility of the caller to ensure proper synchronization when accessing the allocator from multiple threads/CPUs.

## Limitations

- Maximum number of RMIDs is fixed at compile time (default 1024)
- Minimum free time is fixed at initialization 