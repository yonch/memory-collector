# Memory Collector Design

## RMID Allocation Semantics

The Memory Collector uses Resource Monitoring IDs (RMIDs) to track memory usage of processes. To ensure accurate measurement attribution, the RMID allocation system implements the following semantics:

### RMID Lifecycle

1. **Allocation**
   - RMIDs are allocated to thread group leaders (processes)
   - All threads within a process share the same RMID
   - RMID 0 is reserved and considered invalid
   - Allocation fails if no RMIDs are available that have been free long enough

2. **Deallocation**
   - RMIDs are freed when a process terminates
   - The free timestamp is recorded to enforce the limbo period
   - Freed RMIDs are added to a FIFO queue for reuse

3. **Limbo Period**
   - A minimum wait time of 2ms is enforced between RMID deallocation and reallocation
   - This ensures measurement intervals (1ms) remain unambiguous
   - Prevents the ABA problem where measurements from different processes could be mixed

### Measurement Guarantees

1. **Temporal Isolation**
   - Each RMID uniquely identifies a single process during any 1ms measurement window
   - The 2ms limbo period ensures no overlap between processes using the same RMID
   - Userspace can safely aggregate measurements using RMID-indexed arrays

2. **Resource Efficiency**
   - RMIDs are a limited resource (typically 512 maximum)
   - The FIFO reuse policy aims to let cache footprints associated with freed RMIDs to decay before reuse.
   - The limbo period is kept minimal (2ms) to maintain high RMID availability. If we see high jitter in measurement timers, we can increase the limbo period.

3. **Hardware Integration**
   - On systems with hardware RDT support, RMIDs are programmed into MSRs
   - On systems without RDT support, RMIDs are emulated for consistent behavior
   - Context switches update RMIDs in hardware when necessary

### Implementation Details

1. **Data Structures**
   - `struct rmid_info`: Tracks RMID metadata including process info and free timestamp
   - `struct rmid_alloc`: Global allocator with free list (used as a queue) and spinlock protection

2. **Concurrency**
   - Spinlock protection for all RMID operations
   - Lock-free fast path for thread RMID inheritance

3. **Monitoring**
   - Tracepoints report RMID allocation and deallocation events to the eBPF collector
   - Procfs interface for dumping current RMID assignments (so the eBPF collector can see RMIDs for processes that existed before the collector was loaded)
