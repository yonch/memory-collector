use std::mem::MaybeUninit;
use std::str;
use std::rc::Rc;
use std::cell::RefCell;
use std::thread;
use std::time::Duration;

use anyhow::Result;
use clap::Parser;
use libbpf_rs::skel::OpenSkel;
use libbpf_rs::skel::Skel;
use libbpf_rs::skel::SkelBuilder;
use plain::Plain;
use time::macros::format_description;
use time::OffsetDateTime;
use timeslot::MinTracker;

// Import the perf_events crate components
use perf_events::{Dispatcher, HardwareCounter, PerfMapReader};

// Import our sync_timer module
mod sync_timer;
mod metrics;
mod task_metadata;
mod timeslot_data;

mod collector {
    include!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/src/bpf/collector.skel.rs"
    ));
}

use collector::*;
// Re-export the Metric struct
pub use metrics::Metric;
use task_metadata::{TaskCollection, TaskMetadata};
use timeslot_data::TimeslotData;

unsafe impl Plain for collector::types::task_metadata_msg {}
unsafe impl Plain for collector::types::task_free_msg {}
unsafe impl Plain for collector::types::timer_finished_processing_msg {}
unsafe impl Plain for collector::types::perf_measurement_msg {}

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

fn format_time() -> String {
    if let Ok(now) = OffsetDateTime::now_local() {
        let format = format_description!("[hour]:[minute]:[second].[subsecond digits:3]");
        now.format(&format)
            .unwrap_or_else(|_| "00:00:00.000".to_string())
    } else {
        "00:00:00.000".to_string()
    }
}

// Handle task metadata events
fn handle_task_metadata(_ring_index: usize, data: &[u8], app_state: &Rc<RefCell<AppState>>) {
    let event: &collector::types::task_metadata_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse task metadata event: {:?}", e);
            return;
        }
    };

    // Create task metadata and add to collection
    let metadata = TaskMetadata::new(event.pid, event.comm);
    let mut state = app_state.borrow_mut();
    state.task_collection.add(metadata);
}

// Handle task free events
fn handle_task_free(_ring_index: usize, data: &[u8], app_state: &Rc<RefCell<AppState>>) {
    let event: &collector::types::task_free_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse task free event: {:?}", e);
            return;
        }
    };
    
    // Queue the task for removal
    let mut state = app_state.borrow_mut();
    state.task_collection.queue_removal(event.pid);
}

// Handle performance measurement events
fn handle_perf_measurement(_ring_index: usize, data: &[u8], app_state: &Rc<RefCell<AppState>>) {
    let event: &collector::types::perf_measurement_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse perf measurement event: {:?}", e);
            return;
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
    let mut state = app_state.borrow_mut();
    let pid = event.pid;
    
    let metadata = state.task_collection.lookup(pid).cloned();
    state.current_timeslot.update(pid, metadata, metric);
}

// Handle timer finished processing events
fn handle_timer_finished_processing(
    ring_index: usize,
    data: &[u8],
    app_state: &Rc<RefCell<AppState>>,
) {
    let event: &collector::types::timer_finished_processing_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse timer finished processing event: {:?}", e);
            return;
        }
    };

    // Update the min tracker with the CPU ID and timestamp
    let timestamp = event.header.timestamp;
    let mut state = app_state.borrow_mut();

    if let Err(e) = state.min_tracker.update(ring_index, timestamp) {
        eprintln!("Failed to update min tracker: {:?}", e);
        return;
    }

    // Check if the minimum time slot has changed
    if let Some(min_slot) = state.min_tracker.get_min() {
        if state.last_min_slot.map_or(true, |last| last != min_slot) {
            // Print out the current timeslot data before switching
            let now = format_time();
            println!(
                "{} TIMESLOT_COMPLETE: timestamp={} task_count={}",
                now,
                state.current_timeslot.start_timestamp,
                state.current_timeslot.task_count()
            );
            
            // Print details for each task
            for (pid, task_data) in state.current_timeslot.iter_tasks() {
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
            
            // Create a new timeslot with the current minimum timestamp
            state.current_timeslot = TimeslotData::new(min_slot);
            state.last_min_slot = Some(min_slot);
            
            // End of time slot - flush queued removals
            state.task_collection.flush_removals();
        }
    }
}

