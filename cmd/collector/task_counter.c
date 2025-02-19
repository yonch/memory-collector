//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/sched.h>

#define TASK_COMM_LEN 16
#define MAX_RMID 512  // Match with max_entries in rmid_map

// Define the event structure that matches the Go side
struct event {
    __u64 counter;
    __u64 cycles;
    __u64 instructions;
    __u64 llc_misses;
};

// Tracepoint event structs
struct rmid_alloc_args {
    __u64 trace_entry;
    __u32 rmid;
    char comm[TASK_COMM_LEN];
    __u32 tgid;
    __u64 timestamp;
};

struct rmid_free_args {
    __u64 trace_entry;
    __u32 rmid;
    __u64 timestamp;
};

struct rmid_existing_args {
    __u64 trace_entry;
    __u32 rmid;
    char comm[TASK_COMM_LEN];
    __u32 tgid;
    __u64 timestamp;
};

// Structure for RMID allocation metadata
struct rmid_metadata {
    char comm[TASK_COMM_LEN];
    __u32 tgid;
    __u64 timestamp;  // Single timestamp field for all events
    __u8 valid;       // Whether this RMID is currently valid
};

// Declare maps for RMID tracking
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 512);
    __type(key, __u32);  // RMID
    __type(value, struct rmid_metadata);
} rmid_map SEC(".maps");

// Declare the perf event arrays
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} cycles SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} instructions SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} llc_misses SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, 0);
    __uint(value_size, 0);
    __type(value, struct event);
} events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} event_count SEC(".maps");

void increase_count(void *ctx) {
    __u32 key = 0;
    __u64 *count = bpf_map_lookup_elem(&event_count, &key);
    if (count) {
        __sync_fetch_and_add(count, 1);
    }
}

// Handler for RMID allocation events
SEC("tracepoint/memory_collector/memory_collector_rmid_alloc")
int handle_rmid_alloc(struct rmid_alloc_args *ctx) {
    struct rmid_metadata meta = {};
    struct rmid_metadata *existing;
    
    // Check RMID bounds
    if (ctx->rmid >= MAX_RMID) {
        bpf_trace_printk("RMID %u exceeds maximum allowed value %u\\n", ctx->rmid, MAX_RMID-1);
        return 0;
    }
    
    // Look up existing metadata
    existing = bpf_map_lookup_elem(&rmid_map, &ctx->rmid);
    
    // Only update if:
    // 1. No existing entry OR
    // 2. Existing entry is invalid (freed) OR
    // 3. New timestamp is newer than existing timestamp
    if (!existing || 
        existing->timestamp < ctx->timestamp) {
        
        // Copy data from tracepoint
        __builtin_memcpy(meta.comm, ctx->comm, TASK_COMM_LEN);
        meta.tgid = ctx->tgid;
        meta.timestamp = ctx->timestamp;
        meta.valid = 1;

        bpf_map_update_elem(&rmid_map, &ctx->rmid, &meta, BPF_ANY);
    }
    return 0;
}

// Handler for RMID deallocation events
SEC("tracepoint/memory_collector/memory_collector_rmid_free")
int handle_rmid_free(struct rmid_free_args *ctx) {
    struct rmid_metadata *existing;
    struct rmid_metadata meta = {};
    
    // Check RMID bounds
    if (ctx->rmid >= MAX_RMID) {
        bpf_trace_printk("RMID %u exceeds maximum allowed value %u\\n", ctx->rmid, MAX_RMID-1);
        return 0;
    }
    
    // Look up existing metadata
    existing = bpf_map_lookup_elem(&rmid_map, &ctx->rmid);
    
    // Only update if:
    // 1. No existing entry OR
    // 2. New timestamp is newer than existing timestamp
    if (!existing || existing->timestamp < ctx->timestamp) {
        meta.timestamp = ctx->timestamp;
        meta.valid = 0;

        bpf_map_update_elem(&rmid_map, &ctx->rmid, &meta, BPF_ANY);
    }
    return 0;
}

// Handler for existing RMID dump events
SEC("tracepoint/memory_collector/memory_collector_rmid_existing")
int handle_rmid_existing(struct rmid_existing_args *ctx) {
    struct rmid_metadata meta = {};
    struct rmid_metadata *existing;
    
    // Check RMID bounds
    if (ctx->rmid >= MAX_RMID) {
        bpf_trace_printk("RMID %u exceeds maximum allowed value %u\\n", ctx->rmid, MAX_RMID-1);
        return 0;
    }
    
    // Look up existing metadata
    existing = bpf_map_lookup_elem(&rmid_map, &ctx->rmid);
    
    // Only update if:
    // 1. No existing entry OR
    // 2. Existing entry is invalid (freed) OR
    // 3. New timestamp is newer than existing timestamp
    if (!existing || 
        existing->timestamp < ctx->timestamp) {
        
        // Copy data from tracepoint
        __builtin_memcpy(meta.comm, ctx->comm, TASK_COMM_LEN);
        meta.tgid = ctx->tgid;
        meta.timestamp = ctx->timestamp;
        meta.valid = 1;

        bpf_map_update_elem(&rmid_map, &ctx->rmid, &meta, BPF_ANY);
    }
    return 0;
}

SEC("tracepoint/memory_collector/memory_collector_sample")
int count_events(void *ctx) {
    struct event e = {};
    e.counter = 1;
    
    // Read cycles from perf event
    struct bpf_perf_event_value cycles_val = {};
    int err = bpf_perf_event_read_value(&cycles, BPF_F_CURRENT_CPU, &cycles_val, sizeof(cycles_val));
    if (err == 0) {
        e.cycles = cycles_val.counter;
    }

    // Read instructions from perf event
    struct bpf_perf_event_value instructions_val = {};
    err = bpf_perf_event_read_value(&instructions, BPF_F_CURRENT_CPU, &instructions_val, sizeof(instructions_val));
    if (err == 0) {
        e.instructions = instructions_val.counter;
    }

    // Read LLC misses from perf event
    struct bpf_perf_event_value llc_misses_val = {};
    err = bpf_perf_event_read_value(&llc_misses, BPF_F_CURRENT_CPU, &llc_misses_val, sizeof(llc_misses_val));
    if (err == 0) {
        e.llc_misses = llc_misses_val.counter;
    }

    // Submit the event to the perf event array
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e));

    increase_count(ctx);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL";