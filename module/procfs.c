#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "procfs.h"
#include "collector.h"

#define MAX_COMMAND_LENGTH 32

static ssize_t procfs_write(struct file *file, const char __user *buf,
                           size_t count, loff_t *ppos)
{
    char cmd[MAX_COMMAND_LENGTH];
    size_t len = min(count, sizeof(cmd) - 1);
    struct procfs_data *data;

    if (copy_from_user(cmd, buf, len))
        return -EFAULT;

    cmd[len] = '\0';

    data = pde_data(file_inode(file)); // pde_data replaced PDE_DATA in 5.17
    if (!data || !data->dump_callback)
        return -EINVAL;

    // Process each line in the input
    char *line = cmd;
    char *next_line;
    while (line) {
        // Find next line
        next_line = strchr(line, '\n');
        if (next_line)
            *next_line++ = '\0';

        // Trim whitespace
        while (*line && (*line == ' ' || *line == '\t'))
            line++;

        // Check if this line starts with "dump"
        if (strncmp(line, "dump", 4) == 0) {
            data->dump_callback();
        }

        line = next_line;
    }

    return count;
}

static const struct proc_ops procfs_ops = {
    .proc_write = procfs_write,
};

int procfs_init(struct procfs_data *data)
{
    if (!data || !data->name)
        return -EINVAL;

    data->proc_entry = proc_create_data(data->name, 0220, NULL, &procfs_ops, data);
    if (!data->proc_entry) {
        pr_err(LOG_PREFIX "Failed to create /proc/%s\n", data->name);
        return -ENOMEM;
    }

    return 0;
}

void procfs_cleanup(struct procfs_data *data)
{
    if (data && data->proc_entry) {
        remove_proc_entry(data->name, NULL);
        data->proc_entry = NULL;
    }
} 