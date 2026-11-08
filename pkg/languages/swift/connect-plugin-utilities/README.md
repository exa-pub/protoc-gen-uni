# ConnectPluginUtilities 1.2.3, C port

The library of [Connect-Swift](https://github.com/connectrpc/connect-swift) (Apache-2.0) shared by
its two protoc plugins, [connectrpc/swift](../../../../generators/connectrpc/swift/README.md) and
[connectrpc/swift-mocks](../../../../generators/connectrpc/swift-mocks/README.md):
[`Plugins/ConnectPluginUtilities`](https://github.com/connectrpc/connect-swift/tree/1.2.3/Plugins/ConnectPluginUtilities)
at tag `1.2.3`. It builds on SwiftProtobufPluginLibrary 1.37.0 (the version in connect-swift's
`Package.resolved`), ported in [`../swift-protobuf`](../swift-protobuf) with its 1.37.0 edition
defaults.

The API is `connect_plugin_utilities.h` (prefix `cpu_`); each `.c` file names the Swift file it
ports:

- `generator_options.c`: `GeneratorOptions.swift` (options and their errors);
- `file_path_components.c`: `FilePathComponents.swift` (output file names);
- `extensions.c`: `MethodDescriptor+Extensions.swift`, `ServiceDescriptor+Extensions.swift`
  (paths, names, method signatures);
- `generator.c`: `Generator.swift`, the base class: a plugin fills a `CpuGenerator` with its
  `outputFileExtension` and `printContent(for:)` override and runs `cpu_main`.

Tests: `tests/connect_plugin_utilities_test.c`, with the cases of upstream's
`Tests/UnitTests/ConnectPluginUtilitiesTests` and option errors checked against the remote plugin.
