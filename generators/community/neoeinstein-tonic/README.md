# community/neoeinstein-tonic

C port of `protoc-gen-tonic` from [protoc-gen-prost](https://github.com/neoeinstein/protoc-gen-prost)
(Apache-2.0), with the gRPC code generator of [tonic-build](https://github.com/hyperium/tonic)
(MIT): <https://buf.build/community/neoeinstein-tonic>. No Rust toolchain required. It writes
[tonic](https://github.com/hyperium/tonic) clients and servers for the services, using the message
types of [community/neoeinstein-prost](../neoeinstein-prost). Generated code needs the `tonic` and
`tonic-prost` crates 0.14.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/rust
    opt: [uni-name=community/neoeinstein-prost, uni-version=v0.5.0]
  - local: protoc-gen-uni
    out: gen/rust
    opt: [uni-name=community/neoeinstein-tonic, uni-version=v0.5.0]
```

Without `no_include`, every `<package>.tonic.rs` is included into the `<package>.rs` of
neoeinstein-prost through the insertion point `module`, so run it after neoeinstein-prost with the
same `out` (and the same `flat_output_dir` and `default_package_filename`). The buf.build registry
runs it with `no_include=true` and `flat_output_dir`.

Options are the same as upstream: [plugin options](https://github.com/neoeinstein/protoc-gen-prost/blob/protoc-gen-tonic-v0.5.0/protoc-gen-tonic/README.md#options),
with upstream's error texts. All are implemented: `no_include`, `flat_output_dir` (not in
upstream's README), `compile_well_known_types`, `disable_package_emission`,
`default_package_filename`, `extern_path`, `no_server`, `no_client`, `no_transport`,
`client_mod_attribute`, `client_attribute`, `server_mod_attribute` and `server_attribute`.

## Versions

| version | upstream sources | tonic-build |
|---------|------------------|-------------|
| v0.5.0 | [`protoc-gen-tonic`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-tonic-v0.5.0/protoc-gen-tonic/src) (commit 6915e08b, same as protoc-gen-prost 0.5.0), [`tonic-build`](https://github.com/hyperium/tonic/tree/03894a2708c562d6b31c155b1ddc850510ed6c0e/tonic-build/src) 0.14.2, with [`protoc-gen-prost`](https://github.com/neoeinstein/protoc-gen-prost/tree/protoc-gen-prost-v0.5.0/protoc-gen-prost/src) 0.5.0, prost-build 0.14.1, [`heck`](https://github.com/withoutboats/heck/tree/v0.5.0/src) 0.5.0, prettyplease 0.2.37, syn 2.0.109, proc-macro2 1.0.103 (`Cargo.lock` of the crate; Rust 1.91.1) | 0.14.2 |

Each `.c` file names the upstream file it ports, except `unsupported.c` (the checks below).
tonic-build is ported in [`pkg/languages/rust/tonic-build`](../../../pkg/languages/rust/tonic-build),
the parsing of the attribute and `extern_path` texts in
[`pkg/languages/rust/syn`](../../../pkg/languages/rust/syn) (with the proc-macro2 lexer), the
printer in [`pkg/languages/rust/formatters/prettyplease`](../../../pkg/languages/rust/formatters/prettyplease).
The parts shared with the other protoc-gen-prost plugins are in
[`pkg/languages/rust`](../../../pkg/languages/rust): parameter parsing, the module requests, the
response, the prost decoding checks and heck.

**Class: formatted** ([LAYOUT.md](../../../docs/LAYOUT.md)). tonic-build quotes token streams, which
protoc-gen-tonic parses as a `syn::File` and prints with `prettyplease::unparse` after a
`// @generated` line. The port builds the syn nodes that those tokens parse to and prints them
with the prettyplease port.

Upstream behaviours the port keeps:

- Doc comments come from the leading comment of the service or method, found with a binary search
  over the `SourceCodeInfo` locations in request order (upstream does not sort them), so a
  comment is missing where the search misses; the empty leading comment of a location that is
  found gives an empty `///`.
- Attribute patterns are matched against the package (`*_mod_attribute`) and the service name
  `package.Service` (`*_attribute`) without a leading dot: `.` matches everything, a pattern with
  a leading dot nothing else, a pattern without one matches by suffix or equality.
- Attribute values are unescaped twice (`\,` and `\\`), once by the parameter parser and once by
  protoc-gen-tonic.

The request is checked like `CodeGeneratorRequest::decode` of prost 0.14.1 checks it: invalid
UTF-8 in a string field, a known field with another wire type and nesting beyond prost's recursion
limit fail with prost's error text (`failed to decode Protobuf message: ...`).

## Known differences

- Errors go to `CodeGeneratorResponse.error`. Upstream panics (exit status 101, `thread 'main'
  panicked at <file>:<line>:<col>:` and the message on stderr) on inputs it cannot handle, e.g. an
  `extern_path` without a leading dot, an attribute that syn cannot parse, or names that are not
  Rust identifiers; the port reports the panic message (assertions, `str` slicing and `Ident`
  errors with upstream's text, syn errors as an unsupported attribute or type below).
- A request that `pkg/pb` cannot parse at all fails with `protoc-gen-uni: parse request: ...` on
  stderr (exit status 1), also where prost would accept it (messages nested 100 levels below the
  request). protoc and buf never produce such requests.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - attribute texts beyond outer attributes whose meta is a path, a delimited token list, or
    `path = <literal or path>`; `extern_path` targets (and resolved type names) beyond the types
    listed in [`pkg/languages/rust/syn`](../../../pkg/languages/rust/syn/README.md). Upstream
    accepts some of these (a `#[a = 1 + 2]`, a `dyn Trait` target) and panics on the others;
  - a non-ASCII identifier character in an attribute or `extern_path` text (upstream lexes Unicode
    identifiers);
  - an attribute whose token groups (`(...)`, `[...]`, `{...}`) nest more than 255 levels deep:
    `unsupported by protoc-gen-uni: attribute tokens nested more than 255 levels deep` (upstream
    prints them, with output that grows with the square of the depth; see
    [`prettyplease`](../../../pkg/languages/rust/formatters/prettyplease/README.md));
  - a NUL byte in the parameter, `file_to_generate`, or a file name, package, service or method
    name or method input or output type: the port reads them as NUL-terminated strings;
  - a non-ASCII package, service name, method name or method input or output type: they go
    through heck case conversion, which upstream does with Unicode case mapping and
    [`pkg/languages/rust/heck`](../../../pkg/languages/rust/heck) only for ASCII.
- The golden tests cover `no_include=true` only: without it, the output inserts into the files of
  neoeinstein-prost in the same run, which a golden template with one plugin per group cannot
  hold. That mode, attributes with `#[...]` (a template option cannot hold `]` here), more
  `extern_path` forms and `compile_well_known_types` with an `extern_path` were compared with the
  buf.build remote plugins (neoeinstein-prost v0.5.0, then neoeinstein-tonic v0.5.0) by hand.
