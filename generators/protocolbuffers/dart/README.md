# protocolbuffers/dart

C port of `protoc-gen-dart` from [protoc_plugin](https://github.com/google/protobuf.dart/tree/master/protoc_plugin)
(BSD-3-Clause, Copyright the Dart project authors): <https://buf.build/protocolbuffers/dart>. No
Dart SDK required. Generated code needs `package:protobuf` (and `package:grpc` for the `grpc`
option) of the matching version.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/dart
    opt: [uni-name=protocolbuffers/dart, uni-version=v25.1.0, grpc]
```

Options are the same as upstream, none takes a value:
- `grpc`: gRPC client and service classes in `.pbgrpc.dart` instead of the `.pbserver.dart` and
  client API classes of `package:protobuf`'s RPC;
- `generate_kythe_info`: `.pb.dart.meta` and `.pbenum.dart.meta` files (GeneratedCodeInfo as JSON);
  `.pb.dart` and `.pbenum.dart` are then not formatted;
- `disable_constructor_args`: message factories without named field arguments.

Errors use upstream's texts (`Error found trying to parse the option: ...`, `Unknown option (...).`).

Class: **formatted**. Upstream prints Dart with an `IndentingWriter` and formats every file (except
the two unformatted files of `generate_kythe_info`) with dart_style 3.1.13 in the short style
(every file starts with `// @dart = 3.3`). The port builds the dart_style nodes of everything it
prints ([`pkg/languages/dart/formatters/dart_style`](../../../pkg/languages/dart/formatters/dart_style)),
linearizes them into the text upstream prints and formats that tree.

## Versions

| version | upstream sources |
|---------|------------------|
| v25.1.0 | [`protoc_plugin/lib`](https://github.com/google/protobuf.dart/tree/protoc_plugin-v25.1.0/protoc_plugin/lib) (in [`pkg/languages/dart/protoc_plugin`](../../../pkg/languages/dart/protoc_plugin)), the parts of `package:protobuf` it uses (in [`pkg/languages/dart/protobuf`](../../../pkg/languages/dart/protobuf)), `package:path` ([`pkg/languages/dart/path`](../../../pkg/languages/dart/path)), dart:core/dart:convert behavior ([`pkg/languages/dart/std`](../../../pkg/languages/dart/std)), dart_style 3.1.13 (the remote plugin is built with Dart 3.13.3 and an unlocked `dart_style: ^3.0.0`, see [bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/dart/v25.1.0)) |

Each `.c` file names the upstream file it ports.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that names them:

- Malformed UTF-8 in a string field of the request (`unsupported by protoc-gen-uni: invalid UTF-8
  ...`): the Dart runtime decodes it with replacement characters. protoc and buf never send it.

## Known differences

- Where upstream throws an uncaught exception (unknown types, invalid `dart_name` or mixin
  options, invalid default values, editions outside PROTO2..2024, an absolute file name, ...), the
  Dart program prints `Unhandled exception:`, the message and a stack trace to stderr and exits
  with status 255. The port returns `Unhandled exception:\n<message>` as
  `CodeGeneratorResponse.error`, without the stack trace.
- A message, enum, enum value or field name made only of underscores makes upstream loop forever
  (`avoidInitialUnderscore`); the port fails with an error instead.
- `double`/`float` values in descriptor JSON constants (only `UninterpretedOption.double_value`,
  which protoc never sends) follow Dart's `double.toString` and `toInt` (clamped to 64 bits); the
  clamping of huge integral values was not checked against the Dart VM.
- File names with characters outside `A-Za-z0-9-._~!$&'()*+,;=:@/` are percent-encoded like
  `Uri.file` does; Dart's `Uri.parse` normalization of such paths in imports was not checked.
- Upstream's `--stderr` warning `Too many source code locations. Skipping.` (several locations
  with the same path) is not printed; the comment is left out like upstream does.
