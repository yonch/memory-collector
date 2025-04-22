//go:build ignore

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "protocol.bpf.h"


struct events_map events SEC(".maps");


// Force golang generation of enums and structs
const enum msg_type *unused_bpf2go_generate_msg_type __attribute__((unused));
const struct perf_measurement_msg *unused_bpf2go_generate_perf_measurement_msg __attribute__((unused));
const struct rmid_alloc_msg *unused_bpf2go_generate_rmid_alloc_msg __attribute__((unused));
const struct rmid_free_msg *unused_bpf2go_generate_rmid_free_msg __attribute__((unused)); 