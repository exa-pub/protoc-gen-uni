# grpc/swift

C port of `protoc-gen-grpc-swift` from [grpc-swift](https://github.com/grpc/grpc-swift) 1.x
(Apache-2.0): <https://buf.build/grpc/swift>. No Swift toolchain required. For every file to
generate that has services it writes `<file>.grpc.swift` with, per service, the NIO and
async/await clients and server providers of gRPC Swift 1.x; with `ReflectionData=true` it also
writes `<file>.grpc.reflection` for every file to generate (the base64 serialized
`FileDescriptorProto`, for the reflection service). Generated code uses the message types of
[apple/swift](../../apple/swift/README.md) and needs the `GRPC` library of grpc-swift 1.x.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/swift
    opt: [uni-name=grpc/swift, uni-version=v1.27.6, Visibility=Public, FileNaming=PathToUnderscores]
```

## Versions

| version | upstream sources | plugin library |
|---------|------------------|----------------|
| v1.27.6 | [`Sources/protoc-gen-grpc-swift`](https://github.com/grpc/grpc-swift/tree/1.27.6/Sources/protoc-gen-grpc-swift) at tag `1.27.6` | `SwiftProtobufPluginLibrary` of [swift-protobuf 1.38.1](https://github.com/apple/swift-protobuf/tree/1.38.1/Sources/SwiftProtobufPluginLibrary) |

The buf.build recipe (`plugins/grpc/swift/v1.27.6`) clones the tag and builds the
`protoc-gen-grpc-swift` product with Swift 6.3.3. The package has no `Package.resolved`, so
swift-protobuf is the newest release matching `from: "1.31.0"` when the image was built; the
recipe declares `buf.build/apple/swift:v1.38.1` as its dependency, and the output agrees (with
1.39.0 it would be the same: the plugin libraries differ only in their edition defaults, which
show only for files with an edition after 2024, rejected before the plugin runs). Its registry
configuration adds `Visibility=Public` and `FileNaming=PathToUnderscores` for generated SDKs (not
applied by `buf generate`).

The plugin library is the port of `SwiftProtobufPluginLibrary` in
[`pkg/languages/swift/swift-protobuf`](../../../pkg/languages/swift/swift-protobuf) (1.39.0)
with the bundled edition defaults of 1.38.1 (`sw_edition_defaults_1_38_1`). The reflection data
is `FileDescriptorProto.serializedData()` of the file as received, which SwiftProtobuf writes
like [`pkg/pb`](../../../pkg/pb/pb.h) does (known fields in field number order, packed as in
`descriptor.proto`, then the unknown fields, custom options among them, in wire order), encoded
with Foundation's `Data.base64EncodedString()` (standard alphabet, padded).

Each `.c` file names the upstream files it ports: `GenerateGRPC.swift` is in `generator.c`,
`Options.swift` in `options.c`, `Generator.swift`, `Generator-Names.swift`,
`Generator-Metadata.swift`, `Types.swift` and `StreamingType.swift` in `grpc_generator.c`, the
client and server generators in `client.c` and `server.c`.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream prints Swift text with
`CodePrinter` (two-space indent) and runs no formatter: the port prints the same text.

## Options

All options of upstream, with the same parsing (keys and values are trimmed of whitespace and
case-sensitive, booleans are `true` or `false`):

- `Visibility=Internal|Public|Package` (default `Internal`);
- `Server=<bool>`, `Client=<bool>` (default `true`), `TestClient=<bool>` (default `false`);
- `KeepMethodCasing=<bool>` (default `false`);
- `FileNaming=FullPath|PathToUnderscores|DropPath` (default `FullPath`);
- `ExtraModuleImports=<module>` (repeatable), `GRPCModuleName=<name>` (default `GRPC`),
  `SwiftProtobufModuleName=<name>` (default `SwiftProtobuf`; only the `import`): not empty;
- `ReflectionData=<bool>` (default `false`);
- `UseAccessLevelOnImports=<bool>`: parsed, no effect (as upstream);
- `_V2`: `false` is ignored, anything else fails with `unsupportedV2`;
- `ProtoPathModuleMappings=<file>`, see below (empty: ignored).

Errors are upstream's, as Swift prints the thrown error: `unknownParameter(name: "foo")`,
`invalidParameterValue(name: "Visibility", value: "public")`, `unsupportedV2`.
`supported_features` are proto3 optional and editions, from proto2 to edition 2023.

## Known differences

- `ProtoPathModuleMappings=<file>` is not read: it always fails with
  `wrappedError(message: "Parameter \'ProtoPathModuleMappings=<file>\'", error: SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: "<file>"))`,
  like the remote plugin on buf.build, which cannot see local files. A path that exists in the
  remote plugin's image (for example `/etc/passwd`) fails there with a text format error instead.
- Errors where upstream crashes (unknown types or files, out of range indexes, a file with an
  edition after 2024) go to `CodeGeneratorResponse.error`. A request that SwiftProtobuf cannot
  decode (invalid UTF-8 in a string field, a missing required field) fails with
  `protoc-gen-grpc-swift: Received an unparsable request from the compiler: <error>` on stderr
  and exit status 1; upstream prints the same and exits with status 0 without a response.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the input. protoc and buf never produce such names.

- The names listed for [apple/swift](../../apple/swift/README.md#unsupported) (non-ASCII or NUL
  in file names, packages, type names, ...).
- A non-ASCII character in an option key or value that goes to an error text: upstream quotes it
  with `String.debugDescription`, which escapes some non-ASCII characters by grapheme break rules.
- A non-ASCII character in `GRPCModuleName`, `SwiftProtobufModuleName` or `ExtraModuleImports`
  for a file with services: upstream sorts the imports by Swift string order (Unicode canonical
  equivalence).
