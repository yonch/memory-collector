#ifndef __TASK_METADATA_BPF_H
#define __TASK_METADATA_BPF_H

#include "vmlinux.h"
#include "protocol.bpf.h"

// Check if we should send metadata for this task (without actually sending)
// Returns 1 if metadata should be sent, 0 if not
int should_send_task_metadata(struct task_struct *task);

// Helper function to check if a task is a kernel thread
static __always_inline int is_kernel_thread(struct task_struct *task) {
    if (!task)
        return 0;
    
    // Kernel threads either have PF_KTHREAD flag or no mm
    return (task->flags & 0x00200000 /* PF_KTHREAD */) || !task->mm;
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