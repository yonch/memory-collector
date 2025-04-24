#ifndef __RMID_ALLOCATOR_BPF_H
#define __RMID_ALLOCATOR_BPF_H

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

// Helper function to check if an RMID is valid
static __always_inline __u32 rmid_is_valid(struct rmid_allocator *allocator, __u32 rmid) {
    return (rmid != 0 && rmid < allocator->num_rmids);
}

// Helper function to check if an RMID is allocated
static __always_inline __u8 rmid_is_allocated(struct rmid_allocator *allocator, __u32 rmid) {
    if (!allocator)
        return 0;
        
    if (rmid >= MAX_RMIDS)
        return 0;
        
    return allocator->is_allocated[rmid];
}

// Function to initialize the allocator
static __always_inline __u8 rmid_init(struct rmid_allocator *allocator, __u32 num_rmids, __u64 min_free_time_ns) {
    if (!allocator)
        return 0;
        
    // Validate num_rmids is within bounds
    if (num_rmids == 0 || num_rmids > MAX_RMIDS)
        return 0;
        
    allocator->num_rmids = num_rmids;
    allocator->min_free_time_ns = min_free_time_ns;
    allocator->free_head = 1;
    allocator->free_tail = num_rmids;
    
    // Initialize free list with all RMIDs
    for (__u32 i = 0; i < MAX_RMIDS; i++) {
        allocator->free_list[i].rmid = i;
        allocator->free_list[i].free_timestamp = 0;
    }
    
    return 1;
}

// Function to allocate an RMID
static __always_inline __u32 rmid_alloc(struct rmid_allocator *allocator, __u64 timestamp) {
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
    if (rmid > MAX_RMIDS)
        return 0;
    
    // Update free head (let it grow)
    allocator->free_head++;
    
    // Mark as allocated
    allocator->is_allocated[rmid] = 1;
    
    return rmid;
}

// Function to free an RMID
static __always_inline __s64 rmid_free(struct rmid_allocator *allocator, __u32 rmid, __u64 timestamp) {
    if (!allocator)
        return -1;
        
    if (!rmid_is_valid(allocator, rmid))
        return -1;

    if (!rmid_is_allocated(allocator, rmid))
        return -1;

    if (rmid >= MAX_RMIDS)
        return -1;

    // Mark as free
    allocator->is_allocated[rmid] = 0;
    
    // Add to free list
    struct rmid_free_entry *entry = &allocator->free_list[allocator->free_tail % MAX_RMIDS];
    entry->rmid = rmid;
    entry->free_timestamp = timestamp;
    allocator->free_tail++;

    return 0;
}

#endif /* __RMID_ALLOCATOR_BPF_H */ 