use std::collections::HashMap;

/// Represents metadata for a single task
#[derive(Clone)]
pub struct TaskMetadata {
    pub pid: u32,
    pub comm: [u8; 16],
}

impl TaskMetadata {
    pub fn new(pid: u32, comm: [u8; 16]) -> Self {
        Self { pid, comm }
    }
}

/// Collection to manage multiple tasks with queued removal support
pub struct TaskCollection {
    tasks: HashMap<u32, TaskMetadata>,
    removal_queue: Vec<u32>,
}

impl TaskCollection {
    pub fn new() -> Self {
        Self {
            tasks: HashMap::new(),
            removal_queue: Vec::new(),
        }
    }

    /// Add a task to the collection
    pub fn add(&mut self, metadata: TaskMetadata) {
        self.tasks.insert(metadata.pid, metadata);
    }

    /// Look up a task by its PID
    pub fn lookup(&self, pid: u32) -> Option<&TaskMetadata> {
        self.tasks.get(&pid)
    }

    /// Queue a task for removal without immediately removing it
    pub fn queue_removal(&mut self, pid: u32) {
        if self.tasks.contains_key(&pid) {
            self.removal_queue.push(pid);
        }
    }

    /// Execute all queued removals
    pub fn flush_removals(&mut self) {
        for pid in self.removal_queue.drain(..) {
            self.tasks.remove(&pid);
        }
    }

    /// Get an iterator over all tasks
    pub fn iter(&self) -> impl Iterator<Item = &TaskMetadata> {
        self.tasks.values()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_task_collection() {
        let mut collection = TaskCollection::new();
        
        // Add tasks
        let task1 = TaskMetadata::new(1, [0; 16]);
        let task2 = TaskMetadata::new(2, [0; 16]);
        collection.add(task1);
        collection.add(task2);
        
        // Lookup
        assert!(collection.lookup(1).is_some());
        assert!(collection.lookup(2).is_some());
        assert!(collection.lookup(3).is_none());
        
        // Queue removal
        collection.queue_removal(1);
        
        // Task should still be available before flush
        assert!(collection.lookup(1).is_some());
        
        // Flush removals
        collection.flush_removals();
        
        // Task should be gone after flush
        assert!(collection.lookup(1).is_none());
        assert!(collection.lookup(2).is_some());
    }
} 