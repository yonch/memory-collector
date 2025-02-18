package main

import (
	"net/http"
    "github.com/prometheus/client_golang/prometheus/promhttp"
)


func main() {
	go recordMetrics()
	
	http.Handle("/metrics", promhttp.Handler())
	http.ListenAndServe(":2112", nil)
}

