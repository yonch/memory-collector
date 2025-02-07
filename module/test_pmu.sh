#!/bin/bash
set -e

echo "Building kernel module..."
make clean
make

echo "Loading kernel module..."
sudo insmod build/memory_collector.ko

echo "Verifying module is loaded..."
lsmod | grep memory_collector || {
    echo "Error: Module failed to load"
    exit 1
}

echo "Setting up tracing..."
sudo trace-cmd start -e memory_collector_sample

echo "Collecting samples for 1 second..."
sleep 1

echo "Stopping trace..."
sudo trace-cmd stop

echo "Extracting trace data..."
sudo trace-cmd extract

echo "Reading trace report..."
sudo trace-cmd report > trace_output.txt

echo "Validating output..."
# Check if we have any trace entries
SAMPLE_COUNT=$(grep "memory_collector_sample:" trace_output.txt | wc -l)
CPU_COUNT=$(nproc)
EXPECTED_MIN=$((900 * CPU_COUNT))

if [ $SAMPLE_COUNT -lt $EXPECTED_MIN ]; then
    echo "Error: Got $SAMPLE_COUNT trace entries, expected at least $EXPECTED_MIN"
    exit 1
fi

echo "Unloading module..."
sudo rmmod memory_collector

echo "Cleaning up trace..."
sudo trace-cmd reset

echo "Test completed successfully!"
echo "Sample count: $SAMPLE_COUNT"
echo "Output saved to trace_output.txt" 