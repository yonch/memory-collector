//go:build linux

package main

import (
	"fmt"
	"runtime"
	"sync"

	"github.com/cilium/ebpf"
	"golang.org/x/sys/unix"
)

// EventOpener manages perf event file descriptors for hardware events
type EventOpener struct {
	mu       sync.Mutex
	array    *ebpf.Map
	eventFDs []int
}

// NewEventOpener creates perf events for each CPU with the given attributes
func NewEventOpener(array *ebpf.Map, attr unix.PerfEventAttr) (*EventOpener, error) {
	nCPU := int(array.MaxEntries())
	eventFDs := make([]int, 0, nCPU)

	// Clone the map to keep a reference
	array, err := array.Clone()
	if err != nil {
		return nil, err
	}

	// Create perf events for each CPU
	for cpu := 0; cpu < nCPU; cpu++ {
		fd, err := unix.PerfEventOpen(&attr, -1, cpu, -1, 0)
		if err != nil {
			// Clean up already opened FDs
			for _, fd := range eventFDs {
				unix.Close(fd)
			}
			return nil, fmt.Errorf("failed to open perf event on CPU %d: %v", cpu, err)
		}

		eventFDs = append(eventFDs, fd)

		// Store FD in map
		if err := array.Put(uint32(cpu), uint32(fd)); err != nil {
			// Clean up
			for _, fd := range eventFDs {
				unix.Close(fd)
			}
			return nil, fmt.Errorf("failed to update map for CPU %d: %v", cpu, err)
		}
	}

	eo := &EventOpener{
		array:    array,
		eventFDs: eventFDs,
	}
	runtime.SetFinalizer(eo, (*EventOpener).Close)
	return eo, nil
}

// Close cleans up the event opener resources
func (eo *EventOpener) Close() error {
	eo.mu.Lock()
	defer eo.mu.Unlock()

	if eo.eventFDs == nil {
		return nil
	}

	var firstErr error
	for _, fd := range eo.eventFDs {
		if err := unix.Close(fd); err != nil && firstErr == nil {
			firstErr = err
		}
	}

	if err := eo.array.Close(); err != nil && firstErr == nil {
		firstErr = err
	}

	eo.eventFDs = nil
	eo.array = nil

	return firstErr
} 