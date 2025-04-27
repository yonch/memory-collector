//! # Timeslot
//!
//! A crate for tracking and synchronizing time slots across multiple CPU cores.
//!
//! This crate is designed to help systems that need to know when all CPU cores have
//! completed processing a given time slot, making it safe to process and emit metrics
//! for that time slot.
//!
//! The primary interface is through the [`MinTracker`] struct, which tracks the minimum
//! time slot that all CPU cores have reported as complete.

pub mod min_tracker;

pub use min_tracker::*;
