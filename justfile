gobin := `which go`

[working-directory: 'cmd/cpi-count']
cpi-bin:
    {{gobin}} build . && sudo ./cpi-count

[working-directory: 'cmd/cpi-count']
cpi-test:
    sudo {{gobin}} test -count=1 ./...
