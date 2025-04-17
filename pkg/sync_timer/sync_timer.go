package sync_timer

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang bpf sync_timer.bpf.c

import (
	"fmt"
	"os"
	"time"

	"runtime"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/rlimit"
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

// Return the callback program
func (st *SyncTimer) GetCallbackProgram() *ebpf.Program {
	return st.objs.Callback
}

// Start initializes and starts the synchronized timer system
func (st *SyncTimer) Start() error {
	// Wait for initialization to complete
	timeout := time.After(time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	// Initialize timers on each CPU
	for cpu := 0; cpu < runtime.NumCPU(); cpu++ {
		setCPUAffinity(cpu)

		// Run the initialization program on the target CPU
		ret, err := st.objs.InitTimer.Run(nil)
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
				if err := st.objs.InitStatus.Lookup(uint32(cpu), &initFlag); err != nil || initFlag == 0 {
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
	key := uint32(0) // Delete the timer state to stop the timer
	st.objs.TimerStates.Delete(key)

	// Remove the cgroup
	os.RemoveAll(st.cgroupPath)

	st.objs.Close()
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
