// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "collector.h"

// Map to track which tasks have had metadata reported
struct {
    __uint(type, BPF_MAP_TYPE_TASK_STORAGE);
    __uint(map_flags, BPF_F_NO_PREALLOC);
    __type(key, int);
    __type(value, __u64);
} task_metadata_storage SEC(".maps");

// Performance event output for events
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u32));
} events SEC(".maps");

// Initialize value for task storage
static const __u64 TASK_METADATA_INIT = 0;  // 0 = not reported yet

// Helper function to check if a task is a kernel thread
static __always_inline int is_kernel_thread(struct task_struct *task)
{
    if (!task)
        return 0;
    
    // Kernel threads either have PF_KTHREAD flag or no mm
    return (task->flags & 0x00200000 /* PF_KTHREAD */) || !task->mm;
}

// Send task metadata to userspace
static __always_inline int send_task_metadata(void *ctx, struct task_struct *task)
{
    if (!task)
        return 0;
    
    struct task_metadata_msg msg = {};
    
    msg.timestamp = bpf_ktime_get_ns();
    msg.type = MSG_TYPE_TASK_METADATA;
    msg.pid = task->pid;
    
    bpf_probe_read_kernel_str(&msg.comm, sizeof(msg.comm), task->comm);
    
    return bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, 
                                 &msg, sizeof(msg));
}

// Send task free event to userspace
static __always_inline int send_task_free(void *ctx, struct task_struct *task)
{
    if (!task)
        return 0;
    
    struct task_free_msg msg = {};
    
    msg.timestamp = bpf_ktime_get_ns();
    msg.type = MSG_TYPE_TASK_FREE;
    msg.pid = task->pid;
    
    return bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, 
                                 &msg, sizeof(msg));
}

// Check and report task metadata if needed
static __always_inline int check_and_send_metadata(void *ctx, struct task_struct *task)
{
    if (!task || is_kernel_thread(task))
        return 0;
    
    // Use group leader for process identification
    struct task_struct *leader = task->group_leader;
    if (!leader)
        return 0;
    
    // Get or create metadata tracking entry
    __u64 *reported = bpf_task_storage_get(&task_metadata_storage, leader, 
                                          (void *)&TASK_METADATA_INIT, 
                                          BPF_LOCAL_STORAGE_GET_F_CREATE);
    
    if (!reported)
        return 0;

    if (*reported == 1)
        return 0;
    
    // Use atomic compare-and-swap to check and update the reported status
    if (__sync_val_compare_and_swap(reported, 0, 1) == 0) {
        // We're the first to report this task
        return send_task_metadata(ctx, leader);
    }
    
    return 0;
}

SEC("tp_btf/sched_switch")
int handle_sched_switch(u64 *ctx)
{
    struct task_struct *prev = (struct task_struct *)ctx[1];
    struct task_struct *next = (struct task_struct *)ctx[2];
    
    // Check for task metadata for both prev and next task
    check_and_send_metadata(ctx, prev);
    check_and_send_metadata(ctx, next);
    
    return 0;
}

SEC("raw_tp/sched_process_free")
int handle_process_free(u64 *ctx)
{
    struct task_struct *task = (void *)ctx[0];
    
    // Skip if not a group leader or kernel thread
    if (!task || task != task->group_leader || is_kernel_thread(task))
        return 0;
    
    // Report task free event
    send_task_free(ctx, task);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL"; 