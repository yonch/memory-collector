#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <linux/resctrl.h>
#include <asm/resctrl.h>
#include <linux/module.h>

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
    u32 max_leaf = 0;
    u32 has_rdt = 0;
    u32 has_cmt = 0;
    u32 highest_rmid = 0;

    max_leaf = cpuid_eax(0);
    pr_info("Memory Collector: max_leaf: %u\n", max_leaf);

    if (max_leaf < 7) {
        pr_err("Memory Collector: max_leaf is less than 7\n");
        return -EINVAL;
    }

    has_rdt = (cpuid_ebx(0x7) >> 12) & 0x1;
    pr_info("Memory Collector: has_rdt: %u\n", has_rdt);

    if (!has_rdt) {
        pr_err("Memory Collector: has_rdt is 0\n");
        return -EINVAL;
    }

    has_cmt = (cpuid_edx(0xf) >> 1) & 0x1;
    pr_info("Memory Collector: has_cmt: %u\n", has_cmt);

    if (!has_cmt) {
        pr_err("Memory Collector: has_cmt is 0\n");
        return -EINVAL;
    }
    
    highest_rmid = cpuid_ebx(0xf);
    pr_info("Memory Collector: highest_rmid: %u\n", highest_rmid);

    return 0;
}

int resctrl_init(void)
{
    int cpu;
    int ret = 0;

    ret = enumerate_cpuid();
    if (ret) {
        pr_err("Memory Collector: Failed to enumerate CPUID\n");
        return ret;
    }

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
    
    return ret;
}

int resctrl_exit(void) 
{
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