//! # perf
//!
//! A Rust library for working with eBPF perf ring buffers. This crate provides
//! interfaces for interacting with Linux perf ring buffers commonly used for
//! eBPF programs.
//!

mod ring;
mod memory_ring_storage;
mod ring_reader;
#[cfg(target_os = "linux")]
mod mmap_ring_storage;

pub use ring::*;
pub use memory_ring_storage::*;
pub use ring_reader::*;
#[cfg(target_os = "linux")]
pub use mmap_ring_storage::*;

use std::os::unix::io::RawFd;
use thiserror::Error;

/// Errors that can occur when using perf ring storage
#[derive(Error, Debug)]
pub enum RingStorageError {
    #[error("OS error: {0}")]
    OsError(std::io::Error),
}

/// Perf ring buffer storage trait
pub trait RingStorage {
    /// Return the raw data buffer containing metadata page and data pages
    fn data(&self) -> &[u8];
    
    /// Return the number of data pages in the ring buffer
    fn num_data_pages(&self) -> u32;
    
    /// Return the system page size
    fn page_size(&self) -> u64;
    
    /// Return the file descriptor if this is a perf event storage, or -1 otherwise
    fn file_descriptor(&self) -> RawFd;
}
