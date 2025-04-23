use std::cmp::Ordering as CmpOrdering;
use std::collections::BinaryHeap;
use thiserror::Error;

use crate::{PerfRing, PerfRingError, PERF_RECORD_SAMPLE};

/// Errors that can occur when using the ring reader
#[derive(Error, Debug)]
pub enum RingReaderError {
    #[error("no rings available")]
    NoRings,

    #[error("reader is not active")]
    NotActive,

    #[error("reader is already active")]
    AlreadyActive,

    #[error("buffer empty")]
    BufferEmpty,

    #[error("perf ring error: {0}")]
    PerfRingError(#[from] PerfRingError),
}

/// A perf entry represents a timestamped entry from a specific ring
struct PerfEntry {
    timestamp: u64,
    ring_index: usize,
}

impl Eq for PerfEntry {}

impl PartialEq for PerfEntry {
    fn eq(&self, other: &Self) -> bool {
        self.timestamp == other.timestamp
    }
}

// We implement Ord and PartialOrd to create a min-heap
// (BinaryHeap in Rust is a max-heap by default)
impl PartialOrd for PerfEntry {
    fn partial_cmp(&self, other: &Self) -> Option<CmpOrdering> {
        Some(self.cmp(other))
    }
}

impl Ord for PerfEntry {
    fn cmp(&self, other: &Self) -> CmpOrdering {
        // Reverse ordering for min-heap
        other.timestamp.cmp(&self.timestamp)
    }
}

/// RingReader provides sorted access to events from multiple perf rings
pub struct Reader {
    rings: Vec<PerfRing>,
    heap: BinaryHeap<PerfEntry>,
    in_heap: Vec<bool>,
    active: bool,
}

impl Reader {
    /// Creates a new reader for accessing events
    pub fn new() -> Self {
        Reader {
            rings: Vec::new(),
            heap: BinaryHeap::new(),
            in_heap: Vec::new(),
            active: false,
        }
    }

    /// Adds a ring to the collection
    pub fn add_ring(&mut self, ring: PerfRing) -> Result<(), RingReaderError> {
        if self.active {
            return Err(RingReaderError::AlreadyActive);
        }

        self.rings.push(ring);
        self.in_heap.push(false);

        Ok(())
    }

    /// Begins a read batch, initializing the heap with available entries
    pub fn start(&mut self) -> Result<(), RingReaderError> {
        if self.rings.is_empty() {
            return Err(RingReaderError::NoRings);
        }

        if self.active {
            return Err(RingReaderError::AlreadyActive);
        }

        // Start read batches and initialize the heap
        for i in 0..self.rings.len() {
            self.rings[i].start_read_batch();

            if !self.in_heap[i] {
                self.maintain_heap_entry(i)?;
            }
        }

        self.active = true;
        Ok(())
    }

    /// Ends the current read batch
    pub fn finish(&mut self) -> Result<(), RingReaderError> {
        if !self.active {
            return Ok(());
        }

        for ring in &mut self.rings {
            ring.finish_read_batch();
        }

        self.active = false;
        Ok(())
    }

    /// Returns true if there are no more events to read
    pub fn is_empty(&self) -> bool {
        if !self.active {
            return true;
        }

        self.heap.is_empty()
    }

    /// Returns the timestamp of the next event
    pub fn peek_timestamp(&self) -> Result<u64, RingReaderError> {
        if !self.active {
            return Err(RingReaderError::NotActive);
        }

        self.heap
            .peek()
            .map(|entry| entry.timestamp)
            .ok_or(RingReaderError::BufferEmpty)
    }

    /// Returns the ring containing the next event and its index
    pub fn current_ring(&self) -> Result<(&PerfRing, usize), RingReaderError> {
        if !self.active {
            return Err(RingReaderError::NotActive);
        }

        match self.heap.peek() {
            Some(entry) => Ok((&self.rings[entry.ring_index], entry.ring_index)),
            None => Err(RingReaderError::BufferEmpty),
        }
    }

