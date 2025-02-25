#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "rmid_allocator.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Memory Collector Project");
MODULE_DESCRIPTION("Test module for RMID allocator");
MODULE_VERSION("1.0");

// Define the tracepoint
#define CREATE_TRACE_POINTS
#include "tracepoints.h"

#define TEST_PREFIX "rmid_allocator_test: "
#define TEST_RESULT "test_result:"

#define TEST_MAX_RMID_DEFINED 4
static struct rmid_alloc test_allocator;
static spinlock_t test_lock;  // Lock for test synchronization
static const u32 TEST_MAX_RMID = TEST_MAX_RMID_DEFINED;
static const u64 TEST_MIN_FREE_TIME = 2000000;  // 2ms in ns

static void report_test_result(const char *test_name, bool passed, const char *message)
{
    pr_info(TEST_RESULT "%s:%s%s%s\n", test_name, 
            passed ? "pass" : "fail",
            message ? ":" : "",
            message ? message : "");
}

static bool test_init_cleanup(void)
{
    int ret;
    bool passed = true;
    
    ret = init_rmid_allocator(&test_allocator, TEST_MAX_RMID, TEST_MIN_FREE_TIME);
    if (ret) {
        report_test_result("init_cleanup", false, "init failed");
        return false;
    }

    // Verify initial state
    if (test_allocator.max_rmid != TEST_MAX_RMID) {
        passed = false;
        pr_err(TEST_PREFIX "max_rmid mismatch: expected %u, got %u\n",
               TEST_MAX_RMID, test_allocator.max_rmid);
    }

    if (test_allocator.min_free_time_ns != TEST_MIN_FREE_TIME) {
        passed = false;
        pr_err(TEST_PREFIX "min_free_time mismatch: expected %llu, got %llu\n",
               TEST_MIN_FREE_TIME, test_allocator.min_free_time_ns);
    }

    cleanup_rmid_allocator(&test_allocator);

    report_test_result("init_cleanup", passed, NULL);
    return passed;
}

static bool test_rmid_allocation(void)
{
    bool passed = true;
    u32 rmid;
    unsigned long flags;
    const char *test_comm = "test_proc";
    const pid_t test_tgid = 1234;
    u64 now = 1000000;  // Start at 1ms

    int result = init_rmid_allocator(&test_allocator, TEST_MAX_RMID, TEST_MIN_FREE_TIME);
    if (result) {
        pr_err(TEST_PREFIX "init_rmid_allocator failed: %d\n", result);
        return false;
    }

    spin_lock_irqsave(&test_lock, flags);

    // Should be able to allocate TEST_MAX_RMID RMIDs
    for (u32 i = 1; i <= TEST_MAX_RMID; i++) {
        rmid = rmid_alloc(&test_allocator, test_comm, test_tgid + i, now);
        if (rmid != i) {
            passed = false;
            pr_err(TEST_PREFIX "allocation %u returned unexpected RMID: expected %u, got %u\n",
                   i, i, rmid);
            break;
        }
    }

    // Next allocation should fail (return 0)
    rmid = rmid_alloc(&test_allocator, test_comm, test_tgid, now);
    if (rmid != 0) {
        passed = false;
        pr_err(TEST_PREFIX "allocation beyond max did not return 0: got %u\n", rmid);
    }

    spin_unlock_irqrestore(&test_lock, flags);
    cleanup_rmid_allocator(&test_allocator);

    report_test_result("rmid_allocation", passed, NULL);
    return passed;
}

