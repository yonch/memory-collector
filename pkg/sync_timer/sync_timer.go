package sync_timer

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang bpf sync_timer.bpf.c

import (
	"fmt"
	"time"

	"runtime"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/rlimit"
	"github.com/unvariance/collector/pkg/perf_ebpf"
	"golang.org/x/sys/unix"
)

// SyncTimer manages the eBPF-based synchronized timer system
type SyncTimer struct {
	objs bpfObjects
}

// NewSyncTimer creates a new synchronized timer system
func NewSyncTimer() (*SyncTimer, error) {
	// Allow the current process to lock memory for eBPF resources
	if err := rlimit.RemoveMemlock(); err != nil {
		return nil, fmt.Errorf("removing memlock: %w", err)
	}

	// Load pre-compiled BPF program
	objs := bpfObjects{}
	if err := loadBpfObjects(&objs, nil); err != nil {
		return nil, fmt.Errorf("loading BPF objects: %w", err)
	}

	return &SyncTimer{
		objs: objs,
	}, nil
}

// Start initializes and starts the synchronized timer system
func (st *SyncTimer) Start() error {
	// Configure perf events for initialization
	commonOpts := unix.PerfEventAttr{
		Type:        unix.PERF_TYPE_HARDWARE,
		Config:      unix.PERF_COUNT_HW_CPU_CYCLES,
		Sample:      1000000, // Sample every 1M cycles
		Sample_type: unix.PERF_SAMPLE_RAW,
		Read_format: unix.PERF_FORMAT_TOTAL_TIME_ENABLED | unix.PERF_FORMAT_TOTAL_TIME_RUNNING,
		Bits:        unix.PerfBitDisabled | unix.PerfBitExcludeKernel,
		Wakeup:      1,
	}

	// Create event opener for initialization
	initEvents, err := perf_ebpf.NewEventOpener(st.objs.InitEvents, commonOpts)
	if err != nil {
		return fmt.Errorf("creating event opener: %w", err)
	}
	defer initEvents.Close()

	// Start initialization events
	if err := initEvents.Start(); err != nil {
		return fmt.Errorf("starting init events: %w", err)
	}

	// Attach the initialization program to each perf event
	links := make([]*link.RawLink, 0, runtime.NumCPU())
	defer func() {
		// Clean up all links
		for _, link := range links {
			link.Close()
		}
	}()

	for cpu := 0; cpu < runtime.NumCPU(); cpu++ {
		// Get the file descriptor from the init_events map
		var fd uint32
		if err := st.objs.InitEvents.Lookup(uint32(cpu), &fd); err != nil {
			return fmt.Errorf("looking up init event FD for CPU %d: %w", cpu, err)
		}

		// Create and attach the raw link
		rawLink, err := link.AttachRawLink(link.RawLinkOptions{
			Target:  int(fd),
			Program: st.objs.InitTimers,
			Attach:  ebpf.AttachPerfEvent,
		})
		if err != nil {
			return fmt.Errorf("attaching init program to CPU %d: %w", cpu, err)
		}
		links = append(links, rawLink)
	}

	// Wait for initialization to complete
	timeout := time.After(time.Second)
	ticker := time.NewTicker(time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			return fmt.Errorf("timed out waiting for timer initialization")
		case <-ticker.C:
			// Check initialization status for all CPUs
			allInitialized := true
			for cpu := 0; cpu < runtime.NumCPU(); cpu++ {
				var initFlag uint8
				if err := st.objs.InitStatus.Lookup(uint32(cpu), &initFlag); err != nil || initFlag == 0 {
					allInitialized = false
					break
				}
			}

			if allInitialized {
				return nil
			}
		}
	}
}

// Stop cleans up the synchronized timer system
func (st *SyncTimer) Stop() {
	// Reset all timer states to stop the timers
	key := uint32(0) // Delete the timer state to stop the timer
	st.objs.TimerStates.Delete(key)

	st.objs.Close()
}

// SetCallback configures the callback function for the timer
func (st *SyncTimer) SetCallback(prog *ebpf.Program) error {
	// Update the callback in the BPF program array
	if err := st.objs.Callbacks.Put(uint32(0), prog); err != nil {
		return fmt.Errorf("setting callback: %w", err)
	}
	return nil
}
