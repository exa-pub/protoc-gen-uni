# community/google-gnostic-openapi

C port of `protoc-gen-openapi` from [google/gnostic](https://github.com/google/gnostic)
(Apache-2.0): <https://buf.build/community/google-gnostic-openapi>. No Go toolchain required. It
generates an OpenAPI v3 description (YAML) of the services of the input files from their
`google.api.http` bindings, comments and `openapi.v3` annotations.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/openapi
    opt: [uni-name=community/google-gnostic-openapi, uni-version=v0.7.1, naming=proto]
```

Options are the same as upstream: [options](https://github.com/google/gnostic/blob/v0.7.1/cmd/protoc-gen-openapi/README.md),
plus the `compiler/protogen` options (`paths`, `module`, `annotate_code`, `M…`).

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v0.7.1 | [`cmd/protoc-gen-openapi`](https://github.com/google/gnostic/tree/v0.7.1/cmd/protoc-gen-openapi) (`main.go`, `generator/`), [gnostic-models v0.7.0](https://github.com/google/gnostic-models/tree/v0.7.0) (`openapiv3`, `compiler`), [go.yaml.in/yaml/v3 v3.0.4](https://github.com/yaml/go-yaml/tree/v3.0.4), [genproto `googleapis/api` 20250811160224](https://github.com/googleapis/go-genproto/tree/6b04f9b4fc78/googleapis/api/annotations), [`compiler/protogen` v1.36.7](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.7/compiler/protogen) | 1.25.0 |

The versions are those of gnostic's `go.mod` (the buf.build image runs `go install …@v0.7.1`)
and of the `golang:1.25.0` image of its
[Dockerfile](https://github.com/bufbuild/plugins/tree/main/plugins/community/google-gnostic-openapi/v0.7.1).
Each `.c` file names the upstream file it ports:

- `v0.7.1/generator.c`: `main.go` (the flags, the two output modes) and the request handling;
  `v0.7.1/generator/`: `generator.go`, `reflector.go`, `utils.go`, `wellknown/`.
- [`pkg/languages/go/yaml`](../../../pkg/languages/go/yaml): go.yaml.in/yaml/v3 for `yaml.Node`:
  `Marshal` (emitter) and `Unmarshal` (reader, scanner, parser, the `Node` composer), `resolve.go`.
- [`pkg/languages/go/gnostic-models`](../../../pkg/languages/go/gnostic-models): the `openapi.v3`
  messages with protobuf-go semantics (decoding, `proto.Merge`), their generated `ToRawInfo`
  methods as tables (`mkopenapiv3.py` derives them from `OpenAPIv3.proto` and `OpenAPIv3.go`),
  `Document.YAMLValue` and the node constructors of `compiler/helpers.go`.
- [`pkg/languages/go/genproto`](../../../pkg/languages/go/genproto): `google.api.HttpRule` and the
  `google.api` extensions linked into the plugin.
- [`pkg/languages/go/protogen`](../../../pkg/languages/go/protogen): `compiler/protogen` with the
  differences of v1.36.7 (`protogen_v1.36.7.c`: every `proto_file` is processed, editions only up
  to 2023).
- [`pkg/languages/go/std`](../../../pkg/languages/go/std): `strconv.ParseInt`/`ParseBool` (the
  flags, `resolve`), `net/url` (`Parse`, `URL.String` of `google.api.default_host`), `sort.Slice`
  (tags, paths and schemas), `strconv.FormatFloat` (`%g`); the Go 1.27.1 port matches Go 1.25.0
  for these.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-openapi builds an OpenAPI
document (protobuf messages of gnostic-models), converts it to a `yaml.Node` tree and prints it
with the YAML emitter of go.yaml.in/yaml/v3 (best width 80, indent 4): the port builds the same
document and node tree and ports that emitter. No third-party formatter is involved. The regular
expressions of `generator.go` (`\(-- .* --\)`, `{([^=}]+)}`, `{(.+)=(.+)}`) are matched by hand
with the same leftmost-first semantics.

All options of upstream are implemented: `version`, `title`, `description`, `naming`,
`fq_schema_naming`, `enum_type`, `depth`, `default_response`, `output_mode`, with the errors of
Go's `flag` package (`no such flag -x`, `parse error`, `value out of range`), and the
`compiler/protogen` options `paths`, `module`, `annotate_code` and `M<file>`. The plugin declares
proto3 `optional`, not editions: buf rejects editions inputs, protoc passes edition 2023 (supported
like upstream) and edition 2024, which fails with the error of protobuf-go v1.36.7.

The YAML port is checked against go.yaml.in/yaml/v3 by
[`pkg/languages/go/yaml/tests`](../../../pkg/languages/go/yaml/tests) (scalars, the parser and
the emitter on 844 cases; larger random corpora gave no difference either).

## Known differences

- Upstream reports some errors through stderr and exit status 1 (`protoc-gen-openapi: <error>`:
  the request, flags and `compiler/protogen`); the port returns the same text in
  `CodeGeneratorResponse.error`, except for a request that cannot be parsed, which goes to stderr
  with exit status 1 and the prefix `protoc-gen-uni: parse request: `.
- `Go package "…" has inconsistent names` of `compiler/protogen` names the first pair of files in
  a fixed order; upstream takes them from a Go map, in random order.
- Malformed values of the linked `google.api` extensions that the plugin does not read
  (`resource`, `resource_definition`, `resource_reference`, `field_info`, `routing`) make the
  request unparsable upstream; the port ignores them.
- Not supported (generation fails with a `CodeGeneratorResponse.error`
  `unsupported by protoc-gen-uni: ...` that names it):
  - inputs where upstream panics (a goroutine trace and exit status 2), all of them with
    `openapi.v3` annotations or wrapper types upstream does not expect: a nil pointer dereference
    on an annotation value without its message (a `NamedAny` without `value`, a
    `SecurityRequirement` entry without `value`, a path item without `value`) or on a
    `google.protobuf.*Value` message without a `value` field;
  - query parameters nested more than 1000 levels deep (a large `depth` with recursive messages);
  - YAML of `openapi.v3.Any` values nested more than 1000 levels, documents nested more than 2000;
  - requests that need more than 256 KiB of stack to decode, as for
    [protocolbuffers/go](../../protocolbuffers/go/README.md), and the NUL bytes it lists.
