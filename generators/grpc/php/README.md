# grpc/php

C port of `grpc_php_plugin` from [gRPC](https://github.com/grpc/grpc) (Apache-2.0):
<https://buf.build/grpc/php>. No protoc required. Generated code needs the `grpc/grpc` Composer
package (or the `grpc` PECL extension), next to the message classes of
[protocolbuffers/php](../../protocolbuffers/php/README.md) v36.2.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/php
    opt: [uni-name=protocolbuffers/php, uni-version=v36.2]
  - local: protoc-gen-uni
    out: gen/php
    opt: [uni-name=grpc/php, uni-version=v1.85.1, generate_server]
```

One file per service: the client stub `<Namespace>/<Service>Client.php` (a `\Grpc\BaseStub`), and
with `generate_server` the server base class `<Namespace>/<Service>Stub.php`. The namespace is
`php_namespace` or the package with each part capitalized. See the
[gRPC PHP basics tutorial](https://grpc.io/docs/languages/php/basics/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints PHP text with the
legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port prints the same text.

## Options

- `class_suffix=<suffix>`: the class name suffix instead of `Client` (the server class gets
  `<suffix>Stub` instead of `Stub`).
- `generate_server`: also write the server base classes (any value).

Any other option fails generation with `<file>: unsupported options: <name>` (for the first file
with services), as upstream.

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v1.85.1 | [`src/compiler`](https://github.com/grpc/grpc/tree/v1.85.1/src/compiler) (`php_plugin.cc`, `php_generator*`) | protobuf 35.1 ([`MODULE.bazel`](https://github.com/grpc/grpc/blob/v1.85.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with [`libprotoc_35.1.c`](../../../pkg/languages/cpp/libprotoc/libprotoc_35.1.c) |

`generator.c` ports `php_plugin.cc`, `php_generator.c` ports `php_generator.{h,cc}` and
`php_generator_helpers.h`. The comment helpers of `generator_helpers.h` and
`proto_parser_helper.cc` (`grpc_plugin_support`, shared by the grpc C++ plugins) are
[`pkg/languages/cpp/grpc`](../../../pkg/languages/cpp/grpc/grpc.h); `GeneratedClassName` of
`compiler/php/names.cc` is [`php_names.c`](../../../pkg/languages/cpp/libprotoc/php_names.h) of
libprotoc. The remote plugin is built with Bazel from the v1.85.1 tag
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/php/v1.85.1)), with
protobuf 35.1 and `GRPC_PROTOBUF_EDITION_SUPPORT`: `supported_features` is
`FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS`, editions PROTO2 to 2024.

Behaviour kept from upstream:

- The PHP reserved words of protobuf 35.1 (80) lack `mixed`, `never` and `object`: a message
  named so gets no `PB` prefix, unlike protocolbuffers/php v36.2 (`LpVersion.php_unreserved_names`
  of libprotoc 35.1).
- The type names keep the case of the message (only the first letter is upper-cased) and a nested
  message keeps the prefix of its reserved containing types (`PBClass\Inner`).
- File comments are those above the `syntax` (or `edition`) statement, printed raw: `$` in them
  stays doubled (`$$`); comments on services and methods go through `Print` and keep a single `$`.
  `*/` in comments becomes `&#42;/`.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason (see [grpc/ruby](../ruby/README.md)).
- When generation fails, the response has only the error. Upstream also lists the files opened
  before the error, which protoc and buf ignore when there is an error.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the element.

- `uninterpreted_option` in any options message (libprotoc): the option interpreter of protoc is
  not ported. protoc and buf never produce it.
- A `php_namespace` or `php_class_prefix` that contains a NUL byte: upstream prints the whole
  string, NUL included, into the generated file and its name; the port's strings end at NUL.
