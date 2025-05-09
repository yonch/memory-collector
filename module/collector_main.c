#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/irq.h>
#include <linux/tracepoint.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include "rdt.h"
#include <linux/workqueue.h>
#include "procfs.h"
#include "rmid_allocator.h"
#include "sync_timer.h"
#include "collector.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Memory subsystem monitoring for Kubernetes");
MODULE_VERSION("1.0");

// Define the tracepoint
#define CREATE_TRACE_POINTS
#include "tracepoints.h"

#define EMULATED_MAX_RMID 512
#define CLOSID_CATCHALL 0

// Global RMID allocator and lock
static struct rmid_alloc rmid_allocator;
static spinlock_t rmid_lock;

// Forward declarations of new functions
static void assign_rmids_to_leaders(void);
static void propagate_leader_rmids(void);
static void reset_cpu_rmid(void *info);
static int detect_and_init_rmid_allocator(void);
static void rdt_timer_tick(struct rdt_state *rdt_state);
static void dump_existing_rmids(void);

// Global procfs data
static struct procfs_data collector_procfs = {
    .name = "unvariance_collector",
    .dump_callback = dump_existing_rmids,
};

struct cpu_state {
    struct rdt_state rdt_state;
};

static struct cpu_state __percpu *cpu_states;
static struct sync_timer collector_timer;

// Forward declare timer callback
static enum hrtimer_restart timer_fn(struct hrtimer *timer);

// Add global cpu_works declaration after other global variables
static struct work_struct __percpu *cpu_works;
static struct workqueue_struct *collector_wq;

static bool rdt_hardware_support = false;

/*
 * Assign RMID to a task
 * If task is a thread group leader, allocates a new RMID if needed
 * If task is a thread, copies RMID from group leader
 */
static void assign_rmid_to_task(struct task_struct *task)
{
    struct task_struct *group_leader;
    u32 rmid;
    unsigned long flags;

    if (!task)
        return;

    group_leader = task->group_leader;
    if (!group_leader)
        return;

    // If this is not the group leader, just copy the leader's RMID
    if (task != group_leader) {
        task->rmid = group_leader->rmid;
        return;
    }

    // First check without lock
    if (group_leader->rmid)
        return;  // Leader already has an RMID

    // We do not assign RMIDs to kernel threads
    if ((group_leader->mm == NULL) || (group_leader->flags & PF_KTHREAD))
        return;

    // No RMID assigned to leader, need to allocate one
    spin_lock_irqsave(&rmid_lock, flags);

    // Recheck after acquiring lock
    if (!group_leader->rmid) {
        // Allocate new RMID for the process
        rmid = rmid_alloc(&rmid_allocator, group_leader->comm, group_leader->tgid, ktime_get_ns());
        group_leader->rmid = rmid;
        // Note: if allocation fails, leader->rmid remains 0
    }

    spin_unlock_irqrestore(&rmid_lock, flags);
}

/*
 * Dump information about all allocated RMIDs
 * Called from procfs write handler
 */
void dump_existing_rmids(void)
{
    unsigned long flags;
    u32 i;
    struct rmid_info *info;

    for (i = 1; i <= rmid_allocator.max_rmid; i++) {
        // Lock for each element to avoid starving the write path
        spin_lock_irqsave(&rmid_lock, flags);
        
        info = rmid_get_info(&rmid_allocator, i);
        if (info && rmid_is_allocated(&rmid_allocator, i)) {
            trace_rmid_existing(
                info->rmid,
                info->comm,
                info->tgid,
                ktime_get_ns()
            );
        }
        
        spin_unlock_irqrestore(&rmid_lock, flags);
    }
}

/*
 * Read memory bandwidth counter for given RMID and output to trace
 */
