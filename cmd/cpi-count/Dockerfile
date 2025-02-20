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
COPY cmd/cpi-count/ cmd/cpi-count/

# Build the application
RUN go build -o cpi-count ./cmd/cpi-count

# Stage 2: Create a lightweight production image
FROM alpine:latest

# Install runtime dependencies and create a cpi-count user
RUN apk --no-cache add ca-certificates && \
    addgroup -S cpi-count && adduser -S cpi-count -G cpi-count

WORKDIR /
COPY --from=builder /app/cpi-count .
RUN chown cpi-count:cpi-count /cpi-count

USER cpi-count
CMD ["./cpi-count"]
