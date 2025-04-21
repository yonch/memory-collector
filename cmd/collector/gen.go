package main

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang -type msg_type -type perf_measurement_msg -type rmid_alloc_msg -type rmid_free_msg bpf collector.c protocol.bpf.c
