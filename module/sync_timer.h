#ifndef _SYNC_TIMER_H
#define _SYNC_TIMER_H

#include <linux/hrtimer.h>
#include <linux/types.h>

/* Per-CPU timer state */
struct sync_timer_cpu {
    struct hrtimer timer;         /* HR timer for this CPU */
    ktime_t next_expected;        /* Next expected timestamp */
};

/* Main synchronous timer structure */
struct sync_timer {
    struct sync_timer_cpu __percpu *cpu_timers;  /* Per-CPU timers */
    enum hrtimer_restart (*timer_fn)(struct hrtimer *);  /* Timer callback */
    u64 interval_ns;              /* Timer interval in nanoseconds */
    bool initialized;             /* Initialization state */
};

/**
 * sync_timer_init - Initialize synchronous timers on all CPUs
 * @timer: Uninitialized timer structure to populate
 * @timer_fn: Callback function to run on timer expiration
 * @interval_ns: Timer interval in nanoseconds
 *
 * Initializes high-resolution timers on all CPUs that trigger
 * synchronously at interval boundaries. After return, all timers
 * are active and will trigger at the next interval boundary.
 *
 * Return: 0 on success, negative error code on failure
 */
int sync_timer_init(struct sync_timer *timer, 
                   enum hrtimer_restart (*timer_fn)(struct hrtimer *),
                   u64 interval_ns);

/**
 * sync_timer_destroy - Clean up and cancel all timers
 * @timer: Timer structure to clean up
 *
 * Cancels all timers and ensures all timer handlers have finished
 * executing before returning. After this function returns, no more
 * timer callbacks will execute.
 */
void sync_timer_destroy(struct sync_timer *timer);

/**
 * sync_timer_restart - Reset timer to next interval boundary
 * @timer: HR timer that expired
 * @timer_data: Associated sync_timer structure
 *
 * Computes the next interval boundary time and resets the timer.
 * This should be called from timer callbacks to maintain synchronization.
 *
 * Return: HRTIMER_RESTART to keep the timer active
 */
enum hrtimer_restart sync_timer_restart(struct hrtimer *timer, 
                                      struct sync_timer *timer_data);

#endif /* _SYNC_TIMER_H */ 