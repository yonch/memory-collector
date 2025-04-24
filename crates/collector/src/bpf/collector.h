/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __COLLECTOR_H
#define __COLLECTOR_H

#define TASK_COMM_LEN 16

// Message types
enum msg_type {
    MSG_TYPE_TASK_METADATA = 1,
    MSG_TYPE_TASK_FREE = 2,
};

// Sample header structure that matches the one in reader.rs
struct sample_header {
    __u32 size;      // Size field (filled by kernel)
    enum msg_type type;      // Message type
    __u64 timestamp; // Timestamp of the event
};

// Structure for task metadata messages
struct task_metadata_msg {
    struct sample_header header; // Common header
    __u32 pid;                   // Process ID
    __u8 comm[TASK_COMM_LEN];    // Process command name
};

// Structure for task free messages
struct task_free_msg {
    struct sample_header header; // Common header
    __u32 pid;                   // Process ID
};


#endif /* __COLLECTOR_H */ 