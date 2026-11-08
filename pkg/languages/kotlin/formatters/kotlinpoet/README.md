# KotlinPoet 2.4.0 and 2.2.0, C port

The Kotlin code builder of the JVM plugins that emit Kotlin: connectrpc/kotlin (2.4.0) and
grpc/kotlin (2.2.0, the differences selected with `kp_select_version`). Upstream:
<https://github.com/square/kotlinpoet/tree/2.4.0/kotlinpoet/src> and
<https://github.com/square/kotlinpoet/tree/2.2.0/kotlinpoet/src> (Apache-2.0, Copyright 2015
Square, Inc.).

## Layout

Formatter library (docs/LAYOUT.md): KotlinPoet has no parser; generators build its specs with the
builders of `kotlinpoet.h` and `kp_file_to_string` writes them like `FileSpec.toString()`.

- `code_block.c`: `CodeBlock.kt` (`CodeBlock.Builder.add` and `addNamed` with their format
  errors, `addStatement`, `trim`, `withoutPrefix`, `joinToCode`) and `trimTrailingNewLine` of
  `Util.jvm.kt`.
- `code_writer.c`: `CodeWriter.kt` (`emitCode`, statements `«»`, indentation `⇥⇤`, KDoc and
  comments, `lookupName` of classes and members, the static import of a member after `%T`,
  `withCollectedImports` with import aliases of classes and members) and `LineWrapper.kt` (soft
  wrapping at `♢`, `·` as a space, 100 columns).
- `specs.c`: `TypeName.kt`, `ClassName.kt`, `ParameterizedTypeName.kt`, `LambdaTypeName.kt`,
  `TypeVariableName.kt` (a name only), `MemberName.kt`, `AnnotationSpec.kt`, `ParameterSpec.kt`,
  `PropertySpec.kt` (getters), `FunSpec.kt` (expression bodies, getters), `TypeSpec.kt` (classes,
  objects and interfaces with a primary constructor and its annotations, constructor properties,
  a superclass with constructor parameters, supertypes, nested types), `FileSpec.kt`.
- `util.c`: `Util.kt` (escaping of names and import aliases, string literals, keywords).

The specs carry what the generators set: KDoc, annotations, modifiers, parameters with default
values, a return type and a body for functions; annotations, an initializer and a getter for
properties; kind, KDoc, annotations, modifiers, primary constructor, superclass, superinterfaces,
properties, functions and nested types for types; comment, annotations, member imports and types
for files.

## KotlinPoet 2.2.0

`kp_select_version(KP_V2_2_0)` selects the behaviour of 2.2.0 where it differs from 2.4.0 for
these specs (the 2.3.0 and 2.4.0 changelogs, checked against the sources):

- `failIfEscapeInvalid`: an escaped identifier (a `%N` name, a segment of a package or class
  name, an import alias) with one of `.;[]/<>:\` is an `IllegalArgumentException` (2.4.0 accepts
  identifiers in backticks).
- KDoc does not escape `/*` and `*/`; a wrapped comment line does not repeat `// `.
- An expression body is the body trimmed without keeping it balanced (no check of unmatched
  statement ends).
- A deferred `%T` (static import logic) emits its `?` before the type; members referenced in KDoc
  are not imported.

The other changes of 2.3.0 and 2.4.0 (`get`/`set` operator names, annotation arrays, context
parameters after annotations, explicit backing fields, type aliases in types, `addComment`) do not
concern the specs of the ported generators.

## Limits

`CodeWriter.emitCode` recurses into nested code blocks; beyond 5000 levels (`KP_MAX_CODE_DEPTH`,
about where the JVM stack of the buf.build plugins overflows) the port throws
`java.lang.StackOverflowError`, which also bounds the C stack.

## Not ported

Extension members and operators of `%M`, `%P`, numbers as `%L` arguments, type variable
declarations and bounds, context receivers and parameters, enums, anonymous classes, type aliases,
setters, secondary constructors, initializer blocks, scripts. Java's Unicode character classes
beyond ASCII (`Character.isJavaIdentifierStart/Part`, case mapping of `Char`) fail with an
"unsupported" error.

## Tests

`tests/kotlinpoet_test.c` builds specs and checks the output of KotlinPoet 2.4.0 for them (from
protoc-gen-connect-kotlin v0.10.0, which builds those specs) and of KotlinPoet 2.2.0 (from
protoc-gen-grpc-kotlin v1.5.0). The golden input
`testdata/proto/kotlin-long` covers long names (KotlinPoet 2 does not wrap at spaces) and comments
with `♢` wrap points and `·` spaces; `testdata/proto/kotlin-grpc-names` (grpc/kotlin) covers
member imports that clash with function names, import aliases of classes and members, and
keywords in packages.
