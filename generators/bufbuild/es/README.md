# bufbuild/es

C port of `protoc-gen-es` from [Protobuf-ES](https://github.com/bufbuild/protobuf-es)
(Apache-2.0): <https://buf.build/bufbuild/es>. No Node.js required. Generated code needs the
`@bufbuild/protobuf` runtime of the same version.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/es
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

Options are the same as upstream: [plugin options](https://github.com/bufbuild/protobuf-es/blob/v2.17.0/MANUAL.md).

## Versions

| version | upstream sources |
|---------|------------------|
| v2.17.0 | [`protoc-gen-es`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protoc-gen-es/src), [`protoplugin`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protoplugin/src), parts of [`protobuf`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protobuf/src) |

Each `.c` file names the upstream file it ports.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-es prints TypeScript and
JavaScript text with `GeneratedFile.print` of `@bufbuild/protoplugin` and runs no formatter: the
port prints the same text. protoplugin can transpile TypeScript with the TypeScript compiler when
a plugin has no JavaScript or declaration generator, but protoc-gen-es has both, so it never does.

All options of upstream are implemented: `target`, `import_extension`, `js_import_style`,
`ts_nocheck`, `elide_plugin_version`, `keep_empty_files`, `map_imports`, `rewrite_imports`,
`bootstrap_wkt`, `json_types`, `valid_types` and `erasable_syntax`, with upstream's errors.

The request is read like `fromBinary` of `@bufbuild/protobuf` reads it: invalid UTF-8 in string
fields (comments, names, default values, the parameter) becomes U+FFFD, as with a `TextDecoder`.

## Known differences

- Errors go to `CodeGeneratorResponse.error`. Upstream prints `protoc-gen-es: <error>` to stderr
  and exits with status 1; a request that cannot be parsed fails with
  `protoc-gen-uni: parse request: <error>` on stderr instead.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`; protoc and buf never
  produce such a request):
  - a NUL byte in a file name, package, dependency, syntax, type reference (`type_name`,
    `extendee`, `input_type`, `output_type`), `file_to_generate`, or in the value of
    `map_imports` or `rewrite_imports`;
  - a known field of the request with an unexpected wire type, which upstream reads by the type
    of the field;
  - a malformed `(pb.enumvalue.json)` or `(buf.validate.field)` option (truncated, invalid tags,
    known fields with an unexpected wire type). Upstream fails with an error of its binary reader
    (`premature EOF`, `illegal tag: ...`) or reads the field by its type.
