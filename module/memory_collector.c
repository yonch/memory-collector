#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/ktime.h>
#include <linux/irq.h>
#include <linux/tracepoint.h>

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

// Keep track of the LLC miss events for each CPU
static struct perf_event **llc_miss_events;

// Keep track of the time sampling
static struct perf_event *sampling_event;

// IPI handler function that will run on each CPU
static void memory_collector_ipi_handler(void *info)
{
    u64 timestamp;
    u32 cpu;
    u64 llc_misses, enabled, running;
    
    timestamp = ktime_get_ns();
    cpu = smp_processor_id();
    
    // Read LLC misses
    if (llc_miss_events[cpu]) {
        llc_misses = perf_event_read_value(llc_miss_events[cpu], &enabled, &running);
    } else {
        llc_misses = 0;
    }

    trace_memory_collector_sample(timestamp, cpu, current->comm, llc_misses);
}

// Overflow handler for the time sampling event
static void memory_collector_overflow_handler(struct perf_event *event,
                                           struct perf_sample_data *data,
                                           struct pt_regs *regs)
{
    const struct cpumask *mask = cpu_online_mask;
    

    // Send IPI to all other CPUs
    smp_call_function_many(mask, memory_collector_ipi_handler, NULL, 1);

    // Run the trace on this CPU
    memory_collector_ipi_handler(NULL);
}


// Modify init function:
static int __init memory_collector_init(void)
{
    int cpu, ret;
    struct perf_event_attr attr = {
        .type = PERF_TYPE_SOFTWARE,
        .size = sizeof(struct perf_event_attr),
        .sample_period = 1000000, // 1ms
        .config = PERF_COUNT_SW_CPU_CLOCK,
    };

    printk(KERN_INFO "Memory Collector: initializing PMU module\n");


    // Create a kernel counter that will drive our sampling
    sampling_event = perf_event_create_kernel_counter(&attr, 
                                                    0, // any CPU
                                                    NULL, // all threads
                                                    memory_collector_overflow_handler,
                                                    NULL);
    if (IS_ERR(sampling_event)) {
        ret = PTR_ERR(sampling_event);
        printk(KERN_ERR "Memory Collector: failed to create sampling event: %d\n", ret);
        return ret;
    }

    // Enable the event
    perf_event_enable(sampling_event);

    // Allocate array for LLC miss events
    llc_miss_events = kcalloc(num_possible_cpus(), sizeof(*llc_miss_events), GFP_KERNEL);
    if (!llc_miss_events) {
        ret = -ENOMEM;
        goto error_alloc;
    }

    // Setup LLC miss event attributes
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.config = PERF_COUNT_HW_CACHE_MISSES;
    attr.size = sizeof(attr);
    attr.disabled = 0;
    attr.exclude_kernel = 0;
    attr.exclude_hv = 0;
    attr.exclude_idle = 0;

    // Create LLC miss counter for each CPU
    for_each_possible_cpu(cpu) {
        llc_miss_events[cpu] = perf_event_create_kernel_counter(&attr, cpu, NULL, NULL, NULL);
        if (IS_ERR(llc_miss_events[cpu])) {
            pr_err("Failed to create LLC miss event for CPU %d\n", cpu);
            llc_miss_events[cpu] = NULL;
            ret = PTR_ERR(llc_miss_events[cpu]);
            goto error_events;
        }
    }

    return 0;

error_events:
    // Cleanup LLC miss events
    for_each_possible_cpu(cpu) {
        if (llc_miss_events[cpu]) {
            perf_event_release_kernel(llc_miss_events[cpu]);
        }
    }
    kfree(llc_miss_events);
error_alloc:
    perf_event_disable(sampling_event);
    perf_event_release_kernel(sampling_event);
    return ret;
}

static void __exit memory_collector_exit(void)
{
    int cpu;
    
    printk(KERN_INFO "Memory Collector: unregistering PMU module\n");
    
    if (sampling_event) {
        perf_event_disable(sampling_event);
        perf_event_release_kernel(sampling_event);
    }
    

    // Cleanup LLC miss events
    for_each_possible_cpu(cpu) {
        if (llc_miss_events[cpu]) {
            perf_event_release_kernel(llc_miss_events[cpu]);
        }
    }
    kfree(llc_miss_events);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);