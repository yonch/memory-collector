//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "task_metadata.bpf.h"
#include "protocol.bpf.h"

struct task_metadata_storage_map task_metadata_storage SEC(".maps");

// Initialize value to be stored in task storage for new tasks
static const __u64 TASK_METADATA_INIT = 0;  // 0 = not reported yet



// Handle process exit
SEC("raw_tp/sched_process_free")
int handle_process_free(u64 *ctx) {
    struct task_struct *task = bpf_get_current_task_btf();

    if (!task)
        return 0;

    // Get or create metadata tracking entry
    __u64 *reported = bpf_task_storage_get(&task_metadata_storage, task, NULL, 0);
    if (!reported)
        return 0;
    if (*reported != 1)
        return 0;
    
    // Report task free event
    __u64 timestamp = bpf_ktime_get_ns();
    send_task_free(ctx, task->pid, timestamp);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL"; 