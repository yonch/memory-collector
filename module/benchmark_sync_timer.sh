#!/bin/bash
set -e

# Function to log to stderr
log() {
    echo "$@" >&2
}

# Function to print usage
usage() {
    echo "Usage: $0 [-n <name>] [-s <stress_command>] [-d <duration>] [-o <output_csv>]"
    echo
    echo "Run sync timer benchmark with optional stress test"
    echo
    echo "Options:"
    echo "  -n <name>           Name of the experiment (default: benchmark)"
    echo "  -s <stress_command> Optional stress command to run during benchmark"
    echo "  -d <duration>       Duration in seconds (default: 10)"
    echo "  -o <output_csv>     Output CSV file (default: benchmark_results.csv)"
    echo "  -h                  Show this help message"
    exit 1
}

# Parse command line arguments
NAME="benchmark"
STRESS_CMD=""
DURATION=10
OUTPUT_CSV="benchmark_results.csv"

while getopts "n:s:d:o:h" opt; do
    case $opt in
        n) NAME="$OPTARG" ;;
        s) STRESS_CMD="$OPTARG" ;;
        d) DURATION="$OPTARG" ;;
        o) OUTPUT_CSV="$OPTARG" ;;
        h) usage ;;
        \?) usage ;;
    esac
done

# Make sure we're in the module directory
cd "$(dirname "$0")"

# Generate unique trace file name
TRACE_FILE="/tmp/sync_timer_trace_${NAME}_$$.dat"

log "Running benchmark: $NAME"

# Build the benchmark module if needed
if [ ! -f "build/sync_timer_benchmark_module.ko" ]; then
    log "Building benchmark module..."
    make sync_timer_benchmark
fi

# Load benchmark module and wait
log "Loading benchmark module..."
sudo insmod "build/sync_timer_benchmark_module.ko"

if [ -n "$STRESS_CMD" ]; then
    log "Starting stress: $STRESS_CMD"
    eval "$STRESS_CMD" 1>&2 2>&2 &
    STRESS_PID=$!
    sleep 2  # Let the stress command ramp up
else
    sleep .5  # Let the module ramp up
fi

# Start tracing
log "Starting trace..."
sudo trace-cmd start -e sync_timer_stats

log "Running for $DURATION seconds..."
sleep "$DURATION"

# Stop benchmark and tracing
log "Stopping trace..."
sudo trace-cmd stop

# Kill stress if it was started
if [ -n "$STRESS_PID" ]; then
    pkill -TERM -P "$STRESS_PID" 2>/dev/null
    kill -9 "$STRESS_PID" 2>/dev/null
    wait "$STRESS_PID" 2>/dev/null
fi

# Extract trace data
log "Extracting trace data..."
sudo trace-cmd extract -o "$TRACE_FILE"

log "Unloading module..."
sudo rmmod sync_timer_benchmark_module

# Process trace data
log "Processing trace data..."
./process_benchmark.sh "$TRACE_FILE" "$NAME" "$OUTPUT_CSV"

# Clean up trace file
rm -f "$TRACE_FILE"

log "Benchmark complete. Results appended to $OUTPUT_CSV" 