# protocolbuffers/objc

C port of the Objective-C generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/objc>. No protoc required. Generated code needs
the Objective-C runtime of protobuf (`GPBProtocolBuffers.h`, the `Protobuf` pod).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/objc
    opt: [uni-name=protocolbuffers/objc, uni-version=v36.2]
```

It writes `<dir>/<File>.pbobjc.h` and `<dir>/<File>.pbobjc.m` per file. See the
[Objective-C generated code guide](https://protobuf.dev/reference/objective-c/objective-c-generated/)
and [the generator options](https://github.com/protocolbuffers/protobuf/blob/v36.2/objectivec/README.md).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The generator prints Objective-C text
with `io::Printer::Emit` (raw-string templates, callbacks) and runs no formatter: the port prints
the same text through the libprotoc port of `io::Printer`.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/objectivec/`](https://github.com/protocolbuffers/protobuf/tree/v36.2/src/google/protobuf/compiler/objectivec) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

Each `.c` file names the upstream file it ports (`generator.cc`, `file.cc`, `message.cc`,
`field.cc` and its subclasses, `enum.cc`, `extension.cc`, `oneof.cc`, `import_writer.cc`,
`helpers.cc`, `tf_decode_data.cc`). `names.cc` (shared with grpc's objective_c plugin), the
descriptor pool, `GetSourceLocation` and `io::Printer` are the libprotoc port of the same protobuf
version, which [the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/objc)
is built from.

## Options

All options of `ObjectiveCGenerator::GenerateAll` are parsed like upstream (booleans take `yes`,
`no` or nothing, case insensitive; an unknown option or value is the upstream error):
`expected_prefixes_suppressions`, `prefixes_must_be_registered`, `require_prefixes`,
`generate_for_named_framework`, `runtime_import_prefix`, `use_package_as_prefix`,
`package_as_prefix_forced_prefix`, `headers_use_forward_declarations`, `strip_custom_options`,
`generate_minimal_imports`, `experimental_multi_source_generation`,
`experimental_strip_nonfunctional_codegen`, `annotation_pragma_name`, `annotation_guard_name`
(the `.pbobjc.h.meta` files with `GeneratedCodeInfo`) and `extension_generation_mode`
(`class_based`, `c_function`, `migration`). The options that name files are listed under
Unsupported.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the option. protoc-gen-uni reads no files besides the
request (the remote plugin runs without them, too).

- A non-empty `expected_prefixes_path` other than `-`, `named_framework_to_proto_path_mappings_path`,
  `package_to_prefix_mappings_path` and `proto_package_prefix_exceptions_path`. Upstream reads
  these files on the plugin host.
- `uninterpreted_option` in any options message (see [protocolbuffers/ruby](../ruby/README.md)).

## Known differences

- The environment variables of upstream (`GPB_OBJC_USE_PACKAGE_AS_PREFIX`,
  `GPB_OBJC_EXPECTED_PACKAGE_PREFIXES`, `GPB_OBJC_HEADERS_ONLY`, `GPB_OBJC_SKIP_IMPLS_FILE`, ...)
  are not read; the remote plugin runs without them.
- Warnings that upstream only prints to stderr (deprecated options, prefixes that are short or
  lower case) are not printed.
- Invalid descriptors and internal check failures (`ABSL_CHECK`, which abort upstream): see
  [protocolbuffers/ruby](../ruby/README.md); a failed check gives a `CodeGeneratorResponse.error`
  with its text.
- When generation fails, the response has only the error (upstream also lists the files opened
  before it, which protoc and buf ignore).
