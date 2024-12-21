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

	sum := float64(0)
	gc, err := p.MeasureGroup(func() {
		heavy_workload(&sum)
	})

	if err != nil {
		log.Fatalf("Failed to measure perf group: %s\n", err)
	}

	cycles, instrs := gc.Values[1].Value, gc.Values[0].Value
	log.Printf("Sum is %f\n", sum)
	log.Printf("Ran for %vns\n", gc.Running.Nanoseconds())
	log.Printf("Cycles: %d, Instrs: %d, CPI: %f\n", cycles, instrs, float64(cycles)/float64(instrs))
}

func heavy_workload(sum *float64) {
	for i := 0; i < 1000000; i++ {
		*sum += float64(i)
	}
}
