#!/bin/bash

# Function to log to stderr
log() {
    echo "$@" >&2
}

# Function to run a benchmark with a specific stressor
run_benchmark() {
    local name="$1"
    local stress_cmd="$2"
    local stress_pid=""

    log "Running benchmark: $name"
    
    if [ -n "$stress_cmd" ]; then
        log "Starting stress: $stress_cmd"
        $stress_cmd &
        stress_pid=$!
        sleep 2  # Let the stress command ramp up
    fi

    # Run benchmark and capture JSON output
    local json_output
    json_output=$(./benchmark_sync_timer.sh)
    local status=$?

    # Kill stress if it was started
    if [ -n "$stress_pid" ]; then
        pkill -TERM -P $stress_pid 2>/dev/null
        kill -9 $stress_pid 2>/dev/null
        wait $stress_pid 2>/dev/null
    fi

    if [ $status -ne 0 ]; then
        log "Benchmark failed for: $name"
        return 1
    fi

    # Parse JSON and output CSV line
    local samples=$(echo "$json_output" | jq -r '.samples')
    local min_delta=$(echo "$json_output" | jq -r '.min_delta_ns')
    local max_delta=$(echo "$json_output" | jq -r '.max_delta_ns')
    local mean_delta=$(echo "$json_output" | jq -r '.mean_delta_ns')
    local stddev=$(echo "$json_output" | jq -r '.stddev_ns')
    local missed_ticks=$(echo "$json_output" | jq -r '.missed_ticks')

    echo "$name,$samples,$min_delta,$max_delta,$mean_delta,$stddev,$missed_ticks"
}

# Output CSV header
echo "test,samples,min_delta_ns,max_delta_ns,mean_delta_ns,stddev_ns,missed_ticks"

# Run baseline benchmark
run_benchmark "baseline" ""

# CPU and scheduler stress
run_benchmark "cpu_stress" "stress-ng --cpu \$(nproc) --cpu-method matrixprod"

# Memory and cache contention
run_benchmark "memory_stress" "stress-ng --vm 4 --vm-bytes 75%"

# Interrupt generation
run_benchmark "interrupt_stress" "stress-ng --timer 8 --timer-freq 1000"

# I/O and system call pressure
run_benchmark "io_stress" "stress-ng --hdd 4 --hdd-bytes 1G"
run_benchmark "syscall_stress" "stress-ng --syscall 4"

# Lock contention
run_benchmark "lock_stress" "stress-ng --lockbus 4"
run_benchmark "mutex_stress" "stress-ng --mutex 8" 