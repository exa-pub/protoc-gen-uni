# bufbuild/es

Port of `protoc-gen-es`, the TypeScript/JavaScript generator of
[Protobuf-ES](https://github.com/bufbuild/protobuf-es), rewritten in C: no Node.js required.

- buf.build: <https://buf.build/bufbuild/es>
- Upstream: <https://github.com/bufbuild/protobuf-es>
- License: Apache-2.0; ported files keep the upstream copyright header and name the upstream
  source they were ported from.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/es
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

Generated code needs the `@bufbuild/protobuf` runtime of the same version at runtime, as with upstream.

## Versions

| version | upstream sources |
|---------|------------------|
| v2.17.0 | [`packages/protoc-gen-es/src`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protoc-gen-es/src), [`packages/protoplugin/src`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protoplugin/src), parts of [`packages/protobuf/src`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protobuf/src) |

## Where the code lives

- `<version>/` is a unity build: `generator.c` includes `valid_types.c`, `util.c` and `plugin.c`
  (ports of the files of `packages/protoc-gen-es/src`) and exports only `gen_bufbuild_es_v2_17_0`.
- `pkg/languages/ecmascript/protoplugin` — `@bufbuild/protoplugin` (parameter parsing, schema,
  generated files with automatic imports, file preambles, JSDoc); one `.c` file per upstream file.
- `pkg/languages/ecmascript/protobuf` — the needed parts of `@bufbuild/protobuf`: descriptors,
  naming, editions features, descriptor embedding.
- `pkg/pb` — the wire format and the `descriptor.proto`/`plugin.proto` schemas.

## Port notes

- Requests are parsed like `fromBinary` of protobuf-es (`pkg/pb`): the schema is the
  `descriptor.proto` that `@bufbuild/protobuf` v2.17.0 ships (`FeatureSet.enforce_proto_limits` is a
  known field), there is no extension registry (extensions stay unknown fields), unknown values of
  closed enums become unknown fields (`int32`, sign-extended), required fields
  (`UninterpretedOption.NamePart`) are not checked, and nesting is limited to 100 messages
  (`recursionLimit`). Embedded descriptors (`fileDesc("<base64>")`) are serialized like
  `toBinary`: known fields in field number order, then unknown fields in wire order.
- Imported names are sorted with `localeCompare` upstream; `protoplugin/collate.c` emulates the ICU
  root collation for identifier characters (`_` < `$` < digits < letters, case-insensitive first,
  lowercase before uppercase).
- On inputs that protoc and buf do not produce, the port still follows upstream: U+2028/U+2029 in
  values echoed into the preamble are escaped (`\u2028`) like `escapeValue`, and names in
  "invalid ... name" errors are quoted like `JSON.stringify`.

## Known differences

- Errors are returned in `CodeGeneratorResponse.error` as `protoc-gen-es: <error>`; upstream
  prints the same text to stderr and exits with status 1.
- File and package names are handled as C strings: a NUL byte ends them (protoc and buf never
  produce one). NUL bytes in comments, default values, JSON names and the parameter are kept.
- The transpilation fallback of `@bufbuild/protoplugin` is not ported; protoc-gen-es never uses it.

## Options

Same as upstream: `target` (`ts`, `js`, `dts`, combined with `+`), `import_extension`,
`js_import_style`, `keep_empty_files`, `ts_nocheck`, `elide_plugin_version`, `bootstrap_wkt`,
`rewrite_imports`, `map_imports`, `json_types`, `valid_types`, `erasable_syntax`.

Protobuf-ES v2 provides native TS, JS and DTS generators, so the TypeScript-compiler based
transpilation of `@bufbuild/protoplugin` is not needed and not ported.

Golden option groups and tests: `testdata/checksums/bufbuild/es/<version>.buf.gen.yaml`.
