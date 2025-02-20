package perf

import (
	"runtime"
	"testing"

	"golang.org/x/sys/unix"
)

func TestMemoryRingStorage(t *testing.T) {
	nPages := uint32(2)
	storage, err := NewMemoryRingStorage(nPages)
	if err != nil {
		t.Fatalf("failed to create memory storage: %v", err)
	}
	defer storage.Close()

	// Check basic properties
	if storage.NumDataPages() != nPages {
		t.Errorf("expected %d pages, got %d", nPages, storage.NumDataPages())
	}

	if storage.PageSize() != uint64(unix.Getpagesize()) {
		t.Errorf("expected page size %d, got %d", unix.Getpagesize(), storage.PageSize())
	}

	expectedSize := storage.PageSize() * (1 + uint64(nPages))
	if uint64(len(storage.Data())) != expectedSize {
		t.Errorf("expected data size %d, got %d", expectedSize, len(storage.Data()))
	}

	if fd := storage.FileDescriptor(); fd != -1 {
		t.Errorf("expected file descriptor -1, got %d", fd)
	}
}

func TestMmapRingStorage(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("skipping test on non-linux platform")
	}

	nPages := uint32(2)
	storage, err := NewMmapRingStorage(0, nPages, 0) // Wake up on every event
	if err != nil {
		t.Fatalf("failed to create mmap storage: %v", err)
	}
	defer storage.Close()

	// Check basic properties
	if storage.NumDataPages() != nPages {
		t.Errorf("expected %d pages, got %d", nPages, storage.NumDataPages())
	}

	if storage.PageSize() != uint64(unix.Getpagesize()) {
		t.Errorf("expected page size %d, got %d", unix.Getpagesize(), storage.PageSize())
	}

	expectedSize := storage.PageSize() * (1 + uint64(nPages))
	if uint64(len(storage.Data())) != expectedSize {
		t.Errorf("expected data size %d, got %d", expectedSize, len(storage.Data()))
	}

	if fd := storage.FileDescriptor(); fd <= 0 {
		t.Errorf("expected valid file descriptor, got %d", fd)
	}
}

func TestMmapRingStorageClose(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("skipping test on non-linux platform")
	}

	nPages := uint32(2)
	storage, err := NewMmapRingStorage(0, nPages, 0)
	if err != nil {
		t.Fatalf("failed to create mmap storage: %v", err)
	}

	// Test double close
	if err := storage.Close(); err != nil {
		t.Errorf("first close failed: %v", err)
	}

	if err := storage.Close(); err != nil {
		t.Errorf("second close failed: %v", err)
	}
}

func TestMmapRingStorageWatermark(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("skipping test on non-linux platform")
	}

	tests := []struct {
		name           string
		watermarkBytes uint32
	}{
		{
			name:           "wake up on every event",
			watermarkBytes: 0,
		},
		{
			name:           "wake up after 4096 bytes",
			watermarkBytes: 4096,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			nPages := uint32(2)
			storage, err := NewMmapRingStorage(0, nPages, tt.watermarkBytes)
			if err != nil {
				t.Fatalf("failed to create mmap storage: %v", err)
			}
			defer storage.Close()
		})
	}
}
