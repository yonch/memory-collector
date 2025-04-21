package sync_timer

import (
	"fmt"
	"runtime"
	"time"

	"github.com/cilium/ebpf"
	"golang.org/x/sys/unix"
)

// SyncTimer manages the eBPF-based synchronized timer system
type SyncTimer struct {
	initProgram *ebpf.Program
	timerStates *ebpf.Map
	initStatus  *ebpf.Map
}

// NewSyncTimer creates a new synchronized timer system
func NewSyncTimer(initProgram *ebpf.Program, timerStates *ebpf.Map, initStatus *ebpf.Map) *SyncTimer {
	return &SyncTimer{
		initProgram: initProgram,
		timerStates: timerStates,
		initStatus:  initStatus,
	}
}

// Start initializes and starts the synchronized timer system
func (st *SyncTimer) Start() error {
	// Wait for initialization to complete
	timeout := time.After(time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	guard, err := NewCPUAffinityGuard()
	if err != nil {
		return fmt.Errorf("creating CPU affinity guard: %w", err)
	}
	defer guard.Close()

	// Initialize timers on each CPU
	for cpu := 0; cpu < runtime.NumCPU(); cpu++ {
		err := setCPUAffinity(cpu)
		if err != nil {
			return fmt.Errorf("setting CPU affinity for CPU %d: %w", cpu, err)
		}

		// Run the initialization program on the target CPU
		ret, err := st.initProgram.Run(nil)
		if err != nil {
			return fmt.Errorf("running init_timer on CPU %d: %w", cpu, err)
		}
		fmt.Printf("init_timer returned: %d\n", ret)
	}

	for {
		select {
		case <-timeout:
			return fmt.Errorf("timed out waiting for timer initialization")
		case <-ticker.C:
			// Check initialization status for all CPUs
			allInitialized := true
			initializedCount := 0
			for cpu := 0; cpu < runtime.NumCPU(); cpu++ {
				var initFlag uint8
				if err := st.initStatus.Lookup(uint32(cpu), &initFlag); err != nil || initFlag == 0 {
					allInitialized = false
					continue
				}
				initializedCount++
			}
			fmt.Printf("initializedCount: %d\n", initializedCount)

			if allInitialized {
				return nil
			}
		}
	}
}

// Stop cleans up the synchronized timer system
func (st *SyncTimer) Stop() {
	// Reset all timer states to stop the timers
	for cpu := 0; cpu < runtime.NumCPU(); cpu++ {
		key := uint32(cpu)
		_ = st.timerStates.Delete(key)
		_ = st.initStatus.Delete(key)
	}
}

// setCPUAffinity sets the CPU affinity for the current thread to a specific CPU core
func setCPUAffinity(cpu int) error {
	// Create a CPU set with only the specified CPU
	var cpuSet unix.CPUSet
	cpuSet.Zero()
	cpuSet.Set(cpu)

	// Set the CPU affinity for the current thread
	pid := unix.Gettid()
	return unix.SchedSetaffinity(pid, &cpuSet)
}

// CPU affinity guard: on instantiation, reads the current CPU affinity. Its Close method restores the original CPU affinity.
type CPUAffinityGuard struct {
	originalCPUSet unix.CPUSet
}

func (c *CPUAffinityGuard) Close() error {
	pid := unix.Gettid()
	return unix.SchedSetaffinity(pid, &c.originalCPUSet)
}

// NewCPUAffinityGuard creates a new CPU affinity guard
func NewCPUAffinityGuard() (*CPUAffinityGuard, error) {
	guard := &CPUAffinityGuard{}
	guard.originalCPUSet.Zero()

	pid := unix.Gettid()
	// Get the current CPU affinity
	err := unix.SchedGetaffinity(pid, &guard.originalCPUSet)
	if err != nil {
		return nil, fmt.Errorf("getting current CPU affinity: %w", err)
	}
	return guard, nil
}
