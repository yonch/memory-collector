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

### Usage

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

### Memory Layout

The perf ring buffer consists of:
1. A metadata page containing shared information
2. A power-of-2 sized data buffer for the actual ring

The data buffer is treated as a circular buffer where:
- Writers append to the tail
- Readers consume from the head
- Buffer wrapping is handled automatically

### Thread Safety

The implementation uses proper memory barriers through the metadata page to ensure thread safety between kernel writers and userspace readers. The batched operations help minimize cache line bouncing between processors.

## Testing

Run the tests with:

```bash
go test -v ./pkg/perf
```

The test suite includes:
- Basic initialization tests
- Write and read operations
- Buffer wraparound handling
- Bytes remaining calculation 