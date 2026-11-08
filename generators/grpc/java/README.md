# grpc/java

C port of `protoc-gen-grpc-java` from [gRPC-Java](https://github.com/grpc/grpc-java)
(Apache-2.0): <https://buf.build/grpc/java>. No protoc required. Generated code needs
`io.grpc:grpc-core`, `io.grpc:grpc-protobuf` (or `grpc-protobuf-lite` with the `lite` option) and
`io.grpc:grpc-stub` 1.85.1, next to the message classes of
[protocolbuffers/java](../../protocolbuffers/java/README.md) v36.2.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/java
    opt: [uni-name=protocolbuffers/java, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/java
    opt: [uni-name=grpc/java, uni-version=v1.85.1]
```

One `<Service>Grpc.java` per service, in the directory of the file's Java package. See the
[gRPC Java generated code reference](https://grpc.io/docs/languages/java/generated-code/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints Java text with the
legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port prints the same text.

## Options

As upstream (`java_plugin.cpp`): options are parsed with `ParseGeneratorParameter`, and unknown
options and values are ignored.

| option | effect |
|--------|--------|
| `lite` | marshallers of `io.grpc.protobuf.lite.ProtoLiteUtils`, no schema descriptor suppliers |
| `noversion` | no ` (version 1.85.1)` in the `@javax.annotation.Generated` value |
| `@generated=omit` | default: only `@io.grpc.stub.annotations.GrpcGenerated` |
| `@generated=javax` | also `@javax.annotation.Generated(value = "by gRPC proto compiler (version 1.85.1)", comments = "Source: <file>")` |

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`compiler/src/java_plugin/cpp`](https://github.com/grpc/grpc-java/tree/v1.85.1/compiler/src/java_plugin/cpp) | protobuf 36.0 ([`buildscripts/make_dependencies.sh`](https://github.com/grpc/grpc-java/blob/v1.85.1/buildscripts/make_dependencies.sh)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) 36.2 |

`generator.c` ports `java_plugin.cpp`, `java_generator.c` ports `java_generator.{h,cpp}`. The remote
plugin runs the prebuilt `protoc-gen-grpc-java-1.85.1-linux-<arch>.exe` from Maven Central
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/java/v1.85.1)),
built with protobuf 36.0: `plugin.cc`, `code_generator.cc`, `descriptor.cc`,
`feature_resolver.cc`, `io/printer.cc` and the Java names of `compiler/java` (`QualifiedClassName`)
are the same as in 36.2, so the port uses the libprotoc 36.2 port and its
[`compiler/java`](../../../pkg/languages/cpp/libprotoc/compiler/java/libprotoc_java.h) names.
Editions up to 2026 are accepted, with the `pb.java` feature extension.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason.
- Failed internal checks (`ABSL_CHECK` of libprotoc's Java names, e.g. `pb.java.nest_in_file_class`
  on a nested message used as a method type) abort upstream without a response; the port returns
  `/protoc-gen-grpc-java: Check failed: ...` in `CodeGeneratorResponse.error`.

## Unsupported

Nothing: every option and input upstream accepts is supported.
