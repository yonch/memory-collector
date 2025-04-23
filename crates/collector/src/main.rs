use std::mem::MaybeUninit;
use std::time::Duration;
use std::str;

use anyhow::Result;
use clap::Parser;
use libbpf_rs::skel::OpenSkel;
use libbpf_rs::skel::Skel;
use libbpf_rs::skel::SkelBuilder;
use libbpf_rs::PerfBufferBuilder;
use plain::Plain;
use time::macros::format_description;
use time::OffsetDateTime;

mod collector {
    include!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/src/bpf/collector.skel.rs"
    ));
}

use collector::*;

// Event message types from BPF
#[repr(u32)]
#[derive(Debug, Copy, Clone, PartialEq)]
enum MsgType {
    TaskMetadata = 1,
    TaskFree = 2,
}

impl From<u32> for MsgType {
    fn from(value: u32) -> Self {
        match value {
            1 => MsgType::TaskMetadata,
            2 => MsgType::TaskFree,
            _ => panic!("Unknown message type: {}", value),
        }
    }
}

// Task metadata message from BPF
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
struct TaskMetadataMsg {
    timestamp: u64,
    msg_type: u32,
    pid: u32,
    comm: [u8; 16],
}

// Task free message from BPF
#[repr(C)]
#[derive(Debug, Default, Copy, Clone)]
struct TaskFreeMsg {
    timestamp: u64,
    msg_type: u32,
    pid: u32,
}

unsafe impl Plain for TaskMetadataMsg {}
unsafe impl Plain for TaskFreeMsg {}

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

fn format_time() -> String {
    if let Ok(now) = OffsetDateTime::now_local() {
        let format = format_description!("[hour]:[minute]:[second].[subsecond digits:3]");
        now.format(&format)
            .unwrap_or_else(|_| "00:00:00.000".to_string())
    } else {
        "00:00:00.000".to_string()
    }
}

fn handle_event(_cpu: i32, data: &[u8]) {
    // First determine the event type (read first 8 bytes for timestamp, then 4 bytes for type)
    if data.len() < 12 {
        eprintln!("Event data too short");
        return;
    }
    
    let mut type_bytes = [0u8; 4];
    type_bytes.copy_from_slice(&data[8..12]);
    let msg_type = u32::from_ne_bytes(type_bytes);
    
    let now = format_time();
    
    match MsgType::from(msg_type) {
        MsgType::TaskMetadata => {
            let mut event = TaskMetadataMsg::default();
            if let Err(e) = plain::copy_from_bytes(&mut event, data) {
                eprintln!("Failed to parse task metadata event: {:?}", e);
                return;
            }
            
            let comm = match str::from_utf8(&event.comm) {
                Ok(s) => s.trim_end_matches(char::from(0)),
                Err(_) => "<invalid utf8>",
            };
            
            println!(
                "{} TASK_NEW: pid={:<7} comm={:<16}",
                now, event.pid, comm
            );
        },
        MsgType::TaskFree => {
            let mut event = TaskFreeMsg::default();
            if let Err(e) = plain::copy_from_bytes(&mut event, data) {
                eprintln!("Failed to parse task free event: {:?}", e);
                return;
            }
            
            println!(
                "{} TASK_EXIT: pid={:<7}",
                now, event.pid
            );
        }
    }
}

fn handle_lost_events(cpu: i32, count: u64) {
    eprintln!("Lost {} events on CPU {}", count, cpu);
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
    
    // Attach the tracepoints
    skel.attach()?;
    
    println!("Successfully started! Tracing task lifecycle events...");
    println!("{:<23} {:<9} {:<16}", "TIME", "EVENT", "DETAILS");
    println!("{}", "-".repeat(60));

    // Set up the perf buffer to read events from the kernel
    let perf = PerfBufferBuilder::new(&skel.maps.events)
        .sample_cb(handle_event)
        .lost_cb(handle_lost_events)
        .build()?;

    // Process events
    let poll_timeout = Duration::from_millis(100);
    
    if opts.duration > 0 {
        let duration = Duration::from_secs(opts.duration);
        let start_time = std::time::Instant::now();
        
        // Run for the specified duration
        while start_time.elapsed() < duration {
            if let Err(e) = perf.poll(poll_timeout) {
                eprintln!("Error polling perf buffer: {}", e);
                break;
            }
        }
    } else {
        // Run indefinitely
        loop {
            if let Err(e) = perf.poll(poll_timeout) {
                eprintln!("Error polling perf buffer: {}", e);
                break;
            }
        }
    }

    Ok(())
}
