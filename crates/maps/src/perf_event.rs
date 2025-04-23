//! Perf event utilities for eBPF maps.
//!
//! This module provides functions for opening perf events and
//! setting them up for use with eBPF maps.

use libbpf_rs::{MapCore as _, MapMut};
use perf_event_open_sys as sys;
use std::io;

/// Error type for perf event operations
#[derive(Debug, thiserror::Error)]
pub enum PerfEventError {
    /// Error opening perf event
    #[error("failed to open perf event on CPU {cpu}: {source}")]
    OpenError {
        /// CPU where the error occurred
        cpu: i32,
        /// Source error
        source: io::Error,
    },

    /// Error updating map
    #[error("failed to update map for CPU {cpu}: {source}")]
    MapUpdateError {
        /// CPU where the error occurred
        cpu: i32,
        /// Source error
        source: libbpf_rs::Error,
    },

    /// Error enabling perf event
    #[error("failed to enable perf event: {0}")]
    EnableError(io::Error),

    /// Error getting map info
    #[error("failed to get map info: {0}")]
    MapInfoError(libbpf_rs::Error),
}

/// Opens perf events for each CPU and updates the provided map with the file descriptors.
///
/// # Arguments
///
/// * `map` - A mutable reference to a libbpf-rs map to store the file descriptors
/// * `attr` - Perf event attributes
///
/// # Returns
///
/// * `Ok(())` on success
/// * `Err(PerfEventError)` on failure
///
/// # Example
///
/// ```no_run
/// use maps::perf_event;
/// use libbpf_rs::MapMut;
/// use perf_event_open_sys as sys;
///
/// fn example(map: &mut MapMut) -> Result<(), perf_event::PerfEventError> {
///     // Configure perf event attributes - similar to MmapStorage::new
///     let mut attr = sys::bindings::perf_event_attr::default();
///     attr.size = std::mem::size_of::<sys::bindings::perf_event_attr>() as u32;
///     attr.type_ = sys::bindings::PERF_TYPE_SOFTWARE;
///     attr.config = sys::bindings::PERF_COUNT_SW_BPF_OUTPUT as u64;
///     attr.sample_type = sys::bindings::PERF_SAMPLE_RAW as u64;
///     
///     // Configure watermark behavior
///     let n_watermark_bytes = 0; // Wake up on every event
///     if n_watermark_bytes > 0 {
///         attr.set_watermark(1);
///         attr.__bindgen_anon_2.wakeup_watermark = n_watermark_bytes;
///     } else {
///         attr.__bindgen_anon_2.wakeup_events = 1;
///     }
///     
///     perf_event::open_events(map, &mut attr)?;
///     perf_event::start_events(map)?;
///     
///     Ok(())
/// }
/// ```
pub fn open_events(
    map: &mut MapMut,
    attr: &mut sys::bindings::perf_event_attr,
) -> Result<(), PerfEventError> {
    // Determine number of CPUs from map max entries
    let n_cpu = map.info().map(|info| info.info.max_entries  as i32).map_err(|e| PerfEventError::MapInfoError(e))?;

    // Open perf events for each CPU
    for cpu in 0..n_cpu {
        // Open perf event
        let fd = unsafe {
            sys::perf_event_open(
                attr,
                -1, // pid (all threads)
                cpu,
                -1, // group_fd
                sys::bindings::PERF_FLAG_FD_CLOEXEC as u64,
            )
        };

        if fd < 0 {
            return Err(PerfEventError::OpenError {
                cpu,
                source: io::Error::last_os_error(),
            });
        }

        // Store FD in map
        // Convert cpu to u32 for key and fd to u32 for value
        let key = cpu.to_le_bytes();
        let value = (fd as u32).to_le_bytes();

        if let Err(err) = map.update(&key, &value, libbpf_rs::MapFlags::ANY) {
            // Close the fd we just opened to avoid leaking it
            unsafe {
                libc::close(fd);
            }

            return Err(PerfEventError::MapUpdateError { cpu, source: err });
        }
    }

    Ok(())
}

/// Enables all perf events stored in the map.
///
/// # Arguments
///
/// * `map` - A reference to a libbpf-rs map containing perf event file descriptors
///
/// # Returns
///
/// * `Ok(())` on success
/// * `Err(PerfEventError)` on failure
pub fn start_events(map: &MapMut) -> Result<(), PerfEventError> {
    // Determine number of CPUs from map max entries
    let n_cpu = map.info().map(|info| info.info.max_entries as i32).map_err(|e| PerfEventError::MapInfoError(e))?;

    // Iterate through each CPU's file descriptor and enable the perf event
    for cpu in 0..n_cpu {
        let key = (cpu as u32).to_le_bytes();

        if let Some(value) = map.lookup(&key, libbpf_rs::MapFlags::ANY).unwrap_or(None) {
            if value.len() >= 4 {
                // Convert bytes to file descriptor
                let fd = u32::from_le_bytes([value[0], value[1], value[2], value[3]]) as i32;

                // Enable the perf event
                let ret = unsafe {
                    libc::ioctl(fd, sys::bindings::ENABLE as libc::c_ulong, 0)
                };

                if ret < 0 {
                    return Err(PerfEventError::EnableError(io::Error::last_os_error()));
                }
            }
        }
    }

    Ok(())
}

/// Opens perf events for BPF output maps configured similarly to MmapStorage.
///
/// This helper configures and opens perf events for each CPU aimed to be used for perf rings.
///
/// # Arguments
///
/// * `map` - A mutable reference to a libbpf-rs map to store the file descriptors
/// * `n_watermark_bytes` - Number of bytes to wait before waking up. If 0, wake up on every event.
///
/// # Returns
///
/// * `Ok(())` on success
/// * `Err(PerfEventError)` on failure
///
/// # Example
///
/// ```no_run
/// use maps::perf_event;
/// use libbpf_rs::MapMut;
///
/// fn example(map: &mut MapMut) -> Result<(), perf_event::PerfEventError> {
///     // Open perf events with 2 pages and wake up on every event
///     perf_event::open_event_maps(map, 0)?;
///     
///     Ok(())
/// }
/// ```
pub fn open_event_maps(
    map: &mut MapMut,
    n_watermark_bytes: u32
) -> Result<(), PerfEventError> {
    // Configure perf event attributes
    let mut attr = sys::bindings::perf_event_attr::default();
    attr.size = std::mem::size_of::<sys::bindings::perf_event_attr>() as u32;
    attr.type_ = sys::bindings::PERF_TYPE_SOFTWARE;
    attr.config = sys::bindings::PERF_COUNT_SW_BPF_OUTPUT as u64;
    attr.sample_type = sys::bindings::PERF_SAMPLE_RAW as u64;

    // Configure watermark behavior
    if n_watermark_bytes > 0 {
        attr.set_watermark(1);
        attr.__bindgen_anon_2.wakeup_watermark = n_watermark_bytes;
    } else {
        attr.__bindgen_anon_2.wakeup_events = 1; // Wake up on every event
    }

    // Open the events
    open_events(map, &mut attr)
}
