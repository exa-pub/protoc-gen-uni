# connectrpc/go

C port of `protoc-gen-connect-go` from [connect-go](https://github.com/connectrpc/connect-go)
(Apache-2.0): <https://buf.build/connectrpc/go>. No Go toolchain required. It generates the Connect
clients and handlers (`.connect.go`) for the types of
[protocolbuffers/go](../../protocolbuffers/go/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=connectrpc/go, uni-version=v2.0.2, paths=source_relative]
```

Options are the same as upstream: [connect-go getting started](https://connectrpc.com/docs/go/getting-started),
`package_suffix` and the `compiler/protogen` options.

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v2.0.2 | [`cmd/protoc-gen-connect-go`](https://github.com/connectrpc/connect-go/tree/v2.0.2/cmd/protoc-gen-connect-go) (built by [bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/connectrpc/go/v2.0.2) without patches), [`compiler/protogen` v1.36.11](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.11/compiler/protogen) | 1.27.2 |

Each `.c` file names the upstream file it ports: `main.go` is in `main.c`, its `main` function
(`protogen.Options.Run` with the flag set) in `generator.c`. The plugin framework is the
`compiler/protogen` port in [`pkg/languages/go`](../../../pkg/languages/go) (v1.36.12) with the
v1.36.11 runtime switch `go_protobuf_v1_36_11`, as for [grpc/go](../../grpc/go/README.md). Output
is formatted with the Go standard library port in
[`pkg/languages/go/std`](../../../pkg/languages/go/std) at its default release, Go 1.27.1, which
formats like Go 1.27.2.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-connect-go prints Go source
text into a `compiler/protogen` `GeneratedFile`, which formats it with `go/format.Source` of the
Go standard library: the port prints the same text and formats it with the port of that library.
No third-party formatter is involved.

All options of upstream are implemented:
- `package_suffix` (default `connect`; empty: the package of the `.pb.go` files), which must be a
  Go identifier (`package_suffix "<value>" is not a valid Go identifier`, checked for each file
  with services);
- the `compiler/protogen` options `paths`, `module`, `annotate_code` and `M<file>`;
- unknown options fail with `no such flag -<name>`, as upstream.

Imports of `connectrpc.com/connect` are rewritten to `connectrpc.com/connect/v2`
(`ImportRewriteFunc`). protobuf-go picks some output details with `internal/detrand`, a bit
derived from a hash of the plugin binary; the port uses the bit of the buf.build binary (false:
one space between the fields of the `.connect.go.meta` files of `annotate_code`).

## Known differences

- Errors of `Options.New` and of the request (upstream: `protoc-gen-connect-go: <error>` on
  stderr, exit status 1) go to `CodeGeneratorResponse.error` with the same text; a request that
  cannot be parsed fails with `protoc-gen-uni: parse request: <error>` on stderr.
- The random choices and nesting limits of the `compiler/protogen` port, listed in
  [protocolbuffers/go](../../protocolbuffers/go/README.md#known-differences).
- A file named `cmd/protoc-gen-go/testdata/...` (protodesc lets such files use any edition) with
  an edition outside PROTO2..2024 other than UNSTABLE: upstream exits in protodesc with
  `internal error: unsupported edition ...` on stderr; the port reports the edition check of
  `compiler/protogen` v1.36.12 (`edition ... is greater than the maximum supported edition ...`).
- Not supported (protoc and buf never produce such a request). Generation fails with a
  `CodeGeneratorResponse.error` `unsupported by protoc-gen-uni: ...` that names it:
  - a NUL byte in a file name (`proto_file`, `dependency`), in `go_package` or in an `M<file>`
    parameter;
  - messages nested so deeply that decoding them needs more than 256 KiB of stack, around a
    thousand levels (upstream allows 10000).
