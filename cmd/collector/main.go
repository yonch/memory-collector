package main

import (
	"log"
	"os"
	"os/signal"
	"time"

	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/rlimit"
)

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang taskCounter task_counter.c -- -I/usr/include/x86_64-linux-gnu

func main() {
	// Allow the current process to lock memory for eBPF resources
	if err := rlimit.RemoveMemlock(); err != nil {
		log.Fatal(err)
	}

	// Load pre-compiled programs and maps into the kernel
	objs := taskCounterObjects{}
	if err := loadTaskCounterObjects(&objs, nil); err != nil {
		log.Fatal(err)
	}
	defer objs.Close()

	// Attach the tracepoint program
	tp, err := link.Tracepoint("memory_collector", "memory_collector_sample", objs.CountEvents, nil)
	if err != nil {
		log.Fatal(err)
	}
	defer tp.Close()

	// Catch CTRL+C
	stopper := make(chan os.Signal, 1)
	signal.Notify(stopper, os.Interrupt)

	// Print the event count every second for 5 seconds
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()

	log.Println("Counting memory_collector_sample events...")
	
	timeout := time.After(5 * time.Second)
	for {
		select {
		case <-ticker.C:
			var count uint64
			var key uint32 = 0
			if err := objs.EventCount.Lookup(&key, &count); err != nil {
				log.Fatal(err)
			}
			log.Printf("Event count: %d\n", count)
		case <-timeout:
			log.Println("Finished counting after 5 seconds")
			return
		case <-stopper:
			log.Println("Received interrupt, exiting...")
			return
		}
	}
}