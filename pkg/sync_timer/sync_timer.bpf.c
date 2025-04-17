//go:build ignore

#include <linux/bpf.h>
#include <linux/time.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define NSEC_PER_MSEC 1000000ULL

/* Per-CPU timer state */
struct timer_state {
    struct bpf_timer timer;
    __u64 last_tick;
    __u64 next_expected;  // Absolute time for next tick
};

/* Maps */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, struct timer_state);
} timer_states SEC(".maps");

/* Map to track initialization status per CPU */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u8);  // Boolean flag for each CPU
} init_status SEC(".maps");

/* Helper function to calculate absolute difference */
static __always_inline __u64 abs_diff(__u64 a, __u64 b) {
    return a > b ? a - b : b - a;
}

/* Helper function to align time to next interval */
static __always_inline __u64 align_to_interval(__u64 time, __u64 interval) {
    return (time / interval) * interval;
}

/* Callback function that can be extended */
__attribute__((noinline))
SEC("xdp/callback")
int callback(struct xdp_md *ctx)
{
    return 0;
}

/* Timer callback function */
static int timer_callback(void *map, int *key, struct timer_state *state)
{
    __u64 now = bpf_ktime_get_ns();
    __u64 expected_tick = now / NSEC_PER_MSEC;
    __u64 actual_tick = state->last_tick + 1;
    __u64 delta;

    /* Check for missed ticks */
    if (expected_tick > actual_tick) {
        actual_tick = expected_tick;
    }

    /* Update tick counter */
    state->last_tick = actual_tick;

    /* Calculate timing delta */
    delta = abs_diff(now, actual_tick * NSEC_PER_MSEC);

    /* Calculate next absolute time for timer */
    state->next_expected = align_to_interval(now + NSEC_PER_MSEC, NSEC_PER_MSEC);

    /* Reschedule timer for next interval using absolute time */
    bpf_timer_start(&state->timer, state->next_expected, BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN);

    /* Call the callback function */
    struct xdp_md ctx = {};
    callback(&ctx);

    return 0;
}

/* Timer initialization function */
SEC("syscall")
int init_timer(struct bpf_sock_addr *ctx)
{
    __u32 cpu = bpf_get_smp_processor_id();
    struct timer_state *state;
    __u64 now;
    int ret;
    __u8 init_flag = 1;
    __u32 key = 0;

    static const char msg[] = "init_timer called on CPU %d\n";
    bpf_trace_printk(msg, sizeof(msg), cpu);

    /* Get timer state for this CPU */
    state = bpf_map_lookup_elem(&timer_states, &cpu);
    if (!state) {
        // Create a new timer state
        struct timer_state new_state = {};
        ret = bpf_map_update_elem(&timer_states, &cpu, &new_state, BPF_ANY);
        if (ret) {
            return 1000 + ret;
        }

        state = bpf_map_lookup_elem(&timer_states, &cpu);
        if (!state) {
            return 2000 + ret;
        }
    }

    /* Initialize timer */
    ret = bpf_timer_init(&state->timer, &timer_states, CLOCK_MONOTONIC);
    if (ret)
        return 3000 + ret;

    /* Set callback function */
    ret = bpf_timer_set_callback(&state->timer, timer_callback);
    if (ret)
        return 4000 + ret;

    /* Calculate first absolute time for timer */
    now = bpf_ktime_get_ns();
    state->next_expected = align_to_interval(now + NSEC_PER_MSEC, NSEC_PER_MSEC);
    state->last_tick = now / NSEC_PER_MSEC;

    /* Start timer with absolute time */
    ret = bpf_timer_start(&state->timer, state->next_expected, BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN);
    if (ret)
        return 5000 + ret;

    /* Update initialization status */
    ret = bpf_map_update_elem(&init_status, &cpu, &init_flag, BPF_ANY);
    if (ret) {
        return 6000 + ret;
    }

    return 0;  // Success
}

char _license[] SEC("license") = "GPL"; 