#ifndef __TASK_METADATA_BPF_H
#define __TASK_METADATA_BPF_H

#include "vmlinux.h"
#include "protocol.bpf.h"

struct task_metadata_storage_map {
    __uint(type, BPF_MAP_TYPE_TASK_STORAGE);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, __u32);
    __type(value, __u64);
};
extern struct task_metadata_storage_map task_metadata_storage SEC(".maps");


// Helper function to check if a task is a kernel thread
static __always_inline int is_kernel_thread(struct task_struct *task) {
    if (!task)
        return 0;
    
    // Kernel threads either have PF_KTHREAD flag or no mm
    return (task->flags & 0x00200000 /* PF_KTHREAD */) || !task->mm;
}

// Check if we should send metadata for this task (without actually sending)
// Returns 1 if metadata should be sent, 0 if not
// Check if metadata needs to be reported (without reporting it)
static __always_inline int should_send_task_metadata(struct task_struct *task) {
    if (!task)
        return 0;
    
    // Skip kernel threads (redundant check, also done in the caller)
    if (is_kernel_thread(task))
        return 0;
    
    // Get or create metadata tracking entry
    __u64 *reported = bpf_task_storage_get(&task_metadata_storage, task, 
                                          NULL, 
                                          BPF_LOCAL_STORAGE_GET_F_CREATE);
    
    if (!reported)
        return 0;
    
    // Use atomic compare-and-swap to check and update the reported status
    // If it returns 0, we were the ones who changed it from 0->1
    if (__sync_val_compare_and_swap(reported, 0, 1) == 0) {
        // We're the first to report this task
        return 1;
    }
    
    return 0;
}

// Check if task metadata needs to be reported to userspace and report if needed
// Returns 1 if metadata was reported, 0 if not
static __always_inline int send_task_metadata_if_needed(void *ctx, struct task_struct *task) {
    if (!task)
        return 0;
    
    // Skip kernel threads
    if (is_kernel_thread(task))
        return 0;
    
    // Use group leader
    struct task_struct *leader = task->group_leader;
    if (!leader)
        return 0;
    
    // Check if we should send metadata
    if (should_send_task_metadata(leader)) {
        // We need to send metadata
        __u64 timestamp = bpf_ktime_get_ns();
        send_task_metadata(ctx, leader->pid, leader->comm, timestamp);
        return 1;
    }
    
    return 0;
}

#endif /* __TASK_METADATA_BPF_H */ 