# community/nipunn1313-mypy

C port of `protoc-gen-mypy` from [mypy-protobuf](https://github.com/nipunn1313/mypy-protobuf)
(Apache-2.0): <https://buf.build/community/nipunn1313-mypy>. No Python required. It generates
`<name>_pb2.pyi` type stubs for the Python protobuf runtime.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=community/nipunn1313-mypy, uni-version=v5.1.0, readable_stubs]
```

Options are the same as upstream: [mypy-protobuf README](https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/README.md).
Like upstream, an option is enabled when its name occurs anywhere in the parameter.

## Versions

| version | upstream sources | Python |
|---------|------------------|--------|
| v5.1.0 | [`mypy_protobuf/main.py`](https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/mypy_protobuf/main.py), [`extensions.proto`](https://github.com/nipunn1313/mypy-protobuf/blob/v5.1.0/proto/mypy_protobuf/extensions.proto), `WKTBASES` of protobuf 6.33.1 | 3.13 |

`generator.c` ports the `main()` entry point of `main.py`. The rest of `main.py` is the library
[`pkg/languages/python/mypy-protobuf`](../../../pkg/languages/python/mypy-protobuf/mypy_protobuf.h),
shared with `protoc-gen-mypy_grpc` ([community/nipunn1313-mypy-grpc](../nipunn1313-mypy-grpc/README.md)).
The Python semantics the output depends on (`str.rstrip`, `sorted`, `repr`, `ast.parse` of the
deprecation messages) are in [`pkg/languages/python/std`](../../../pkg/languages/python/std/python.h).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-mypy builds the `.pyi` text
line by line (`PkgWriter._write_line`) and runs no formatter: the port prints the same lines.

All options of `protoc-gen-mypy` are implemented: `readable_stubs`,
`relax_strict_optional_primitives`, `use_default_deprecation_warnings` and `quiet` (stderr only).
`generate_concrete_servicer_stubs`, `only_sync` and `only_async` only affect
`protoc-gen-mypy_grpc` and are ignored, as upstream does.

The request is read like upb (protobuf 6.33.1) reads it, with `mypy_protobuf/extensions.proto`
registered: a malformed `(mypy_protobuf.options)` or one of its strings that is not valid UTF-8,
anywhere in the request, fails with upstream's `DecodeError`; a comment or the parameter that is
not valid UTF-8 fails with upstream's `TypeError`; a map entry chain deeper than Python's recursion
limit fails with `RecursionError` at the same depth.

## Known differences

- Errors go to `CodeGeneratorResponse.error`, prefixed with `protoc-gen-mypy: `. Upstream raises
  a Python exception (a traceback on stderr) and exits with status 1.
- The warnings that upstream prints to stderr (`Writing mypy to ...`, deprecated legacy options,
  unparsable deprecation comments) are not printed.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - a deprecation comment whose validity as Python the port cannot decide. Upstream uses the
    comment as the `@deprecated` message when `"""<comment>"""` parses with `ast.parse`. The port
    decides it exactly for string literals (with prefixes, escapes, comments, `;`, line
    continuations) and for the syntax errors the tokens prove (invalid characters, unterminated
    strings, unbalanced brackets, invalid escapes, a name next to a string, mixed bytes and str);
    other Python code (`a""" + """b`), f-string replacement fields and `\N{name}` escapes (there is
    no Unicode name database) are not supported. Only comments that contain `"""` or `\N{` can
    reach this, and only on deprecated elements without `use_default_deprecation_warnings`;
  - a NUL byte in a message or enum name or a package of any file, or in another name, syntax,
    type name, extendee, dependency, `file_to_generate` or mypy_protobuf option value that the
    output uses (upstream prints it); protoc and buf never produce one;
  - a string of that list that is not valid UTF-8 (upb returns it as `bytes`, which upstream
    prints as `b'...'` or fails on with a `TypeError`); protoc and buf never produce one;
  - a field or extension name with non-ASCII characters, whose `str.upper()` needs Unicode case
    mapping; protoc and buf only allow ASCII identifiers.
- A request with messages nested deeper than 100 levels fails with
  `protoc-gen-uni: parse request: ...` on stderr; upb accepts one more level (a file with 99 nested
  messages). protoc and buf never produce one.
