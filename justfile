gobin := `which go`

[working-directory: 'cmd/cpi-count']
cpi-bin:
    {{gobin}} build . && ./cpi-count

[working-directory: 'cmd/cpi-count']
cpi-test:
    {{gobin}} test -count=1 ./...
