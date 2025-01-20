gobin := `which go`

[working-directory: 'cmd/cpi-count']
cpi-bin:
    {{gobin}} build . && sudo -E ./cpi-count

[working-directory: 'cmd/cpi-count']
cpi-test:
    sudo -E {{gobin}} test -count=1 ./...
