#ifndef PROTOCOL_BPF_H
#define PROTOCOL_BPF_H

// Struct for passing perf measurement parameters
struct perf_measurement_params {
    __u32 rmid;
    __u64 cycles_delta;
    __u64 instructions_delta;
    __u64 llc_misses_delta;
    __u64 time_delta_ns;
    __u64 timestamp;
};

// Function to send perf measurement data
void send_perf_measurement(void *ctx, struct perf_measurement_params *params);

// Function to send RMID allocation message
void send_rmid_alloc(void *ctx, __u32 rmid, const char *comm, __u32 tgid, __u64 timestamp);

// Function to send RMID free message
void send_rmid_free(void *ctx, __u32 rmid, __u64 timestamp);

#endif // PROTOCOL_BPF_H 