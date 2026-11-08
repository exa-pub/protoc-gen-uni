# grpc/swift-protobuf

C port of `protoc-gen-grpc-swift-2` from [grpc-swift-protobuf](https://github.com/grpc/grpc-swift-protobuf)
(Apache-2.0): <https://buf.build/grpc/swift-protobuf>, the code generator of gRPC Swift 2. No
Swift toolchain required. For every file to generate it writes `<file>.grpc.swift`: per service
the metadata namespace (`enum <Prefix><Service>` with the method descriptors), the server
protocols (`StreamingServiceProtocol`, `ServiceProtocol`, `SimpleServiceProtocol`) with their
default implementations and the client (`ClientProtocol`, `Client`) with its convenience
methods; a file without services gets `// This file contained no services.`. Generated code uses
the message types of [apple/swift](../../apple/swift/README.md) and needs the `GRPCCore` and
`GRPCProtobuf` libraries of gRPC Swift 2.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/swift
    opt: [uni-name=grpc/swift-protobuf, uni-version=v2.5.0, Visibility=Public, FileNaming=PathToUnderscores]
```

## Versions

| version | upstream sources | libraries |
|---------|------------------|-----------|
| v2.5.0 | [`Sources/protoc-gen-grpc-swift-2`](https://github.com/grpc/grpc-swift-protobuf/tree/2.5.0/Sources/protoc-gen-grpc-swift-2), [`Sources/GRPCProtobufCodeGen`](https://github.com/grpc/grpc-swift-protobuf/tree/2.5.0/Sources/GRPCProtobufCodeGen) at tag `2.5.0` | `GRPCCodeGen` of [grpc-swift-2 2.4.4](https://github.com/grpc/grpc-swift-2/tree/2.4.4/Sources/GRPCCodeGen), `SwiftProtobufPluginLibrary` of [swift-protobuf 1.39.0](https://github.com/apple/swift-protobuf/tree/1.39.0/Sources/SwiftProtobufPluginLibrary) (`Package.resolved` of the buf recipe) |

The buf.build recipe (`plugins/grpc/swift-protobuf/v2.5.0`) builds the `protoc-gen-grpc-swift-2`
product of the tag with Swift 6.4.0. Its registry configuration adds `Visibility=Public` and
`FileNaming=PathToUnderscores` for generated SDKs (not applied by `buf generate`).

The libraries are ported in [`pkg/languages/swift/grpc-codegen`](../../../pkg/languages/swift/grpc-codegen)
(`GRPCCodeGen`: the StructuredSwift representation and its `TextBasedRenderer`) and
[`pkg/languages/swift/swift-protobuf`](../../../pkg/languages/swift/swift-protobuf); the Swift
String behavior that shows in the output (grapheme clusters) is in
[`pkg/languages/swift/std`](../../../pkg/languages/swift/std). `GRPCProtobufCodeGen`, used only by
this plugin, is in the version directory (`protobuf_code_gen.c`). Each `.c` file names the upstream
file it ports.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream builds a StructuredSwift tree
and renders it with its own `TextBasedRenderer` (four-space indent, no line breaking) and runs no
formatter: the port builds the same tree and renders the same text.

## Options

All options of upstream, with the same parsing (keys and values are case-sensitive, booleans are
`true` or `false` in any ASCII case):

- `Visibility=Internal|Public|Package` (default `Internal`);
- `Server=<bool>`, `Client=<bool>` (default `true`);
- `FileNaming=FullPath|PathToUnderscores|DropPath` (default `FullPath`);
- `ExtraModuleImports=<module>`, repeatable (sorted);
- `GRPCModuleName=<name>` (default `GRPCCore`), `GRPCProtobufModuleName=<name>` (default
  `GRPCProtobuf`), `SwiftProtobufModuleName=<name>` (default `SwiftProtobuf`, imported when an
  RPC uses a well-known type);
- `Availability=<os> <version>`, repeatable: replaces the default
  `@available(macOS 15.0, iOS 18.0, watchOS 11.0, tvOS 18.0, visionOS 2.0, *)`;
- `UseAccessLevelOnImports=<bool>` (default `false`);
- `ReflectionData`: always fails (`Unsupported parameter 'ReflectionData': ...`), as upstream;
- `ProtoPathModuleMappings=<file>`, see below.

Errors are upstream's: `Unknown generation parameter 'foo'`,
`Unknown value for generation parameter 'Visibility': 'public'`. `supported_features` are proto3
optional and editions, from proto2 to edition 2026.

## Known differences

- `ProtoPathModuleMappings=<file>` is not read: it always fails with
  `Parameter 'ProtoPathModuleMappings=<file>': failToOpen(path: "<file>")`, like the remote plugin
  on buf.build, which cannot see local files.
- Upstream's errors for duplicate service or method names print a runtime address that differs
  per run: `Code(value: GRPCCodeGen.CodeGenError.Code.(unknown context at $55eb92e088b8).Value.nonUniqueMethodName): "..."`.
  The port prints `$0` (the input `services-case` is left out of the golden tests for this).
- Errors where upstream crashes (unknown types or files, a file name made only of `.`, which
  traps with `Fatal error: Index out of range`) go to `CodeGeneratorResponse.error`. A request
  that SwiftProtobuf cannot decode (invalid UTF-8 in a string field, a missing required field)
  fails with `protoc-gen-grpc-swift-2: Received an unparsable request from the compiler: <error>`
  on stderr and exit status 1; upstream prints the same and exits with status 0 without a
  response.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the input.

- The names listed for [apple/swift](../../apple/swift/README.md#unsupported) (non-ASCII or NUL
  in file names, packages, type names, ...): protoc and buf never produce them.
- Characters whose grapheme cluster breaking is not ported (see
  [`pkg/languages/swift/std`](../../../pkg/languages/swift/std/swift_std.h)) where upstream works
  on Swift Characters: at the start of a comment line of a service or method (upstream drops the
  first four Characters of `///<comment>`), for example a combining mark, a Devanagari letter or
  a Hangul jamo right after `//`, and in an `Availability` value. Latin, Greek, Cyrillic, CJK,
  kana, Hangul syllables, common symbols and emoji are supported.
- A NUL byte in a module name option or in `Availability`; a non-ASCII character in
  `ExtraModuleImports` when there are several (upstream sorts them by Swift string order, which
  is canonical equivalence); a non-ASCII or NUL `ProtoPathModuleMappings` path (its error text
  quotes the path as Swift does).