    /// Consumes the current event and updates the heap
    pub fn pop(&mut self) -> Result<(), RingReaderError> {
        if !self.active {
            return Err(RingReaderError::NotActive);
        }

        let Some(entry) = self.heap.pop() else {
            return Err(RingReaderError::BufferEmpty);
        };

        self.rings[entry.ring_index].pop()?;

        // Update the heap entry for this ring
        self.maintain_heap_entry(entry.ring_index)?;

        Ok(())
    }

    /// Manages the heap entry for a ring
    /// For PERF_RECORD_SAMPLE records, the timestamp is read from the first 8 bytes of the record data.
    /// A timestamp of 0 is assigned in the following cases:
    /// - Non-sample records (e.g., PERF_RECORD_LOST)
    /// - Malformed sample records (less than 8 bytes)
    /// - Failed timestamp reads
    /// This ensures such records are processed as soon as possible.
    fn maintain_heap_entry(&mut self, idx: usize) -> Result<(), RingReaderError> {
        let in_heap = self.in_heap[idx];

        // If the ring is empty, remove its entry if it's in the heap
        let bytes_remaining = self.rings[idx].bytes_remaining();
        if bytes_remaining == 0 {
            if self.in_heap[idx] {
                // Remove from heap (BinaryHeap doesn't have a direct remove method,
                // so we need to rebuild without that element)
                self.in_heap[idx] = false;
                let ring_index = idx;
                self.heap.retain(|entry| entry.ring_index != ring_index);
            }
            return Ok(());
        };

        // Get the timestamp for the current entry
        let mut timestamp = 0;
        if self.rings[idx].peek_type() == PERF_RECORD_SAMPLE {
            // Sample records have an 8-byte timestamp after the header
            // Skip the first 8 bytes (sample record) and read the timestamp
            let mut buf = [0u8; 8];
            if self.rings[idx].peek_copy(&mut buf, 4).is_ok() {
                timestamp = u64::from_le_bytes(buf);
            }
        }
        // if we cannot read the timestamp, leave it as 0 (most urgent to process)

        // Update or add the entry
        let entry = PerfEntry {
            timestamp,
            ring_index: idx,
        };

        if in_heap {
            // Since BinaryHeap doesn't have a direct way to update entries,
            // we remove and re-add
            let ring_index = idx;
            self.heap.retain(|entry| entry.ring_index != ring_index);
            self.heap.push(entry);
        } else {
            // Add new entry
            self.heap.push(entry);
            self.in_heap[idx] = true;
        }

        Ok(())
    }
}

impl Default for Reader {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use crate::PERF_RECORD_LOST;

    use super::*;

