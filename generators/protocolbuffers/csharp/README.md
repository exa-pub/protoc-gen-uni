# protocolbuffers/csharp

C port of the C# generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/csharp>. No protoc required. Generated code needs
the `Google.Protobuf` NuGet package (3.36.2 or later).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/csharp
    opt: [uni-name=protocolbuffers/csharp, uni-version=v36.2]
```

It writes one `<FileBase>.cs` per file (the file name without directory and `.proto`, in
PascalCase), with the reflection class, the extensions, enums and messages of the file. See the
[C# generated code guide](https://protobuf.dev/reference/csharp/csharp-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The generator prints C# text with
`io::Printer` and runs no formatter: the port prints the same text. The serialized
`FileDescriptorProto` (in Base64, 60 characters per line) is embedded in every file, so the
descriptor pool, retention stripping and the C++ wire format decide those bytes.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/csharp/csharp_*.cc`](https://github.com/protocolbuffers/protobuf/tree/v36.2/src/google/protobuf/compiler/csharp) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

Each `.c` file of `v36.2/` ports the upstream file of the same name; the plugin main is the one of
[the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/csharp)
(`csharp::Generator`, `PluginMain`). `names.cc` (namespaces, class names, output file names) is
shared with grpc's csharp plugin and lives in libprotoc
([`csharp_names.h`](../../../pkg/languages/cpp/libprotoc/csharp_names.h)). The feature set
defaults include `pb.csharp` (`nullable_reference_types`, from edition 2026).

The field generator classes (`FieldGeneratorBase` and its subclasses) are one struct with the kind
of the most derived class, and each virtual method dispatches to the implementation the class
inherits or overrides. Upstream creates a field generator per field and method and scans the
fields for each presence index; the port keeps one generator per field and computes the presence
indexes once (same output).

## Options

Parsed like upstream (`ParseGeneratorParameter`); the first unknown option fails with
`Unknown generator option: <key>`.

| option | effect |
|--------|--------|
| `file_extension=<ext>` | extension of the generated files (default `.cs`) |
| `base_namespace=<ns>` | writes the files in directories of their namespace below `<ns>` (even when empty: the whole namespace); a namespace outside `<ns>` fails with `Namespace <namespace> is not a prefix namespace of base namespace <ns>` |
| `internal_access` | `internal` instead of `public` classes |
| `serializable` | `[global::System.SerializableAttribute]` on messages |
| `experimental_strip_nonfunctional_codegen` | no doc comments, no embedded descriptor, no feature imports, no synthetic oneofs in the reflection info |

A `csharp_namespace` with a character that could inject code (`;{}()'"` and others) fails like
upstream: `Invalid character in csharp_namespace option: ...`.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element. protoc and buf never produce them.

- `uninterpreted_option` in any options message (see [protocolbuffers/ruby](../ruby/README.md)).
- A `csharp_namespace` with a NUL byte in a dependency of a generated file (a generated file with
  one fails with the upstream error above). Names are C strings in the port.
- A field of a message type of a `google/protobuf/wrappers.proto` in the request whose message has
  no fields, or wrapper types nested through their first field more than 32 levels deep (undefined
  behavior or unbounded recursion upstream).

## Known differences

- Invalid descriptors and internal check failures: see [protocolbuffers/ruby](../ruby/README.md);
  a failed check gives a `CodeGeneratorResponse.error` with its text.
- When generation fails, the response has only the error (upstream also lists the files opened
  before it, which protoc and buf ignore).
- The warning upstream logs to stderr for an enum value name made unique with `_` is not printed.
