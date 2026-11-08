# protocolbuffers/kotlin

C port of the Kotlin generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/kotlin>. No protoc required. The generated
Kotlin DSL builds on the Java classes of [protocolbuffers/java](../java/README.md) at the same
version (generate both) and needs `com.google.protobuf:protobuf-kotlin` 4.36.2, or
`protobuf-kotlin-lite` 4.36.2 with the `lite` option.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/java
    opt: [uni-name=protocolbuffers/java, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/java
    opt: [uni-name=protocolbuffers/kotlin, uni-version=v36.2]
```

See the [Kotlin generated code guide](https://protobuf.dev/reference/kotlin/kotlin-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The Kotlin generator of protoc prints
Kotlin text with `io::Printer` and runs no formatter: the port prints the same text. Doc comments
are the KDoc variants of the Java generator's (`FieldDescriptor::DebugString()` first lines).

## Options

As upstream (`compiler/kotlin/generator.cc`); unknown options fail with `Unknown generator option: K`.

| option | effect |
|--------|--------|
| `lite` | DSL for the Java Lite runtime (`protobuf-kotlin-lite`) |
| `no_jvm_dsl` | with `lite`: no `@JvmName`/`@JvmSynthetic` annotations and no JVM-only extension list members (Kotlin Native); `OrNull` getters on `Foo` and `Foo.Builder` instead of `FooOrBuilder`. Without `lite` it only drops the `@JvmSynthetic` of the field members (as upstream) |
| `annotate_code` | appends `// google.protobuf.GeneratedCodeInfo: <base64>` (the `io::Printer` annotations) to every file |
| `output_list_file=F` | also writes `F` with the generated `.kt` files, one per line |
| `experimental_strip_nonfunctional_codegen` | upstream test mode: no comments |
| `immutable`, `shared` | accepted, no effect (always set) |
| `annotation_list_file=F` | accepted, no effect (as upstream) |
| `mutable` | fails with `Mutable not supported by Kotlin generator` |

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/kotlin`](https://github.com/protocolbuffers/protobuf/tree/v36.2/src/google/protobuf/compiler/kotlin) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

The layout follows upstream: `generator.c` (`generator.cc` and the plugin main of
bufbuild/plugins), `file.c`, `message.c` and `field.c` (one `.c` per upstream `.cc`). The shared
core of `compiler/java` (names, name resolver, helpers incl. `JvmName`, context, doc comments) is
[`pkg/languages/cpp/libprotoc/compiler/java`](../../../pkg/languages/cpp/libprotoc/compiler/java/libprotoc_java.h),
shared with protocolbuffers/java. The C++ classes are C structs, methods are functions named
`kt_<class>_<method>`.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr and exits with
  status 1. The port puts the same text (without the log prefixes) in
  `CodeGeneratorResponse.error`.
- Failed internal checks (`ABSL_CHECK`, e.g. `pb.java.nest_in_file_class` on a nested type of a
  dependency) abort upstream without a response; the port returns
  `protoc-gen-kotlin: Check failed: ...` in `CodeGeneratorResponse.error`.
- Comment lines are printed up to a NUL byte in the comment (upstream prints the NUL byte).

## Unsupported

Nothing: every option and input upstream accepts is ported.
