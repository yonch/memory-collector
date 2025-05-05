// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

// Map to store the cgroup ID for retrieval in userspace
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} cgroup_id_map SEC(".maps");

// Function to retrieve the current cgroup ID and store it in the map
SEC("syscall")
int get_cgroup_id(void *ctx) {
    // Use key 0 for simple access
    __u32 key = 0;
    
    // Get the cgroup ID of the current task
    __u64 cgroup_id = bpf_get_current_cgroup_id();
    
    // Store the cgroup ID in the map for retrieval by userspace
    bpf_map_update_elem(&cgroup_id_map, &key, &cgroup_id, BPF_ANY);
    
    return 0;
}

// License required for BPF programs
char LICENSE[] SEC("license") = "GPL"; 