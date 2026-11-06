# protocolbuffers/go

Port of `protoc-gen-go`, the Go code generator of [protobuf-go](https://github.com/protocolbuffers/protobuf-go).

- buf.build: <https://buf.build/protocolbuffers/go>
- Upstream: <https://github.com/protocolbuffers/protobuf-go> (module `google.golang.org/protobuf`)
- License: BSD-3-Clause; ported files keep the upstream copyright header.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=protocolbuffers/go, uni-version=v1.36.12, paths=source_relative]
```

## Versions

| version  | upstream sources |
|----------|------------------|
| v1.36.12 | [`cmd/protoc-gen-go/main.go`](https://github.com/protocolbuffers/protobuf-go/blob/v1.36.12/cmd/protoc-gen-go/main.go), [`cmd/protoc-gen-go/internal_gengo/`](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.12/cmd/protoc-gen-go/internal_gengo) |

`internal_gengo` cannot be imported from another module, so it is copied to `<version>/gengo`.
The protobuf-go `internal/*` packages it uses are ported to `pkg/languages/go/`:

| upstream | port |
|----------|------|
| `internal/version`, `internal/editionssupport` | constants in `<version>/gengo/main.go` |
| `internal/genid` | `pkg/languages/go/genid` (only the needed constants) |
| `internal/encoding/tag`, `internal/encoding/defval` | `pkg/languages/go/tag` (`Marshal` only) |
| `internal/strs` | `pkg/languages/go/strs` |
| `internal/filedesc` (`EditionFeatures`, `UsePresenceForField`), `internal/editiondefaults` | `pkg/languages/go/features` (pb.go features resolved with the public API) |

`compiler/protogen` is forked to `pkg/languages/go/protogen`: it formats generated code with
`go/printer`, whose output depends on the Go toolchain. The remote plugin is built with Go 1.26.5
and Go 1.27 formats some comments differently, so the fork uses the Go 1.26.5 `go/printer` vendored
in `pkg/languages/go/stdlib/go1.26.5` (see its README) and a fixed list of predeclared identifiers
instead of `go/types.Universe`.

## Known differences

- Errors from parameter parsing (`protogen.Options.New`) are returned in
  `CodeGeneratorResponse.error` as `protoc-gen-go: <error>`; upstream prints the same text to stderr
  and exits with status 1.
- `annotate_code`: the `.pb.go.meta` files are `prototext` output, which protobuf-go deliberately
  randomizes (extra spaces chosen from a hash of the running binary), so their whitespace differs
  from upstream's (and between any two builds of upstream). The `.pb.go` files are identical.
- `go/parser`, `go/ast`, `go/token`, `strconv.Quote` and `unicode` tables still come from the
  building toolchain; this only matters for non-ASCII file or package names.

## Options

Same as upstream: `paths`, `module`, `M<file>=<go package>`, `annotate_code`, `default_api_level`,
`apilevelM<file>=<level>`, `experimental_strip_nonfunctional_codegen`, `plugins` (rejected, as upstream).

Golden option groups and tests: `testdata/checksums/protocolbuffers/go/<version>.buf.gen.yaml`.
