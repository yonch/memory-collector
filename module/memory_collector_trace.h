#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory_collector

#if !defined(_MEMORY_COLLECTOR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MEMORY_COLLECTOR_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(memory_collector_sample,
    TP_PROTO(u32 core_id, u64 timestamp, const char *comm, u64 llc_misses),
    
    TP_ARGS(core_id, timestamp, comm, llc_misses),
    
    TP_STRUCT__entry(
        __field(u32, core_id)
        __field(u64, timestamp)
        __array(char, comm, 16)
        __field(u64, llc_misses)
    ),
    
    TP_fast_assign(
        __entry->core_id = core_id;
        __entry->timestamp = timestamp;
        memcpy(__entry->comm, comm, 16);
        __entry->llc_misses = llc_misses;
    ),
    
    TP_printk("cpu=%u timestamp=%llu comm=%s llc_misses=%llu",
        __entry->core_id,
        __entry->timestamp,
        __entry->comm,
        __entry->llc_misses)
);

#endif /* _MEMORY_COLLECTOR_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE memory_collector_trace

/* This part must be outside protection */
#include <trace/define_trace.h> 