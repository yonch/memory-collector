package main

import (
	"log"
	"runtime"

	"github.com/elastic/go-perf"
)

func main() {
	g := perf.Group{}
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

	sum := 0
	gc, err := p.MeasureGroup(func() {
		for i := 0; i < 10000; i++ {
			sum += i
		}
	})

	if err != nil {
		log.Fatalf("Failed to measure perf group: %s\n", err)
	}

	cycles, instrs := gc.Values[1].Value, gc.Values[0].Value
	log.Printf("Ran for %vms\n", gc.Running.Milliseconds())
	log.Printf("Cycles: %d, Instrs: %d, CPI: %f\n", cycles, instrs, float64(cycles)/float64(instrs))
}
