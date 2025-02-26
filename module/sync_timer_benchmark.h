#undef TRACE_SYSTEM
#define TRACE_SYSTEM sync_timer_benchmark

#if !defined(_SYNC_TIMER_BENCHMARK_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _SYNC_TIMER_BENCHMARK_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(sync_timer_stats,
    TP_PROTO(u64 timestamp, u64 tick_number, u64 min_delay, u64 max_delay,
             u64 mean_delay, u64 stddev, u32 sample_count, u32 missing_count),
    
    TP_ARGS(timestamp, tick_number, min_delay, max_delay, mean_delay, stddev,
            sample_count, missing_count),
    
    TP_STRUCT__entry(
        __field(u64, timestamp)
        __field(u64, tick_number)
        __field(u64, min_delay)
        __field(u64, max_delay)
        __field(u64, mean_delay)
        __field(u64, stddev)
        __field(u32, sample_count)
        __field(u32, missing_count)
    ),
    
    TP_fast_assign(
        __entry->timestamp = timestamp;
        __entry->tick_number = tick_number;
        __entry->min_delay = min_delay;
        __entry->max_delay = max_delay;
        __entry->mean_delay = mean_delay;
        __entry->stddev = stddev;
        __entry->sample_count = sample_count;
        __entry->missing_count = missing_count;
    ),
    
    TP_printk("ts=%llu tick=%llu min=%llu max=%llu mean=%llu stddev=%llu samples=%u missing=%u",
        __entry->timestamp, __entry->tick_number, __entry->min_delay,
        __entry->max_delay, __entry->mean_delay, __entry->stddev,
        __entry->sample_count, __entry->missing_count)
);

#endif /* _SYNC_TIMER_BENCHMARK_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE sync_timer_benchmark

/* This part must be outside protection */
#include <trace/define_trace.h> 