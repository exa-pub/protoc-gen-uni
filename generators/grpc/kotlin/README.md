# grpc/kotlin

C port of `protoc-gen-grpc-kotlin` from [gRPC-Kotlin](https://github.com/grpc/grpc-kotlin)
(Apache-2.0, Copyright gRPC authors): <https://buf.build/grpc/kotlin>. No JVM required. Generated
code needs `io.grpc:grpc-kotlin-stub` 1.5.0 and `kotlinx-coroutines-core`, next to the Java classes
of [grpc/java](../java/README.md) (`<Service>Grpc`) and of the messages
([protocolbuffers/java](../../protocolbuffers/java/README.md); buf.build pairs it with grpc/java
v1.75.0 and protocolbuffers/kotlin v32.0).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/kotlin
    opt: [uni-name=grpc/kotlin, uni-version=v1.5.0]
```

For every file to generate with services it writes `<java package dir>/<OuterClass>GrpcKt.kt`: per
service an object `<Service>GrpcKt` with `SERVICE_NAME`, `serviceDescriptor`, a `<method>Method`
property per method, the client stub `<Service>CoroutineStub` and the server base class
`<Service>CoroutineImplBase`. The outer class name is the plugin's own (`DescriptorUtil.kt`):
`java_outer_classname`, else the file name in camel case, with `OuterClass` appended when a
top-level enum, a message (or a type nested in it) or a service has that name.

## Versions

| version | upstream sources |
|---------|------------------|
| v1.5.0 | [`compiler/src/main/java/io/grpc/kotlin/generator`](https://github.com/grpc/grpc-kotlin/tree/v1.5.0/compiler/src/main/java/io/grpc/kotlin/generator) at tag `v1.5.0`; [KotlinPoet 2.2.0](https://github.com/square/kotlinpoet/tree/2.2.0) ([`pkg/languages/kotlin/formatters/kotlinpoet`](../../../pkg/languages/kotlin/formatters/kotlinpoet/README.md), the differences of 2.2.0 from 2.4.0); protobuf-java 3.25 `Descriptors` ([`pkg/languages/java/protobuf-java`](../../../pkg/languages/java/protobuf-java/protobuf_java.h)); Guava `CaseFormat` (camel case of method names) |

The buf.build plugin runs `protoc-gen-grpc-kotlin-1.5.0-jdk8.jar` from Maven Central, a fat jar
with KotlinPoet 2.2.0, protobuf-java 3.25.x (its `Descriptors` classes are the same in all 3.25
releases) and Guava, on Temurin 25
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/kotlin/v1.5.0)).
Each `.c` file names the upstream files it ports: [`generator.c`](v1.5.0/generator.c)
(`GeneratorRunner.kt`, `ProtoFileCodeGenerator.kt`, `ServiceCodeGenerator.kt` and the four
service generators), [`protoc.c`](v1.5.0/protoc.c) (the `protoc` package: request, descriptors,
names, `DescriptorUtil.kt`, `GeneratorConfig.kt`, response).

**Class: formatted** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream builds KotlinPoet specs
(an object with properties, getters and nested classes, member names `%M` with their imports and
aliases, named code blocks) and writes them with `FileSpec.toString()`; the port builds the same
specs with the KotlinPoet library in its 2.2.0 mode.

The host is the JVM: the request is built like `FileDescriptor.buildFrom` of protobuf-java 3.25
(descriptor validation), strings are decoded like `new String(bytes, UTF_8)`, file paths are
`java.nio.file.Paths` (empty package segments dropped, runs of `/` collapsed). Exceptions of the
generation become the response error with their stack trace (`Throwables.getStackTraceAsString`),
next to `supported_features` (proto3 optional); the parse error of the request ends the JVM.

## Options

None: the parameter is ignored. `supported_features` is proto3 optional; editions are not
declared (buf.build rejects editions inputs for this plugin).

## Known differences

- Error stack traces: the port reproduces the frames of the exceptions thrown at fixed places of
  the plugin (a missing file or dependency, the empty name of a method such as `_`, the empty outer
  class name of a file such as `_.proto`, and the identifiers KotlinPoet 2.2.0 refuses to escape in
  a package or an outer class name). For exceptions thrown inside protobuf-java (descriptor
  validation) the error is the first line of the trace only (`Throwable.toString`).
- `bindService` nests one KotlinPoet code block per method, and `CodeWriter.emitCode` recurses into
  them: upstream overflows the JVM stack (`StackOverflowError`, which ends the JVM) from about 5000
  methods in one service, varying from run to run up to about 6000. The port fails beyond 5000
  nested code blocks with the first line of that error.
- A request that protobuf-java cannot parse (an `UninterpretedOption.NamePart` without its required
  fields) ends the JVM with the `IOException` and its cause on stderr; the port returns the first two
  lines as the response error.

## Unsupported

Each fails with a `CodeGeneratorResponse.error` that names it:

- Characters beyond ASCII where KotlinPoet applies Java's Unicode character classes
  (`Character.isJavaIdentifierStart/Part`, `Char.uppercaseChar`): in `java_package`, the package,
  and `java_outer_classname` (they become escaped Kotlin names and import aliases).
- U+0000 in names and options.
- Two classes of the same simple name whose packages give the same import alias for every number
  of package segments (KotlinPoet loops forever on them).
