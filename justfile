gobin := `which go`

[working-directory: 'cmd/cpi-count']
cpi-test:
    sudo {{gobin}} test -count=1 ./...
