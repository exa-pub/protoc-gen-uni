# protocolbuffers/ruby

C port of the Ruby generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/ruby>. No protoc required. Generated code needs
the `google-protobuf` gem.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/ruby
    opt: [uni-name=protocolbuffers/ruby, uni-version=v36.2]
```

The generator has no options; upstream ignores the parameter. See the
[Ruby generated code guide](https://protobuf.dev/reference/ruby/ruby-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The Ruby generator of protoc prints Ruby
text with `io::Printer` and runs no formatter: the port prints the same text. Most of the output is
the serialized `FileDescriptorProto`, so the descriptor pool, retention stripping and the C++ wire
format (see below) decide the bytes.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/ruby/ruby_generator.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/ruby/ruby_generator.cc) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

Each `.c` file names the upstream file it ports. The descriptor pool, validation, retention
stripping, serialization of the embedded descriptor and `io::Printer` are the libprotoc port of
the same protobuf version, which [the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/ruby)
is built from.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr and exits with
  status 1. The port puts the same text (without the log prefixes) in
  `CodeGeneratorResponse.error`.
- When generation fails (an invalid `ruby_package`), the response has only the error. Upstream
  also lists the file opened before the error, which protoc and buf ignore when there is an error.
- Warnings that upstream only logs to stderr (a `ruby_package` with `.`, unused imports) are not
  printed.

Custom options are re-serialized like upstream when the request contains
`google/protobuf/descriptor.proto` (the dynamic-message path of `StripSourceRetentionOptions`):
known fields by number, then unknown fields; map entries in parse order with duplicates, each
written with both key and value (`testdata/proto/map-options`).

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element. protoc and buf never produce them.

- `uninterpreted_option` in any options message: the option interpreter of protoc
  (`OptionInterpreter` in `descriptor.cc`) is not ported. Upstream would interpret the options
  while it builds the file, and the embedded descriptor would hold the interpreted values.
