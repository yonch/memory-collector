#ifndef _COLLECTOR_RESCTRL_H_
#define _COLLECTOR_RESCTRL_H_


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
int resctrl_init_cpu(struct rdt_state *rdt_state);

/*
 * Read memory bandwidth counter for given RMID and output to trace
 */
void resctrl_timer_tick(struct rdt_state *rdt_state);

/*
 * Read memory bandwidth counter for given RMID
 * val is set to the bandwidth value on success
 * Returns -EIO if error occurred
 * Returns -EINVAL if data unavailable
 */
int read_rmid_mbm(u32 rmid, u64 *val);

#endif /* _COLLECTOR_RESCTRL_H_ */ 