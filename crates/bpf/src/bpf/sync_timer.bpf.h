#pragma once

#define NSEC_PER_MSEC 1000000ULL
#define CLOCK_MONOTONIC 1

/* Error codes for sync timer initialization */
enum sync_timer_init_error {
    SYNC_TIMER_SUCCESS = 0,
    SYNC_TIMER_MAP_UPDATE_FAILED = 1,
    SYNC_TIMER_MAP_LOOKUP_FAILED = 2,
    SYNC_TIMER_TIMER_INIT_FAILED = 3,
    SYNC_TIMER_TIMER_SET_CALLBACK_FAILED = 4,
    SYNC_TIMER_TIMER_START_FAILED = 5,
};

// Dummy instance to make skeleton generation work
enum sync_timer_init_error sync_timer_init_error_ = 0;

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
    int (*timer_callback)(void *, int *, struct sync_timer_state *)
) {
    __u32 cpu = bpf_get_smp_processor_id();
    struct sync_timer_state *state;
    __u64 now;
    int ret;

    /* Get timer state for this CPU */
    state = bpf_map_lookup_elem(timer_states_map, &cpu);
    if (!state) {
        struct sync_timer_state new_state = {};
        ret = bpf_map_update_elem(timer_states_map, &cpu, &new_state, BPF_ANY);
        if (ret < 0) {
            return SYNC_TIMER_MAP_UPDATE_FAILED;
        }
        state = bpf_map_lookup_elem(timer_states_map, &cpu);
        if (!state) {
            return SYNC_TIMER_MAP_LOOKUP_FAILED;
        }
    }

    /* Initialize timer */
    now = bpf_ktime_get_ns();
    state->next_expected = __sync_timer_align_to_interval(now + NSEC_PER_MSEC, NSEC_PER_MSEC);
    
    ret = bpf_timer_init(&state->timer, timer_states_map, CLOCK_MONOTONIC);
    if (ret < 0) {
        return SYNC_TIMER_TIMER_INIT_FAILED;
    }
    
    ret = bpf_timer_set_callback(&state->timer, timer_callback);
    if (ret < 0) {
        return SYNC_TIMER_TIMER_SET_CALLBACK_FAILED;
    }
    
    ret = bpf_timer_start(&state->timer, state->next_expected, BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN);
    if (ret < 0) {
        return SYNC_TIMER_TIMER_START_FAILED;
    }

    return SYNC_TIMER_SUCCESS;
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
    return __sync_timer_shared_init(&sync_timer_states_##timer_name, sync_timer_callback_##timer_name); \
} 