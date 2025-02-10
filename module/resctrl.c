#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <linux/resctrl.h>
#include <asm/resctrl.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/atomic.h>

#include "resctrl.h"

MODULE_LICENSE("GPL");

#ifndef RESCTRL_RESERVED_RMID
#define RESCTRL_RESERVED_RMID 0
#endif

#define RMID_VAL_ERROR BIT_ULL(63)
#define RMID_VAL_UNAVAIL BIT_ULL(62)

/* Structure to pass RMID to IPI function */
struct ipi_rmid_args {
    u32 rmid;
    int status;
};

/* Add these declarations at the top after the includes */
static struct timer_list cpuid_check_timer;
static atomic_t check_counter = ATOMIC_INIT(0);
#define MAX_CHECKS 6

/*
 * IPI function to write RMID to MSR
 * Called on each CPU by on_each_cpu()
 */
static void ipi_write_rmid(void *info)
{
    struct ipi_rmid_args *args = info;
    u32 closid = 0;

    // if we're not on CPU 2, don't do anything
    if (smp_processor_id() != 2) {
        args->status = 0;
        return;
    }
    
    if (wrmsr_safe(MSR_IA32_PQR_ASSOC, args->rmid, closid) != 0) {
        args->status = -EIO;
    } else {
        args->status = 0;
    }
}

static int enumerate_cpuid(void)
{
    pr_info("memory_collector: Starting enumerate_cpuid\n");
    
    unsigned int eax, ebx, ecx, edx;
    int ret = 0;

    if (!boot_cpu_has( X86_FEATURE_CQM_LLC)) {
        pr_err("memory_collector: CPU does not support QoS monitoring\n");
        return -ENODEV;
    }

    pr_debug("memory_collector: Checking CPUID.0x7.0 for RDT support\n");
    cpuid_count(0x7, 0, &eax, &ebx, &ecx, &edx);
    if (!(ebx & (1 << 12))) {
        pr_err("memory_collector: RDT monitoring not supported (CPUID.0x7.0:EBX.12)\n");
        return -ENODEV;
    }

    pr_debug("memory_collector: Checking CPUID.0xF.0 for L3 monitoring\n");
    cpuid_count(0xF, 0, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1 << 1))) {
        pr_err("memory_collector: L3 monitoring not supported (CPUID.0xF.0:EDX.1)\n");
        return -ENODEV;
    }

    pr_debug("memory_collector: Checking CPUID.0xF.1 for L3 occupancy monitoring\n");
    cpuid_count(0xF, 1, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1 << 0))) {
        pr_err("memory_collector: L3 occupancy monitoring not supported (CPUID.0xF.1:EDX.0)\n");
        return -ENODEV;
    }

    pr_info("memory_collector: enumerate_cpuid completed successfully\n");
    return ret;
}

static void cpuid_check_timer_callback(struct timer_list *t)
{
    int check_num = atomic_inc_return(&check_counter);
    unsigned int eax, ebx, ecx, edx;

    pr_info("memory_collector: Running CPUID check %d of %d\n", check_num, MAX_CHECKS);

    switch (check_num) {
        case 1:
            if (!boot_cpu_has( X86_FEATURE_CQM_LLC)) {
                pr_err("memory_collector: CPU does not support QoS monitoring\n");
            }
            break;
        case 2:
            pr_debug("memory_collector: Checking CPUID.0x7.0 for RDT support\n");
            cpuid_count(0x7, 0, &eax, &ebx, &ecx, &edx);
            if (!(ebx & (1 << 12))) {
                pr_err("memory_collector: RDT monitoring not supported (CPUID.0x7.0:EBX.12)\n");
            }
            break;
        case 3:
            pr_debug("memory_collector: Checking CPUID.0xF.0 for L3 monitoring\n");
            cpuid_count(0xF, 0, &eax, &ebx, &ecx, &edx);
            if (!(edx & (1 << 1))) {
                pr_err("memory_collector: L3 monitoring not supported (CPUID.0xF.0:EDX.1)\n");
            }
            break;
        case 4:
            pr_debug("memory_collector: Checking CPUID.0xF.1 for L3 occupancy monitoring\n");
            cpuid_count(0xF, 1, &eax, &ebx, &ecx, &edx);
            if (!(edx & (1 << 0))) {
                pr_err("memory_collector: L3 occupancy monitoring not supported (CPUID.0xF.1:EDX.0)\n");
            }
            break;
        default:
            pr_info("memory_collector: Completed all %d CPUID checks\n", MAX_CHECKS);
            break;
    }

    mod_timer(&cpuid_check_timer, jiffies + (5 * HZ));
}

int resctrl_init(void)
{
    int cpu;
    int ret = 0;

    // Initialize the timer
    timer_setup(&cpuid_check_timer, cpuid_check_timer_callback, 0);
    
    // ret = enumerate_cpuid();
    // if (ret) {
    //     pr_err("Memory Collector: Failed to enumerate CPUID\n");
    //     return ret;
    // }

    // for_each_online_cpu(cpu) {
    //     struct ipi_rmid_args args = {
    //         .rmid = cpu + 1,
    //         .status = 0
    //     };
        
    //     on_each_cpu_mask(cpumask_of(cpu), ipi_write_rmid, &args, 1);
        
    //     if (args.status) {
    //         pr_err("Memory Collector: Failed to set RMID %u on CPU %d\n", args.rmid, cpu);
    //         ret = args.status;
    //         break;
    //     }
    //     pr_info("Memory Collector: Successfully set RMID %u on CPU %d\n", args.rmid, cpu);
    // }

    // Start the timer for first check in 5 seconds
    mod_timer(&cpuid_check_timer, jiffies + (5 * HZ));
    pr_info("memory_collector: Started periodic CPUID checks\n");
    
    return ret;
}

int resctrl_exit(void) 
{
    // Delete the timer first
    del_timer_sync(&cpuid_check_timer);
    
    int failure_count = 0;
    int cpu;
    
    struct ipi_rmid_args args = {
        .rmid = RESCTRL_RESERVED_RMID,
        .status = 0
    };
    
    // for_each_online_cpu(cpu) {
    //     on_each_cpu_mask(cpumask_of(cpu), ipi_write_rmid, &args, 1);
        
    //     if (args.status) {
    //         pr_err("Memory Collector: Failed to set RMID %u on CPU %d\n", args.rmid, cpu);
    //         failure_count++;
    //         continue;
    //     }
    //     pr_info("Memory Collector: Successfully set RMID %u on CPU %d\n", args.rmid, cpu);
    // }

    if (failure_count > 0) {
        pr_err("Memory Collector: Failed to reset RMIDs to default on %d CPUs\n", failure_count);
        return -EIO;
    }
    
    pr_info("Memory Collector: Successfully reset all RMIDs to default\n");
    return 0;
}

int read_rmid_mbm(u32 rmid, u64 *val)
{
    int err;
    
    err = wrmsr_safe(MSR_IA32_QM_EVTSEL, 
                     rmid,
                     QOS_L3_MBM_TOTAL_EVENT_ID);
    if (err)
        return err;

    err = rdmsrl_safe(MSR_IA32_QM_CTR, val);
    if (err)
        return err;
    
    if (*val & RMID_VAL_ERROR)
        return -EIO;
    if (*val & RMID_VAL_UNAVAIL) 
        return -EINVAL;
        
    return 0;
} 