// Handle lost events
fn handle_lost_events(ring_index: usize, _data: &[u8]) {
    eprintln!("Lost events notification on ring {}", ring_index);
}

fn main() -> Result<()> {
    let opts = Command::parse();

    // Allow the current process to lock memory for eBPF resources
    let _ = libbpf_rs::set_print(None);

    // Open BPF program
    let mut skel_builder = CollectorSkelBuilder::default();
    if opts.verbose {
        skel_builder.obj_builder.debug(true);
    }

    let mut open_object = MaybeUninit::uninit();
    let open_skel = skel_builder.open(&mut open_object)?;

    // Load & verify program
    let mut skel = open_skel.load()?;

    // Initialize perf event rings for the hardware counters
    if let Err(e) = perf_events::open_perf_counter(&mut skel.maps.cycles, HardwareCounter::Cycles) {
        return Err(anyhow::anyhow!("Failed to open cycles counter: {:?}", e));
    }

    if let Err(e) =
        perf_events::open_perf_counter(&mut skel.maps.instructions, HardwareCounter::Instructions)
    {
        return Err(anyhow::anyhow!(
            "Failed to open instructions counter: {:?}",
            e
        ));
    }

    if let Err(e) =
        perf_events::open_perf_counter(&mut skel.maps.llc_misses, HardwareCounter::LLCMisses)
    {
        return Err(anyhow::anyhow!(
            "Failed to open LLC misses counter: {:?}",
            e
        ));
    }

    // Initialize the sync timer
    sync_timer::initialize_sync_timer(&skel.progs.sync_timer_init_collect)?;

    // Attach the tracepoints
    skel.attach()?;

    println!("Successfully started! Tracing and aggregating task performance...");
    println!("Metrics will be reported at the end of each timeslot.");
    println!("{}", "-".repeat(60));

    // Set up the perf map reader for the events map
    let buffer_pages = 32;
    let watermark_bytes = 0; // Wake up on every event
    let mut perf_map_reader =
        PerfMapReader::new(&mut skel.maps.events, buffer_pages, watermark_bytes)
            .map_err(|e| anyhow::anyhow!("Failed to create PerfMapReader: {}", e))?;

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

    // Create a dispatcher to handle events
    let mut dispatcher = Dispatcher::new();

    // Register handlers for each message type with app state
    let app_state_clone = app_state.clone();
    dispatcher.subscribe(
        types::msg_type::MSG_TYPE_TASK_METADATA as u32,
        move |ring_index, data| {
            handle_task_metadata(ring_index, data, &app_state_clone)
        },
    );

    let app_state_clone = app_state.clone();
    dispatcher.subscribe(
        types::msg_type::MSG_TYPE_TASK_FREE as u32,
        move |ring_index, data| {
            handle_task_free(ring_index, data, &app_state_clone)
        },
    );

    // App state clone for the perf measurement callback
    let app_state_clone = app_state.clone();
    dispatcher.subscribe(
        types::msg_type::MSG_TYPE_PERF_MEASUREMENT as u32,
        move |ring_index, data| {
            handle_perf_measurement(ring_index, data, &app_state_clone)
        },
    );

    // App state clone for the timer callback
    let app_state_clone = app_state.clone();
    dispatcher.subscribe(
        types::msg_type::MSG_TYPE_TIMER_FINISHED_PROCESSING as u32,
        move |ring_index, data| {
            handle_timer_finished_processing(ring_index, data, &app_state_clone)
        },
    );

    dispatcher.subscribe_lost_samples(handle_lost_events);

    // Get the reader from the map reader
    let reader = perf_map_reader.reader_mut();

    // Process events
    let duration = Duration::from_secs(opts.duration);
    let start_time = std::time::Instant::now();

    // Run for the specified duration
    while opts.duration <= 0 || start_time.elapsed() < duration {
        // Start a read batch
        reader.start()?;

        // Dispatch all available events
        dispatcher.dispatch_all(reader)?;

        // Finish the read batch
        reader.finish()?;

        // Short sleep to avoid busy-waiting
        std::thread::sleep(Duration::from_millis(10));
    }

    Ok(())
}
