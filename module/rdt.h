#ifndef _COLLECTOR_RESCTRL_H_
#define _COLLECTOR_RESCTRL_H_

#include <linux/types.h>

// per-CPU state for RDT
struct rdt_state {
    bool supports_llc_occupancy;
    bool supports_mbm_total;
    bool supports_mbm_local;
    bool has_overflow_bit;
    bool supports_non_cpu_agent_mbm;
    bool supports_non_cpu_agent_cache;
    u32 max_rmid;
    u32 counter_width;
};

/* Function declarations */

/*
 * Initialize RDT state for given CPU
 * Returns 0 on success, negative error code on failure
 */
int rdt_init_cpu(struct rdt_state *rdt_state);

/*
 * Read RDT counter for given RMID
 * val is set to the counter value on success
 * Returns -EIO if error occurred
 * Returns -EINVAL if data unavailable
 */
int rdt_read_llc_occupancy(u32 rmid, u64 *val);
int rdt_read_mbm_total(u32 rmid, u64 *val);
int rdt_read_mbm_local(u32 rmid, u64 *val);

/*
 * Write RMID and CLOSID to MSR_IA32_PQR_ASSOC
 * Returns 0 on success, negative error code on failure
 */
int rdt_write_rmid_closid(u32 rmid, u32 closid);

#endif /* _COLLECTOR_RESCTRL_H_ */ 