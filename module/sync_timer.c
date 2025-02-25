#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include "sync_timer.h"
#include "collector.h"

/* Structure for CPU timer initialization */
struct timer_init_data {
    struct work_struct work;
    struct sync_timer *timer;
};

/* Internal function to initialize timer on a specific CPU */
static void init_cpu_timer(struct work_struct *work)
{
    struct timer_init_data *init_data = container_of(work, struct timer_init_data, work);
    struct sync_timer *timer = init_data->timer;
    struct sync_timer_cpu *cpu_timer;
    ktime_t now;
    int cpu = smp_processor_id();

    cpu_timer = per_cpu_ptr(timer->cpu_timers, cpu);
    
    /* Initialize HR timer */
    hrtimer_init(&cpu_timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
    cpu_timer->timer.function = timer->timer_fn;

    /* Calculate next interval boundary */
    now = ktime_get();
    cpu_timer->next_expected = ktime_add_ns(now, timer->interval_ns);
    cpu_timer->next_expected = ktime_sub_ns(cpu_timer->next_expected, ktime_to_ns(cpu_timer->next_expected) % timer->interval_ns);

    pr_debug(LOG_PREFIX "Initializing timer on CPU %d, interval: %llu ns, now: %lld ns, next_expected: %lld ns\n", cpu, timer->interval_ns, ktime_to_ns(now), ktime_to_ns(cpu_timer->next_expected));

    /* Start the timer */
    hrtimer_start(&cpu_timer->timer, cpu_timer->next_expected, HRTIMER_MODE_ABS_PINNED);
}

int sync_timer_init(struct sync_timer *timer,
                   enum hrtimer_restart (*timer_fn)(struct hrtimer *),
                   u64 interval_ns)
{
    int ret = -EINVAL;
    int cpu;
    struct workqueue_struct *wq = NULL;
    struct timer_init_data __percpu *init_data = NULL;

    if (!timer || !timer_fn || !interval_ns)
        return -EINVAL;

    /* Initialize timer structure */
    timer->timer_fn = timer_fn;
    timer->interval_ns = interval_ns;
    timer->initialized = false;

    /* Allocate per-CPU timer structures */
    timer->cpu_timers = alloc_percpu(struct sync_timer_cpu);
    if (!timer->cpu_timers) {
        pr_err(LOG_PREFIX "Failed to allocate per-CPU timer structures\n");
        ret = -ENOMEM;
        goto err_out;
    }

    /* Create temporary workqueue for initialization */
    wq = alloc_workqueue("sync_timer_init", /* NB: DO _NOT_ USE WQ_UNBOUND! */ 0, 0);
    if (!wq) {
        pr_err(LOG_PREFIX "Failed to create workqueue\n");
        ret = -ENOMEM;
        goto err_free_cpu_timers;
    }

    /* Allocate initialization data structures */
    init_data = alloc_percpu(struct timer_init_data);
    if (!init_data) {
        pr_err(LOG_PREFIX "Failed to allocate initialization data\n");
        ret = -ENOMEM;
        goto err_destroy_workqueue;
    }

    /* Initialize and queue work for each CPU */
    for_each_online_cpu(cpu) {
        struct timer_init_data *cpu_init_data = per_cpu_ptr(init_data, cpu);
        cpu_init_data->timer = timer;
        INIT_WORK(&cpu_init_data->work, init_cpu_timer);
        queue_work_on(cpu, wq, &cpu_init_data->work);
    }

    /* Wait for all initialization work to complete */
    flush_workqueue(wq);

    /* Clean up temporary resources */
    destroy_workqueue(wq);
    free_percpu(init_data);

    timer->initialized = true;
    return 0;

err_destroy_workqueue:
    destroy_workqueue(wq);
err_free_cpu_timers:
    free_percpu(timer->cpu_timers);
err_out:
    return ret;
}

void sync_timer_destroy(struct sync_timer *timer)
{
    int cpu;

    if (!timer || !timer->initialized)
        return;

    /* Cancel timers on all CPUs */
    for_each_online_cpu(cpu) {
        struct sync_timer_cpu *cpu_timer = per_cpu_ptr(timer->cpu_timers, cpu);
        hrtimer_cancel(&cpu_timer->timer);
    }

    /* Free per-CPU structures */
    free_percpu(timer->cpu_timers);
    timer->initialized = false;
}

enum hrtimer_restart sync_timer_restart(struct hrtimer *timer,
                                      struct sync_timer *timer_data)
{
    struct sync_timer_cpu *cpu_timer;
    ktime_t now;

    if (!timer_data || !timer_data->initialized)
        return HRTIMER_NORESTART;

    cpu_timer = container_of(timer, struct sync_timer_cpu, timer);
    now = ktime_get();

    /* Calculate next interval boundary */
    cpu_timer->next_expected = ktime_add_ns(now, timer_data->interval_ns);
    cpu_timer->next_expected = ktime_sub_ns(cpu_timer->next_expected, ktime_to_ns(cpu_timer->next_expected) % timer_data->interval_ns);

    /* Set next expiration time */
    hrtimer_set_expires(timer, cpu_timer->next_expected);
    return HRTIMER_RESTART;
} 