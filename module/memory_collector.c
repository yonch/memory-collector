#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/ktime.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Memory subsystem monitoring for Kubernetes");
MODULE_VERSION("1.0");

// Data structure for PMU samples
struct memory_collector_data {
    u64 timestamp;      // Current timestamp
    u32 core_id;        // CPU core number
    char comm[16];      // Current task comm (name)
} __packed;

// PMU type for our custom events
static struct pmu memory_collector_pmu;

// PMU event attributes
static DEFINE_PER_CPU(struct perf_event *, sampling_event);

// PMU callback functions
static void memory_collector_start(struct perf_event *event, int flags)
{
    // Enable sampling
    event->hw.state = 0;
}

static void memory_collector_stop(struct perf_event *event, int flags)
{
    // Disable sampling
    event->hw.state = PERF_HES_STOPPED;
}

static int memory_collector_add(struct perf_event *event, int flags)
{
    // Store event in per-CPU variable
    __this_cpu_write(sampling_event, event);
    return 0;
}

static void memory_collector_del(struct perf_event *event, int flags)
{
    // Clear per-CPU event
    __this_cpu_write(sampling_event, NULL);
}

static void memory_collector_read(struct perf_event *event)
{
    struct memory_collector_data data;
    struct perf_sample_data sample_data;
    struct pt_regs *regs = get_irq_regs();

    // Initialize sample data
    perf_sample_data_init(&sample_data, 0, 0);

    // Fill data structure
    data.timestamp = ktime_get_ns();
    data.core_id = smp_processor_id();
    strncpy(data.comm, current->comm, sizeof(data.comm) - 1);
    data.comm[sizeof(data.comm) - 1] = '\0';

    // Output data through perf buffer
    if (regs) {
        perf_event_output(event, &sample_data, regs);
    }
}

// PMU configuration
static int memory_collector_event_init(struct perf_event *event)
{
    if (event->attr.type != memory_collector_pmu.type)
        return -ENOENT;

    // Set callbacks
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

static int __init memory_collector_init(void)
{
    int ret;

    printk(KERN_INFO "Memory Collector: initializing PMU module\n");

    // Register PMU
    ret = perf_pmu_register(&memory_collector_pmu, "memory_collector", -1);
    if (ret) {
        printk(KERN_ERR "Memory Collector: failed to register PMU: %d\n", ret);
        return ret;
    }

    return 0;
}

static void __exit memory_collector_exit(void)
{
    printk(KERN_INFO "Memory Collector: unregistering PMU module\n");
    perf_pmu_unregister(&memory_collector_pmu);
}

module_init(memory_collector_init);
module_exit(memory_collector_exit); 