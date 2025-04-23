//! Maps utilities for eBPF programs.
//!
//! This crate provides utilities for working with eBPF maps,
//! particularly focused on perf events.

#![cfg_attr(not(test), warn(clippy::unwrap_used))]

pub mod perf_event;
pub mod perf_rings;
