#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "procfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Test module for procfs functionality");
MODULE_VERSION("1.0");

#define TEST_PREFIX "procfs_test: "
#define TEST_PROC_NAME "procfs_test"
#define TEST_RESULT "test_result:"

static struct procfs_data test_procfs;
static int dump_call_count = 0;

static void test_dump_callback(void)
{
    dump_call_count++;
    pr_info(TEST_PREFIX "dump callback called (count=%d)\n", dump_call_count);
}

static bool test_procfs_init(void)
{
    int ret;
    bool passed = true;

    // Initialize with NULL data
    ret = procfs_init(NULL);
    if (ret != -EINVAL) {
        pr_err(TEST_PREFIX "init with NULL data should return -EINVAL, got %d\n", ret);
        passed = false;
    }

    // Initialize with valid data
    test_procfs.name = TEST_PROC_NAME;
    test_procfs.dump_callback = test_dump_callback;
    ret = procfs_init(&test_procfs);
    if (ret != 0) {
        pr_err(TEST_PREFIX "init with valid data failed: %d\n", ret);
        passed = false;
    }

    // Verify procfs entry exists
    struct proc_dir_entry *entry = test_procfs.proc_entry;
    if (!entry) {
        pr_err(TEST_PREFIX "proc entry not created\n");
        passed = false;
    }

    pr_info(TEST_RESULT "procfs_init:%s\n", passed ? "pass" : "fail");
    return passed;
}

static bool test_procfs_cleanup(void)
{
    bool passed = true;

    // Test cleanup with NULL
    procfs_cleanup(NULL);  // Should not crash

    // Test cleanup with valid data
    procfs_cleanup(&test_procfs);
    if (test_procfs.proc_entry != NULL) {
        pr_err(TEST_PREFIX "proc_entry not nulled after cleanup\n");
        passed = false;
    }

    pr_info(TEST_RESULT "procfs_cleanup:%s\n", passed ? "pass" : "fail");
    return passed;
}

static int __init procfs_test_init(void)
{
    bool all_passed = true;

    pr_info(TEST_PREFIX "starting tests\n");

    all_passed &= test_procfs_init();
    
    // Don't run cleanup test yet - we need the procfs entry for external testing
    
    pr_info(TEST_PREFIX "initialization tests %s\n", all_passed ? "passed" : "failed");
    return 0;
}

static void __exit procfs_test_exit(void)
{
    bool cleanup_passed;

    cleanup_passed = test_procfs_cleanup();
    pr_info(TEST_PREFIX "cleanup tests %s\n", cleanup_passed ? "passed" : "failed");
    pr_info(TEST_PREFIX "total dump calls: %d\n", dump_call_count);
}

module_init(procfs_test_init);
module_exit(procfs_test_exit); 