package main

import (
	"crypto/sha256"
	"encoding/base64"
	"log"
	"os"
	"runtime"

	"github.com/elastic/go-perf"
)

func init() {
	// Pinning to a specific OS thread ensures that the measurements
	// using both the `go-perf` library and Linux `perf` produce
	// similar measurements.
	// Interestingly, although undesired, if this step is omitted:
	// - Linux perf reports ~4x the number of cycles AND instructions
	// - The CPI remains relatively similar because both cycles and instructions increase proportionally
	runtime.LockOSThread()
}

func main() {
	pid := os.Getpid()
	log.Printf("Current PID: %d\n", pid)

	perfCmd := NewPerfCmd(pid)
	if err := perfCmd.Start(); err != nil {
		log.Fatalf("Failed to execute perf cmd: %v\n", err)
	}

	log.Printf("Started perf cmd\n")
	g := perf.Group{
		CountFormat: perf.CountFormat{
			Running: true,
		},
	}
	g.Add(perf.Instructions, perf.CPUCycles)

	p, err := g.Open(perf.CallingThread, perf.AnyCPU)
	if err != nil {
		log.Fatalf("Failed to open perf events: %s\n", err)
	}

	var workloadOutput string
	gc, err := p.MeasureGroup(func() {
		workloadOutput = heavyWorkload()
	})

	if err != nil {
		log.Fatalf("Failed to measure perf group: %s\n", err)
	}

	p.Close()
	perfOutput, err := perfCmd.End()
	if err != nil {
		log.Fatalf("Failed to end perf cmd: %v\n", err)
	}

	cycles, instrs := gc.Values[1].Value, gc.Values[0].Value
	log.Printf("Output is %s\n", workloadOutput)
	log.Printf("Ran for %dms\n", gc.Running.Milliseconds())
	log.Printf("GoPerf Cycles: %d, GoPerf Instrs: %d, GoPerf CPI: %f\n", cycles, instrs, float64(cycles)/float64(instrs))
	log.Printf("PerfCmd Cycles: %d, PerfCmd Instrs: %d, PerfCmd CPI: %f\n", int64(perfOutput.Cycles), int64(perfOutput.Instrs), perfOutput.Cycles/perfOutput.Instrs)
}

func heavyWorkload() string {
	seedStr := "1sAMsDJGtS3zNrK6MfeysFvUYOzlHqtj"

	var hash string
	hashBytes := sha256.Sum256([]byte(seedStr))

	for i := 0; i < 999999; i++ {
		hash = base64.StdEncoding.EncodeToString(hashBytes[:])
		hashBytes = sha256.Sum256([]byte(hash))
	}

	return hash
}
