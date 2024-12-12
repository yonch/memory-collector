package main

import (
	"net/http"

	"github.com/prometheus/client_golang/prometheus"
	// "github.com/prometheus/client_golang/prometheus/promauto"
    "github.com/prometheus/client_golang/prometheus/promhttp"
)

func recordMetrics() {
	upMetric := prometheus.NewGauge(prometheus.GaugeOpts{
			Namespace: 	"perfpod",
			Subsystem: 	"memory_collector",
			Name: 		"up_metric",
			Help: 		"Test metric to confirm skeleton application functionality.",
		})
		prometheus.MustRegister(upMetric)
	
		upMetric.Set(1)
}

func main() {
	recordMetrics()
	
	http.Handle("/metrics", promhttp.Handler())
	http.ListenAndServe(":2112", nil)
}

