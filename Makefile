VERSION ?= dev

.PHONY: all build test lint tidy clean

all: lint test build

build:
	go build -ldflags "-X main.version=$(VERSION)" -o protoc-gen-uni .

test:
	go test -race ./...

lint:
	golangci-lint run

tidy:
	go mod tidy

clean:
	rm -rf protoc-gen-uni dist
