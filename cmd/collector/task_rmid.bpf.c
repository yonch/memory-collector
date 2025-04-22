//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

#include "task_rmid.bpf.h"
#include "protocol.bpf.h"
#include "rmid_allocator.bpf.h"

// Define missing constants
#define PF_KTHREAD 0x00200000

#define TASK_COMM_LEN 16

// Structure containing the RMID allocator and lock
struct task_rmid {
    struct bpf_spin_lock lock;
    struct rmid_allocator allocator;
};

// Structure for RMID initialization parameters
struct task_rmid_init_params {
    __u32 max_rmids;
    __u64 min_free_time_ns;
};

// Global RMID state map
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct task_rmid);
} task_rmid_map SEC(".maps");

// Helper functions for BPF-side use only
static __always_inline __u32 get_task_rmid(struct task_struct *task) {
    if (!task)
        return 0;
    return task->rmid;
}

static __always_inline void set_task_rmid(struct task_struct *task, __u32 rmid) {
    if (!task)
        return;
    task->rmid = rmid;
}

static __always_inline struct task_struct *get_group_leader(struct task_struct *task) {
    if (!task)
        return NULL;
    return task->group_leader;
}

static __always_inline int is_kernel_thread(struct task_struct *task) {
    if (!task)
        return 0;
    return (task->flags & PF_KTHREAD) || !task->mm;
}

// Initialize the RMID management system
int task_rmid_init(__u32 max_rmids, __u64 min_free_time_ns) {
    __u32 key = 0;
    struct task_rmid *state;
    
    state = bpf_map_lookup_elem(&task_rmid_map, &key);
    if (!state)
        return 0;
    
    // Initialize allocator
    return rmid_init(&state->allocator, max_rmids, min_free_time_ns);
}

// Get the RMID for a task - public API function
__u32 task_rmid_get(struct task_struct *task) {
    if (!task)
        return 0;
    
    // If not the group leader, get RMID from leader
    if (task != task->group_leader)
        return get_task_rmid(task->group_leader);
    
    return get_task_rmid(task);
}

// Helper function to allocate an RMID for a task
static __always_inline __u32 allocate_rmid(void *ctx, struct task_struct *task) {
    __u32 key = 0;
    struct task_rmid *state;
    __u64 timestamp = bpf_ktime_get_ns();
    __u32 rmid = 0;

    // Get the allocator from the map
    state = bpf_map_lookup_elem(&task_rmid_map, &key);
    if (!state)
        return 0;

    // Acquire lock using proper BPF helper
    bpf_spin_lock(&state->lock);

    // Check if task already has an RMID under the lock to avoid races
    if (get_task_rmid(task) != 0) {
        // Task already has an RMID - nothing to do
        bpf_spin_unlock(&state->lock);
        return get_task_rmid(task);
    }

    // Allocate a new RMID since the task doesn't have one
    rmid = rmid_alloc(&state->allocator, timestamp);
    
    // Set the RMID in the task while still under the lock
    if (rmid != 0) {
        set_task_rmid(task, rmid);
    }
    
    // Release lock using proper BPF helper
    bpf_spin_unlock(&state->lock);

    if (rmid == 0)
        return 0;

    // Send allocation event to userspace
    send_rmid_alloc(ctx, rmid, task->comm, task->tgid, timestamp);

    return rmid;
}

// Helper function to free an RMID
static __always_inline void free_rmid(void *ctx, __u32 rmid) {
    __u32 key = 0;
    struct task_rmid *state;
    __u64 timestamp = bpf_ktime_get_ns();

    // Get the allocator from the map
    state = bpf_map_lookup_elem(&task_rmid_map, &key);
    if (!state)
        return;

    // Acquire lock using proper BPF helper
    bpf_spin_lock(&state->lock);

    // Free the RMID
    rmid_free(&state->allocator, rmid, timestamp);
    
    // Release lock using proper BPF helper
    bpf_spin_unlock(&state->lock);

    // Send free event to userspace
    send_rmid_free(ctx, rmid, timestamp);
}

// Handle process fork
SEC("tp_btf/sched_process_fork")
int handle_process_fork(u64 *ctx) {
    struct task_struct *task = (void *)ctx[1];
    struct task_struct *leader = get_group_leader(task);
    __u32 rmid;

    // Skip if task is a kernel thread
    if (is_kernel_thread(task))
        return 0;

    // If this is a thread (not the group leader), copy RMID from leader
    if (task != leader) {
        rmid = get_task_rmid(leader);
        if (!rmid) {
            rmid = allocate_rmid(ctx, leader);
        }
        if (rmid) {
            set_task_rmid(task, rmid);
        }
        return 0;
    }

    // For group leaders, allocate a new RMID
    rmid = allocate_rmid(ctx, task);
    // No need to explicitly set RMID here as it's already set in allocate_rmid

    return 0;
}

// Handle process exit
SEC("tp_btf/sched_process_free")
int handle_process_free(u64 *ctx) {
    struct task_struct *task = (void *)ctx[0];
    struct task_struct *leader = get_group_leader(task);
    __u32 rmid;

    // Only free RMID when the group leader exits
    if (task != leader)
        return 0;

    rmid = get_task_rmid(task);
    if (rmid) {
        free_rmid(ctx, rmid);
    }

    return 0;
}

// Syscall program to initialize the RMID system
SEC("syscall")
int task_rmid_init_prog(struct task_rmid_init_params *ctx) {
    if (!ctx)
        return -1;

    return task_rmid_init(ctx->max_rmids, ctx->min_free_time_ns);
}

char LICENSE[] SEC("license") = "GPL"; 