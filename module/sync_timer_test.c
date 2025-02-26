#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/percpu.h>
#include "sync_timer.h"
#include "collector.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Test module for sync timer functionality");
MODULE_VERSION("1.0");

#define TEST_PREFIX "sync_timer_test: "
#define TEST_RESULT "test_result:"
#define TEST_INTERVAL_NS (NSEC_PER_MSEC)  // 1ms interval
#define TEST_DURATION_MS 100  // Test duration in milliseconds

struct cpu_tick_data {
    u64 last_tick;           /* Last tick number for this CPU */
};

static struct sync_timer test_timer;
static struct cpu_tick_data __percpu *cpu_ticks;
static atomic_t callback_count;
static atomic_t error_count;
static atomic_t max_tick_diff;
static DEFINE_SPINLOCK(test_lock);

static void report_test_result(const char *test_name, bool passed, const char *message)
{
    pr_info(TEST_RESULT "%s:%s%s%s\n", test_name, 
            passed ? "pass" : "fail",
            message ? ":" : "",
            message ? message : "");
}

static enum hrtimer_restart test_timer_fn(struct hrtimer *timer)
{
    struct cpu_tick_data *tick_data;
    u64 now, expected_tick, actual_tick;
    int cpu = smp_processor_id();
    unsigned long flags;

    now = ktime_get_ns();
    tick_data = per_cpu_ptr(cpu_ticks, cpu);

    spin_lock_irqsave(&test_lock, flags);

    /* Calculate expected and actual tick numbers */
    expected_tick = div64_u64(now, TEST_INTERVAL_NS);
    actual_tick = tick_data->last_tick + 1;

    /* Check for missed ticks */
    if (expected_tick > actual_tick) {
        atomic_inc(&error_count);
    }

    /* Track maximum tick difference between CPUs */
    if (tick_data->last_tick > 0) {
        int tick_diff = abs((int)(tick_data->last_tick - expected_tick));
        int current_max = atomic_read(&max_tick_diff);
        if (tick_diff > current_max) {
            atomic_set(&max_tick_diff, tick_diff);
        }
    }

    /* Update tick counter */
    tick_data->last_tick = actual_tick;

    atomic_inc(&callback_count);
    spin_unlock_irqrestore(&test_lock, flags);

    return sync_timer_restart(timer, &test_timer);
}

static bool test_timer_init(void)
{
    int ret, cpu;
    bool passed = true;

    atomic_set(&callback_count, 0);
    atomic_set(&error_count, 0);
    atomic_set(&max_tick_diff, 0);

    /* Allocate per-CPU tick tracking data */
    cpu_ticks = alloc_percpu(struct cpu_tick_data);
    if (!cpu_ticks) {
        report_test_result("timer_init", false, "failed to allocate CPU tick data");
        return false;
    }

    /* Initialize per-CPU tick data */
    for_each_possible_cpu(cpu) {
        struct cpu_tick_data *tick_data = per_cpu_ptr(cpu_ticks, cpu);
        tick_data->last_tick = div64_u64(ktime_get_ns(), TEST_INTERVAL_NS);
    }

    ret = sync_timer_init(&test_timer, test_timer_fn, TEST_INTERVAL_NS);
    if (ret) {
        free_percpu(cpu_ticks);
        report_test_result("timer_init", false, "init failed");
        return false;
    }

    /* Wait for some callbacks to occur */
    msleep(TEST_DURATION_MS);

    /* Verify callbacks occurred on all CPUs */
    if (atomic_read(&callback_count) < TEST_DURATION_MS * num_online_cpus()) {
        passed = false;
        pr_err(TEST_PREFIX "Too few callbacks: expected >= %d, got %d\n",
               TEST_DURATION_MS * num_online_cpus(),
               atomic_read(&callback_count));
    }

    /* Check for errors during test */
    if (atomic_read(&error_count) > 0) {
        passed = false;
        pr_err(TEST_PREFIX "Detected %d timing errors\n",
               atomic_read(&error_count));
    }

    /* Report maximum tick difference */
    pr_info(TEST_PREFIX "Maximum tick difference between CPUs: %d\n",
            atomic_read(&max_tick_diff));

    sync_timer_destroy(&test_timer);
    free_percpu(cpu_ticks);

    report_test_result("timer_init", passed, NULL);
    return passed;
}

static bool test_timer_cleanup(void)
{
    int ret, old_count;
    bool passed = true;

    /* Allocate per-CPU tick tracking data */
    cpu_ticks = alloc_percpu(struct cpu_tick_data);
    if (!cpu_ticks) {
        report_test_result("timer_cleanup", false, "failed to allocate CPU tick data");
        return false;
    }

    /* Initialize timer */
    ret = sync_timer_init(&test_timer, test_timer_fn, TEST_INTERVAL_NS);
    if (ret) {
        free_percpu(cpu_ticks);
        report_test_result("timer_cleanup", false, "init failed");
        return false;
    }

    /* Let it run briefly */
    msleep(10);

    /* Destroy timer */
    sync_timer_destroy(&test_timer);

    old_count = atomic_read(&callback_count);

    free_percpu(cpu_ticks);

    /* Wait to ensure no more callbacks occur */
    msleep(10);

    if (atomic_read(&callback_count) != old_count) {
        passed = false;
        pr_err(TEST_PREFIX "Callbacks occurred after destroy\n");
    }

    report_test_result("timer_cleanup", passed, NULL);
    return passed;
}

static int __init sync_timer_test_init(void)
{
    bool all_passed = true;

    pr_info(TEST_PREFIX "starting tests\n");

    all_passed &= test_timer_init();
    all_passed &= test_timer_cleanup();

    pr_info(TEST_PREFIX "tests %s\n", all_passed ? "passed" : "failed");
    return 0;
}

static void __exit sync_timer_test_exit(void)
{
    pr_info(TEST_PREFIX "module unloaded\n");
}

module_init(sync_timer_test_init);
module_exit(sync_timer_test_exit); 