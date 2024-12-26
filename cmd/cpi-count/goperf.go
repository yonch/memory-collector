package main

import (
	"crypto/sha256"
	"encoding/base64"

	"github.com/elastic/go-perf"
)

type GoPerf struct {
	group      *perf.Group
	event      *perf.Event
	groupCount *perf.GroupCount

	// This struct stores the output of the measured workload
	// to prevent the compiler from optimizing the work away
	workloadOutput string
}

func NewGoPerf() *GoPerf {
	group := perf.Group{
		CountFormat: perf.CountFormat{
			Running: true,
		},
	}
	group.Add(perf.Instructions, perf.CPUCycles)

	goperf := GoPerf{
		group: &group,
	}

	return &goperf
}

func (p *GoPerf) StartWorkloadMeasurement() error {
	evt, err := p.group.Open(perf.CallingThread, perf.AnyCPU)
	if err != nil {
		return err
	}
	p.event = evt

	gc, err := p.event.MeasureGroup(func() {
		p.workloadOutput = heavyWorkload()
	})

	if err != nil {
		return err
	}

	p.groupCount = &gc

	return nil
}

func (p *GoPerf) End() (*PerfOutput, error) {
	if err := p.event.Close(); err != nil {
		return nil, err
	}

	return &PerfOutput{
		Instrs: float64(p.groupCount.Values[0].Value),
		Cycles: float64(p.groupCount.Values[1].Value),
	}, nil
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
