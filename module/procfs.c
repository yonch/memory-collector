#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "procfs.h"

#define PROC_ENTRY_NAME "unvariance_collector"

extern void dump_existing_rmids(void);  // Declare the external function

static ssize_t collector_write(struct file *file, const char __user *buf,
                             size_t count, loff_t *ppos)
{
    char cmd[32];
    size_t len = min(count, sizeof(cmd) - 1);

    if (copy_from_user(cmd, buf, len))
        return -EFAULT;

    cmd[len] = '\0';

    // If the command is "dump", trigger RMID dump
    if (strncmp(cmd, "dump", 4) == 0) {
        dump_existing_rmids();
        return count;
    }

    return -EINVAL;
}

static const struct proc_ops collector_proc_ops = {
    .proc_write = collector_write,
};

static struct proc_dir_entry *proc_entry;

int init_procfs(void)
{
    proc_entry = proc_create(PROC_ENTRY_NAME, 0220, NULL, &collector_proc_ops);
    if (!proc_entry) {
        pr_err("Failed to create /proc/%s\n", PROC_ENTRY_NAME);
        return -ENOMEM;
    }
    return 0;
}

void cleanup_procfs(void)
{
    if (proc_entry) {
        remove_proc_entry(PROC_ENTRY_NAME, NULL);
        proc_entry = NULL;
    }
} 