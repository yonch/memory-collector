package perf

import (
	"fmt"
	"sync/atomic"
	"unsafe"
)

// PerfEventHeader represents the header of a perf event
type PerfEventHeader struct {
	Type uint32
	Misc uint16
	Size uint16
}

// PerfRing represents a perf ring buffer with shared metadata and data pages
type PerfRing struct {
	// Shared metadata page
	meta *PerfEventMmapPage
	// Data buffer
	data []byte
	// Mask for quick modulo operations (buffer size - 1)
	bufMask uint64
	// Current head position for reading
	head uint64
	// Current tail position for writing
	tail uint64
}

// PerfEventMmapPage represents the shared metadata page
type PerfEventMmapPage struct {
	Version        uint32         // ABI version
	Compat_version uint32         // Lowest compatible version
	Pad1           [1024 - 8]byte // Pad to 1024 bytes
	Data_head      uint64         // Head in the data section
	Data_tail      uint64         // Tail in the data section
	Data_offset    uint64         // Offset of data section
	Data_size      uint64         // Size of data section
	AuxOffset      uint64         // Offset of aux section
	AuxSize        uint64         // Size of aux section
}

// InitContiguous initializes a PerfRing using contiguous memory
func InitContiguous(data []byte, nPages uint32, pageSize uint64) (*PerfRing, error) {
	if data == nil {
		return nil, fmt.Errorf("data buffer cannot be nil")
	}

	bufLen := uint64(nPages) * pageSize
	if (bufLen&(bufLen-1)) != 0 || bufLen < 8 {
		return nil, fmt.Errorf("buffer length must be a power of 2 and at least 8 bytes")
	}

	// First page is metadata, rest is data
	meta := (*PerfEventMmapPage)(unsafe.Pointer(&data[0]))
	// if Data_offset is not given (older kernels), we need to skip a full page, otherwise we skip Data_offset bytes
	dataStart := meta.Data_offset
	if dataStart == 0 {
		dataStart = pageSize
	}

	ring := &PerfRing{
		meta:    meta,
		data:    data[dataStart : dataStart+bufLen],
		bufMask: bufLen - 1,
		head:    atomic.LoadUint64(&meta.Data_tail),
		tail:    atomic.LoadUint64(&meta.Data_head),
	}

	return ring, nil
}

// StartWriteBatch starts a write batch operation
func (r *PerfRing) StartWriteBatch() {
	// Get the current tail position from shared memory using atomic load
	r.head = atomic.LoadUint64(&r.meta.Data_tail)
}

// Write writes data to the ring buffer with the given type
func (r *PerfRing) Write(data []byte, eventType uint32) (int, error) {
	if len(data) == 0 {
		return 0, fmt.Errorf("cannot write empty data")
	}

	// Calculate total size including header, aligned to 8 bytes
	alignedLen := ((uint32(len(data)) + uint32(unsafe.Sizeof(PerfEventHeader{})) + 7) & ^uint32(7))
	if alignedLen > uint32(r.bufMask) {
		return 0, fmt.Errorf("data too large for buffer")
	}

	// Check if there's enough space
	if r.tail+uint64(alignedLen)-r.head > r.bufMask+1 {
		return 0, fmt.Errorf("buffer full")
	}

	// Write header
	header := PerfEventHeader{
		Type: eventType,
		Size: uint16(alignedLen),
	}
	headerPos := r.tail & r.bufMask
	*(*PerfEventHeader)(unsafe.Pointer(&r.data[headerPos])) = header

	// Write data
	dataPos := (r.tail + uint64(unsafe.Sizeof(header))) & r.bufMask
	if dataPos+uint64(len(data)) <= uint64(len(r.data)) {
		// Data fits without wrapping
		copy(r.data[dataPos:], data)
	} else {
		// Data wraps around buffer end
		firstPart := uint64(len(r.data)) - dataPos
		copy(r.data[dataPos:], data[:firstPart])
		copy(r.data[0:], data[firstPart:])
	}

	r.tail += uint64(alignedLen)
	return int(dataPos), nil
}

// FinishWriteBatch commits the write batch
func (r *PerfRing) FinishWriteBatch() {
	// Ensure all writes are visible before updating tail using atomic store
	atomic.StoreUint64(&r.meta.Data_head, r.tail)
}

// StartReadBatch starts a read batch operation
func (r *PerfRing) StartReadBatch() {
	// Get the current head position from shared memory using atomic load
	r.tail = atomic.LoadUint64(&r.meta.Data_head)
}

// PeekSize returns the size of the next event in the ring buffer
func (r *PerfRing) PeekSize() (int, error) {
	if r.tail == r.head {
		return 0, fmt.Errorf("buffer empty")
	}

	header := (*PerfEventHeader)(unsafe.Pointer(&r.data[r.head&r.bufMask]))
	return int(header.Size - uint16(unsafe.Sizeof(PerfEventHeader{}))), nil
}

// PeekType returns the type of the next event
func (r *PerfRing) PeekType() uint32 {
	header := (*PerfEventHeader)(unsafe.Pointer(&r.data[r.head&r.bufMask]))
	return header.Type
}

// PeekCopy copies data from the ring buffer without consuming it
func (r *PerfRing) PeekCopy(buf []byte, offset uint16) error {
	size, err := r.PeekSize()
	if err != nil {
		return err
	}

	if len(buf) > int(size) {
		return fmt.Errorf("buffer too small")
	}

	startPos := (r.head + uint64(unsafe.Sizeof(PerfEventHeader{})) + uint64(offset)) & r.bufMask
	endPos := (startPos + uint64(len(buf)) - 1) & r.bufMask

	if endPos < startPos {
		// Data wraps around buffer end
		firstLen := uint64(len(r.data)) - startPos
		copy(buf, r.data[startPos:startPos+firstLen])
		copy(buf[firstLen:], r.data[:endPos+1])
	} else {
		// Data is contiguous
		copy(buf, r.data[startPos:startPos+uint64(len(buf))])
	}

	return nil
}

// Pop consumes the current event
func (r *PerfRing) Pop() error {
	if r.tail == r.head {
		return fmt.Errorf("buffer empty")
	}

	header := (*PerfEventHeader)(unsafe.Pointer(&r.data[r.head&r.bufMask]))
	r.head += uint64(header.Size)
	return nil
}

// FinishReadBatch commits the read batch
func (r *PerfRing) FinishReadBatch() {
	// Update tail position using atomic store
	atomic.StoreUint64(&r.meta.Data_tail, r.head)
}

// BytesRemaining returns the number of bytes available to read
func (r *PerfRing) BytesRemaining() uint32 {
	begin := r.head & r.bufMask
	end := r.tail & r.bufMask

	if end < begin {
		return uint32((r.bufMask + 1) - begin + end)
	}

	return uint32(end - begin)
}
