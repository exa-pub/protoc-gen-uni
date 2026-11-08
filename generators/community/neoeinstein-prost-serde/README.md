# community/neoeinstein-prost-serde

C port of `protoc-gen-prost-serde` from [protoc-gen-prost](https://github.com/neoeinstein/protoc-gen-prost)
(Apache-2.0), with the code generator of [pbjson-build](https://github.com/influxdata/pbjson)
(MIT): <https://buf.build/community/neoeinstein-prost-serde>. No Rust toolchain required. It
writes `serde::Serialize` and `serde::Deserialize` implementations that follow the protobuf JSON
mapping for the types that [community/neoeinstein-prost](../neoeinstein-prost) generates. Generated
code needs the `serde` and `pbjson` crates (and `pbjson-types` for the well-known types).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/rust
    opt: [uni-name=community/neoeinstein-prost, uni-version=v0.5.0]
  - local: protoc-gen-uni
    out: gen/rust
    opt: [uni-name=community/neoeinstein-prost-serde, uni-version=v0.4.0]
```

Without `no_include`, every `<package>.serde.rs` is included into the `<package>.rs` of
neoeinstein-prost through the insertion point `module`, so run it after neoeinstein-prost with the
same `out` (and the same `flat_output_dir` and `default_package_filename`).

Options are the same as upstream: [plugin options](https://github.com/neoeinstein/protoc-gen-prost/blob/protoc-gen-prost-serde-v0.4.0/protoc-gen-prost-serde/README.md#options),
with upstream's error texts. All are implemented: `btree_map`, `default_package_filename`,
`extern_path`, `retain_enum_prefix`, `preserve_proto_field_names`, `ignore_unknown_fields`,
`emit_fields`, `use_integers_for_enums`, `no_include`, `flat_output_dir` and `exclude` (the last
two are not in upstream's README). As upstream, `ignore_unknown_fields=false`, `emit_fields=false`
and `use_integers_for_enums=false` are invalid parameters.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). pbjson-build writes the code with
`write!`/`writeln!` and an `Indent` helper, and nothing is pretty-printed; the port prints the same
text.

The request is checked like `CodeGeneratorRequest::decode` of prost 0.14.1 checks it: invalid
UTF-8 in a string field, a known field with another wire type and nesting beyond prost's recursion
limit fail with prost's error text (`failed to decode Protobuf message: ...`).

## Versions

| version | upstream sources | pbjson-build |
|---------|------------------|--------------|
| v0.4.0 | [`protoc-gen-prost-serde`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-prost-serde-v0.4.0/protoc-gen-prost-serde/src), [`pbjson-build`](https://github.com/influxdata/pbjson/tree/3587e007496f87790afd7ac7b5ff45cfe1df3297/pbjson-build/src), with [`protoc-gen-prost`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-prost-v0.5.0/protoc-gen-prost/src) 0.5.0 and [`heck`](https://github.com/withoutboats/heck/tree/v0.5.0/src) 0.5.0 | 0.8.0 |

Each `.c` file names the upstream file it ports, except `unsupported.c` (the checks below).
pbjson-build is ported in [`pkg/languages/rust/pbjson-build`](../../../pkg/languages/rust/pbjson-build)
(its README describes the descriptor set lookup). The parts shared with the other protoc-gen-prost
plugins are in [`pkg/languages/rust`](../../../pkg/languages/rust): parameter parsing, the module
requests, the response, the prost decoding checks (`protoc-gen-prost/decode.c`) and heck.

## Known differences

- Errors go to `CodeGeneratorResponse.error`. Upstream panics (exit status 101, message on stderr)
  on inputs it cannot handle, e.g. editions (`unknown syntax: editions`), a file without package
  (`expected package`), relative type names and some invalid descriptors; the port reports a
  message like the panic's. Upstream recurses without a limit through map entries whose value is a
  map entry and overflows its stack on a cycle; the port fails at depth 64.
- A request that `pkg/pb` cannot parse at all fails with `protoc-gen-uni: parse request: ...` on
  stderr (exit status 1), also where prost would accept it (messages nested 100 levels below the
  request). protoc and buf never produce such requests.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`; protoc and buf never
  produce such a request):
  - a NUL byte in the parameter, `file_to_generate`, or a name, package, syntax, `type_name` or
    `json_name` of a file, message, field, oneof, enum or enum value: the port reads them as
    NUL-terminated strings;
  - a non-ASCII package, and a non-ASCII identifier that goes through heck case conversion or
    keyword escaping (message, enum, field, oneof and enum value names, and package parts):
    upstream uses Unicode case mapping, [`pkg/languages/rust/heck`](../../../pkg/languages/rust/heck)
    only ASCII.
- The golden tests cover only `no_include`: without it, the output inserts into the files of
  neoeinstein-prost in the same run, which a golden template with one plugin per group cannot
  hold. That mode was compared with the buf.build remote plugins (neoeinstein-prost v0.5.0, then
  neoeinstein-prost-serde v0.4.0, with and without `flat_output_dir`) by hand.
