#!/bin/bash
set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Function to print colored output
print_color() {
    color=$1
    shift
    echo -e "${color}$@${NC}"
}

# Clean up function
cleanup() {
    # Unload test module if loaded
    if lsmod | grep -q "rmid_allocator_test_module"; then
        sudo rmmod rmid_allocator_test_module
    fi
    # Clear dmesg buffer
    sudo dmesg -c > /dev/null
}

# Run cleanup on script exit
trap cleanup EXIT

echo "Building test module..."
make clean
make rmid_allocator_test

echo "Loading test module..."
sudo dmesg -c > /dev/null  # Clear dmesg buffer
sudo insmod build/rmid_allocator_test_module.ko

echo "Collecting test results..."
dmesg_output=$(sudo dmesg)

echo "Unloading test module..."
sudo rmmod rmid_allocator_test_module || true

# Parse test results
total_tests=0
passed_tests=0
failed_tests=0

echo -e "\nTest Results:"
echo "============="

while IFS= read -r line; do
    if [[ $line =~ "test_result:"([^:]+):([^:]+)(:(.*))? ]]; then
        test_name="${BASH_REMATCH[1]}"
        result="${BASH_REMATCH[2]}"
        message="${BASH_REMATCH[4]}"
        
        ((total_tests++))
        
        if [ "$result" == "pass" ]; then
            ((passed_tests++))
            print_color $GREEN "✓ $test_name"
        else
            ((failed_tests++))
            print_color $RED "✗ $test_name"
            [ ! -z "$message" ] && echo "  Error: $message"
        fi
    fi
done <<< "$dmesg_output"

echo -e "\nSummary:"
echo "========"
echo "Total tests: $total_tests"
print_color $GREEN "Passed: $passed_tests"
print_color $RED "Failed: $failed_tests"

# Exit with failure if any tests failed
[ $failed_tests -eq 0 ] || exit 1 