#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/math64.h>
#include "sync_timer.h"
#include "collector.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Benchmark module for sync timer functionality");
MODULE_VERSION("1.0");

#define BENCH_PREFIX "sync_timer_bench: "
#define BENCH_INTERVAL_NS (NSEC_PER_MSEC)  // 1ms interval

struct timer_stats {
    u64 min_delta;           /* Minimum time delta from expected */
    u64 max_delta;           /* Maximum time delta from expected */
    u64 sum_delta;           /* Sum of deltas for mean calculation */
    u64 sum_delta_squared;   /* Sum of squared deltas for std dev */
    u64 sample_count;        /* Number of samples collected */
    u64 missed_ticks;        /* Number of missed ticks */
    u64 last_tick;           /* Last tick number */
};

static struct sync_timer bench_timer;
static struct timer_stats __percpu *cpu_stats;

static void report_stats(void)
{
    int cpu;
    u64 total_samples = 0;
    u64 total_missed = 0;
    u64 global_min = U64_MAX;
    u64 global_max = 0;
    u64 global_sum = 0;
    u64 global_sum_squared = 0;

    for_each_online_cpu(cpu) {
        struct timer_stats *stats = per_cpu_ptr(cpu_stats, cpu);
        u64 mean;

        if (stats->sample_count == 0)
            continue;

        mean = div64_u64(stats->sum_delta, stats->sample_count);

        pr_info(BENCH_PREFIX "CPU %d stats:\n", cpu);
        pr_info(BENCH_PREFIX "  Samples: %llu\n", stats->sample_count);
        pr_info(BENCH_PREFIX "  Min delta: %llu ns\n", stats->min_delta);
        pr_info(BENCH_PREFIX "  Max delta: %llu ns\n", stats->max_delta);
        pr_info(BENCH_PREFIX "  Mean delta: %llu ns\n", mean);
        pr_info(BENCH_PREFIX "  Missed ticks: %llu\n", stats->missed_ticks);

        total_samples += stats->sample_count;
        total_missed += stats->missed_ticks;
        global_min = min(global_min, stats->min_delta);
        global_max = max(global_max, stats->max_delta);
        global_sum += stats->sum_delta;
        global_sum_squared += stats->sum_delta_squared;
    }

    if (total_samples > 0) {
        u64 global_mean = div64_u64(global_sum, total_samples);
        u64 variance = div64_u64(global_sum_squared, total_samples) - (global_mean * global_mean);
        u64 stddev = int_sqrt64(variance);

        pr_info(BENCH_PREFIX "Global stats:\n");
        pr_info(BENCH_PREFIX "  Total samples: %llu\n", total_samples);
        pr_info(BENCH_PREFIX "  Global min delta: %llu ns\n", global_min);
        pr_info(BENCH_PREFIX "  Global max delta: %llu ns\n", global_max);
        pr_info(BENCH_PREFIX "  Global mean delta: %llu ns\n", global_mean);
        pr_info(BENCH_PREFIX "  Global stddev: %llu ns\n", stddev);
        pr_info(BENCH_PREFIX "  Total missed ticks: %llu\n", total_missed);
    }
}

static enum hrtimer_restart bench_timer_fn(struct hrtimer *timer)
{
    struct timer_stats *stats;
    u64 now, expected_tick, actual_tick, delta;
    // get measurement first so as to not introduce further jitter
    now = ktime_get_ns();

    int cpu = smp_processor_id();

    stats = per_cpu_ptr(cpu_stats, cpu);
    
    /* Calculate expected and actual tick numbers */
    expected_tick = div64_u64(now, BENCH_INTERVAL_NS);
    actual_tick = stats->last_tick + 1;

    /* Check for missed ticks */
    if (expected_tick > actual_tick) {
        stats->missed_ticks += expected_tick - actual_tick;
        actual_tick = expected_tick;
    }

    /* Update tick counter */
    stats->last_tick = actual_tick;

    /* Calculate timing statistics */
    delta = abs_diff(now, actual_tick * BENCH_INTERVAL_NS);
    stats->min_delta = min(stats->min_delta, delta);
    stats->max_delta = max(stats->max_delta, delta);
    stats->sum_delta += delta;
    stats->sum_delta_squared += delta * delta;
    stats->sample_count++;

    return sync_timer_restart(timer, &bench_timer);
}

static int __init sync_timer_bench_init(void)
{
    int ret, cpu;

    pr_info(BENCH_PREFIX "starting benchmark\n");

    /* Allocate per-CPU stats */
    cpu_stats = alloc_percpu(struct timer_stats);
    if (!cpu_stats) {
        pr_err(BENCH_PREFIX "Failed to allocate per-CPU stats\n");
        return -ENOMEM;
    }

    /* Initialize stats */
    for_each_possible_cpu(cpu) {
        struct timer_stats *stats = per_cpu_ptr(cpu_stats, cpu);
        stats->min_delta = U64_MAX;
        stats->max_delta = 0;
        stats->sum_delta = 0;
        stats->sum_delta_squared = 0;
        stats->sample_count = 0;
        stats->missed_ticks = 0;
        stats->last_tick = div64_u64(ktime_get_ns(), BENCH_INTERVAL_NS);
    }

    /* Initialize timer */
    ret = sync_timer_init(&bench_timer, bench_timer_fn, BENCH_INTERVAL_NS);
    if (ret) {
        pr_err(BENCH_PREFIX "Failed to initialize timer: %d\n", ret);
        free_percpu(cpu_stats);
        return ret;
    }

    return 0;
}

static void __exit sync_timer_bench_exit(void)
{
    sync_timer_destroy(&bench_timer);
    report_stats();
    free_percpu(cpu_stats);
    pr_info(BENCH_PREFIX "benchmark complete\n");
}

module_init(sync_timer_bench_init);
module_exit(sync_timer_bench_exit); 