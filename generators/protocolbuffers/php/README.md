# protocolbuffers/php

C port of the PHP generator of `protoc` from [Protocol Buffers](https://github.com/protocolbuffers/protobuf)
(BSD-3-Clause): <https://buf.build/protocolbuffers/php>. No protoc required. Generated code needs
the `google/protobuf` Composer package or the `protobuf` PECL extension.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/php
    opt: [uni-name=protocolbuffers/php, uni-version=v36.2]
```

It writes one class file per message and enum (`<Namespace>/<Class>.php`, nested types as
`<Outer>/<Inner>.php`) and one metadata class per file (`GPBMetadata/<Dir>/<File>.php`) that adds
the serialized descriptor to the pool. See the
[PHP generated code guide](https://protobuf.dev/reference/php/php-generated/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The generator prints PHP text with
`io::Printer` (`^` as the variable delimiter) and runs no formatter: the port prints the same
text. The metadata classes embed a serialized `FileDescriptorSet`, so the descriptor pool,
retention stripping and the C++ wire format decide those bytes; the doc comments come from the
source code info and the first line of `FieldDescriptor::DebugString()`.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v36.2 | [`compiler/php/php_generator.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/php/php_generator.cc), [`names.cc`](https://github.com/protocolbuffers/protobuf/blob/v36.2/src/google/protobuf/compiler/php/names.cc) | [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) |

`php_generator.c` ports `php_generator.cc`; the plugin main is the one of
[the remote plugin](https://github.com/bufbuild/plugins/tree/main/plugins/protocolbuffers/php).
`names.cc` (shared with grpc's php plugin) is
[`php_names.c`](../../../pkg/languages/cpp/libprotoc/php_names.h) of libprotoc, and so are the
descriptor pool, `GetSourceLocation`, `DebugString`, `absl::StrCat(double)` and `io::Printer`.

Behaviour kept from upstream:

- The embedded `FileDescriptorSet` drops the first dependency that is *not*
  `google/protobuf/descriptor.proto` (the loop was meant to drop descriptor.proto), then the
  public and weak dependencies and all extensions.
- Default values of `float` and `double` fields with presence print with 6 significant digits
  (`absl::StrCat`).
- Required fields, groups (and `DELIMITED` fields) and closed enums (all proto2 enums) fail
  generation with upstream's error, except with `internal`.
- A `php_namespace` or `php_metadata_namespace` with a character that could inject code fails
  generation with upstream's error.

## Options

- `internal`: generate `google/protobuf/descriptor.proto` as `Google\Protobuf\Internal` classes
  registered with `addMessage`/`addEnum`; any other file fails with upstream's error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element.

- `aggregate_metadata[=<prefix>#...]`: `GenerateAddFilesToPool` orders the files by iterating absl
  hash sets of descriptor pointers, so upstream's output changes from run to run.
- `internal_generate_c_wkt`: writes `../ext/google/protobuf/wkt.inc`, the well-known types of the
  protobuf PHP extension (used by protobuf's own build).
- `uninterpreted_option` in any options message (see [protocolbuffers/ruby](../ruby/README.md)).

## Known differences

- An unknown option aborts upstream (`ABSL_LOG(FATAL)`, a log line with a timestamp on stderr).
  The port returns the error `Unknown codegen option: <name>`. An option that is only `=` signs,
  which makes upstream read past the end of a vector, is such an unknown option with an empty name.
- Invalid descriptors and internal check failures: see [protocolbuffers/ruby](../ruby/README.md).
- A message of a file named `google/protobuf/wrappers.proto` that has no field `value`, used as a
  field type, crashes upstream (a null pointer in `GenerateWrapperFieldGetterDocComment`). The port
  returns the error `protoc-gen-php: wrapper type <name> has no field "value" (upstream crashes)`.
- When generation fails, the response has only the error (upstream also lists the files opened
  before it, which protoc and buf ignore).
- A comment line that contains a NUL byte ends at it in the generated doc comment (upstream prints
  the whole line).
