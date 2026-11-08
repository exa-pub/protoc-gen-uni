# bufbuild/grpc-py

C port of `protoc-gen-grpc-py` from
[protoc-gen-grpc-py](https://github.com/bufbuild/protoc-gen-grpc-py) (Apache-2.0):
<https://buf.build/bufbuild/grpc-py>. No Python required. For every file with services it
generates a `<name>_pb_grpc.py` module with typed gRPC servicers and clients (async and sync) for
the [grpcio](https://pypi.org/project/grpcio/) runtime and the messages of
[bufbuild/py](../py/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=bufbuild/grpc-py, uni-version=v0.5.0, io=async]
```

## Versions

| version | upstream sources | Python |
|---------|------------------|--------|
| v0.5.0 | [`src/protoc_gen_grpc_py/__init__.py`](https://github.com/bufbuild/protoc-gen-grpc-py/blob/v0.5.0/src/protoc_gen_grpc_py/__init__.py) at tag `v0.5.0` (the same file as in the PyPI wheel [`protoc-gen-grpc-py==0.5.0`](https://pypi.org/project/protoc-gen-grpc-py/0.5.0/)); [`protobuf-py==0.7.0`](https://pypi.org/project/protobuf-py/0.7.0/) | 3.13 |

The buf.build recipe (`plugins/bufbuild/grpc-py/v0.5.0`) installs `protoc-gen-grpc-py==0.5.0` and
`protobuf-py==0.7.0` on Python 3.13. Its registry configuration adds `map_imports=**:` for
generated SDKs; with protoc-gen-uni it is an ordinary option.

The plugin framework `protobuf.plugin` (options, the generated file with its imports and preamble,
comments, sanitization) and the descriptor model are the upstream library protobuf-py, shared with
bufbuild/py and connectrpc/py:
[`pkg/languages/python/protobuf-py`](../../../pkg/languages/python/protobuf-py/protobuf_py.h).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-grpc-py prints lines with the
framework's `File.print` and runs no formatter: the port prints the same lines.

## Options

All options of upstream, with the framework's parsing (comma-separated `key=value`, keys stripped,
unknown and repeated keys are errors with upstream's traceback):

- `io=sync|async` (default: both): generate only the synchronous or only the asynchronous stubs;
  the value matches a member name or value of `_IOOption` case-insensitively;
- framework options: `no_fmt_off`, `escape_module_with_hash`, `map_imports=<glob>:<package>`
  (repeatable).

## Known differences

- Errors are those of the framework (see [bufbuild/py](../py/README.md#known-differences)): exact
  tracebacks for option errors, the last traceback line for invalid descriptors.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - a non-ASCII character in a method name (`str.lower()` of `_pascal_to_snake_case`); protoc and
    buf only allow ASCII identifiers;
  - the inputs bufbuild/py does not support (a NUL character in a name or the parameter, ...).
