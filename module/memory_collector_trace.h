#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory_collector

#if !defined(_MEMORY_COLLECTOR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MEMORY_COLLECTOR_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(memory_collector_sample,
    TP_PROTO(u32 cpu, u64 timestamp, const char *comm, u64 llc_misses, u64 cycles, u64 instructions),
    
    TP_ARGS(cpu, timestamp, comm, llc_misses, cycles, instructions),
    
    TP_STRUCT__entry(
        __field(u32, cpu)
        __field(u64, timestamp)
        __array(char, comm, TASK_COMM_LEN)
        __field(u64, llc_misses)
        __field(u64, cycles)
        __field(u64, instructions)
    ),
    
    TP_fast_assign(
        __entry->cpu = cpu;
        __entry->timestamp = timestamp;
        memcpy(__entry->comm, comm, TASK_COMM_LEN);
        __entry->llc_misses = llc_misses;
        __entry->cycles = cycles;
        __entry->instructions = instructions;
    ),
    
    TP_printk("cpu=%u timestamp=%llu comm=%s llc_misses=%llu cycles=%llu instructions=%llu",
        __entry->cpu, __entry->timestamp, __entry->comm,
        __entry->llc_misses, __entry->cycles, __entry->instructions)
);

#endif /* _MEMORY_COLLECTOR_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE memory_collector_trace

/* This part must be outside protection */
#include <trace/define_trace.h> 