    #[test]
    fn test_ring_reader() {
        let mut reader = Reader::new();

        // Create test rings
        let page_size = 4096u64;
        let n_pages = 2u32;
        let mut data1 = vec![0u8; (page_size * (1 + u64::from(n_pages))) as usize];
        let mut data2 = vec![0u8; (page_size * (1 + u64::from(n_pages))) as usize];

        let ring1 = unsafe { PerfRing::init_contiguous(&mut data1, n_pages, page_size).unwrap() };
        let ring2 = unsafe { PerfRing::init_contiguous(&mut data2, n_pages, page_size).unwrap() };

        // Add rings to reader
        reader.add_ring(ring1).unwrap();
        reader.add_ring(ring2).unwrap();

        // recreate the rings from the same memory ranges
        let mut ring1 =
            unsafe { PerfRing::init_contiguous(&mut data1, n_pages, page_size).unwrap() };
        let mut ring2 =
            unsafe { PerfRing::init_contiguous(&mut data2, n_pages, page_size).unwrap() };

        // Test that adding a ring while active fails
        reader.start().unwrap();
        assert!(matches!(
            reader.add_ring(unsafe {
                PerfRing::init_contiguous(&mut data1, n_pages, page_size).unwrap()
            }),
            Err(RingReaderError::AlreadyActive)
        ));
        reader.finish().unwrap();

        // Initially should be empty
        reader.start().unwrap();
        assert!(reader.is_empty());
        reader.finish().unwrap();

        // Test operations before Start should fail
        assert!(reader.is_empty());
        assert!(matches!(
            reader.peek_timestamp(),
            Err(RingReaderError::NotActive)
        ));
        assert!(matches!(
            reader.current_ring(),
            Err(RingReaderError::NotActive)
        ));
        assert!(matches!(reader.pop(), Err(RingReaderError::NotActive)));

        // Create events with timestamps
        let mut event1 = vec![0u8; 16]; // 8 bytes for timestamp + "event1"
        event1[0..8].copy_from_slice(&100u64.to_le_bytes()); // timestamp 100
        event1[8..16].copy_from_slice(b"event1  ");

        let mut event2 = vec![0u8; 16]; // 8 bytes for timestamp + "event2"
        event2[0..8].copy_from_slice(&200u64.to_le_bytes()); // timestamp 200
        event2[8..16].copy_from_slice(b"event2  ");

        // Write events to rings
        ring1.start_write_batch();
        ring1.write(&event1, PERF_RECORD_SAMPLE).unwrap();
        ring1.finish_write_batch();

        ring2.start_write_batch();
        ring2.write(&event2, PERF_RECORD_SAMPLE).unwrap();
        ring2.finish_write_batch();

        // Start a new batch to see the new events
        reader.start().unwrap();

        // Test reading events
        assert!(!reader.is_empty());

        // Pop events and verify they come in timestamp order
        let expected_timestamps = [100, 200];
        let expected_ring_data = [&event1[..], &event2[..]];

        for (i, &expected) in expected_timestamps.iter().enumerate() {
            let ts = reader.peek_timestamp().unwrap();
            assert_eq!(ts, expected, "Expected timestamp {}, got {}", expected, ts);

            // Get current ring and verify it's not nil
            let (ring, idx) = reader.current_ring().unwrap();
            assert!(
                idx < reader.rings.len(),
                "Ring index {} out of bounds [0, {})",
                idx,
                reader.rings.len()
            );

            // Copy the ring's data into a new buffer
            let size = ring.peek_size().unwrap();

            // Calculate expected size (aligned to 8 bytes)
            let expected_size = ((expected_ring_data[i].len() + 4 + 7) / 8) * 8;
            assert_eq!(
                size, expected_size,
                "Expected size {}, got {}",
                expected_size, size
            );

            let mut ring_data = vec![0u8; expected_ring_data[i].len()];
            ring.peek_copy(&mut ring_data, 4).unwrap();

            assert_eq!(
                &ring_data[..],
                expected_ring_data[i],
                "Expected ring data {:?}, got {:?}",
                expected_ring_data[i],
                ring_data
            );

            reader.pop().unwrap();
        }

        // Should be empty after reading all events
        assert!(
            reader.is_empty(),
            "Expected reader to be empty after reading all events"
        );

        // Finish the reader
        reader.finish().unwrap();

        // Test operations after Finish should fail
        assert!(reader.is_empty());
        assert!(matches!(
            reader.peek_timestamp(),
            Err(RingReaderError::NotActive)
        ));
        assert!(matches!(
            reader.current_ring(),
            Err(RingReaderError::NotActive)
        ));
        assert!(matches!(reader.pop(), Err(RingReaderError::NotActive)));
    }

