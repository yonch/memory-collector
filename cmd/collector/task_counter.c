//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/sched.h>

#define TASK_COMM_LEN 16
#define MAX_RMID 512  // Match with max_entries in rmid_map

// Define the event structure that matches the Go side
struct event {
    __u64 counter;
    __u64 cycles_delta;
    __u64 instructions_delta;
    __u64 llc_misses_delta;
    __u64 time_delta_ns;
};

// Structure to store previous counter values per CPU
struct prev_counters {
    __u64 cycles;
    __u64 instructions;
    __u64 llc_misses;
    __u64 timestamp;
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

// Per-CPU map to store previous counter values
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct prev_counters);
} prev_counters_map SEC(".maps");

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

// Helper function to compute delta with wraparound handling
static __u64 compute_delta(__u64 current, __u64 previous) {
    return current - previous;
}

// Handler for RMID allocation events
SEC("tracepoint/memory_collector/memory_collector_rmid_alloc")
int handle_rmid_alloc(struct rmid_alloc_args *ctx) {
    struct rmid_metadata meta = {};
    struct rmid_metadata *existing;
    int rmid = ctx->rmid;
    
    // Check RMID bounds
    if (rmid >= MAX_RMID) {
        bpf_printk("unvariance_collector: handle_rmid_alloc: RMID %u exceeds maximum allowed value %u\\n", rmid, MAX_RMID-1);
        return 0;
    }
    
    // Look up existing metadata
    existing = bpf_map_lookup_elem(&rmid_map, &rmid);
    
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

        bpf_map_update_elem(&rmid_map, &rmid, &meta, BPF_ANY);
    }
    return 0;
}

// Handler for RMID deallocation events
SEC("tracepoint/memory_collector/memory_collector_rmid_free")
int handle_rmid_free(struct rmid_free_args *ctx) {
    struct rmid_metadata *existing;
    struct rmid_metadata meta = {};
    int rmid = ctx->rmid;
    
    // Check RMID bounds
    if (rmid >= MAX_RMID) {
        bpf_printk("unvariance_collector: handle_rmid_free: RMID %u exceeds maximum allowed value %u\\n", rmid, MAX_RMID-1);
        return 0;
    }
    
    // Look up existing metadata
    existing = bpf_map_lookup_elem(&rmid_map, &rmid);
    
    // Only update if:
    // 1. No existing entry OR
    // 2. New timestamp is newer than existing timestamp
    if (!existing || existing->timestamp < ctx->timestamp) {
        meta.timestamp = ctx->timestamp;
        meta.valid = 0;

        bpf_map_update_elem(&rmid_map, &rmid, &meta, BPF_ANY);
    }
    return 0;
}

// Handler for existing RMID dump events
SEC("tracepoint/memory_collector/memory_collector_rmid_existing")
int handle_rmid_existing(struct rmid_existing_args *ctx) {
    struct rmid_metadata meta = {};
    struct rmid_metadata *existing;
    int rmid = ctx->rmid;
    
    if (rmid >= MAX_RMID) {
        bpf_printk("unvariance_collector: handle_rmid_existing: RMID %u exceeds maximum allowed value %u\\n", rmid, MAX_RMID-1);
        return 0;
    }
    
    // Look up existing metadata
    existing = bpf_map_lookup_elem(&rmid_map, &rmid);
    
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

        bpf_map_update_elem(&rmid_map, &rmid, &meta, BPF_ANY);
    }
    return 0;
}

SEC("tracepoint/memory_collector/memory_collector_sample")
int count_events(void *ctx) {
    struct event e = {};
    e.counter = 1;
    
    // Get current timestamp
    __u64 now = bpf_ktime_get_ns();
    
    // Get previous counters
    __u32 zero = 0;
    struct prev_counters *prev = bpf_map_lookup_elem(&prev_counters_map, &zero);
    if (!prev) {
        return 0;  // Should never happen since it's a per-CPU array
    }

    // Read current counter values
    struct bpf_perf_event_value cycles_val = {};
    struct bpf_perf_event_value instructions_val = {};
    struct bpf_perf_event_value llc_misses_val = {};
    
    int err = bpf_perf_event_read_value(&cycles, BPF_F_CURRENT_CPU, &cycles_val, sizeof(cycles_val));
    if (err == 0) {
        e.cycles_delta = compute_delta(cycles_val.counter, prev->cycles);
        prev->cycles = cycles_val.counter;
    }

    err = bpf_perf_event_read_value(&instructions, BPF_F_CURRENT_CPU, &instructions_val, sizeof(instructions_val));
    if (err == 0) {
        e.instructions_delta = compute_delta(instructions_val.counter, prev->instructions);
        prev->instructions = instructions_val.counter;
    }

    err = bpf_perf_event_read_value(&llc_misses, BPF_F_CURRENT_CPU, &llc_misses_val, sizeof(llc_misses_val));
    if (err == 0) {
        e.llc_misses_delta = compute_delta(llc_misses_val.counter, prev->llc_misses);
        prev->llc_misses = llc_misses_val.counter;
    }

    // Compute time delta and update timestamp
    e.time_delta_ns = compute_delta(now, prev->timestamp);
    prev->timestamp = now;

    // Submit the event to the perf event array
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e));

    increase_count(ctx);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL";