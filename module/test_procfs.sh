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

# Function to count dump callback calls from dmesg
count_dump_calls() {
    sudo dmesg | grep "dump callback called" | wc -l
}

# Function to verify dump call count increased by expected amount
verify_dump_calls() {
    local prev_count=$1
    local expected_new=$2
    local test_name=$3
    
    local current_count=$(count_dump_calls)
    local expected_count=$((prev_count + expected_new))
    
    if [ "$current_count" -eq "$expected_count" ]; then
        print_color $GREEN "✓ $test_name (dump calls: $current_count, expected: $expected_count)"
        return 0
    else
        print_color $RED "✗ $test_name (dump calls: $current_count, expected: $expected_count)"
        return 1
    fi
}

# Clean up function
cleanup() {
    # Unload test module if loaded
    if lsmod | grep -q "procfs_test_module"; then
        sudo rmmod procfs_test_module
    fi
    # Clear dmesg buffer
    sudo dmesg -c > /dev/null
}

# Run cleanup on script exit
trap cleanup EXIT

# Track test results
declare -i total_tests=0
declare -i passed_tests=0
declare -i failed_tests=0

# Function to record test result
record_test() {
    local success=$1
    let total_tests+=1
    if [ "$success" -eq 0 ]; then
        let passed_tests+=1
    else
        let failed_tests+=1
    fi
}

echo "Building test module..."
make procfs_test

echo "Loading test module..."
sudo dmesg -c > /dev/null  # Clear dmesg buffer
sudo insmod build/procfs_test_module.ko

echo "Verifying procfs entry exists..."
if [ ! -f "/proc/procfs_test" ]; then
    print_color $RED "Error: /proc/procfs_test not found"
    exit 1
fi

# Initial count should be 0
initial_count=$(count_dump_calls)
if [ "$initial_count" -ne 0 ]; then
    print_color $RED "Error: Initial dump call count is $initial_count, expected 0"
    exit 1
fi

echo "Testing single dump command..."
echo "dump" | sudo tee /proc/procfs_test > /dev/null
verify_dump_calls $initial_count 1 "single_dump"
record_test $?
prev_count=$(count_dump_calls)

echo "Testing dump with extra text..."
echo "dump with extra text" | sudo tee /proc/procfs_test > /dev/null
verify_dump_calls $prev_count 1 "dump_with_text"
record_test $?
prev_count=$(count_dump_calls)

echo "Testing multiple dump commands..."
echo -e "dump\ndump\ndump" | sudo tee /proc/procfs_test > /dev/null
verify_dump_calls $prev_count 3 "multiple_dumps"
record_test $?
prev_count=$(count_dump_calls)

echo "Testing invalid commands..."
echo "not a dump command" | sudo tee /proc/procfs_test > /dev/null
verify_dump_calls $prev_count 0 "invalid_command"
record_test $?
prev_count=$(count_dump_calls)

echo "Testing mixed valid/invalid commands..."
echo -e "dump\nnot a dump\ndump" | sudo tee /proc/procfs_test > /dev/null
verify_dump_calls $prev_count 2 "mixed_commands"
record_test $?

# Get test results from module
dmesg_output="/tmp/procfs_test_$$.txt"
sudo dmesg > "$dmesg_output"
test_results="/tmp/procfs_test_results_$$.txt"
grep "test_result:" "$dmesg_output" > "$test_results"

echo "Unloading test module..."
sudo rmmod procfs_test_module


echo -e "\nModule Test Results:"
echo "==================="

while IFS= read -r line; do
    test_name=$(echo "$line" | cut -d':' -f2)
    result=$(echo "$line" | cut -d':' -f3)
    
    if [ "$result" = "pass" ]; then
        print_color $GREEN "✓ $test_name"
        let passed_tests+=1
    else
        message=$(echo "$line" | cut -d':' -f4-)
        print_color $RED "✗ $test_name"
        [ ! -z "$message" ] && echo "  Error: $message"
        let failed_tests+=1
    fi
    let total_tests+=1
done < "$test_results"

# Clean up temporary files
rm -f "$dmesg_output" "$test_results"

echo -e "\nSummary:"
echo "========"
echo "Total tests: $total_tests"
print_color $GREEN "Passed: $passed_tests"
if [ $failed_tests -gt 0 ]; then
    print_color $RED "Failed: $failed_tests"
    echo -e "\nDmesg output:"
    echo "========"
    sudo dmesg
fi

# Exit with failure if any tests failed
[ $failed_tests -eq 0 ] || exit 1 