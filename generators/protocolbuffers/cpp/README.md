# protocolbuffers/cpp

C port of the C++ generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/cpp>. No protoc required. Generated code needs
the C++ runtime of protobuf 7.36.2 (`google/protobuf/*.h`, checked by `PROTOBUF_VERSION`).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/cpp
    opt: [uni-name=protocolbuffers/cpp, uni-version=v36.2]
```

It writes `<file>.pb.h` and `<file>.pb.cc` per file (and `<file>.proto.h` with `proto_h`,
`<file>.pb.h.meta` with `annotate_headers`). See the
[C++ generated code guide](https://protobuf.dev/reference/cpp/cpp-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The generator prints C++ text with
`io::Printer::Emit` (raw-string templates, callbacks, annotations) and runs no formatter: the port
prints the same text through the libprotoc port of `io::Printer`. The Emit templates are
upstream's raw strings with their common indentation reduced to one space (TokenizeFormat strips
it, so the output is the same).

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/cpp/`](https://github.com/protocolbuffers/protobuf/tree/v36.2/src/google/protobuf/compiler/cpp), [`generated_message_tctable_gen.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/generated_message_tctable_gen.cc) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

Each `.c` file names the upstream file it ports: `generator.cc`, `file.cc`, `message.cc`,
`field.cc` and `field_generators/*.cc`, `enum.cc`, `extension.cc`, `service.cc`, `helpers.cc`,
`tracker.cc`, `parse_function_generator.cc`, `message_layout_helper.cc` with `padding_optimizer.h`,
`field_layout.cc` and `field_chunk.cc` (in `message_layout_helper.c`), and the parse tables of
`generated_message_tctable_gen.cc` with the type cards and parse function names of
`generated_message_tctable_impl.h` (`tctable_gen.c`), and `GenerateEnumData` of
`generated_enum_util.cc` (in `enum.c`). `names.h` (shared with grpc's cpp plugin), the descriptor
pool, retention stripping, the serialized descriptors, `DebugString` and `io::Printer` are the
libprotoc port of the same protobuf version, which
[the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/cpp) is
built from. `ifndef_guard.cc` and `namespace_printer.cc` are not used by the generator.

The plugin is built like protoc's open-source release: `opensource_runtime` is true, there is no
profile (`access_info_map`), so lazy fields, split messages, inlined strings, verification and the
profile-driven layout are disabled, as upstream decides them.

## Options

All options of `CppGenerator::GenerateAll` are parsed like upstream; an unknown one is the error
`Unknown generator option: <key>`: `dllexport_decl`, `annotate_headers`,
`annotation_pragma_name`, `annotation_guard_name`, `speed`, `code_size`, `lite`, `proto_h`,
`proto_static_reflection_h` (no effect), `annotate_accessor`, `protos_for_field_listener_events`,
`inject_field_listener_events`, `forbidden_field_listener_events`, `force_eagerly_verified_lazy`
(no effect in the open-source build), `experimental_strip_nonfunctional_codegen` and
`descriptor_implicit_weak_messages` (no effect in the open-source build). The ones listed under
Unsupported are rejected.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the option or element.

- `lite_implicit_weak_fields` (numbered `.out/N.cc` files with implicit weak messages) and
  `experimental_cpp_micro_string`.
- An extension of `google.protobuf.FeatureSet` with a message type in a generated file (a custom
  feature set): upstream embeds its `FeatureSetDefaults` compiled by
  `FeatureResolver::CompileDefaults`, which is not ported for generator use.
- `[weak = true]` fields: upstream fails a check in the open-source build
  (`!options.opensource_runtime`). A plain `import weak` is generated like upstream (the dependency
  is treated as a normal one); only the rare path where upstream asks whether a dependency is weak
  in the open-source build fails its check and gives the error here too: a weak import of a known
  feature proto (such as `google/protobuf/cpp_features.proto`) with
  `experimental_strip_nonfunctional_codegen`.
- `uninterpreted_option` in any options message (see [protocolbuffers/ruby](../ruby/README.md)).

## Known differences

- Invalid descriptors and internal check failures (`ABSL_CHECK`, which abort upstream): see
  [protocolbuffers/ruby](../ruby/README.md); a failed check gives a `CodeGeneratorResponse.error`
  with its text.
- Warnings that upstream only logs to stderr (`optimize_for = CODE_SIZE` that falls back to
  `SPEED`) are not printed.
- When generation fails, the response has only the error (upstream also lists the files opened
  before it, which protoc and buf ignore).
- Memory: the arena keeps every temporary, so peak memory is about 13 times the generated code
  (about 20 KB per message). A request whose output nears the 2 GB response limit (upstream then
  fails with `protoc-gen-cpp: Error writing to stdout.`) needs tens of GB and can be killed by the
  system before it reaches that limit; for example, a file with 100,000 messages needs about 33 GB.
