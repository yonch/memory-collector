use std::collections::BTreeMap;
use thiserror::Error;

/// Errors that can occur during MinTracker operations
#[derive(Error, Debug, PartialEq)]
pub enum Error {
    /// The provided CPU ID was outside the valid range
    #[error("CPU ID {0} is out of range (max: {1})")]
    CpuIdOutOfRange(usize, usize),
    
    /// A timestamp update was attempted that would go backward in time
    #[error("Non-monotonic timestamp update for CPU {0}: previous={1}, new={2}")]
    NonMonotonicTimestamp(usize, u64, u64),
}

/// Tracks the minimum time slot that all CPUs have reported as complete.
///
/// `MinTracker` is designed to track CPU progress through time slots and 
/// determine the minimum time slot that all CPUs have completed. This is useful
/// for synchronization in multi-CPU systems where you need to know when all CPUs
/// have processed data up to a certain point in time.
///
/// # Features
///
/// - Monotonic timestamp enforcement (timestamps must always increase)
/// - Efficient calculation of minimum complete time slot using a BTreeMap
/// - Handles non-boundary timestamps by mapping them to time slot boundaries
///
/// # Examples
///
/// ```
/// use timeslot::MinTracker;
///
/// // Create a tracker with 1ms time slots and 4 CPUs
/// let mut tracker = MinTracker::new(1_000_000, 4); // 1ms in nanoseconds
///
/// // Update with timestamps from each CPU
/// tracker.update(0, 5_000_000).unwrap(); // CPU 0 reporting 5ms
/// tracker.update(1, 3_000_000).unwrap(); // CPU 1 reporting 3ms
/// tracker.update(2, 4_000_000).unwrap(); // CPU 2 reporting 4ms
/// tracker.update(3, 6_000_000).unwrap(); // CPU 3 reporting 6ms
///
/// // Get the minimum time slot that all CPUs have completed
/// assert_eq!(tracker.get_min(), Some(3_000_000));
/// ```
///
/// # Non-boundary timestamps
///
/// Timestamps don't need to be aligned to time slot boundaries:
///
/// ```
/// use timeslot::MinTracker;
///
/// let mut tracker = MinTracker::new(1000, 2);
///
/// // Non-boundary timestamps are mapped to their time slots
/// tracker.update(0, 5432).unwrap(); // Maps to time slot 5000
/// tracker.update(1, 3789).unwrap(); // Maps to time slot 3000
/// 
/// // The minimum time slot is 3000
/// assert_eq!(tracker.get_min(), Some(3000));
/// ```
pub struct MinTracker {
    /// Size of each time slot in nanoseconds
    time_slot_size: u64,
    
    /// Latest timestamp reported by each CPU
    cpu_timestamps: Vec<Option<u64>>,
    
    /// Map of time slots to count of CPUs reporting that time slot as their latest
    time_slot_counts: BTreeMap<u64, usize>,
    
    /// Count of CPUs that have not yet reported a timestamp
    uninitialized_cpus: usize,
}

impl MinTracker {
    /// Creates a new MinTracker.
    /// 
    /// # Arguments
    /// 
    /// * `time_slot_size` - The size of each time slot in nanoseconds
    /// * `num_cpus` - The number of CPUs to track
    ///
    /// # Examples
    ///
    /// ```
    /// use timeslot::MinTracker;
    ///
    /// // Create a tracker with 1ms time slots and 4 CPUs
    /// let tracker = MinTracker::new(1_000_000, 4);
    /// ```
    pub fn new(time_slot_size: u64, num_cpus: usize) -> Self {
        Self {
            time_slot_size,
            cpu_timestamps: vec![None; num_cpus],
            time_slot_counts: BTreeMap::new(),
            uninitialized_cpus: num_cpus,
        }
    }

