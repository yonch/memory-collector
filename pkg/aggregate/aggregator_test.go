package aggregate

import (
	"testing"
)

func TestNewAggregator(t *testing.T) {
	tests := []struct {
		name    string
		config  Config
		wantErr bool
	}{
		{
			name: "valid config",
			config: Config{
				SlotLength: 1_000_000, // 1ms
				WindowSize: 4,
				SlotOffset: 0,
			},
			wantErr: false,
		},
		{
			name: "zero slot length",
			config: Config{
				SlotLength: 0,
				WindowSize: 4,
				SlotOffset: 0,
			},
			wantErr: true,
		},
		{
			name: "zero window size",
			config: Config{
				SlotLength: 1_000_000,
				WindowSize: 0,
				SlotOffset: 0,
			},
			wantErr: true,
		},
		{
			name: "offset >= slot length",
			config: Config{
				SlotLength: 1_000_000,
				WindowSize: 4,
				SlotOffset: 1_000_000,
			},
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := NewAggregator(tt.config)
			if (err != nil) != tt.wantErr {
				t.Errorf("NewAggregator() error = %v, wantErr %v", err, tt.wantErr)
			}
		})
	}
}

func TestAggregator_UpdateMeasurement(t *testing.T) {
	config := Config{
		SlotLength: 1_000_000, // 1ms
		WindowSize: 4,
		SlotOffset: 0,
	}

	agg, err := NewAggregator(config)
	if err != nil {
		t.Fatalf("Failed to create aggregator: %v", err)
	}

	// Test single measurement within one slot
	m1 := &Measurement{
		RMID:         1,
		Cycles:       1000,
		Instructions: 2000,
		LLCMisses:    100,
		Timestamp:    1_500_000, // End time at 1.5ms
		Duration:     500_000,   // 0.5ms duration, so started at 1.0ms
	}

	if err := agg.UpdateMeasurement(m1); err != nil {
		t.Errorf("UpdateMeasurement() error = %v", err)
	}

	if len(agg.timeSlots) != 4 {
		t.Errorf("Expected 4 slots after first measurement, got %d", len(agg.timeSlots))
	}

	slot := agg.timeSlots[3]
	agg1, exists := slot.Aggregations[m1.RMID]
	if !exists {
		t.Errorf("Expected aggregation for RMID %d in slot starting at %d", m1.RMID, slot.StartTime)
	}

	if agg1.Cycles != m1.Cycles {
		t.Errorf("Expected cycles %d, got %d", m1.Cycles, agg1.Cycles)
	}

	// Test measurement spanning two slots
	m2 := &Measurement{
		RMID:         1,
		Cycles:       2000,
		Instructions: 4000,
		LLCMisses:    200,
		Timestamp:    2_500_000, // End time at 2.5ms
		Duration:     1_000_000, // 1ms duration, so started at 1.5ms
	}

	if err := agg.UpdateMeasurement(m2); err != nil {
		t.Errorf("UpdateMeasurement() error = %v", err)
	}

	if len(agg.timeSlots) != 4 {
		t.Errorf("Expected 4 slots after second measurement, got %d", len(agg.timeSlots))
	}

	// Verify proportional distribution
	firstSlot := agg.timeSlots[2]
	secondSlot := agg.timeSlots[3]

	agg1 = firstSlot.Aggregations[m2.RMID]
	agg2 := secondSlot.Aggregations[m2.RMID]

	// First slot should have 0.5ms worth of measurements
	expectedFirstSlotProportion := float64(500_000) / float64(m2.Duration)
	expectedFirstSlotCycles := uint64(float64(m2.Cycles) * expectedFirstSlotProportion)

	if agg1.Cycles != m1.Cycles+expectedFirstSlotCycles {
		t.Errorf("Expected first slot cycles %d, got %d", m1.Cycles+expectedFirstSlotCycles, agg1.Cycles)
	}

	// Second slot should have 0.5ms worth of measurements (2.0ms-2.5ms)
	expectedSecondSlotProportion := float64(500_000) / float64(m2.Duration)
	expectedSecondSlotCycles := uint64(float64(m2.Cycles) * expectedSecondSlotProportion)

	if agg2.Cycles != expectedSecondSlotCycles {
		t.Errorf("Expected second slot cycles %d, got %d", expectedSecondSlotCycles, agg2.Cycles)
	}
}

