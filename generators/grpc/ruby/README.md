# grpc/ruby

C port of `grpc_ruby_plugin` from [gRPC](https://github.com/grpc/grpc) (Apache-2.0):
<https://buf.build/grpc/ruby>. No protoc required. Generated code needs the `grpc` gem, next to
the message classes of [protocolbuffers/ruby](../../protocolbuffers/ruby/README.md) v36.2 (the
`<file>_pb.rb` it requires).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/ruby
    opt: [uni-name=protocolbuffers/ruby, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/ruby
    opt: [uni-name=grpc/ruby, uni-version=v1.85.1]
```

One `<file>_services_pb.rb` per file with services, with a `Service` class and a `Stub` per
service in the modules of `ruby_package` (or the package). See the
[gRPC Ruby basics tutorial](https://grpc.io/docs/languages/ruby/basics/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints Ruby text with the
legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port prints the same text.

## Options

None: upstream ignores the parameter.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`src/compiler`](https://github.com/grpc/grpc/tree/v1.85.1/src/compiler) (`ruby_plugin.cc`, `ruby_generator*`) | protobuf 35.1 ([`MODULE.bazel`](https://github.com/grpc/grpc/blob/v1.85.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_35.1.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_35.1.c) |

`generator.c` ports `ruby_plugin.cc`, `ruby_generator.c` ports `ruby_generator.{h,cc}`,
`ruby_generator_helpers-inl.h`, `ruby_generator_map-inl.h` and `ruby_generator_string-inl.h`. The
comment helpers of `generator_helpers.h` and `proto_parser_helper.cc` (`grpc_plugin_support`,
shared by the grpc C++ plugins) are [`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h).
The remote plugin is built with Bazel from the v1.85.1 tag
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/ruby/v1.85.1)),
with protobuf 35.1 and `GRPC_PROTOBUF_EDITION_SUPPORT`: `supported_features` is
`FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to 2024. libprotoc 35.1
differs from 36.2 by its maximum known edition (2024), its `descriptor.proto` and the validation
of map fields (see `libprotoc_35.1.c`).

Like upstream, file comments are those above the `syntax` (or `edition`) statement, printed raw:
`$` in them stays doubled (`$$`), while comments on services and methods go through `Print` with
`$` escaped, so they keep a single `$`.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason. The validation and its texts are those of libprotoc 36.2 except for the parts versioned
  for 35.1; the remaining differences of `descriptor.cc` between 35.1 and 36.2 (edition 2026,
  `pb.enumvalue.json`, the size limit of a file) only show for descriptors that protoc and buf
  reject.
- When generation fails (a file name that does not end with one of the characters of `.proto`),
  the response has only the error. Upstream also lists the files opened before the error, which
  protoc and buf ignore when there is an error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element.

- `uninterpreted_option` in any options message (libprotoc): the option interpreter of protoc is
  not ported. protoc and buf never produce it.
- A `ruby_package` that contains a NUL byte: upstream prints the whole string, NUL included, into
  the generated file; the port's strings end at NUL.
