//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "../../pkg/sync_timer/sync_timer.bpf.h"

#define NSEC_PER_MSEC 1000000ULL

static void benchmark_callback(void);

/* Define the benchmark sync timer */
DEFINE_SYNC_TIMER(benchmark, benchmark_callback)

/* Benchmark event structure */
struct benchmark_msg {
    __u64 timestamp;
    __u64 tick_number;
    __u64 delta;
};
const struct benchmark_msg *unused_bpf2go_generate_benchmark_msg __attribute__((unused)); // force golang generation of the struct

struct benchmark_result {
    struct benchmark_msg event;
    __u8 is_valid;
};

/* Perf event map for benchmark output */
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} events SEC(".maps");

/* Per-CPU array for storing benchmark results */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(struct benchmark_result));
    __uint(max_entries, 1024);
} benchmark_results SEC(".maps");

/* Per-CPU array for storing benchmark results */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
    __uint(max_entries, 1);
} benchmark_next_result SEC(".maps");

/* Context for the for_each callback */
struct flush_ctx {
    void *ctx;
    __u32 cpu;
    struct bpf_timer *expired_timer;
};

/* Callback function to flush benchmark results */
static long flush_benchmark_results(void *map, const void *key, void *value, void *ctx)
{
    struct flush_ctx *flush_ctx = (struct flush_ctx *)ctx;
    struct benchmark_result *result = (struct benchmark_result *)value;

    if (!result->is_valid) {
        // reset the next index
        __u32 key = 0;
        bpf_map_update_elem(&benchmark_next_result, &key, &key, BPF_ANY);

        // we're done! Stop iterating
        return 1;
    }
    
    /* Output to perf ring buffer */
    bpf_perf_event_output(flush_ctx->ctx, &events, BPF_F_CURRENT_CPU, &result->event, sizeof(result->event));

    /* mark invalid */
    result->is_valid = 0;
    
    return 0;
}

/* HR Timer expire exit tracepoint handler */
SEC("tracepoint/timer/hrtimer_expire_exit")
int handle_hrtimer_expire_exit(void *ctx)
{
    struct bpf_timer *expired_timer = (struct bpf_timer *)ctx;
    __u32 cpu = bpf_get_smp_processor_id();
        
    struct flush_ctx flush_ctx = {
        .ctx = ctx,
        .cpu = cpu,
        .expired_timer = expired_timer
    };
    
    /* Flush all entries for this CPU */
    long (*cb_p)(void *, const void *, void *, void *) = &flush_benchmark_results;
    bpf_for_each_map_elem(&benchmark_results, cb_p, &flush_ctx, 0);
    
    return 0;
}

/* Helper function to calculate absolute difference */
static __always_inline __u64 abs_diff(__u64 a, __u64 b) {
    return a > b ? a - b : b - a;
}

/* Benchmark callback function */
static void benchmark_callback(void)
{
    __u32 cpu = bpf_get_smp_processor_id();
    __u64 now = bpf_ktime_get_ns();
    __u64 expected_tick = now / NSEC_PER_MSEC;
    __u64 delta = abs_diff(now, expected_tick * NSEC_PER_MSEC);
    __u32 key = 0;

    /* Store benchmark event in per-CPU array */
    struct benchmark_result result = {
        .event = {
            .timestamp = now,
            .tick_number = expected_tick,
            .delta = delta,
        },
        .is_valid = 1,
    };

    __u32 *index_ptr = bpf_map_lookup_percpu_elem(&benchmark_next_result, &key, cpu);
    if (!index_ptr) {
        bpf_map_update_elem(&benchmark_next_result, &key, &key, BPF_ANY);
        index_ptr = bpf_map_lookup_percpu_elem(&benchmark_next_result, &key, cpu);
        if (!index_ptr) {
            bpf_printk("benchmark_callback: failed to get index pointer\n");
            return;
        }
    }

    bpf_map_update_elem(&benchmark_results, index_ptr, &result, BPF_ANY);
    *index_ptr = (*index_ptr + 1) % 1024;
}


char _license[] SEC("license") = "GPL";