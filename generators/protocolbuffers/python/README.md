# protocolbuffers/python

C port of the Python generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/python>. No protoc required. Generated code needs
the `protobuf` package (7.36.2 or later of the 7.x line: the modules check it with
`ValidateProtobufRuntimeVersion`).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=protocolbuffers/python, uni-version=v36.2]
```

It writes `<dir>/<file>_pb2.py` per file, and with `pyi_out` also the stub
`<dir>/<file>_pb2.pyi` of [protocolbuffers/pyi](../pyi/README.md). See the
[Python generated code guide](https://protobuf.dev/reference/python/python-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The generator prints Python text with
`io::Printer` and runs no formatter: the port prints the same text. Most of the output is the
serialized `FileDescriptorProto` (`AddSerializedFile`), the serialized options of every element
and the offsets of the elements in the serialized file, so the descriptor pool, retention stripping
and the C++ wire format decide the bytes.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/python/generator.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/python/generator.cc), [`helpers.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/python/helpers.cc), [`pyi_generator.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/python/pyi_generator.cc) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

`python_generator.c` ports `generator.cc`; the plugin main is the one of
[the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/python),
which keeps the open source runtime paths (no `bootstrap`, no google3 imports). The helpers and
`PyiGenerator` are shared with protocolbuffers/pyi in
[`pkg/languages/cpp/libprotoc/compiler/python`](../../../pkg/languages/cpp/libprotoc/compiler/python/libprotoc_python.h).

Behaviour kept from upstream:

- `_serialized_start` and `_serialized_end` are the first match of the element's serialized proto
  in the serialized file (`std::string::find`), which is not always the element's own position.
  Upstream searches the whole file for every element (quadratic); the port finds the same match
  with a suffix array of the serialized file (O(n log n)).
- Generating `google/protobuf/descriptor.proto` itself adds the pure-Python descriptors
  (`_USE_C_DESCRIPTORS` branches) and the resolved features of every element, computed through a
  dynamic `FeatureSet` of the request's own `descriptor.proto` (`testdata/proto/es-bootstrap`).

## Options

Parsed like upstream: every unknown option overwrites the error, so the last one is reported as
`Unknown generator option: <key>`.

| option | effect |
|--------|--------|
| `pyi_out` | also writes the `.pyi` stub (protocolbuffers/pyi), before the `.py` file |
| `annotate_code` | passed to the stub generator (no effect on the output) |
| `experimental_strip_nonfunctional_codegen` | passed to the stub generator |

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that contains
`unsupported by protoc-gen-uni:` and names the element.

- `uninterpreted_option` in any options message (see [protocolbuffers/ruby](../ruby/README.md)):
  `unsupported by protoc-gen-uni: uninterpreted_option in the options of ...`.
- Module aliases copied through public imports that would take more than 16 MiB: `<file>:
  unsupported by protoc-gen-uni: module aliases copied through public imports larger than 16 MiB`.
  `CopyPublicDependenciesAliases` prints one alias per path through the public imports, which
  grows exponentially with layers of files that publicly import several others, and
  quadratically with a file that imports every file of a chain of public imports; upstream would
  print them all. The size is computed before printing, so memory stays bounded.
- A serialized file of 2 GiB or more (beyond the protobuf message limit):
  `protoc-gen-python: unsupported by protoc-gen-uni: a serialized file of 2 GiB or more`.

## Known differences

- Invalid descriptors and internal check failures (`ABSL_CHECK`, which abort upstream, e.g. a
  generated `google/protobuf/descriptor.proto` without `FeatureSet`): see
  [protocolbuffers/ruby](../ruby/README.md); a failed check gives a `CodeGeneratorResponse.error`
  with its text.
- When generation fails, the response has only the error (upstream also lists the files opened
  before it, which protoc and buf ignore).
