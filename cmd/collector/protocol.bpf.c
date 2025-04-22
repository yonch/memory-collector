//go:build ignore

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "protocol.bpf.h"

#define TASK_COMM_LEN 16

// Perf event array for all events
struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, 0);
    __uint(value_size, 0);
} events SEC(".maps");


// Message types for all events
enum msg_type {
    MSG_TYPE_PERF = 0,
    MSG_TYPE_RMID_ALLOC = 1,
    MSG_TYPE_RMID_FREE = 2,
};
const enum msg_type *unused_bpf2go_generate_msg_type __attribute__((unused)); // force golang generation of the enum

// Define the event structure that matches the Go side
struct perf_measurement_msg {
    __u64 timestamp;  // Must be first field for ring buffer ordering
    __u32 type;      // MSG_TYPE_PERF
    __u32 rmid;
    __u64 cycles_delta;
    __u64 instructions_delta;
    __u64 llc_misses_delta;
    __u64 time_delta_ns;
};
const struct perf_measurement_msg *unused_bpf2go_generate_perf_measurement_msg __attribute__((unused)); // force golang generation of the struct

// Structure for RMID allocation messages
struct rmid_alloc_msg {
    __u64 timestamp;  // Must be first field for ring buffer ordering
    __u32 type;      // MSG_TYPE_RMID_ALLOC
    __u32 rmid;
    char comm[TASK_COMM_LEN];
    __u32 tgid;
};
const struct rmid_alloc_msg *unused_bpf2go_generate_rmid_alloc_msg __attribute__((unused)); // force golang generation of the struct

// Structure for RMID free messages
struct rmid_free_msg {
    __u64 timestamp;  // Must be first field for ring buffer ordering
    __u32 type;      // MSG_TYPE_RMID_FREE
    __u32 rmid;
};
const struct rmid_free_msg *unused_bpf2go_generate_rmid_free_msg __attribute__((unused)); // force golang generation of the struct

// Helper function to send RMID allocation message
void send_rmid_alloc(void *ctx, __u32 rmid, const char *comm, __u32 tgid, __u64 timestamp) {
    struct rmid_alloc_msg msg = {
        .timestamp = timestamp,
        .type = MSG_TYPE_RMID_ALLOC,
        .rmid = rmid,
        .tgid = tgid,
    };
    __builtin_memcpy(msg.comm, comm, TASK_COMM_LEN);
    
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &msg, sizeof(msg));
}

// Helper function to send RMID free message
void send_rmid_free(void *ctx, __u32 rmid, __u64 timestamp) {
    struct rmid_free_msg msg = {
        .timestamp = timestamp,
        .type = MSG_TYPE_RMID_FREE,
        .rmid = rmid,
    };
    
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &msg, sizeof(msg));
}

// Helper function to send perf measurement data
void send_perf_measurement(void *ctx, struct perf_measurement_params *params) {
    struct perf_measurement_msg msg = {
        .timestamp = params->timestamp,
        .type = MSG_TYPE_PERF,
        .rmid = params->rmid,
        .cycles_delta = params->cycles_delta,
        .instructions_delta = params->instructions_delta,
        .llc_misses_delta = params->llc_misses_delta,
        .time_delta_ns = params->time_delta_ns
    };
    
    bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &msg, sizeof(msg));
}
