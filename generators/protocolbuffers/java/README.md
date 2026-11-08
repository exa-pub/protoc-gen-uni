# protocolbuffers/java

C port of the Java generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/java>. No protoc required. Generated code needs
`com.google.protobuf:protobuf-java` 4.36.2, or `protobuf-javalite` 4.36.2 with the `lite` option.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/java
    opt: [uni-name=protocolbuffers/java, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/java-lite
    opt: [uni-name=protocolbuffers/java, uni-version=v36.2, lite]
```

See the [Java generated code guide](https://protobuf.dev/reference/java/java-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The Java generator of protoc prints Java
text with `io::Printer` and runs no formatter: the port prints the same text. The full runtime
output embeds the serialized `FileDescriptorProto` (retention stripping and the C++ wire format of
libprotoc decide the bytes); field comments are the first line of `FieldDescriptor::DebugString()`.

## Options

As upstream (`compiler/java/generator.cc`); unknown options fail with `Unknown generator option: K`.

| option | effect |
|--------|--------|
| `lite` | Java Lite runtime code (`java/lite`) instead of the full runtime (`java/full`) |
| `immutable`, `shared` | the default immutable API; `shared` alone generates nothing |
| `output_list_file=F` | also writes `F` with the generated `.java` files, one per line |
| `annotation_list_file=F` | also writes `F` (empty: `annotate_code` is not supported) |
| `experimental_strip_nonfunctional_codegen` | upstream test mode: no comments, no descriptor data |
| `bootstrap` | accepted, no effect (as upstream) |
| `mutable` | not in the open source release: upstream aborts (`ABSL_CHECK`); the port fails with the check text. With `lite`: `lite runtime generator option cannot be used with mutable API.` |

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/java`](https://github.com/protocolbuffers/protobuf/tree/v36.2/src/google/protobuf/compiler/java) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

The layout follows upstream: `generator.c` (`generator.cc`), `file.c`, `shared_code_generator.c`,
`generator_factory.c` (the factories of `full/` and `lite/` and the generator constructors),
`full/` and `lite/` (one `.c` per upstream `.cc`). The shared core of `compiler/java` that the
Kotlin and gRPC Java generators also use (names, name resolver, helpers, context, doc comments,
field variables, message serialization) is
[`pkg/languages/cpp/libprotoc/compiler/java`](../../../pkg/languages/cpp/libprotoc/compiler/java/libprotoc_java.h).
The C++ classes are C structs: a field generator is a `JvFieldGen` with the vtable of its class,
methods are functions named `<Class>_<Method>`. Each `.c` file names the upstream file it ports.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr and exits with
  status 1. The port puts the same text (without the log prefixes) in
  `CodeGeneratorResponse.error`.
- Failed internal checks (`ABSL_CHECK`, e.g. `pb.java.nest_in_file_class` on a nested type of a
  dependency, or the `mutable` option) abort upstream without a response; the port returns
  `protoc-gen-java: Check failed: ...` in `CodeGeneratorResponse.error`.
- Warnings that upstream only logs to stderr (conflicting field names, a file class name equal to a
  type name ignoring case, `optimize_for = LITE_RUNTIME` without `lite`) are not printed.
- Comment lines are printed up to a NUL byte in the comment (upstream prints the NUL byte).

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element.

- `annotate_code`: the annotations of `io::Printer` (`GeneratedCodeInfo` in `.java.pb.meta`
  files) are not ported.
- The `pb.java.large_enum` feature (edition 2024): `GenerateLarge` of `internal_helpers.cc` (the
  `Printer::Emit` code of enums split into interfaces) is not ported.
