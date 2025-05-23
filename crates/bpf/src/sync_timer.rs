use anyhow::Result;
use log::{debug, error, warn};
use nix::sched::{sched_getaffinity, sched_getcpu, sched_setaffinity, CpuSet};
use nix::unistd::Pid;
use thiserror::Error;

// Import the auto-generated enum from BPF skeleton
use crate::bpf::types::sync_timer_init_error;

/// Errors that can occur during sync timer initialization
#[derive(Error, Debug)]
pub enum SyncTimerError {
    #[error("Failed to get current CPU affinity")]
    AffinityGetFailed(#[source] nix::Error),

    #[error("Failed to get CPU count")]
    CpuCountFailed(#[source] libbpf_rs::Error),

    #[error("Failed to set CPU {} in CpuSet", cpu)]
    CpuSetFailed { cpu: usize },

    #[error("Failed to set CPU affinity to core {}", cpu)]
    AffinitySetFailed {
        cpu: usize,
        #[source]
        source: nix::Error,
    },

    #[error("Failed to get current CPU")]
    CurrentCpuFailed(#[source] nix::Error),

    #[error("Failed to run BPF init program on core {}", cpu)]
    BpfProgramFailed {
        cpu: usize,
        #[source]
        source: libbpf_rs::Error,
    },

    #[error("BPF map update failed on core {}", cpu)]
    MapUpdateFailed { cpu: usize },

    #[error("BPF map lookup failed after insertion on core {}", cpu)]
    MapLookupFailed { cpu: usize },

    #[error("BPF timer initialization failed on core {} (may indicate EINVAL: invalid parameters, or EOPNOTSUPP: operation not supported - CPU pinning for BPF timers requires Linux kernel 6.7 or later)", cpu)]
    TimerInitFailed { cpu: usize },

    #[error("BPF timer callback setup failed on core {} (may indicate EINVAL: invalid parameters, or EOPNOTSUPP: operation not supported - BPF timer functionality requires Linux kernel 6.7 or later)", cpu)]
    TimerSetCallbackFailed { cpu: usize },

    #[error("BPF timer start failed on core {} (may indicate EINVAL: invalid parameters, or EOPNOTSUPP: operation not supported - CPU pinning for BPF timers requires Linux kernel 6.7 or later)", cpu)]
    TimerStartFailed { cpu: usize },

    #[error("Unknown BPF error code {} on core {}", code, cpu)]
    UnknownBpfError { cpu: usize, code: u32 },

    #[error("Failed to pin to CPU {}. Currently on CPU {}", target, current)]
    CpuPinFailed { target: usize, current: usize },

    #[error("Failed to restore original CPU affinity")]
    AffinityRestoreFailed(#[source] nix::Error),

    #[error("Failed to initialize timer on {} out of {} cores: {:?}. This may indicate hardware/kernel limitations with BPF timers, insufficient permissions, or platform incompatibility. Note: CPU pinning for BPF timers requires Linux kernel 6.7 or later.", failed_count, total_count, failed_cores)]
    MultipleFailures {
        failed_cores: Vec<usize>,
        failed_count: usize,
        total_count: usize,
    },
}

/// Initializes and starts a synchronized timer on all available CPU cores
///
/// This function initializes BPF timers on every available CPU core, ensuring
/// synchronized timer firing across all cores. It provides detailed error
/// information when any step of the initialization process fails.
///
/// # Errors
///
/// Returns `SyncTimerError` with specific details about what failed:
/// - CPU affinity operations
/// - BPF program execution
/// - BPF timer setup (init, callback, start)
/// - BPF map operations
///
/// # Example
///
/// ```rust,no_run
/// use bpf::{BpfLoader, sync_timer::SyncTimerError};
/// use log::{error, info};
///
/// let mut loader = BpfLoader::new(false)?;
///
/// match loader.start_sync_timer() {
///     Ok(()) => info!("Sync timer initialized successfully"),
///     Err(e) => {
///         error!("Sync timer initialization failed: {}", e);
///         
///         // Check if it's specifically a sync timer error with kernel requirements
///         if let Some(sync_error) = e.downcast_ref::<SyncTimerError>() {
///             match sync_error {
///                 SyncTimerError::TimerInitFailed { cpu } => {
///                     error!("Timer init failed on CPU {}", cpu);
///                     error!("This may indicate your kernel doesn't support BPF timer CPU pinning.");
///                     error!("Linux kernel 6.7 or later is required for BPF timer functionality.");
///                     error!("Current kernel: {}", std::process::Command::new("uname")
///                         .arg("-r").output().map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
///                         .unwrap_or_else(|_| "unknown".to_string()));
///                 },
///                 SyncTimerError::TimerStartFailed { cpu } => {
///                     error!("Timer start failed on CPU {}", cpu);
///                     error!("BPF timer CPU pinning requires kernel 6.7+");
///                 },
///                 SyncTimerError::MultipleFailures { failed_cores, .. } => {
///                     error!("Timer initialization failed on multiple cores: {:?}", failed_cores);
///                     error!("This is likely due to insufficient kernel support.");
///                     error!("Please ensure you're running Linux kernel 6.7 or later.");
///                 },
///                 _ => error!("Other sync timer error: {}", sync_error),
///             }
///         }
///         
///         std::process::exit(1);
///     }
/// }
/// ```
pub fn initialize_sync_timer(
    timer_init_prog: &libbpf_rs::ProgramMut,
) -> Result<(), SyncTimerError> {
    debug!("Initializing synchronized timer on all cores...");

    // Get current thread's CPU affinity to restore it later
    let current_pid = Pid::from_raw(0); // 0 means the current thread
    let original_cpu_set =
        sched_getaffinity(current_pid).map_err(SyncTimerError::AffinityGetFailed)?;

    // Determine the number of available CPUs
    let num_possible_cpus =
        libbpf_rs::num_possible_cpus().map_err(SyncTimerError::CpuCountFailed)?;

    debug!("Found {} CPU cores", num_possible_cpus);

    // Track any failed initializations
    let mut failed_cores = Vec::new();

    // Initialize timer on each core sequentially
    for cpu_id in 0..num_possible_cpus {
        if let Err(e) = initialize_timer_on_core(timer_init_prog, cpu_id, current_pid) {
            error!("Timer initialization failed on core {}: {}", cpu_id, e);
            failed_cores.push(cpu_id);
        } else {
            debug!("Timer initialization succeeded on core {}", cpu_id);
        }
    }

    // Restore original CPU affinity
    sched_setaffinity(current_pid, &original_cpu_set)
        .map_err(SyncTimerError::AffinityRestoreFailed)?;

    // Check if any cores failed initialization
    if !failed_cores.is_empty() {
        return Err(SyncTimerError::MultipleFailures {
            failed_cores: failed_cores.clone(),
            failed_count: failed_cores.len(),
            total_count: num_possible_cpus,
        });
    }

    debug!(
        "Synchronized timer initialized on {} cores",
        num_possible_cpus
    );
    Ok(())
}

/// Initialize timer on a specific CPU core
fn initialize_timer_on_core(
    timer_init_prog: &libbpf_rs::ProgramMut,
    cpu_id: usize,
    current_pid: Pid,
) -> Result<(), SyncTimerError> {
    // Create a CPU set with just this core
    let mut cpu_set = CpuSet::new();
    cpu_set
        .set(cpu_id)
        .map_err(|_| SyncTimerError::CpuSetFailed { cpu: cpu_id })?;

    // Set CPU affinity for the current thread
    sched_setaffinity(current_pid, &cpu_set).map_err(|e| SyncTimerError::AffinitySetFailed {
        cpu: cpu_id,
        source: e,
    })?;

    // Verify we're running on the correct CPU
    let current_cpu = sched_getcpu().map_err(SyncTimerError::CurrentCpuFailed)?;

    if current_cpu as usize != cpu_id {
        warn!(
            "Failed to pin to CPU {}. Currently on CPU {}",
            cpu_id, current_cpu
        );
        return Err(SyncTimerError::CpuPinFailed {
            target: cpu_id,
            current: current_cpu as usize,
        });
    }

    debug!("Initializing timer on CPU {}", cpu_id);

    // Create empty input for the BPF program
    let mut context_in = [0u8; 16];
    let mut input = libbpf_rs::ProgramInput::default();
    input.context_in = Some(&mut context_in);

    // Run the initialization program on this core
    let output = timer_init_prog
        .test_run(input)
        .map_err(|e| SyncTimerError::BpfProgramFailed {
            cpu: cpu_id,
            source: e,
        })?;

    // Check return value using the auto-generated enum
    if output.return_value != sync_timer_init_error::SYNC_TIMER_SUCCESS as u32 {
        return match output.return_value {
            v if v == sync_timer_init_error::SYNC_TIMER_MAP_UPDATE_FAILED as u32 => {
                Err(SyncTimerError::MapUpdateFailed { cpu: cpu_id })
            }
            v if v == sync_timer_init_error::SYNC_TIMER_MAP_LOOKUP_FAILED as u32 => {
                Err(SyncTimerError::MapLookupFailed { cpu: cpu_id })
            }
            v if v == sync_timer_init_error::SYNC_TIMER_TIMER_INIT_FAILED as u32 => {
                Err(SyncTimerError::TimerInitFailed { cpu: cpu_id })
            }
            v if v == sync_timer_init_error::SYNC_TIMER_TIMER_SET_CALLBACK_FAILED as u32 => {
                Err(SyncTimerError::TimerSetCallbackFailed { cpu: cpu_id })
            }
            v if v == sync_timer_init_error::SYNC_TIMER_TIMER_START_FAILED as u32 => {
                Err(SyncTimerError::TimerStartFailed { cpu: cpu_id })
            }
            unknown => Err(SyncTimerError::UnknownBpfError {
                cpu: cpu_id,
                code: unknown,
            }),
        };
    }

    Ok(())
}