    #[test]
    fn test_lost_records() {
        let mut reader = Reader::new();

        // Create two test rings
        let page_size = 4096u64;
        let n_pages = 2u32;
        let mut data1 = vec![0u8; (page_size * (1 + u64::from(n_pages))) as usize];
        let mut data2 = vec![0u8; (page_size * (1 + u64::from(n_pages))) as usize];

        let ring1 = unsafe { PerfRing::init_contiguous(&mut data1, n_pages, page_size).unwrap() };
        let ring2 = unsafe { PerfRing::init_contiguous(&mut data2, n_pages, page_size).unwrap() };

        reader.add_ring(ring1).unwrap();
        reader.add_ring(ring2).unwrap();

        let mut ring1 =
            unsafe { PerfRing::init_contiguous(&mut data1, n_pages, page_size).unwrap() };
        let mut ring2 =
            unsafe { PerfRing::init_contiguous(&mut data2, n_pages, page_size).unwrap() };

        // Test 1: Show that events within a single ring maintain their order regardless of type
        let mut event1 = vec![0u8; 16];
        event1[0..8].copy_from_slice(&100u64.to_le_bytes()); // timestamp 100
        event1[8..16].copy_from_slice(b"event1  ");

        let mut event2 = vec![0u8; 16]; // Lost event data
        event2[0..8].copy_from_slice(&0u64.to_le_bytes()); // timestamp doesn't matter for lost events
        event2[8..16].copy_from_slice(b"lost!   ");

        // Write both events to ring1
        ring1.start_write_batch();
        ring1.write(&event1, PERF_RECORD_SAMPLE).unwrap();
        ring1.write(&event2, PERF_RECORD_LOST).unwrap();
        ring1.finish_write_batch();

        // Start reader and verify events come in ring order (not by type)
        reader.start().unwrap();

        // First event should be event1 (timestamp 100)
        let ts = reader.peek_timestamp().unwrap();
        assert_eq!(ts, 100, "Expected timestamp 100, got {}", ts);

        let (ring, idx) = reader.current_ring().unwrap();
        assert_eq!(idx, 0, "Expected ring index 0, got {}", idx);
        assert_eq!(
            ring.peek_type(),
            PERF_RECORD_SAMPLE,
            "Expected PERF_RECORD_SAMPLE"
        );
        reader.pop().unwrap();

        // Second event should be lost event (timestamp 0)
        let ts = reader.peek_timestamp().unwrap();
        assert_eq!(ts, 0, "Expected timestamp 0 for lost event, got {}", ts);

        let (ring, idx) = reader.current_ring().unwrap();
        assert_eq!(idx, 0, "Expected ring index 0, got {}", idx);
        assert_eq!(
            ring.peek_type(),
            PERF_RECORD_LOST,
            "Expected PERF_RECORD_LOST"
        );
        reader.pop().unwrap();

        reader.finish().unwrap();

        // Test 2: Show that lost events from one ring are processed before normal events from another ring
        // Ring1: Normal event with timestamp 100
        // Ring2: Lost event (should get timestamp 0)
        let mut normal_event = vec![0u8; 16];
        normal_event[0..8].copy_from_slice(&100u64.to_le_bytes()); // timestamp 100
        normal_event[8..16].copy_from_slice(b"normal  ");

        let mut lost_event = vec![0u8; 16];
        lost_event[8..16].copy_from_slice(b"lost!   ");

        // Write events to rings
        ring1.start_write_batch();
        ring1.write(&normal_event, PERF_RECORD_SAMPLE).unwrap();
        ring1.finish_write_batch();

        ring2.start_write_batch();
        ring2.write(&lost_event, PERF_RECORD_LOST).unwrap();
        ring2.finish_write_batch();

        // Start reader and verify lost event comes first
        reader.start().unwrap();

        // First event should be lost event (timestamp 0)
        let ts = reader.peek_timestamp().unwrap();
        assert_eq!(ts, 0, "Expected timestamp 0 for lost event, got {}", ts);

        let (ring, idx) = reader.current_ring().unwrap();
        assert_eq!(idx, 1, "Expected ring index 1, got {}", idx);
        assert_eq!(
            ring.peek_type(),
            PERF_RECORD_LOST,
            "Expected PERF_RECORD_LOST"
        );
        reader.pop().unwrap();

        // Second event should be normal event (timestamp 100)
        let ts = reader.peek_timestamp().unwrap();
        assert_eq!(
            ts, 100,
            "Expected timestamp 100 for normal event, got {}",
            ts
        );

        let (ring, idx) = reader.current_ring().unwrap();
        assert_eq!(idx, 0, "Expected ring index 0, got {}", idx);
        assert_eq!(
            ring.peek_type(),
            PERF_RECORD_SAMPLE,
            "Expected PERF_RECORD_SAMPLE"
        );
        reader.pop().unwrap();

        // Should be empty after reading all events
        assert!(
            reader.is_empty(),
            "Expected reader to be empty after reading all events"
        );

        reader.finish().unwrap();
    }
}
