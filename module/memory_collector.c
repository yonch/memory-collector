#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/irq.h>
#include <linux/tracepoint.h>
#include "resctrl.h"
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Memory subsystem monitoring for Kubernetes");
MODULE_VERSION("1.0");

#ifndef CONFIG_PERF_EVENTS
#error "This module requires CONFIG_PERF_EVENTS"
#endif

// Define the tracepoint
#define CREATE_TRACE_POINTS
#include "memory_collector_trace.h"

// Replace the cpu_state struct and global variable definitions
struct cpu_state {
    struct perf_event *ctx_switch;
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
    
    trace_memory_collector_sample(cpu, timestamp, current->comm, is_context_switch);
}

// Add context switch handler
static void context_switch_handler(struct perf_event *event,
                                 struct perf_sample_data *data,
                                 struct pt_regs *regs)
{
    // Call the existing sample collection function
    collect_sample_on_current_cpu(true);
}

// Modify init_cpu_state to verify the work struct matches the current CPU
static void init_cpu_state(struct work_struct *work)
{
    struct perf_event_attr attr;
    int ret;
    int cpu = smp_processor_id();
    struct work_struct *expected_work = per_cpu_ptr(cpu_works, cpu);
    struct cpu_state *state;
    ktime_t now;

    // Verify this work matches the expected work for this CPU
    if (work != expected_work) {
        pr_err("CPU mismatch in init_cpu_state. On CPU %d, expected work %px, got %px\n",
               cpu, expected_work, work);
        return;
    }

    state = this_cpu_ptr(cpu_states);
    
    state->ctx_switch = NULL;

    // Setup context switch event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.sample_period = 1;
    
    state->ctx_switch = perf_event_create_kernel_counter(&attr, cpu, NULL, 
                                                        context_switch_handler, 
                                                        NULL);
    if (IS_ERR(state->ctx_switch)) {
        ret = PTR_ERR(state->ctx_switch);
        pr_err("Failed to create context switch event for CPU %d: error %d\n", cpu, ret);
        state->ctx_switch = NULL;
    }

    // Initialize and start the timer (moved from start_cpu_timer)
    hrtimer_init(&state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
    state->timer.function = timer_fn;
    
    now = ktime_get();
    state->next_expected = ktime_add_ns(now, NSEC_PER_MSEC);
    state->next_expected = ktime_set(ktime_to_ns(state->next_expected) / NSEC_PER_SEC,
                     (ktime_to_ns(state->next_expected) % NSEC_PER_SEC) /
                     NSEC_PER_MSEC * NSEC_PER_MSEC);
    
    hrtimer_start(&state->timer, state->next_expected, HRTIMER_MODE_ABS_PINNED);

    resctrl_init_cpu(&state->rdt_state);
}

// Update cleanup_cpu to clean up context switch event
static void cleanup_cpu(int cpu)
{
    struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);

    pr_debug("cleanup_cpu for CPU %d\n", cpu);

    hrtimer_cancel(&state->timer);

    if (state->ctx_switch) {
        perf_event_release_kernel(state->ctx_switch);
        state->ctx_switch = NULL;
    }
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

// Update the init function to use workqueue
static int __init memory_collector_init(void)
{
    int cpu, ret;

    printk(KERN_INFO "Memory Collector: initializing\n");

    cpu_works = NULL;

    // Allocate percpu array
    cpu_states = alloc_percpu(struct cpu_state);
    if (!cpu_states) {
        ret = -ENOMEM;
        goto error_alloc;
    }

    // Initialize the cpu_states
    for_each_possible_cpu(cpu) {
        struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
        state->ctx_switch = NULL;
        hrtimer_init(&state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
        state->timer.function = timer_fn;
    }

    // Create workqueue
    collector_wq = alloc_workqueue("collector_wq", 0, 0);
    if (!collector_wq) {
        ret = -ENOMEM;
        goto error_wq;
    }

    // Allocate per-CPU work structures (now global)
    cpu_works = alloc_percpu(struct work_struct);
    if (!cpu_works) {
        ret = -ENOMEM;
        goto error_work_alloc;
    }

    // Initialize and queue work for each CPU
    pr_info("Memory Collector: initializing per-cpu perf events\n");
    for_each_online_cpu(cpu) {
        struct work_struct *work = per_cpu_ptr(cpu_works, cpu);
        INIT_WORK(work, init_cpu_state);
        queue_work_on(cpu, collector_wq, work);
    }

    // Wait for all work to complete
    flush_workqueue(collector_wq);
    free_percpu(cpu_works);
    cpu_works = NULL;
    pr_info("Memory Collector: workqueue flushed\n");

    // Check initialization results
    pr_info("Memory Collector: checking per-cpu perf events\n");
    for_each_possible_cpu(cpu) {
        struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
        if (state->ctx_switch == NULL) {
            ret = -ENODEV;
            goto error_cpu_init;
        }
    }

    pr_info("Memory Collector: initialization completed\n");
    return 0;

error_cpu_init:
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
    if (cpu_works) {
        free_percpu(cpu_works);
        cpu_works = NULL;
    }
error_work_alloc:
    destroy_workqueue(collector_wq);
error_wq:
    free_percpu(cpu_states);
error_alloc:
    pr_err("Memory Collector: initialization failed, ret = %d\n", ret);
    return ret;
}

// Update the exit function to destroy workqueue
static void __exit memory_collector_exit(void)
{
    int cpu;
    
    printk(KERN_INFO "Memory Collector: unregistering PMU module\n");
    
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
    if (cpu_works) {
        // should be NULL already in any execution outcome of init, but adding for clarity
        free_percpu(cpu_works);
        cpu_works = NULL;
    }
    
    destroy_workqueue(collector_wq);
    free_percpu(cpu_states);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);