# GRPCCodeGen 2.4.4, C port

The IDL-independent code generator of gRPC Swift 2: [`Sources/GRPCCodeGen`](https://github.com/grpc/grpc-swift-2/tree/2.4.4/Sources/GRPCCodeGen)
of grpc-swift-2 2.4.4 (Apache-2.0; its StructuredSwift representation and `TextBasedRenderer`
come from swift-openapi-generator, Apache-2.0). Used by
[grpc/swift-protobuf](../../../../generators/grpc/swift-protobuf/README.md), whose
`GRPCProtobufCodeGen` fills a `CodeGenerationRequest` from the descriptors.

The API is `grpc_codegen.h` (prefix `gc_`): `GcRequest` (`CodeGenerationRequest`), `GcConfig`
(`CodeGenerator.Config`) and `gc_generate` (`CodeGenerator.generate`), which throws the
`CodeGenError`s of the translator with `fail()`. Internally (`structured_swift.h`) the translators
build the StructuredSwift nodes that upstream builds and `renderer.c` renders them as
`TextBasedRenderer` does, including its `nextLineAppendsToLastLine` line joining. Only the node
kinds and fields that GRPCCodeGen uses are ported. Each `.c` file names the Swift files it ports:

- `struct.c`: the conveniences of `StructuredSwiftRepresentation.swift`, `Namer.swift`;
- `service_metadata.c`, `server.c`, `client.c`: `StructuredSwift+ServiceMetadata.swift`,
  `StructuredSwift+Server.swift`, `StructuredSwift+Client.swift`;
- `translator.c`: `IDLToStructuredSwiftTranslator.swift`, `ServerCodeTranslator.swift`,
  `ClientCodeTranslator.swift`, `MetadataTranslator.swift`, `Docs.swift`, `CodeGenerator.swift`,
  `CodeGenError.swift`;
- `renderer.c`: `TextBasedRenderer.swift`.

Swift String operations on Characters (splitting comments into lines, `dropFirst(4)` of
documentation lines) use [`../std`](../std).

Tests: `tests/grpc_codegen_test.c`, with the expected values of upstream's
`Tests/GRPCCodeGenTests` (`DocsTests`, `IDLToStructuredSwiftTranslatorSnippetBasedTests`).
