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

echo "Starting perf recording..."
sudo perf record -e memory_collector/sampling=1/ -a -o perf.data sleep 1

echo "Generating perf report..."
sudo perf script -i perf.data > perf_output.txt

echo "Validating output..."
# Check sample count (expect ~1000 samples per CPU)
CPU_COUNT=$(nproc)
EXPECTED_MIN=$((900 * CPU_COUNT))
SAMPLE_COUNT=$(wc -l < perf_output.txt)

if [ $SAMPLE_COUNT -lt $EXPECTED_MIN ]; then
    echo "Error: Got $SAMPLE_COUNT samples, expected at least $EXPECTED_MIN"
    exit 1
fi

# Validate data format
if ! grep -q "timestamp:" perf_output.txt; then
    echo "Error: Missing timestamp data"
    exit 1
fi

if ! grep -q "core_id:" perf_output.txt; then
    echo "Error: Missing core_id data"
    exit 1
fi

if ! grep -q "comm:" perf_output.txt; then
    echo "Error: Missing comm data"
    exit 1
fi

echo "Unloading module..."
sudo rmmod memory_collector

echo "Test completed successfully!"
echo "Sample count: $SAMPLE_COUNT"
echo "Output saved to perf_output.txt" 