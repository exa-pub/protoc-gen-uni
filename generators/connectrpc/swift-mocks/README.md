# connectrpc/swift-mocks

C port of `protoc-gen-connect-swift-mocks` from
[Connect-Swift](https://github.com/connectrpc/connect-swift) (Apache-2.0):
<https://buf.build/connectrpc/swift-mocks>. No Swift toolchain required. For every file to
generate that has services it writes `<file>.mock.swift` with, per service, a mock class
`<Prefix><Service>ClientMock` of the client protocol that
[connectrpc/swift](../swift/README.md) generates. Generated code needs the `ConnectMocks` library
of Connect-Swift.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/swift
    opt: [uni-name=connectrpc/swift-mocks, uni-version=v1.2.3, Visibility=Public, FileNaming=PathToUnderscores]
```

## Versions

| version | upstream sources | plugin library |
|---------|------------------|----------------|
| v1.2.3 | [`Plugins/ConnectMocksPlugin`](https://github.com/connectrpc/connect-swift/tree/1.2.3/Plugins/ConnectMocksPlugin), [`Plugins/ConnectPluginUtilities`](https://github.com/connectrpc/connect-swift/tree/1.2.3/Plugins/ConnectPluginUtilities) at tag `1.2.3` | `SwiftProtobufPluginLibrary` of [swift-protobuf 1.37.0](https://github.com/apple/swift-protobuf/tree/1.37.0/Sources/SwiftProtobufPluginLibrary) (`Package.resolved`) |

The buf.build recipe (`plugins/connectrpc/swift-mocks/v1.2.3`) builds the
`protoc-gen-connect-swift-mocks` product of the tag with Swift 6.3.2. Its registry configuration
adds `Visibility=Public` and `FileNaming=PathToUnderscores` for generated SDKs (not applied by
`buf generate`).

The options parser, file naming, the header and the imports are `ConnectPluginUtilities`, shared
with connectrpc/swift and ported in
[`pkg/languages/swift/connect-plugin-utilities`](../../../pkg/languages/swift/connect-plugin-utilities);
see [connectrpc/swift](../swift/README.md) for the plugin library version.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream prints Swift text with
`CodePrinter` (four-space indent) and runs no formatter: the port prints the same text.

## Options

The options of [connectrpc/swift](../swift/README.md#options), with the same parsing and errors.
`Visibility=Public` makes the mock classes and methods `open` and their properties `public`;
`GenerateCallbackMethods=true` adds callback mocks and `import Combine`.
`GenerateServiceMetadata` is accepted and has no effect. `supported_features` are proto3 optional
and editions, from proto2 to edition 2023.

## Known differences

As for [connectrpc/swift](../swift/README.md#known-differences) (`ProtoPathModuleMappings`, crashes,
undecodable requests, reported as `protoc-gen-connect-swift-mocks: Received an unparsable request
from the compiler: <error>`).

## Unsupported

As for [connectrpc/swift](../swift/README.md#unsupported).
