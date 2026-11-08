# community/protobuf-c

C port of `protoc-gen-c` from [protobuf-c](https://github.com/protobuf-c/protobuf-c) (BSD-2-Clause,
with code from protobuf under its BSD-3-Clause license): <https://buf.build/community/protobuf-c>.
No protoc required. Generated code needs the `libprotobuf-c` runtime.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/c
    opt: [uni-name=community/protobuf-c, uni-version=v1.5.2]
```

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). `protoc-gen-c` prints C text with the
`io::Printer` of libprotoc and runs no formatter: the port prints the same text.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.5.2 | [`protoc-gen-c/`](https://github.com/protobuf-c/protobuf-c/tree/v1.5.2/protoc-gen-c), [`protobuf-c/protobuf-c.proto`](https://github.com/protobuf-c/protobuf-c/blob/v1.5.2/protobuf-c/protobuf-c.proto) | 3.21.12 ([`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h), `LpVersion` [3.21.12](../../../pkg/languages/cpp/libprotoc/libprotoc_3.21.12.c) and its [`io::Printer`](../../../pkg/languages/cpp/libprotoc/printer_3.21.12.h)) |

Each `.c` file names the upstream file it ports. [The remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/community/protobuf-c/v1.5.2)
builds the release tarball on Debian bookworm against its `libprotoc-dev` package, protobuf
3.21.12: the descriptor pool, its validation and log, `plugin.cc` and the old `io::Printer` are
those of 3.21.12. Before protobuf 26 the generator overrides neither the supported features nor
the editions: the response has `supported_features = 0` and no editions range, and a file with
`syntax = "editions"` fails with the pool's log (`Unrecognized syntax: editions`).

## Options

- `dllexport_decl=<macro>`: written before the exported declarations (`<macro> ` when not empty).
- Any other option fails with `<file>: Unknown generator option: <name>`.

The options of `protobuf-c/protobuf-c.proto` are read like upstream, where its generated code is
linked into the plugin: `pb_c_file` (`no_generate`, `gen_pack_helpers`, `gen_init_helpers`,
`const_strings`, `use_oneof_field_name`, `c_package`), `pb_c_msg` (`gen_pack_helpers`,
`gen_init_helpers`, `base_field_name`) and `pb_c_field` (`string_as_bytes`). A payload of one of
them that does not parse makes the request unparseable (`protoc-gen-c: protoc sent unparseable
request to plugin.`, exit status 1).

## Known differences

- Upstream crashes with SIGSEGV on a message with a group field (there is no field generator for
  `TYPE_GROUP`) and on `use_oneof_field_name` with a field that is not in a oneof. The port fails
  with `protoc-gen-c: unsupported by protoc-gen-uni: ...` naming the field.
- Invalid descriptors: upstream logs the errors of the pool to stderr and exits with status 1. The
  port puts the same text, with the `[libprotobuf ERROR ...]` prefixes, in
  `CodeGeneratorResponse.error`. Warnings that upstream also logs (a proto2 enum whose values
  differ only by case and the enum name prefix) are not printed, so the text differs when a file
  has both warnings and errors; the order of several errors of a file can differ too (the port
  validates in the order of libprotoc 36.2).
- When generation fails, the response has only the error. Upstream also lists the files opened
  before the error, which protoc and buf ignore when there is an error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the input. protoc and buf never produce them.

- A NUL byte in the parameter or in the protobuf-c options `c_package` and `base_field_name` (the
  port keeps them as C strings).
- `uninterpreted_option` in any options message (the option interpreter of protoc is not ported,
  see [libprotoc](../../../pkg/languages/cpp/libprotoc/descriptor.c)).
