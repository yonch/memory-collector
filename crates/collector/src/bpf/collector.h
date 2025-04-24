/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __COLLECTOR_H
#define __COLLECTOR_H

#define TASK_COMM_LEN 16

// Message types
enum msg_type {
    MSG_TYPE_TASK_METADATA = 1,
    MSG_TYPE_TASK_FREE = 2,
};

// Structure for task metadata messages
struct task_metadata_msg {
    __u64 timestamp;  // Timestamp of the event
    __u32 type;       // MSG_TYPE_TASK_METADATA
    __u32 pid;        // Process ID
    char comm[TASK_COMM_LEN];  // Process command name
};

// Structure for task free messages
struct task_free_msg {
    __u64 timestamp;  // Timestamp of the event
    __u32 type;       // MSG_TYPE_TASK_FREE
    __u32 pid;        // Process ID
};

// Dummy instances to make skeleton generation work
struct task_metadata_msg _task_metadata_msg = {0};
struct task_free_msg _task_free_msg = {0};

#endif /* __COLLECTOR_H */ 