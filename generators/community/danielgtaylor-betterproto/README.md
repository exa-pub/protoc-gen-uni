# community/danielgtaylor-betterproto

C port of `protoc-gen-python_betterproto` from [betterproto](https://github.com/danielgtaylor/python-betterproto)
(MIT, Copyright (c) 2019 Daniel G. Taylor): <https://buf.build/community/danielgtaylor-betterproto>.
No Python required. It generates dataclass-based Python 3.7 messages and grpclib service stubs,
one `<package>.py` per protobuf package plus the `__init__.py` files of their directories.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/python
    opt: [uni-name=community/danielgtaylor-betterproto, uni-version=v1.2.5]
```

The plugin has no options: like upstream, the parameter is ignored.

## Versions

| version | upstream sources | Python, libraries |
|---------|------------------|-------------------|
| v1.2.5 | [`betterproto/plugin.py`](https://github.com/danielgtaylor/python-betterproto/blob/v1.2.5/betterproto/plugin.py), [`casing.py`](https://github.com/danielgtaylor/python-betterproto/blob/v1.2.5/betterproto/casing.py), [`templates/template.py.j2`](https://github.com/danielgtaylor/python-betterproto/blob/v1.2.5/betterproto/templates/template.py.j2) | Python 3.11.4, black 24.3.0, Jinja2 3.1.6, stringcase 1.2.0, protobuf 4.23.2 (the pins of the buf.build image) |

**Class: formatted** ([LAYOUT.md](../../../docs/LAYOUT.md)). Upstream renders a Jinja2 template and
formats the text with `black.format_str(..., mode=FileMode(target_versions={PY37}))`. The port
builds the blib2to3 tree of the rendered text directly (`template.c`: each statement with the
prefix that the template's blank lines and comment lines give its first token) and formats it
with the black port in [`pkg/languages/python/formatters/black`](../../../pkg/languages/python/formatters/black/README.md).
Also used: `textwrap.wrap` (CPython 3.11) and `str` semantics from
[`pkg/languages/python/std`](../../../pkg/languages/python/std/python.h), the case conversions of
stringcase 1.2.0 from [`pkg/languages/python/stringcase`](../../../pkg/languages/python/stringcase/stringcase.h).

Files: `plugin.c` ports `plugin.py` and `casing.py`, `template.c` the template, `generator.c` the
driver (`generate_code`: grouping by package, file names, `__init__.py` files).

Upstream behaviour kept as is:

- every file of the request except package `google.protobuf` is generated, not only
  `file_to_generate`; files of one package are merged, and a file without package becomes the
  module of its path;
- `supported_features` is not set, so buf warns about proto3 `optional` (generated like a
  oneof field) and rejects editions;
- the quirks of the generated code: `type_name.lstrip(package)` strips characters, not a prefix;
  imported types are relative imports of their full package (`from .google import protobuf`);
  nested types are flattened by concatenating names; a map field is recognized by the name of
  its entry type; comments are wrapped at 79 columns after their newlines are removed (lines run
  together), a nested enum is looked up at the path of its parent's `enum_type` field;
- the `__init__.py` files are always emitted (upstream checks for them in its working directory,
  which is empty in the buf.build image).

## Known differences

- Errors go to `CodeGeneratorResponse.error`, prefixed with `protoc-gen-python_betterproto: `
  and with the Python exception text (`NotImplementedError: Unknown type 10` for groups,
  `NotImplementedError: Client streaming not yet supported`, `IndexError: ...`). Upstream raises
  the exception (a traceback on stderr) and exits with status 1. When black cannot parse the
  rendered text (an enum or service with no body, a keyword used as a name), the port fails with a
  `black.parsing.InvalidInput` message without black's line and column.
- The `Writing <file>` lines that upstream prints to stderr are not printed; the `__init__.py`
  files come in a fixed order (upstream: the order of a Python set).
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - a name used in the generated code (message, enum, value, field, oneof, service, method,
    package, type name) that is not an ASCII identifier; protoc and buf never produce one;
  - a string of the request that the output uses and that is not valid UTF-8 or contains NUL;
  - a file name with a control character, or an output directory that `os.makedirs` would fail
    to create (absolute path, `..`, a component over 255 bytes);
  - a comment that becomes a docstring containing `"""` or ending with a backslash that escapes
    the closing quotes (black usually cannot parse the result: upstream fails too), and a comment
    line that black reads as a directive (`# fmt: off`, `# fmt: skip`, `# yapf: disable`).
