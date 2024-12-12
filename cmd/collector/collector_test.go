package collector

import (
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/prometheus/client_golang/prometheus/testutil"
	"github.com/stretchr/testify/require"
)

// TODO
func TestMetricsUp() {
	// testutil.ScrapeAndCompare()
	// "http://localhost:2112/metrics"
	// # HELP perfpod_memory_collector_up_metric Test metric to confirm skeleton application functionality.
	// # TYPE perfpod_memory_collector_up_metric gauge
	// perfpod_memory_collector_up_metric 1
}

