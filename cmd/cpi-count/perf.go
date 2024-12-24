package main

import (
	"bufio"
	"encoding/json"
	"io"
	"log"
	"strconv"
)

type perfOutput struct {
	Event string `json:"event"`
	Count string `json:"counter-value"`
}

type perfOutputCollated struct {
	Instrs float64
	Cycles float64
}

func parsePerfCmdOutput(output io.Reader) perfOutputCollated {
	scanner := bufio.NewScanner(output)
	var out perfOutputCollated

	for scanner.Scan() {
		line := scanner.Text()
		var data perfOutput
		err := json.Unmarshal([]byte(line), &data)
		if err != nil {
			log.Fatalf("Failed to parse perf cmd output: %s\n", line)
		}

		count, err := strconv.ParseFloat(data.Count, 64)

		if err != nil {
			log.Fatalf("Failed to parse perf cmd counter value: %s\n", data.Count)
		}

		switch data.Event {
		case "instructions":
			out.Instrs = count
		case "cycles":
			out.Cycles = count
		}
	}

	if err := scanner.Err(); err != nil {
		log.Fatalf("Failed to scan perf cmd output\n")
	}

	return out
}
