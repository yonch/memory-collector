//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

// Define the event structure that matches the Go side
struct event {
    __u64 counter;
    __u64 cycles;
    __u64 instructions;
    __u64 llc_misses;
};

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