static void rdt_timer_tick(struct rdt_state *rdt_state)
{
    int cpu = smp_processor_id();
    u64 now = ktime_get_ns();
    int llc_occupancy_err = 0;
    u64 llc_occupancy_val = 0;
    int mbm_total_err = 0;
    u64 mbm_total_val = 0;
    int mbm_local_err = 0;
    u64 mbm_local_val = 0;

    // for now, just output the first 4 RMID, on CPUs 0..3
    if (cpu > 4) {
        return;
    }

    // if we support cache, read it on this CPU
    if (rdt_state->supports_llc_occupancy) {
        llc_occupancy_err = rdt_read_llc_occupancy(cpu, &llc_occupancy_val);
    } else {
        llc_occupancy_err = -ENODEV;
    }

    // if we support mbm, read it on this CPU
    if (rdt_state->supports_mbm_total) {
        mbm_total_err = rdt_read_mbm_total(cpu, &mbm_total_val);
    } else {
        mbm_total_err = -ENODEV;
    }

    // if we support mbm local, read it on this CPU
    if (rdt_state->supports_mbm_local) {
        mbm_local_err = rdt_read_mbm_local(cpu, &mbm_local_val);
    } else {
        mbm_local_err = -ENODEV;
    }

    trace_rdt_sample(cpu, now, llc_occupancy_val, llc_occupancy_err, mbm_total_val, mbm_total_err, mbm_local_val, mbm_local_err);
}

static void collect_sample_on_current_cpu(bool is_context_switch)
{
    trace_measure_perf_counters(is_context_switch, current->rmid);

    struct cpu_state *state = this_cpu_ptr(cpu_states);
    rdt_timer_tick(&state->rdt_state);
}

static void probe_sched_switch(void *data,
                             bool preempt,
                             struct task_struct *prev,
                             struct task_struct *next,
                             unsigned int prev_state)
{
    // Only collect sample if RMID is changing
    if (prev->rmid != next->rmid) {
        // Collect sample for the outgoing task
        collect_sample_on_current_cpu(true);

        // Update RMID if we have hardware support
        if (rdt_hardware_support) {
            rdt_write_rmid_closid(next->rmid, CLOSID_CATCHALL);
        }
    }
}

static void init_cpu_state(struct work_struct *work)
{
    int ret;
    int cpu = smp_processor_id();
    struct work_struct *expected_work = per_cpu_ptr(cpu_works, cpu);
    struct cpu_state *state;

    // Verify this work matches the expected work for this CPU
    if (work != expected_work) {
        pr_err(LOG_PREFIX "CPU mismatch in init_cpu_state. On CPU %d, expected work %px, got %px\n",
               cpu, expected_work, work);
        return;
    }

    state = this_cpu_ptr(cpu_states);
    
    // Initialize RDT state for this CPU (must run on the CPU being initialized)
    ret = rdt_init_cpu(&state->rdt_state);
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize RDT state for CPU %d: error %d\n", cpu, ret);
        return;
    }
}

static void cleanup_cpu(int cpu)
{
    pr_debug(LOG_PREFIX "cleanup_cpu for CPU %d\n", cpu);
}

static enum hrtimer_restart timer_fn(struct hrtimer *timer)
{
    // Collect the sample
    collect_sample_on_current_cpu(false);
    
    // Let the sync timer module handle the restart
    return sync_timer_restart(timer, &collector_timer);
}

// Add this function after the other RMID-related functions
static void reset_all_task_rmids(void)
{
    struct task_struct *task;

    rcu_read_lock();
    for_each_process(task) {
        struct task_struct *thread;
        // Reset the thread group leader
        task->rmid = 0;
        // Reset all threads in the group
        for_each_thread(task, thread) {
            thread->rmid = 0;
        }
    }
    rcu_read_unlock();
}

// Tracepoint probes for process lifecycle events
static void probe_sched_process_fork(void *data, struct task_struct *parent, struct task_struct *child)
{
    assign_rmid_to_task(child);
}

static void probe_sched_process_free(void *data, struct task_struct *task)
{
    struct task_struct *leader = task->group_leader;
    unsigned long flags;
    u32 rmid;

    if (!leader || task != leader)
        return;  // Only free RMID when group leader is freed

    rmid = leader->rmid;
    if (rmid) {
        spin_lock_irqsave(&rmid_lock, flags);

        rmid_free(&rmid_allocator, rmid, ktime_get_ns());

        spin_unlock_irqrestore(&rmid_lock, flags);
    }
}

// Tracepoint probe registration structures
static struct tracepoint *tp_sched_process_fork;
static struct tracepoint *tp_sched_process_free;
static struct tracepoint *tp_sched_switch;

