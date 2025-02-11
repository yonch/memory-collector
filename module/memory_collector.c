#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/irq.h>
#include <linux/tracepoint.h>
#include "resctrl.h"

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

// Add the init_cpu and cleanup_cpu function declarations
static void cleanup_cpu(int cpu);

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

// Update init_cpu to include context switch event setup
static void init_cpu(void *info)
{
    struct perf_event_attr attr;
    int ret;
    int cpu = smp_processor_id();
    struct cpu_state *state = this_cpu_ptr(cpu_states);
    
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
        pr_err("Failed to create LLC miss event for CPU %d\n", cpu);
        state->llc_miss = NULL;
        goto error;
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
        pr_err("Failed to create cycles event for CPU %d\n", cpu);
        state->cycles = NULL;
        goto error;
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
        pr_err("Failed to create instructions event for CPU %d\n", cpu);
        state->instructions = NULL;
        goto error;
    }

    // After setting up instructions event, add context switch event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.sample_period = 1; // Sample every context switch
    
    state->ctx_switch = perf_event_create_kernel_counter(&attr, cpu, NULL, 
                                                                context_switch_handler, 
                                                                NULL);
    if (IS_ERR(state->ctx_switch)) {
        ret = PTR_ERR(state->ctx_switch);
        pr_err("Failed to create context switch event for CPU %d\n", cpu);
        state->ctx_switch = NULL;
        goto error;
    }

    return;
error:
    cleanup_cpu(cpu);
}

// Update cleanup_cpu to clean up context switch event
static void cleanup_cpu(int cpu)
{
    struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
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

static void start_cpu_timer(void *info)
{
    struct cpu_state *state;
    ktime_t now;
    
    state = this_cpu_ptr(cpu_states);
    
    hrtimer_init(&state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
    state->timer.function = timer_fn;
    
    // Start timer at next millisecond boundary
    now = ktime_get();
    state->next_expected = ktime_add_ns(now, NSEC_PER_MSEC);
    state->next_expected = ktime_set(ktime_to_ns(state->next_expected) / NSEC_PER_SEC,
                     (ktime_to_ns(state->next_expected) % NSEC_PER_SEC) /
                     NSEC_PER_MSEC * NSEC_PER_MSEC);
    
    hrtimer_start(&state->timer, state->next_expected, HRTIMER_MODE_ABS);
}

// Update the init function
static int __init memory_collector_init(void)
{
    int cpu, ret;

    printk(KERN_INFO "Memory Collector: initializing\n");

    // Allocate percpu array
    cpu_states = alloc_percpu(struct cpu_state);
    if (!cpu_states) {
        ret = -ENOMEM;
        goto error_alloc;
    }

    // Initialize each CPU
    pr_info("Memory Collector: initializing per-cpu perf events\n");
    on_each_cpu(init_cpu, NULL, 1);

    pr_info("Memory Collector: checking per-cpu perf events\n");
    for_each_possible_cpu(cpu) {
        struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
        if (state->ctx_switch == NULL) {
            // there was an error during one of the init_cpu calls
            goto error_cpu_init;
        }
    }

    pr_info("Memory Collector: initializing resctrl\n");
    ret = resctrl_init();
    if (ret < 0) {
        pr_err("Failed to initialize resctrl: %d\n", ret);
        goto error_resctrl;
    }

    pr_info("Memory Collector: starting timers on all CPUs\n");
    on_each_cpu(start_cpu_timer, NULL, 1);

    return 0;

error_resctrl:
    resctrl_exit();
error_cpu_init:
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
    free_percpu(cpu_states);
error_alloc:
    return ret;
}

// Update the exit function
static void __exit memory_collector_exit(void)
{
    int cpu;
    
    printk(KERN_INFO "Memory Collector: unregistering PMU module\n");
    
    
    // Cancel timers on all CPUs
    for_each_possible_cpu(cpu) {
        struct cpu_state *state = per_cpu_ptr(cpu_states, cpu);
        hrtimer_cancel(&state->timer);
    }

    // Call resctrl exit
    resctrl_exit();

    // Cleanup all CPUs
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
    
    free_percpu(cpu_states);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);