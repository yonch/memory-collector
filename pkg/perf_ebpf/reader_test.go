package perf_ebpf

import (
	"testing"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/rlimit"
)

func TestNewPerfMapReader(t *testing.T) {
	// Create a test eBPF map
	mapSpec := &ebpf.MapSpec{
		Type:       ebpf.PerfEventArray,
		KeySize:    4,
		ValueSize:  4,
		MaxEntries: 4, // Support 4 CPUs for testing
	}

	// remove memlock limit
	if err := rlimit.RemoveMemlock(); err != nil {
		t.Fatalf("failed to remove memlock: %v", err)
	}

	array, err := ebpf.NewMap(mapSpec)
	if err != nil {
		t.Fatalf("failed to create map: %v", err)
	}
	defer array.Close()

	tests := []struct {
		name    string
		array   *ebpf.Map
		opts    Options
		wantErr bool
	}{
		{
			name:    "nil array",
			array:   nil,
			opts:    Options{BufferSize: 4096, WatermarkBytes: 1024},
			wantErr: true,
		},
		{
			name:    "zero buffer size",
			array:   array,
			opts:    Options{BufferSize: 0, WatermarkBytes: 1024},
			wantErr: true,
		},
		{
			name:    "watermark too large",
			array:   array,
			opts:    Options{BufferSize: 4096, WatermarkBytes: 4096},
			wantErr: true,
		},
		{
			name:    "valid options",
			array:   array,
			opts:    Options{BufferSize: 4096, WatermarkBytes: 1024},
			wantErr: false,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			reader, err := NewPerfMapReader(tt.array, tt.opts)
			if tt.wantErr {
				if err == nil {
					t.Error("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Errorf("unexpected error: %v", err)
				return
			}
			defer reader.Close()

			// Verify reader is properly initialized
			if reader.Reader() == nil {
				t.Error("expected non-nil reader")
			}
		})
	}
}