static bool test_rmid_free_and_reuse(void)
{
    bool passed = true;
    u32 rmid1, rmid2;
    unsigned long flags;
    const char *test_comm = "test_proc";
    const pid_t test_tgid = 1234;
    u64 now = 1000000;  // Start at 1ms
    u32 allocated_rmids[TEST_MAX_RMID_DEFINED];

    init_rmid_allocator(&test_allocator, TEST_MAX_RMID, TEST_MIN_FREE_TIME);

    spin_lock_irqsave(&test_lock, flags);

    // First allocate all RMIDs
    for (u32 i = 0; i < TEST_MAX_RMID; i++) {
        allocated_rmids[i] = rmid_alloc(&test_allocator, test_comm, test_tgid + i, now);
        if (allocated_rmids[i] != i + 1) {
            passed = false;
            pr_err(TEST_PREFIX "initial allocation %u failed: expected %u, got %u\n",
                   i, i + 1, allocated_rmids[i]);
            goto out;
        }
    }

    // Free the first RMID
    rmid_free(&test_allocator, allocated_rmids[0], now);

    // Try to allocate immediately - should fail due to min_free_time
    rmid1 = rmid_alloc(&test_allocator, test_comm, test_tgid, now);
    if (rmid1 != 0) {
        passed = false;
        pr_err(TEST_PREFIX "immediate reallocation should have failed: got %u\n", rmid1);
        goto out;
    }

    // Try to allocate just before min_free_time - should fail due to min_free_time
    rmid1 = rmid_alloc(&test_allocator, test_comm, test_tgid, now + TEST_MIN_FREE_TIME - 1);
    if (rmid1 != 0) {
        passed = false;
        pr_err(TEST_PREFIX "immediate reallocation should have failed: got %u\n", rmid1);
        goto out;
    }

    // Wait past min_free_time and try again
    now += TEST_MIN_FREE_TIME;
    rmid2 = rmid_alloc(&test_allocator, test_comm, test_tgid, now);
    if (rmid2 != allocated_rmids[0]) {
        passed = false;
        pr_err(TEST_PREFIX "delayed reallocation did not get freed RMID: expected %u, got %u\n",
               allocated_rmids[0], rmid2);
    }

out:
    spin_unlock_irqrestore(&test_lock, flags);
    cleanup_rmid_allocator(&test_allocator);

    report_test_result("rmid_free_and_reuse", passed, NULL);
    return passed;
}

static bool test_rmid_info_and_status(void)
{
    bool passed = true;
    unsigned long flags;
    const char *test_comm = "test_proc";
    const pid_t test_tgid = 1234;
    u64 now = 1000000;
    struct rmid_info *info;
    u32 rmid;

    init_rmid_allocator(&test_allocator, TEST_MAX_RMID, TEST_MIN_FREE_TIME);

    spin_lock_irqsave(&test_lock, flags);

    // Verify invalid RMID handling
    if (rmid_get_info(&test_allocator, 0) != NULL) {
        passed = false;
        pr_err(TEST_PREFIX "rmid_get_info returned non-NULL for RMID 0\n");
    }

    if (rmid_get_info(&test_allocator, TEST_MAX_RMID + 1) != NULL) {
        passed = false;
        pr_err(TEST_PREFIX "rmid_get_info returned non-NULL for invalid RMID\n");
    }

    // Allocate an RMID and verify info
    rmid = rmid_alloc(&test_allocator, test_comm, test_tgid, now);
    if (!rmid_is_allocated(&test_allocator, rmid)) {
        passed = false;
        pr_err(TEST_PREFIX "rmid_is_allocated returned false for allocated RMID\n");
    }

    info = rmid_get_info(&test_allocator, rmid);
    if (!info) {
        passed = false;
        pr_err(TEST_PREFIX "rmid_get_info returned NULL for valid RMID\n");
    } else {
        if (info->tgid != test_tgid) {
            passed = false;
            pr_err(TEST_PREFIX "RMID info has wrong tgid: expected %d, got %d\n",
                   test_tgid, info->tgid);
        }
        if (strncmp(info->comm, test_comm, TASK_COMM_LEN) != 0) {
            passed = false;
            pr_err(TEST_PREFIX "RMID info has wrong comm: expected %s, got %s\n",
                   test_comm, info->comm);
        }
    }

    spin_unlock_irqrestore(&test_lock, flags);
    cleanup_rmid_allocator(&test_allocator);

    report_test_result("rmid_info_and_status", passed, NULL);
    return passed;
}

static int __init rmid_allocator_test_init(void)
{
    bool all_passed = true;

    pr_info(TEST_PREFIX "starting tests\n");

    // Initialize test lock
    spin_lock_init(&test_lock);

    all_passed &= test_init_cleanup();
    all_passed &= test_rmid_allocation();
    all_passed &= test_rmid_free_and_reuse();
    all_passed &= test_rmid_info_and_status();

    pr_info(TEST_PREFIX "tests %s\n", all_passed ? "passed" : "failed");
    return all_passed ? 0 : -EINVAL;
}

static void __exit rmid_allocator_test_exit(void)
{
    pr_info(TEST_PREFIX "module unloaded\n");
}

module_init(rmid_allocator_test_init);
module_exit(rmid_allocator_test_exit); 