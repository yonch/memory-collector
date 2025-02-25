package aggregate

import (
	"fmt"
)

// Measurement represents a single measurement from a perf event
type Measurement struct {
	RMID         uint32
	Cycles       uint64
	Instructions uint64
	LLCMisses    uint64
	Timestamp    uint64 // nanoseconds
	Duration     uint64 // nanoseconds
}

// TimeSlotAggregation represents aggregated measurements for a specific RMID in a time slot
type TimeSlotAggregation struct {
	RMID         uint32
	Cycles       uint64
	Instructions uint64
	LLCMisses    uint64
	Duration     uint64 // nanoseconds
}

// TimeSlot represents all aggregations for a specific time window
type TimeSlot struct {
	StartTime    uint64                          // nanoseconds
	EndTime      uint64                          // nanoseconds
	Aggregations map[uint32]*TimeSlotAggregation // keyed by RMID
}

// Config holds the configuration parameters for the aggregator
type Config struct {
	SlotLength uint64 // nanoseconds
	WindowSize uint   // number of consecutive slots
	SlotOffset uint64 // nanoseconds, modulo SlotLength
}

// Aggregator manages the sliding window of time slots and measurement aggregation
type Aggregator struct {
	config    Config
	timeSlots []*TimeSlot
}

// NewAggregator creates a new Aggregator with the given configuration
func NewAggregator(config Config) (*Aggregator, error) {
	if config.SlotLength == 0 {
		return nil, fmt.Errorf("slot length must be greater than 0")
	}
	if config.WindowSize == 0 {
		return nil, fmt.Errorf("window size must be greater than 0")
	}
	if config.SlotOffset >= config.SlotLength {
		return nil, fmt.Errorf("slot offset must be less than slot length")
	}

	return &Aggregator{
		config:    config,
		timeSlots: make([]*TimeSlot, 0, config.WindowSize),
	}, nil
}

// getSlotStartTime returns the start time of the slot that would contain the given timestamp
func (a *Aggregator) getSlotStartTime(timestamp uint64) uint64 {
	// Adjust timestamp by offset
	adjusted := timestamp - a.config.SlotOffset
	// Find the start of the slot by integer division
	slotStart := (adjusted / a.config.SlotLength) * a.config.SlotLength
	// Add back the offset
	return slotStart + a.config.SlotOffset
}

// createTimeSlot creates a new time slot for the given start time
func (a *Aggregator) createTimeSlot(startTime uint64) *TimeSlot {
	return &TimeSlot{
		StartTime:    startTime,
		EndTime:      startTime + a.config.SlotLength,
		Aggregations: make(map[uint32]*TimeSlotAggregation),
	}
}

// AdvanceWindow advances the window to accommodate a new measurement time,
// returning any completed time slots that fall out of the window.
// Maintains the invariant that there are exactly WindowSize consecutive time slots after advancing.
func (a *Aggregator) AdvanceWindow(timestamp uint64, duration uint64) []*TimeSlot {

	var completedSlots []*TimeSlot = nil
	windowSize := a.config.WindowSize

	// Calculate the end time of the measurement
	measurementEndTime := timestamp - 1
	newEndSlotStart := a.getSlotStartTime(measurementEndTime)

	if len(a.timeSlots) > 0 {
		// Calculate how many slots need to be retired based on the time difference
		oldestCurrentStart := a.timeSlots[0].StartTime

		// if we only add timeslots, we will have slotsWithoutRetirement slots
		slotsWithoutRetirement := uint64((newEndSlotStart-oldestCurrentStart)/a.config.SlotLength) + 1

		// if that will be more than the window size, we need to retire some. be careful with underflow
		numExtraWithoutRetirement := slotsWithoutRetirement - uint64(windowSize)
		if numExtraWithoutRetirement > slotsWithoutRetirement {
			numExtraWithoutRetirement = 0
		}

		// but we only need to retire slots we actually have (we won't append)
		slotsToRetire := min(numExtraWithoutRetirement, uint64(len(a.timeSlots)))

		if slotsToRetire > 0 {
			remainingSlots := uint64(len(a.timeSlots)) - slotsToRetire
			completedSlots = make([]*TimeSlot, slotsToRetire)
			copy(completedSlots, a.timeSlots[:slotsToRetire])
			copy(a.timeSlots, a.timeSlots[slotsToRetire:])
			a.timeSlots = a.timeSlots[:remainingSlots]
		}
	}

	// now add new slots up to WindowSize which will have start time newEndSlotStart
	existingSlots := len(a.timeSlots)
	// expand the timeSlot slice to WindowSize
	a.timeSlots = a.timeSlots[:windowSize]
	for i := existingSlots; i < int(windowSize); i++ {
		a.timeSlots[i] = a.createTimeSlot(newEndSlotStart - uint64(int(windowSize)-1-i)*a.config.SlotLength)
	}

	return completedSlots
}

