#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory_collector

#if !defined(_MEMORY_COLLECTOR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MEMORY_COLLECTOR_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(memory_collector_sample,
    TP_PROTO(u32 cpu, u64 timestamp, const char *comm, bool is_context_switch),
    
    TP_ARGS(cpu, timestamp, comm, is_context_switch),
    
    TP_STRUCT__entry(
        __field(u32, cpu)
        __field(u64, timestamp)
        __array(char, comm, TASK_COMM_LEN)
        __field(bool, is_context_switch)
    ),
    
    TP_fast_assign(
        __entry->cpu = cpu;
        __entry->timestamp = timestamp;
        memcpy(__entry->comm, comm, TASK_COMM_LEN);
        __entry->is_context_switch = is_context_switch;
    ),
    
    TP_printk("cpu=%u timestamp=%llu comm=%s is_context_switch=%d",
        __entry->cpu, __entry->timestamp, __entry->comm, __entry->is_context_switch)
);

TRACE_EVENT(memory_collector_mbm_total,
    TP_PROTO(u32 rmid, u64 timestamp, u64 val, int err),
    TP_ARGS(rmid, timestamp, val, err),
    TP_STRUCT__entry(
        __field(u32, rmid)
        __field(u64, timestamp)
        __field(u64, val)
        __field(int, err)
    ),
    
    TP_fast_assign(
        __entry->rmid = rmid;
        __entry->timestamp = timestamp;
        __entry->val = val;
        __entry->err = err;
    ),
    
    TP_printk("rmid=%u timestamp=%llu val=%llu err=%d",
        __entry->rmid, __entry->timestamp, __entry->val, __entry->err)
);

#endif /* _MEMORY_COLLECTOR_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE memory_collector_trace

/* This part must be outside protection */
#include <trace/define_trace.h> 