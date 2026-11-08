# grpc-ecosystem/openapiv2

C port of `protoc-gen-openapiv2` from [grpc-gateway](https://github.com/grpc-ecosystem/grpc-gateway)
(BSD-3-Clause, Copyright (c) 2015, Gengo, Inc.): <https://buf.build/grpc-ecosystem/openapiv2>. No Go
toolchain required. It generates OpenAPI v2 (Swagger) documents, JSON or YAML, for the services of
the files and their `google.api.http` annotations, with the `openapiv2_*` options of
`protoc-gen-openapiv2/options/annotations.proto`.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/openapiv2
    opt: [uni-name=grpc-ecosystem/openapiv2, uni-version=v2.33.0, allow_merge=true, output_format=yaml]
```

Options are the same as upstream: [protoc-gen-openapiv2 options](https://grpc-ecosystem.github.io/grpc-gateway/docs/mapping/customizing_openapi_output/).

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v2.33.0 | [`protoc-gen-openapiv2`](https://github.com/grpc-ecosystem/grpc-gateway/tree/v2.33.0/protoc-gen-openapiv2) (de0b890d), [`compiler/protogen` v1.36.12](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.12/compiler/protogen), `encoding/protojson` v1.36.12, `go.yaml.in/yaml/v3` v3.0.5, genproto `googleapis/api` 2ffab32bce7e and `googleapis/rpc` ce908d29299b, `golang.org/x/text` v0.43.0 | 1.27.3 (`encoding/json` on the jsonv2 engine, the default since Go 1.27) |

buf.build builds the plugin without patches ([Dockerfile](https://github.com/bufbuild/plugins/tree/main/plugins/grpc-ecosystem/openapiv2/v2.33.0)).

Each `.c` file names the upstream file it ports: `main.go`, `internal/genopenapi/generator.go` and
`format.go` are in `generator.c`, `types.go` in `genopenapi.h` and `types.c` (with the
`MarshalJSON`/`MarshalYAML` methods of `generator.go`), `template.go` and `helpers.go` in
`template.c`, `naming.go` in `naming.c`. The packages the plugin shares with protoc-gen-grpc-gateway
(`internal/descriptor` with the registry, `httprule`, `casing`, `codegenerator`) and the tables of
the options package are the library [`pkg/languages/go/grpc-gateway`](../../../pkg/languages/go/grpc-gateway);
`encoding/json` (Go 1.27, jsonv2 engine), `strconv.ParseFloat` and the `time.Parse` layouts of the
header checks are in the Go standard library port [`pkg/languages/go/std`](../../../pkg/languages/go/std),
`yaml.v3` in [`pkg/languages/go/yaml`](../../../pkg/languages/go/yaml), the JSON of
`google.protobuf.Value` (extension values) in [`pkg/languages/go/protojson`](../../../pkg/languages/go/protojson),
the `google.api` extensions in [`pkg/languages/go/genproto`](../../../pkg/languages/go/genproto), and
the request is loaded with the `compiler/protogen` port in [`pkg/languages/go`](../../../pkg/languages/go).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream builds Go structs and marshals
them with `encoding/json` (`json.Encoder` with `SetIndent("", "  ")`) or `yaml.v3` (`SetIndent(2)`);
the port builds the same objects (`genopenapi.h`, with Go's value, slice and map semantics where the
code depends on them) and writes what the two encoders write for their struct tags: field order,
`omitempty`, inlined extensions, the `MarshalJSON`/`MarshalYAML` methods, key order of maps (bytes
for JSON, the natural order of `yaml.v3` for YAML), number and string formats, HTML escaping. The
YAML is built as `yaml.Node` trees with the scalar styles of the value encoder and emitted by the
`yaml.v3` port. Upstream's quirks are kept, for example: the required list of a field's
`openapiv2_field` option is sorted and compacted in place (a later use of the option sees it so),
`expand_slashed_path_patterns` rewrites the path parameters of the binding in place, merged files
`copy` their paths over the leader's, comments of services are looked up by their index among the
services that have methods.

Options (`flag.CommandLine.Set` from the parameter: bool flags parse with `strconv.ParseBool`,
`recursive-depth` with `strconv.ParseInt`; an element without `=` sets `true` for
`allow_delete_body`, `allow_merge`, `allow_repeated_fields_in_body` and `include_package_in_tags`,
and the empty string for the others; `M<file>=<path>` goes to protogen):
`allow_merge`, `merge_file_name` (`apidocs`), `output_format` (`json`, `yaml`), `json_names_for_fields`
(`true`), `openapi_naming_strategy` (`legacy`, `simple`, `package`, `fqn`), `fqn_for_openapi_name`,
`allow_delete_body`, `repeated_path_param_separator` (`csv`, `pipes`, `ssv`, `tsv`),
`include_package_in_tags`, `ignore_comments`, `remove_internal_comments`, `disable_default_errors`,
`enums_as_ints`, `simple_operation_ids`, `proto3_optional_nullable`, `generate_unbound_methods`,
`recursive-depth` (`1000`), `omit_enum_default_value`, `visibility_restriction_selectors`
(repeatable), `disable_service_tags`, `disable_default_responses`, `use_allof_for_refs`,
`omit_array_item_type_when_ref_sibling`, `allow_patch_feature` (`true`), `preserve_rpc_order`,
`enable_rpc_deprecation`, `enable_field_deprecation`, `expand_slashed_path_patterns`,
`use_proto3_field_semantics`, `generate_x_go_type`, `ignore_go_package_option`; accepted without
effect, as upstream: `import_prefix`, `allow_repeated_fields_in_body`, `logtostderr`, `file` and
`version` (main reads them before the parameter is parsed). `grpc_api_configuration=<file>` and
`openapi_configuration=<file>` read a YAML file from the plugin's file system; the buf.build plugin
runs in an image that holds nothing but the plugin, so the port gives the error upstream gives
there, `failed to read gRPC API Configuration description from "<file>": open <file>: no such file or directory`
(`OpenAPI Configuration` for the other).

protobuf-go picks some output details with `internal/detrand`, a bit derived from a hash of the
plugin binary. The port uses the bit of the binary built like the buf.build image (Go 1.27.3,
`-trimpath -ldflags="-s -w"`, linux/amd64): it shows as a no-break space after `proto:` in the
errors of protobuf-go (`protojson` errors of extension values, `Options.New`).

## Known differences

- Errors upstream reports with `grpclog.Fatal` (stderr with a timestamp, exit status 1: a request
  that cannot be parsed, malformed values of the linked extensions, a parameter that does not
  parse, `fqn_for_openapi_name` with `openapi_naming_strategy`) fail with
  `protoc-gen-openapiv2: <error>` on stderr.
- The random choices of upstream: when several entries of an option map are invalid (extension
  keys without `x-`, headers), the port reports the first in key order, upstream a random one.
- The random choices and nesting limits of the `compiler/protogen` port, listed in
  [protocolbuffers/go](../../protocolbuffers/go/README.md#known-differences).
- The extensions linked into the plugin are decoded with the request upstream. The port checks
  `google.api.http`, the `openapiv2_*` options and `google.api.VisibilityRule` with their schemas
  and the `google.api` string extensions for UTF-8, and the other `google.api` message extensions
  only for the wire format.
- Not supported. Generation fails with a `CodeGeneratorResponse.error`
  `unsupported by protoc-gen-uni: ...` that names it:
  - `use_go_templates=true` (comments and descriptions as Go `text/template` templates, with the
    `import`, `fieldcomments` and `arg` functions; upstream also reads files with `import`);
  - `grpc_api_configuration` and `openapi_configuration` naming a path that exists in the
    plugin's container (the root, `/etc`, `/proc`, `/dev`, `/sys` and what is below them, the
    plugin binary): the port reads no files;
  - inputs where upstream panics (a Go stack trace on stderr), for example a JSON schema option of
    type `ARRAY` on a message, `expand_slashed_path_patterns` with a pattern on a field it cannot
    find, a PATCH binding without body on a request with an `update_mask`, a `ref` in an option
    that names neither a message nor an enum (infinite recursion), extension keys that make
    `reflect.StructOf` panic (invalid or duplicate Go field names after title casing);
  - extension keys that are not ASCII in JSON output (the title casing of `golang.org/x/text` for
    their Go field names is not ported);
  - errors of `encoding/json` and of `json.Unmarshal` (YAML output) on values that are not JSON:
    invalid `example`s, header `default`s or extension values, non-finite numbers in JSON schema
    options (upstream: an error naming the chain of `MarshalJSON` calls and byte offsets);
  - query parameters of recursive messages that upstream would expand without finishing (more
    than two million steps; lower `recursive-depth`);
  - a NUL byte in a file name, `go_package`, `M<file>` parameter or `openapi_naming_strategy`;
  - messages or JSON values nested so deeply that the port's recursion would exhaust its stack.
