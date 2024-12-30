# Stage 1: Build the Go binary
FROM golang:1.22.9 AS builder

ENV GO111MODULE=on \
    CGO_ENABLED=0 \
    GOOS=linux \
    GOARCH=amd64

# Set the working directory
WORKDIR /app

# Copy the module files
COPY go.mod go.sum ./
RUN go mod download

# Copy the source code
COPY . .

# Build the application
RUN go build -o collector

# Stage 2: Create a lightweight production image
FROM alpine:latest

# Install runtime dependencies and create a collector user
RUN apk --no-cache add ca-certificates && \
    addgroup -S collector && adduser -S collector -G collector

WORKDIR /
COPY --from=builder /app/collector .
RUN chown collector:collector /collector

USER collector
CMD ["./collector"]
