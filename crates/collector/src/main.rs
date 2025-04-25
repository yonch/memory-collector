use std::cell::RefCell;
use std::rc::Rc;
use std::str;
use std::thread;
use std::time::Duration;

use anyhow::Result;
use clap::Parser;
use time::macros::format_description;
use time::OffsetDateTime;
use timeslot::MinTracker;

// Import the perf_events crate components

// Import the bpf crate components
use bpf::{
    msg_type, BpfLoader, PerfMeasurementMsg, TaskFreeMsg, TaskMetadataMsg,
    TimerFinishedProcessingMsg,
};

// Import local modules
mod metrics;
mod task_metadata;
mod timeslot_data;

// Re-export the Metric struct
pub use metrics::Metric;
use task_metadata::{TaskCollection, TaskMetadata};
use timeslot_data::TimeslotData;

/// Linux process monitoring tool
#[derive(Debug, Parser)]
struct Command {
    /// Verbose debug output
    #[arg(short, long)]
    verbose: bool,

    /// Track duration in seconds (0 = unlimited)
    #[arg(short, long, default_value = "0")]
    duration: u64,
}

// Application state containing task collection and timer tracking
struct AppState {
    min_tracker: MinTracker,
    last_min_slot: Option<u64>,
    task_collection: TaskCollection,
    current_timeslot: TimeslotData,
}

impl AppState {
    // Handle task metadata events
    fn handle_task_metadata(&mut self, _ring_index: usize, data: &[u8]) -> Result<()> {
        let event: &TaskMetadataMsg = match plain::from_bytes(data) {
            Ok(event) => event,
            Err(e) => {
                return Err(anyhow::anyhow!(
                    "Failed to parse task metadata event: {:?}",
                    e
                ));
            }
        };

        // Create task metadata and add to collection
        let metadata = TaskMetadata::new(event.pid, event.comm);
        self.task_collection.add(metadata);
        Ok(())
    }

    // Handle task free events
    fn handle_task_free(&mut self, _ring_index: usize, data: &[u8]) -> Result<()> {
        let event: &TaskFreeMsg = match plain::from_bytes(data) {
            Ok(event) => event,
            Err(e) => {
                return Err(anyhow::anyhow!("Failed to parse task free event: {:?}", e));
            }
        };

        // Queue the task for removal
        self.task_collection.queue_removal(event.pid);
        Ok(())
    }

    // Handle performance measurement events
    fn handle_perf_measurement(&mut self, _ring_index: usize, data: &[u8]) -> Result<()> {
        let event: &PerfMeasurementMsg = match plain::from_bytes(data) {
            Ok(event) => event,
            Err(e) => {
                return Err(anyhow::anyhow!(
                    "Failed to parse perf measurement event: {:?}",
                    e
                ));
            }
        };

        // Create metric from the performance measurements
        let metric = Metric::from_deltas(
            event.cycles_delta,
            event.instructions_delta,
            event.llc_misses_delta,
            event.time_delta_ns,
        );

        // Look up task metadata and update timeslot data
        let pid = event.pid;
        let metadata = self.task_collection.lookup(pid).cloned();
        self.current_timeslot.update(pid, metadata, metric);
        Ok(())
    }

    // Handle timer finished processing events
    fn handle_timer_finished_processing(&mut self, ring_index: usize, data: &[u8]) -> Result<()> {
        let event: &TimerFinishedProcessingMsg = match plain::from_bytes(data) {
            Ok(event) => event,
            Err(e) => {
                return Err(anyhow::anyhow!(
                    "Failed to parse timer finished processing event: {:?}",
                    e
                ));
            }
        };

        // Update the min tracker with the CPU ID and timestamp
        let timestamp = event.header.timestamp;

        if let Err(e) = self.min_tracker.update(ring_index, timestamp) {
            return Err(anyhow::anyhow!("Failed to update min tracker: {:?}", e));
        }

        // Check if the minimum time slot has changed
        if let Some(min_slot) = self.min_tracker.get_min() {
            if self.last_min_slot.map_or(true, |last| last != min_slot) {
                // Print out the current timeslot data before switching
                let now = format_time();
                println!(
                    "{} TIMESLOT_COMPLETE: timestamp={} task_count={}",
                    now,
                    self.current_timeslot.start_timestamp,
                    self.current_timeslot.task_count()
                );

                // Print details for each task
                for (pid, task_data) in self.current_timeslot.iter_tasks() {
                    let comm = if let Some(ref metadata) = task_data.metadata {
                        match str::from_utf8(&metadata.comm) {
                            Ok(s) => s.trim_end_matches(char::from(0)),
                            Err(_) => "<invalid utf8>",
                        }
                    } else {
                        "<unknown>"
                    };

                    // if comm is "collector" print the metrics
                    if comm == "collector" {
                        println!(
                            "  PID={:<7} COMM={:<16} cycles={:<12} instructions={:<12} llc_misses={:<8} time_ns={:<12}",
                            pid,
                            comm,
                            task_data.metrics.cycles,
                            task_data.metrics.instructions,
                            task_data.metrics.llc_misses,
                            task_data.metrics.time_ns
                        );
                    }
                }

                println!("{}", "-".repeat(60));

                // Replace the current timeslot with a new one
                self.current_timeslot = TimeslotData::new(min_slot);
                self.last_min_slot = Some(min_slot);

                // End of time slot - flush queued removals
                self.task_collection.flush_removals();
            }
        }
        Ok(())
    }

