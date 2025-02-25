#ifndef _PROCFS_H
#define _PROCFS_H

#include <linux/proc_fs.h>

/**
 * Structure to encapsulate procfs-related data and functionality
 */
struct procfs_data {
    struct proc_dir_entry *proc_entry;  /* Procfs entry pointer */
    const char *name;                   /* Name of the procfs entry */
    void (*dump_callback)(void);        /* Callback for handling dump commands */
};

/**
 * Initialize procfs functionality
 * @data: Pointer to procfs_data structure containing configuration
 * Returns 0 on success, negative error code on failure
 */
int procfs_init(struct procfs_data *data);

/**
 * Clean up procfs functionality
 * @data: Pointer to procfs_data structure to clean up
 */
void procfs_cleanup(struct procfs_data *data);

#endif /* _PROCFS_H */ 