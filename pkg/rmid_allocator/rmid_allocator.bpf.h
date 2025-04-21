#ifndef __RMID_ALLOCATOR_BPF_H
#define __RMID_ALLOCATOR_BPF_H

#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define MAX_RMIDS 1024
#define TASK_COMM_LEN 16

// Free list entry structure
struct rmid_free_entry {
    __u32 rmid;
    __u64 free_timestamp;
};

// Allocator state structure
struct rmid_allocator {
    __u32 num_rmids;         // Number of RMIDs to allocate (1-based)
    __u64 min_free_time_ns;  // Minimum time before RMID reuse
    __u64 free_head;         // Growing index of head in free circular buffer
    __u64 free_tail;         // Growing index of tail in free circular buffer
    __u8 is_allocated[MAX_RMIDS];
    struct rmid_free_entry free_list[MAX_RMIDS];
};

// Function declarations
__u8 rmid_is_allocated(struct rmid_allocator *allocator, __u32 rmid);
__u32 rmid_alloc(struct rmid_allocator *allocator, __u64 timestamp);
void rmid_free(struct rmid_allocator *allocator, __u32 rmid, __u64 timestamp);
__u8 rmid_init(struct rmid_allocator *allocator, __u32 num_rmids, __u64 min_free_time_ns);

#endif /* __RMID_ALLOCATOR_BPF_H */ 