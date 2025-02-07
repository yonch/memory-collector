#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Memory subsystem monitoring for Kubernetes");
MODULE_VERSION("1.0");

static int __init memory_collector_init(void)
{
    printk(KERN_INFO "Memory Collector: module loaded\n");
    return 0;
}

static void __exit memory_collector_exit(void)
{
    printk(KERN_INFO "Memory Collector: module unloaded\n");
}

module_init(memory_collector_init);
module_exit(memory_collector_exit); 