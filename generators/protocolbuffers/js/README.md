# protocolbuffers/js

C port of `protoc-gen-js` from [protobuf-javascript](https://github.com/protocolbuffers/protobuf-javascript)
(BSD-3-Clause): <https://buf.build/protocolbuffers/js>. No protoc and no Node.js required.
Generated code needs the `google-protobuf` npm package.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/js
    opt: [uni-name=protocolbuffers/js, uni-version=v4.0.3, import_style=commonjs, binary]
```

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). `protoc-gen-js` is a C++ plugin that
prints JavaScript with `io::Printer` (its legacy `Print` API) and runs no formatter: the port
prints the same text.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v4.0.3 | [`generator/`](https://github.com/protocolbuffers/protobuf-javascript/tree/v4.0.3/generator) (`js_generator.cc`, `well_known_types_embed.cc`, `protoc-gen-js.cc`) | 33.4 ([`MODULE.bazel`](https://github.com/protocolbuffers/protobuf-javascript/blob/v4.0.3/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_33.4.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_33.4.c) |

The remote plugin is the prebuilt `protoc-gen-js` of the v4.0.3 release
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/js)).
Each `.c` file names the upstream file it ports. The descriptor pool, its validation, the feature
defaults, `io::Printer` and `compiler/scc.h` are the libprotoc port; libprotoc 33.4 differs from
36.2 by its maximum edition (2024) and its `descriptor.proto` (see `libprotoc_33.4.c`).

`supported_features` is `FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to
2024.

## Options

Options are parsed like upstream (`GeneratorOptions::ParseFromOptions`), with its error texts:

| option | |
|--------|---|
| `import_style=closure\|commonjs\|commonjs_strict\|browser\|es6` | default `closure`. An unknown style sets the error `Unknown import style X, expected one of: closure, commonjs, browser, es6.` but generation goes on, so the run fails with it |
| `binary` | getters of `bytes` fields return `(string\|Uint8Array)` instead of base64 strings |
| `output_dir=DIR`, or a bare `DIR` | directory of the output files (default `.`) |
| `namespace_prefix=NS` | namespace instead of `proto.<package>` |
| `annotate_code` | a base64 `GeneratedCodeInfo` comment at the end of every file |
| `error_on_name_conflict` | ignored (upstream logs a warning) |
| `add_require_for_enums`, `testonly`, `library=NAME`, `extension=EXT`, `one_output_file_per_input_file` | only with `import_style=closure`, else the error `The add_require_for_enums, testonly, library, extension, and one_output_file_per_input_file options should only be used for import_style=closure` |

Any other `key=value` gives `Unknown option: key`. Output: one `<name>_pb.js` per input file
(non-closure styles, or `one_output_file_per_input_file`), one file for everything (`library`), or
one file per strongly connected component of messages, enum and file extensions (closure; the
well-known type files keep one file each, named `/<name>.js` after the output directory like
upstream).

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr and exits with
  status 1. The port puts the same text (without the log prefixes) in
  `CodeGeneratorResponse.error`. The validation and its texts are those of libprotoc 36.2 except
  for the parts versioned for 33.4 (`pkg/languages/cpp/libprotoc/libprotoc_33.4.c`): the edition
  range, the 33.4 `descriptor.proto`, the feature lifetime validation with the texts of
  `feature_resolver.cc` 33.4, and `java_multiple_files` in edition 2024 as an error. The remaining
  differences of `descriptor.cc` between 33.4 and 36.2 (custom JSON names, name length limits, ...)
  only show for descriptors that protoc and buf reject.
- Failed checks of upstream (`import_style=commonjs_strict` with a `namespace_prefix` that does not
  start with `proto.`) abort the plugin; the port returns
  `protoc-gen-js: Check failed: ...` as `CodeGeneratorResponse.error`.
- When generation fails, the response has only the error. Upstream also lists the files opened
  before the error, which protoc and buf ignore when there is an error.
- Warnings that upstream only logs to stderr (`error_on_name_conflict`, string defaults that are
  not valid UTF-8 or outside the BMP, which are truncated in both) are not printed.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element (libprotoc). protoc and buf never produce
them.

- `uninterpreted_option` in any options message: the option interpreter of protoc is not ported.
