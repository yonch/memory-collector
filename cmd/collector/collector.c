//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "protocol.bpf.h"
#include "task_metadata.bpf.h"


// Structure to store previous counter values per CPU
struct prev_counters {
    __u64 cycles;
    __u64 instructions;
    __u64 llc_misses;
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

SEC("tracepoint/sched/sched_switch")
int measure_perf(struct trace_event_raw_sched_switch *ctx) {
    // Get current task before checking counters
    struct task_struct *current_task = bpf_get_current_task_btf();
    
    // Check and report task metadata if needed
    send_task_metadata_if_needed(ctx, current_task);

    // report using the PID of the task leader
    __u32 pid = current_task->group_leader->pid;
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
    
    struct perf_measurement_params params = {
        .pid = pid  // Use pid instead of rmid
    };
    
    int err = bpf_perf_event_read_value(&cycles, BPF_F_CURRENT_CPU, &cycles_val, sizeof(cycles_val));
    if (err == 0) {
        params.cycles_delta = compute_delta(cycles_val.counter, prev->cycles);
        prev->cycles = cycles_val.counter;
    }

    err = bpf_perf_event_read_value(&instructions, BPF_F_CURRENT_CPU, &instructions_val, sizeof(instructions_val));
    if (err == 0) {
        params.instructions_delta = compute_delta(instructions_val.counter, prev->instructions);
        prev->instructions = instructions_val.counter;
    }

    err = bpf_perf_event_read_value(&llc_misses, BPF_F_CURRENT_CPU, &llc_misses_val, sizeof(llc_misses_val));
    if (err == 0) {
        params.llc_misses_delta = compute_delta(llc_misses_val.counter, prev->llc_misses);
        prev->llc_misses = llc_misses_val.counter;
    }

    // Compute time delta and update timestamp
    now = bpf_ktime_get_ns();
    // if prev->timestamp is 0, this is the first event. We did not have the counter and timestamp values,
    // so do not emit this event -- only use it to initialize the counters
    if (prev->timestamp != 0) {
        params.time_delta_ns = compute_delta(now, prev->timestamp);
        params.timestamp = now;
        // Submit the event using the helper function
        send_perf_measurement(ctx, &params);
    }
    prev->timestamp = now;
    
    increase_count(ctx);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";