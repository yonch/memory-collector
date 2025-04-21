module github.com/unvariance/collector

go 1.23.0

toolchain go1.23.8

// replace github.com/cilium/ebpf/cmd/bpf2go => github.com/yonch/cilium-ebpf/cmd/bpf2go v0.0.0-20250419025143-874e9a29af02
replace github.com/cilium/ebpf => github.com/yonch/cilium-ebpf v0.0.0-20250419031303-b709db450429
require (
	github.com/cilium/ebpf v0.18.0
	github.com/elastic/go-perf v0.0.0-20241029065020-30bec95324b8
	github.com/go-quicktest/qt v1.101.1-0.20240301121107-c6c8733fa1e6
	github.com/prometheus/client_golang v1.21.0
	github.com/stretchr/testify v1.10.0
	github.com/xitongsys/parquet-go v1.6.2
	github.com/xitongsys/parquet-go-source v0.0.0-20241021075129-b732d2ac9c9b
	golang.org/x/sys v0.31.0
)

require (
	github.com/apache/arrow/go/arrow v0.0.0-20200730104253-651201b0f516 // indirect
	github.com/apache/thrift v0.14.2 // indirect
	github.com/beorn7/perks v1.0.1 // indirect
	github.com/cespare/xxhash/v2 v2.3.0 // indirect
	github.com/davecgh/go-spew v1.1.1 // indirect
	github.com/golang/snappy v0.0.3 // indirect
	github.com/google/go-cmp v0.6.0 // indirect
	github.com/klauspost/compress v1.17.11 // indirect
	github.com/kr/pretty v0.3.1 // indirect
	github.com/kr/text v0.2.0 // indirect
	github.com/kylelemons/godebug v1.1.0 // indirect
	github.com/munnerz/goautoneg v0.0.0-20191010083416-a7dc8b61c822 // indirect
	github.com/pierrec/lz4/v4 v4.1.8 // indirect
	github.com/pmezard/go-difflib v1.0.0 // indirect
	github.com/prometheus/client_model v0.6.1 // indirect
	github.com/prometheus/common v0.62.0 // indirect
	github.com/prometheus/procfs v0.15.1 // indirect
	github.com/rogpeppe/go-internal v1.12.0 // indirect
	golang.org/x/xerrors v0.0.0-20200804184101-5ec99f83aff1 // indirect
	google.golang.org/protobuf v1.36.1 // indirect
	gopkg.in/yaml.v3 v3.0.1 // indirect
)