func TestAggregator_AdvanceWindow(t *testing.T) {
	tests := []struct {
		name               string
		config             Config
		initialSlots       []uint64 // start times for initial slots
		timestamp          uint64   // end time of measurement
		duration           uint64
		wantCompletedCount int
		wantSlotCount      int
		wantStartTimes     []uint64 // expected start times after advance
	}{
		{
			name: "initial creation of window",
			config: Config{
				SlotLength: 1_000_000, // 1ms
				WindowSize: 4,
				SlotOffset: 0,
			},
			initialSlots:       nil,
			timestamp:          1_500_000, // Measurement ends at 1.5ms
			duration:           500_000,   // Started at 1.0ms
			wantCompletedCount: 0,
			wantSlotCount:      4,
			wantStartTimes: []uint64{
				^uint64(0) - 2_000_000 + 1, // consecutive slots
				^uint64(0) - 1_000_000 + 1,
				0,
				1_000_000, // slot containing measurement
			},
		},
		{
			name: "no advancement needed",
			config: Config{
				SlotLength: 1_000_000,
				WindowSize: 4,
				SlotOffset: 0,
			},
			initialSlots: []uint64{
				0,
				1_000_000,
				2_000_000,
				3_000_000,
			},
			timestamp:          1_500_000, // Ends at 1.5ms
			duration:           500_000,   // Started at 1.0ms
			wantCompletedCount: 0,
			wantSlotCount:      4,
			wantStartTimes: []uint64{
				0,
				1_000_000,
				2_000_000,
				3_000_000,
			},
		},
		{
			name: "advance one slot",
			config: Config{
				SlotLength: 1_000_000,
				WindowSize: 4,
				SlotOffset: 0,
			},
			initialSlots: []uint64{
				0,
				1_000_000,
				2_000_000,
				3_000_000,
			},
			timestamp:          4_500_000, // Ends at 4.5ms
			duration:           500_000,   // Started at 4.0ms
			wantCompletedCount: 1,
			wantSlotCount:      4,
			wantStartTimes: []uint64{
				1_000_000,
				2_000_000,
				3_000_000,
				4_000_000,
			},
		},
		{
			name: "advance multiple slots",
			config: Config{
				SlotLength: 1_000_000,
				WindowSize: 4,
				SlotOffset: 0,
			},
			initialSlots: []uint64{
				0,
				1_000_000,
				2_000_000,
				3_000_000,
			},
			timestamp:          6_500_000, // Ends at 6.5ms
			duration:           500_000,   // Started at 6.0ms
			wantCompletedCount: 3,
			wantSlotCount:      4,
			wantStartTimes: []uint64{
				3_000_000,
				4_000_000,
				5_000_000,
				6_000_000,
			},
		},
		{
			name: "advance with offset",
			config: Config{
				SlotLength: 1_000_000,
				WindowSize: 3,
				SlotOffset: 100_000,
			},
			initialSlots: []uint64{
				100_000,
				1_100_000,
				2_100_000,
			},
			timestamp:          3_600_000, // Ends at 3.6ms
			duration:           500_000,   // Started at 3.1ms
			wantCompletedCount: 1,
			wantSlotCount:      3,
			wantStartTimes: []uint64{
				1_100_000,
				2_100_000,
				3_100_000,
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			agg, err := NewAggregator(tt.config)
			if err != nil {
				t.Fatalf("NewAggregator() error = %v", err)
			}

			// Create initial slots if specified
			if tt.initialSlots != nil {
				for _, startTime := range tt.initialSlots {
					agg.timeSlots = append(agg.timeSlots, agg.createTimeSlot(startTime))
				}
			}

			// Advance window
			completed := agg.AdvanceWindow(tt.timestamp, tt.duration)

			// Check completed slots count
			if len(completed) != tt.wantCompletedCount {
				t.Errorf("AdvanceWindow() completed slots = %v, want %v", len(completed), tt.wantCompletedCount)
			}

			// Check resulting slot count
			if len(agg.timeSlots) != tt.wantSlotCount {
				t.Errorf("After AdvanceWindow() slot count = %v, want %v", len(agg.timeSlots), tt.wantSlotCount)
			}

			// Check slot start times
			for i, want := range tt.wantStartTimes {
				if i >= len(agg.timeSlots) {
					t.Errorf("Missing expected slot at index %d", i)
					continue
				}
				if got := agg.timeSlots[i].StartTime; got != want {
					t.Errorf("Slot[%d].StartTime = %v, want %v", i, got, want)
				}
			}

			// Verify slots are consecutive
			for i := 1; i < len(agg.timeSlots); i++ {
				if agg.timeSlots[i].StartTime != agg.timeSlots[i-1].EndTime {
					t.Errorf("Slots not consecutive at index %d: %v != %v",
						i, agg.timeSlots[i].StartTime, agg.timeSlots[i-1].EndTime)
				}
			}
		})
	}
}

