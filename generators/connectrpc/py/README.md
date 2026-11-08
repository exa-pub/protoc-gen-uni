# connectrpc/py

C port of `protoc-gen-connectrpc` from [connect-py](https://github.com/connectrpc/connect-py)
(Apache-2.0): <https://buf.build/connectrpc/py>. No Python required. It generates
`<name>_connect.py` modules with the Connect clients, the ASGI/WSGI applications and the service
protocols of every file with services, for the protobuf-py runtime (default) or for
`google.protobuf` (`protobuf=google`).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=connectrpc/py, uni-version=v0.13.0, io=async]
```

## Versions

| version | upstream sources | Python |
|---------|------------------|--------|
| v0.13.0 | [`protoc-gen-connectrpc/protoc_gen_connectrpc/__init__.py`](https://github.com/connectrpc/connect-py/blob/v0.13.0/protoc-gen-connectrpc/protoc_gen_connectrpc/__init__.py) at tag `v0.13.0` (PyPI `protoc-gen-connectrpc==0.13.0`); plugin framework PyPI `protobuf-py==0.7.0` | 3.13 |

The buf.build recipe (`plugins/connectrpc/py/v0.13.0`) installs `protoc-gen-connectrpc==0.13.0`
and `protobuf-py==0.7.0` on Python 3.13.16; its registry configuration adds `map_imports=**:`
for generated SDKs (not applied by `buf generate`).

The plugin framework `protobuf.plugin` (option parsing, the generated file with its imports and
preamble, `map_imports`, comments, the descriptor model) is the upstream library shared with
bufbuild/py: [`pkg/languages/python/protobuf-py`](../../../pkg/languages/python/protobuf-py/protobuf_py.h).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-connectrpc prints lines with
the framework's `File.print` and runs no formatter: the port prints the same lines.

## Options

All options of upstream, with the framework's parsing (comma-separated `key=value`, keys
stripped, enum values matched case-insensitively by member name or value, unknown keys and
repeated keys are errors):

- `protobuf=py|google` (default `py`): the message classes of protobuf-py (`<name>_pb`) or of
  `google.protobuf` (`<name>_pb2`, with `google_protobuf_codecs` and a client constructor);
- `io=sync|async` (default: both): generate only the synchronous or the asynchronous stubs;
- framework options: `no_fmt_off`, `escape_module_with_hash`, `map_imports=<glob>:<package>`
  (repeatable).

## Known differences

- Errors go to `CodeGeneratorResponse.error` like upstream (`traceback.format_exc()`): exact for
  the option errors, the last line of the traceback for invalid descriptors (see
  [bufbuild/py](../../bufbuild/py/README.md)), whose reading of the request this port shares.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`): a non-ASCII
  character in a method name (upstream applies `str.lower()` to the snake_case name; protoc and
  buf only allow ASCII identifiers), and the cases listed for bufbuild/py.
