package perf

import (
	"container/heap"
	"errors"
	"unsafe"
)

var (
	// ErrNoRings is returned when trying to read from a container with no rings
	ErrNoRings = errors.New("no rings available")
	// ErrNotActive is returned when trying to use a reader outside of a batch
	ErrNotActive = errors.New("reader is not active")
	// ErrAlreadyActive is returned when trying to modify a reader while it's active
	ErrAlreadyActive = errors.New("reader is already active")
)

const (
	// PERF_RECORD_SAMPLE is the type for sample records
	PERF_RECORD_SAMPLE = 9
	// PERF_RECORD_LOST is the type for lost sample records
	PERF_RECORD_LOST = 2
)

// perfEntry represents a timestamped entry from a specific ring
type perfEntry struct {
	timestamp uint64 // Event timestamp
	ringIndex int    // Index of the source ring
}

// perfEntryHeap implements heap.Interface for perfEntry
type perfEntryHeap struct {
	entries []perfEntry
	size    int // Number of valid entries in the heap
}

func (h *perfEntryHeap) Len() int { return h.size }
func (h *perfEntryHeap) Less(i, j int) bool {
	return h.entries[i].timestamp < h.entries[j].timestamp
}
func (h *perfEntryHeap) Swap(i, j int) {
	h.entries[i], h.entries[j] = h.entries[j], h.entries[i]
}
func (h *perfEntryHeap) Push(x interface{}) {
	h.entries[h.size] = x.(perfEntry)
	h.size++
}
func (h *perfEntryHeap) Pop() interface{} {
	h.size--
	return h.entries[h.size]
}

// Reader provides sorted access to events from multiple rings
type Reader struct {
	rings  []*PerfRing   // Rings for each CPU
	heap   perfEntryHeap // Heap of entries sorted by timestamp
	inHeap []bool        // Tracks whether each ring has an entry in the heap
	active bool
}

// NewReader creates a new reader for accessing events
func NewReader() *Reader {
	return &Reader{
		rings: make([]*PerfRing, 0),
		heap: perfEntryHeap{
			entries: make([]perfEntry, 0),
		},
		inHeap: make([]bool, 0),
	}
}

// AddRing adds a ring to the collection
func (r *Reader) AddRing(ring *PerfRing) error {
	if r.active {
		return ErrAlreadyActive
	}

	r.rings = append(r.rings, ring)
	r.inHeap = append(r.inHeap, false)

	// Grow the heap entries slice if needed
	if cap(r.heap.entries) < len(r.rings) {
		newEntries := make([]perfEntry, len(r.rings))
		copy(newEntries, r.heap.entries)
		r.heap.entries = newEntries
	}
	return nil
}

// Start begins a read batch, initializing the heap with available entries
func (r *Reader) Start() error {
	if len(r.rings) == 0 {
		return ErrNoRings
	}
	if r.active {
		return ErrAlreadyActive
	}

	// Start read batches and initialize the heap
	for i, ring := range r.rings {
		ring.StartReadBatch()
		if !r.inHeap[i] {
			r.maintainHeapEntry(i)
		}
	}

	r.active = true
	return nil
}

// Finish ends the current read batch
func (r *Reader) Finish() error {
	if !r.active {
		return nil
	}

	for _, ring := range r.rings {
		ring.FinishReadBatch()
	}

	r.active = false
	return nil
}

// Empty returns true if there are no more events to read
func (r *Reader) Empty() bool {
	if !r.active {
		return true
	}
	return r.heap.size == 0
}

// PeekTimestamp returns the timestamp of the next event
func (r *Reader) PeekTimestamp() (uint64, error) {
	if !r.active {
		return 0, ErrNotActive
	}
	if r.heap.size == 0 {
		return 0, ErrBufferEmpty
	}
	return r.heap.entries[0].timestamp, nil
}

// CurrentRing returns the ring containing the next event
func (r *Reader) CurrentRing() (*PerfRing, error) {
	if !r.active {
		return nil, ErrNotActive
	}
	if r.heap.size == 0 {
		return nil, ErrBufferEmpty
	}
	entry := r.heap.entries[0]
	return r.rings[entry.ringIndex], nil
}

// Pop consumes the current event and updates the heap
func (r *Reader) Pop() error {
	if !r.active {
		return ErrNotActive
	}
	if r.heap.size == 0 {
		return ErrBufferEmpty
	}

	entry := r.heap.entries[0]
	ring := r.rings[entry.ringIndex]

	if err := ring.Pop(); err != nil {
		return err
	}

	// Update the heap entry for this ring
	r.maintainHeapEntry(entry.ringIndex)

	return nil
}

// maintainHeapEntry manages the heap entry for a ring
// For PERF_RECORD_SAMPLE records, the timestamp is read from the first 8 bytes of the record data.
// A timestamp of 0 is assigned in the following cases:
// - Non-sample records (e.g., PERF_RECORD_LOST)
// - Malformed sample records (less than 8 bytes)
// - Failed timestamp reads
// This ensures such records are processed as soon as possible.
func (r *Reader) maintainHeapEntry(idx int) {
	ring := r.rings[idx]
	inHeap := r.inHeap[idx]

	// sanity check: if we call maintainHeapEntry, the ring must be *the minimum* in the heap
	if inHeap && (r.heap.size == 0 || r.heap.entries[0].ringIndex != idx) {
		panic("maintainHeapEntry was called for a ring that is not the minimum in the heap (should never happen)")
	}

	// If the ring is empty, remove its entry if it's in the heap
	if _, err := ring.PeekSize(); err != nil {
		if r.inHeap[idx] {
			heap.Remove(&r.heap, 0)
			r.inHeap[idx] = false
		}
		return
	}

	// Get the timestamp for the current entry
	var timestamp uint64 = 0
	if ring.PeekType() == PERF_RECORD_SAMPLE {
		// Sample records have an 8-byte timestamp after the header
		// Skip the first 8 bytes (sample record) and read the timestamp
		buf := make([]byte, 8)
		if err := ring.PeekCopy(buf, 4); err == nil {
			timestamp = *(*uint64)(unsafe.Pointer(&buf[0]))
		}
	}
	// if we cannot read the timestamp, set it to 0 (most urgent to process)

	// Update or add the entry
	entry := perfEntry{
		timestamp: timestamp,
		ringIndex: idx,
	}

	if r.inHeap[idx] {
		// Update existing entry and fix heap
		r.heap.entries[0] = entry
		heap.Fix(&r.heap, 0)
	} else {
		// Add new entry
		heap.Push(&r.heap, entry)
		r.inHeap[idx] = true
	}
}
