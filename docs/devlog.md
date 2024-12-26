Documentation of development steps, environment, and dependencies  

- Contributors: atimeofday
- Goals: Create skeleton collector with Prometheus endpoint
- Issues: https://github.com/perfpod/memory-collector/issues/19

Initial environment and tools:
```
# Shell: Bash
distrobox create --image fedora:40 --name memory-collector 
distrobox enter memory-collector
sudo dnf install git go

# cd to preferred project directory
# Clone (fork of) project
git clone https://github.com/perfpod/memory-collector
cd memory-collector
```

Issue 19 objective 1: Create a `main.go` file in `cmd/collector`
```
mkdir -p cmd/collector
cd cmd/collector
touch main.go
```

- Prometheus client_golang reference guide: https://prometheus.io/docs/guides/go-application/
- Go package installation reference: https://go.dev/doc/go-get-install-deprecation
- Go Module reference: https://go.dev/ref/mod#go-mod-init
- `go get` and `go install` require a Go Module and/or @version tag as of Go 1.17 in August 2021
- Prometheus go_client installation instructions appear to be outdated and missing a piece
- Submitted issue to Prometheus documentation repository: https://github.com/prometheus/docs/issues/2556#issue-2736636166
- Proceeded with Prometheus client_golang guide 
```
cd cmd/collector
go mod init memory-collector
go get github.com/prometheus/client_golang/prometheus
go get github.com/prometheus/client_golang/prometheus/promauto
go get github.com/prometheus/client_golang/prometheus/promhttp
```

Issue 19 objective 2: Expose an endpoint on a known fixed port 
```
# Wrote and tested example Go exposition application from Prometheus guide
go run main.go &
curl http://localhost:2112/metrics
```

Issue 19 objective 3: Expose the `up` metric with value 1
```
// Created, registered, and set an 'up' metric in func main()

upMetric := prometheus.NewGauge(prometheus.GaugeOpts{
	Namespace: 	"perfpod",
	Subsystem: 	"memory_collector",
	Name: 		"up_metric",
	Help: 		"Test metric to confirm skeleton application functionality.",
})
prometheus.MustRegister(upMetric)

upMetric.Set(1)
```

Issue 19 objective 4: Manually verify: query the endpoint using `curl` or `wget`
```
curl -s http://localhost:2112/metrics | grep up_metric
```
Output:
```
# HELP perfpod_memory_collector_up_metric Test metric to confirm skeleton application functionality.
# TYPE perfpod_memory_collector_up_metric gauge
perfpod_memory_collector_up_metric 1
```

Issue 19 objective 5: Move the code into a function (not `main()`)
```
// Moved Up metric into "func recordMetrics()" and added function call in main()

func main() {
	recordMetrics()
	
	http.Handle("/metrics", promhttp.Handler())
	http.ListenAndServe(":2112", nil)
}

// Repeated manual verification endpoint query
```

Issue 19 objective 6: Add an integration test that verifies the metrics are up, using client_golang's testutil
- TO DO
- May require assistance

---

- Issue 19 split into 5/5 done and new Issue 20
- Issue 19 5/5 PR opened and merged

---

- Contributors: atimeofday
- Goals: Add integration test to Prometheus endpoint
- Issues: https://github.com/perfpod/memory-collector/issues/20

Research & references:
```
https://go.dev/doc/tutorial/add-a-test
https://albertmoreno.dev/posts/testing-prometheus-metrics-in-integration-tests-in-golang/
https://github.com/prometheus/client_golang/blob/main/prometheus/testutil/testutil.go
https://github.com/prometheus/client_golang/blob/main/prometheus/testutil/testutil_test.go
```

```
go get github.com/prometheus/client_golang/prometheus/testutil 
go get github.com/stretchr/testify/require
```

Go test format:
```
[filename]_test.go

import(
	[...]
)

func [TestFunction](t *testing.T) {
	// Set test values
	// Perform test
}

// Perform more tests
```

1. Created skeleton test based on examples 

```
func TestMetricsUp(t *testing.T) {
	require.Eventuallyf(t, func() bool {

  		// Test values
  		// ??? expected format

		if err := testutil.ScrapeAndCompare(serverURL+"/metrics", strings.NewReader(expected), metricName); err == nil {
		    return true
		} else {
			t.Log(err.Error())
			return false
		}
	}, time.Second, 100*time.Millisecond, "Could not find metric %s with value %d", metricName, expectedMetricValue)
}
```

2. Checked the implementation of the testutil ScrapeAndCompare function, and notably, the implementation of its own integration test.
3. Located and implemented the exact input template required by the function, then implemented generalized code for the template.
4. Researched goroutines to allow automatically initializing the (currently local) remote server to be tested.

```
go main()
time.Sleep(1 * time.Second)
```

5. Refined logical flow from example code for improved readability.

---

- Issue 20 done

---

- Contributors: 
- Goals: 
- Issues: 