func TestAggregator_SlotAlignment(t *testing.T) {
	config := Config{
		SlotLength: 1_000_000, // 1ms
		WindowSize: 4,
		SlotOffset: 100_000, // 0.1ms offset
	}

	agg, err := NewAggregator(config)
	if err != nil {
		t.Fatalf("Failed to create aggregator: %v", err)
	}

	// Add measurement that should align to offset boundary
	m1 := &Measurement{
		RMID:         1,
		Cycles:       1000,
		Instructions: 2000,
		LLCMisses:    100,
		Timestamp:    3_550_000, // End time at 1.55ms
		Duration:     500_000,   // 0.5ms duration, so started at 1.05ms
	}

	if err := agg.UpdateMeasurement(m1); err != nil {
		t.Errorf("UpdateMeasurement() error = %v", err)
	}

	if len(agg.timeSlots) != 4 {
		t.Fatalf("Expected 4 slots, got %d", len(agg.timeSlots))
	}

	// Verify slot alignment
	var expectedStart uint64 = 100_000 // Should align to 0ms + 0.1ms boundary
	if agg.timeSlots[0].StartTime != expectedStart {
		t.Errorf("Expected slot start time %d, got %d", expectedStart, agg.timeSlots[0].StartTime)
	}
}

func TestAggregator_Reset(t *testing.T) {
	config := Config{
		SlotLength: 1_000_000, // 1ms
		WindowSize: 4,
		SlotOffset: 0,
	}

	agg, err := NewAggregator(config)
	if err != nil {
		t.Fatalf("Failed to create aggregator: %v", err)
	}

	// Add a measurement that ends at 1.5ms, started at 1.0ms
	m1 := &Measurement{
		RMID:         1,
		Cycles:       1000,
		Instructions: 2000,
		LLCMisses:    100,
		Timestamp:    1_500_000, // End time at 1.5ms
		Duration:     500_000,   // 0.5ms duration, so started at 1.0ms
	}

	if err := agg.UpdateMeasurement(m1); err != nil {
		t.Errorf("UpdateMeasurement() error = %v", err)
	}

	// Reset and verify
	slots := agg.Reset()
	if len(slots) != 4 {
		t.Errorf("Expected 4 slots returned from Reset(), got %d", len(slots))
	}

	if len(agg.timeSlots) != 0 {
		t.Errorf("Expected 0 slots after Reset(), got %d", len(agg.timeSlots))
	}

	// Verify the returned slot contains the measurement
	// The measurement should be in the slot containing 1.0ms-2.0ms
	var found bool
	for _, slot := range slots {
		if slot.StartTime == 1_000_000 {
			agg1, exists := slot.Aggregations[m1.RMID]
			if !exists {
				t.Errorf("Expected aggregation for RMID %d in slot starting at %d", m1.RMID, slot.StartTime)
			} else if agg1.Cycles != m1.Cycles {
				t.Errorf("Expected cycles %d in returned slot, got %d", m1.Cycles, agg1.Cycles)
			}
			found = true
			break
		}
	}
	if !found {
		t.Error("Did not find slot containing measurement")
	}
}

