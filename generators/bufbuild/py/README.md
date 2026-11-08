# bufbuild/py

C port of `protoc-gen-py` from [protobuf-py](https://github.com/bufbuild/protobuf-py)
(Apache-2.0): <https://buf.build/bufbuild/py>. No Python required. It generates `<name>_pb.py`
modules for the protobuf-py runtime and the `__init__.py` files of their packages.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=bufbuild/py, uni-version=v0.7.0, init_files=false]
```

## Versions

| version | upstream sources | Python |
|---------|------------------|--------|
| v0.7.0 | PyPI [`protoc-gen-py==0.7.0`](https://pypi.org/project/protoc-gen-py/0.7.0/) (`protoc_gen_py/__init__.py`), [`protobuf-py-pure==0.7.0`](https://pypi.org/project/protobuf-py-pure/0.7.0/) (`protobuf/plugin/*`, `_descriptors.py`, `_file_registry.py`, `_registry.py`, `_bootstrap.py`, `_sanitization.py`, `_names.py`, `_wire/*`, `_from_binary.py`, `_to_binary.py`, `wkt/google/protobuf/descriptor_pb.py`) | 3.13 |

The buf.build recipe (`plugins/bufbuild/py/v0.7.0`) installs `protoc-gen-py==0.7.0` and
`protobuf-py==0.7.0` on Python 3.13; `protobuf-py-ext`, a Rust extension that speeds up
serialization, does not change the output.

The plugin framework `protobuf.plugin` and the runtime parts it relies on (descriptor model,
binary format, sanitization) are an upstream library shared with connectrpc/py and
bufbuild/grpc-py: [`pkg/languages/python/protobuf-py`](../../../pkg/languages/python/protobuf-py/protobuf_py.h).
The Python semantics (`str.rstrip`, `str.splitlines`, `sorted`, `repr` of bytes and floats,
`int()`/`float()` of the default values, `sys.stdlib_module_names`, `hashlib.sha256`) are in
[`pkg/languages/python/std`](../../../pkg/languages/python/std/python.h). Each `.c` file names the
upstream file it ports.

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-py prints lines with the
framework's `File.print` and runs no formatter: the port prints the same lines.

## Options

All options of upstream, with its parsing (comma-separated `key=value`, keys stripped, a bare key
is `true` for a bool, unknown keys and repeated keys are errors):

- `init_files` (default `true`): generate `__init__.py` for every package directory;
- framework options: `no_fmt_off`, `escape_module_with_hash`, `map_imports=<glob>:<package>`
  (repeatable; `**`, `*` and a trailing `/` like upstream; an empty package maps to the canonical
  module path).

`google/protobuf/descriptor.proto` itself is generated like upstream's `_generate_boot`: the
descriptor as `repr()` of protobuf-py's message classes, whose names (class qualnames, attribute
and enum member names, unset values) the library embeds.

The embedded descriptor is the `FileDescriptorProto` without `source_code_info`, serialized like
protobuf-py's `to_binary` (fields in declaration order of descriptor.proto, unknown fields grouped
by number); the request is read with protobuf-py's rules (strings must be valid UTF-8, unknown
values of closed enums become unknown fields).

## Known differences

- Errors go to `CodeGeneratorResponse.error` like upstream, which returns
  `traceback.format_exc()`. The traceback is exact for the option errors (unknown option, a value
  that is not a bool, a repeated option, a malformed `map_imports`). For other errors (invalid
  descriptors: an unresolvable type or dependency, a default value that does not parse, an
  unsupported edition) the error is the last line of the traceback, e.g.
  `ValueError: could not find message: .foo.Bar`. The `UnicodeDecodeError` and
  `UnicodeEncodeError` texts of a bytes default value (`_c_unescape`) and of a string that is not
  UTF-8 follow CPython 3.13, positions included.
- A request that upstream fails to parse (an exception outside the `try` of `run`) ends the
  plugin with status 1 like upstream, with the exception on stderr instead of a traceback.
- The `__init__.py` files are in sorted order in the response; upstream iterates a Python `set`,
  so their order changes between runs. The files are the same.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - a non-ASCII character where upstream applies `str.upper()` or `str.isdigit()` (a field name
    after `_`, an enum name, the start of an enum value name after the stripped prefix) or Unicode
    digits in an integer or float default value; protoc and buf only allow ASCII identifiers and
    write ASCII defaults;
  - a `\N{name}` escape in a bytes default value (no Unicode name database);
  - a NUL character in a name, type name, dependency or the parameter; protoc and buf never
    produce one.
- Invalid descriptors that protoc and buf never produce can differ: a symbol defined twice in
  different files (upstream's registry re-adds the dependencies of each file, so an earlier
  definition can win again; here the last one added wins), and two equal declarations in one
  scope (upstream finds the comments of an element with `list.index`, i.e. by equality).
- A request with a field that overruns its enclosing message is rejected by pkg/pb before it
  reaches the generator (status 1); protobuf-py reads such a field from the enclosing data.
