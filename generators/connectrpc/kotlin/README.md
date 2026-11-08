# connectrpc/kotlin

C port of `protoc-gen-connect-kotlin` from [connect-kotlin](https://github.com/connectrpc/connect-kotlin)
(Apache-2.0, Copyright The Connect Authors): <https://buf.build/connectrpc/kotlin>. No JVM
required. For every service of the files to generate it writes `<java package dir>/<Service>ClientInterface.kt`
(an interface with the client methods) and `<java package dir>/<Service>Client.kt` (its
implementation over `com.connectrpc.ProtocolClientInterface`). Generated code needs the
`com.connectrpc:connect-kotlin` runtime and the Java or Kotlin messages of the protos.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/kotlin
    opt: [uni-name=connectrpc/kotlin, uni-version=v0.10.0, generateCallbackMethods=true]
```

## Versions

| version | upstream sources |
|---------|------------------|
| v0.10.0 | [`protoc-gen-connect-kotlin/src/main/kotlin`](https://github.com/connectrpc/connect-kotlin/tree/v0.10.0/protoc-gen-connect-kotlin/src/main/kotlin/com/connectrpc/protocgen/connect) at tag `v0.10.0`; [KotlinPoet 2.4.0](https://github.com/square/kotlinpoet/tree/2.4.0) ([`pkg/languages/kotlin/formatters/kotlinpoet`](../../../pkg/languages/kotlin/formatters/kotlinpoet/README.md)); protobuf-java 4.36.2 (`Descriptors`, `GeneratorNames`: [`pkg/languages/java/protobuf-java`](../../../pkg/languages/java/protobuf-java/protobuf_java.h)) |

The buf.build plugin runs the shadow jar of the plugin (versions from `gradle/libs.versions.toml`:
KotlinPoet 2.4.0, protobuf-java 4.36.2, Kotlin 2.4.21) on Temurin 25. Each `.c` file names the
upstream file it ports: [`plugin.c`](v0.10.0/plugin.c) (`Main.kt`, `internal/Plugin.kt`,
`internal/CodeGenerator.kt`), [`parameters.c`](v0.10.0/parameters.c) (`internal/Parameters.kt`,
`internal/ProtoHelpers.kt`, `internal/SourceInfo.kt`), [`generator.c`](v0.10.0/generator.c)
(`Generator.kt`).

**Class: formatted** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream builds KotlinPoet specs
(`FileSpec`, `TypeSpec`, `FunSpec`, `CodeBlock`, ...) and writes them with `FileSpec.toString()`;
the port builds the same specs with the KotlinPoet library, which emits them like KotlinPoet 2.4.0
(import collection with aliases, `%N` escaping of keywords, statements, KDoc, wrapping at the `♢`
wrap points of comments at 100 columns).

The host is the JVM: the request is parsed and built like `CodeGeneratorRequest.parseFrom` and
`Descriptors.FileDescriptor.buildFrom` of protobuf-java 4.36.2 (descriptor validation, feature
resolution of editions up to 2026), strings are decoded like `new String(bytes, UTF_8)` (malformed
UTF-8 becomes U+FFFD), Java class names come from `GeneratorNames` (`java_package`,
`java_outer_classname`, `java_multiple_files`, the `pb.java` features `nest_in_file_class` and
`use_old_outer_classname_default`), and the plugin's exceptions end the JVM with a stack trace,
which buf reports as the error: the port returns that text as `CodeGeneratorResponse.error`. Like
upstream, `ExtensionRegistry.add` fails on group (and delimited) extensions and on message
extensions whose type has required fields (`DynamicMessage.build`).

## Options

Same parsing as upstream (`Parameters.kt`): the parameter is split at `,`, each part at its first
`=`; keys are camel-cased (`generate_callback_methods` is `generateCallbackMethods`; characters
other than ASCII letters and digits are dropped), values are Kotlin `toBoolean()` (`true` in any
case; anything else is false), the last value of a key wins and unknown keys are ignored.

- `generateCoroutineMethods` (default true): `suspend` unary methods returning `ResponseMessage`.
- `generateCallbackMethods` (default false): unary methods with an `onResult` callback returning
  `Cancelable`.
- `generateBlockingUnaryMethods` (default false): `<method>Blocking` unary methods returning
  `UnaryBlockingCall`.

`supported_features` are proto3 optional and editions, from proto2 to edition 2026.

## Unsupported

Each fails with a `CodeGeneratorResponse.error` that names it:

- Characters beyond ASCII where KotlinPoet applies Java's Unicode character classes
  (`Character.isJavaIdentifierStart/Part`, `Char.uppercaseChar`): in `java_package` and
  `java_outer_classname` (they become escaped Kotlin names and import aliases).
- U+0000 in names, options and comments.
- Two classes of the same simple name whose packages give the same import alias for every number
  of package segments (KotlinPoet loops forever on them).

## Known differences

- Errors: where upstream reports an exception thrown inside protobuf-java or KotlinPoet (a
  `DescriptorValidationException`, an `IllegalArgumentException` of a `CodeBlock` format, the
  `Throwable: failure on generating <file>` around an exception of `FileSpec.toString`), the error
  is `Exception in thread "main" ` and the first line of the stack trace; the frames follow only
  for exceptions the plugin throws itself (missing dependencies, unknown files to generate, the
  `ExtensionRegistry.add` and `DynamicMessage.build` failures). The line of the frame of a missing
  dependency (`Plugin.kt:189`) is inferred from the source.
- The extensions are registered in the order of a Java `HashMap` of the file names
  (`String.hashCode`, resizes included), which decides which failing extension is reported first;
  tree bins (eight or more file names in one bucket of a table of 64 or more) are not reproduced.
