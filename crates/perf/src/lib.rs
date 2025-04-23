//! # perf
//!
//! A Rust library for working with eBPF perf ring buffers. This crate provides
//! interfaces for interacting with Linux perf ring buffers commonly used for
//! eBPF programs.
//!

mod ring;
pub use ring::*;
