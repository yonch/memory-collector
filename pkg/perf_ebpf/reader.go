// Package perf_ebpf provides integration between perf ring buffers and eBPF maps.
package perf_ebpf

import (
	"fmt"

	"github.com/cilium/ebpf"
	"github.com/unvariance/collector/pkg/perf"
)

// Options controls the behavior of PerfMapReader
type Options struct {
	// The size of each per-CPU buffer in bytes
	BufferSize int
	// The number of bytes that must be written before waking up userspace
	// Must be less than BufferSize
	WatermarkBytes uint32
}

// PerfMapReader manages perf ring buffers connected to an eBPF map
type PerfMapReader struct {
	array   *ebpf.Map
	rings   []*perf.PerfRing
	storage []*perf.MmapRingStorage
	reader  *perf.Reader
}

// NewPerfMapReader creates a new reader connected to an eBPF map
func NewPerfMapReader(array *ebpf.Map, opts Options) (*PerfMapReader, error) {
	if array == nil {
		return nil, fmt.Errorf("array cannot be nil")
	}

	if opts.BufferSize < 1 {
		return nil, fmt.Errorf("buffer size must be greater than 0")
	}

	if opts.WatermarkBytes >= uint32(opts.BufferSize) {
		return nil, fmt.Errorf("watermark must be less than buffer size")
	}

	// Get number of possible CPUs from the map
	nCPU := int(array.MaxEntries())
	if nCPU < 1 {
		return nil, fmt.Errorf("invalid number of CPUs in map: %d", nCPU)
	}

	pmr := &PerfMapReader{
		array:   array,
		rings:   make([]*perf.PerfRing, 0, nCPU),
		storage: make([]*perf.MmapRingStorage, 0, nCPU),
	}

	// Create storage and rings for each CPU
	for cpu := 0; cpu < nCPU; cpu++ {
		storage, err := perf.NewMmapRingStorage(cpu, uint32(opts.BufferSize/4096), opts.WatermarkBytes)
		if err != nil {
			pmr.Close()
			return nil, fmt.Errorf("failed to create storage for CPU %d: %w", cpu, err)
		}
		pmr.storage = append(pmr.storage, storage)

		ring, err := perf.InitContiguous(storage.Data(), storage.NumDataPages(), storage.PageSize())
		if err != nil {
			pmr.Close()
			return nil, fmt.Errorf("failed to init ring for CPU %d: %w", cpu, err)
		}
		pmr.rings = append(pmr.rings, ring)

		// Store the file descriptor in the eBPF map
		if err := array.Put(uint32(cpu), storage.FileDescriptor()); err != nil {
			pmr.Close()
			return nil, fmt.Errorf("failed to update map for CPU %d: %w", cpu, err)
		}
	}

	// Create reader
	reader := perf.NewReader()
	for _, ring := range pmr.rings {
		if err := reader.AddRing(ring); err != nil {
			pmr.Close()
			return nil, fmt.Errorf("failed to add ring to reader: %w", err)
		}
	}
	pmr.reader = reader

	return pmr, nil
}

// Reader returns the underlying perf.Reader
func (pmr *PerfMapReader) Reader() *perf.Reader {
	return pmr.reader
}

// Close releases all resources
func (pmr *PerfMapReader) Close() error {
	if pmr.reader != nil {
		pmr.reader.Finish()
	}

	for _, storage := range pmr.storage {
		if storage != nil {
			storage.Close()
		}
	}

	// Clear references
	pmr.rings = nil
	pmr.storage = nil
	pmr.reader = nil
	pmr.array = nil

	return nil
}
