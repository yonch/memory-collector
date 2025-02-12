//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

// Define the event structure that matches the Go side
struct event {
    __u64 counter;
};

// Declare the perf event array
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, 0);
    __uint(value_size, 0);
    __type(value, struct event);
} events SEC(".maps");

SEC("tracepoint/memory_collector/memory_collector_sample")
int count_events(void *ctx) {
    struct event e = {};
    e.counter = 1;

    // Submit the event to the perf event array
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &e, sizeof(e));
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL";