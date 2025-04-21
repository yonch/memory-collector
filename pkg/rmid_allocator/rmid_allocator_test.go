package rmid_allocator

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"testing"
	"time"

	"github.com/cilium/ebpf/rlimit"
)

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang -type rmid_init_input -type rmid_init_output -type rmid_alloc_input -type rmid_alloc_output -type rmid_free_input -type rmid_free_output -type rmid_is_allocated_input -type rmid_is_allocated_output RmidTest rmid_allocator.bpf.c rmid_allocator_test.bpf.c -- -I../headers

// Wrapper functions
func RmidInit(objs *RmidTestObjects, maxRmid uint32, minFreeTimeNs uint64) error {
	input := RmidTestRmidInitInput{
		MaxRmid:       maxRmid,
		MinFreeTimeNs: minFreeTimeNs,
	}

	buf := new(bytes.Buffer)
	if err := binary.Write(buf, binary.LittleEndian, input); err != nil {
		return fmt.Errorf("serializing input: %w", err)
	}

	ret, result, err := objs.RmidTestPrograms.TestRmidInit.Test(buf.Bytes())
	if err != nil {
		return fmt.Errorf("calling test function: %w", err)
	}
	if ret != 0 {
		return fmt.Errorf("test function returned non-zero: %d", ret)
	}

	var output RmidTestRmidInitOutput
	if err := binary.Read(bytes.NewReader(result), binary.LittleEndian, &output); err != nil {
		return fmt.Errorf("deserializing output: %w", err)
	}

	if output.Success == 0 {
		return fmt.Errorf("initialization failed")
	}

	return nil
}

func RmidAlloc(objs *RmidTestObjects, timestamp uint64) (uint32, error) {
	input := RmidTestRmidAllocInput{
		Timestamp: timestamp,
	}

	buf := new(bytes.Buffer)
	if err := binary.Write(buf, binary.LittleEndian, input); err != nil {
		return 0, fmt.Errorf("serializing input: %w", err)
	}

	ret, result, err := objs.RmidTestPrograms.TestRmidAlloc.Test(buf.Bytes())
	if err != nil {
		return 0, fmt.Errorf("calling test function: %w", err)
	}
	if ret != 0 {
		return 0, fmt.Errorf("test function returned non-zero: %d", ret)
	}

	var output RmidTestRmidAllocOutput
	if err := binary.Read(bytes.NewReader(result), binary.LittleEndian, &output); err != nil {
		return 0, fmt.Errorf("deserializing output: %w", err)
	}

	return output.Rmid, nil
}

func RmidFree(objs *RmidTestObjects, rmid uint32, timestamp uint64) error {
	input := RmidTestRmidFreeInput{
		Rmid:      rmid,
		Timestamp: timestamp,
	}

	buf := new(bytes.Buffer)
	if err := binary.Write(buf, binary.LittleEndian, input); err != nil {
		return fmt.Errorf("serializing input: %w", err)
	}

	ret, result, err := objs.RmidTestPrograms.TestRmidFree.Test(buf.Bytes())
	if err != nil {
		return fmt.Errorf("calling test function: %w", err)
	}
	if ret != 0 {
		return fmt.Errorf("test function returned non-zero: %d", ret)
	}

	var output RmidTestRmidFreeOutput
	if err := binary.Read(bytes.NewReader(result), binary.LittleEndian, &output); err != nil {
		return fmt.Errorf("deserializing output: %w", err)
	}

	if output.Success == 0 {
		return fmt.Errorf("free operation failed")
	}

	return nil
}

func RmidIsAllocated(objs *RmidTestObjects, rmid uint32) (bool, error) {
	input := RmidTestRmidIsAllocatedInput{
		Rmid: rmid,
	}

	buf := new(bytes.Buffer)
	if err := binary.Write(buf, binary.LittleEndian, input); err != nil {
		return false, fmt.Errorf("serializing input: %w", err)
	}

	ret, result, err := objs.RmidTestPrograms.TestRmidIsAllocated.Test(buf.Bytes())
	if err != nil {
		return false, fmt.Errorf("calling test function: %w", err)
	}
	if ret != 0 {
		return false, fmt.Errorf("test function returned non-zero: %d", ret)
	}

	var output RmidTestRmidIsAllocatedOutput
	if err := binary.Read(bytes.NewReader(result), binary.LittleEndian, &output); err != nil {
		return false, fmt.Errorf("deserializing output: %w", err)
	}

	return output.Allocated != 0, nil
}

// Test functions
func TestRmidAllocation(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}
	// Load the compiled program
	objs := RmidTestObjects{}
	if err := LoadRmidTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize allocator
	const maxRmid = 4
	const minFreeTimeNs = 2000000 // 2ms

	// Initialize the allocator
	if err := RmidInit(&objs, maxRmid, minFreeTimeNs); err != nil {
		t.Fatalf("Failed to initialize allocator: %v", err)
	}

	// Test allocation
	rmid, err := RmidAlloc(&objs, uint64(time.Now().UnixNano()))
	if err != nil {
		t.Fatalf("Failed to allocate RMID: %v", err)
	}

	if rmid != 1 {
		t.Errorf("Expected RMID 1, got %d", rmid)
	}

	// Verify RMID is allocated
	allocated, err := RmidIsAllocated(&objs, rmid)
	if err != nil {
		t.Fatalf("Failed to check RMID allocation: %v", err)
	}
	if !allocated {
		t.Error("RMID should be allocated")
	}
}

