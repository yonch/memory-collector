use crate::metrics::Metric;
use crate::task_metadata::TaskMetadata;
use std::collections::HashMap;

/// Represents data collected for a specific timeslot
pub struct TimeslotData {
    /// Timestamp at the end of this timeslot
    pub start_timestamp: u64,
    /// Map from PID to task data (metadata + metrics)
    pub tasks: HashMap<u32, TaskData>,
}

/// Combines task metadata with metrics
pub struct TaskData {
    /// Task metadata (may be None for kernel threads)
    pub metadata: Option<TaskMetadata>,
    /// Performance metrics for this task
    pub metrics: Metric,
}

impl TimeslotData {
    /// Creates a new timeslot data container
    pub fn new(start_timestamp: u64) -> Self {
        Self {
            start_timestamp,
            tasks: HashMap::new(),
        }
    }

    /// Updates or inserts task data for a given PID
    pub fn update(&mut self, pid: u32, metadata: Option<TaskMetadata>, metrics: Metric) {
        if let Some(task_data) = self.tasks.get_mut(&pid) {
            // Update existing entry
            task_data.metrics.add(&metrics);
        } else {
            // Create new entry
            self.tasks.insert(pid, TaskData::new(metadata, metrics));
        }
    }

    /// Returns an iterator over all task data
    pub fn iter_tasks(&self) -> impl Iterator<Item = (&u32, &TaskData)> {
        self.tasks.iter()
    }

    /// Returns the number of tracked tasks
    pub fn task_count(&self) -> usize {
        self.tasks.len()
    }
}

impl TaskData {
    /// Creates a new task data entry
    pub fn new(metadata: Option<TaskMetadata>, metrics: Metric) -> Self {
        Self { metadata, metrics }
    }
}
