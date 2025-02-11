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
    struct perf_event *llc_miss;
    struct perf_event *cycles;
    struct perf_event *instructions;
    struct perf_event *ctx_switch;
    struct hrtimer timer;
    ktime_t next_expected;
};

static struct cpu_state __percpu *cpu_states;

static void cleanup_cpu(int cpu);
static enum hrtimer_restart timer_fn(struct hrtimer *timer);

// Add global cpu_works declaration after other global variables
static struct work_struct __percpu *cpu_works;
static struct workqueue_struct *collector_wq;

static void collect_sample_on_current_cpu(bool is_context_switch)
{
    u64 timestamp;
    u32 cpu;
    u64 llc_misses = 0, cycles = 0, instructions = 0;
    u64 enabled, running;
    
    timestamp = ktime_get_ns();
    cpu = smp_processor_id();
    struct cpu_state *state = this_cpu_ptr(cpu_states);
    
    // Read LLC misses
    if (state->llc_miss) {
        llc_misses = perf_event_read_value(state->llc_miss, &enabled, &running);
    }

    // Read cycles
    if (state->cycles) {
        cycles = perf_event_read_value(state->cycles, &enabled, &running);
    }

    // Read instructions
    if (state->instructions) {
        instructions = perf_event_read_value(state->instructions, &enabled, &running);
    }

    trace_memory_collector_sample(cpu, timestamp, current->comm, 
                                llc_misses, cycles, instructions, is_context_switch);
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
    pr_info("init_cpu_state for CPU %d, got work %px\n", cpu, work);
    
    state->llc_miss = NULL;
    state->cycles = NULL;
    state->instructions = NULL;
    state->ctx_switch = NULL;

    // Setup LLC miss event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.config = PERF_COUNT_HW_CACHE_MISSES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.exclude_kernel = 0;
    attr.exclude_hv = 0;
    attr.exclude_idle = 0;

    state->llc_miss = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
    if (IS_ERR(state->llc_miss)) {
        ret = PTR_ERR(state->llc_miss);
        pr_err("Failed to create LLC miss event for CPU %d: error %d\n", cpu, ret);
        state->llc_miss = NULL;
    }

    // Setup cycles event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    
    state->cycles = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
    if (IS_ERR(state->cycles)) {
        ret = PTR_ERR(state->cycles);
        pr_err("Failed to create cycles event for CPU %d: error %d\n", cpu, ret);
        state->cycles = NULL;
    }

    // Setup instructions event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    
    state->instructions = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
    if (IS_ERR(state->instructions)) {
        ret = PTR_ERR(state->instructions);
        pr_err("Failed to create instructions event for CPU %d: error %d\n", cpu, ret);
        state->instructions = NULL;
    }

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
    if (state->llc_miss) {
        perf_event_release_kernel(state->llc_miss);
        state->llc_miss = NULL;
    }
    if (state->cycles) {
        perf_event_release_kernel(state->cycles);
        state->cycles = NULL;
    }
    if (state->instructions) {
        perf_event_release_kernel(state->instructions);
        state->instructions = NULL;
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
    
    hrtimer_forward_now(timer, state->next_expected);
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
        state->llc_miss = NULL;
        state->cycles = NULL;
        state->instructions = NULL;
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

    // // Check initialization results
    // pr_info("Memory Collector: checking per-cpu perf events\n");
    // for_each_possible_cpu(cpu) {
    //     struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
    //     if (state->ctx_switch == NULL) {
    //         res = -ENODEV;
    //         goto error_cpu_init;
    //     }
    // }

    pr_info("Memory Collector: initializing resctrl\n");
    ret = resctrl_init();
    if (ret < 0) {
        pr_err("Failed to initialize resctrl: %d\n", ret);
        goto error_resctrl;
    }

    pr_info("Memory Collector: initialization completed\n");
    return 0;

error_resctrl:
    resctrl_exit();
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
    
    resctrl_exit();

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