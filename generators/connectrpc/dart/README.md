# connectrpc/dart

C port of `protoc-gen-connect-dart` from [Connect-Dart](https://github.com/connectrpc/connect-dart)
(Apache-2.0, Copyright The Connect Authors): <https://buf.build/connectrpc/dart>. No Dart SDK
required. For every file to generate that has services it writes `<dir>/<name>.connect.spec.dart`
(an `abstract final class` per service with the `connect.Spec` of each method) and
`<dir>/<name>.connect.client.dart` (an `extension type <Service>Client` per service). Generated
code needs `package:connectrpc` and the messages of [protocolbuffers/dart](../../protocolbuffers/dart/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/dart
    opt: [uni-name=connectrpc/dart, uni-version=v2.0.1]
```

## Versions

| version | upstream sources |
|---------|------------------|
| v2.0.1 | [`packages/connect/bin`](https://github.com/connectrpc/connect-dart/tree/v2.0.1/packages/connect/bin) at tag `v2.0.1` (8e4e5a62), with its vendored `descriptor.pb.dart` and `plugin.pb.dart`; `package:protobuf` 6.1.0 ([`pkg/languages/dart/protobuf`](../../../pkg/languages/dart/protobuf)), `package:path` 1.9.1 ([`pkg/languages/dart/path`](../../../pkg/languages/dart/path)), dart:core and dart:convert ([`pkg/languages/dart/std`](../../../pkg/languages/dart/std)) |

The buf.build recipe (`plugins/connectrpc/dart/v2.0.1`) compiles `bin/protoc-gen-connect-dart.dart`
with `dart compile exe` in the `dart:3.13.5-sdk` image after an unlocked `dart pub get` (the
repository has no `pubspec.lock`; `protobuf: ^6.0.0` and `path: ^1.9.0` resolve to 6.1.0 and 1.9.1).
Each `.c` file names the upstream file it ports.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream prints Dart text line by line
and runs no formatter: the port prints the same text.

The behaviors of the Dart runtime that show in the output are ported with it:

- the request is parsed like `CodeGeneratorRequest.fromBuffer` (recursion limit 100; 32-bit fields
  from the low 32 bits of a varint; a varint field with a length-delimited tag reads its value at
  the length), and its exceptions (`InvalidProtocolBufferException: ...`, `RangeError ...`,
  `Invalid argument(s): ...`) become `CodeGeneratorResponse.error` as upstream catches them;
- strings are decoded like `Utf8Decoder(allowMalformed: true)`: malformed UTF-8 becomes U+FFFD and a
  leading byte order mark is dropped;
- documentation comments come from the first source location of the service or method path,
  whose index is `List.indexOf` with `GeneratedMessage ==` (equal services or methods share the
  comments of the first);
- the first character of a method name is lowercased like `String.toLowerCase` of the Dart VM
  (one UTF-16 code unit, simple case mappings);
- import paths use the posix `Context` of `package:path` (`normalize`, `relative`, including its
  `PathException`).

## Options

Same parsing and errors as upstream (`parameter.split(",")`, each `key=value` split at every `=`, the
value is the last part):

- `keep_empty_files` (`keep_empty_files=true|false`, an empty value is true): also files without
  services, which get the header and no declarations. Any other value fails with
  `invalid plugin option 'keep_empty_files' must be a bool, got <value>` (`bool.tryParse`,
  case-sensitive).
- Any other option fails with `Unsupported plugin option <option>`.

`supported_features` are proto3 optional and editions, from proto2 to edition 2024.

## Unsupported

Nothing: every input upstream accepts gives the same output.

## Known differences

- A request that `pkg/pb` cannot parse (malformed wire format, a varint field with a
  length-delimited tag, a tag or length varint above 32 bits, ...) fails in protoc-gen-uni before
  the generator runs (`protoc-gen-uni: parse request: ...`, exit status 1): the parameter has to be
  read to select the generator. Upstream parses it with its own rules and reports most such requests
  as `CodeGeneratorResponse.error` (`InvalidProtocolBufferException: ...`). Requests nested deeper
  than `pkg/pb` takes reach the port, which parses them like upstream.
- When generation fails, upstream also returns the files generated before the error (protoc and buf
  ignore them); the port returns only the error.
