#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>
#include <linux/resctrl.h>
#include <asm/resctrl.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/atomic.h>

#include "rdt.h"
#include "memory_collector_trace.h"
#include "collector.h"

#define RMID_VAL_ERROR BIT_ULL(63)
#define RMID_VAL_UNAVAIL BIT_ULL(62)

/* Structure to pass RMID to IPI function */
struct ipi_rmid_args {
    u32 rmid;
    int status;
};
/*
 * write RMID and CLOSID to MSR
 */
int rdt_write_rmid_closid(u32 rmid, u32 closid)
{
    return wrmsr_safe(MSR_IA32_PQR_ASSOC, rmid, closid);
}

void rdt_timer_tick(struct rdt_state *rdt_state)
{
    int cpu = smp_processor_id();
    u64 now = ktime_get_ns();
    int llc_occupancy_err = 0;
    u64 llc_occupancy_val = 0;
    int mbm_total_err = 0;
    u64 mbm_total_val = 0;
    int mbm_local_err = 0;
    u64 mbm_local_val = 0;

    // for now, just output the first 4 RMID, on CPUs 0..3
    if (cpu > 4) {
        return;
    }

    // if we support cache, read it on this CPU
    if (rdt_state->supports_llc_occupancy) {
        llc_occupancy_err = rdt_read_resctrl_value(cpu, QOS_L3_OCCUP_EVENT_ID, &llc_occupancy_val);
    } else {
        llc_occupancy_err = -ENODEV;
    }

    // if we support mbm, read it on this CPU
    if (rdt_state->supports_mbm_total) {
        mbm_total_err = rdt_read_resctrl_value(cpu, QOS_L3_MBM_TOTAL_EVENT_ID, &mbm_total_val);
    } else {
        mbm_total_err = -ENODEV;
    }

    // if we support mbm local, read it on this CPU
    if (rdt_state->supports_mbm_local) {
        mbm_local_err = rdt_read_resctrl_value(cpu, QOS_L3_MBM_LOCAL_EVENT_ID, &mbm_local_val);
    } else {
        mbm_local_err = -ENODEV;
    }

    trace_memory_collector_resctrl(cpu, now, llc_occupancy_val, llc_occupancy_err, mbm_total_val, mbm_total_err, mbm_local_val, mbm_local_err);
}

int rdt_init_cpu(struct rdt_state *rdt_state)
{
    int cpu = smp_processor_id();
    unsigned int eax, ebx, ecx, edx;

    pr_debug(LOG_PREFIX "Starting enumerate_cpuid on CPU %d\n", cpu);

    memset(rdt_state, 0, sizeof(struct rdt_state));

    // Check for RDT monitoring support
    if (!boot_cpu_has(X86_FEATURE_CQM_LLC)) {
        pr_debug(LOG_PREFIX "CPU does not support QoS monitoring\n");
        return 0;  // Return success but with no capabilities
    }

    pr_debug(LOG_PREFIX "Checking CPUID.0x7.0 for RDT support\n");
    cpuid_count(0x7, 0, &eax, &ebx, &ecx, &edx);
    if (!(ebx & (1 << 12))) {
        pr_debug(LOG_PREFIX "RDT monitoring not supported (CPUID.0x7.0:EBX.12)\n");
        return 0;  // Return success but with no capabilities
    }

    pr_debug(LOG_PREFIX "Checking CPUID.0xF.0 for L3 monitoring\n");
    cpuid_count(0xF, 0, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1 << 1))) {
        pr_debug(LOG_PREFIX "L3 monitoring not supported (CPUID.0xF.0:EDX.1)\n");
        return 0;  // Return success but with no capabilities
    }

    pr_debug(LOG_PREFIX "Checking CPUID.0xF.1 for L3 occupancy monitoring\n");
    cpuid_count(0xF, 1, &eax, &ebx, &ecx, &edx);
    rdt_state->supports_llc_occupancy = (edx & (1 << 0));
    rdt_state->supports_mbm_total = (edx & (1 << 1));
    rdt_state->supports_mbm_local = (edx & (1 << 2));
    rdt_state->max_rmid = ecx;
    rdt_state->counter_width = (eax & 0xFF) + 24;
    rdt_state->has_overflow_bit = (eax & (1 << 8));
    rdt_state->supports_non_cpu_agent_cache = (eax & (1 << 8));
    rdt_state->supports_non_cpu_agent_mbm = (eax & (1 << 10));

    pr_debug(LOG_PREFIX "capabilities of core %d: llc_occupancy: %d, mbm_total: %d, mbm_local: %d, max_rmid: %d, counter_width: %d, has_overflow_bit: %d, supports_non_cpu_agent_cache: %d, supports_non_cpu_agent_mbm: %d\n", 
             cpu, rdt_state->supports_llc_occupancy, rdt_state->supports_mbm_total, rdt_state->supports_mbm_local, rdt_state->max_rmid, rdt_state->counter_width, rdt_state->has_overflow_bit, rdt_state->supports_non_cpu_agent_cache, rdt_state->supports_non_cpu_agent_mbm);


    pr_debug(LOG_PREFIX "enumerate_cpuid completed successfully on CPU %d\n", cpu);
    return 0;
}

int rdt_read_resctrl_value(u32 rmid, u32 event_id, u64 *val)
{
    int err;
    
    err = wrmsr_safe(MSR_IA32_QM_EVTSEL, 
                     event_id,
                     rmid);
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