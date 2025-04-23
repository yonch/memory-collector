//! # perf
//!
//! A Rust library for working with eBPF perf ring buffers. This crate provides
//! interfaces for interacting with Linux perf ring buffers commonly used for
//! eBPF programs.
//!

mod memory_storage;
#[cfg(target_os = "linux")]
mod mmap_storage;
mod reader;
mod ring;
mod helpers;
pub mod map_reader;

pub use memory_storage::*;
#[cfg(target_os = "linux")]
pub use mmap_storage::*;
pub use reader::*;
pub use ring::*;
pub use helpers::*;

use std::os::unix::io::RawFd;
use thiserror::Error;

/// Errors that can occur when using perf ring storage
#[derive(Error, Debug)]
pub enum StorageError {
    #[error("OS error: {0}")]
    OsError(std::io::Error),
}

/// Perf ring buffer storage trait
pub trait Storage {
    /// Return the raw data buffer containing metadata page and data pages
    fn data(&self) -> &[u8];

    /// Return the number of data pages in the ring buffer
    fn num_data_pages(&self) -> u32;

    /// Return the system page size
    fn page_size(&self) -> u64;

    /// Return the file descriptor if this is a perf event storage, or -1 otherwise
    fn file_descriptor(&self) -> RawFd;
}