    /// Updates the timestamp for a CPU.
    ///
    /// This method records a new timestamp for the specified CPU and updates
    /// internal tracking of which time slots are the minimum across all CPUs.
    ///
    /// # Arguments
    ///
    /// * `cpu_id` - The CPU ID (0-based)
    /// * `timestamp` - The timestamp reported by this CPU
    ///
    /// # Returns
    ///
    /// * `Result<(), Error>` - Returns an error if the timestamp is not monotonically 
    ///   increasing or if the CPU ID is out of range
    ///
    /// # Examples
    ///
    /// ```
    /// use timeslot::MinTracker;
    ///
    /// let mut tracker = MinTracker::new(1000, 2);
    /// 
    /// // Update CPU 0 with timestamp 5000
    /// tracker.update(0, 5000).unwrap();
    ///
    /// // Update CPU 1 with timestamp 3000
    /// tracker.update(1, 3000).unwrap();
    /// ```
    ///
    /// # Errors
    ///
    /// Returns an error if:
    /// 
    /// * The CPU ID is out of range (`CpuIdOutOfRange`)
    /// * The timestamp update is not monotonically increasing (`NonMonotonicTimestamp`)
    pub fn update(&mut self, cpu_id: usize, timestamp: u64) -> Result<(), Error> {
        // Check if the CPU ID is valid
        if cpu_id >= self.cpu_timestamps.len() {
            return Err(Error::CpuIdOutOfRange(cpu_id, self.cpu_timestamps.len() - 1));
        }

        // Get the current timestamp for this CPU
        let prev_timestamp = self.cpu_timestamps[cpu_id];
        
        // Calculate the time slot for the new timestamp
        let new_slot = timestamp / self.time_slot_size;
        
        match prev_timestamp {
            None => {
                // First report from this CPU
                self.uninitialized_cpus -= 1;
                
                // Increment the count for the new time slot
                *self.time_slot_counts.entry(new_slot).or_insert(0) += 1;
            }
            Some(prev) => {
                // Check if the timestamp is monotonically increasing
                if prev > timestamp {
                    return Err(Error::NonMonotonicTimestamp(cpu_id, prev, timestamp));
                }
                
                // Calculate the previous time slot
                let current_slot = prev / self.time_slot_size;
                
                // Only update if the time slot has changed
                if current_slot != new_slot {
                    // Decrement the count for the previous time slot
                    if let Some(count) = self.time_slot_counts.get_mut(&current_slot) {
                        *count -= 1;
                        if *count == 0 {
                            self.time_slot_counts.remove(&current_slot);
                        }
                    }
                    
                    // Increment the count for the new time slot
                    *self.time_slot_counts.entry(new_slot).or_insert(0) += 1;
                }
            }
        }
        
        // Update the CPU's timestamp
        self.cpu_timestamps[cpu_id] = Some(timestamp);
        
        Ok(())
    }

