# community/planetscale-vtprotobuf

C port of `protoc-gen-go-vtproto` from [planetscale/vtprotobuf](https://github.com/planetscale/vtprotobuf)
(BSD-3-Clause): <https://buf.build/community/planetscale-vtprotobuf>. No Go toolchain required. It
generates fast `MarshalVT`/`UnmarshalVT`/`SizeVT`/`CloneVT`/`EqualVT` methods, memory pools and
gRPC services for the types of [protocolbuffers/go](../../protocolbuffers/go/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=community/planetscale-vtprotobuf, uni-version=v0.6.0, paths=source_relative]
```

Options are the same as upstream: [options](https://github.com/planetscale/vtprotobuf/blob/v0.6.0/README.md)
(`features`, `pool`, `pool-exclude`, `wrap`, `allow-empty`, `buildTag`), plus the
`compiler/protogen` options (`paths`, `module`, `annotate_code`, `M…`). The `vtproto.mempool`
message option is read from `vtproto/ext.proto` (field 64101).

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v0.6.0 | [`cmd/protoc-gen-go-vtproto`, `generator/`, `features/`](https://github.com/planetscale/vtprotobuf/tree/v0.6.0), [`compiler/protogen` v1.31.0](https://github.com/protocolbuffers/protobuf-go/tree/v1.31.0/compiler/protogen) | 1.21.6 |

Each `.c` file names the upstream file it ports. vtprotobuf's `go.mod` pins protobuf-go v1.31.0,
so it uses the `compiler/protogen` port of
[`pkg/languages/go/protogen`](../../../pkg/languages/go/protogen) (v1.36.12) with
`PgOptions.Version = &pg_v1_31_0`: the parts where v1.31.0 differs are in `protogen_v1.31.0.c`
(no editions, its validation and texts, no API levels, every `ProtoFile`, no
`supported_features` with an error). Output is formatted with the
Go 1.26.5 port of the standard library; for this plugin's output it prints the same as Go 1.21.6
(`go/printer` differs only for generics and channel conversions, which the plugin does not emit).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-go-vtproto prints Go source
text with `GeneratedFile.P` of `compiler/protogen`, which formats it with `go/format.Source` of the
Go standard library: the port prints the same text and formats it with the port of that library in
[`pkg/languages/go`](../../../pkg/languages/go). No third-party formatter is involved.

All options of upstream are implemented, with the error texts of Go's `flag` package (`no such
flag -x`, `parse error`, `syntax error in pattern`) and of the plugin (`unknown feature: "x"`).
Like upstream (protobuf-go v1.31.0 knows only proto2 and proto3), editions files are rejected:
`invalid FileDescriptorProto "x.proto": proto: invalid syntax: "editions"` (as all errors of
protobuf-go in the buf.build binary, with a no-break space after `proto:`).

The request is decoded with the `descriptor.proto` of protobuf-go v1.36.12, the descriptors are
built by the protobuf-go v1.36.12 port with the v1.31.0 syntax check and the v1.31.0 validation
rules and texts on top. Fields that only one of the versions knows have no effect on the output.

## Unsupported

Requests that upstream accepts and the port does not support fail with a
`CodeGeneratorResponse.error` `unsupported by protoc-gen-uni: ...` that names them (protoc and buf
never produce such a request):

- a NUL byte in a file name (`proto_file`, `dependency`), in `go_package` or in an `M<file>`
  parameter;
- messages nested so deeply that decoding them (with the extensions the request declares, for
  custom options) needs more than 256 KiB of stack, around a thousand levels; upstream allows 10000
  (protoc and buf stop at 32 levels of message declarations and 100 levels of option values). Up
  to that limit, requests nested deeper than the 100 levels of `pkg/pb` are generated like
  upstream: the generator parses the request itself (`parses_raw`);
- `pool=`/`pool-exclude=` patterns whose matching takes more than 2^30 steps (bytes copied plus
  matching steps, over all matches of a request): upstream copies the pattern for every `{` it
  meets, which is quadratic in the pattern length for every message, e.g. thousands of nested
  `{...}` (`pool or pool-exclude patterns that take over 1073 million steps to match`). The
  matcher also limits alternatives nested in non-last alternatives to 100 levels, which the
  parameter cannot express (its values have no commas, so every `{...}` has one alternative).

## Known differences

- Errors of `Options.New` (options, descriptors) are returned in `CodeGeneratorResponse.error`
  with upstream's text `protoc-gen-go-vtproto: <error>`; upstream prints that text to stderr and
  exits with status 1. A panic (upstream: a goroutine trace with exit status 2) is reported the
  same way, with the panic value as `<error>`. A request that cannot be decoded goes to stderr with
  exit status 1, as upstream.
- Upstream picks from a Go map at random for the two files the "inconsistent names" error names.
  The port picks in a fixed order.
- For descriptors that protoc and buf reject anyway, the reported error can differ (e.g. which of
  several problems comes first, unresolvable weak imports).
