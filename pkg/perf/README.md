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

## Usage

### Basic Usage

```go
// Initialize a ring buffer
data := make([]byte, pageSize*(1+nPages)) // 1 meta page + n data pages
ring, err := perf.InitContiguous(data, nPages, pageSize)
if err != nil {
    // Handle error
}

// Write data
ring.StartWriteBatch()
offset, err := ring.Write(data, eventType)
ring.FinishWriteBatch()

// Read data
ring.StartReadBatch()
size, _ := ring.PeekSize()
buf := make([]byte, size)
ring.PeekCopy(buf, 0, uint16(size))
ring.Pop()
ring.FinishReadBatch()
```

### Using Mmap Storage

```go
// Create mmap-based storage that wakes up after accumulating 4KB of data
storage, err := NewMmapRingStorage(0, 8, 4096)
if err != nil {
    // Handle error
}
defer storage.Close()

// Or wake up on every event
storage, err := NewMmapRingStorage(0, 8, 0)
if err != nil {
    // Handle error
}
defer storage.Close()
```

## Testing

Run the tests with:

```bash
go test -v ./pkg/perf
```

The test suite includes:
- Basic initialization tests
- Storage implementation tests
- Watermark configuration tests
- Error cases and cleanup 