static void lookup_tracepoints(struct tracepoint *tp, void *ignore)
{
    if (!strcmp(tp->name, "sched_process_fork"))
        tp_sched_process_fork = tp;
    else if (!strcmp(tp->name, "sched_process_free"))
        tp_sched_process_free = tp;
    else if (!strcmp(tp->name, "sched_switch"))
        tp_sched_switch = tp;
}

static void cleanup_tracepoints(void)
{
    if (tp_sched_process_fork) {
        tracepoint_probe_unregister(tp_sched_process_fork,
                                   probe_sched_process_fork, NULL);
    }
    if (tp_sched_process_free) {
        tracepoint_probe_unregister(tp_sched_process_free,
                                   probe_sched_process_free, NULL);
    }
    if (tp_sched_switch) {
        tracepoint_probe_unregister(tp_sched_switch,
                                   probe_sched_switch, NULL);
    }
}

static int init_tracepoints(void)
{
    int ret;

    for_each_kernel_tracepoint(lookup_tracepoints, NULL);

    if (!tp_sched_process_fork || !tp_sched_process_free || !tp_sched_switch) {
        pr_err(LOG_PREFIX "Failed to find required tracepoints\n");
        return -EINVAL;
    }

    ret = tracepoint_probe_register(tp_sched_process_fork,
                                   probe_sched_process_fork, NULL);
    if (ret) {
        pr_err(LOG_PREFIX "Failed to register fork tracepoint\n");
        return ret;
    }

    ret = tracepoint_probe_register(tp_sched_process_free,
                                   probe_sched_process_free, NULL);
    if (ret) {
        tracepoint_probe_unregister(tp_sched_process_fork,
                                   probe_sched_process_fork, NULL);
        pr_err(LOG_PREFIX "Failed to register free tracepoint\n");
        return ret;
    }

    ret = tracepoint_probe_register(tp_sched_switch,
                                   probe_sched_switch, NULL);
    if (ret) {
        pr_err(LOG_PREFIX "Failed to register switch tracepoint\n");
        cleanup_tracepoints();
        return ret;
    }

    return 0;
}

static int __init memory_collector_init(void)
{
    int ret;
    int cpu;

    pr_info(LOG_PREFIX "loading module\n");

    // Reset all task RMIDs at the start
    reset_all_task_rmids();

    // Allocate per-CPU state
    cpu_states = alloc_percpu(struct cpu_state);
    if (!cpu_states) {
        pr_err(LOG_PREFIX "Failed to allocate per-CPU state\n");
        return -ENOMEM;
    }

    // Create workqueue first
    collector_wq = alloc_workqueue("memory_collector_wq", 0, 0);
    if (!collector_wq) {
        pr_err(LOG_PREFIX "Failed to create workqueue\n");
        ret = -ENOMEM;
        goto err_free_cpu_states;
    }

    // Allocate per-CPU work
    cpu_works = alloc_percpu(struct work_struct);
    if (!cpu_works) {
        pr_err(LOG_PREFIX "Failed to allocate per-CPU work\n");
        ret = -ENOMEM;
        goto err_destroy_workqueue;
    }

    // Initialize per-CPU work and state
    for_each_possible_cpu(cpu) {
        struct work_struct *work = per_cpu_ptr(cpu_works, cpu);
        INIT_WORK(work, init_cpu_state);
        queue_work_on(cpu, collector_wq, work);
    }

    // Wait for all initialization work to complete
    flush_workqueue(collector_wq);

    // Initialize RMID allocator after all CPUs are initialized
    ret = detect_and_init_rmid_allocator();
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize RMID allocator\n");
        goto err_free_works;
    }

    // Initialize tracepoints
    ret = init_tracepoints();
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize tracepoints\n");
        goto err_cleanup_rmid;
    }

    // Initialize procfs interface
    ret = procfs_init(&collector_procfs);
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize procfs interface\n");
        goto err_cleanup_tracepoints;
    }

    // Two-phase RMID assignment
    assign_rmids_to_leaders();    // First assign RMIDs to group leaders
    propagate_leader_rmids();     // Then propagate to all threads

    // Initialize sync timer last
    ret = sync_timer_init(&collector_timer, timer_fn, NSEC_PER_MSEC);
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize sync timer: %d\n", ret);
        goto err_cleanup_procfs;
    }

    pr_info(LOG_PREFIX "module loaded\n");
    return 0;

