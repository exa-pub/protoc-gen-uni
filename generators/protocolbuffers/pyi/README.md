# protocolbuffers/pyi

C port of the Python stub (`.pyi`) generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/pyi>. No protoc required. The stubs describe
the modules of [protocolbuffers/python](../python/README.md) for type checkers.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=protocolbuffers/pyi, uni-version=v36.2]
```

It writes `<dir>/<file>_pb2.pyi` per file. See the
[Python generated code guide](https://protobuf.dev/reference/python/python-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The generator prints Python text with
`io::Printer` (4 spaces per indent) and runs no formatter: the port prints the same text.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/python/pyi_generator.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/python/pyi_generator.cc), [`helpers.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/python/helpers.cc) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

The generator itself is the plugin main of [the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/pyi)
(`pyi_plugin_main.cc`). `PyiGenerator` and the Python helpers are shared with the `pyi_out` option
of protocolbuffers/python, so they live in libprotoc:
[`pkg/languages/cpp/libprotoc/compiler/python`](../../../pkg/languages/cpp/libprotoc/compiler/python/libprotoc_python.h),
next to the descriptor pool and `io::Printer` of the same protobuf version.

## Options

Parsed like upstream; any other option is the upstream error `Unknown generator option: <key>`.

| option | effect |
|--------|--------|
| `annotate_code` | accepted; upstream collects annotations but never writes them, so the output is the same |
| `experimental_strip_nonfunctional_codegen` | no imports of `google/protobuf/cpp_features.proto` and `java_features.proto` |
| `<name>.pyi` | writes every file to `<name>.pyi` instead of `<dir>/<file>_pb2.pyi` |

## Unsupported

- `uninterpreted_option` in any options message (see [protocolbuffers/ruby](../ruby/README.md)).

## Known differences

- Invalid descriptors: see [protocolbuffers/ruby](../ruby/README.md).
