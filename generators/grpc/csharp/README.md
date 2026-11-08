# grpc/csharp

C port of `grpc_csharp_plugin` from [gRPC](https://github.com/grpc/grpc) (Apache-2.0):
<https://buf.build/grpc/csharp>. No protoc required. Generated code needs the `Grpc.Core.Api`
(or `Grpc.Net.Common`) NuGet package, next to the message classes of
[protocolbuffers/csharp](../../protocolbuffers/csharp/README.md) v36.2 (the reflection class and
message types it refers to).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/csharp
    opt: [uni-name=protocolbuffers/csharp, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/csharp
    opt: [uni-name=grpc/csharp, uni-version=v1.85.1, base_namespace=]
```

One `<File>Grpc.cs` per file with services, with a static class per service holding the
marshallers, the `grpc::Method` fields, the service descriptor, the server base class
`<Service>Base`, the client `<Service>Client` and `BindService`, in the namespace of
`csharp_namespace` (or the package in PascalCase). See the
[gRPC C# basics tutorial](https://grpc.io/docs/languages/csharp/basics/). The remote plugin's
registry configuration adds `base_namespace=` (output directories by namespace, like
protocolbuffers/csharp), as in the example above.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints C# text with the
legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port prints the same text.

## Options

Comma-separated `key=value` pairs (`ParseGeneratorParameter`), each split on the first `=`.

| option | effect |
|--------|--------|
| `no_client` | no `<Service>Client` class |
| `no_server` | no `<Service>Base` class nor `BindService` methods |
| `internal_access` | the service class is `internal` instead of `public` |
| `file_suffix=<s>` | the suffix of the file name instead of `Grpc.cs` |
| `base_namespace=<ns>` | the file goes to the directories of its namespace below `<ns>` and is named like protocolbuffers/csharp names its file (`GetOutputFile`); a namespace outside `<ns>` is the error `<file>: Namespace <namespace> is not a prefix namespace of base namespace <ns>` |
| `append_async_suffix=true` | server methods end with `Async` (any other value: false) |

Any other key is the error `<file>: Unknown generator option: <key>`.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`src/compiler`](https://github.com/grpc/grpc/tree/v1.85.1/src/compiler) (`csharp_plugin.cc`, `csharp_generator*`) | protobuf 35.1 ([`MODULE.bazel`](https://github.com/grpc/grpc/blob/v1.85.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_35.1.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_35.1.c) |

`generator.c` ports `csharp_plugin.cc`, `csharp_generator.c` ports `csharp_generator.{h,cc}` and
`csharp_generator_helpers.h`. The `GRPC_CUSTOM_CSHARP_*` names of `config_protobuf.h`
(`GetClassName`, `GetFileNamespace`, `GetOutputFile`, `GetReflectionClassName` of
`compiler/csharp/names.cc`, the same in 35.1 and 36.2) are
[`csharp_names.h`](../../../pkg/languages/cpp/libprotoc/csharp_names.h) of libprotoc, shared with
protocolbuffers/csharp. The comment helpers of `generator_helpers.h` (`grpc_plugin_support`,
shared by the grpc C++ plugins) are [`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h).
The remote plugin is built with Bazel from the v1.85.1 tag
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/csharp/v1.85.1)),
with protobuf 35.1 and `GRPC_PROTOBUF_EDITION_SUPPORT`: `supported_features` is
`FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to 2024 (see
[grpc/ruby](../ruby/README.md) for what 35.1 changes in libprotoc).

Like upstream, file comments are those above the `syntax` (or `edition`) statement, printed raw
up to a NUL byte (`PrintRaw(c_str())`) with `$` doubled; comments on services and methods become
`<summary>` doc comments with `&` and `<` escaped, a leading `/` as `&#x2F;`, and NUL bytes kept.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason (see [grpc/ruby](../ruby/README.md)).
- When generation fails (an unknown option, a namespace outside `base_namespace`), the response
  has only the error. Upstream also lists the files opened before the error, which protoc and buf
  ignore when there is an error.
- An empty output file name with `base_namespace` (`file_suffix=` and a file without package or
  `csharp_namespace` whose base name has no letters or digits, such as `_.proto`): upstream
  aborts on an `ABSL_CHECK` ("Code generator returned false but provided no error
  description."); the port returns that text as the error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that names the element.

- `uninterpreted_option` in any options message (libprotoc): the option interpreter of protoc is
  not ported. protoc and buf never produce it. The error is reported while the descriptors are
  built and starts with `unsupported by protoc-gen-uni:`.
- A `csharp_namespace` that contains a NUL byte (libprotoc's C# names): upstream prints the whole
  string, NUL included, into the generated file; the port's names end at NUL. The error is raised
  during generation, so like every failure there it starts with the program name:
  `/grpc_csharp_plugin_binary: unsupported by protoc-gen-uni: csharp_namespace with a NUL byte in
  "<file>"`.
