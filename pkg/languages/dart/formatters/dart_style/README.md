# dart_style 3.1.13, C port (short style, no front end)

The formatter that `protocolbuffers/dart` applies to every generated file:
`DartFormatter(languageVersion: Version(3, 7, 0)).format(source)` (protoc_plugin 25.1.0,
`lib/src/formatter.dart`). The buf.build remote plugin `protocolbuffers/dart:v25.1.0` was built on
2026-09-12 with the Dart SDK 3.13.3 and an unlocked `dart_style: ^3.0.0`, which resolved to
**3.1.13** (published 2026-08-26; 3.1.14 came out on 2026-10-15). Upstream:
<https://pub.dev/packages/dart_style/versions/3.1.13> (BSD-3-Clause, Copyright the Dart project
authors).

Every generated file starts with `// @dart = 3.3`. dart_style formats code whose language version
(the `@dart` comment overrides `languageVersion`) is below 3.7 with the **short** style, the
pre-3.7 formatter in `lib/src/short/`, so only that style is ported. A unit without the comment
(or with 3.7 or later) would be formatted with the tall style and fails here.

## Layout

The formatter front end (scanner, parser, analyzer) is not ported (docs/LAYOUT.md): the generator
builds the nodes directly.

- `dart_style.h` is the node API: tokens, the unresolved AST of package:analyzer 14 (`NodeKind`
  lists the fields of each kind) and constructors for the node kinds generated code contains
  (`ds_class`, `ds_method`, `ds_call`, `ds_cascade`, ...).
- `nodes.c` (not a port) holds the constructors and the linearizer `ds_linearize`, which walks the
  tree in source order, links the tokens, sets node begin/end tokens and parents and writes the
  source text. The text is what the generator's IndentingWriter would have printed: it is the
  output of `generate_kythe_info`, and the formatter reads its line structure (blank lines,
  comment positions) like dart_style reads the source. Layout hints carried by the nodes:
  newlines before a node or token with an indentation relative to the enclosing body (`ds_nl`,
  `ds_tnl`), comments before a node (`ds_comment`), the vertical layout of collections, argument
  and parameter lists (`DS_VERTICAL`), trailing commas (`DS_TRAILING_COMMA`, `ds_comma`).
  Declarations, members and statements go on their own lines at the body's indentation (+2 per
  level); everything else on one line with single spaces. The shapes are those the analyzer's
  parser produces for the same text (`a.b` of two identifiers is a PrefixedIdentifier, `m(...)`
  and `prefix.Type(...)` are MethodInvocations, cascade sections have no target, ...).
- `short/*.c` mirror `lib/src/short/` (`chunk.dart` + `nesting_level.dart` + `nesting_builder.dart`
  → `chunk.c`; `rule/*.dart` + `constants.dart` → `rule.c`; `line_splitting/*.dart` →
  `line_splitter.c`; `line_writer.dart`, `chunk_builder.dart`, `source_visitor.dart`,
  `argument_list_visitor.dart`, `call_chain_visitor.dart`), same function order and names
  (`ChunkBuilder.startSpan` → `cb_startSpan`). The source visitor keeps the visit methods of the
  node kinds generated code contains; others fail with "unsupported AST node". `ast_extensions.c`
  and `dart_formatter.c` (with `string_compare.dart`) port the shared files.
- `dart_style.c` is a unity build; only the API of `dart_style.h` is exported. Conventions:
  `internal.h`.

Not ported: the tall style, the front end, selections, `// dart format width=` (tall only),
`TrailingCommas` (tall only), experiment flags.

Known deviations: trees nested deeper than 2000 levels are rejected instead of being limited by the
Dart VM stack; error messages differ.

## Tests

- `tests/format_test.c` (`unit/dart_style_format`): node fixtures with the source text the
  linearizer writes and the output of dart_style 3.1.13 on the Dart SDK 3.13.3 for that text
  (long calls, nested type arguments, closures, conditionals, UTF-16 widths, layout hints).
- The golden tests of `protocolbuffers/dart` check every generated file, including
  `testdata/proto/dart-long` (names, defaults and comments longer than the page width) and the
  unformatted files of the `kythe` option group (the linearized text).

A new dart_style version: this directory implements the newest version in use, so port the upstream
diff of `lib/src/short/` file by file and rerun the tests against that version. A part that differs
for an older version a generator still needs gets a versioned file (see docs/RULES.md).
