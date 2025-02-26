#!/bin/bash
set -e

# Function to print usage
usage() {
    echo "Usage: $0 <trace_file> <experiment_name> <output_csv>"
    echo
    echo "Process sync timer benchmark trace data and generate CSV output"
    echo
    echo "Arguments:"
    echo "  trace_file      - Input trace file from trace-cmd"
    echo "  experiment_name - Name of experiment (e.g., baseline, cpu_stress)"
    echo "  output_csv      - Output CSV file path"
    exit 1
}

# Check arguments
if [ "$#" -ne 3 ]; then
    usage
fi

TRACE_FILE="$1"
EXPERIMENT="$2"
OUTPUT_CSV="$3"

# Create CSV header if file doesn't exist
if [ ! -f "$OUTPUT_CSV" ]; then
    echo "timestamp,tick,min_delay,max_delay,mean_delay,stddev,samples,missing,experiment" > "$OUTPUT_CSV"
fi

# Process trace file and append to CSV in one go
# Format: ts=<ts> tick=<tick> min=<min> max=<max> mean=<mean> stddev=<stddev> samples=<n> missing=<m>
trace-cmd report -i "$TRACE_FILE" | grep "sync_timer_stats:" | \
awk -v experiment="$EXPERIMENT" '{
    for(i=1; i<=NF; i++) {
        split($i, pair, "=")
        if(pair[1] == "ts") ts = pair[2]
        if(pair[1] == "tick") tick = pair[2]
        if(pair[1] == "min") min = pair[2]
        if(pair[1] == "max") max = pair[2]
        if(pair[1] == "mean") mean = pair[2]
        if(pair[1] == "stddev") stddev = pair[2]
        if(pair[1] == "samples") samples = pair[2]
        if(pair[1] == "missing") missing = pair[2]
    }
    printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n", ts, tick, min, max, mean, stddev, samples, missing, experiment >> "'$OUTPUT_CSV'"
}'

echo "Processing complete. Data appended to $OUTPUT_CSV" 