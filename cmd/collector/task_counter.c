//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} event_count SEC(".maps");

SEC("tracepoint/memory_collector/memory_collector_sample")
int count_events(void *ctx) {
    __u32 key = 0;
    __u64 *count = bpf_map_lookup_elem(&event_count, &key);
    if (count) {
        __sync_fetch_and_add(count, 1);
    }
    return 0;
}

char LICENSE[] SEC("license") = "Dual MIT/GPL";