# bufbuild/es

Port of `protoc-gen-es`, the TypeScript/JavaScript generator of
[Protobuf-ES](https://github.com/bufbuild/protobuf-es), rewritten in Go: no Node.js required.

- buf.build: <https://buf.build/bufbuild/es>
- Upstream: <https://github.com/bufbuild/protobuf-es>
- License: Apache-2.0; ported files reference the upstream source they were ported from.

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

The protoplugin framework and the needed parts of `@bufbuild/protobuf` (descriptors, naming,
editions features, descriptor embedding) are ported into `pkg/languages/ecmascript/`.

## Port notes

- Embedded descriptors (`fileDesc("<base64>")`) are serialized with Go's `proto.Marshal`, which
  writes known fields in field number order followed by unknown fields, as protobuf-es does; both
  runtimes ship the same `descriptor.proto` schema except `FeatureSet.enforce_proto_limits` (9),
  which Go keeps as an unknown field (in input order, so the bytes still match). protobuf-es parses
  descriptors without an extension registry, while Go parses extensions that are linked into the
  binary (e.g. `pb.go` features) and writes them *before* the known fields, so they are converted
  back to unknown fields first (`protobuf.ExtensionsToUnknown`), merged by field number.
- Imported names are sorted with `localeCompare` upstream; `protoplugin.LocaleCompare` emulates
  the ICU root collation for identifier characters (`_` < `$` < digits < letters, case-insensitive
  first, lowercase before uppercase).

## Known differences

- Errors are returned in `CodeGeneratorResponse.error` as `protoc-gen-es: <error>`; upstream
  prints the same text to stderr and exits with status 1.
- The transpilation fallback of `@bufbuild/protoplugin` is not ported; protoc-gen-es never uses it.

## Options

Same as upstream: `target` (`ts`, `js`, `dts`, combined with `+`), `import_extension`,
`js_import_style`, `keep_empty_files`, `ts_nocheck`, `elide_plugin_version`, `bootstrap_wkt`,
`rewrite_imports`, `map_imports`, `json_types`, `valid_types`, `erasable_syntax`.

Protobuf-ES v2 provides native TS, JS and DTS generators, so the TypeScript-compiler based
transpilation of `@bufbuild/protoplugin` is not needed and not ported.

Golden option groups and tests: `testdata/checksums/bufbuild/es/<version>.buf.gen.yaml`.
