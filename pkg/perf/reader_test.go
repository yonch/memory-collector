package perf

import (
	"encoding/binary"
	"fmt"
	"slices"
	"testing"
)

func TestReader(t *testing.T) {
	reader := NewReader()

	// Create test rings
	pageSize := uint64(4096)
	nPages := uint32(2)
	data1 := make([]byte, pageSize*(1+uint64(nPages)))
	data2 := make([]byte, pageSize*(1+uint64(nPages)))

	ring1, err := InitContiguous(data1, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to create ring1: %v", err)
	}

	ring2, err := InitContiguous(data2, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to create ring2: %v", err)
	}

	// Add rings to reader
	if err := reader.AddRing(ring1); err != nil {
		t.Fatalf("failed to add ring1: %v", err)
	}
	if err := reader.AddRing(ring2); err != nil {
		t.Fatalf("failed to add ring2: %v", err)
	}

	// Test that adding a ring while active fails
	if err := reader.Start(); err != nil {
		t.Fatalf("failed to start reader: %v", err)
	}
	if err := reader.AddRing(ring1); err != ErrAlreadyActive {
		t.Errorf("expected ErrAlreadyActive, got %v", err)
	}
	reader.Finish()

	// Test operations before Start should fail
	if !reader.Empty() {
		t.Error("expected reader to be empty when not active")
	}
	if _, err := reader.PeekTimestamp(); err != ErrNotActive {
		t.Errorf("expected ErrNotActive, got %v", err)
	}
	if _, err := reader.CurrentRing(); err != ErrNotActive {
		t.Errorf("expected ErrNotActive, got %v", err)
	}
	if err := reader.Pop(); err != ErrNotActive {
		t.Errorf("expected ErrNotActive, got %v", err)
	}

	// Start the reader
	if err := reader.Start(); err != nil {
		t.Fatalf("failed to start reader: %v", err)
	}

	// Initially should be empty
	if !reader.Empty() {
		t.Error("expected reader to be empty")
	}

	reader.Finish()

	// Create events with timestamps
	event1 := make([]byte, 16)                     // 8 bytes for timestamp + "event1"
	binary.LittleEndian.PutUint64(event1[:8], 100) // timestamp 100
	copy(event1[8:], []byte("event1"))
	// print the hex of event1
	fmt.Printf("event1: %x\n", event1)

	event2 := make([]byte, 16)                     // 8 bytes for timestamp + "event2"
	binary.LittleEndian.PutUint64(event2[:8], 200) // timestamp 200
	copy(event2[8:], []byte("event2"))

	ring1.StartWriteBatch()
	if _, err := ring1.Write(event1, PERF_RECORD_SAMPLE); err != nil {
		t.Fatalf("failed to write event1: %v", err)
	}
	ring1.FinishWriteBatch()

	ring2.StartWriteBatch()
	if _, err := ring2.Write(event2, PERF_RECORD_SAMPLE); err != nil {
		t.Fatalf("failed to write event2: %v", err)
	}
	ring2.FinishWriteBatch()

	// Start a new batch to see the new events
	if err := reader.Start(); err != nil {
		t.Fatalf("failed to restart reader: %v", err)
	}

	// Test reading events
	if reader.Empty() {
		t.Error("expected reader to not be empty")
	}

	// Pop events and verify they come in timestamp order
	expectedTimestamps := []uint64{100, 200}
	expectedRingData := [][]byte{event1, event2}
	for i, expected := range expectedTimestamps {
		ts, err := reader.PeekTimestamp()
		if err != nil {
			t.Errorf("failed to peek timestamp %d: %v", i, err)
		}
		if ts != expected {
			t.Errorf("expected timestamp %d, got %d", expected, ts)
		}

		// Get current ring and verify it's not nil
		ring, err := reader.CurrentRing()
		if err != nil {
			t.Errorf("failed to get current ring: %v", err)
		}
		if ring == nil {
			t.Error("expected non-nil current ring")
		}

		// Copy the ring's data into a new buffer
		size, err := ring.PeekSize()
		if err != nil {
			t.Errorf("failed to peek size: %v", err)
		}
		ringData := make([]byte, size)
		if err := ring.PeekCopy(ringData, 0); err != nil {
			t.Errorf("failed to peek copy ring data: %v", err)
		}
		fmt.Printf("ring data: %x\n", ringData)

		if !slices.Equal(ringData, expectedRingData[i]) {
			t.Errorf("expected ring data %x, got %x", expectedRingData[i], ringData)
		}

		if err := reader.Pop(); err != nil {
			t.Errorf("failed to pop event %d: %v", i, err)
		}
	}

	// Should be empty after reading all events
	if !reader.Empty() {
		t.Error("expected reader to be empty after reading all events")
	}

	// Finish the reader
	if err := reader.Finish(); err != nil {
		t.Errorf("failed to finish reader: %v", err)
	}

	// Test operations after Finish should fail
	if !reader.Empty() {
		t.Error("expected reader to be empty when not active")
	}
	if _, err := reader.PeekTimestamp(); err != ErrNotActive {
		t.Errorf("expected ErrNotActive, got %v", err)
	}
	if _, err := reader.CurrentRing(); err != ErrNotActive {
		t.Errorf("expected ErrNotActive, got %v", err)
	}
	if err := reader.Pop(); err != ErrNotActive {
		t.Errorf("expected ErrNotActive, got %v", err)
	}
}

