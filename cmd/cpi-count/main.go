package main

import (
	"crypto/sha256"
	"encoding/base64"
	"log"
	"os"
	"runtime"
	"time"

	"github.com/elastic/go-perf"
)

func main() {
	pid := os.Getpid()
	log.Printf("Current PID: %d\n", pid)

	g := perf.Group{
		CountFormat: perf.CountFormat{
			Running: true,
		},
	}
	g.Add(perf.Instructions, perf.CPUCycles)

	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	p, err := g.Open(perf.CallingThread, perf.AnyCPU)
	if err != nil {
		log.Fatalf("Failed to open perf events: %s\n", err)
	}
	defer func() {
		log.Printf("Finished running, closing event group.\n")
		p.Close()
	}()

	perfCmd := NewPerfCmd(pid)

	if err := perfCmd.Start(); err != nil {
		log.Fatalf("Failed to execute perf cmd: %v\n", err)
	}
	log.Printf("Started perf cmd\n")

	time.Sleep(100 * time.Millisecond)

	var workloadOutput string
	gc, err := p.MeasureGroup(func() {
		workloadOutput = heavyWorkload()
	})

	if err != nil {
		log.Fatalf("Failed to measure perf group: %s\n", err)
	}

	cycles, instrs := gc.Values[1].Value, gc.Values[0].Value
	log.Printf("Output is %s\n", workloadOutput)
	log.Printf("Ran for %dms\n", gc.Running.Milliseconds())
	log.Printf("GoPerf Cycles: %d, GoPerf Instrs: %d, GoPerf CPI: %f\n", cycles, instrs, float64(cycles)/float64(instrs))

	if err := perfCmd.End(); err != nil {
		log.Fatalf("Failed to end perf cmd: %v\n", err)
	}

	perfOutput := perfCmd.Output()
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
