package main

import (
	"runtime"
	"testing"

	"github.com/stretchr/testify/require"
)

func TestGoPerfVsPerfCmd(t *testing.T) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	perfCmd := NewPerfCmd()
	err := perfCmd.Start()

	goperf := NewGoPerf()
	err = goperf.StartWorkloadMeasurement()
	require.NoError(t, err)

	goperfOutput, err := goperf.End()
	require.NoError(t, err)

	perfOutput, err := perfCmd.End()
	require.NoError(t, err)

	require.InEpsilon(t, goperfOutput.Cycles, perfOutput.Cycles, 0.15)
	require.InEpsilon(t, goperfOutput.Instrs, perfOutput.Instrs, 0.15)
	require.InEpsilon(t, goperfOutput.CPI(), perfOutput.CPI(), 0.08)
}
