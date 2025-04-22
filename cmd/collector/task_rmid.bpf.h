#ifndef __TASK_RMID_BPF_H
#define __TASK_RMID_BPF_H


// Initialize the RMID management system
// Must be called before using any other functions
// Returns 1 on success, 0 on failure
int task_rmid_init(__u32 max_rmids, __u64 min_free_time_ns);

// Get the RMID for a task
// If the task is not a group leader, gets the RMID from its group leader
// Returns 0 if the task doesn't have an RMID
__u32 task_rmid_get(struct task_struct *task);

#endif /* __TASK_RMID_BPF_H */ 