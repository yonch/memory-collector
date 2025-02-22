#include <linux/sched.h>
#include "rmid_allocator.h"
#include "tracepoints.h"
#include "collector.h"

// Minimum time (in nanoseconds) an RMID must remain unused before reallocation
// Set to 2ms to ensure no overlap during 1ms measurement intervals
#define RMID_MINIMUM_FREE_TIME_NS (2 * NSEC_PER_MSEC)

// forward declarations
static u32 _rmid_alloc(const char *comm, pid_t tgid);

static struct rmid_alloc rmid_allocator;

/*
 * Internal helper to allocate an RMID.
 * Caller must hold rmid_allocator.lock.
 * Returns 0 if no RMID is available (RMID 0 is reserved/invalid).
 */
static u32 _rmid_alloc(const char *comm, pid_t tgid)
{
    struct rmid_info *info;
    u32 rmid;
    u64 now = ktime_get_ns();

    // Check if we have any free RMIDs
    if (list_empty(&rmid_allocator.free_list)) {
        return 0;  // RMID 0 is reserved/invalid
    }

    // Get the RMID that was freed the longest time ago
    info = list_first_entry(&rmid_allocator.free_list, struct rmid_info, list);

    // Check if enough time has passed since this RMID was freed
    if (now - info->last_free_timestamp < RMID_MINIMUM_FREE_TIME_NS) {
        return 0;  // No RMIDs available that have been free long enough
    }

    list_del_init(&info->list);

    // Update RMID info
    strncpy(info->comm, comm, TASK_COMM_LEN - 1);
    info->comm[TASK_COMM_LEN - 1] = '\0';
    info->tgid = tgid;
    rmid = info->rmid;

    // Emit tracepoint for RMID allocation while holding the lock
    trace_rmid_alloc(rmid, comm, tgid, now);

    return rmid;
}

void assign_rmid_to_task(struct task_struct *task)
{
    struct task_struct *group_leader;
    u32 rmid;
    unsigned long flags;

    if (!task)
        return;

    group_leader = task->group_leader;
    if (!group_leader)
        return;

    // If this is not the group leader, just copy the leader's RMID
    if (task != group_leader) {
        task->rmid = group_leader->rmid;
        return;
    }

    // First check without lock
    if (group_leader->rmid)
        return;  // Leader already has an RMID

    // We do not assign RMIDs to kernel threads
    if ((group_leader->mm == NULL) || (group_leader->flags & PF_KTHREAD))
        return;

    // No RMID assigned to leader, need to allocate one
    spin_lock_irqsave(&rmid_allocator.lock, flags);

    // Recheck after acquiring lock
    if (!group_leader->rmid) {
        // Allocate new RMID for the process
        rmid = _rmid_alloc(group_leader->comm, group_leader->tgid);
        group_leader->rmid = rmid;
        // Note: if allocation fails, leader->rmid remains 0
    }

    spin_unlock_irqrestore(&rmid_allocator.lock, flags);
}

// Make dump_existing_rmids available to procfs.c
void dump_existing_rmids(void)
{
    unsigned long flags;
    u32 i;
    struct rmid_info *info;

    for (i = 1; i <= rmid_allocator.max_rmid; i++) {
        // Lock for each element to avoid starving the write path
        spin_lock_irqsave(&rmid_allocator.lock, flags);
        
        info = &rmid_allocator.rmids[i];

        // Only emit tracepoint if RMID is in use (not on free list)
        if (list_empty(&info->list)) {
            trace_rmid_existing(
                info->rmid,
                info->comm,
                info->tgid,
                ktime_get_ns()
            );
        }
        
        spin_unlock_irqrestore(&rmid_allocator.lock, flags);
    }
}

// RMID allocation and initialization functions
int init_rmid_allocator(u32 max_rmid)
{
    // Initialize allocator structure with spinlock
    spin_lock_init(&rmid_allocator.lock);
    INIT_LIST_HEAD(&rmid_allocator.free_list);
    rmid_allocator.max_rmid = max_rmid;

    // Allocate array of RMID info structures
    rmid_allocator.rmids = kzalloc(sizeof(struct rmid_info) * (max_rmid + 1), GFP_KERNEL);
    if (!rmid_allocator.rmids) {
        pr_err(LOG_PREFIX "Failed to allocate RMID info array\n");
        return -ENOMEM;
    }

    // Initialize all RMIDs (skip RMID 0 as it's reserved)
    for (u32 i = 0; i <= max_rmid; i++) {
        INIT_LIST_HEAD(&rmid_allocator.rmids[i].list);
        rmid_allocator.rmids[i].rmid = i;
        rmid_allocator.rmids[i].tgid = 0;
        rmid_allocator.rmids[i].last_free_timestamp = 0;  // Initialize to 0 to allow immediate allocation
        if (i != RMID_INVALID) {  // Don't add RMID 0 to free list
            list_add_tail(&rmid_allocator.rmids[i].list, &rmid_allocator.free_list);
        }
    }

    return 0;
}

void cleanup_rmid_allocator(void)
{
    // we assume there are no concurrent accesses to the RMID allocator

    // free all allocated RMIDs, so the tracepoints are emitted. This does a little extra work in locking
    // maintaining the free list, but avoids adding another tracepoint location (and maintaining that code with changes)
    for (u32 i = 1; i <= rmid_allocator.max_rmid; i++) {
        if (list_empty(&rmid_allocator.rmids[i].list)) {
            rmid_free(i);
        }
    }

    kfree(rmid_allocator.rmids);
    rmid_allocator.rmids = NULL;
}

void rmid_free(u32 rmid)
{
    unsigned long flags;
    struct rmid_info *info;

    if (rmid == RMID_INVALID || rmid > rmid_allocator.max_rmid)
        return;

    spin_lock_irqsave(&rmid_allocator.lock, flags);

    info = &rmid_allocator.rmids[rmid];
    info->tgid = 0;
    info->last_free_timestamp = ktime_get_ns();  // Record free timestamp
    list_add_tail(&info->list, &rmid_allocator.free_list);

    // Emit tracepoint for RMID deallocation while holding the lock
    trace_rmid_free(rmid, info->last_free_timestamp);

    spin_unlock_irqrestore(&rmid_allocator.lock, flags);
}
