package backoff

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"math/rand"
	"testing"

	"github.com/cilium/ebpf/rlimit"
)

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang -type backoff_should_try_input -type backoff_should_try_output BackoffTest backoff_test.bpf.c -- -I.

const XDP_PASS = 2

// Wrapper functions
func BackoffInit(objs *BackoffTestObjects) error {
	ret, _, err := objs.BackoffTestPrograms.WrapBackoffInit.Test([]byte{})
	if err != nil {
		return fmt.Errorf("calling test function: %w", err)
	}
	if ret != XDP_PASS {
		return fmt.Errorf("test function returned non-zero: %d", ret)
	}
	return nil
}

func BackoffUpdateSuccess(objs *BackoffTestObjects) error {
	ret, _, err := objs.BackoffTestPrograms.WrapBackoffUpdateSuccess.Test([]byte{})
	if err != nil {
		return fmt.Errorf("calling test function: %w", err)
	}
	if ret != XDP_PASS {
		return fmt.Errorf("test function returned non-zero: %d", ret)
	}
	return nil
}

func BackoffUpdateFailure(objs *BackoffTestObjects) error {
	ret, _, err := objs.BackoffTestPrograms.WrapBackoffUpdateFailure.Test([]byte{})
	if err != nil {
		return fmt.Errorf("calling test function: %w", err)
	}
	if ret != XDP_PASS {
		return fmt.Errorf("test function returned non-zero: %d", ret)
	}
	return nil
}

func BackoffInBackoff(objs *BackoffTestObjects) (bool, error) {
	ret, result, err := objs.BackoffTestPrograms.WrapBackoffInBackoff.Test([]byte{1 /* must be non-empty */})
	if err != nil {
		return false, fmt.Errorf("calling test function: %w", err)
	}
	if ret != XDP_PASS {
		return false, fmt.Errorf("test function returned non-zero: %d", ret)
	}

	var inBackoff uint8
	if err := binary.Read(bytes.NewReader(result), binary.LittleEndian, &inBackoff); err != nil {
		return false, fmt.Errorf("deserializing output: %w", err)
	}

	return inBackoff != 0, nil
}

func BackoffShouldTry(objs *BackoffTestObjects, randomValue uint32) (bool, error) {
	input := BackoffTestBackoffShouldTryInput{
		RandomValue: randomValue,
	}

	buf := new(bytes.Buffer)
	if err := binary.Write(buf, binary.LittleEndian, input); err != nil {
		return false, fmt.Errorf("serializing input: %w", err)
	}

	ret, result, err := objs.BackoffTestPrograms.WrapBackoffShouldTry.Test(buf.Bytes())
	if err != nil {
		return false, fmt.Errorf("calling test function: %w", err)
	}
	if ret != XDP_PASS {
		return false, fmt.Errorf("test function returned non-zero: %d", ret)
	}

	var shouldTry uint8
	if err := binary.Read(bytes.NewReader(result), binary.LittleEndian, &shouldTry); err != nil {
		return false, fmt.Errorf("deserializing output: %w", err)
	}

	return shouldTry != 0, nil
}

// Test functions
func TestBackoffInit(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}

	objs := BackoffTestObjects{}
	if err := LoadBackoffTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	if err := BackoffInit(&objs); err != nil {
		t.Fatalf("Failed to initialize backoff: %v", err)
	}

	inBackoff, err := BackoffInBackoff(&objs)
	if err != nil {
		t.Fatalf("Failed to check backoff state: %v", err)
	}
	if inBackoff {
		t.Error("Expected not to be in backoff mode after initialization")
	}
}

