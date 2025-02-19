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

// Forward declarations of new functions
static void assign_rmids_to_leaders(void);
static void propagate_leader_rmids(void);
static void reset_cpu_rmid(void *info);
static int detect_and_init_rmid_allocator(void);
static void rdt_timer_tick(struct rdt_state *rdt_state);

struct cpu_state {
    struct hrtimer timer;
    ktime_t next_expected;
    struct rdt_state rdt_state;
};

static struct cpu_state __percpu *cpu_states;

static void cleanup_cpu(int cpu);
static enum hrtimer_restart timer_fn(struct hrtimer *timer);

// Add global cpu_works declaration after other global variables
static struct work_struct __percpu *cpu_works;
static struct workqueue_struct *collector_wq;

static bool rdt_hardware_support = false;

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

    trace_memory_collector_resctrl(cpu, now, llc_occupancy_val, llc_occupancy_err, mbm_total_val, mbm_total_err, mbm_local_val, mbm_local_err);
}

static void collect_sample_on_current_cpu(bool is_context_switch)
{
    u64 timestamp = ktime_get_ns();
    u32 cpu = smp_processor_id();
    struct cpu_state *state = this_cpu_ptr(cpu_states);
    
    trace_memory_collector_sample(cpu, timestamp, current->comm, is_context_switch, current->rmid);

    rdt_timer_tick(&state->rdt_state);
}

static void probe_sched_switch(void *data,
                             bool preempt,
                             struct task_struct *prev,
                             struct task_struct *next,
                             unsigned int prev_state)
{
    // Collect sample for the outgoing task
    collect_sample_on_current_cpu(true);

    // Update RMID if it's changing and we have hardware support
    if (prev->rmid != next->rmid && rdt_hardware_support) {
        rdt_write_rmid_closid(next->rmid, CLOSID_CATCHALL);
    }
}

static void init_cpu_state(struct work_struct *work)
{
    int ret;
    int cpu = smp_processor_id();
    struct work_struct *expected_work = per_cpu_ptr(cpu_works, cpu);
    struct cpu_state *state;
    ktime_t now;

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

    // Initialize and start the timer
    hrtimer_init(&state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
    state->timer.function = timer_fn;
    
    now = ktime_get();
    state->next_expected = ktime_add_ns(now, NSEC_PER_MSEC);
    state->next_expected = ktime_set(ktime_to_ns(state->next_expected) / NSEC_PER_SEC,
                     (ktime_to_ns(state->next_expected) % NSEC_PER_SEC) /
                     NSEC_PER_MSEC * NSEC_PER_MSEC);
    
    hrtimer_start(&state->timer, state->next_expected, HRTIMER_MODE_ABS_PINNED);
}

static void cleanup_cpu(int cpu)
{
    struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);

    pr_debug(LOG_PREFIX "cleanup_cpu for CPU %d\n", cpu);

    hrtimer_cancel(&state->timer);
}

static enum hrtimer_restart timer_fn(struct hrtimer *timer)
{
    struct cpu_state *state = container_of(timer, struct cpu_state, timer);
    ktime_t now = ktime_get();
    
    // Collect the sample
    collect_sample_on_current_cpu(false);
    
    // Schedule next timer
    state->next_expected = ktime_add_ns(now, NSEC_PER_MSEC);
    state->next_expected = ktime_set(ktime_to_ns(state->next_expected) / NSEC_PER_SEC,
                     (ktime_to_ns(state->next_expected) % NSEC_PER_SEC) /
                     NSEC_PER_MSEC * NSEC_PER_MSEC);
    
    hrtimer_set_expires(timer, state->next_expected);
    return HRTIMER_RESTART;
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
    u32 rmid;

    if (!leader || task != leader)
        return;  // Only free RMID when group leader is freed

    rmid = leader->rmid;
    if (rmid)
        rmid_free(rmid);
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

    // Initialize the cpu_states so we can clean up safely if an error occurs
    for_each_possible_cpu(cpu) {
        struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
        hrtimer_init(&state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
        state->timer.function = timer_fn;
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

    // Wait for all CPU initialization to complete
    flush_workqueue(collector_wq);

    // Initialize RMID allocator after all CPUs are initialized
    ret = detect_and_init_rmid_allocator();
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize RMID allocator\n");
        goto err_cleanup_cpu_states;
    }

    // Initialize tracepoints
    ret = init_tracepoints();
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize tracepoints\n");
        goto err_cleanup_rmid;
    }

    // Initialize procfs interface
    ret = init_procfs();
    if (ret) {
        pr_err(LOG_PREFIX "Failed to initialize procfs interface\n");
        goto err_cleanup_tracepoints;
    }

    // Two-phase RMID assignment
    assign_rmids_to_leaders();    // First assign RMIDs to group leaders
    propagate_leader_rmids();     // Then propagate to all threads

    pr_info(LOG_PREFIX "module loaded\n");
    return 0;

err_cleanup_tracepoints:
    cleanup_tracepoints();
err_cleanup_rmid:
    cleanup_rmid_allocator();
err_cleanup_cpu_states:
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
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

    // Clean up procfs interface
    cleanup_procfs();

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
    cleanup_rmid_allocator();

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
    u32 min_max_rmid = U32_MAX;
    struct cpu_state *state;

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
    return init_rmid_allocator(min_max_rmid);
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