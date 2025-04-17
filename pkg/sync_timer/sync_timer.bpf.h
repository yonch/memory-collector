#pragma once

#include <linux/bpf.h>
#include <linux/time.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define NSEC_PER_MSEC 1000000ULL

/* Common structures and helper functions */
struct sync_timer_state {
    struct bpf_timer timer;
    __u64 last_tick;
    __u64 next_expected;  // Absolute time for next tick
};

/* Helper function to calculate absolute difference */
static __always_inline __u64 __sync_timer_abs_diff(__u64 a, __u64 b) {
    return a > b ? a - b : b - a;
}

/* Helper function to align time to next interval */
static __always_inline __u64 __sync_timer_align_to_interval(__u64 time, __u64 interval) {
    return (time / interval) * interval;
}

/* Shared timer callback implementation */
static __always_inline int __sync_timer_shared_callback(
    void *map,
    int *key,
    struct sync_timer_state *state,
    void (*callback_func)(void)
) {
    __u64 now = bpf_ktime_get_ns();
    __u64 expected_tick = now / NSEC_PER_MSEC;
    __u64 actual_tick = state->last_tick + 1;
    __u64 delta;

    /* Call the provided callback function */
    callback_func();

    /* Check for missed ticks */
    if (expected_tick > actual_tick) {
        actual_tick = expected_tick;
    }

    /* Update tick counter */
    state->last_tick = actual_tick;

    /* Calculate timing delta */
    delta = __sync_timer_abs_diff(now, actual_tick * NSEC_PER_MSEC);

    /* Calculate next absolute time for timer */
    state->next_expected = __sync_timer_align_to_interval(now + NSEC_PER_MSEC, NSEC_PER_MSEC);

    /* Reschedule timer for next interval using absolute time */
    bpf_timer_start(&state->timer, state->next_expected, BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN);

    return 0;
}

/* Shared timer initialization implementation */
static __always_inline int __sync_timer_shared_init(
    void *timer_states_map,
    void *init_status_map,
    int (*timer_callback)(void *, int *, struct sync_timer_state *)
) {
    __u32 cpu = bpf_get_smp_processor_id();
    struct sync_timer_state *state;
    __u64 now;
    int ret;
    __u8 init_flag = 1;
    __u32 key = 0;

    /* Get timer state for this CPU */
    state = bpf_map_lookup_elem(timer_states_map, &cpu);
    if (!state) {
        struct sync_timer_state new_state = {};
        ret = bpf_map_update_elem(timer_states_map, &cpu, &new_state, BPF_ANY);
        if (ret < 0) {
            return ret;
        }
        state = bpf_map_lookup_elem(timer_states_map, &cpu);
        if (!state) {
            return -1;
        }
    }

    /* Initialize timer if not already done */
    if (bpf_map_lookup_elem(init_status_map, &cpu) == NULL) {
        now = bpf_ktime_get_ns();
        state->next_expected = __sync_timer_align_to_interval(now + NSEC_PER_MSEC, NSEC_PER_MSEC);
        bpf_timer_init(&state->timer, timer_states_map, CLOCK_MONOTONIC);
        bpf_timer_set_callback(&state->timer, timer_callback);
        bpf_timer_start(&state->timer, state->next_expected, BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN);
        bpf_map_update_elem(init_status_map, &cpu, &init_flag, BPF_ANY);
    }

    return 0;
}

/* Macro to define a complete sync timer implementation */
#define DEFINE_SYNC_TIMER(timer_name, callback_func) \
\
/* Timer state map */ \
struct { \
    __uint(type, BPF_MAP_TYPE_HASH); \
    __uint(max_entries, 1024); \
    __type(key, __u32); \
    __type(value, struct sync_timer_state); \
} sync_timer_states_##timer_name SEC(".maps"); \
\
/* Init status map */ \
struct { \
    __uint(type, BPF_MAP_TYPE_HASH); \
    __uint(max_entries, 1024); \
    __type(key, __u32); \
    __type(value, __u8); \
} sync_timer_init_status_##timer_name SEC(".maps"); \
\
/* Timer callback function */ \
static int sync_timer_callback_##timer_name(void *map, int *key, struct sync_timer_state *state) \
{ \
    return __sync_timer_shared_callback(map, key, state, callback_func); \
} \
\
/* Timer initialization function */ \
SEC("syscall") \
int sync_timer_init_##timer_name(struct bpf_sock_addr *ctx) \
{ \
    return __sync_timer_shared_init(&sync_timer_states_##timer_name, &sync_timer_init_status_##timer_name, sync_timer_callback_##timer_name); \
} 