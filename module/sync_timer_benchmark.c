#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/math64.h>
#include <linux/tracepoint.h>
#include "sync_timer.h"
#include "collector.h"
#include "sync_timer_benchmark.h"

/* Define tracepoint for benchmark statistics */
#define CREATE_TRACE_POINTS
#include "sync_timer_benchmark.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Benchmark module for sync timer functionality");
MODULE_VERSION("1.0");

#define BENCH_PREFIX "sync_timer_bench: "
#define BENCH_INTERVAL_NS (NSEC_PER_MSEC)  // 1ms interval
#define BUFFER_SIZE 128  // Size of circular buffer
#define STATS_LAG 100   // Process stats 100 ticks behind to ensure all CPUs have written
#define STATS_CPU 0     // CPU responsible for computing statistics

struct sample_entry {
    u64 delta;          /* Time delta from expected */
    bool valid;         /* Whether this entry contains valid data */
};

struct timer_stats {
    u64 min_delta;           /* Minimum time delta from expected */
    u64 max_delta;           /* Maximum time delta from expected */
    u64 sum_delta;           /* Sum of deltas for mean calculation */
    u64 sum_delta_squared;   /* Sum of squared deltas for std dev */
    u64 sample_count;        /* Number of samples collected */
    u64 missed_ticks;        /* Number of missed ticks */
    u64 last_tick;          /* Last tick number */
    struct sample_entry samples[BUFFER_SIZE];  /* Circular buffer of samples */
};

static struct sync_timer bench_timer;
static struct timer_stats __percpu *cpu_stats;

/* Compute statistics for a given tick across all CPUs */
static void compute_tick_stats(u64 tick_number)
{
    int cpu;
    u64 min_delta = U64_MAX;
    u64 max_delta = 0;
    u64 sum_delta = 0;
    u64 sum_delta_squared = 0;
    u32 sample_count = 0;
    u32 missing_count = 0;
    u64 mean, variance, stddev;
    u64 now = ktime_get_ns();
    int buf_idx = tick_number % BUFFER_SIZE;

    /* Collect statistics across all CPUs */
    for_each_online_cpu(cpu) {
        struct timer_stats *stats = per_cpu_ptr(cpu_stats, cpu);
        struct sample_entry *entry = &stats->samples[buf_idx];

        if (entry->valid) {
            u64 delta = entry->delta;
            min_delta = min(min_delta, delta);
            max_delta = max(max_delta, delta);
            sum_delta += delta;
            sum_delta_squared += delta * delta;
            sample_count++;
            
            /* Clear the entry after processing */
            entry->valid = false;
        } else {
            missing_count++;
        }

        /* Clear the entry after processing */
        entry->valid = false;
    }

    /* Only emit tracepoint if we have samples */
    if (sample_count > 0) {
        mean = div64_u64(sum_delta, sample_count);
        variance = div64_u64(sum_delta_squared, sample_count) - (mean * mean);
        stddev = int_sqrt64(variance);

        trace_sync_timer_stats(now, tick_number, min_delta, max_delta,
                             mean, stddev, sample_count, missing_count);
    }
}

static void report_final_stats(void)
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
    int cpu, buf_idx;
    
    /* Get measurement first to minimize jitter */
    now = ktime_get_ns();
    cpu = smp_processor_id();
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

    /* Store sample in circular buffer */
    buf_idx = actual_tick % BUFFER_SIZE;
    stats->samples[buf_idx].delta = delta;
    stats->samples[buf_idx].valid = true;

    /* If we're on the stats CPU, process an old tick */
    if (cpu == STATS_CPU && actual_tick > STATS_LAG) {
        compute_tick_stats(actual_tick - STATS_LAG);
    }

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
        int i;

        stats->min_delta = U64_MAX;
        stats->max_delta = 0;
        stats->sum_delta = 0;
        stats->sum_delta_squared = 0;
        stats->sample_count = 0;
        stats->missed_ticks = 0;
        stats->last_tick = div64_u64(ktime_get_ns(), BENCH_INTERVAL_NS);

        /* Initialize circular buffer */
        for (i = 0; i < BUFFER_SIZE; i++) {
            stats->samples[i].valid = false;
        }
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
    report_final_stats();
    free_percpu(cpu_stats);
    pr_info(BENCH_PREFIX "benchmark complete\n");
}

module_init(sync_timer_bench_init);
module_exit(sync_timer_bench_exit); 