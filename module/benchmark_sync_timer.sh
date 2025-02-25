#!/bin/bash

# Constants
MODULE_NAME="sync_timer_benchmark_module"
DMESG_PREFIX="sync_timer_bench:"
RUNTIME=10  # Runtime in seconds

# Function to extract numeric value from a line
extract_value() {
    echo "$1" | grep -o '[0-9]\+' | head -1
}

# Function to log to stderr
log() {
    echo "$@" >&2
}

# Clear dmesg
sudo dmesg -C

# Load the module
log "Loading benchmark module..."
sudo insmod "build/${MODULE_NAME}.ko"
if [ $? -ne 0 ]; then
    log "Failed to load module"
    exit 1
fi

# Wait for specified runtime
log "Running benchmark for ${RUNTIME} seconds..."
sleep ${RUNTIME}

# Unload the module
log "Unloading module..."
sudo rmmod "${MODULE_NAME}"
if [ $? -ne 0 ]; then
    log "Failed to unload module"
    exit 1
fi

# Process dmesg output
log "Processing results..."
dmesg | grep "${DMESG_PREFIX}" > benchmark_raw.log

# Extract global statistics
TOTAL_SAMPLES=$(grep "Total samples:" benchmark_raw.log | extract_value)
MIN_DELTA=$(grep "Global min delta:" benchmark_raw.log | extract_value)
MAX_DELTA=$(grep "Global max delta:" benchmark_raw.log | extract_value)
MEAN_DELTA=$(grep "Global mean delta:" benchmark_raw.log | extract_value)
STDDEV=$(grep "Global stddev:" benchmark_raw.log | extract_value)
MISSED_TICKS=$(grep "Total missed ticks:" benchmark_raw.log | extract_value)

# Output JSON to stdout
cat << EOF
{
    "samples": ${TOTAL_SAMPLES},
    "min_delta_ns": ${MIN_DELTA},
    "max_delta_ns": ${MAX_DELTA},
    "mean_delta_ns": ${MEAN_DELTA},
    "stddev_ns": ${STDDEV},
    "missed_ticks": ${MISSED_TICKS}
}
EOF

# Clean up
rm -f benchmark_raw.log

log "Results written to benchmark_results.csv"
log "Raw log available in benchmark_raw.log"

# Print summary
log
log "Benchmark Summary:"
log "Total Samples: ${TOTAL_SAMPLES}"
log "Min Delta: ${MIN_DELTA} ns"
log "Max Delta: ${MAX_DELTA} ns"
log "Mean Delta: ${MEAN_DELTA} ns"
log "Standard Deviation: ${STDDEV} ns"
log "Missed Ticks: ${MISSED_TICKS}" 