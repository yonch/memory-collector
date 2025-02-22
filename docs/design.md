# RMID Tracking Design

## Overview

The RMID (Resource Monitoring ID) tracking system maintains a record of RMID allocations and deallocations in userspace, integrated with the time slot based aggregation system. The system consists of three main components:

1. Kernel Module: 
  - Manages RMID allocation and deallocation
  - Sends time-based and sched_switch based tracepoints to trigger perf measurement in eBPF
  - Reads Intel RDT memory bandwidth and cache footprint metrics by RMID and sends them to eBPF
2. eBPF Program:
  - Relays RMID lifecycle and RDT measurements to userspace
  - Reads perf measurements (cycles, instructions, etc) and sends to userspace
3. Userspace Collector: Processes events and maintains RMID state

## Kernel Module Semantics

The kernel module provides the following guarantees for RMID management:

1. RMID Allocation:
   - RMIDs are allocated to thread group leaders (processes)
   - All threads within a process share the same RMID
   - RMID 0 is reserved and considered invalid
   - Each RMID uniquely identifies a single process during any measurement window
   - Allocation includes process metadata (comm, tgid)

2. RMID Lifetime:
   - RMIDs remain valid until explicitly freed
   - RMIDs are freed when a process terminates
   - After being freed, an RMID cannot be reused for at least 2ms (limbo period)
   - This limbo period ensures measurement intervals (1ms) remain unambiguous
   - Prevents the ABA problem where measurements from different processes could be mixed

3. RMID State:
   - The kernel maintains the mapping between processes and RMIDs
   - RMIDs are process-specific and persist across thread creation
   - RMIDs are automatically freed when a process exits
   - On systems with hardware RDT support, RMIDs are programmed into MSRs
   - On systems without RDT support, RMIDs are emulated for consistent behavior

4. Resource Management:
   - RMIDs are a limited resource (typically 512 maximum)
   - Freed RMIDs are added to a FIFO queue for reuse
   - The FIFO reuse policy allows cache footprints to decay before reuse
   - The limbo period is kept minimal (2ms) to maintain high RMID availability

5. State Dumps
   - A procfs interface allows dumping current RMID assignments. 
   - Enables collectors to receive state of processes that existed before collector startup

## Message Protocol

The eBPF program communicates three types of events to userspace through a perf event array:

1. Performance Measurement, including cycles, instructions, LLC misses, and time delta -- attributed to RMID
2. RMID Allocation
3. RMID Free

### Message Flow

1. eBPF code sends all messages via a single perf event array
2. Messages are enqueued in arrival time order in the per-cpu ring buffers
3. Userspace processes messages from all per-cpu ring buffers in global timestamp order

## Userspace RMID Package

### Components

1. `Metadata` Structure: 
   - Maintains metadata for each RMID
2. `Message` Structure: 
   - Holds previously received messages that have not been processed into the current state
3. `Tracker` Structure:
   - Maintains current RMID state
   - Queues updates for ordered processing
   - Preserves metadata after RMID free

### Key Operations

1. `Alloc(rmid, comm, tgid, timestamp)`:
   - Enqueues RMID allocation with metadata
   - Maintains timestamp order

2. `Free(rmid, timestamp)`:
   - Enqueues RMID free event
   - Maintains timestamp order

3. `Advance(timestamp)`:
   - Processes queued events up to timestamp
   - Updates current state snapshot
   - Maintains FIFO ordering of events

### Integration with Time Slot System

1. Time Slot Structure:
   - 1ms duration
   - Maintains window of several slots
   - Retires oldest slot when window advances

2. RMID State Management:
   - RMID tracker advances with each time slot retirement
   - Metadata preserved after free for correct attribution
   - Kernel's 2ms limbo period ensures measurement integrity
   - Each RMID uniquely identifies a single process during any 1ms measurement window

3. Event Processing:
   - All events (perf, alloc, free) processed in timestamp order
   - RMID state advanced before writing each time slot
   - Measurements attributed using RMID state from appropriate time slot

## Metadata Preservation

The system preserves RMID metadata after an RMID is freed to ensure correct attribution of measurements within the same time slot. This is necessary because:

1. An RMID may be freed during a time slot
2. Measurements from that RMID may still arrive for the same time slot
3. The metadata is needed to properly attribute these measurements
4. The kernel's 2ms limbo period prevents incorrect attribution by ensuring no RMID reuse within measurement windows

## Error Handling

1. Message Parsing:
   - Invalid message types logged and skipped
   - Malformed messages logged and skipped
   - Lost messages tracked and reported

2. Time Ordering:
   - Messages processed strictly in timestamp order
   - Safe timestamp arithmetic for overflow handling
   - Efficient queue management

3. Resource Management:
   - Proper cleanup on shutdown
   - Memory usage bounded by window size
   - Efficient state tracking
   - RMID allocation failures logged when no RMIDs are available that have been free long enough
