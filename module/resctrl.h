#ifndef _COLLECTOR_RESCTRL_H_
#define _COLLECTOR_RESCTRL_H_


/* Function declarations */

/*
 * Initialize RMIDs per CPU via IPI
 * Returns 0 on success, negative error code on failure
 */
int resctrl_init(void);

/*
 * Reset all CPU RMIDs to default via IPI
 * Returns 0 on success, negative error code on failure
 */
int resctrl_exit(void);

/*
 * Read memory bandwidth counter for given RMID
 * val is set to the bandwidth value on success
 * Returns -EIO if error occurred
 * Returns -EINVAL if data unavailable
 */
int read_rmid_mbm(u32 rmid, u64 *val);

#endif /* _COLLECTOR_RESCTRL_H_ */ 