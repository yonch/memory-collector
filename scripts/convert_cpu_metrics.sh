#!/bin/bash

# Simple script to convert pidstat CPU metrics from semicolon to comma-separated format
# Usage: ./convert_cpu_metrics.sh input_file output_file

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <input_file> <output_file>"
    echo "  <input_file>: Path to the raw CPU metrics file"
    echo "  <output_file>: Path where the converted CSV will be written"
    exit 1
fi

input_file="$1"
output_file="$2"

# Create CSV header
echo "timestamp,uid,pid,usr_pct,system_pct,guest_pct,wait_pct,cpu_pct,cpu_num,command" > "$output_file"

# Use awk for simple conversion - just replace semicolons with commas and handle quoting
awk 'BEGIN { FS=";" }

# Skip the Linux metadata line and empty lines
/^Linux/ || /^$/ { next }

# Skip header lines
/^[0-9]+;UID;PID/ || /^UID;PID/ { next }

# Process data rows
NF >= 9 {
    # Just convert semicolons to commas for the first 9 fields
    printf "%s,%s,%s,%s,%s,%s,%s,%s,%s", $1, $2, $3, $4, $5, $6, $7, $8, $9;
    
    # Handle the command field (if any)
    if (NF >= 10) {
        command = $10;
        for (i=11; i<=NF; i++) {
            command = command ";" $i;
        }
        printf ",\"%s\"", command;
    } else {
        printf ",\"\"";
    }
    
    printf "\n";
}' "$input_file" >> "$output_file"

echo "Conversion complete: $output_file" 