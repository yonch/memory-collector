package perf

import (
	"testing"
	"unsafe"
)

func TestInitContiguous(t *testing.T) {
	pageSize := uint64(4096)
	nPages := uint32(2)
	data := make([]byte, pageSize*(1+uint64(nPages))) // 1 meta page + 2 data pages

	tests := []struct {
		name      string
		data      []byte
		nPages    uint32
		pageSize  uint64
		wantError bool
	}{
		{
			name:      "valid initialization",
			data:      data,
			nPages:    nPages,
			pageSize:  pageSize,
			wantError: false,
		},
		{
			name:      "nil data",
			data:      nil,
			nPages:    nPages,
			pageSize:  pageSize,
			wantError: true,
		},
		{
			name:      "invalid buffer size",
			data:      make([]byte, 7), // Less than minimum size
			nPages:    1,
			pageSize:  7,
			wantError: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			ring, err := InitContiguous(tt.data, tt.nPages, tt.pageSize)
			if tt.wantError {
				if err == nil {
					t.Error("expected error, got nil")
				}
			} else {
				if err != nil {
					t.Errorf("unexpected error: %v", err)
				}
				if ring == nil {
					t.Error("expected non-nil ring")
				}
			}
		})
	}
}

func TestWriteAndRead(t *testing.T) {
	pageSize := uint64(4096)
	nPages := uint32(2)
	data := make([]byte, pageSize*(1+uint64(nPages)))

	ring, err := InitContiguous(data, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to initialize ring: %v", err)
	}

	testData := []byte("test data")
	eventType := uint32(1)

	// Start write batch
	ring.StartWriteBatch()

	// Write data
	offset, err := ring.Write(testData, eventType)
	if err != nil {
		t.Fatalf("failed to write data: %v", err)
	}

	// Verify offset is within buffer bounds
	if offset < 0 || offset >= int(pageSize*uint64(nPages)) {
		t.Errorf("offset %d outside buffer bounds [0, %d)", offset, pageSize*uint64(nPages))
	}

	// Finish write batch
	ring.FinishWriteBatch()

	// Start read batch
	ring.StartReadBatch()

	// Check size
	size, err := ring.PeekSize()
	if err != nil {
		t.Fatalf("failed to peek size: %v", err)
	}
	if size != (len(testData)+7)/8*8 {
		t.Errorf("expected size %d, got %d", (len(testData)+7)/8*8, size)
	}

	// Check type
	if typ := ring.PeekType(); typ != eventType {
		t.Errorf("expected type %d, got %d", eventType, typ)
	}

	// Read data
	readBuf := make([]byte, size)
	err = ring.PeekCopy(readBuf, 0)
	if err != nil {
		t.Fatalf("failed to peek copy: %v", err)
	}

	// Compare data
	if string(readBuf[:len(testData)]) != string(testData) {
		t.Errorf("expected data %q, got %q", testData, readBuf)
	}

	// Pop the event
	if err := ring.Pop(); err != nil {
		t.Fatalf("failed to pop event: %v", err)
	}

	// Check remaining bytes (should be 0)
	if remaining := ring.BytesRemaining(); remaining != 0 {
		t.Errorf("expected 0 bytes remaining, got %d", remaining)
	}

	// Finish read batch
	ring.FinishReadBatch()
}

func TestBytesRemaining(t *testing.T) {
	pageSize := uint64(4096)
	nPages := uint32(2)
	data := make([]byte, pageSize*(1+uint64(nPages)))

	ring, err := InitContiguous(data, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to initialize ring: %v", err)
	}

	remaining := ring.BytesRemaining()

	if remaining != 0 {
		t.Errorf("expected 0 bytes remaining in empty buffer, got %d", remaining)
	}
}

func TestWraparound(t *testing.T) {
	pageSize := uint64(4096)
	nPages := uint32(2)
	data := make([]byte, pageSize*(1+uint64(nPages)))

	ring, err := InitContiguous(data, nPages, pageSize)
	if err != nil {
		t.Fatalf("failed to initialize ring: %v", err)
	}

	// Write data that will wrap around the buffer
	dataSize := int(pageSize) - int(unsafe.Sizeof(PerfEventHeader{})) - 10
	testData := make([]byte, dataSize)
	for i := range testData {
		testData[i] = byte(i)
	}

	ring.StartWriteBatch()

	// Write first chunk
	_, err = ring.Write(testData, 1)
	if err != nil {
		t.Fatalf("failed to write first chunk: %v", err)
	}

	// Write second chunk
	_, err = ring.Write(testData, 2)
	if err != nil {
		t.Fatalf("failed to write second chunk: %v", err)
	}

	ring.FinishWriteBatch()

	// Read and verify both chunks
	ring.StartReadBatch()

	// Read first chunk
	readBuf := make([]byte, dataSize)
	err = ring.PeekCopy(readBuf, 0)
	if err != nil {
		t.Fatalf("failed to read first chunk: %v", err)
	}
	for i := range readBuf {
		if readBuf[i] != testData[i] {
			t.Errorf("first chunk mismatch at index %d: expected %d, got %d", i, testData[i], readBuf[i])
		}
	}
	err = ring.Pop()
	if err != nil {
		t.Fatalf("failed to pop first chunk: %v", err)
	}

	ring.FinishReadBatch()

	// there should now be space for one more event, that would wrap around the buffer. Write it.
	ring.StartWriteBatch()
	_, err = ring.Write(testData, 3)
	if err != nil {
		t.Fatalf("failed to write third chunk: %v", err)
	}
	ring.FinishWriteBatch()

	// Now read the second and third chunks and verify they are correct
	ring.StartReadBatch()

	// Read second chunk
	err = ring.PeekCopy(readBuf, 0)
	if err != nil {
		t.Fatalf("failed to read second chunk: %v", err)
	}
	for i := range readBuf {
		if readBuf[i] != testData[i] {
			t.Errorf("second chunk mismatch at index %d: expected %d, got %d", i, testData[i], readBuf[i])
		}
	}
	err = ring.Pop()
	if err != nil {
		t.Fatalf("failed to pop second chunk: %v", err)
	}

	// Read third chunk
	err = ring.PeekCopy(readBuf, 0)
	if err != nil {
		t.Fatalf("failed to read third chunk: %v", err)
	}
	for i := range readBuf {
		if readBuf[i] != testData[i] {
			t.Errorf("third chunk mismatch at index %d: expected %d, got %d", i, testData[i], readBuf[i])
		}
	}
	err = ring.Pop()
	if err != nil {
		t.Fatalf("failed to pop third chunk: %v", err)
	}

	ring.FinishReadBatch()

	// ring should be empty now
	if remaining := ring.BytesRemaining(); remaining != 0 {
		t.Errorf("expected 0 bytes remaining, got %d", remaining)
	}
}
