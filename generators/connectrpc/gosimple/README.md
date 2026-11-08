# connectrpc/gosimple

C port of `protoc-gen-connect-go` v1 from [connect-go](https://github.com/connectrpc/connect-go)
(Apache-2.0): <https://buf.build/connectrpc/gosimple>. No Go toolchain required. It generates the
Connect clients and handlers (`.connect.go`) of `connectrpc.com/connect` v1 for the types of
[protocolbuffers/go](../../protocolbuffers/go/README.md); with the `simple` option, which buf.build
lists in its registry options, clients and handlers take and return the request and response
messages instead of `connect.Request` and `connect.Response`.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=connectrpc/gosimple, uni-version=v1.21.1, paths=source_relative, simple]
```

Options are the same as upstream: [connect-go getting started](https://connectrpc.com/docs/go/getting-started),
`package_suffix`, `simple` and the `compiler/protogen` options.

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v1.21.1 | [`cmd/protoc-gen-connect-go`](https://github.com/connectrpc/connect-go/tree/v1.21.1/cmd/protoc-gen-connect-go) (built by [bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/connectrpc/gosimple/v1.21.1) without patches), [`compiler/protogen` v1.36.11](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.11/compiler/protogen) | 1.27.2 |

Each `.c` file names the upstream file it ports: `main.go` is in `main.c`, its `main` function
(`protogen.Options.Run` with the flag set) in `generator.c`. It is the same program as
[connectrpc/go](../go/README.md) at another major version (about 870 lines differ), so the port
keeps its own copy of `main.go`; `token.IsIdentifier` comes from the Go standard library port. The
plugin framework is the `compiler/protogen` port in [`pkg/languages/go`](../../../pkg/languages/go)
(v1.36.12) with the v1.36.11 runtime switch `go_protobuf_v1_36_11`. Output is formatted with the
Go standard library port in [`pkg/languages/go/std`](../../../pkg/languages/go/std) at its default
release, Go 1.27.1, which formats like Go 1.27.2.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-connect-go prints Go source
text into a `compiler/protogen` `GeneratedFile`, which formats it with `go/format.Source` of the
Go standard library: the port prints the same text and formats it with the port of that library.
No third-party formatter is involved.

All options of upstream are implemented:
- `simple`: empty or `true` (simple signatures), `false` (the default); any other value fails with
  `unknown value for option "simple" (must be one of "", "true", "false"): "<value>"`;
- `package_suffix` (default `connect`; empty: the package of the `.pb.go` files), which must be a
  Go identifier (`package_suffix "<value>" is not a valid Go identifier`, checked for each file
  with services);
- the `compiler/protogen` options `paths`, `module`, `annotate_code` and `M<file>`;
- unknown options fail with `no such flag -<name>`, as upstream.

protobuf-go picks some output details with `internal/detrand`, a bit derived from a hash of the
plugin binary; the port uses the bit of the buf.build binary (false: one space between the fields
of the `.connect.go.meta` files of `annotate_code`).

## Known differences

- An invalid `simple` value: upstream's error response also sets `supported_features` (editions)
  but not `minimum_edition`, so a local buf rejects the response (`CodeGeneratorResponse:
  supported_features: FEATURE_SUPPORTS_EDITIONS specified but no minimum_edition set`) and protoc
  adds an editions error for editions files; buf.build shows the error alone. The port's error
  response also sets the editions (PROTO2..2024), so every client shows the error alone.
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
