# community/nipunn1313-mypy-grpc

C port of `protoc-gen-mypy_grpc` from [mypy-protobuf](https://github.com/nipunn1313/mypy-protobuf)
(Apache-2.0): <https://buf.build/community/nipunn1313-mypy-grpc>. No Python required. For every
file it generates `<name>_pb2_grpc.pyi`, type stubs for the `<name>_pb2_grpc.py` module of
grpcio-tools (`grpc/python`): the stubs, async stubs and servicers of the services.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=community/nipunn1313-mypy-grpc, uni-version=v5.1.0, only_async]
```

## Versions

| version | upstream sources | Python |
|---------|------------------|--------|
| v5.1.0 | [`mypy_protobuf/main.py`](https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/mypy_protobuf/main.py), [`extensions.proto`](https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/proto/mypy_protobuf/extensions.proto) of `mypy-protobuf==5.1.0`, `protobuf==6.33.1` | 3.13 |

The buf.build recipe (`plugins/community/nipunn1313-mypy-grpc/v5.1.0`) installs
`mypy-protobuf==5.1.0` and `protobuf==6.33.1` on Python 3.13 and runs `protoc-gen-mypy_grpc`, the
`grpc()` entry point of `main.py`. The rest of `main.py` (descriptors, imports, the `PkgWriter`
lines, comments, deprecation messages) is shared with
[community/nipunn1313-mypy](../nipunn1313-mypy/README.md) in the library
[`pkg/languages/python/mypy-protobuf`](../../../pkg/languages/python/mypy-protobuf/mypy_protobuf.h).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). `PkgWriter` builds the `.pyi` text line
by line and runs no formatter: the port prints the same lines.

## Options

All options of upstream. Like upstream, an option is enabled when its name occurs anywhere in the
parameter:

- `only_sync`, `only_async`: generate only the synchronous or only the asynchronous stubs and
  servicer types (default: both). Both together fail with upstream's traceback
  (`ValueError: Cannot specify both only_sync and only_async`). The upstream README names them
  `sync_only` and `async_only`; like upstream, those names have no effect;
- `generate_concrete_servicer_stubs`: servicers without `abc.ABCMeta` and `@abstractmethod`;
- `readable_stubs`: imports without the `_` alias prefix;
- `use_default_deprecation_warnings`: the default `@deprecated` messages instead of the comments;
- `relax_strict_optional_primitives` has no effect on the gRPC stubs; `quiet` only silences
  messages on stderr.

The request is read like upb (protobuf 6.33.1) reads it, with `mypy_protobuf/extensions.proto`
registered, as in community/nipunn1313-mypy.

## Known differences

- Errors other than the `only_sync`/`only_async` conflict go to `CodeGeneratorResponse.error`,
  prefixed with `protoc-gen-mypy_grpc: `, with the last line of upstream's traceback (for example
  `KeyError: '.pkg.Missing'`). Upstream prints the whole traceback on stderr and exits with
  status 1.
- The `Writing mypy to ...` messages that upstream prints to stderr are not printed.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`), as in
  [community/nipunn1313-mypy](../nipunn1313-mypy/README.md#known-differences):
  - a deprecation comment of a deprecated service or method whose validity as Python the port
    cannot decide (only comments that contain `"""` or `\N{`, without
    `use_default_deprecation_warnings`);
  - a NUL byte, or a string that is not valid UTF-8, in a name, package, dependency,
    `file_to_generate`, `input_type` or `output_type` that the output uses; protoc and buf never
    produce one.
- A request with messages nested deeper than 100 levels fails with
  `protoc-gen-uni: parse request: ...` on stderr; upb accepts one more level.
