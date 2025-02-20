package perf

import (
	"fmt"
	"os"
	"runtime"

	"golang.org/x/sys/unix"
)

// RingStorage defines the interface for perf ring buffer storage
type RingStorage interface {
	// Data returns the raw data buffer containing metadata page and data pages
	Data() []byte
	// NumDataPages returns the number of data pages in the ring buffer
	NumDataPages() uint32
	// PageSize returns the system page size
	PageSize() uint64
	// Close releases any resources associated with the storage
	Close() error
	// FileDescriptor returns the file descriptor if this is a perf event storage, or -1 otherwise
	FileDescriptor() int
}

// MemoryRingStorage implements RingStorage using regular memory allocation
// This is useful for testing and inter-thread communication
type MemoryRingStorage struct {
	data       []byte
	nDataPages uint32
	pageSize   uint64
}

// NewMemoryRingStorage creates a new memory-based ring storage
func NewMemoryRingStorage(nPages uint32) (*MemoryRingStorage, error) {
	pageSize := uint64(os.Getpagesize())
	totalSize := pageSize * (1 + uint64(nPages)) // 1 metadata page + data pages

	data := make([]byte, totalSize)
	storage := &MemoryRingStorage{
		data:       data,
		nDataPages: nPages,
		pageSize:   pageSize,
	}

	return storage, nil
}

func (s *MemoryRingStorage) Data() []byte         { return s.data }
func (s *MemoryRingStorage) NumDataPages() uint32 { return s.nDataPages }
func (s *MemoryRingStorage) PageSize() uint64     { return s.pageSize }
func (s *MemoryRingStorage) Close() error         { return nil }
func (s *MemoryRingStorage) FileDescriptor() int  { return -1 }

// MmapRingStorage implements RingStorage using perf_event_open and mmap
type MmapRingStorage struct {
	data       []byte
	nDataPages uint32
	pageSize   uint64
	fd         int
}

// NewMmapRingStorage creates a new mmap-based ring storage
// cpu: the CPU to monitor (-1 for any CPU)
// nPages: number of data pages in the ring buffer
// nWatermarkBytes: number of bytes to wait before waking up. If 0, wake up on every event.
func NewMmapRingStorage(cpu int, nPages uint32, nWatermarkBytes uint32) (*MmapRingStorage, error) {
	pageSize := uint64(os.Getpagesize())

	// Configure perf event attributes
	attr := unix.PerfEventAttr{
		Type:        unix.PERF_TYPE_SOFTWARE,
		Config:      unix.PERF_COUNT_SW_BPF_OUTPUT,
		Sample_type: unix.PERF_SAMPLE_RAW,
	}

	// Configure watermark behavior
	if nWatermarkBytes > 0 {
		attr.Bits = unix.PerfBitWatermark
		attr.Wakeup = nWatermarkBytes
	} else {
		attr.Wakeup = 1 // Wake up on every event
	}

	// Open perf event
	fd, err := unix.PerfEventOpen(&attr, -1, cpu, -1, unix.PERF_FLAG_FD_CLOEXEC)
	if err != nil {
		return nil, fmt.Errorf("perf_event_open failed: %w", err)
	}

	// Set up cleanup in case of errors
	success := false
	defer func() {
		if !success {
			unix.Close(fd)
		}
	}()

	// Calculate total size and mmap the buffer
	totalSize := pageSize * (1 + uint64(nPages)) // 1 metadata page + data pages
	data, err := unix.Mmap(fd, 0, int(totalSize), unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
	if err != nil {
		return nil, fmt.Errorf("mmap failed: %w", err)
	}

	storage := &MmapRingStorage{
		data:       data,
		nDataPages: nPages,
		pageSize:   pageSize,
		fd:         fd,
	}

	// Set up finalizer to ensure cleanup
	runtime.SetFinalizer(storage, (*MmapRingStorage).Close)
	success = true
	return storage, nil
}

func (s *MmapRingStorage) Data() []byte         { return s.data }
func (s *MmapRingStorage) NumDataPages() uint32 { return s.nDataPages }
func (s *MmapRingStorage) PageSize() uint64     { return s.pageSize }
func (s *MmapRingStorage) FileDescriptor() int  { return s.fd }

// Close releases the mmap'd memory and closes the file descriptor
func (s *MmapRingStorage) Close() error {
	if s.data != nil {
		if err := unix.Munmap(s.data); err != nil {
			return fmt.Errorf("munmap failed: %w", err)
		}
		s.data = nil
	}

	if s.fd != -1 {
		if err := unix.Close(s.fd); err != nil {
			return fmt.Errorf("close failed: %w", err)
		}
		s.fd = -1
	}

	runtime.SetFinalizer(s, nil)
	return nil
}
