#ifndef BACKOFF_H
#define BACKOFF_H

#include <linux/types.h>

// Maximum number of consecutive failures before reaching max backoff
#define MAX_BACKOFF_LEVEL 7

// Backoff state structure
struct backoff_state {
    __u8 consecutive_failures;  // Number of consecutive failures
    __u8 in_backoff;           // Whether we're currently in backoff mode
};

// Initialize or reset the backoff state
static __always_inline void backoff_init(struct backoff_state *state) {
    state->consecutive_failures = 0;
    state->in_backoff = 0;
}

// Update state after a success
static __always_inline void backoff_update_success(struct backoff_state *state) {
    state->consecutive_failures = 0;
    state->in_backoff = 0;
}

// Update state after a failure
static __always_inline void backoff_update_failure(struct backoff_state *state) {
    state->consecutive_failures++;
    if (state->consecutive_failures > MAX_BACKOFF_LEVEL) {
        state->consecutive_failures = MAX_BACKOFF_LEVEL;
    }
    state->in_backoff = 1;
}

// Check if we're currently in backoff mode
static __always_inline __u8 backoff_in_backoff(struct backoff_state *state) {
    return state->in_backoff;
}

// Determine if we should try the operation based on random input
static __always_inline __u8 backoff_should_try(struct backoff_state *state, __u32 random_value) {
    if (!state->in_backoff) {
        return 1;
    }

    // Calculate probability threshold (1/2^level)
    __u32 threshold = 1 << state->consecutive_failures;
    
    // Use random value to determine if we should try
    return (random_value % threshold) == 0;
}

#endif // BACKOFF_H 