func TestRmidFreeAndReuse(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}
	// Load the compiled program
	objs := RmidTestObjects{}
	if err := LoadRmidTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize allocator
	const maxRmid = 4
	const minFreeTimeNs = 2000000 // 2ms

	// Initialize the allocator
	if err := RmidInit(&objs, maxRmid, minFreeTimeNs); err != nil {
		t.Fatalf("Failed to initialize allocator: %v", err)
	}

	// Allocate all RMIDs
	now := uint64(time.Now().UnixNano())
	for i := 1; i <= maxRmid; i++ {
		_, err := RmidAlloc(&objs, now)
		if err != nil {
			t.Fatalf("Failed to allocate RMID %d: %v", i, err)
		}
	}

	// Free the RMID 1
	if err := RmidFree(&objs, 1, now); err != nil {
		t.Fatalf("Failed to free RMID: %v", err)
	}

	// Try to allocate immediately - should fail
	rmid, err := RmidAlloc(&objs, now)
	if err != nil {
		t.Fatalf("Failed to call test function: %v", err)
	}
	if rmid != 0 {
		t.Error("Expected allocation to fail due to min free time")
	}

	// Try to allocate at the free time boundary - should fail
	rmid, err = RmidAlloc(&objs, now+minFreeTimeNs-1)
	if err != nil {
		t.Fatalf("Failed to call test function: %v", err)
	}
	if rmid != 0 {
		t.Error("Expected allocation to fail due to min free time")
	}

	// Wait past min free time and try again
	rmid, err = RmidAlloc(&objs, now+minFreeTimeNs)
	if err != nil {
		t.Fatalf("Failed to call test function: %v", err)
	}
	if rmid != 1 {
		t.Error("Expected allocation to succeed after min free time")
	}
}

func TestRmidExhaustion(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}
	// Load the compiled program
	objs := RmidTestObjects{}
	if err := LoadRmidTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize allocator
	const maxRmid = 4
	const minFreeTimeNs = 2000000 // 2ms

	// Initialize the allocator
	if err := RmidInit(&objs, maxRmid, minFreeTimeNs); err != nil {
		t.Fatalf("Failed to initialize allocator: %v", err)
	}

	// Allocate all RMIDs
	now := uint64(time.Now().UnixNano())
	for i := 1; i <= maxRmid; i++ {
		rmid, err := RmidAlloc(&objs, now)
		if err != nil {
			t.Fatalf("Failed to allocate RMID %d: %v", i, err)
		}
		if rmid != uint32(i) {
			t.Errorf("Expected RMID %d, got %d", i, rmid)
		}
	}

	// Try to allocate one more - should fail
	rmid, err := RmidAlloc(&objs, now)
	if err != nil {
		t.Fatalf("Failed to call test function: %v", err)
	}
	if rmid != 0 {
		t.Error("Expected allocation to fail when all RMIDs are in use")
	}
}

func TestInvalidRmid(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}
	// Load the compiled program
	objs := RmidTestObjects{}
	if err := LoadRmidTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize allocator
	const maxRmid = 4
	const minFreeTimeNs = 2000000 // 2ms

	// Initialize the allocator
	if err := RmidInit(&objs, maxRmid, minFreeTimeNs); err != nil {
		t.Fatalf("Failed to initialize allocator: %v", err)
	}

	// Test invalid RMID 0
	allocated, err := RmidIsAllocated(&objs, 0)
	if err != nil {
		t.Fatalf("Failed to check RMID allocation: %v", err)
	}
	if allocated {
		t.Error("Expected RMID 0 to be invalid")
	}

	// Test RMID beyond max
	allocated, err = RmidIsAllocated(&objs, maxRmid+1)
	if err != nil {
		t.Fatalf("Failed to check RMID allocation: %v", err)
	}
	if allocated {
		t.Error("Expected RMID beyond max to be invalid")
	}
}

func TestInvalidMaxRmid(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}
	// Load the compiled program
	objs := RmidTestObjects{}
	if err := LoadRmidTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	var maxRmid uint32
	err := objs.RmidTestVariables.MaxRmids.Get(&maxRmid)
	if err != nil {
		t.Fatalf("Failed to get max RMIDs: %v", err)
	}

	// Test with max_rmid = 0
	err = RmidInit(&objs, 0, 2000000)
	if err == nil {
		t.Error("Expected error when max_rmid is 0")
	}

	// Test with max_rmid > MAX_RMIDS
	err = RmidInit(&objs, maxRmid+1, 2000000)
	if err == nil {
		t.Error("Expected error when max_rmid exceeds MAX_RMIDS")
	}

	// Test with valid max_rmid
	err = RmidInit(&objs, maxRmid, 2000000)
	if err != nil {
		t.Errorf("Unexpected error with valid max_rmid: %v", err)
	}
}
