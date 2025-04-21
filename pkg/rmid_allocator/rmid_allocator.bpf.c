//go:build ignore

#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "rmid_allocator.bpf.h"

// Helper function to check if an RMID is valid
static __always_inline __u8 rmid_is_valid(struct rmid_allocator *allocator, __u32 rmid) {
    return (rmid != 0 && rmid <= allocator->num_rmids) ? 1 : 0;
}

// Helper function to check if an RMID is allocated
__u8 rmid_is_allocated(struct rmid_allocator *allocator, __u32 rmid) {
    if (!allocator)
        return 0;
        
    if (!rmid_is_valid(allocator, rmid))
        return 0;
        
    return allocator->is_allocated[rmid];
}

// Function to initialize the allocator
__u8 rmid_init(struct rmid_allocator *allocator, __u32 num_rmids, __u64 min_free_time_ns) {
    if (!allocator)
        return 0;
        
    // Validate num_rmids is within bounds
    if (num_rmids == 0 || num_rmids > MAX_RMIDS)
        return 0;
        
    allocator->num_rmids = num_rmids;
    allocator->min_free_time_ns = min_free_time_ns;
    allocator->free_head = 0;
    allocator->free_tail = 0;
    
    // Initialize free list with all valid RMIDs
    for (__u32 i = 1; i <= num_rmids; i++) {
        allocator->free_list[i-1].rmid = i;
        allocator->free_list[i-1].free_timestamp = 0;
    }
    allocator->free_tail = num_rmids;
    
    return 1;
}

// Function to allocate an RMID
__u32 rmid_alloc(struct rmid_allocator *allocator, __u64 timestamp) {
    if (!allocator)
        return 0;
        
    __u32 rmid;
    
    // Check if there are any free RMIDs by comparing head and tail
    if (allocator->free_head == allocator->free_tail)
        return 0;
        
    // Get next free RMID from circular buffer
    struct rmid_free_entry *entry = &allocator->free_list[allocator->free_head % MAX_RMIDS];
    
    // Check if enough time has passed since this RMID was freed
    if (timestamp - entry->free_timestamp < allocator->min_free_time_ns)
        return 0;
        
    rmid = entry->rmid;
    
    // Update free head (let it grow)
    allocator->free_head++;
    
    // Mark as allocated
    allocator->is_allocated[rmid] = 1;
    
    return rmid;
}

// Function to free an RMID
void rmid_free(struct rmid_allocator *allocator, __u32 rmid, __u64 timestamp) {
    if (!allocator)
        return;
        
    if (!rmid_is_valid(allocator, rmid) || !rmid_is_allocated(allocator, rmid))
        return;
        
    // Mark as free
    allocator->is_allocated[rmid] = 0;
    
    // Add to free list
    struct rmid_free_entry *entry = &allocator->free_list[allocator->free_tail % MAX_RMIDS];
    entry->rmid = rmid;
    entry->free_timestamp = timestamp;
    allocator->free_tail++;
}

char _license[] SEC("license") = "GPL"; 