func TestAggregator_UpdateMeasurement_TimestampWraparound(t *testing.T) {
	config := Config{
		SlotLength: 1_000_000, // 1ms
		WindowSize: 4,
		SlotOffset: 0,
	}

	agg, err := NewAggregator(config)
	if err != nil {
		t.Fatalf("Failed to create aggregator: %v", err)
	}

	// Create a measurement with a very large timestamp
	// Measurement ends at max-2ms, started at max-3ms
	m1 := &Measurement{
		RMID:         1,
		Cycles:       1000,
		Instructions: 2000,
		LLCMisses:    100,
		Timestamp:    ^uint64(0) - 2_000_000, // End time at max-2ms
		Duration:     1_000_000,              // 1ms duration, so started at max-3ms
	}

	if err := agg.UpdateMeasurement(m1); err != nil {
		t.Errorf("UpdateMeasurement() error = %v", err)
	}

	// Verify we have exactly WindowSize slots
	if len(agg.timeSlots) != 4 {
		t.Errorf("Expected 4 slots, got %d", len(agg.timeSlots))
	}

	// Verify the measurement was properly attributed
	var totalDuration uint64
	var totalCycles uint64
	for _, slot := range agg.timeSlots {
		if agg1, exists := slot.Aggregations[m1.RMID]; exists {
			totalDuration += agg1.Duration
			totalCycles += agg1.Cycles
		}
	}

	if totalDuration != m1.Duration {
		t.Errorf("Total duration mismatch: got %d, want %d", totalDuration, m1.Duration)
	}
	if totalCycles != m1.Cycles {
		t.Errorf("Total cycles mismatch: got %d, want %d", totalCycles, m1.Cycles)
	}

	// Test a measurement that spans the uint64 wraparound point
	m2 := &Measurement{
		RMID:         2,
		Cycles:       2000,
		Instructions: 4000,
		LLCMisses:    200,
		Timestamp:    500_000,   // End time at 0.5ms
		Duration:     1_000_000, // 1ms duration, so started at max-0.5ms
	}

	if err := agg.UpdateMeasurement(m2); err != nil {
		t.Errorf("UpdateMeasurement() error = %v", err)
	}

	// Verify the measurement was properly split across slots
	totalDuration = 0
	totalCycles = 0
	for _, slot := range agg.timeSlots {
		if agg2, exists := slot.Aggregations[m2.RMID]; exists {
			totalDuration += agg2.Duration
			totalCycles += agg2.Cycles
		}
	}

	if totalDuration != m2.Duration {
		t.Errorf("Total duration mismatch for wraparound measurement: got %d, want %d", totalDuration, m2.Duration)
	}
	if totalCycles != m2.Cycles {
		t.Errorf("Total cycles mismatch for wraparound measurement: got %d, want %d", totalCycles, m2.Cycles)
	}
}

func TestSafeSubtract(t *testing.T) {
	tests := []struct {
		name     string
		a        uint64
		b        uint64
		expected int64
	}{
		{
			name:     "normal subtraction",
			a:        1000,
			b:        500,
			expected: 500,
		},
		{
			name:     "negative result",
			a:        500,
			b:        1000,
			expected: -500,
		},
		{
			name:     "large timestamps",
			a:        ^uint64(0) - 500,
			b:        ^uint64(0) - 1000,
			expected: 500,
		},
		{
			name:     "wraparound case",
			a:        0,
			b:        ^uint64(0),
			expected: 1,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			result := safeSubtract(tt.a, tt.b)
			if result != tt.expected {
				t.Errorf("safeSubtract(%d, %d) = %d, want %d", tt.a, tt.b, result, tt.expected)
			}
		})
	}
}
