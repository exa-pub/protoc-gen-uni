# grpc/python

C port of `grpc_python_plugin` from [gRPC](https://github.com/grpc/grpc) (Apache-2.0):
<https://buf.build/grpc/python>. No protoc required. Generated code needs the `grpcio` package
(`grpcio>=1.85.1`), next to the message modules of
[protocolbuffers/python](../../protocolbuffers/python/README.md) v36.2 (the `<file>_pb2` it
imports).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=protocolbuffers/python, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=grpc/python, uni-version=v1.85.1]
```

One `<file>_pb2_grpc.py` per file, with or without services: per service a `<Service>Stub`, a
`<Service>Servicer`, `add_<Service>Servicer_to_server` and the experimental `<Service>` class of
static methods. See the [gRPC Python basics tutorial](https://grpc.io/docs/languages/python/basics/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints Python text with the
legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port prints the same text.

## Options

Positional, comma-separated: `[<version>[,<strip prefix>]]`.

- `<version>`: `grpc_2_0` (the default, also for an empty parameter) writes `<file>_pb2_grpc.py`;
  `grpc_1_0` also writes the deprecated GA and Beta API into the insertion point `module_scope` of
  `<file>_pb2.py`, so it needs protocolbuffers/python in the same run and output directory (else
  buf fails, for upstream as for the port). Anything else: `Invalid grpc version '<version>'.`
- `<strip prefix>`: removed from the start of the imported module names, which are dotted:
  `google.` turns `google.api.http_pb2` into `api.http_pb2`, while `google/` matches nothing.
- More than two elements: `--grpc_python_out received too many comma-delimited parameters.`
  Like upstream's split, a final empty element is dropped (`grpc_2_0,` is `grpc_2_0`).

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`src/compiler`](https://github.com/grpc/grpc/tree/v1.85.1/src/compiler) (`python_plugin.cc`, `python_generator*`, `python_private_generator.h`, `protobuf_plugin.h`) | protobuf 35.1 ([`MODULE.bazel`](https://github.com/grpc/grpc/blob/v1.85.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_35.1.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_35.1.c) |

`generator.c` ports `python_plugin.cc` and `PythonGrpcGenerator::Generate`; `python_generator.c`
ports the rest of `python_generator.{h,cc}`, `python_private_generator.h`,
`python_generator_helpers.h` and the Python parts of `protobuf_plugin.h`. The comment helpers of
`generator_helpers.h` (`grpc_plugin_support`, shared by the grpc C++ plugins) are
[`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h); `GeneratorContext::OpenForInsert`
is in libprotoc (`plugin.c`). The remote plugin is built with Bazel from the v1.85.1 tag
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/python/v1.85.1)),
with protobuf 35.1 and `GRPC_PROTOBUF_EDITION_SUPPORT`: `supported_features` is
`FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to 2024. See
[grpc/ruby](../ruby/README.md) for what libprotoc 35.1 changes.

Tested: the golden template covers the default, `grpc_2_0` with a strip prefix and the two
parameter errors. `grpc_1_0` cannot be a golden group (the insertion needs the `_pb2.py` of
another plugin); its output with protocolbuffers/python v36.2 in the same run was compared with the
remote plugins by hand (inputs `services`, `comments`, `imports`: identical).

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason (see [grpc/ruby](../ruby/README.md)).
- When generation fails (a file name or the file of a request or response type whose name does
  not end with one of the characters of `.proto`, or a parameter error), the response has only the
  error. Upstream also lists the files opened before the error, which protoc and buf ignore when
  there is an error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element.

- `uninterpreted_option` in any options message (libprotoc): the option interpreter of protoc is
  not ported. protoc and buf never produce it.
