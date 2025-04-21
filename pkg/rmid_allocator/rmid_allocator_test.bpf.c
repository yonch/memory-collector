//go:build ignore

#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "rmid_allocator.bpf.h"

// Make bpf2go wrap this variable, for the test that checks if num_rmids is within bounds
const __u32 max_rmids = MAX_RMIDS;

// Map to store the allocator
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct rmid_allocator);
} rmid_allocator_map SEC(".maps");

// Input/output structures for test functions
struct rmid_init_input {
    __u32 num_rmids;
    __u64 min_free_time_ns;
};
const struct rmid_init_input *unused_bpf2go_generate_rmid_init_input __attribute__((unused));

struct rmid_init_output {
    __u8 success;
};
const struct rmid_init_output *unused_bpf2go_generate_rmid_init_output __attribute__((unused));

struct rmid_alloc_input {
    __u64 timestamp;
};
const struct rmid_alloc_input *unused_bpf2go_generate_rmid_alloc_input __attribute__((unused));

struct rmid_alloc_output {
    __u32 rmid;
};
const struct rmid_alloc_output *unused_bpf2go_generate_rmid_alloc_output __attribute__((unused));

struct rmid_free_input {
    __u32 rmid;
    __u64 timestamp;
};
const struct rmid_free_input *unused_bpf2go_generate_rmid_free_input __attribute__((unused));

struct rmid_free_output {
    __u8 success;
};
const struct rmid_free_output *unused_bpf2go_generate_rmid_free_output __attribute__((unused));

struct rmid_is_allocated_input {
    __u32 rmid;
};
const struct rmid_is_allocated_input *unused_bpf2go_generate_rmid_is_allocated_input __attribute__((unused));

struct rmid_is_allocated_output {
    __u8 allocated;
};
const struct rmid_is_allocated_output *unused_bpf2go_generate_rmid_is_allocated_output __attribute__((unused));

// Helper to get allocator
static __always_inline struct rmid_allocator *get_allocator() {
    __u32 key = 0;
    return bpf_map_lookup_elem(&rmid_allocator_map, &key);
}

// Test wrapper for rmid_init
SEC("xdp")
int test_rmid_init(struct xdp_md *ctx) {
    struct rmid_init_input input;
    struct rmid_init_output output;
    struct rmid_allocator *allocator;
    
    // Get allocator
    allocator = get_allocator();
    if (!allocator)
        return XDP_ABORTED;
    
    // Read input from packet data
    if (bpf_xdp_load_bytes(ctx, 0, &input, sizeof(input)) < 0)
        return XDP_ABORTED;
        
    // Call the actual function and store result
    output.success = rmid_init(allocator, input.num_rmids, input.min_free_time_ns);
    
    // Write output to packet data
    if (bpf_xdp_store_bytes(ctx, 0, &output, sizeof(output)) < 0)
        return XDP_ABORTED;
        
    return XDP_PASS;
}

// Test wrapper for rmid_alloc
SEC("xdp")
int test_rmid_alloc(struct xdp_md *ctx) {
    struct rmid_alloc_input input;
    struct rmid_alloc_output output;
    struct rmid_allocator *allocator;
    
    // Get allocator
    allocator = get_allocator();
    if (!allocator)
        return XDP_ABORTED;
    
    // Read input from packet data
    if (bpf_xdp_load_bytes(ctx, 0, &input, sizeof(input)) < 0)
        return XDP_ABORTED;
        
    // Call the actual function
    output.rmid = rmid_alloc(allocator, input.timestamp);
    
    // Write output to packet data
    if (bpf_xdp_store_bytes(ctx, 0, &output, sizeof(output)) < 0)
        return XDP_ABORTED;
        
    return XDP_PASS;
}

// Test wrapper for rmid_free
SEC("xdp")
int test_rmid_free(struct xdp_md *ctx) {
    struct rmid_free_input input;
    struct rmid_free_output output;
    struct rmid_allocator *allocator;
    
    // Get allocator
    allocator = get_allocator();
    if (!allocator)
        return XDP_ABORTED;
    
    // Read input from packet data
    if (bpf_xdp_load_bytes(ctx, 0, &input, sizeof(input)) < 0)
        return XDP_ABORTED;
        
    // Call the actual function
    rmid_free(allocator, input.rmid, input.timestamp);
    output.success = 1;
    
    // Write output to packet data
    if (bpf_xdp_store_bytes(ctx, 0, &output, sizeof(output)) < 0)
        return XDP_ABORTED;
        
    return XDP_PASS;
}

// Test wrapper for rmid_is_allocated
SEC("xdp")
int test_rmid_is_allocated(struct xdp_md *ctx) {
    struct rmid_is_allocated_input input;
    struct rmid_is_allocated_output output;
    struct rmid_allocator *allocator;
    
    // Get allocator
    allocator = get_allocator();
    if (!allocator)
        return XDP_ABORTED;
    
    // Read input from packet data
    if (bpf_xdp_load_bytes(ctx, 0, &input, sizeof(input)) < 0)
        return XDP_ABORTED;
        
    // Call the actual function
    output.allocated = rmid_is_allocated(allocator, input.rmid);
    
    // Write output to packet data
    if (bpf_xdp_store_bytes(ctx, 0, &output, sizeof(output)) < 0)
        return XDP_ABORTED;
        
    return XDP_PASS;
}

char _license[] SEC("license") = "GPL"; 