# community/scalapb-zio-grpc

C port of `protoc-gen-zio` from [ZIO gRPC](https://github.com/scalapb/zio-grpc) (Apache-2.0):
<https://buf.build/community/scalapb-zio-grpc>. No JVM required. For every file with services it
generates `Zio<File>.scala` with the ZIO service traits, bindings and clients of its services; the
messages come from [community/scalapb-scala](../scalapb-scala/README.md) of the same ScalaPB version
(0.11.21), and the gRPC descriptors (`<Service>Grpc`) from its `grpc` option. Generated code needs
`zio-grpc-core` 0.6.4.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/scala
    opt: [uni-name=community/scalapb-zio-grpc, uni-version=v0.6.4]
```

## Versions

| version | upstream sources |
|---------|------------------|
| v0.6.4 | [`ZioCodeGenerator.scala`](https://github.com/scalapb/zio-grpc/blob/v0.6.4/code-gen/src/main/scala/scalapb/zio_grpc/ZioCodeGenerator.scala); [ScalaPB 0.11.21 `compiler-plugin`](https://github.com/scalapb/ScalaPB/tree/v0.11.21/compiler-plugin/src/main/scala/scalapb), [protoc-bridge v0.9.10 `protoc-gen`](https://github.com/scalapb/protoc-bridge/tree/v0.9.10/protoc-gen/src/main/scala/protocgen), protobuf-java 3.25.8, Scala 2.12.21 |

The buf.build recipe (`plugins/community/scalapb-zio-grpc/v0.6.4`) runs the self-contained jar
`protoc-gen-zio-0.6.4-unix.sh` from Maven Central on Temurin 25 (`java -jar`); its `BuildInfo`
names ScalaPB 0.11.21, protobuf 3.25.8 and grpc 1.62.2. The generator
([`generator.c`](v0.6.4/generator.c)) ports `ZioCodeGenerator`; what it uses of ScalaPB
(`DescriptorImplicits`, `NameUtils`, `FunctionalPrinter`, `GeneratorParams`), protoc-bridge
(`CodeGenApp`) and protobuf-java (`Descriptors`) is shared with community/scalapb-scala in the
libraries [`pkg/languages/scala/scalapb`](../../../pkg/languages/scala/scalapb/scalapb.h),
[`pkg/languages/scala/protoc-bridge`](../../../pkg/languages/scala/protoc-bridge/protocgen.h) and
[`pkg/languages/java/protobuf-java`](../../../pkg/languages/java/protobuf-java/protobuf_java.h).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). `ZioFilePrinter` prints Scala text line
by line with ScalaPB's `FunctionalPrinter` and runs no formatter: the port prints the same text.

## Options

The [ScalaPB generator parameters](https://scalapb.github.io/docs/scalapbc/#passing-generator-parameters)
(`java_conversions`, `flat_package`, `grpc`, `single_line_to_proto_string`,
`ascii_format_to_string`, `no_lenses`, `retain_source_code_info`, `scala3_sources`), parsed like
ScalaPB does (`Unrecognized parameters: ...`), and the
[`scalapb.proto` options](https://scalapb.github.io/docs/customizations) in the protos. Of these,
`flat_package` and the file options that name the Scala package and types (`package_name`,
`flat_package`, `object_name`, `scope`, `no_primitive_wrappers`, `(scalapb.message).type`)
change the output; the other parameters are accepted and have no effect, as upstream.

Only proto2 and proto3 are supported (`FEATURE_PROTO3_OPTIONAL`), as upstream: buf rejects editions
files.

## Unsupported

As in [community/scalapb-scala](../scalapb-scala/README.md), generation fails with a
`CodeGeneratorResponse.error` that names it (`... not supported by protoc-gen-uni` or
`protoc-gen-uni does not support ...`) instead of producing a different output:

- `field_transformations` in `scalapb.proto` options (ScalaPB resolves them when it builds the
  file options, also for zio-grpc).
- Characters of the Basic Multilingual Plane beyond ASCII where ScalaPB applies Java's Unicode
  character rules: in file names (they become `Zio<File>` and the default package object) and as
  the first character of a name that becomes a Scala identifier (package components,
  `package_name`).
- U+0000 in names, options and comments (in the parameter it is supported).

## Known differences

- Errors go to `CodeGeneratorResponse.error` with the same message, but where upstream reports an
  exception with its stack trace (a `GeneratorException` of the ScalaPB file options, which
  zio-grpc does not catch: `scalapb.compiler.GeneratorException: <file>: a package statement is
  required ...`; a `DescriptorValidationException`, ...), only its first line is reproduced.
- When several files are invalid, upstream reports the first one in the iteration order of a Scala
  hash map; the port uses request order (see community/scalapb-scala).
