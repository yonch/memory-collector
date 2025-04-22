//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "protocol.bpf.h"


struct events_map events SEC(".maps");


// Force golang generation of enums and structs
const enum msg_type *unused_bpf2go_generate_msg_type __attribute__((unused));
const struct perf_measurement_msg *unused_bpf2go_generate_perf_measurement_msg __attribute__((unused));
const struct task_metadata_msg *unused_bpf2go_generate_task_metadata_msg __attribute__((unused));
const struct task_free_msg *unused_bpf2go_generate_task_free_msg __attribute__((unused)); 