use anyhow::{anyhow, Result};
use libbpf_rs::skel::{OpenSkel, Skel, SkelBuilder};
use libbpf_rs::OpenObject;
use perf_events::{Dispatcher, HardwareCounter, PerfMapReader};
use std::mem::MaybeUninit;
use std::time::Duration;

pub mod sync_timer;

// Include the generated skeletons
mod bpf {
    include!("bpf/collector.skel.rs");
}

#[cfg(test)]
mod test_bpf {
    include!("bpf/cgroup_inode_test.skel.rs");
}

// Re-export the specific types we need
pub use bpf::types::{
    msg_type, perf_measurement_msg as PerfMeasurementMsg, task_free_msg as TaskFreeMsg,
    task_metadata_msg as TaskMetadataMsg,
    timer_finished_processing_msg as TimerFinishedProcessingMsg,
    timer_migration_msg as TimerMigrationMsg,
};

// Implement Plain for message types
unsafe impl plain::Plain for TaskMetadataMsg {}
unsafe impl plain::Plain for TaskFreeMsg {}
unsafe impl plain::Plain for TimerFinishedProcessingMsg {}
unsafe impl plain::Plain for PerfMeasurementMsg {}
unsafe impl plain::Plain for TimerMigrationMsg {}

// Re-export important sync timer types
pub use sync_timer::SyncTimerError;

/// The BPF dispatcher to manage BPF program lifecycle
pub struct BpfLoader {
    skel: bpf::CollectorSkel<'static>,
    dispatcher: Dispatcher,
    perf_map_reader: PerfMapReader,
}

impl BpfLoader {
    /// Create a new BPF loader with initialized skeleton
    pub fn new(verbose: bool) -> Result<Self> {
        // Allow the current process to lock memory for eBPF resources
        let _ = libbpf_rs::set_print(None);

        // Open BPF program
        let mut skel_builder = bpf::CollectorSkelBuilder::default();
        if verbose {
            skel_builder.obj_builder.debug(true);
        }

        // Create and leak the storage to give it a 'static lifetime
        // This is a controlled memory leak, but it's acceptable because:
        // 1. It happens once per program run
        // 2. It's needed to make the lifetime mechanics work properly
        // 3. The memory will be reclaimed when the program exits
        let obj_ref = Box::leak(Box::new(MaybeUninit::<OpenObject>::uninit()));

        // Open and load the skeleton with 'static lifetime
        let open_skel = skel_builder.open(obj_ref)?;
        let mut skel = open_skel.load()?;

        // Initialize perf event rings for the hardware counters
        if let Err(e) =
            perf_events::open_perf_counter(&mut skel.maps.cycles, HardwareCounter::Cycles)
        {
            return Err(anyhow!("Failed to open cycles counter: {:?}", e));
        }

        if let Err(e) = perf_events::open_perf_counter(
            &mut skel.maps.instructions,
            HardwareCounter::Instructions,
        ) {
            return Err(anyhow!("Failed to open instructions counter: {:?}", e));
        }

        if let Err(e) =
            perf_events::open_perf_counter(&mut skel.maps.llc_misses, HardwareCounter::LLCMisses)
        {
            return Err(anyhow!("Failed to open LLC misses counter: {:?}", e));
        }

        // Set up the perf map reader for the events map
        let buffer_pages = 32;
        let watermark_bytes = 0; // Wake up on every event
        let perf_map_reader =
            PerfMapReader::new(&mut skel.maps.events, buffer_pages, watermark_bytes)
                .map_err(|e| anyhow!("Failed to create PerfMapReader: {}", e))?;

        // Create a dispatcher to handle events
        let dispatcher = Dispatcher::new();

        Ok(Self {
            skel,
            dispatcher,
            perf_map_reader,
        })
    }

    /// Get a reference to the perf events dispatcher
    pub fn dispatcher(&self) -> &Dispatcher {
        &self.dispatcher
    }

    /// Get a mutable reference to the perf events dispatcher
    pub fn dispatcher_mut(&mut self) -> &mut Dispatcher {
        &mut self.dispatcher
    }

    /// Initialize and start the sync timer
    pub fn start_sync_timer(&mut self) -> Result<()> {
        sync_timer::initialize_sync_timer(
            &self.skel.progs.sync_timer_init_collect,
            &self.skel.progs.sync_timer_init_legacy_collect,
        )
        .map_err(|e| anyhow::anyhow!("Sync timer initialization failed: {}", e))
    }

    /// Attach BPF programs
    pub fn attach(&mut self) -> Result<()> {
        // Attach all BPF programs
        self.skel.attach()?;

        Ok(())
    }

    /// Poll the ring buffer for events
    pub fn poll_events(&mut self, timeout_ms: u64) -> Result<()> {
        // Get the reader from the map reader
        let reader_mut = self.perf_map_reader.reader_mut();

        // Start a read batch
        reader_mut.start()?;

        // Dispatch all available events
        self.dispatcher.dispatch_all(reader_mut)?;

        // Finish the read batch
        reader_mut.finish()?;

        // Short sleep to avoid busy-waiting if requested
        if timeout_ms > 0 {
            std::thread::sleep(Duration::from_millis(timeout_ms));
        }

        Ok(())
    }

    /// Get a reference to the BPF skeleton
    pub fn skel(&self) -> &bpf::CollectorSkel<'static> {
        &self.skel
    }

    /// Get a mutable reference to the BPF skeleton
    pub fn skel_mut(&mut self) -> &mut bpf::CollectorSkel<'static> {
        &mut self.skel
    }
}
