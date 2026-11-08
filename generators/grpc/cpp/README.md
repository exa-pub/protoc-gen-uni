# grpc/cpp

C port of `grpc_cpp_plugin` from [gRPC](https://github.com/grpc/grpc) (Apache-2.0):
<https://buf.build/grpc/cpp>. No protoc required. Generated code needs gRPC C++ (`grpcpp`), next
to the message classes of [protocolbuffers/cpp](../../protocolbuffers/cpp/README.md) v36.2 (the
`<file>.pb.h` it includes).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/cpp
    opt: [uni-name=protocolbuffers/cpp, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/cpp
    opt: [uni-name=grpc/cpp, uni-version=v1.85.1]
```

`<file>.grpc.pb.h` and `<file>.grpc.pb.cc` per file, with or without services, and
`<file>_mock.grpc.pb.h` with `generate_mock_code=true`: per service a class with the service
name, a `Stub`, the `Service` base class and the `With*Method_` templates of the async, callback,
generic, raw and streamed APIs, in the namespaces of the package. See the
[gRPC C++ basics tutorial](https://grpc.io/docs/languages/cpp/basics/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints C++ text with the
legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port prints the same text.

## Options

Comma-separated `key=value` pairs, each split on `=`.

| option | default | effect |
|--------|---------|--------|
| `services_namespace=<ns>` | none | puts the service classes into namespace `<ns>` |
| `use_system_headers=true\|false` | `true` | grpc includes with `<>` or `""` |
| `grpc_search_path=<dir>` | none | prefix of the grpc includes (a `/` is added) |
| `generate_mock_code=true\|false` | `false` | also writes `<file>_mock.grpc.pb.h` (Google Mock) |
| `gmock_search_path=<dir>` | none | `#include "<dir>/gmock.h"` instead of `gmock/gmock.h` |
| `additional_header_includes=<a>:<b>` | none | extra `#include "<a>"` lines at the top of `.grpc.pb.h` |
| `message_header_extension=<ext>` | `.pb.h` | extension of the message headers included |
| `include_import_headers=true\|false` | `false` | includes the message headers of the imports |
| `allow_sync_server_api=true\|false` | `true` | `virtual` synchronous service methods and the streamed services |
| `allow_cq_api=true\|false` | `true` | the completion queue (async) API |
| `allow_deprecated=true\|false` | `false` | `[[deprecated]]` on deprecated services |

A boolean other than `true` or `false` fails with `<file>: Invalid parameter: <key>=<value>`, any
other key with `<file>: Unknown parameter: <key>[=<value>]`. An option given twice takes the last
value, except that `generate_mock_code=false` and `include_import_headers=false` never clear an
earlier `=true` (upstream sets only `true`). A file with
`option cc_generic_services = true` fails with `cpp grpc proto compiler plugin does not work with
generic services. ...`.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`src/compiler`](https://github.com/grpc/grpc/tree/v1.85.1/src/compiler) (`cpp_plugin.{h,cc}`, `cpp_generator*`) | protobuf 35.1 ([`MODULE.bazel`](https://github.com/grpc/grpc/blob/v1.85.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_35.1.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_35.1.c) |

`generator.c` ports `cpp_plugin.{h,cc}` (the parameters and the files written),
`cpp_generator.c` ports `cpp_generator.{h,cc}` with the parts of `cpp_generator_helpers.h`
(`ClassName`, `GetCppComments`), `protobuf_plugin.h` and `config.h` it uses. The helpers of
`generator_helpers.h` and `proto_parser_helper.cc` (`grpc_plugin_support`, shared by the grpc C++
plugins) are [`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h). The remote
plugin is built with Bazel from the v1.85.1 tag in the default `fastbuild` mode
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/cpp/v1.85.1)), so the
debug checks of libprotoc are on; with protobuf 35.1 and `GRPC_PROTOBUF_EDITION_SUPPORT`,
`supported_features` is `FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to
2024.

Like upstream, file comments are those above the `syntax` (or `edition`) statement, printed raw:
`$` in them stays doubled (`$$`), while comments on services and methods go through `Print` with
`$` escaped, so they keep a single `$`.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason. The validation and its texts are those of libprotoc 36.2 except for the parts versioned
  for 35.1 (see [grpc/ruby](../ruby/README.md)).
- With `include_import_headers=true`, the names of the imports are printed as `Print` formats: an
  import whose name has an odd number of `$` (or names a variable that is not defined) fails the
  debug check of `io::Printer`, which aborts upstream with no response. The port returns
  `/grpc_cpp_plugin_binary: undefined variable: "<name>"` in `CodeGeneratorResponse.error`.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that names the element, after
`/grpc_cpp_plugin_binary: unsupported by protoc-gen-uni:`.

- A known option without `=` (`generate_mock_code`): upstream reads `param[1]` past the end of a
  vector (undefined behavior).
- A parameter that contains a NUL byte: the port's strings end at NUL.
- `uninterpreted_option` in any options message (libprotoc): the option interpreter of protoc is
  not ported. protoc and buf never produce it.