err_cleanup_procfs:
    procfs_cleanup(&collector_procfs);
err_cleanup_tracepoints:
    cleanup_tracepoints();
err_cleanup_rmid:
    cleanup_rmid_allocator(&rmid_allocator);
err_free_works:
    free_percpu(cpu_works);
err_destroy_workqueue:
    destroy_workqueue(collector_wq);
err_free_cpu_states:
    free_percpu(cpu_states);
    return ret;
}

static void __exit memory_collector_exit(void)
{
    int cpu;

    pr_info(LOG_PREFIX "unloading module\n");

    // Clean up sync timer first
    sync_timer_destroy(&collector_timer);

    // Clean up procfs interface
    procfs_cleanup(&collector_procfs);

    // Clean up per-CPU resources
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }

    // Clean up workqueue and per-CPU work
    if (collector_wq) {
        flush_workqueue(collector_wq);
        destroy_workqueue(collector_wq);
    }
    free_percpu(cpu_works);

    // Clean up tracepoints
    cleanup_tracepoints();
    
    // Ensure all tracepoint handlers finished before freeing resources
    tracepoint_synchronize_unregister();

    // Reset RMID to 0 on all CPUs if we have hardware support
    if (rdt_hardware_support) {
        on_each_cpu(reset_cpu_rmid, NULL, 1);
    }

    // Clean up RMID allocator
    cleanup_rmid_allocator(&rmid_allocator);

    // Free per-CPU state
    free_percpu(cpu_states);

    // Reset all task RMIDs
    reset_all_task_rmids();

    pr_info(LOG_PREFIX "done unloading\n");
}

// RMID allocation and initialization functions
static int detect_and_init_rmid_allocator(void)
{
    int cpu;
    int ret;
    u32 min_max_rmid = U32_MAX;
    struct cpu_state *state;

    // Initialize RMID lock
    spin_lock_init(&rmid_lock);

    // Find minimum max_rmid across all CPUs
    for_each_possible_cpu(cpu) {
        state = per_cpu_ptr(cpu_states, cpu);
        if (state->rdt_state.max_rmid < min_max_rmid)
            min_max_rmid = state->rdt_state.max_rmid;
    }

    if (min_max_rmid == U32_MAX || min_max_rmid == 0) {
        min_max_rmid = EMULATED_MAX_RMID;
        rdt_hardware_support = false;
        pr_info(LOG_PREFIX "Using emulated RMIDs (max=%d)\n", EMULATED_MAX_RMID);
    } else {
        rdt_hardware_support = true;
        pr_info(LOG_PREFIX "Using hardware RMIDs (max=%d)\n", min_max_rmid);
    }

    // Initialize allocator structure with spinlock
    ret = init_rmid_allocator(&rmid_allocator, min_max_rmid, 2 * NSEC_PER_MSEC);
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize RMID allocator\n");
        return ret;
    }

    return 0;
}

/*
 * Assign RMIDs to all thread group leaders in the system.
 * This should be called before assigning RMIDs to non-leaders.
 */
static void assign_rmids_to_leaders(void)
{
    struct task_struct *task;

    rcu_read_lock();
    for_each_process(task) {
        if (task == task->group_leader) {
            assign_rmid_to_task(task);
            pr_debug(LOG_PREFIX "assigned RMID %d to leader %s\n", task->rmid, task->comm);
        }
    }
    rcu_read_unlock();
}

/*
 * Propagate leader RMIDs to all threads in their groups.
 * This should be called after assign_rmids_to_leaders.
 */
static void propagate_leader_rmids(void)
{
    struct task_struct *task;
    struct task_struct *thread;

    rcu_read_lock();
    for_each_process(task) {
        for_each_thread(task, thread) {
            if (thread != thread->group_leader) {
                thread->rmid = thread->group_leader->rmid;
            }
        }
    }
    rcu_read_unlock();
}

// Move reset_cpu_rmid function definition before it's used
static void reset_cpu_rmid(void *info)
{
    rdt_write_rmid_closid(RMID_INVALID, CLOSID_CATCHALL);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);