use anyhow::{Context, Result};
use nix::sched::{sched_getaffinity, sched_getcpu, sched_setaffinity, CpuSet};
use nix::unistd::Pid;

/// Initializes and starts a synchronized timer on all available CPU cores
pub fn initialize_sync_timer(timer_init_prog: &libbpf_rs::ProgramMut) -> Result<()> {
    println!("Initializing synchronized timer on all cores...");

    // Get current thread's CPU affinity to restore it later
    let current_pid = Pid::from_raw(0); // 0 means the current thread
    let original_cpu_set =
        sched_getaffinity(current_pid).context("Failed to get current CPU affinity")?;

    // Determine the number of available CPUs and find all available CPUs in the original set
    let num_possible_cpus = libbpf_rs::num_possible_cpus().context("Failed to get CPU count")?;

    println!("Found {}  CPU cores", num_possible_cpus);

    // Track any failed initializations
    let mut failed_cores = Vec::new();

    // Initialize timer on each core sequentially
    for cpu_id in 0..num_possible_cpus {
        // Create a CPU set with just this core
        let mut cpu_set = CpuSet::new();
        cpu_set
            .set(cpu_id)
            .with_context(|| format!("Failed to set CPU {} in CpuSet", cpu_id))?;

        // Set CPU affinity for the current thread
        sched_setaffinity(current_pid, &cpu_set)
            .with_context(|| format!("Failed to set CPU affinity to core {}", cpu_id))?;

        // Verify we're running on the correct CPU
        let current_cpu = sched_getcpu().with_context(|| format!("Failed to get current CPU"))?;

        if current_cpu != cpu_id {
            println!(
                "Warning: Failed to pin to CPU {}. Currently on CPU {}",
                cpu_id, current_cpu
            );
            failed_cores.push(cpu_id);
            continue;
        }

        // Run the initialization program
        println!("Initializing timer on CPU {}", cpu_id);

        // Create empty input for the BPF program
        let mut context_in = [0u8; 16];
        let mut input = libbpf_rs::ProgramInput::default();
        input.context_in = Some(&mut context_in);

        // Run the initialization program on this core
        let output = timer_init_prog
            .test_run(input)
            .with_context(|| format!("Failed to run init program on core {}", cpu_id))?;

        // Check return value
        if output.return_value != 0 {
            println!(
                "Timer initialization failed on core {} with code {}",
                cpu_id, output.return_value
            );
            failed_cores.push(cpu_id);
        }
    }

    // Restore original CPU affinity
    sched_setaffinity(current_pid, &original_cpu_set)
        .context("Failed to restore original CPU affinity")?;

    // Check if any cores failed initialization
    if !failed_cores.is_empty() {
        return Err(anyhow::anyhow!(
            "Failed to initialize timer on cores: {:?}",
            failed_cores
        ));
    }

    println!(
        "Synchronized timer initialized on {} cores",
        num_possible_cpus
    );
    Ok(())
}
