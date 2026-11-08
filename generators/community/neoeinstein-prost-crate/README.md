# community/neoeinstein-prost-crate

C port of `protoc-gen-prost-crate` from [protoc-gen-prost](https://github.com/neoeinstein/protoc-gen-prost)
(Apache-2.0): <https://buf.build/community/neoeinstein-prost-crate>. No Rust toolchain required.
It writes the include file (`mod.rs`) with a `pub mod` block and an `include!` for every module
that [community/neoeinstein-prost](../neoeinstein-prost) generates, and the Cargo features of the
packages. Run it with the same `out` as neoeinstein-prost.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/rust
    opt: [uni-name=community/neoeinstein-prost-crate, uni-version=v0.5.0, include_file=lib.rs, no_features]
```

Options are the same as upstream: [plugin options](https://github.com/neoeinstein/protoc-gen-prost/blob/protoc-gen-prost-crate-v0.5.0/protoc-gen-prost-crate/README.md#options),
with upstream's error texts. All are implemented except `gen_crate` (see below).

Class: direct (docs/LAYOUT.md). Upstream builds the include file and the Cargo features with
`String` pushes and runs no formatter, so the port prints the same text.

The request is checked like `CodeGeneratorRequest::decode` of prost 0.14.1 checks it, with the
messages of prost-types 0.14.1: invalid UTF-8 in a string field, a known field with another wire
type and nesting beyond prost's recursion limit fail with prost's error text
(`failed to decode Protobuf message: ...`). Fields that prost-types 0.14.1 does not know (editions,
features, `source_file_descriptors`, ...) are skipped like upstream skips them.

## Versions

| version | upstream sources | prost-build |
|---------|------------------|-------------|
| v0.5.0 | [`protoc-gen-prost-crate`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-prost-crate-v0.5.0/protoc-gen-prost-crate/src), with [`protoc-gen-prost`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-prost-v0.5.0/protoc-gen-prost/src) 0.5.0 as a library | 0.14.1 |

Each `.c` file names the upstream file it ports, except `index.c`: a hash index that makes
`only_include` and the dependency search of the features linear in the names (upstream scans all
prefixes or packages per name), with the same results. The parts shared with the other
protoc-gen-prost plugins are in [`pkg/languages/rust`](../../../pkg/languages/rust), among them the
prost decoding checks (`prost/decode.c`).

## Known differences

- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - `gen_crate[=<template>]`. Upstream reads the `Cargo.toml` template from the file system it runs
    on; protoc-gen-uni reads nothing but the request. The buf.build remote plugin fails with
    `No such file or directory (os error 2)` for a template outside its container but reads the
    files of its container (`gen_crate=/dev/null` gives an empty `Cargo.toml`), so no fixed
    answer matches it.
  - a NUL byte in the parameter, a file name, `file_to_generate`, a package, and (without
    `no_features`) in the `type_name` of a field or the `input_type`/`output_type` of a method.
    The port reads them as NUL-terminated strings. protoc and buf never produce one.
  - a package with non-ASCII characters. Upstream derives module names with heck's Unicode case
    conversion; [`pkg/languages/rust`](../../../pkg/languages/rust) converts ASCII only. Protobuf
    identifiers are ASCII.
- Errors go to `CodeGeneratorResponse.error`. Upstream panics (exit status 101, message on stderr)
  when a generated file has no package.
- A request that `pkg/pb` cannot parse fails with `protoc-gen-uni: parse request: ...` on stderr
  (exit status 1), also where prost would accept it: messages nested 100 levels below the request
  (prost allows that depth), and groups in unknown fields at the deepest levels. protoc and buf
  never produce such requests.
- The golden tests cover only `no_features`. Without it, the features are inserted at the
  insertion point `features` of `Cargo.toml`, which buf only finds with `include_file=Cargo.toml`
  and a package named `features`. That mode was compared with the buf.build remote plugin on such
  inputs by hand.
