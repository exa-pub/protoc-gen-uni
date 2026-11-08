# community/nanopb

C port of `protoc-gen-nanopb` from [nanopb](https://github.com/nanopb/nanopb) (zlib license):
<https://buf.build/community/nanopb>. No Python required. It generates `<name>.pb.h` and
`<name>.pb.c` for the nanopb C runtime.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/c
    opt: [uni-name=community/nanopb, uni-version=v0.4.91, -s, "max_size:32"]
```

Options are the same as upstream: the command line of `nanopb_generator.py`
([reference](https://jpa.kapsi.fi/nanopb/docs/reference.html#nanopb_generator.py)), split at `,`
with `shlex` (or at spaces with `shlex.split` when the parameter has no `,` and contains ` -`),
and the `(nanopb)` options of [`nanopb.proto`](https://github.com/nanopb/nanopb/blob/0.4.9.1/generator/proto/nanopb.proto).

## Versions

| version | upstream sources | Python |
|---------|------------------|--------|
| v0.4.91 | PyPI `nanopb==0.4.9.1`: [`generator/nanopb_generator.py`](https://github.com/nanopb/nanopb/blob/0.4.9.1/generator/nanopb_generator.py), [`generator/proto/nanopb.proto`](https://github.com/nanopb/nanopb/blob/0.4.9.1/generator/proto/nanopb.proto); `text_format` of python-protobuf (the remote image does not pin it) | 3.13 |

Each `.c` file names the upstream code it ports:

- `model.c`: `Names`, the `NanoPBOptions` message with its `HasField`/`CopyFrom`/`MergeFrom`
  semantics, the descriptor view (as python-protobuf parses the request: closed enums, defaults);
- `nanopb_generator.c`: naming styles, `EncodedSize`, `Enum`, `Field`, `ExtensionRange`,
  `ExtensionField`, `OneOf`, `Message`;
- `protofile.c`: `Message.default_value`, `MangleNames`, `ProtoFile` (parsing, dependencies),
  `sort_dependencies`;
- `files.c`: `generate_header`, `generate_source`, `parse_file`, `process_file`;
- `plugin.c`, `settings.c`: `optparser`, `process_cmdline`, `main_plugin` and the `-s` settings;
- `tb.c`: the frames of the Python tracebacks.

The Python semantics the output depends on (`shlex`, `optparse`, `%` formatting, `str.strip`,
`sorted`, `repr`, `int()`/`float()` of the default values) are in
[`pkg/languages/python/std`](../../../pkg/languages/python/std/python.h), `text_format.Merge` of
python-protobuf in [`pkg/languages/python/protobuf`](../../../pkg/languages/python/protobuf/python_protobuf.h).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). `nanopb_generator.py` builds the files
from string formatting and runs no formatter: the port prints the same text.

All command line options are implemented: `-x`, `-e`, `-H`, `-S`, `-Q`, `-L` (Python `%`
formatting of the format strings, with its `TypeError`/`ValueError`), `--strip-path`,
`--cpp-descriptors`, `-T`, `-q`, `-v`, `-s` (python-protobuf `text_format` into
`NanoPBOptions`), `--protoc-insertion-points`, `-C`, `-V` (an empty response, like upstream's
`sys.exit(0)`), `-h`/`--help` (the help text as the error); `-f`, `-I`, `-D`, `--protoc-opt` and
`--[no-]error-on-unmatched` are accepted and change nothing, because upstream reads `.options`
files from the image's file system, which has none. All `NanoPBOptions` fields are implemented,
from `-s` and from the `(nanopb_fileopt)`, `(nanopb_msgopt)`, `(nanopb_enumopt)` and `(nanopb)`
extensions.

Errors are upstream's: an uncaught Python exception prints everything written to stderr so far
(`Breaking circular dependency ...` and the other warnings) and the traceback, with the file
paths and line numbers of the remote image (Debian's Python 3.13 in `/usr/lib/python3.13`, the
virtual environment in `/app`), and exits with status 1; an `optparse` error prints the usage and `protoc-gen-nanopb: error: ...` and
exits with status 2. The port returns that text as `CodeGeneratorResponse.error`. Groups
(`NotImplementedError: 10`), `FT_STATIC` without `max_size`, duplicate C names (`KeyError` in
`sort_dependencies`), invalid `-s` settings (`text_format.ParseError`), invalid default values and
the other raise sites are covered.

The golden tests ([template](../../../testdata/checksums/community/nanopb/v0.4.91.buf.gen.yaml))
cover the default output, `-C --strip-path`, the extension options with `--cpp-descriptors`, `-s`
settings with `--protoc-insertion-points`, `-Q`/`-L`, `mangle_names`/`long_names`, the
`(nanopb)` options of `testdata/proto/nanopb-options`, groups (`proto2`, `naming`) and the errors
of `-s`, `optparse` and `shlex`.

## Known differences

- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - `-t`/`--timestamp`: upstream writes the time of generation into the files;
  - the error of a failing run with `-v`, whose stderr log (the `NanoPBOptions` of every element
    and the protobuf version) is not reproduced, or with more than 16 MiB of warnings;
  - an abbreviated `--help` (`--hel`, `-qh`): upstream prints the help on stdout, which is not a
    response;
  - non-ASCII characters in names that are case-converted (`-C`) or in a file or package name of
    a generated file (`make_identifier` uses Unicode `str.upper` and `isalnum`), and in `-s`
    names; protoc and buf only allow ASCII identifiers;
  - a NUL character in a name or string of the request or in the parameter; protoc and buf never
    produce one;
  - `\u`, `\U`, `\N{...}` escapes and escapes the codecs reject in `-s` strings;
  - an import cycle (upstream walks the imports forever), or imports whose walk would take
    upstream more than 20,000,000 steps (it adds each file once per import path, and again for
    every file generated);
  - a default value whose enum short name is also the long name of another enum value;
  - encoded or struct sizes beyond 2^120, more than 4,000,000 size symbols, initializers of
    `max_count` elements larger than 64 MiB in total, or message dependencies too deep or too many
    to check (upstream: `RecursionError`, `MemoryError` or no end). The recursion check counts
    the calls upstream makes (it walks every dependency path, without a memo) and stops at
    20,000,000.
- A request that is not valid protobuf or has a string that is not valid UTF-8 fails with a short
  `DecodeError` text instead of the python-protobuf traceback. Exceptions raised inside
  python-protobuf other than those of `text_format` and the field range checks are approximated
  in the same way (a one-line exception text).
- `RecursionError` is raised at approximately, not exactly, the nesting depth where CPython's
  recursion limit would be reached.
- The `Circular dependency in messages: ...` warning lists the names in message order; upstream
  prints a Python `set`, whose order depends on the hash seed of the process.
