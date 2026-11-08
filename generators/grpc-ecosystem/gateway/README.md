# grpc-ecosystem/gateway

C port of `protoc-gen-grpc-gateway` from [grpc-gateway](https://github.com/grpc-ecosystem/grpc-gateway)
(BSD-3-Clause, Copyright (c) 2015, Gengo, Inc.): <https://buf.build/grpc-ecosystem/gateway>. No Go
toolchain required. It generates the reverse proxy (`.pb.gw.go`) that translates RESTful HTTP/JSON
requests into gRPC calls, from the `google.api.http` annotations of the services, for the types
of [protocolbuffers/go](../../protocolbuffers/go/README.md) and the stubs of
[grpc/go](../../grpc/go/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=grpc-ecosystem/gateway, uni-version=v2.33.0, paths=source_relative]
```

Options are the same as upstream: [grpc-gateway usage](https://grpc-ecosystem.github.io/grpc-gateway/docs/mapping/customizing_your_gateway/),
plus `separate_package` of the buf.build build and the `compiler/protogen` options.

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v2.33.0 | [`protoc-gen-grpc-gateway`](https://github.com/grpc-ecosystem/grpc-gateway/tree/v2.33.0/protoc-gen-grpc-gateway) (de0b890d) with [`separate_pkg_additional_imports.patch`](https://github.com/bufbuild/plugins/tree/main/plugins/grpc-ecosystem/gateway/v2.33.0), [`compiler/protogen` v1.36.12](https://github.com/protocolbuffers/protobuf-go/tree/v1.36.12/compiler/protogen), genproto `googleapis/api` 2ffab32bce7e, `golang.org/x/text` v0.43.0 | 1.27.3 |

Each `.c` file names the upstream file it ports: `main.go` is in `generator.c`,
`internal/gengateway/generator.go` in `gengateway.c`, `template.go` in `template.c`. The packages
the plugin shares with protoc-gen-openapiv2 (`internal/descriptor` with the registry, `httprule`,
`casing`, `utilities`, `codegenerator`) are the library
[`pkg/languages/go/grpc-gateway`](../../../pkg/languages/go/grpc-gateway); buf's patch adds
`internal/descriptor/buf_build.go` and the `separate_package` changes of the registry there, which
do nothing without the option. The plugin framework is the `compiler/protogen` port in
[`pkg/languages/go`](../../../pkg/languages/go), `google.api.HttpRule` comes from
[`pkg/languages/go/genproto`](../../../pkg/languages/go/genproto), and the output is formatted
with the Go standard library port in [`pkg/languages/go/std`](../../../pkg/languages/go/std) in its
default release (Go 1.27.1 formats like 1.27.3): `go/format.Source` (`go_std_format_source`, with
`ast.SortImports`), then `compiler/protogen` reformats the file as for every `.go` file.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin executes `text/template`
templates and formats the text with `go/format.Source` of the Go standard library. The templates are
generator code in `template.c` that writes the same text (the `{{-`/`-}}` trims applied by hand,
so the lines are the same), and an error a template call returns (`GetBodyFieldType`,
`ConvertFuncExpr`) gives the `text/template` error with the position of the call in the template
source. Upstream's quirks are kept: the generator overwrites the names of the messages, services
and methods of a file with their camel case before it uses them (`/<package>.<CamelService>/<CamelMethod>`
in `AnnotateContext`), the template parser accepts `/` for any punctuation token, and so on.

Options (`flag.CommandLine.Set`: bool flags parse with `strconv.ParseBool`, `parse error`
otherwise; unknown names fail with `no such flag -<name>`):
- `register_func_suffix` (`Handler`), `request_context` (`true`), `allow_delete_body`,
  `repeated_path_param_separator` (`csv`, `pipes`, `ssv`, `tsv`; else
  `unknown repeated path parameter separator: <value>`), `allow_patch_feature` (`true`),
  `omit_package_doc`, `strict_deep_wildcard`, `standalone`, `generate_unbound_methods`,
  `use_opaque_api`, `separate_package` (buf's patch: requires `standalone=true`, writes the gateway
  into `<dir>/<pkg>gateway/` and a deprecated alias file at the old path);
- accepted without effect on the output, as upstream: `warn_on_unbound_methods`,
  `allow_repeated_fields_in_body`, `logtostderr`, `version` (main reads it before the parameters
  are set);
- `grpc_api_configuration=<file>` reads a YAML file from the plugin's file system. The buf.build
  plugin runs in an image that holds nothing but the plugin, so it cannot read a user's file
  either: the port gives its error,
  `failed to read gRPC API Configuration description from "<file>": open <file>: no such file or directory`;
- the `compiler/protogen` options `paths`, `module`, `annotate_code` and `M<file>`.

protobuf-go picks some output details with `internal/detrand`, a bit derived from a hash of the
plugin binary. The port uses the bit of the binary built like the buf.build image (Go 1.27.3,
`-trimpath -ldflags="-s -w"`, linux/amd64): it shows as a no-break space after `proto:` in the
errors of protobuf-go.

## Known differences

- Errors of `Options.New` and of the request (upstream: `protoc-gen-grpc-gateway: <error>` on
  stderr, exit status 1) go to `CodeGeneratorResponse.error` with the same text; a request that
  cannot be parsed fails with `protoc-gen-uni: parse request: <error>` on stderr.
- The random choices and nesting limits of the `compiler/protogen` port, listed in
  [protocolbuffers/go](../../protocolbuffers/go/README.md#known-differences).
- The extensions linked into the plugin (`google.api.*` and the `openapiv2_*` options) are
  decoded with the request upstream, so malformed values make it unparsable. The port checks
  `google.api.http` with its schema, the `google.api` string extensions for UTF-8 and
  `field_behavior` for varints, and the other message extensions only for the wire format: invalid
  UTF-8 in a string field inside them, or a wrong type in one of their known fields, is not an
  error.
- Not supported. Generation fails with a `CodeGeneratorResponse.error`
  `unsupported by protoc-gen-uni: ...` that names it:
  - `grpc_api_configuration` naming a path that exists in the plugin's container (the root,
    `/etc`, `/proc`, `/dev`, `/sys` and what is below them, the plugin binary; relative paths from
    `/`): the port reads no files;
  - `standalone=true` with a Go package name that is not ASCII (its alias `ext<Title>` uses the
    Unicode title casing of `golang.org/x/text/cases`, not ported), when the alias is printed;
  - a NUL byte in a file name (`proto_file`, `dependency`), in `go_package` or in an `M<file>`
    parameter;
  - messages nested so deeply that decoding them needs more than 256 KiB of stack, around a
    thousand levels (upstream allows 10000).
