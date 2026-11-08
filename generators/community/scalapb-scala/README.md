# community/scalapb-scala

C port of `protoc-gen-scala` from [ScalaPB](https://github.com/scalapb/ScalaPB) (Apache-2.0):
<https://buf.build/community/scalapb-scala>. No JVM required. Generated code needs the
`scalapb-runtime` (and for `grpc`, `scalapb-runtime-grpc`) of the same version.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/scala
    opt: [uni-name=community/scalapb-scala, uni-version=v0.11.21, grpc]
```

Options are the same as upstream: [generator parameters](https://scalapb.github.io/docs/scalapbc/#passing-generator-parameters)
(`java_conversions`, `flat_package`, `grpc`, `single_line_to_proto_string`,
`ascii_format_to_string`, `no_lenses`, `retain_source_code_info`, `scala3_sources`) and the
[`scalapb.proto` options](https://scalapb.github.io/docs/customizations) in the protos.

## Versions

| version | upstream sources |
|---------|------------------|
| v0.11.21 | [`compiler-plugin`](https://github.com/scalapb/ScalaPB/tree/v0.11.21/compiler-plugin/src/main/scala/scalapb), [`protoc-gen`](https://github.com/scalapb/protoc-bridge/tree/v0.9.10/protoc-gen/src/main/scala/protocgen) of protoc-bridge v0.9.10, parts of [protobuf-java 3.25.8](https://github.com/protocolbuffers/protobuf/tree/v25.8/java/core/src/main/java/com/google/protobuf) (`Descriptors`, `TextFormat`, `UnknownFieldSet`, serialization) |

`protoc-gen-scala` is `scalapb.ScalaPbCodeGenerator`, a `CodeGenApp` of protoc-bridge that runs
`ProtobufGenerator.handleCodeGeneratorRequest`; the generator ([`generator.c`](v0.11.21/generator.c))
is that object, and the code it runs is in libraries shared with the other ScalaPB plugins
([community/scalapb-zio-grpc](../scalapb-zio-grpc/README.md)):
[`pkg/languages/scala/scalapb`](../../../pkg/languages/scala/scalapb/scalapb.h) (the
`compiler-plugin`), [`pkg/languages/scala/protoc-bridge`](../../../pkg/languages/scala/protoc-bridge/protocgen.h)
(`protocgen`), [`pkg/languages/java/protobuf-java`](../../../pkg/languages/java/protobuf-java/protobuf_java.h)
(`Descriptors`, serialization) and [`pkg/languages/java/std`](../../../pkg/languages/java/std/java_std.h)
(`Double.toString`/`Float.toString`, `String`). Each `.c` file names the upstream file it ports.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). ScalaPB prints Scala text line by line
with its `FunctionalPrinter` (explicit indentation, no line breaking) and runs no formatter
(scalafmt is not part of the plugin): the port prints the same text.

The host is the JVM of the buf.build plugin image (Temurin 25): string fields of the request are
decoded like `new String(bytes, UTF_8)` of JDK 25 (`ByteString.toStringUtf8`), so invalid UTF-8
becomes U+FFFD by the JDK's rules (for example, a truncated sequence at the end of a string becomes
one U+FFFD), and strings are compared in UTF-16 order (`String.compareTo`).

Unsupported input fails with a `CodeGeneratorResponse.error` that names it (`... not supported by
protoc-gen-uni` or `protoc-gen-uni does not support ...`) instead of producing a different
output:

- `field_transformations` in `scalapb.proto` options.
- Characters of the Basic Multilingual Plane beyond ASCII where ScalaPB applies Java's Unicode
  character rules (`Character.isLowerCase`/`isUpperCase`/`isDigit`, case mapping): in file names
  (they become object and outer class names) and field `scala_name` options (camel-cased for
  accessors), and as the first character of a name that becomes a Scala identifier (`asSymbol`:
  package components, `java_package`, `package_name`, `scala_name` options). Proto identifiers are ASCII anyway (descriptor validation), and supplementary
  characters (two surrogates in Java, neither letters nor digits) are handled.
- Non-ASCII characters in integer default values (`Long.parseLong` takes any Unicode digit).
- U+0000 in names, options and comments. In string and bytes default values and in the parameter
  it is supported.

## Known differences

- Errors go to `CodeGeneratorResponse.error` with the same message, but where upstream reports
  an unexpected exception (a `DescriptorValidationException`, a `NullPointerException`, ...) with
  its whole stack trace, only the first line is reproduced. A stack overflow of the JVM (for
  example on a chain of thousands of public imports) is not reproduced: the port generates the
  files.
- Preprocessors: like the remote plugin, there is no secondary output directory, so a file that
  enables preprocessors gets upstream's "No secondary outputs available" error; the protoc-bridge
  `ExtraEnv` request field and environment variable, which name a local directory, are ignored.
- When several files or definitions are invalid, upstream reports the first one in the iteration
  order of a Scala hash map (`allProtos`), and `DescriptorPool` searches the dependencies of a file
  in the order of a Java `HashSet` (identity hash codes, which differ between runs) when two of
  them define the same name; the port uses request and import order. Successful output of valid
  requests does not depend on this order.
- Upstream recomputes names on every use (implicit classes), which takes time quadratic in the
  number of definitions of a file; the port computes each once, with the same result. Likewise
  `findLocationByPath` scans all `SourceCodeInfo` locations for every comment; the port sorts
  them once per file and keeps the first location of each path, with the same result.