func TestBackoffUpdateSuccess(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}

	objs := BackoffTestObjects{}
	if err := LoadBackoffTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize
	if err := BackoffInit(&objs); err != nil {
		t.Fatalf("Failed to initialize backoff: %v", err)
	}

	// Update failure to enter backoff mode
	if err := BackoffUpdateFailure(&objs); err != nil {
		t.Fatalf("Failed to update failure: %v", err)
	}

	// Update success to exit backoff mode
	if err := BackoffUpdateSuccess(&objs); err != nil {
		t.Fatalf("Failed to update success: %v", err)
	}

	inBackoff, err := BackoffInBackoff(&objs)
	if err != nil {
		t.Fatalf("Failed to check backoff state: %v", err)
	}
	if inBackoff {
		t.Error("Expected not to be in backoff mode after success")
	}
}

func TestBackoffUpdateFailure(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}

	objs := BackoffTestObjects{}
	if err := LoadBackoffTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize
	if err := BackoffInit(&objs); err != nil {
		t.Fatalf("Failed to initialize backoff: %v", err)
	}

	// Update failure to enter backoff mode
	if err := BackoffUpdateFailure(&objs); err != nil {
		t.Fatalf("Failed to update failure: %v", err)
	}

	inBackoff, err := BackoffInBackoff(&objs)
	if err != nil {
		t.Fatalf("Failed to check backoff state: %v", err)
	}
	if !inBackoff {
		t.Error("Expected to be in backoff mode after failure")
	}
}

func TestBackoffShouldTry(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}

	objs := BackoffTestObjects{}
	if err := LoadBackoffTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Initialize
	if err := BackoffInit(&objs); err != nil {
		t.Fatalf("Failed to initialize backoff: %v", err)
	}

	// Test without backoff
	for i := 0; i < 1000; i++ {
		shouldTry, err := BackoffShouldTry(&objs, 0)
		if err != nil {
			t.Fatalf("Failed to check should try: %v", err)
		}
		if !shouldTry {
			t.Error("Expected should try to be true without backoff")
		}
	}

	// Enter backoff mode
	if err := BackoffUpdateFailure(&objs); err != nil {
		t.Fatalf("Failed to update failure: %v", err)
	}

	// Test with backoff
	shouldTry, err := BackoffShouldTry(&objs, 0)
	if err != nil {
		t.Fatalf("Failed to check should try: %v", err)
	}
	if !shouldTry {
		t.Error("Expected should try to be true with random value 0")
	}

	shouldTry, err = BackoffShouldTry(&objs, 1)
	if err != nil {
		t.Fatalf("Failed to check should try: %v", err)
	}
	if shouldTry {
		t.Error("Expected should try to be false with random value 1")
	}
}

func TestBackoffProbability(t *testing.T) {
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("Failed to remove memlock limit: %v", err)
	}

	objs := BackoffTestObjects{}
	if err := LoadBackoffTestObjects(&objs, nil); err != nil {
		t.Fatalf("Failed to load objects: %v", err)
	}
	defer objs.Close()

	// Test probability distribution for each backoff level
	for level := 1; level <= 9; level++ {
		// Reset state
		if err := BackoffInit(&objs); err != nil {
			t.Fatalf("Failed to initialize backoff: %v", err)
		}

		// Enter backoff mode with specific level
		for i := 0; i < level; i++ {
			if err := BackoffUpdateFailure(&objs); err != nil {
				t.Fatalf("Failed to update failure: %v", err)
			}
		}

		// Test probability
		tries := 10000
		successes := 0
		expectedProbability := math.Max(1.0/float64(uint32(1)<<uint(level)), 1.0/128.0)
		r := rand.New(rand.NewSource(99))

		for i := 0; i < tries; i++ {
			shouldTry, err := BackoffShouldTry(&objs, uint32(r.Uint32()))
			if err != nil {
				t.Fatalf("Failed to check should try: %v", err)
			}
			if shouldTry {
				successes++
			}
		}

		actualProbability := float64(successes) / float64(tries)
		// Allow 20% error margin
		if actualProbability < expectedProbability*0.80 || actualProbability > expectedProbability*1.2 {
			t.Errorf("Level %d: Expected probability %.4f, got %.4f", level, expectedProbability, actualProbability)
		}
	}
}
