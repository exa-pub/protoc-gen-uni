# community/mfridman-go-json

C port of `protoc-gen-go-json` from [mfridman/protoc-gen-go-json](https://github.com/mfridman/protoc-gen-go-json)
(MIT): <https://buf.build/community/mfridman-go-json>. No Go toolchain required. It generates
`MarshalJSON`/`UnmarshalJSON` methods (and optionally `Scan`/`Value`) that call `protojson`, for the
types of [protocolbuffers/go](../../protocolbuffers/go/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=community/mfridman-go-json, uni-version=v1.6.0, paths=source_relative]
```

Options are the same as upstream: [options](https://github.com/mfridman/protoc-gen-go-json/blob/v1.6.0/README.md),
plus the `compiler/protogen` options (`paths`, `module`, `M…`).

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v1.6.0 | [`main.go`, `internal/`](https://github.com/mfridman/protoc-gen-go-json/tree/v1.6.0), [`bufbuild/protoplugin@750e09ce93e1`](https://github.com/bufbuild/protoplugin/tree/750e09ce93e1ea9bb14ed26157a06b733b8aca62), [`compiler/protogen` v1.36.11](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.11/compiler/protogen) | 1.26.1 |

Each `.c` file names the upstream file it ports. `compiler/protogen` v1.36.11 is identical to the
v1.36.12 port in `pkg/languages/go`, and the Go 1.26.1 formatter to the 1.26.5 port.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-go-json prints Go source text
from two `text/template` templates into a `compiler/protogen` `GeneratedFile`, which formats it with
`go/format.Source` of the Go standard library: the port prints the same text (the templates become
C code with the same whitespace) and formats it with the port of that library in
[`pkg/languages/go`](../../../pkg/languages/go). No third-party formatter is involved.

All options of upstream are implemented: `enums_as_ints`, `emit_defaults`,
`emit_defaults_without_null`, `orig_name`, `allow_unknown`, `emit_scanner_valuer` (unknown names
are ignored, as upstream) and the `compiler/protogen` options `paths`, `module`, `annotate_code`
and `M<file>`.

## Known differences

- Request, option and response validation errors go to stderr with exit status 1, as upstream,
  but with the prefix `protoc-gen-uni: parse request: ` before upstream's
  `protoc-gen-go-json: <error>`. A panic of the port (upstream: a goroutine trace with exit
  status 2) is reported the same way.
- Upstream picks from a Go map at random when several `source_file_descriptors` are missing from
  `file_to_generate`. The port reports the first one.
- Not supported (protoc and buf never produce such a request). Generation fails with a
  `CodeGeneratorResponse.error` `unsupported by protoc-gen-uni: ...` that names it, after the
  request validation of `protoplugin` (whose errors are as upstream):
  - a NUL byte in a file name (`proto_file`, `dependency`), in `go_package` or in an `M<file>`
    parameter;
  - messages nested so deeply that decoding them (with the extensions the request declares, for
    custom options) needs more than 256 KiB of stack, around a thousand levels; upstream allows
    10000 (protoc and buf stop at 32 levels of message declarations and 100 levels of option
    values). Up to that limit, requests nested deeper than the 100 levels of `pkg/pb` are
    generated like upstream: the generator parses the request itself (`parses_raw`).
