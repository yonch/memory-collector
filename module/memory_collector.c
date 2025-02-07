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

// PMU type for our custom events
static struct pmu memory_collector_pmu;


// New IPI handler function that will run on each CPU
static void memory_collector_ipi_handler(void *info)
{
    u32 cpu = smp_processor_id();
    
    // Trace the event for this CPU
    trace_memory_collector_sample(cpu, ktime_get_ns(), current->comm);
}

// Modified overflow handler
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

// PMU callback functions
static void memory_collector_start(struct perf_event *event, int flags)
{
    event->hw.state = 0;
}

static void memory_collector_stop(struct perf_event *event, int flags)
{
    event->hw.state = PERF_HES_STOPPED;
}

static int memory_collector_add(struct perf_event *event, int flags)
{
    return 0;
}

static void memory_collector_del(struct perf_event *event, int flags)
{
}

static void memory_collector_read(struct perf_event *event)
{
    // Trigger the overflow handler directly
    struct perf_sample_data data;
    struct pt_regs *regs = get_irq_regs();
    
    perf_sample_data_init(&data, 0, 1);
    
    if (regs) {
        memory_collector_overflow_handler(event, &data, regs);
    }
}

// PMU configuration
static int memory_collector_event_init(struct perf_event *event)
{
    if (event->attr.type != memory_collector_pmu.type)
        return -ENOENT;

    event->destroy = NULL;
    event->hw.config = 0;
    
    return 0;
}

// PMU structure
static struct pmu memory_collector_pmu = {
    .task_ctx_nr = perf_sw_context,
    .event_init  = memory_collector_event_init,
    .add         = memory_collector_add,
    .del         = memory_collector_del,
    .start       = memory_collector_start,
    .stop        = memory_collector_stop,
    .read        = memory_collector_read,
};

// Keep track of our event
static struct perf_event *sampling_event;

static int __init memory_collector_init(void)
{
    int ret;
    struct perf_event_attr attr = {
        .type = PERF_TYPE_SOFTWARE,
        .size = sizeof(struct perf_event_attr),
        .sample_period = 1000000, // 1ms
        .config = PERF_COUNT_SW_CPU_CLOCK,
    };

    printk(KERN_INFO "Memory Collector: initializing PMU module\n");

    // Register PMU
    ret = perf_pmu_register(&memory_collector_pmu, "memory_collector", -1);
    if (ret) {
        printk(KERN_ERR "Memory Collector: failed to register PMU: %d\n", ret);
        return ret;
    }

    // Create a kernel counter that will drive our sampling
    sampling_event = perf_event_create_kernel_counter(&attr, 
                                                    0, // any CPU
                                                    NULL, // all threads
                                                    memory_collector_overflow_handler,
                                                    NULL);
    if (IS_ERR(sampling_event)) {
        ret = PTR_ERR(sampling_event);
        printk(KERN_ERR "Memory Collector: failed to create sampling event: %d\n", ret);
        perf_pmu_unregister(&memory_collector_pmu);
        return ret;
    }

    // Enable the event
    perf_event_enable(sampling_event);

    return 0;
}

static void __exit memory_collector_exit(void)
{
    printk(KERN_INFO "Memory Collector: unregistering PMU module\n");
    
    if (sampling_event) {
        perf_event_disable(sampling_event);
        perf_event_release_kernel(sampling_event);
    }
    
    perf_pmu_unregister(&memory_collector_pmu);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit);