use std::mem::MaybeUninit;
use std::str;
use std::time::Duration;
use std::sync::{Arc, Mutex};
use std::thread;

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
use perf_events::{Dispatcher, PerfMapReader};

// Import our sync_timer module
mod sync_timer;

mod collector {
    include!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/src/bpf/collector.skel.rs"
    ));
}

use collector::*;

unsafe impl Plain for collector::types::task_metadata_msg {}
unsafe impl Plain for collector::types::task_free_msg {}
unsafe impl Plain for collector::types::timer_finished_processing_msg {}

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

// Global state for the timer tracking
struct TimerState {
    min_tracker: MinTracker,
    last_min_slot: Option<u64>,
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
fn handle_task_metadata(_ring_index: usize, data: &[u8]) {
    let event: &collector::types::task_metadata_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse task metadata event: {:?}", e);
            return;
        }
    };

    let comm = match str::from_utf8(&event.comm) {
        Ok(s) => s.trim_end_matches(char::from(0)),
        Err(_) => "<invalid utf8>",
    };

    let now = format_time();
    println!("{} TASK_NEW: pid={:<7} comm={:<16}", now, event.pid, comm);
}

// Handle task free events
fn handle_task_free(_ring_index: usize, data: &[u8]) {
    let event: &collector::types::task_free_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse task free event: {:?}", e);
            return;
        }
    };

    let now = format_time();
    println!("{} TASK_EXIT: pid={:<7}", now, event.pid);
}

// Handle timer finished processing events
fn handle_timer_finished_processing(ring_index: usize, data: &[u8], timer_state: &Arc<Mutex<TimerState>>) {
    let event: &collector::types::timer_finished_processing_msg = match plain::from_bytes(data) {
        Ok(event) => event,
        Err(e) => {
            eprintln!("Failed to parse timer finished processing event: {:?}", e);
            return;
        }
    };

    // Update the min tracker with the CPU ID and timestamp
    let timestamp = event.header.timestamp;
    let mut state = timer_state.lock().unwrap();
    
    if let Err(e) = state.min_tracker.update(ring_index, timestamp) {
        eprintln!("Failed to update min tracker: {:?}", e);
        return;
    }
    
    // Check if the minimum time slot has changed
    if let Some(min_slot) = state.min_tracker.get_min() {
        if state.last_min_slot.map_or(true, |last| last != min_slot) {
            let now = format_time();
            println!(
                "{} MIN_TIMESLOT: All CPUs have processed up to time slot {}, timestamp {}",
                now, min_slot / 100_000_000, min_slot
            );
            state.last_min_slot = Some(min_slot);
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

    // Initialize the sync timer
    sync_timer::initialize_sync_timer(&skel.progs.sync_timer_init_collect)?;

    // Attach the tracepoints
    skel.attach()?;

    println!("Successfully started! Tracing task lifecycle events...");
    println!("{:<23} {:<9} {:<16}", "TIME", "EVENT", "DETAILS");
    println!("{}", "-".repeat(60));

    // Set up the perf map reader for the events map
    let buffer_pages = 2;
    let watermark_bytes = 0; // Wake up on every event
    let mut perf_map_reader =
        PerfMapReader::new(&mut skel.maps.events, buffer_pages, watermark_bytes)
            .map_err(|e| anyhow::anyhow!("Failed to create PerfMapReader: {}", e))?;

    // Get number of CPUs using available_parallelism
    let num_cpus = thread::available_parallelism()
        .map(|p| p.get())
        .unwrap_or(1);
    
    // Create the timer state with MinTracker
    // Use 100ms time slots (convert to nanoseconds)
    let timer_state = Arc::new(Mutex::new(TimerState {
        min_tracker: MinTracker::new(100_000_000, num_cpus),
        last_min_slot: None,
    }));

    // Create a dispatcher to handle events
    let mut dispatcher = Dispatcher::new();

    // Register handlers for each message type
    dispatcher.subscribe(
        types::msg_type::MSG_TYPE_TASK_METADATA as u32,
        handle_task_metadata,
    );
    dispatcher.subscribe(types::msg_type::MSG_TYPE_TASK_FREE as u32, handle_task_free);
    
    // Timer state clone for the callback
    let timer_state_clone = timer_state.clone();
    dispatcher.subscribe(
        types::msg_type::MSG_TYPE_TIMER_FINISHED_PROCESSING as u32,
        move |ring_index, data| handle_timer_finished_processing(ring_index, data, &timer_state_clone),
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
