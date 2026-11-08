# grpc/objc

C port of `grpc_objective_c_plugin` from [gRPC](https://github.com/grpc/grpc) (Apache-2.0):
<https://buf.build/grpc/objc>. No protoc required. Generated code needs gRPC Objective-C
(`ProtoRPC`, `RxLibrary`, the `gRPC-ProtoRPC` pod), next to the message classes of
[protocolbuffers/objc](../../protocolbuffers/objc/README.md) (the `<File>.pbobjc.h` it imports).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/objc
    opt: [uni-name=protocolbuffers/objc, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/objc
    opt: [uni-name=grpc/objc, uni-version=v1.85.1]
```

`<dir>/<File>.pbrpc.h` and `<dir>/<File>.pbrpc.m` per file with services (the path is
`FilePath` of the Objective-C generator), with a `<Service>2` protocol (v2 API), the deprecated
`<Service>` protocol (v1 API) and a `GRPCProtoService` subclass per service. See the
[gRPC Objective-C README](https://github.com/grpc/grpc/tree/v1.85.1/src/objective-c).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin builds Objective-C text with
string concatenation and the legacy `io::Printer::Print` API of libprotoc, and runs no formatter:
the port prints the same text.

## Options

Comma-separated, each split on `=`; unknown options are ignored. They are only parsed for files
with services, so errors name such a file (`<file>: <error>`).

| option | effect |
|--------|--------|
| `generate_for_named_framework=<Framework>` | imports of generated headers as `#import <Framework/File.pbobjc.h>` (the directory dropped). Without a value: `Format: generate_for_named_framework=<Framework>`; empty: `Name of framework cannot be empty for parameter: generate_for_named_framework` |
| `runtime_import_prefix=<dir>` | prefix of the local imports of the well-known types' `GPB*.pbobjc.h` (one trailing `/`, compared case-insensitively, removed). Without a value: `Format: runtime_import_prefix=dir/` |
| `grpc_local_import_prefix=<dir>` | gRPC headers imported as `#import "<dir>ProtoRPC/..."` instead of `<ProtoRPC/...>`. Without a value: `Format: grpc_local_import_prefix=dir/` |
| `no_v1_compatibility` | no v1 API (with or without a value) |

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`src/compiler`](https://github.com/grpc/grpc/tree/v1.85.1/src/compiler) (`objective_c_plugin.cc`, `objective_c_generator*`) | protobuf 35.1 ([`MODULE.bazel`](https://github.com/grpc/grpc/blob/v1.85.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_35.1.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_35.1.c) |

`generator.c` ports `objective_c_plugin.cc`, `objective_c_generator.c` ports
`objective_c_generator.{h,cc}` and `objective_c_generator_helpers.h`. The Objective-C names
(`FilePath`, `FileClassPrefix`, `ClassName`, `IsProtobufLibraryBundledProtoFile`) are
`compiler/objectivec/names.cc` of libprotoc ([`objectivec_names.h`](../../../pkg/languages/cpp/libprotoc/objectivec_names.h)),
the same in protobuf 35.1 and 36.2. The comment helpers of `generator_helpers.h` and `tokenize`,
`StripPrefix`, `StripSuffix`, `LowercaseFirstLetter` (`grpc_plugin_support`, shared by the grpc
C++ plugins) are [`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h). The remote
plugin is built with Bazel from the v1.85.1 tag
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/objc/v1.85.1)),
with protobuf 35.1 and `GRPC_PROTOBUF_EDITION_SUPPORT`: `supported_features` is
`FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to 2024.
`SUPPORT_OBJC_PREFIX_VALIDATION` and `OBJC_SKIP_METHODS_WITHOUT_MESSAGE_PREFIX` are not defined:
class prefixes are not validated and every method is generated.

Like upstream, the comments of methods (leading detached, leading and trailing, each line with its
leading spaces removed) are printed raw, up to a NUL byte.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason. See [grpc/ruby](../ruby/README.md) for the libprotoc 35.1 validation.
- The environment variables of `names.cc` (`GPB_OBJC_USE_PACKAGE_AS_PREFIX`, ...) are not read;
  the remote plugin runs without them.
- When generation fails, the response has only the error (upstream also lists the files opened
  before it, which protoc and buf ignore).

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that names the element, after
`/grpc_objective_c_plugin_binary: unsupported by protoc-gen-uni:`.

- A parameter that contains a NUL byte (when a file has services): the port's strings end at NUL.
- `uninterpreted_option` in any options message (libprotoc): the option interpreter of protoc is
  not ported. protoc and buf never produce it.
