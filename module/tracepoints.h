#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory_collector

#if !defined(_MEMORY_COLLECTOR_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MEMORY_COLLECTOR_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(memory_collector_sample,
    TP_PROTO(bool is_context_switch, u32 rmid),
    
    TP_ARGS(is_context_switch, rmid),
    
    TP_STRUCT__entry(
        __field(u8, is_context_switch)
        __field(u32, rmid)
    ),
    
    TP_fast_assign(
        __entry->is_context_switch = is_context_switch;
        __entry->rmid = rmid;
    ),
    
    TP_printk("context_switch=%d rmid=%u",
        __entry->is_context_switch, __entry->rmid)
);

TRACE_EVENT(memory_collector_resctrl,
    TP_PROTO(u32 rmid, u64 timestamp, u64 llc_occupancy_val, int llc_occupancy_err, u64 mbm_total_val, int mbm_total_err, u64 mbm_local_val, int mbm_local_err),
    TP_ARGS(rmid, timestamp, llc_occupancy_val, llc_occupancy_err, mbm_total_val, mbm_total_err, mbm_local_val, mbm_local_err),
    TP_STRUCT__entry(
        __field(u32, rmid)
        __field(u64, timestamp)
        __field(u64, llc_occupancy_val)
        __field(int, llc_occupancy_err)
        __field(u64, mbm_total_val)
        __field(int, mbm_total_err)
        __field(u64, mbm_local_val)
        __field(int, mbm_local_err)
    ),
    
    TP_fast_assign(
        __entry->rmid = rmid;
        __entry->timestamp = timestamp;
        __entry->llc_occupancy_val = llc_occupancy_val;
        __entry->llc_occupancy_err = llc_occupancy_err;
        __entry->mbm_total_val = mbm_total_val;
        __entry->mbm_total_err = mbm_total_err;
        __entry->mbm_local_val = mbm_local_val;
        __entry->mbm_local_err = mbm_local_err;
    ),
    
    TP_printk("rmid=%u timestamp=%llu llc_occupancy_val=%llu llc_occupancy_err=%d mbm_total_val=%llu mbm_total_err=%d mbm_local_val=%llu mbm_local_err=%d",
        __entry->rmid, __entry->timestamp, __entry->llc_occupancy_val, __entry->llc_occupancy_err, __entry->mbm_total_val, __entry->mbm_total_err, __entry->mbm_local_val, __entry->mbm_local_err)
);

// New tracepoint for RMID allocation
TRACE_EVENT(memory_collector_rmid_alloc,
    TP_PROTO(u32 rmid, const char *comm, pid_t tgid, u64 timestamp),
    TP_ARGS(rmid, comm, tgid, timestamp),
    TP_STRUCT__entry(
        __field(u32, rmid)
        __array(char, comm, TASK_COMM_LEN)
        __field(pid_t, tgid)
        __field(u64, timestamp)
    ),
    TP_fast_assign(
        __entry->rmid = rmid;
        memcpy(__entry->comm, comm, TASK_COMM_LEN);
        __entry->tgid = tgid;
        __entry->timestamp = timestamp;
    ),
    TP_printk("rmid=%u comm=%s tgid=%d timestamp=%llu",
        __entry->rmid, __entry->comm, __entry->tgid, __entry->timestamp)
);

// New tracepoint for RMID deallocation
TRACE_EVENT(memory_collector_rmid_free,
    TP_PROTO(u32 rmid, u64 timestamp),
    TP_ARGS(rmid, timestamp),
    TP_STRUCT__entry(
        __field(u32, rmid)
        __field(u64, timestamp)
    ),
    TP_fast_assign(
        __entry->rmid = rmid;
        __entry->timestamp = timestamp;
    ),
    TP_printk("rmid=%u timestamp=%llu",
        __entry->rmid, __entry->timestamp)
);

// New tracepoint for existing RMID dump
TRACE_EVENT(memory_collector_rmid_existing,
    TP_PROTO(u32 rmid, const char *comm, pid_t tgid, u64 timestamp),
    TP_ARGS(rmid, comm, tgid, timestamp),
    TP_STRUCT__entry(
        __field(u32, rmid)
        __array(char, comm, TASK_COMM_LEN)
        __field(pid_t, tgid)
        __field(u64, timestamp)
    ),
    TP_fast_assign(
        __entry->rmid = rmid;
        memcpy(__entry->comm, comm, TASK_COMM_LEN);
        __entry->tgid = tgid;
        __entry->timestamp = timestamp;
    ),
    TP_printk("rmid=%u comm=%s tgid=%d timestamp=%llu",
        __entry->rmid, __entry->comm, __entry->tgid, __entry->timestamp)
);

#endif /* _MEMORY_COLLECTOR_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE tracepoints

/* This part must be outside protection */
#include <trace/define_trace.h> 