    // Handle lost events
    fn handle_lost_events(&self, ring_index: usize, _data: &[u8]) {
        eprintln!("Lost events notification on ring {}", ring_index);
    }
}

fn format_time() -> String {
    if let Ok(now) = OffsetDateTime::now_local() {
        let format = format_description!("[hour]:[minute]:[second].[subsecond digits:3]");
        now.format(&format)
            .unwrap_or_else(|_| "00:00:00.000".to_string())
    } else {
        "00:00:00.000".to_string()
    }
}

fn main() -> Result<()> {
    let opts = Command::parse();

    // Create a BPF loader with the specified verbosity
    let mut bpf_loader = BpfLoader::new(opts.verbose)?;

    // Initialize the sync timer
    bpf_loader.start_sync_timer()?;

    // Get number of CPUs using available_parallelism
    let num_cpus = thread::available_parallelism()
        .map(|p| p.get())
        .unwrap_or(1);

    // Create application state with flattened timer state
    let app_state = Rc::new(RefCell::new(AppState {
        min_tracker: MinTracker::new(1_000_000, num_cpus),
        last_min_slot: None,
        task_collection: TaskCollection::new(),
        current_timeslot: TimeslotData::new(0), // Start with timestamp 0
    }));

    {
        let dispatcher = bpf_loader.dispatcher_mut();
        // Register handlers for each message type with app state
        let app_state_clone = app_state.clone();
        dispatcher.subscribe(
            msg_type::MSG_TYPE_TASK_METADATA as u32,
            move |ring_index, data| {
                if let Err(e) = app_state_clone
                    .borrow_mut()
                    .handle_task_metadata(ring_index, data)
                {
                    eprintln!("Error handling task metadata: {:?}", e);
                }
            },
        );

        let app_state_clone = app_state.clone();
        dispatcher.subscribe(
            msg_type::MSG_TYPE_TASK_FREE as u32,
            move |ring_index, data| {
                if let Err(e) = app_state_clone
                    .borrow_mut()
                    .handle_task_free(ring_index, data)
                {
                    eprintln!("Error handling task free: {:?}", e);
                }
            },
        );

        // App state clone for the perf measurement callback
        let app_state_clone = app_state.clone();
        dispatcher.subscribe(
            msg_type::MSG_TYPE_PERF_MEASUREMENT as u32,
            move |ring_index, data| {
                if let Err(e) = app_state_clone
                    .borrow_mut()
                    .handle_perf_measurement(ring_index, data)
                {
                    eprintln!("Error handling perf measurement: {:?}", e);
                }
            },
        );

        // App state clone for the timer callback
        let app_state_clone = app_state.clone();
        dispatcher.subscribe(
            msg_type::MSG_TYPE_TIMER_FINISHED_PROCESSING as u32,
            move |ring_index, data| {
                if let Err(e) = app_state_clone
                    .borrow_mut()
                    .handle_timer_finished_processing(ring_index, data)
                {
                    eprintln!("Error handling timer finished: {:?}", e);
                }
            },
        );

        let app_state_clone = app_state.clone();
        dispatcher.subscribe_lost_samples(move |ring_index, data| {
            app_state_clone
                .borrow()
                .handle_lost_events(ring_index, data);
        });
    }

    // Attach BPF programs
    bpf_loader.attach()?;

    println!("Successfully started! Tracing and aggregating task performance...");
    println!("Metrics will be reported at the end of each timeslot.");
    println!("{}", "-".repeat(60));

    // Process events
    let duration = Duration::from_secs(opts.duration);
    let start_time = std::time::Instant::now();

    // Run for the specified duration
    while opts.duration <= 0 || start_time.elapsed() < duration {
        // Poll for events with a 10ms timeout
        bpf_loader.poll_events(10)?;
    }

    Ok(())
}
