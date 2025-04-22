// Package rmid provides userspace tracking of RMID allocations and frees
package rmid

import (
	"sort"
)

// MessageType represents the type of RMID message
type MessageType uint32

const (
	// MessageTypeAlloc represents an RMID allocation message
	MessageTypeAlloc MessageType = 1
	// MessageTypeFree represents an RMID free message
	MessageTypeFree MessageType = 2
)

// Metadata represents the metadata associated with an RMID
type Metadata struct {
	Comm  string // Process command name
	Valid bool   // Whether the RMID is currently valid
}

// Message represents a timestamped RMID update
type Message struct {
	Type      MessageType
	RMID      uint32
	Metadata  Metadata
	Timestamp uint64
}

// Tracker maintains the state of RMID allocations and frees
type Tracker struct {
	// Current state of RMIDs
	rmids map[uint32]Metadata
	// Queue of pending updates
	updates []Message
}

// NewTracker creates a new RMID tracker
func NewTracker() *Tracker {
	return &Tracker{
		rmids:   make(map[uint32]Metadata),
		updates: make([]Message, 0),
	}
}

// Alloc enqueues an RMID allocation with metadata
func (t *Tracker) Alloc(rmid uint32, comm string, timestamp uint64) {
	meta := Metadata{
		Comm:  comm,
		Valid: true,
	}

	msg := Message{
		Type:      MessageTypeAlloc,
		RMID:      rmid,
		Metadata:  meta,
		Timestamp: timestamp,
	}

	t.updates = append(t.updates, msg)
}

// Free enqueues an RMID free event
func (t *Tracker) Free(rmid uint32, timestamp uint64) {
	msg := Message{
		Type:      MessageTypeFree,
		RMID:      rmid,
		Timestamp: timestamp,
	}

	t.updates = append(t.updates, msg)
}

// Advance processes queued events up to the given timestamp
func (t *Tracker) Advance(timestamp uint64) {
	// Updates are sorted by timestamp

	// Find index of first update beyond timestamp using safe arithmetic
	splitIdx := sort.Search(len(t.updates), func(i int) bool {
		return (t.updates[i].Timestamp - timestamp) < (1 << 63)
	})

	// Process updates up to timestamp
	for _, msg := range t.updates[:splitIdx] {
		switch msg.Type {
		case MessageTypeAlloc:
			// Update metadata
			t.rmids[msg.RMID] = msg.Metadata
		case MessageTypeFree:
			// Mark RMID as invalid but preserve metadata
			if meta, exists := t.rmids[msg.RMID]; exists {
				meta.Valid = false
				t.rmids[msg.RMID] = meta
			}
		}
	}

	// Remove processed updates
	t.updates = t.updates[splitIdx:]
}

// GetMetadata returns the metadata for an RMID
func (t *Tracker) GetMetadata(rmid uint32) (Metadata, bool) {
	meta, exists := t.rmids[rmid]
	return meta, exists
}

// GetAllMetadata returns a copy of all stored metadata
func (t *Tracker) GetAllMetadata() map[uint32]Metadata {
	result := make(map[uint32]Metadata, len(t.rmids))
	for id, meta := range t.rmids {
		result[id] = meta
	}
	return result
}

// Reset clears all state and pending updates
func (t *Tracker) Reset() {
	t.rmids = make(map[uint32]Metadata)
	t.updates = t.updates[:0]
}
