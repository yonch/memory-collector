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

# Make sure we're in the benchmark directory
cd "$(dirname "$0")"

# Generate temporary CSV file
TEMP_CSV="/tmp/sync_timer_benchmark_${NAME}_$$.csv"

log "Running benchmark: $NAME"

# Start stress command if specified
if [ -n "$STRESS_CMD" ]; then
    log "Starting stress: $STRESS_CMD"
    eval "$STRESS_CMD" 1>&2 2>&2 &
    STRESS_PID=$!
    sleep 2  # Let the stress command ramp up
fi

# Run the benchmark
log "Starting benchmark for $DURATION seconds..."
./sync_timer_benchmark -duration "${DURATION}s" -csv "$TEMP_CSV" -experiment "$NAME"

# Kill stress if it was started
if [ -n "$STRESS_PID" ]; then
    log "Stopping stressor..."
    pkill -TERM -P "$STRESS_PID" 2>/dev/null || true
    kill -9 "$STRESS_PID" 2>/dev/null || true
    wait "$STRESS_PID" 2>/dev/null || true
fi

# Append results to output CSV, skipping header if file exists
if [ -f "$OUTPUT_CSV" ]; then
    # Skip header line when appending
    tail -n +2 "$TEMP_CSV" >> "$OUTPUT_CSV"
else
    # Copy entire file if output doesn't exist
    cp "$TEMP_CSV" "$OUTPUT_CSV"
fi

# Clean up temporary file
rm -f "$TEMP_CSV"

log "Benchmark complete. Results appended to $OUTPUT_CSV" 