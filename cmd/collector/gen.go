package main

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang -type msg_type -type event -type rmid_alloc_msg -type rmid_free_msg taskCounter task_counter.c
