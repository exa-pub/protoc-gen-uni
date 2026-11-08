# protoc_plugin 25.1.0, C port

The Dart code generator of [protoc_plugin](https://github.com/google/protobuf.dart/tree/protoc_plugin-v25.1.0/protoc_plugin)
(BSD-3-Clause, Copyright the Dart project authors) as a library: `protoc_plugin_generate`
(`protoc_plugin.h`) is `CodeGenerator(stdin, stdout).generate()`. Used by
`generators/protocolbuffers/dart`; other plugins built on `package:protoc_plugin` (names, options)
reuse it from here.

## Layout

One `.c` file per upstream file, same names and order (`names.c` = `lib/names.dart`,
`message_generator.c` = `lib/src/message_generator.dart`, `service_generator.c` = service, client
and gRPC generators, `mixins.c` = `lib/mixins.dart` + `lib/src/well_known_types.dart`,
`shared.c` = `lib/src/shared.dart` + `lib/src/paths.dart`). The Dart classes are structs
(`internal.h`), the methods keep their names (`MessageGenerator.generateConstants` →
`msg_generateConstants`). `protoc_plugin.c` is the unity build.

Output: where upstream prints a declaration through `IndentingWriter`, the port builds its
dart_style nodes (`pkg/languages/dart/formatters/dart_style`, `nodes.c` helpers) and adds them with
the number of newlines upstream's text has before them (`Out` in `internal.h`,
`indenting_writer.c`). Kythe annotations point at the name tokens; their offsets come from the
linearized text. Descriptors are pkg/pb messages with the schemas of protoc_plugin's generated
code and its registered extensions (`pkg/languages/dart/protobuf`).

Uncaught Dart exceptions (`throw` in upstream) are `dart_throw` (`names.c`): the error text
`Unhandled exception:\n<message>` without the stack trace.
