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
    __u32 expected_cpu;   // CPU ID this timer should fire on
    __u64 timer_flags;    // Pre-computed timer flags for bpf_timer_start()
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
    void (*callback_func)(__u32)  // Modified to pass expected CPU ID
) {
    __u64 now = bpf_ktime_get_ns();
    __u64 expected_tick = now / NSEC_PER_MSEC;
    __u64 actual_tick = state->last_tick + 1;
    __u64 delta;

    /* Call the provided callback function with expected CPU ID */
    callback_func(state->expected_cpu);

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

    /* Reschedule timer using pre-computed flags */
    bpf_timer_start(&state->timer, state->next_expected, state->timer_flags);

    return 0;
}

/* Shared timer initialization implementation */
static __always_inline int __sync_timer_shared_init(
    void *timer_states_map,
    int (*timer_callback)(void *, int *, struct sync_timer_state *),
    __u8 use_legacy_mode
) {
    __u32 cpu = bpf_get_smp_processor_id();
    struct sync_timer_state *state;
    __u64 now;
    int ret;

    /* Pre-compute timer flags based on mode */
    __u64 timer_flags = use_legacy_mode ? BPF_F_TIMER_ABS : (BPF_F_TIMER_ABS | BPF_F_TIMER_CPU_PIN);

    /* Get timer state for this CPU */
    state = bpf_map_lookup_elem(timer_states_map, &cpu);
    if (!state) {
        struct sync_timer_state new_state = {};
        new_state.expected_cpu = cpu;  // Store the CPU this timer should fire on
        new_state.timer_flags = timer_flags;
        ret = bpf_map_update_elem(timer_states_map, &cpu, &new_state, BPF_ANY);
        if (ret < 0) {
            return SYNC_TIMER_MAP_UPDATE_FAILED;
        }
        state = bpf_map_lookup_elem(timer_states_map, &cpu);
        if (!state) {
            return SYNC_TIMER_MAP_LOOKUP_FAILED;
        }
    }

    /* Store expected CPU and timer flags */
    state->expected_cpu = cpu;
    state->timer_flags = timer_flags;

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
    
    /* Start timer using pre-computed flags */
    ret = bpf_timer_start(&state->timer, state->next_expected, timer_flags);
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
/* Modern timer initialization function */ \
SEC("syscall") \
int sync_timer_init_##timer_name(struct bpf_sock_addr *ctx) \
{ \
    return __sync_timer_shared_init(&sync_timer_states_##timer_name, sync_timer_callback_##timer_name, 0); \
} \
\
/* Legacy fallback timer initialization function */ \
SEC("syscall") \
int sync_timer_init_legacy_##timer_name(struct bpf_sock_addr *ctx) \
{ \
    return __sync_timer_shared_init(&sync_timer_states_##timer_name, sync_timer_callback_##timer_name, 1); \
} 