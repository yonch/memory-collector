package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"log"
	"os"
	"os/signal"
	"time"

	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/perf"
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

	// Open a perf reader from userspace
	rd, err := perf.NewReader(objs.Events, os.Getpagesize())
	if err != nil {
		log.Fatal(err)
	}
	defer rd.Close()

	// Catch CTRL+C
	stopper := make(chan os.Signal, 1)
	signal.Notify(stopper, os.Interrupt)

	// Print the event count every second for 5 seconds
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()

	timeout := time.After(5 * time.Second)


	log.Println("Waiting for events...")

	// Counter to maintain in userspace
	var totalEvents uint64 = 0

	for {
		select {
		case <-stopper:
			log.Printf("Received interrupt, exiting... Total events: %d\n", totalEvents)
			return

		case <-ticker.C:
			log.Printf("Event count: %d\n", totalEvents)

		case <-timeout:
			log.Println("Finished counting after 5 seconds")
			return

		default:
			record, err := rd.Read()
			if err != nil {
				if errors.Is(err, perf.ErrClosed) {
					return
				}
				log.Printf("Reading from perf event reader: %s", err)
				continue
			}

			if record.LostSamples != 0 {
				log.Printf("Lost %d samples", record.LostSamples)
				continue
			}

			// Parse the raw bytes into our Event struct
			var event taskCounterEvent
			if err := binary.Read(bytes.NewReader(record.RawSample), binary.LittleEndian, &event); err != nil {
				log.Printf("Failed to parse perf event: %s", err)
				continue
			}

			totalEvents++
		}
	}
}