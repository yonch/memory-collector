//go:build ignore

#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "backoff.h"

// Map to store the backoff state
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct backoff_state);
} backoff_state_map SEC(".maps");

// Input/output structures for wrap_backoff_should_try
struct backoff_should_try_input {
    __u32 random_value;
};
const struct backoff_should_try_input *unused_bpf2go_generate_backoff_should_try_input __attribute__((unused));

struct backoff_should_try_output {
    __u8 should_try;
};
const struct backoff_should_try_output *unused_bpf2go_generate_backoff_should_try_output __attribute__((unused));

// Helper to get backoff state
static __always_inline struct backoff_state *get_backoff_state() {
    __u32 key = 0;
    return bpf_map_lookup_elem(&backoff_state_map, &key);
}

// Wrapper for backoff_init
SEC("xdp")
int wrap_backoff_init(struct xdp_md *ctx) {
    struct backoff_state *state = get_backoff_state();
    if (!state)
        return XDP_ABORTED;
    
    backoff_init(state);
    return XDP_PASS;
}

// Wrapper for backoff_update_success
SEC("xdp")
int wrap_backoff_update_success(struct xdp_md *ctx) {
    struct backoff_state *state = get_backoff_state();
    if (!state)
        return XDP_ABORTED;
    
    backoff_update_success(state);
    return XDP_PASS;
}

// Wrapper for backoff_update_failure
SEC("xdp")
int wrap_backoff_update_failure(struct xdp_md *ctx) {
    struct backoff_state *state = get_backoff_state();
    if (!state)
        return XDP_ABORTED;
    
    backoff_update_failure(state);
    return XDP_PASS;
}

// Wrapper for backoff_in_backoff
SEC("xdp")
int wrap_backoff_in_backoff(struct xdp_md *ctx) {
    struct backoff_state *state = get_backoff_state();
    if (!state)
        return XDP_ABORTED;
    
    __u8 in_backoff = backoff_in_backoff(state);
    if (bpf_xdp_store_bytes(ctx, 0, &in_backoff, sizeof(in_backoff)) < 0)
        return XDP_ABORTED;
        
    return XDP_PASS;
}

// Wrapper for backoff_should_try
SEC("xdp")
int wrap_backoff_should_try(struct xdp_md *ctx) {
    struct backoff_should_try_input input;
    struct backoff_state *state;
    
    state = get_backoff_state();
    if (!state)
        return XDP_ABORTED;
    
    if (bpf_xdp_load_bytes(ctx, 0, &input, sizeof(input)) < 0)
        return XDP_ABORTED;
    
    __u8 should_try = backoff_should_try(state, input.random_value);
    if (bpf_xdp_store_bytes(ctx, 0, &should_try, sizeof(should_try)) < 0)
        return XDP_ABORTED;
        
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL"; 