    /// Gets the minimum time slot that all CPUs have completed.
    ///
    /// This returns the lowest timestamp (aligned to a time slot boundary) that
    /// all CPUs have reported as completed. If any CPU has not yet reported a
    /// timestamp, this will return `None`.
    ///
    /// # Returns
    ///
    /// * `Option<u64>` - The minimum timestamp (aligned to a time slot boundary)
    ///   that all CPUs have processed, or `None` if not all CPUs have reported yet
    ///
    /// # Examples
    ///
    /// ```
    /// use timeslot::MinTracker;
    ///
    /// let mut tracker = MinTracker::new(1000, 2);
    /// 
    /// // Not all CPUs have reported yet
    /// tracker.update(0, 5000).unwrap();
    /// assert_eq!(tracker.get_min(), None);
    ///
    /// // Now all CPUs have reported
    /// tracker.update(1, 3000).unwrap();
    /// assert_eq!(tracker.get_min(), Some(3000));
    /// ```
    pub fn get_min(&self) -> Option<u64> {
        // If not all CPUs have reported yet, return None
        if self.uninitialized_cpus > 0 {
            return None;
        }
        
        // Find the minimum time slot with a non-zero count
        self.time_slot_counts.keys().next().map(|&min_slot| {
            // Return the time slot boundary
            min_slot * self.time_slot_size
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use rstest::rstest;

    #[test]
    fn test_initialization() {
        let tracker = MinTracker::new(1000, 4);
        assert_eq!(tracker.get_min(), None, "All CPUs should report before get_min returns a value");
    }

    #[test]
    fn test_single_cpu_update() {
        let mut tracker = MinTracker::new(1000, 1);
        
        // First update should initialize the CPU
        tracker.update(0, 5000).unwrap();
        
        // Now get_min should return the time slot
        assert_eq!(tracker.get_min(), Some(5000 / 1000 * 1000));
    }

    #[test]
    fn test_multiple_cpus_initialization() {
        let mut tracker = MinTracker::new(1000, 3);
        
        // Update CPUs one by one
        tracker.update(0, 5000).unwrap();
        assert_eq!(tracker.get_min(), None);
        
        tracker.update(1, 3000).unwrap();
        assert_eq!(tracker.get_min(), None);
        
        // After all CPUs report, get_min should return the minimum time slot
        tracker.update(2, 4000).unwrap();
        assert_eq!(tracker.get_min(), Some(3000 / 1000 * 1000));
    }

    #[test]
    fn test_monotonic_requirement() {
        let mut tracker = MinTracker::new(1000, 1);
        
        // First update
        tracker.update(0, 5000).unwrap();
        
        // Non-monotonic update should fail
        let result = tracker.update(0, 4000);
        assert!(result.is_err());
        
        if let Err(Error::NonMonotonicTimestamp(cpu_id, prev, new)) = result {
            assert_eq!(cpu_id, 0);
            assert_eq!(prev, 5000);
            assert_eq!(new, 4000);
        } else {
            panic!("Expected NonMonotonicTimestamp error");
        }
    }

    #[test]
    fn test_cpu_id_out_of_range() {
        let mut tracker = MinTracker::new(1000, 2);
        
        // CPU ID 2 is out of range for a tracker with 2 CPUs
        let result = tracker.update(2, 5000);
        assert!(result.is_err());
        
        if let Err(Error::CpuIdOutOfRange(cpu_id, max)) = result {
            assert_eq!(cpu_id, 2);
            assert_eq!(max, 1);
        } else {
            panic!("Expected CpuIdOutOfRange error");
        }
    }

    #[rstest]
    #[case(1000, vec![(0, 5000), (1, 3000), (0, 7000)], Some(3000))]
    #[case(1000, vec![(0, 5000), (1, 6000), (0, 8000), (1, 9000)], Some(8000))]
    #[case(1000, vec![(0, 1000), (1, 2000), (2, 3000), (0, 4000), (1, 5000)], Some(3000))]
    fn test_various_update_patterns(
        #[case] time_slot_size: u64,
        #[case] updates: Vec<(usize, u64)>,
        #[case] expected_min: Option<u64>,
    ) {
        let num_cpus = updates.iter().map(|(cpu, _)| cpu + 1).max().unwrap_or(0);
        let mut tracker = MinTracker::new(time_slot_size, num_cpus);
        
        for (cpu, timestamp) in updates {
            tracker.update(cpu, timestamp).unwrap();
        }
        
        assert_eq!(tracker.get_min(), expected_min);
    }

    #[test]
    fn test_large_time_slot_jumps() {
        let mut tracker = MinTracker::new(1000, 2);
        
        tracker.update(0, 5000).unwrap();
        tracker.update(1, 3000).unwrap();
        
        // Large jump in time for CPU 0
        tracker.update(0, 50000).unwrap();
        assert_eq!(tracker.get_min(), Some(3000 / 1000 * 1000));
        
        // Now update CPU 1 to a higher value
        tracker.update(1, 40000).unwrap();
        assert_eq!(tracker.get_min(), Some(40000 / 1000 * 1000));
    }

    #[test]
    fn test_non_boundary_timestamps() {
        let mut tracker = MinTracker::new(1000, 2);
        
        // Update with non-boundary timestamps
        tracker.update(0, 5432).unwrap(); // Should map to time slot 5000
        tracker.update(1, 3789).unwrap(); // Should map to time slot 3000
        
        // get_min should return the minimum time slot
        assert_eq!(tracker.get_min(), Some(3000));
        
        // Update with more non-boundary timestamps
        tracker.update(0, 7123).unwrap(); // Should map to time slot 7000
        tracker.update(1, 8456).unwrap(); // Should map to time slot 8000
        
        // get_min should return the updated minimum time slot
        assert_eq!(tracker.get_min(), Some(7000));
    }

    #[test]
    fn test_multiple_updates_same_time_slot() {
        let mut tracker = MinTracker::new(1000, 2);
        
        // First updates
        tracker.update(0, 5432).unwrap(); // Time slot 5
        tracker.update(1, 3789).unwrap(); // Time slot 3
        
        // Update CPU 0 again but still in the same time slot
        tracker.update(0, 5999).unwrap(); // Still in time slot 5
        
        // Minimum should still be 3000
        assert_eq!(tracker.get_min(), Some(3000));
        
        // Update CPU 1 to a new time slot
        tracker.update(1, 6100).unwrap(); // Now in time slot 6
        
        // Minimum should now be 5000
        assert_eq!(tracker.get_min(), Some(5000));
    }
} 