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

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-go prints Go source text and
formats it with `go/format.Source` (`go/parser` and `go/printer`), which is part of the Go
standard library: the port prints the same text and formats it with the port of that library in
[`pkg/languages/go`](../../../pkg/languages/go). No third-party formatter is involved.

All options of upstream are implemented: `paths`, `module`, `annotate_code`, `M<file>`,
`default_api_level`, `apilevelM<file>`, `experimental_strip_nonfunctional_codegen` and the
deprecated `plugins` (an error, as upstream). Unknown options fail with `no such flag -<name>`,
as upstream.

protobuf-go picks some output details with `internal/detrand`, a bit derived from a hash of the
plugin binary. The port uses the bit of the buf.build binary: `proto:` in errors is followed by a
no-break space (U+00A0), and the `.pb.go.meta` files of `annotate_code` put two spaces between
fields.

## Known differences

- Errors go to `CodeGeneratorResponse.error`. Upstream prints `protoc-gen-go: <error>` to stderr
  and exits with status 1; a request that cannot be parsed fails with
  `protoc-gen-uni: parse request: <error>` on stderr instead.
- Upstream picks from Go maps at random in a few places, so its error text varies from run to
  run. The port picks in a fixed order:
  - which two files the "inconsistent names" error names;
  - which duplicate reserved name it reports;
  - which required field it reports when several are missing (in extensions or in the messages
    of custom options; the port checks in field number order).
- Nesting limits:
  - messages: 100 levels;
  - option messages: 256 KiB of stack.
- A NUL byte ends file names, `go_package` and `M` values. protoc and buf never produce one.
