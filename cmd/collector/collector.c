//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/sched.h>

#define TASK_COMM_LEN 16

// Message types for all events
enum msg_type {
    MSG_TYPE_PERF = 0,
    MSG_TYPE_RMID_ALLOC = 1,
    MSG_TYPE_RMID_FREE = 2,
};
const enum msg_type *unused_bpf2go_generate_msg_type __attribute__((unused)); // force golang generation of the enum

// Define the event structure that matches the Go side
struct perf_measurement_msg {
    __u64 timestamp;  // Must be first field for ring buffer ordering
    __u32 type;      // MSG_TYPE_PERF
    __u32 rmid;
    __u64 cycles_delta;
    __u64 instructions_delta;
    __u64 llc_misses_delta;
    __u64 time_delta_ns;
};
const struct perf_measurement_msg *unused_bpf2go_generate_perf_measurement_msg __attribute__((unused)); // force golang generation of the struct

// Structure for RMID allocation messages
struct rmid_alloc_msg {
    __u64 timestamp;  // Must be first field for ring buffer ordering
    __u32 type;      // MSG_TYPE_RMID_ALLOC
    __u32 rmid;
    char comm[TASK_COMM_LEN];
    __u32 tgid;
};
const struct rmid_alloc_msg *unused_bpf2go_generate_rmid_alloc_msg __attribute__((unused)); // force golang generation of the struct

// Structure for RMID free messages
struct rmid_free_msg {
    __u64 timestamp;  // Must be first field for ring buffer ordering
    __u32 type;      // MSG_TYPE_RMID_FREE
    __u32 rmid;
};
const struct rmid_free_msg *unused_bpf2go_generate_rmid_free_msg __attribute__((unused)); // force golang generation of the struct

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

// Perf event array for all events
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, 0);
    __uint(value_size, 0);
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

// Helper function to send RMID allocation message
static void send_rmid_alloc(void *ctx, __u32 rmid, const char *comm, __u32 tgid, __u64 timestamp) {
    struct rmid_alloc_msg msg = {
        .timestamp = timestamp,
        .type = MSG_TYPE_RMID_ALLOC,
        .rmid = rmid,
        .tgid = tgid,
    };
    __builtin_memcpy(msg.comm, comm, TASK_COMM_LEN);
    
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &msg, sizeof(msg));
}

// Helper function to send RMID free message
static void send_rmid_free(void *ctx, __u32 rmid, __u64 timestamp) {
    struct rmid_free_msg msg = {
        .timestamp = timestamp,
        .type = MSG_TYPE_RMID_FREE,
        .rmid = rmid,
    };
    
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &msg, sizeof(msg));
}

// Handler for RMID allocation events
SEC("tracepoint/memory_collector/rmid_alloc")
int handle_rmid_alloc(struct rmid_alloc_args *ctx) {
    send_rmid_alloc(ctx, ctx->rmid, ctx->comm, ctx->tgid, ctx->timestamp);
    return 0;
}

// Handler for RMID deallocation events
SEC("tracepoint/memory_collector/rmid_free")
int handle_rmid_free(struct rmid_free_args *ctx) {
    send_rmid_free(ctx, ctx->rmid, ctx->timestamp);
    return 0;
}

// Handler for existing RMID dump events
SEC("tracepoint/memory_collector/rmid_existing")
int handle_rmid_existing(struct rmid_existing_args *ctx) {
    send_rmid_alloc(ctx, ctx->rmid, ctx->comm, ctx->tgid, ctx->timestamp);
    return 0;
}

SEC("tracepoint/memory_collector/measure_perf_counters")
int measure_perf(void *ctx) {
    struct perf_measurement_msg e = {
        .type = MSG_TYPE_PERF,
    };
    
    // Extract RMID from the tracepoint context
    struct {
        __u64 pad;  // Common fields in tracepoint
        __u8 is_context_switch;
        __u32 rmid;
    } *args = ctx;
    
    e.rmid = args->rmid;
    
    __u64 now;
    
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
    now = bpf_ktime_get_ns();
    // if prev->timestamp is 0, this is the first event. We did not have the counter and timestamp values,
    // so do not emit this event -- only use it to initialize the counters
    if (prev->timestamp != 0) {
        e.time_delta_ns = compute_delta(now, prev->timestamp);
        e.timestamp = now;
        // Submit the event to the perf event array
        bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e));
    }
    prev->timestamp = now;
    
    increase_count(ctx);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL";