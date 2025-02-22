package rmid

import (
	"testing"
)

func TestTracker_Basic(t *testing.T) {
	tracker := NewTracker()

	// Test allocation
	tracker.Alloc(1, "test1", 100, 1000)
	tracker.Advance(1500)

	meta, exists := tracker.GetMetadata(1)
	if !exists {
		t.Error("Expected RMID 1 to exist")
	}
	if !meta.Valid {
		t.Error("Expected RMID 1 to be valid")
	}
	if meta.Comm != "test1" {
		t.Errorf("Expected comm 'test1', got '%s'", meta.Comm)
	}
	if meta.Tgid != 100 {
		t.Errorf("Expected tgid 100, got %d", meta.Tgid)
	}

	// Test free
	tracker.Free(1, 2000)
	tracker.Advance(2500)

	meta, exists = tracker.GetMetadata(1)
	if !exists {
		t.Error("Expected RMID 1 to still exist after free")
	}
	if meta.Valid {
		t.Error("Expected RMID 1 to be invalid after free")
	}
}

func TestTracker_FutureEvents(t *testing.T) {
	tracker := NewTracker()

	// Add events
	tracker.Alloc(1, "test1", 100, 1000)
	tracker.Free(1, 2000)
	tracker.Alloc(1, "override1", 200, 3000) // Future event
	tracker.Alloc(2, "test2", 300, 4000)     // Future event

	// Try to advance to timestamp before future event
	tracker.Advance(2500)

	// Check that RMID 1 was processed
	meta, exists := tracker.GetMetadata(1)
	if !exists {
		t.Error("Expected RMID 1 to exist")
	}
	if meta.Valid {
		t.Error("Expected RMID 1 to be invalid")
	}
	if meta.Comm != "test1" {
		t.Errorf("Expected comm 'test1', got '%s'", meta.Comm)
	}
	if meta.Tgid != 100 {
		t.Errorf("Expected tgid 100, got %d", meta.Tgid)
	}

	// Check that RMID 2 was not processed
	_, exists = tracker.GetMetadata(2)
	if exists {
		t.Error("Expected RMID 2 to not exist yet")
	}
}

func TestTracker_Reset(t *testing.T) {
	tracker := NewTracker()

	// Add some events and process them
	tracker.Alloc(1, "test1", 100, 1000)
	tracker.Free(1, 2000)
	tracker.Advance(2500)

	// Reset the tracker
	tracker.Reset()

	// Verify state is cleared
	if len(tracker.rmids) != 0 {
		t.Error("Expected empty rmids after reset")
	}
	if len(tracker.updates) != 0 {
		t.Error("Expected empty updates after reset")
	}

	// Verify we can add new events after reset
	tracker.Alloc(2, "test2", 200, 3000)
	tracker.Advance(3500)

	meta, exists := tracker.GetMetadata(2)
	if !exists {
		t.Error("Expected RMID 2 to exist after reset and new allocation")
	}
	if !meta.Valid {
		t.Error("Expected RMID 2 to be valid")
	}
}

func TestTracker_Reallocation(t *testing.T) {
	tracker := NewTracker()

	// Initial allocation
	tracker.Alloc(1, "test1", 100, 1000)
	tracker.Advance(1500)

	// Free
	tracker.Free(1, 2000)
	tracker.Advance(2500)

	// Reallocate same RMID
	tracker.Alloc(1, "test2", 200, 3000)
	tracker.Advance(3500)

	// Check final state
	meta, exists := tracker.GetMetadata(1)
	if !exists {
		t.Error("Expected RMID 1 to exist")
	}
	if !meta.Valid {
		t.Error("Expected RMID 1 to be valid after reallocation")
	}
	if meta.Comm != "test2" {
		t.Errorf("Expected comm 'test2', got '%s'", meta.Comm)
	}
	if meta.Tgid != 200 {
		t.Errorf("Expected tgid 200, got %d", meta.Tgid)
	}
}