func TestReaderLostRecords(t *testing.T) {
	reader := NewReader()

	// Create two test rings
	pageSize := uint64(4096)
	nPages := uint32(2)
	data1 := make([]byte, pageSize*(1+uint64(nPages)))
	data2 := make([]byte, pageSize*(1+uint64(nPages)))

	ring1, err := InitContiguous(data1, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to create ring1: %v", err)
	}

	ring2, err := InitContiguous(data2, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to create ring2: %v", err)
	}

	if err := reader.AddRing(ring1); err != nil {
		t.Fatalf("failed to add ring1: %v", err)
	}
	if err := reader.AddRing(ring2); err != nil {
		t.Fatalf("failed to add ring2: %v", err)
	}

	// Test 1: Show that events within a single ring maintain their order regardless of type
	event1 := make([]byte, 16)
	binary.LittleEndian.PutUint64(event1[:8], 100)
	copy(event1[8:], []byte("event1"))

	event2 := make([]byte, 16) // Lost event data
	copy(event2[8:], []byte("lost!"))

	// Write both events to ring1
	ring1.StartWriteBatch()
	if _, err := ring1.Write(event1, PERF_RECORD_SAMPLE); err != nil {
		t.Fatalf("failed to write event1: %v", err)
	}
	if _, err := ring1.Write(event2, PERF_RECORD_LOST); err != nil {
		t.Fatalf("failed to write event2: %v", err)
	}
	ring1.FinishWriteBatch()

	// Start reader and verify events come in ring order (not by type)
	if err := reader.Start(); err != nil {
		t.Fatalf("failed to start reader: %v", err)
	}

	// First event should be event1 (timestamp 100)
	ts, err := reader.PeekTimestamp()
	if err != nil {
		t.Errorf("failed to peek timestamp: %v", err)
	}
	if ts != 100 {
		t.Errorf("expected timestamp 100, got %d", ts)
	}

	ring, err := reader.CurrentRing()
	if err != nil {
		t.Errorf("failed to get current ring: %v", err)
	}
	if typ := ring.PeekType(); typ != PERF_RECORD_SAMPLE {
		t.Errorf("expected PERF_RECORD_SAMPLE, got %d", typ)
	}
	if err := reader.Pop(); err != nil {
		t.Errorf("failed to pop event: %v", err)
	}

	// Second event should be lost event (timestamp 0)
	ts, err = reader.PeekTimestamp()
	if err != nil {
		t.Errorf("failed to peek timestamp: %v", err)
	}
	if ts != 0 {
		t.Errorf("expected timestamp 0 for lost event, got %d", ts)
	}

	ring, err = reader.CurrentRing()
	if err != nil {
		t.Errorf("failed to get current ring: %v", err)
	}
	if typ := ring.PeekType(); typ != PERF_RECORD_LOST {
		t.Errorf("expected PERF_RECORD_LOST, got %d", typ)
	}
	if err := reader.Pop(); err != nil {
		t.Errorf("failed to pop event: %v", err)
	}

	reader.Finish()

	// Test 2: Show that lost events from one ring are processed before normal events from another ring
	// Ring1: Normal event with timestamp 100
	// Ring2: Lost event (should get timestamp 0)
	normalEvent := make([]byte, 16)
	binary.LittleEndian.PutUint64(normalEvent[:8], 100)
	copy(normalEvent[8:], []byte("normal"))

	lostEvent := make([]byte, 16)
	copy(lostEvent[8:], []byte("lost!"))

	ring1.StartWriteBatch()
	if _, err := ring1.Write(normalEvent, PERF_RECORD_SAMPLE); err != nil {
		t.Fatalf("failed to write normal event: %v", err)
	}
	ring1.FinishWriteBatch()

	ring2.StartWriteBatch()
	if _, err := ring2.Write(lostEvent, PERF_RECORD_LOST); err != nil {
		t.Fatalf("failed to write lost event: %v", err)
	}
	ring2.FinishWriteBatch()

	// Start reader and verify lost event comes first
	if err := reader.Start(); err != nil {
		t.Fatalf("failed to start reader: %v", err)
	}

	// First event should be lost event (timestamp 0)
	ts, err = reader.PeekTimestamp()
	if err != nil {
		t.Errorf("failed to peek timestamp: %v", err)
	}
	if ts != 0 {
		t.Errorf("expected timestamp 0 for lost event, got %d", ts)
	}

	ring, err = reader.CurrentRing()
	if err != nil {
		t.Errorf("failed to get current ring: %v", err)
	}
	if ring != ring2 {
		t.Error("expected lost event from ring2")
	}
	if typ := ring.PeekType(); typ != PERF_RECORD_LOST {
		t.Errorf("expected PERF_RECORD_LOST, got %d", typ)
	}
	if err := reader.Pop(); err != nil {
		t.Errorf("failed to pop lost event: %v", err)
	}

	// Second event should be normal event (timestamp 100)
	ts, err = reader.PeekTimestamp()
	if err != nil {
		t.Errorf("failed to peek timestamp: %v", err)
	}
	if ts != 100 {
		t.Errorf("expected timestamp 100 for normal event, got %d", ts)
	}

	ring, err = reader.CurrentRing()
	if err != nil {
		t.Errorf("failed to get current ring: %v", err)
	}
	if ring != ring1 {
		t.Error("expected normal event from ring1")
	}
	if typ := ring.PeekType(); typ != PERF_RECORD_SAMPLE {
		t.Errorf("expected PERF_RECORD_SAMPLE, got %d", typ)
	}
	if err := reader.Pop(); err != nil {
		t.Errorf("failed to pop normal event: %v", err)
	}

	// Should be empty after reading all events
	if !reader.Empty() {
		t.Error("expected reader to be empty after reading all events")
	}

	reader.Finish()
}
