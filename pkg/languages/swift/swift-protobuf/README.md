# SwiftProtobufPluginLibrary 1.39.0, C port

The parts of [`SwiftProtobufPluginLibrary`](https://github.com/apple/swift-protobuf/tree/1.39.0/Sources/SwiftProtobufPluginLibrary)
(swift-protobuf 1.39.0, Apache-2.0 with Runtime Library Exception) that `protoc-gen-swift`
(generators/apple/swift), the Connect-Swift plugins (generators/connectrpc/swift, swift-mocks,
through [`connect-plugin-utilities`](../connect-plugin-utilities)), `protoc-gen-grpc-swift`
(generators/grpc/swift) and `protoc-gen-grpc-swift-2` (generators/grpc/swift-protobuf, with
[`grpc-codegen`](../grpc-codegen)) use: the descriptor model with feature resolution and the bundled
edition defaults, `NamingUtils`, `SwiftProtobufNamer`, `CodePrinter`, source comments and the
`CodeGenerator` driver. The API is
`swift_protobuf.h` (prefix `sw_`); each `.c` file names the Swift files it ports.

## Versions

The library implements 1.39.0. Of the other versions in use, the plugin library differs only in
`PluginLibEditionDefaults.swift` (the rest of the diff is `nonisolated` annotations of generated
code):

| version | used by | versioned part |
|---------|---------|----------------|
| 1.39.0 | apple/swift v1.39.0, grpc/swift-protobuf v2.5.0 | — |
| 1.37.0 | connectrpc/swift, swift-mocks v1.2.3 | `edition_defaults_1.37.0.c` (`sw_edition_defaults_1_37_0`: editions up to 2024, no feature 9), selected by `SwCodeGenerator.edition_defaults` |
| 1.38.1 | grpc/swift v1.27.6 | `edition_defaults_1.38.1.c` (`sw_edition_defaults_1_38_1`: the defaults of 1.39.0 up to edition 2024) |

Strings are UTF-8 byte strings: SwiftProtobuf rejects requests with invalid UTF-8 and
`sw_check_request` does the same, so Swift's loops over Unicode scalars become loops over UTF-8
sequences. Lowercasing is ASCII only and strings compare by bytes, which differs from Swift only
for non-ASCII names, so names must be ASCII without NUL (`sw_ascii_name`): anything else fails with
`unsupported by protoc-gen-uni: ...` (protoc and buf never produce such names).

`ProtoFileToModuleMappings` keeps the mapping logic but not the loading of a mappings file (text
format), see the known differences of generators/apple/swift.

Tests: `tests/swift_protobuf_test.c`, with the expected values of upstream's
`Tests/SwiftProtobufPluginLibraryTests`.
