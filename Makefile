VERSION ?= dev

.PHONY: all build test lint tidy clean testdata

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

# make testdata NAME=bufbuild/es VERSION_GEN=v2.17.0  (needs BUF_TOKEN, xz)
testdata:
	scripts/gen-testdata.sh $(NAME) $(VERSION_GEN)
