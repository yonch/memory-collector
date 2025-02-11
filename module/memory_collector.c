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

// Add the cpu_state struct definition after the includes
struct cpu_state {
    struct perf_event *llc_miss;
    struct perf_event *cycles;
    struct perf_event *instructions;
    struct perf_event *ctx_switch;  // New field for context switch event
};

// Replace the llc_miss_events global with cpu_states
static struct cpu_state *cpu_states;
static struct perf_event *sampling_event;

// Add the init_cpu and cleanup_cpu function declarations
static int init_cpu(int cpu);
static void cleanup_cpu(int cpu);

static void collect_sample_on_current_cpu(bool is_context_switch)
{
    u64 timestamp;
    u32 cpu;
    u64 llc_misses = 0, cycles = 0, instructions = 0;
    u64 enabled, running;
    
    timestamp = ktime_get_ns();
    cpu = smp_processor_id();
    
    // Read LLC misses
    if (cpu_states[cpu].llc_miss) {
        llc_misses = perf_event_read_value(cpu_states[cpu].llc_miss, &enabled, &running);
    }

    // Read cycles
    if (cpu_states[cpu].cycles) {
        cycles = perf_event_read_value(cpu_states[cpu].cycles, &enabled, &running);
    }

    // Read instructions
    if (cpu_states[cpu].instructions) {
        instructions = perf_event_read_value(cpu_states[cpu].instructions, &enabled, &running);
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

static void ipi_handler(void *info) {
    collect_sample_on_current_cpu(false);
}

// Overflow handler for the time sampling event
static void memory_collector_overflow_handler(struct perf_event *event,
                                           struct perf_sample_data *data,
                                           struct pt_regs *regs)
{
    const struct cpumask *mask = cpu_online_mask;
    

    // Send IPI to all other CPUs
    smp_call_function_many(mask, ipi_handler, NULL, 1);

    // Run the trace on this CPU
    ipi_handler(NULL);
}

// Update init_cpu to include context switch event setup
static int init_cpu(int cpu)
{
    struct perf_event_attr attr;
    int ret;

    // Setup LLC miss event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.config = PERF_COUNT_HW_CACHE_MISSES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.exclude_kernel = 0;
    attr.exclude_hv = 0;
    attr.exclude_idle = 0;

    cpu_states[cpu].llc_miss = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
    if (IS_ERR(cpu_states[cpu].llc_miss)) {
        ret = PTR_ERR(cpu_states[cpu].llc_miss);
        pr_err("Failed to create LLC miss event for CPU %d\n", cpu);
        cpu_states[cpu].llc_miss = NULL;
        goto error;
    }

    // Setup cycles event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    
    cpu_states[cpu].cycles = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
    if (IS_ERR(cpu_states[cpu].cycles)) {
        ret = PTR_ERR(cpu_states[cpu].cycles);
        pr_err("Failed to create cycles event for CPU %d\n", cpu);
        cpu_states[cpu].cycles = NULL;
        goto error;
    }

    // Setup instructions event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    
    cpu_states[cpu].instructions = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
    if (IS_ERR(cpu_states[cpu].instructions)) {
        ret = PTR_ERR(cpu_states[cpu].instructions);
        pr_err("Failed to create instructions event for CPU %d\n", cpu);
        cpu_states[cpu].instructions = NULL;
        goto error;
    }

    // After setting up instructions event, add context switch event
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CONTEXT_SWITCHES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.sample_period = 1; // Sample every context switch
    
    // cpu_states[cpu].ctx_switch = perf_event_create_kernel_counter(&attr, cpu, NULL, 
    //                                                             context_switch_handler, 
    //                                                             NULL);
    // if (IS_ERR(cpu_states[cpu].ctx_switch)) {
    //     ret = PTR_ERR(cpu_states[cpu].ctx_switch);
    //     pr_err("Failed to create context switch event for CPU %d\n", cpu);
    //     cpu_states[cpu].ctx_switch = NULL;
    //     goto error;
    // }
    cpu_states[cpu].ctx_switch = NULL;

    return 0;

error:
    cleanup_cpu(cpu);
    return ret;
}

// Update cleanup_cpu to clean up context switch event
static void cleanup_cpu(int cpu)
{
    if (cpu_states[cpu].llc_miss) {
        perf_event_release_kernel(cpu_states[cpu].llc_miss);
        cpu_states[cpu].llc_miss = NULL;
    }
    if (cpu_states[cpu].cycles) {
        perf_event_release_kernel(cpu_states[cpu].cycles);
        cpu_states[cpu].cycles = NULL;
    }
    if (cpu_states[cpu].instructions) {
        perf_event_release_kernel(cpu_states[cpu].instructions);
        cpu_states[cpu].instructions = NULL;
    }
    if (cpu_states[cpu].ctx_switch) {
        perf_event_release_kernel(cpu_states[cpu].ctx_switch);
        cpu_states[cpu].ctx_switch = NULL;
    }
}

static void enable_sampling_work(struct work_struct *work)
{
    struct perf_event_attr attr = {
        .type = PERF_TYPE_SOFTWARE,
        .size = sizeof(struct perf_event_attr),
        .sample_period = 1000000, // 1ms
        .config = PERF_COUNT_SW_CPU_CLOCK,
    };

    pr_info("Memory Collector: enabling time-based sampling\n");
    
    // Create sampling event
    sampling_event = perf_event_create_kernel_counter(&attr, 
                                                    0, // any CPU
                                                    NULL, // all threads
                                                    memory_collector_overflow_handler,
                                                    NULL);
    if (IS_ERR(sampling_event)) {
        pr_err("Memory Collector: failed to create sampling event: %ld\n", PTR_ERR(sampling_event));
        sampling_event = NULL;
        return;
    }

    // Enable the samplingevent
    perf_event_enable(sampling_event);
}

static DECLARE_DELAYED_WORK(enable_sampling_delayed, enable_sampling_work);


// Update the init function
static int __init memory_collector_init(void)
{
    int cpu, ret;

    printk(KERN_INFO "Memory Collector: initializing\n");

    // Allocate array for CPU states
    cpu_states = kcalloc(num_possible_cpus(), sizeof(*cpu_states), GFP_KERNEL);
    if (!cpu_states) {
        ret = -ENOMEM;
        goto error_alloc;
    }

    // Initialize each CPU
    for_each_possible_cpu(cpu) {
        ret = init_cpu(cpu);
        if (ret < 0) {
            goto error_cpu_init;
        }
    }

    ret = resctrl_init();
    if (ret < 0) {
        pr_err("Failed to initialize resctrl: %d\n", ret);
        goto error_resctrl;
    }

    ret = schedule_delayed_work(&enable_sampling_delayed, msecs_to_jiffies(1000));  // 1 second delay
    if (!ret) {
        // unexpected since the work should not already be on a queue
        pr_err("Memory Collector: failed to schedule sampling work\n");
        goto error_schedule;
    }

    return 0;

error_schedule:
error_resctrl:
    resctrl_exit();
error_cpu_init:
    // Cleanup CPUs that were initialized
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
    kfree(cpu_states);
error_alloc:
    return ret;
}

// Update the exit function
static void __exit memory_collector_exit(void)
{
    int cpu;
    
    printk(KERN_INFO "Memory Collector: unregistering PMU module\n");
    
    if (sampling_event) {
        perf_event_disable(sampling_event);
        perf_event_release_kernel(sampling_event);
    }

    // Call resctrl exit first
    resctrl_exit();

    // Cleanup all CPUs
    for_each_possible_cpu(cpu) {
        cleanup_cpu(cpu);
    }
    kfree(cpu_states);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);