# grpc/go

C port of `protoc-gen-go-grpc` from [grpc-go](https://github.com/grpc/grpc-go) (Apache-2.0):
<https://buf.build/grpc/go>. No Go toolchain required. It generates the gRPC client and server
stubs (`_grpc.pb.go`) for the types of [protocolbuffers/go](../../protocolbuffers/go/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=grpc/go, uni-version=v1.6.2, paths=source_relative]
```

Options are the same as upstream: [grpc-go quick start](https://grpc.io/docs/languages/go/quickstart/),
plus `separate_package` of the buf.build build and the `compiler/protogen` options.

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v1.6.2 | [`cmd/protoc-gen-go-grpc`](https://github.com/grpc/grpc-go/tree/cmd/protoc-gen-go-grpc/v1.6.2/cmd/protoc-gen-go-grpc) with [`separate-package.patch`](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/go/v1.6.2), [`compiler/protogen` v1.36.11](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.11/compiler/protogen) | 1.26.3 |

Each `.c` file names the upstream file it ports: `main.go` is in `generator.c`, `grpc.go` in
`grpc.c`. The plugin framework is the `compiler/protogen` port in
[`pkg/languages/go`](../../../pkg/languages/go) (v1.36.12) with the v1.36.11 runtime switch
`go_protobuf_v1_36_11` (protodesc parses integer default values in base 10). `compiler/protogen`
itself is the same in both versions apart from `ResolvedFeatures`, which the port does not
expose and protoc-gen-go-grpc does not use. Output is formatted with the Go standard library port
in [`pkg/languages/go/std`](../../../pkg/languages/go/std) set to Go 1.26.5 (`go_std_go1_26_5`):
Go 1.26.3 formats the same way (`git diff go1.26.3 go1.26.5 -- src/go src/strconv src/unicode
src/text src/strings src/flag` touches only `go/build`).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-go-grpc prints Go source
text into a `compiler/protogen` `GeneratedFile`, which formats it with `go/format.Source` of the
Go standard library: the port prints the same text and formats it with the port of that library.
No third-party formatter is involved.

All options of upstream are implemented:
- `require_unimplemented_servers` (default `true`) and `separate_package` (default `false`; buf's
  patch: the package `<name>grpc` in the subdirectory `<name>grpc/` of the generated file,
  importing the messages from the original package), parsed with `strconv.ParseBool` as Go's
  `flag` package does (`parse error` otherwise);
- the `compiler/protogen` options `paths`, `module`, `annotate_code` and `M<file>`;
- unknown options fail with `no such flag -<name>`, as upstream.

protobuf-go picks some output details with `internal/detrand`, a bit derived from a hash of the
plugin binary. The port uses the bit of the buf.build binary (it shows in the `.pb.go.meta` files
of `annotate_code`).

## Known differences

- Errors of `Options.New` and of the request (upstream: `protoc-gen-go-grpc: <error>` on stderr,
  exit status 1) go to `CodeGeneratorResponse.error` with the same text; a request that cannot
  be parsed fails with `protoc-gen-uni: parse request: <error>` on stderr.
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
