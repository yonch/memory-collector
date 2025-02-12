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
RUN go build -o prometheus-metrics

# Stage 2: Create a lightweight production image
FROM alpine:latest

# Install runtime dependencies and create a prometheus-metrics user
RUN apk --no-cache add ca-certificates && \
    addgroup -S prometheus-metrics && adduser -S prometheus-metrics -G prometheus-metrics

WORKDIR /
COPY --from=builder /app/prometheus-metrics .
RUN chown prometheus-metrics:prometheus-metrics /prometheus-metrics

USER prometheus-metrics
CMD ["./prometheus-metrics"] 