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
#include "resctrl.h"
#include <linux/workqueue.h>
#include "procfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Memory subsystem monitoring for Kubernetes");
MODULE_VERSION("1.0");

#define LOG_PREFIX "Memory Collector: "

// Define the tracepoint
#define CREATE_TRACE_POINTS
#include "memory_collector_trace.h"

#define EMULATED_MAX_RMID 512
#define RMID_INVALID 0
#define CLOSID_CATCHALL 0

// RMID allocation structure
struct rmid_info {
    struct list_head list;  // For free list
    u32 rmid;
    char comm[TASK_COMM_LEN];  // Name of task leader
    pid_t tgid;  // Thread group ID (process ID)
};

struct rmid_alloc {
    spinlock_t lock;  // Protects all fields
    struct list_head free_list;  // List of free RMIDs
    u32 max_rmid;  // Minimum of max_rmid across all CPUs
    struct rmid_info *rmids;  // Array of RMID info, indexed by RMID
    bool hardware_support;  // true if RDT hardware support is detected
};

static struct rmid_alloc rmid_allocator;

// Forward declarations of new functions
static void rmid_free(u32 rmid);
static u32 _rmid_alloc(const char *comm, pid_t tgid);
static int init_rmid_allocator(void);
static void cleanup_rmid_allocator(void);
static void assign_rmid_to_task(struct task_struct *task);
static void assign_rmids_to_leaders(void);
static void propagate_leader_rmids(void);
static void reset_cpu_rmid(void *info);

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

static void collect_sample_on_current_cpu(bool is_context_switch)
{
    u64 timestamp = ktime_get_ns();
    u32 cpu = smp_processor_id();
    struct cpu_state *state = this_cpu_ptr(cpu_states);
    
    trace_memory_collector_sample(cpu, timestamp, current->comm, is_context_switch, current->rmid);

    resctrl_timer_tick(&state->rdt_state);
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
    if (prev->rmid != next->rmid && rmid_allocator.hardware_support) {
        write_rmid_closid(next->rmid, CLOSID_CATCHALL);
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
    ret = resctrl_init_cpu(&state->rdt_state);
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

/*
 * Internal helper to allocate an RMID.
 * Caller must hold rmid_allocator.lock.
 * Returns 0 if no RMID is available (RMID 0 is reserved/invalid).
 */
static u32 _rmid_alloc(const char *comm, pid_t tgid)
{
    struct rmid_info *info;
    u32 rmid;

    // Check if we have any free RMIDs
    if (list_empty(&rmid_allocator.free_list)) {
        return 0;  // RMID 0 is reserved/invalid
    }

    // Get the RMID that was freed the longest time ago
    info = list_first_entry(&rmid_allocator.free_list, struct rmid_info, list);
    list_del_init(&info->list);

    // Update RMID info
    strncpy(info->comm, comm, TASK_COMM_LEN - 1);
    info->comm[TASK_COMM_LEN - 1] = '\0';
    info->tgid = tgid;
    rmid = info->rmid;

    // Emit tracepoint for RMID allocation while holding the lock
    trace_memory_collector_rmid_alloc(rmid, comm, tgid, ktime_get_ns());

    return rmid;
}

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
    spin_lock_irqsave(&rmid_allocator.lock, flags);

    // Recheck after acquiring lock
    if (!group_leader->rmid) {
        // Allocate new RMID for the process
        rmid = _rmid_alloc(group_leader->comm, group_leader->tgid);
        group_leader->rmid = rmid;
        // Note: if allocation fails, leader->rmid remains 0
    }

    spin_unlock_irqrestore(&rmid_allocator.lock, flags);
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

extern void dump_existing_rmids(void);

// Make dump_existing_rmids available to procfs.c
void dump_existing_rmids(void)
{
    unsigned long flags;
    u32 i;
    struct rmid_info *info;

    for (i = 1; i <= rmid_allocator.max_rmid; i++) {
        // Lock for each element to avoid starving the write path
        spin_lock_irqsave(&rmid_allocator.lock, flags);
        
        info = &rmid_allocator.rmids[i];

        // Only emit tracepoint if RMID is in use (not on free list)
        if (list_empty(&info->list)) {
            trace_memory_collector_rmid_existing(
                info->rmid,
                info->comm,
                info->tgid,
                ktime_get_ns()
            );
        }
        
        spin_unlock_irqrestore(&rmid_allocator.lock, flags);
    }
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
    ret = init_rmid_allocator();
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
    if (rmid_allocator.hardware_support) {
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
static int init_rmid_allocator(void)
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
        rmid_allocator.hardware_support = false;
        pr_info(LOG_PREFIX "Using emulated RMIDs (max=%d)\n", EMULATED_MAX_RMID);
    } else {
        rmid_allocator.hardware_support = true;
        pr_info(LOG_PREFIX "Using hardware RMIDs (max=%d)\n", min_max_rmid);
    }

    // Initialize allocator structure with spinlock
    spin_lock_init(&rmid_allocator.lock);
    INIT_LIST_HEAD(&rmid_allocator.free_list);
    rmid_allocator.max_rmid = min_max_rmid;

    // Allocate array of RMID info structures
    rmid_allocator.rmids = kzalloc(sizeof(struct rmid_info) * (min_max_rmid + 1), GFP_KERNEL);
    if (!rmid_allocator.rmids) {
        pr_err(LOG_PREFIX "Failed to allocate RMID info array\n");
        return -ENOMEM;
    }

    // Initialize all RMIDs (skip RMID 0 as it's reserved)
    for (u32 i = 0; i <= min_max_rmid; i++) {
        INIT_LIST_HEAD(&rmid_allocator.rmids[i].list);
        rmid_allocator.rmids[i].rmid = i;
        rmid_allocator.rmids[i].tgid = 0;
        if (i != RMID_INVALID) {  // Don't add RMID 0 to free list
            list_add_tail(&rmid_allocator.rmids[i].list, &rmid_allocator.free_list);
        }
    }

    return 0;
}

static void cleanup_rmid_allocator(void)
{
    kfree(rmid_allocator.rmids);
    rmid_allocator.rmids = NULL;
}

static void rmid_free(u32 rmid)
{
    unsigned long flags;
    struct rmid_info *info;

    if (rmid == RMID_INVALID || rmid > rmid_allocator.max_rmid)
        return;

    spin_lock_irqsave(&rmid_allocator.lock, flags);

    info = &rmid_allocator.rmids[rmid];
    info->tgid = 0;
    list_add_tail(&info->list, &rmid_allocator.free_list);

    // Emit tracepoint for RMID deallocation while holding the lock
    trace_memory_collector_rmid_free(rmid, ktime_get_ns());

    spin_unlock_irqrestore(&rmid_allocator.lock, flags);
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
            pr_info(LOG_PREFIX "assigned RMID %d to leader %s\n", task->rmid, task->comm);
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
    write_rmid_closid(RMID_INVALID, CLOSID_CATCHALL);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);