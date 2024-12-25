package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"strconv"
)

type PerfCmd struct {
	cmd    *exec.Cmd
	output *bytes.Buffer
}

func NewPerfCmd(pid int) *PerfCmd {
	var buf bytes.Buffer

	cmd := exec.Command("perf", "stat", "-j", "-e", "instructions,cycles", "-p", strconv.Itoa(pid))
	cmd.Stdout = &buf
	cmd.Stderr = &buf

	return &PerfCmd{
		cmd:    cmd,
		output: &buf,
	}
}

func (p *PerfCmd) Start() error {
	return p.cmd.Start()
}

func (p *PerfCmd) End() (*PerfOutput, error) {
	// Send Ctrl-C to the perf process...
	if err := p.cmd.Process.Signal(os.Interrupt); err != nil {
		return nil, err
	}

	// ... and wait for it to finish writing to stdout/stderr buffers and exit.
	if err := p.cmd.Wait(); err != nil {
		if _, ok := err.(*exec.ExitError); !ok {
			return nil, err
		}
	}

	output, err := parsePerfCmdOutput(p.output)
	if err != nil {
		return nil, err
	}

	return output, nil
}

type perfOutput struct {
	Event string `json:"event"`
	Count string `json:"counter-value"`
}

type PerfOutput struct {
	Instrs float64
	Cycles float64
}

func parsePerfCmdOutput(output io.Reader) (*PerfOutput, error) {
	scanner := bufio.NewScanner(output)
	var out PerfOutput

	for scanner.Scan() {
		line := scanner.Text()
		var data perfOutput
		err := json.Unmarshal([]byte(line), &data)
		if err != nil {
			return nil, err
		}

		count, err := strconv.ParseFloat(data.Count, 64)

		if err != nil {
			return nil, err
		}

		switch data.Event {
		case "instructions":
			out.Instrs = count
		case "cycles":
			out.Cycles = count
		}
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return &out, nil
}
