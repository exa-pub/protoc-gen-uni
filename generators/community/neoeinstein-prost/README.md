# community/neoeinstein-prost

C port of `protoc-gen-prost` from [protoc-gen-prost](https://github.com/neoeinstein/protoc-gen-prost)
(Apache-2.0), with the code generator of [prost-build](https://github.com/tokio-rs/prost)
(Apache-2.0): <https://buf.build/community/neoeinstein-prost>. No Rust toolchain required.
Generated code needs the `prost` crate of the version in the table (and `prost-types` for the
well-known types).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/rust
    opt: [uni-name=community/neoeinstein-prost, uni-version=v0.5.0, file_descriptor_set]
```

Options are the same as upstream: [plugin options](https://github.com/neoeinstein/protoc-gen-prost/blob/protoc-gen-prost-v0.5.0/protoc-gen-prost/README.md#options).

## Versions

| version | upstream sources | prost-build |
|---------|------------------|-------------|
| v0.5.0 | [`protoc-gen-prost`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-prost-v0.5.0/protoc-gen-prost/src), [`prost-build`](https://github.com/tokio-rs/prost/tree/v0.14.1/prost-build/src), [`heck`](https://github.com/withoutboats/heck/tree/v0.5.0/src) 0.5.0 | 0.14.1 |

Each `.c` file names the upstream file it ports. The parts shared with the other
protoc-gen-prost plugins are in [`pkg/languages/rust`](../../../pkg/languages/rust).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-prost depends on prost-build
with `default-features = false`, so the `format` feature (syn + prettyplease) is off: prost-build
builds the code as plain strings and nothing is pretty-printed. The port prints the same strings.

All options of upstream are implemented: `btree_map`, `bytes`, `boxed`, `disable_comments`,
`skip_debug`, `default_package_filename`, `extern_path`, `type_attribute`, `field_attribute`,
`enum_attribute`, `message_attribute`, `compile_well_known_types`, `retain_enum_prefix`,
`enable_type_names`, `flat_output_dir` and `file_descriptor_set`, with upstream's errors.

## Known differences

- Errors go to `CodeGeneratorResponse.error`. Upstream panics (exit status 101, message on stderr)
  on inputs it cannot handle, e.g. editions (`unknown syntax: editions`), and on some invalid
  descriptors; the port reports the panic message.
- `file_descriptor_set` embeds every file as the buf.build remote plugin receives it: buf's
  `SourceCodeInfo` extension follows the locations. Upstream copies the request bytes, and a local
  buf writes that extension first.
- Upstream recurses once per message field in `can_message_derive_copy`/`_eq` and recomputes
  every message each time, so very long chains of message fields overflow its stack or take
  exponential time; the port gives the same answers without either.
- A request that cannot be parsed at all (truncated, malformed tags, nesting deeper than 100)
  fails with `protoc-gen-uni: parse request: ...` on stderr and exit status 1. Upstream reports
  prost's `DecodeError` in `CodeGeneratorResponse.error`.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`; protoc and buf never
  produce such a request):
  - a NUL byte in any string of the request (names are handled as C strings);
  - invalid UTF-8 in a string of the request, or a known field with an unexpected wire type:
    upstream fails with a prost `DecodeError` text (`failed to decode Protobuf message: ...`);
  - non-ASCII characters in an identifier that goes through heck case conversion or Rust keyword
    escaping (packages, message, field, oneof, enum and enum value names, type references, enum
    default values, the Rust path segments of an `extern_path` that resolves a nested type):
    upstream uses Unicode case mapping, the port only ASCII;
  - non-ASCII characters in an `extern_path` target written with `{:?}` (the `enumeration`
    attribute of an enum field): the Unicode printable and grapheme-extend tables are not ported.
