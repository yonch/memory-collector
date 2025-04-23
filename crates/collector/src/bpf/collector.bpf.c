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

// Hash map to track exited group leaders that need to be reported during process_free
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, __u32);  // PID of the exited group leader
    __type(value, __u8); // Just a presence indicator
} exited_leaders SEC(".maps");

// Performance event output for events
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(u32));
    __uint(value_size, sizeof(u32));
} events SEC(".maps");

// Initialize value for task storage
static const __u64 TASK_METADATA_INIT = 0;  // 0 = not reported yet
// Value to store in the exited_leaders map
static const __u8 LEADER_PRESENT = 1;

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
static __always_inline int send_task_free(void *ctx, __u32 pid)
{
    struct task_free_msg msg = {};
    
    msg.timestamp = bpf_ktime_get_ns();
    msg.type = MSG_TYPE_TASK_FREE;
    msg.pid = pid;
    
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

SEC("tracepoint/sched/sched_process_exit")
int handle_process_exit(struct trace_event_raw_sched_process_template *ctx)
{
    struct task_struct *task = bpf_get_current_task_btf();

    // Skip if not a group leader or kernel thread
    if (!task || task != task->group_leader)
        return 0;

    // Add task to the list of tasks to be reported
    __u32 pid = task->pid;
    bpf_map_update_elem(&exited_leaders, &pid, &LEADER_PRESENT, BPF_ANY);

    return 0;
}


SEC("tracepoint/sched/sched_process_free")
int handle_process_free(struct trace_event_raw_sched_process_template *ctx)
{
    __u32 pid = ctx->pid;
    
    // Check if this is a registered group leader that needs reporting
    __u8 *present = bpf_map_lookup_elem(&exited_leaders, &pid);
    if (!present) {
        // Not an exited group leader we care about
        return 0;
    }
    
    // Remove from the tracking map
    bpf_map_delete_elem(&exited_leaders, &pid);
    
    // Report task free event
    send_task_free(ctx, pid);
    
    return 0;
}

char LICENSE[] SEC("license") = "GPL"; 