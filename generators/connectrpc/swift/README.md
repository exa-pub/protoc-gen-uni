# connectrpc/swift

C port of `protoc-gen-connect-swift` from
[Connect-Swift](https://github.com/connectrpc/connect-swift) (Apache-2.0):
<https://buf.build/connectrpc/swift>. No Swift toolchain required. For every file to generate
that has services it writes `<file>.connect.swift` with, per service, a client protocol
`<Prefix><Service>ClientInterface` and its implementation `<Prefix><Service>Client` on
`Connect.ProtocolClientInterface`. Generated code uses the message types of
[apple/swift](../../apple/swift/README.md) and needs the `Connect` library of Connect-Swift.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/swift
    opt: [uni-name=connectrpc/swift, uni-version=v1.2.3, Visibility=Public, FileNaming=PathToUnderscores]
```

## Versions

| version | upstream sources | plugin library |
|---------|------------------|----------------|
| v1.2.3 | [`Plugins/ConnectSwiftPlugin`](https://github.com/connectrpc/connect-swift/tree/1.2.3/Plugins/ConnectSwiftPlugin), [`Plugins/ConnectPluginUtilities`](https://github.com/connectrpc/connect-swift/tree/1.2.3/Plugins/ConnectPluginUtilities) at tag `1.2.3` | `SwiftProtobufPluginLibrary` of [swift-protobuf 1.37.0](https://github.com/apple/swift-protobuf/tree/1.37.0/Sources/SwiftProtobufPluginLibrary) (`Package.resolved`) |

The buf.build recipe (`plugins/connectrpc/swift/v1.2.3`) builds the `protoc-gen-connect-swift`
product of the tag with Swift 6.3.2. Its registry configuration adds `Visibility=Public` and
`FileNaming=PathToUnderscores` for generated SDKs (not applied by `buf generate`).

`ConnectPluginUtilities`, the library shared with [connectrpc/swift-mocks](../swift-mocks/README.md),
is ported in [`pkg/languages/swift/connect-plugin-utilities`](../../../pkg/languages/swift/connect-plugin-utilities)
and builds on the port of `SwiftProtobufPluginLibrary`,
[`pkg/languages/swift/swift-protobuf`](../../../pkg/languages/swift/swift-protobuf), which
implements 1.39.0; 1.37.0 differs only in its bundled edition defaults (editions up to 2024),
selected with `sw_edition_defaults_1_37_0`.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream prints Swift text with
`CodePrinter` (four-space indent) and runs no formatter: the port prints the same text.

## Options

All options of upstream, with the same parsing (keys and values are case-sensitive, booleans are
`true` or `false`):

- `Visibility=Internal|Public|Package` (default `Internal`);
- `FileNaming=FullPath|PathToUnderscores|DropPath` (default `FullPath`);
- `GenerateAsyncMethods=<bool>` (default `true`), `GenerateCallbackMethods=<bool>` (default
  `false`); at least one must be `true`;
- `GenerateServiceMetadata=<bool>` (default `true`): the `Metadata.Methods` method specs;
- `KeepMethodCasing=<bool>` (default `false`);
- `SwiftProtobufModuleName=<name>` (default `SwiftProtobuf`): only the `import` (type names keep
  the `SwiftProtobuf.` prefix, as upstream);
- `ExtraModuleImports=<module>`, repeatable;
- `ProtoPathModuleMappings=<file>`, see below.

Errors are upstream's, as Swift prints the thrown error: `unknownParameter(string: "foo")`,
`invalidParameterValue(key: "Visibility", value: "public")`,
`GeneratorError(message: "Either async methods or callback methods must be enabled")`.
`supported_features` are proto3 optional and editions, from proto2 to edition 2023.

## Known differences

- `ProtoPathModuleMappings=<file>` is not read: it always fails with
  `deserializationError(key: "ProtoPathModuleMappings", error: SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: "<file>"))`,
  like the remote plugin on buf.build, which cannot see local files. A path that exists in the
  remote plugin's image (for example `/etc/passwd`) fails there with a text format error instead
  (`... error: SwiftProtobuf.TextFormatDecodingError.unknownField)`).
- Errors where upstream crashes (unknown types or files, out of range indexes, a file with an
  edition after 2024) go to `CodeGeneratorResponse.error`. A request that SwiftProtobuf cannot
  decode (invalid UTF-8 in a string field, a missing required field) fails with
  `protoc-gen-connect-swift: Received an unparsable request from the compiler: <error>` on stderr
  and exit status 1; upstream prints the same and exits with status 0 without a response.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the input. protoc and buf never produce such names.

- The names listed for [apple/swift](../../apple/swift/README.md#unsupported) (non-ASCII or NUL
  in file names, packages, type names, ...).
- A non-ASCII character in an option key or value that goes to an error text: upstream quotes it
  with `String.debugDescription`, which escapes some non-ASCII characters by grapheme break rules.
- A non-ASCII character in `ExtraModuleImports` or `SwiftProtobufModuleName` of a file with
  services: upstream sorts the imports by Swift string order (Unicode canonical equivalence).
