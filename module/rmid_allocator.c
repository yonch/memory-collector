#include <linux/sched.h>
#include "rmid_allocator.h"
#include "tracepoints.h"
#include "collector.h"

// Minimum time (in nanoseconds) an RMID must remain unused before reallocation
// Set to 2ms to ensure no overlap during 1ms measurement intervals
#define RMID_MINIMUM_FREE_TIME_NS (2 * NSEC_PER_MSEC)


/*
 * Internal helper to check if an RMID is valid
 */
static bool rmid_is_valid(struct rmid_alloc *allocator, u32 rmid)
{
    return rmid != RMID_INVALID && rmid <= allocator->max_rmid;
}

struct rmid_info *rmid_get_info(struct rmid_alloc *allocator, u32 rmid)
{
    if (!rmid_is_valid(allocator, rmid))
        return NULL;
    return &allocator->rmids[rmid];
}

bool rmid_is_allocated(struct rmid_alloc *allocator, u32 rmid)
{
    struct rmid_info *info = rmid_get_info(allocator, rmid);
    if (!info)
        return false;
    return list_empty(&info->list);
}

u32 rmid_alloc(struct rmid_alloc *allocator, const char *comm, pid_t tgid, u64 timestamp)
{
    struct rmid_info *info;
    u32 rmid;

    // Check if we have any free RMIDs
    if (list_empty(&allocator->free_list)) {
        return 0;  // RMID 0 is reserved/invalid
    }

    // Get the RMID that was freed the longest time ago
    info = list_first_entry(&allocator->free_list, struct rmid_info, list);

    // Check if enough time has passed since this RMID was freed
    if (timestamp - info->last_free_timestamp < allocator->min_free_time_ns) {
        return 0;  // No RMIDs available that have been free long enough
    }

    list_del_init(&info->list);

    // Update RMID info
    strncpy(info->comm, comm, TASK_COMM_LEN - 1);
    info->comm[TASK_COMM_LEN - 1] = '\0';
    info->tgid = tgid;
    rmid = info->rmid;

    // Emit tracepoint for RMID allocation
    trace_rmid_alloc(rmid, comm, tgid, timestamp);

    return rmid;
}

void rmid_free(struct rmid_alloc *allocator, u32 rmid, u64 timestamp)
{
    struct rmid_info *info;

    if (!rmid_is_valid(allocator, rmid))
        return;

    info = &allocator->rmids[rmid];
    info->tgid = 0;
    info->last_free_timestamp = timestamp;
    list_add_tail(&info->list, &allocator->free_list);

    // Emit tracepoint for RMID deallocation
    trace_rmid_free(rmid, timestamp);
}

int init_rmid_allocator(struct rmid_alloc *allocator, u32 max_rmid, u64 min_free_time_ns)
{
    // Initialize allocator structure
    INIT_LIST_HEAD(&allocator->free_list);
    allocator->max_rmid = max_rmid;
    allocator->min_free_time_ns = min_free_time_ns;

    // Allocate array of RMID info structures
    allocator->rmids = kzalloc(sizeof(struct rmid_info) * (max_rmid + 1), GFP_KERNEL);
    if (!allocator->rmids) {
        pr_err(LOG_PREFIX "Failed to allocate RMID info array\n");
        return -ENOMEM;
    }

    // Initialize all RMIDs (skip RMID 0 as it's reserved)
    for (u32 i = 0; i <= max_rmid; i++) {
        INIT_LIST_HEAD(&allocator->rmids[i].list);
        allocator->rmids[i].rmid = i;
        allocator->rmids[i].tgid = 0;
        allocator->rmids[i].last_free_timestamp = -min_free_time_ns;  // Initialize to -min_free_time_ns to allow immediate allocation
        if (i != RMID_INVALID) {  // Don't add RMID 0 to free list
            list_add_tail(&allocator->rmids[i].list, &allocator->free_list);
        }
    }

    return 0;
}

void cleanup_rmid_allocator(struct rmid_alloc *allocator)
{
    // Free all allocated RMIDs to emit tracepoints
    for (u32 i = 1; i <= allocator->max_rmid; i++) {
        if (rmid_is_allocated(allocator, i)) {
            rmid_free(allocator, i, ktime_get_ns());
        }
    }

    kfree(allocator->rmids);
    allocator->rmids = NULL;
}
