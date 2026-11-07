# protocolbuffers/go

C port of `protoc-gen-go` from [protobuf-go](https://github.com/protocolbuffers/protobuf-go)
(BSD-3-Clause): <https://buf.build/protocolbuffers/go>. No Go toolchain required.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=protocolbuffers/go, uni-version=v1.36.12, paths=source_relative]
```

Options are the same as upstream: [Go generated code guide](https://protobuf.dev/reference/go/go-generated/).

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v1.36.12 | [`cmd/protoc-gen-go`](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.12/cmd/protoc-gen-go), [`compiler/protogen`](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.12/compiler/protogen) | 1.26.5 |

Each `.c` file names the upstream file it ports. Output is formatted with a port of the Go
standard library of the version in the table: this is the Go version used to build
[the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/go).

## Known differences

- Option and descriptor errors go to `CodeGeneratorResponse.error`. Upstream prints them to
  stderr and exits with status 1.
- `annotate_code`: `.pb.go.meta` uses one space between fields. Upstream randomizes it per binary.
  The `.pb.go` files are identical.
- `proto:` in errors is always followed by a plain space. Upstream sometimes uses a no-break space.
- Upstream picks from Go maps at random in two places. The port picks in a fixed order:
  - which two files the "inconsistent names" error names;
  - which duplicate reserved name it reports.
- Required fields are not checked when the request is parsed.
- Nesting limits:
  - messages: 100 levels;
  - option messages: 256 KiB of stack.
- A NUL byte ends file names, `go_package` and `M` values. protoc and buf never produce one.
