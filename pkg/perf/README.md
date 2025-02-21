# Perf Package

This package provides a Go implementation of the Linux perf ring buffer interface, designed for efficient communication between kernel and userspace. It is particularly useful for collecting eBPF samples from per-CPU perf rings.

## Layer 1: Basic PerfRing

The basic PerfRing layer provides core functionality for working with contiguous memory containing shared structure and data. It implements:

- Reading and writing to PerfRing
- Support for peeking record metadata
- Batched operations for efficient reading and writing
- Proper memory barrier handling for concurrent access

### Key Components

#### PerfRing

The main structure that represents a perf ring buffer. It contains:
- Shared metadata page
- Data buffer
- Buffer mask for efficient modulo operations
- Head and tail pointers for reading/writing

#### Operations

- `InitContiguous`: Initialize a PerfRing using contiguous memory
- `StartWriteBatch`/`FinishWriteBatch`: Batch write operations
- `StartReadBatch`/`FinishReadBatch`: Batch read operations
- `Write`: Write data to the ring buffer
- `PeekSize`/`PeekType`: Examine record metadata without consuming
- `PeekCopy`: Copy data from the ring without consuming
- `Pop`: Consume the current record
- `BytesRemaining`: Get available bytes to read

## Layer 2: Storage Layer

The storage layer provides different implementations for managing the underlying memory of perf ring buffers. It focuses solely on memory management and perf event configuration.

### RingStorage Interface

The common interface implemented by all storage types:
```go
type RingStorage interface {
    Data() []byte
    NumDataPages() uint32
    PageSize() uint64
    Close() error
    FileDescriptor() int
}
```

### Memory-based Storage

`MemoryRingStorage` provides a simple memory-based implementation useful for:
- Testing
- Inter-thread communication
- Scenarios not requiring kernel interaction

```go
storage, err := NewMemoryRingStorage(nPages)
```

### Mmap-based Storage

`MmapRingStorage` provides kernel integration through:
- `perf_event_open` syscall
- Memory mapping of ring buffer
- Support for BPF program output
- Configurable watermark settings

```go
// Create mmap-based storage with watermark configuration
storage, err := NewMmapRingStorage(
    cpu,           // CPU to monitor (-1 for any CPU)
    nPages,        // Number of data pages
    watermarkBytes // Bytes to accumulate before waking up (0 for every event)
)
```

Features:
- Configurable watermark for event batching
- Proper cleanup with finalizers
- Integration with BPF program output

### Memory Layout

The storage layer manages:
1. Metadata page (perf event shared page)
2. Data pages (ring buffer)
3. Memory mapping and permissions
4. Page size alignment

## Layer 3: Reader Layer

The reader layer provides functionality for reading from multiple CPU rings and sorting events by timestamp. This layer is particularly useful when dealing with multi-CPU systems where events need to be processed in chronological order.

### Record Format Requirements

For `PERF_RECORD_SAMPLE` records, each record must include a timestamp as its first 8 bytes in the form of a uint64. This timestamp is used by the reader to maintain chronological order when reading from multiple rings. The timestamp should be placed immediately after the perf event header in the record data.

For example:
```
[perf_event_header]  // Standard perf event header
[uint64 timestamp]   // 8-byte timestamp required for ordering
[remaining data...]  // Rest of the record data
```

The reader handles the following special cases:
- Non-sample records (e.g., `PERF_RECORD_LOST`): Assigned timestamp 0 to ensure immediate processing
- Malformed sample records (less than 8 bytes): Assigned timestamp 0 to ensure immediate processing
- Failed timestamp reads: Assigned timestamp 0 to ensure immediate processing

It is the responsibility of the user of the reader to:
- Ensure proper timestamp placement in sample records
- Handle malformed records appropriately when encountered
- Process non-sample records (like `PERF_RECORD_LOST`) as needed
- Handle records with timestamp 0 according to their application logic

### Key Components

#### RingContainer

Manages multiple CPU rings:
- Maintains a heap of entries sorted by timestamp
- Dynamically grows as rings are added
- Provides efficient timestamp-based access

```go
container := NewRingContainer()

// Add ring for CPU 0
container.AddRing(ring0)
```

#### Reader

Provides sorted access to events:
- Reads events in timestamp order
- Supports maximum timestamp cutoff
- Maintains proper cleanup of resources

```go
// Create reader with max timestamp
maxTimestamp := uint64(time.Now().UnixNano())
reader, err := NewReader(container, maxTimestamp)
if err != nil {
    // Handle error
}
defer reader.Close()

// Read events in timestamp order
for !reader.Empty() {
    ring := reader.CurrentRing()
    // Process event from ring
    reader.Pop()
}
```


## Usage

### Complete Example

```go
// Create storage
storage, err := NewMmapRingStorage(0, 8, 4096)
if err != nil {
    // Handle error
}
defer storage.Close()

// Initialize ring
ring, err := InitContiguous(storage.Data(), storage.NumDataPages(), storage.PageSize())
if err != nil {
    // Handle error
}

// Create container and add ring
container := NewRingContainer()
if err := container.AddRing(ring); err != nil {
    // Handle error
}

// Create reader
reader, err := NewReader(container, maxTimestamp)
if err != nil {
    // Handle error
}
defer reader.Close()

// Read events in timestamp order
for !reader.Empty() {
    ring := reader.CurrentRing()
    size, _ := ring.PeekSize()
    buf := make([]byte, size)
    ring.PeekCopy(buf, 0)
    // Process event
    reader.Pop()
}
```

## Testing

The test suite includes:
- Basic ring buffer operations
- Storage implementation tests
- Multi-CPU ring container tests
- Reader timestamp ordering tests
- Error cases and cleanup
- Watermark configuration tests

Run the tests with:

```bash
go test -v ./pkg/perf
```

Note: Some tests require root privileges or appropriate capabilities (CAP_PERFMON) to run perf_event_open syscalls. 