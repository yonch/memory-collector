package main

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang -type benchmark_msg bpf benchmark.bpf.c

import (
	"bytes"
	"encoding/binary"
	"encoding/csv"
	"flag"
	"fmt"
	"log"
	"math"
	"math/bits"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/rlimit"
	"github.com/unvariance/collector/pkg/perf_ebpf"
	"github.com/unvariance/collector/pkg/sync_timer"
)

func main() {
	// Parse command line flags
	duration := flag.Duration("duration", 10*time.Second, "Duration to run the benchmark")
	csvFile := flag.String("csv", "results.csv", "Output CSV file for benchmark results")
	experimentName := flag.String("experiment", "", "Name of the experiment (e.g., baseline, cpu_stress)")
	flag.Parse()

	// Allow the current process to lock memory for eBPF resources
	if err := rlimit.RemoveMemlock(); err != nil {
		fmt.Printf("Error removing memlock: %v\n", err)
		os.Exit(1)
	}

	// Load pre-compiled BPF program
	objs := bpfObjects{}
	if err := loadBpfObjects(&objs, nil); err != nil {
		fmt.Printf("Error loading BPF objects: %v\n", err)
		os.Exit(1)
	}
	defer objs.Close()

	// Attach HR Timer expire exit tracepoint
	tp, err := link.Tracepoint("timer", "hrtimer_expire_exit", objs.HandleHrtimerExpireExit, nil)
	if err != nil {
		fmt.Printf("Error attaching tracepoint: %v\n", err)
		os.Exit(1)
	}
	defer tp.Close()

	// Create sync timer with the benchmark implementation
	timer := sync_timer.NewSyncTimer(
		objs.SyncTimerInitBenchmark,
		objs.SyncTimerStatesBenchmark,
		objs.SyncTimerInitStatusBenchmark,
	)

	// Calculate buffer size for perf rings
	// Each event is 24 bytes (3 uint64s) + 8 bytes overhead
	// We expect 1000 events per second (1ms interval)
	// Multiply by duration in seconds and add some headroom
	eventSize := 32 // 24 bytes + 8 bytes overhead
	eventsPerSecond := 1000
	expectedEvents := int(float64(duration.Seconds()) * float64(eventsPerSecond))
	totalSize := eventSize * expectedEvents

	// Round up to next power of 2 pages
	pageSize := os.Getpagesize()
	pages := (totalSize + pageSize - 1) / pageSize
	pages = max(pages, 1)
	// Round up to next power of 2
	pages = 1 << (64 - bits.LeadingZeros64(uint64(pages-1)))
	bufferSize := pages * pageSize

	// Set up perf rings for ebpf -> userspace
	opts := perf_ebpf.Options{
		BufferSize:     bufferSize,
		WatermarkBytes: uint32(bufferSize / 2),
	}

	// Create our perf map reader
	rd, err := perf_ebpf.NewPerfMapReader(objs.Events, opts)
	if err != nil {
		fmt.Printf("Error creating perf map reader: %v\n", err)
		os.Exit(1)
	}
	defer rd.Close()

	// Start sync timer
	if err := timer.Start(); err != nil {
		fmt.Printf("Error starting sync timer: %v\n", err)
		os.Exit(1)
	}
	defer timer.Stop()

	// Set up signal handling
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)

	// Create CSV file
	outputFile, err := os.Create(*csvFile)
	if err != nil {
		fmt.Printf("Error creating CSV file: %v\n", err)
		os.Exit(1)
	}
	defer outputFile.Close()

	// Write CSV header
	csvWriter := csv.NewWriter(outputFile)
	defer csvWriter.Flush()
	if err := csvWriter.Write([]string{
		"timestamp",
		"tick",
		"min_delay",
		"max_delay",
		"mean_delay",
		"stddev",
		"samples",
		"experiment",
	}); err != nil {
		fmt.Printf("Error writing CSV header: %v\n", err)
		os.Exit(1)
	}

	// Run for specified duration
	fmt.Printf("Running benchmark for %v...\n", *duration)
	select {
	case <-time.After(*duration):
		fmt.Println("Benchmark completed")
	case <-sig:
		fmt.Println("Received signal, stopping benchmark")
	}

	// Read and process events
	reader := rd.Reader()
	if err := reader.Start(); err != nil {
		fmt.Printf("Error starting reader: %v\n", err)
		os.Exit(1)
	}
	defer reader.Finish()

	// Map to store statistics per tick number
	tickStats := make(map[uint64]struct {
		count           uint64
		sumDelta        uint64
		minDelta        uint64
		maxDelta        uint64
		sumSquaredDelta uint64
	})

	for !reader.Empty() {
		// Get current ring
		ring, _, err := reader.CurrentRing()
		if err != nil {
			fmt.Printf("Error getting current ring: %v\n", err)
			break
		}

		// Read event data
		eventData := make([]byte, 24) // Size of bench_event struct
		if err := ring.PeekCopy(eventData, 4); err != nil {
			fmt.Printf("Error reading event: %v\n", err)
			break
		}

		// Parse event
		var event bpfBenchmarkMsg
		if err := binary.Read(bytes.NewReader(eventData), binary.LittleEndian, &event); err != nil {
			log.Printf("Failed to parse perf event: %s", err)
			break
		}

		// Update statistics for this tick
		stats := tickStats[event.TickNumber]
		stats.count++
		stats.sumDelta += event.Delta
		stats.sumSquaredDelta += event.Delta * event.Delta
		if stats.count == 1 || event.Delta < stats.minDelta {
			stats.minDelta = event.Delta
		}
		if stats.count == 1 || event.Delta > stats.maxDelta {
			stats.maxDelta = event.Delta
		}
		tickStats[event.TickNumber] = stats

		// Consume the event
		if err := reader.Pop(); err != nil {
			fmt.Printf("Error consuming event: %v\n", err)
			break
		}
	}

	// Write statistics to CSV
	for tickNumber, stats := range tickStats {
		meanDelay := float64(stats.sumDelta) / float64(stats.count)
		variance := float64(stats.sumSquaredDelta)/float64(stats.count) - (meanDelay * meanDelay)
		stddev := math.Sqrt(variance)

		if err := csvWriter.Write([]string{
			fmt.Sprintf("%d", tickNumber*1_000_000),
			fmt.Sprintf("%d", tickNumber),
			fmt.Sprintf("%d", stats.minDelta),
			fmt.Sprintf("%d", stats.maxDelta),
			fmt.Sprintf("%.2f", meanDelay),
			fmt.Sprintf("%.2f", stddev),
			fmt.Sprintf("%d", stats.count),
			*experimentName,
		}); err != nil {
			fmt.Printf("Error writing CSV row: %v\n", err)
			break
		}
	}

	// Print summary statistics
	var totalEvents uint64
	var totalDelta uint64
	var minDelta uint64 = ^uint64(0)
	var maxDelta uint64

	for _, stats := range tickStats {
		totalEvents += stats.count
		totalDelta += stats.sumDelta
		if stats.minDelta < minDelta {
			minDelta = stats.minDelta
		}
		if stats.maxDelta > maxDelta {
			maxDelta = stats.maxDelta
		}
	}

	if totalEvents > 0 {
		avgDelta := float64(totalDelta) / float64(totalEvents)
		fmt.Printf("\nBenchmark Statistics:\n")
		fmt.Printf("Total Events: %d\n", totalEvents)
		fmt.Printf("Average Delta: %.2f ns\n", avgDelta)
		fmt.Printf("Minimum Delta: %d ns\n", minDelta)
		fmt.Printf("Maximum Delta: %d ns\n", maxDelta)
	}
}
