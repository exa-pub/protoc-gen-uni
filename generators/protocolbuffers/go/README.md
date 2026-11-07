# protocolbuffers/go

Port of `protoc-gen-go`, the Go code generator of [protobuf-go](https://github.com/protocolbuffers/protobuf-go),
rewritten in C: no Go toolchain required.

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

## Where the code lives

`<version>/` is a unity build (`generator.c` includes the other files and exports only
`gen_protocolbuffers_go_v1_36_12`); every `.c` file names the upstream file it ports
(`main.go` → `main.c`, `init.go` + `init_opaque.go` → `init.c`, `reflect.go`, `opaque.go`,
`well_known_types.go`). The Open-API-only message code of `main.go` is not ported because
`opaqueGenMessageHook` always takes over. The protobuf-go packages it needs are ported to
`pkg/languages/go/`:

| upstream (v1.36.12) | port |
|---------------------|------|
| `compiler/protogen` | `pkg/languages/go/protogen` (library: `protogen.h`) |
| `reflect/protodesc`, `internal/filedesc`, `reflect/protoregistry` (Files) | `pkg/languages/go/protodesc` (`desc.c`, `validate.c`) |
| `proto` Marshal (deterministic)/Unmarshal, `types/dynamicpb`, `internal/order`, `descriptorpb`/`pluginpb`/`gofeaturespb` | `pkg/languages/go/protodesc` (`wire.c`, `schema.c`) |
| `internal/editiondefaults`, `protodesc/editions.go`, `filedesc/presence.go` | `pkg/languages/go/features` (the defaults as a table) |
| `internal/encoding/tag`, `internal/encoding/defval` (GoTag) | `pkg/languages/go/tag` |
| `internal/strs` | `pkg/languages/go/strs` |
| `internal/genid` | `pkg/languages/go/genid/genid.h` (the needed constants) |
| `internal/version`, `internal/editionssupport`, `runtime/protoimpl.GenVersion` | constants in `<version>/main.c` |

Generated code is formatted like `GeneratedFile.Content` does with `go/parser` + `go/printer`; the
remote plugin is built with Go 1.26.5 (Go 1.27 formats some comments differently), so the port uses
the Go 1.26.5 standard library port in `pkg/languages/go/stdlib/go1.26.5` (see its README) and a
fixed list of predeclared identifiers instead of `go/types.Universe`.

The raw descriptor embedded in `.pb.go` is serialized like protobuf-go does: the request is parsed
with the types linked into protoc-gen-go (the `pb.go` feature extension of `go_features.proto`),
re-parsed with dynamic types for the extensions declared in the request's files when there are
any, stripped of `retention = RETENTION_SOURCE` fields and marshaled deterministically (extensions
first, dynamic messages in `LegacyFieldOrder`, maps sorted by key).

## Known differences

- Errors from parameter parsing and descriptor validation (`protogen.Options.New`) are returned in
  `CodeGeneratorResponse.error` as `protoc-gen-go: <error>`; upstream prints the same text to
  stderr and exits with status 1. Panics of upstream (impossible inputs such as an unknown
  `api_level` value) are errors too.
- `annotate_code`: the `.pb.go.meta` files are `prototext` output, which protobuf-go deliberately
  randomizes (one or two spaces between fields, chosen from a hash of the running binary), so their
  whitespace differs from upstream's (and between any two builds of upstream); the port writes one
  space. The `.pb.go` files are identical.
- protobuf-go errors start with `proto:` followed by a space or a no-break space (also chosen per
  binary); the port writes a space.
- Where upstream iterates Go maps, the port uses a fixed order: which two files the
  `Go package ... has inconsistent names` error names (random upstream), and which duplicate is
  reported for duplicate reserved names.
- `proto.Unmarshal` without `AllowPartial` rejects missing required fields (only in
  `UninterpretedOption.NamePart`, or in custom option messages); the port does not check required
  fields. The request is parsed by `pkg/pb` first (nesting limited to 100 messages), and nested
  messages inside options are limited by 256 KiB of C stack (protobuf-go: 10000 levels).
- Float default values are parsed with the C library's `strtod` (correctly rounded on musl and
  glibc) for the syntax protoc writes; Go's `strconv.ParseFloat` differs only for unusual
  spellings.
- NUL bytes in file names, `go_package` options and `M` parameter values end the string (protoc
  and buf never produce them); in other strings of the request (parameters, references and names
  in error messages, comments, JSON names, default values) they are kept like upstream.
- Not ported from `compiler/protogen`: `ResolvedFeatures` (descriptorpb feature sets per
  declaration; the edition features are on `Desc->features`), `Options.FeatureSetDefaults`, `Run`.

## Options

Same as upstream: `paths`, `module`, `M<file>=<go package>`, `annotate_code`, `default_api_level`,
`apilevelM<file>=<level>`, `experimental_strip_nonfunctional_codegen`, `plugins` (rejected, as upstream).

Golden option groups and tests: `testdata/checksums/protocolbuffers/go/<version>.buf.gen.yaml`.

## Size and speed

The port links the Go formatter (`pkg/languages/go/stdlib/go1.26.5`, about 220 KB of code at `-O3`, about 120 KB at `-Os`),
the largest part of the binary. Code that runs once per request or declaration (gengo, protogen,
protodesc init and validation) is marked `COLD` (optimized for size), and the well-known type
methods are text templates (`well_known_types.c`) instead of `P` calls: together −123 KB on x86_64
without a measurable change in speed.

Stripped static binaries from the toolchain image; time: the 48 golden requests (12 tests × 4 option
groups) run 10 times, one process per request, best of 3 (x86_64):

| build | x86_64 bytes | aarch64 bytes | 480 requests |
|-------|-------------:|--------------:|-------------:|
| bufbuild/es only (before this generator, `-O3`) | 110 032 | 107 712 | — |
| `-O3` (`-DUNI_OPT=O3`, the default until the switch to `-Os`) | 508 888 | 476 296 | 4.97–5.26 s |
| `-O3`, formatter TU `-O2` (`-DUNI_GOFMT_OPT=O2`) | 488 408 | 464 008 | 4.90–5.17 s (−1.5 %) |
| `-O3`, formatter TU `-Os` (`-DUNI_GOFMT_OPT=Os`) | 406 488 | 402 568 | 5.24–5.66 s (+6 %) |
| **default (`-Os`)** | **349 312** | **357 512** | see below |
| former Go implementation (git `c9b2e00`, `go build -ldflags='-s -w'`) | 6 578 336 | | 5.76 s |

All 222 requests of the fuzz seed corpus (both generators, every option group), one process per
request, best of 5 (x86_64): `-O3` + LTO 1300 ms, `-O2` + LTO 1265 ms, `-Os` + LTO 1214 ms,
`-Os` without LTO 1269 ms. `-Os` is not slower: a plugin process handles a single request, so
loading a smaller binary outweighs the faster hot loops of `-O3`.
