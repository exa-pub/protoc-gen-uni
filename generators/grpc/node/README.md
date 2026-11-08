# grpc/node

C port of `grpc_node_plugin` from the `grpc-tools` npm package of
[grpc-node](https://github.com/grpc/grpc-node) (Apache-2.0): <https://buf.build/grpc/node>. No
protoc required. Generated code needs `@grpc/grpc-js` (with the `grpc_js` option; the default
requires the deprecated `grpc` package) and the message classes of
[protocolbuffers/js](../../protocolbuffers/js/README.md) with `import_style=commonjs`
(`google-protobuf`).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/js
    opt: [uni-name=protocolbuffers/js, uni-version=v4.0.3, import_style=commonjs]
  - local: protoc-gen-uni
    out: gen/js
    opt: [uni-name=grpc/node, uni-version=v1.13.1, grpc_js]
```

One `<file>_grpc_pb.js` per input file: a service definition object per service, the
serializers of its request and response types and, without `generate_package_definition`, a client
constructor. A file without services gets the single line `// GENERATED CODE -- NO SERVICES IN
PROTO` (no newline). Messages are required from `<path>_pb.js` relative to the file, those under
`google/protobuf` from `google-protobuf/`.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints JavaScript text with
the `io::Printer` of libprotoc 3.19.1 and runs no formatter: the port prints the same text,
including the indentation quirk of `PrintRaw` (comments are indented on their first line only, and
the line after them not at all).

## Options

As upstream (`node_plugin.cc`): the parameter is split at commas and each part compared exactly
(case-sensitive, no trimming); unknown options and values are ignored.

| option | effect |
|--------|--------|
| `grpc_js` | `require('@grpc/grpc-js')` instead of `require('grpc')` |
| `generate_package_definition` | no `grpc` import and no client constructors; services are exported as `exports['<full name>']` |
| `omit_serialize_instanceof` | serializers do not check `instanceof` |

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.13.1 | [`packages/grpc-tools/src`](https://github.com/grpc/grpc-node/tree/grpc-tools%401.13.1/packages/grpc-tools/src) | protobuf 3.19.1 (`packages/grpc-tools/deps/protobuf` submodule): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h), `LpVersion` [3.19.1](../../../pkg/languages/cpp/libprotoc/libprotoc_3.19.1.c) on top of [3.21.12](../../../pkg/languages/cpp/libprotoc/libprotoc_3.21.12.c) and its [`io::Printer`](../../../pkg/languages/cpp/libprotoc/printer_3.21.12.h) |

`generator.c` ports `node_plugin.cc`, `node_generator.c` ports `node_generator.{h,cc}` and
`node_generator_helpers.h`. grpc-tools vendors `generator_helpers.h` of grpc v1.25.0, whose
`StripProto` is case-sensitive and whose comments are not `$`-escaped:
[`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h) with its 1.25.0 part
(`generator_helpers_1.25.0.c`). [The remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/node/v1.13.1)
runs the prebuilt `grpc_node_plugin` of the `grpc-tools` 1.13.1 npm package (built on GitHub
Actions with CMake, statically linked).

The plugin declares `FEATURE_PROTO3_OPTIONAL` only and protobuf 3.19.1 knows no editions: a file
with `syntax = "editions"` fails with the log of the descriptor pool
(`[libprotobuf ERROR .../descriptor.cc:4205]   <file>: Unrecognized syntax: editions`), which the
port returns as `CodeGeneratorResponse.error`. The template tests it with `editions2023`.

## Known differences

- Invalid descriptors: upstream logs the errors (and warnings) of the pool to stderr and exits
  with status 1. The port puts the same text, with the `[libprotobuf ERROR ...]` and
  `[libprotobuf WARNING ...]` prefixes, in `CodeGeneratorResponse.error`. The order of several
  errors of a file can differ for some invalid inputs (the port validates in the order of libprotoc
  36.2 except where [3.19.1](../../../pkg/languages/cpp/libprotoc/libprotoc_3.19.1.c) says
  otherwise).
- A request that does not parse fails on stderr with exit status 1, as upstream, but with the
  prefix `protoc-gen-uni: parse request: ` (and, when `pkg/pb` cannot parse it at all, the message
  of protoc-gen-uni instead of `/grpc_node_plugin: protoc sent unparseable request to plugin.`).
- When generation fails, the response has only the error. Upstream also lists the files opened
  before the error, which protoc and buf ignore when there is an error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that names the input. protoc and buf
never produce them.

- `uninterpreted_option` in any options message (the option interpreter of protoc is not ported,
  see [libprotoc](../../../pkg/languages/cpp/libprotoc/descriptor.c)).
- A package whose prefixes (every part of the name before a `.`) take over 1 MiB together, such
  as 1,000 levels of 10-byte names. libprotoc 3.19.1 has no limit on the package and copies each
  prefix, so its time and memory grow with the square of the name; later versions reject packages
  over 511 bytes or 100 dots.
