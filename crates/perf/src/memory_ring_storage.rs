use std::os::fd::RawFd;

use crate::RingStorageError;
use crate::RingStorage;


/// Memory-based ring storage implementation
/// 
/// This is useful for testing and inter-thread communication
pub struct MemoryRingStorage {
    data: Vec<u8>,
    n_data_pages: u32,
    page_size: u64,
}

impl MemoryRingStorage {
    /// Create a new memory-based ring storage
    pub fn new(n_pages: u32) -> Result<Self, RingStorageError> {
        let page_size = page_size::get() as u64;
        let total_size = page_size * (1 + u64::from(n_pages)); // 1 metadata page + data pages

        let data = vec![0; total_size as usize];
        
        Ok(MemoryRingStorage {
            data,
            n_data_pages: n_pages,
            page_size,
        })
    }
}

impl RingStorage for MemoryRingStorage {
    fn data(&self) -> &[u8] {
        &self.data
    }
    
    fn num_data_pages(&self) -> u32 {
        self.n_data_pages
    }
    
    fn page_size(&self) -> u64 {
        self.page_size
    }
    
    fn file_descriptor(&self) -> RawFd {
        -1
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_memory_ring_storage() {
        let n_pages = 2;
        let storage = MemoryRingStorage::new(n_pages).unwrap();
        
        // Check basic properties
        assert_eq!(storage.num_data_pages(), n_pages);
        assert_eq!(storage.page_size(), page_size::get() as u64);
        
        let expected_size = storage.page_size() * (1 + u64::from(n_pages));
        assert_eq!(storage.data().len() as u64, expected_size);
        
        assert_eq!(storage.file_descriptor(), -1);
    }
}