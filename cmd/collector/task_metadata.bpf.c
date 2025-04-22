//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "task_metadata.bpf.h"
#include "protocol.bpf.h"

// Map to track which tasks have had metadata reported
struct {
    __uint(type, BPF_MAP_TYPE_TASK_STORAGE);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, __u32);
    __type(value, __u64);
} task_metadata_storage SEC(".maps");

// Initialize value to be stored in task storage for new tasks
static const __u64 TASK_METADATA_INIT = 0;  // 0 = not reported yet

// Check if metadata needs to be reported (without reporting it)
int should_send_task_metadata(struct task_struct *task) {
    if (!task)
        return 0;
    
    // Skip kernel threads (redundant check, also done in the caller)
    if (is_kernel_thread(task))
        return 0;
    
    // Get or create metadata tracking entry
    __u64 *reported = bpf_task_storage_get(&task_metadata_storage, task, 
                                          (void *)&TASK_METADATA_INIT, 
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

// Handle process exit
SEC("raw_tp/sched_process_free")
int handle_process_free(u64 *ctx) {
    struct task_struct *task = (void *)ctx[0];
    
    // Skip if not a group leader or kernel thread
    if (!task || task != task->group_leader || is_kernel_thread(task))
        return 0;
    
    // Report task free event
    __u64 timestamp = bpf_ktime_get_ns();
    send_task_free(ctx, task->pid, timestamp);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL"; 