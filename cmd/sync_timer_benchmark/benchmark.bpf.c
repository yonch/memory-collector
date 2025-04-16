//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define NSEC_PER_MSEC 1000000ULL

/* Helper function to calculate absolute difference */
static __always_inline __u64 abs_diff(__u64 a, __u64 b) {
    return a > b ? a - b : b - a;
}

/* Benchmark event structure */
struct benchmark_msg {
    __u64 timestamp;
    __u64 tick_number;
    __u64 delta;
};
const struct benchmark_msg *unused_bpf2go_generate_benchmark_msg __attribute__((unused)); // force golang generation of the struct

/* Perf event map for benchmark output */
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} events SEC(".maps");

/* Benchmark callback program */
SEC("timer_callback")
int benchmark_callback(struct bpf_perf_event_data *ctx)
{
    __u64 now = bpf_ktime_get_ns();
    __u64 expected_tick = now / NSEC_PER_MSEC;
    __u64 delta = abs_diff(now, expected_tick * NSEC_PER_MSEC);

    /* Emit benchmark event */
    struct benchmark_msg event = {
        .timestamp = now,
        .tick_number = expected_tick,
        .delta = delta,
    };

    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event));
    return 0;
}

char _license[] SEC("license") = "GPL"; 