// safeSubtract performs timestamp subtraction that handles potential underflow
// Returns the signed difference between a and b
func safeSubtract(a, b uint64) int64 {
	// Convert to int64 and subtract to get signed result
	// This handles cases where b > a (negative difference) correctly
	return int64(a) - int64(b)
}

// UpdateMeasurement updates aggregations with a new measurement
func (a *Aggregator) UpdateMeasurement(m *Measurement) error {
	// Advance window first to ensure we have the correct slots
	a.AdvanceWindow(m.Timestamp, m.Duration)

	// Find the slots this measurement belongs to
	measurementEndTime := m.Timestamp
	remainingDuration := m.Duration
	remainingCycles := m.Cycles
	remainingInstructions := m.Instructions
	remainingLLCMisses := m.LLCMisses
	measurementStartTime := m.Timestamp - m.Duration

	for _, slot := range a.timeSlots {
		// Skip slots that end before the measurement starts
		// Use safe subtraction to handle potential underflow
		if safeSubtract(measurementStartTime, slot.EndTime) >= 0 {
			continue
		}

		// Calculate overlap duration using safe arithmetic
		var overlapStart, overlapEnd uint64

		// For overlapStart, take the later of measurement start and slot start
		if safeSubtract(measurementStartTime, slot.StartTime) >= 0 {
			overlapStart = measurementStartTime
		} else {
			overlapStart = slot.StartTime
		}

		// For overlapEnd, take the earlier of measurement end and slot end
		if safeSubtract(measurementEndTime, slot.EndTime) >= 0 {
			overlapEnd = slot.EndTime
		} else {
			overlapEnd = measurementEndTime
		}

		// Check if there's actually an overlap
		if safeSubtract(overlapEnd, overlapStart) <= 0 {
			continue
		}

		// Calculate overlap duration
		overlapDuration := overlapEnd - overlapStart

		// Calculate proportional values
		var cycles, instructions, llcMisses uint64
		if overlapDuration == remainingDuration {
			cycles = remainingCycles
			instructions = remainingInstructions
			llcMisses = remainingLLCMisses
		} else {
			proportion := float64(overlapDuration) / float64(remainingDuration)
			cycles = uint64(float64(remainingCycles) * proportion)
			instructions = uint64(float64(remainingInstructions) * proportion)
			llcMisses = uint64(float64(remainingLLCMisses) * proportion)
		}

		// Update or create aggregation for this RMID
		agg, exists := slot.Aggregations[m.RMID]
		if !exists {
			agg = &TimeSlotAggregation{
				RMID: m.RMID,
			}
			slot.Aggregations[m.RMID] = agg
		}

		// Update aggregation
		agg.Cycles += cycles
		agg.Instructions += instructions
		agg.LLCMisses += llcMisses
		agg.Duration += overlapDuration

		// Update remaining values
		remainingDuration -= overlapDuration
		remainingCycles -= cycles
		remainingInstructions -= instructions
		remainingLLCMisses -= llcMisses
		measurementStartTime = overlapEnd

		if remainingDuration == 0 {
			break
		}
	}

	return nil
}

// Reset returns all existing time slots and resets the aggregator
func (a *Aggregator) Reset() []*TimeSlot {
	slots := a.timeSlots
	a.timeSlots = make([]*TimeSlot, 0, a.config.WindowSize)
	return slots
}

// func max(a, b uint64) uint64 {
// 	if a > b {
// 		return a
// 	}
// 	return b
// }

// func min(a, b uint64) uint64 {
// 	if a < b {
// 		return b
// 	}
// 	return a
// }
