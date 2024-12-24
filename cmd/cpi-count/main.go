package main

import (
	"bytes"
	"log"
	"os"
	"os/exec"
	"runtime"
	"strconv"
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

	var perfOutputBuf bytes.Buffer

	perfCmd := exec.Command("perf", "stat", "-j", "-e", "instructions,cycles", "-p", strconv.Itoa(pid))
	perfCmd.Stdout = &perfOutputBuf
	perfCmd.Stderr = &perfOutputBuf

	if err := perfCmd.Start(); err != nil {
		log.Fatalf("Failed to execute perf cmd\n")
	}
	log.Printf("Started perf cmd\n")

	time.Sleep(100 * time.Millisecond)

	sum := float64(0)
	gc, err := p.MeasureGroup(func() {
		heavyWorkload(&sum)
	})

	if err != nil {
		log.Fatalf("Failed to measure perf group: %s\n", err)
	}

	cycles, instrs := gc.Values[1].Value, gc.Values[0].Value
	log.Printf("Sum is %f\n", sum)
	log.Printf("Ran for %dms\n", gc.Running.Milliseconds())
	log.Printf("GoPerf Cycles: %d, GoPerf Instrs: %d, GoPerf CPI: %f\n", cycles, instrs, float64(cycles)/float64(instrs))

	if err := perfCmd.Process.Signal(os.Interrupt); err != nil {
		log.Fatalf("Failed to send SIGINT to perf: %v\n", err)
	}

	if err := perfCmd.Wait(); err != nil {
		if _, ok := err.(*exec.ExitError); !ok {
			log.Fatalf("Error waiting for perf cmd: %v\n", err)
		}
	}

	perfOutput := parsePerfCmdOutput(&perfOutputBuf)
	log.Printf("PerfCmd Cycles: %d, PerfCmd Instrs: %d, PerfCmd CPI: %f\n", int64(perfOutput.Cycles), int64(perfOutput.Instrs), perfOutput.Cycles/perfOutput.Instrs)
}

func heavyWorkload(sum *float64) {
	for i := 0; i < 10000000; i++ {
		*sum += float64(i)
	}
}
