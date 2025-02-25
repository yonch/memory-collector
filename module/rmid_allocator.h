#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#define RMID_INVALID 0

// RMID allocation structure
struct rmid_info {
    struct list_head list;  // For free list
    u32 rmid;
    char comm[TASK_COMM_LEN];  // Name of task leader
    pid_t tgid;  // Thread group ID (process ID)
    u64 last_free_timestamp;  // Timestamp when RMID was last freed
};

/**
 * RMID allocator structure
 * Note: This structure is not thread-safe. Callers must provide their own
 * synchronization when accessing this structure from multiple threads/CPUs.
 */
struct rmid_alloc {
    struct list_head free_list;  // List of free RMIDs
    u32 max_rmid;  // Minimum of max_rmid across all CPUs
    struct rmid_info *rmids;  // Array of RMID info, indexed by RMID
    u64 min_free_time_ns;  // Minimum time an RMID must be free before reallocation
};

/**
 * Initialize RMID allocator
 * @allocator: Pointer to allocator structure to initialize
 * @max_rmid: Maximum RMID value to use
 * @min_free_time_ns: Minimum time (ns) an RMID must be free before reallocation
 * Returns 0 on success, negative error code on failure
 */
int init_rmid_allocator(struct rmid_alloc *allocator, u32 max_rmid, u64 min_free_time_ns);

/**
 * Clean up RMID allocator
 * @allocator: Pointer to allocator structure to clean up
 * Note: Caller must ensure no other threads are accessing the allocator during cleanup
 */
void cleanup_rmid_allocator(struct rmid_alloc *allocator);

/**
 * Allocate a new RMID
 * @allocator: Pointer to allocator structure
 * @comm: Command name to associate with RMID
 * @tgid: Thread group ID to associate with RMID
 * @timestamp: Current timestamp in nanoseconds
 * Returns allocated RMID, or 0 if none available
 * Note: Caller must provide synchronization when using from multiple threads/CPUs
 */
u32 rmid_alloc(struct rmid_alloc *allocator, const char *comm, pid_t tgid, u64 timestamp);

/**
 * Free an RMID
 * @allocator: Pointer to allocator structure
 * @rmid: RMID to free
 * @timestamp: Current timestamp in nanoseconds
 * Note: Caller must provide synchronization when using from multiple threads/CPUs
 */
void rmid_free(struct rmid_alloc *allocator, u32 rmid, u64 timestamp);

/**
 * Get RMID info structure
 * @allocator: Pointer to allocator structure
 * @rmid: RMID to get info for
 * Returns pointer to RMID info structure, or NULL if RMID invalid
 * Note: Caller must provide synchronization when using from multiple threads/CPUs
 */
struct rmid_info *rmid_get_info(struct rmid_alloc *allocator, u32 rmid);

/**
 * Check if RMID is allocated
 * @allocator: Pointer to allocator structure
 * @rmid: RMID to check
 * Returns true if RMID is allocated, false otherwise
 * Note: Caller must provide synchronization when using from multiple threads/CPUs
 */
bool rmid_is_allocated(struct rmid_alloc *allocator, u32 rmid);
