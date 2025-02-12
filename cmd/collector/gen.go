package main

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go -target bpfel -cc clang taskCounter task_counter.c
