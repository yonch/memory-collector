#!/bin/bash

# Clear dmesg buffer
sudo dmesg -c > /dev/null

# Load the module
sudo insmod build/collector.ko

# Store the timestamp when we started
start_time=$(date +%s)

echo "Module loaded, monitoring dmesg output..."
echo "Press Ctrl+C to stop monitoring"

for i in {1..30}; do
    # Get new dmesg entries and timestamp them
    sudo dmesg -c | while read line; do
        current_time=$(($(date +%s) - start_time))
        echo "[$current_time sec] $line"
    done
    
    sleep 1
done 

sudo rmmod collector
