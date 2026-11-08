# Layout

A port mirrors upstream: the same libraries with the same boundaries, the same visible behavior of
the host's standard library. Two exceptions: a third-party formatter (dart_style, black,
prettyplease, …) gets nodes built by the generator instead of text, so its parser is not ported;
and `pkg/` gets only truly shared code (the spec-defined descriptor model, shortest float), while
similar code stays in its upstream library.

```
pkg/
├── core/ pb/ uni/           libc helpers; wire format and schemas; uni-name dispatch
├── proto/                   descriptor model shared by plugin libraries
├── num/                     shortest float
└── languages/<language>/
    ├── std/                 standard library, as far as it shows in output
    ├── <library>/           upstream library under its own name (protogen, libprotoc, heck, …)
    └── formatters/<name>/   formatter layout engine + API to build its nodes
generators/<owner>/<plugin>/ discovered by generators/CMakeLists.txt
├── README.md                upstream, versions, options, differences
├── common/                  code shared by the versions
└── <version>/               CMakeLists.txt: uni_generator(LIBRARIES ...); generator.c (unity build)
tests/{unit,e2e}/, fuzz/     unit tests; golden.sh, check-registry.sh; libFuzzer harness
testdata/
├── proto/<test>/            input buf workspaces
├── checksums/<owner>/<plugin>/
│   ├── <version>.buf.gen.yaml  buf template: test inputs, option groups
│   └── checksums.txt        upstream output sum per version and test
└── gen/                     upstream output for diffs (not committed)
```

Dependencies point down: generator → common → libraries, formatters → std → proto, num → core, pb;
generators never use each other.

## Applying it

- **Host and target.** The host is the language upstream is written in: it decides number printing,
  Unicode, sort order, error texts and libraries. The target is the language it emits: it decides the
  formatter. They can differ (libprotoc in C++ emits Java; nanopb in Python emits C), so
  `pkg/languages/<language>/` serves both roles.
- **Class**, stated in the generator README. *Direct*: upstream prints text and runs no third-party
  formatter, so we print the same text. Go is direct: `go/format.Source` is part of the Go standard
  library, ported in `pkg/languages/go/`. *Formatted*: upstream runs a formatter or a width-aware code
  builder (dart_style, black, dprint, prettyplease, the TypeScript printer, kotlinpoet), so the
  generator builds that formatter's nodes.
- **Formatter nodes** belong to the formatter (there is no shared language tree) and carry the layout
  hints it would take from text: blank lines, author line breaks, magic trailing commas, standalone
  comments. Only the formatter breaks lines. Test it with node fixtures checked against upstream and
  a golden input with long names and comments.
- **A plugin library** (protogen, protoplugin, libprotoc, swift-protobuf, …) gives its family what
  upstream gets from it: option parsing, names, imports, the printer, error texts. It builds on
  `pkg/proto` (descriptors, editions features, comments, json_name); validation and its error texts
  stay in the library.
- **Templates** (text/template, Jinja2) become generator code with the same whitespace. **Host
  errors** (panic, traceback, exception) become `CodeGeneratorResponse.error` with the same text.
- **Complexity:** the arena never frees, so a quadratic algorithm also takes quadratic memory. Keep
  upstream's complexity.
