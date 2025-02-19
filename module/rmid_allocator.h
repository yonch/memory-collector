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
};

struct rmid_alloc {
    spinlock_t lock;  // Protects all fields
    struct list_head free_list;  // List of free RMIDs
    u32 max_rmid;  // Minimum of max_rmid across all CPUs
    struct rmid_info *rmids;  // Array of RMID info, indexed by RMID
};


void rmid_free(u32 rmid);
int init_rmid_allocator(u32 max_rmid);
void cleanup_rmid_allocator(void);
void assign_rmid_to_task(struct task_struct *task);
void dump_existing_rmids(void);
