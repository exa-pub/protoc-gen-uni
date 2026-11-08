# Go 1.27.1 standard library, C port

The formatting that `compiler/protogen` applies to every generated Go file: `go/parser`
(ParseComments), an inserted import block and `go/printer` with
`Config{Mode: TabIndent | UseSpaces, Tabwidth: 8}`, and the parts of `unicode` and `strconv` that
generators use. The output depends on the standard library of the toolchain that built the plugin,
so this directory implements the newest Go release in use, go1.27.1 (sources:
<https://go.dev/dl/go1.27.1.src.tar.gz>; BSD-3-Clause, Copyright The Go Authors; go1.27.2 and
go1.27.3 are identical in these packages), and an older release where it differs:

| release | used by | what differs from go1.27.1 |
|---------|---------|----------------------------|
| go1.26.5 (`go1.26.5/`, library `go_std_go1_26_5`) | protocolbuffers/go v1.36.12, community/mfridman-go-json (Go 1.26.1, same output), community/planetscale-vtprotobuf (Go 1.21.6, same output for this plugin) | Unicode 15.0.0 instead of 17.0.0 (unicode predicates, `strconv.IsPrint`); `go/printer`: `math.Log`/`math.Exp` of amd64 instead of `log2ish`/`exp2ish` in the alignment heuristic, multi-line composite literals count in `indentList` (return statements, go.dev/issue/7195), doc comments before an identifier are reformatted (go.dev/issue/52605); `go/parser`: "method must have no type parameters"; `go/doc/comment`: no `uuid` standard package |

A generator built with an older release links its library and selects it for its request:
`go_std_use(&go_std_go1_26_5)` before and `go_std_use(NULL)` after (`go_std.h`). The release is a
`go_std_release` (`release.h`): its data (the rune ranges where its Unicode tables differ, generated
by `go1.26.5/mkdiff.py`; `math.Log`/`math.Exp` in `go1.26.5/math.c`) and its minor version, which
the code that differs only by a condition tests in place (`go_std_rel->minor < 27`).

## Layout

Files mirror Go's `src/` (`go/printer/nodes.go` -> `go/printer/nodes.c`, same function order and
names: `pkg_Name`, methods `pkg_Type_Method`). `go_std.c` is a unity build that includes every
`.c`; all functions are `static` and only the API in `go_std.h` is exported
(`go_std_format_content`, `go_std_format_source` (`go/format.Source`), `go_std_decl_idents`, `go_std_forward_decls` (the declaration walk of
protoc-gen-go's public imports), `go_std_quote`, `go_std_quote_rune`, `go_std_format_float` (`strconv.FormatFloat`),
`go_std_is_letter`/`is_digit`/`is_upper`, `go_std_to_upper`/`to_lower` (`unicode`),
`go_std_is_keyword` (`token`), `go_std_parse_int`/`parse_uint`/`parse_bool` (`strconv`),
`go_std_url_parse`/`go_std_url_string` (`net/url`), `go_std_sort_slice` (`sort.Slice`) and
`go_std_use` (the release)).
Conventions: `internal.h`.

Ported: `go/token`, `go/scanner`, `go/ast` (types, Pos/End, Inspect, SortImports), `go/parser` (full grammar),
`go/printer`, `go/doc/comment` (Parse + Printer.Comment), `go/build/constraint` (what the printer
needs), `text/tabwriter`, `strconv` (quoting; `FormatFloat` for `e`/`f`/`g` from
`internal/strconv/decimal.go` and `ftoa.go`, always through the multiprecision path, which yields the
same digits as Go's Ryū fast path), `ParseInt`/`ParseUint`/`ParseBool` (`strconv/atoi.c`), `net/url` (`Parse`, `URL.String`),
`sort.Slice` (pdqsort, `sort/zsortfunc.c`), `unicode` predicates and case mappings (tables generated
by `unicode/mktables.py`, `strconv/mkisprint.py` from the Go sources), `unicode/utf8`, a `strings`
subset. Not ported (marked `not ported` in the code): object resolution, parser trace mode,
`File.GoVersion`, `printer.CommentedNode`, HTML/Markdown/Text output of `go/doc/comment`.

Known deviations: the parser stops with Go's "exceeded max nesting depth" error when it has used
512 KiB of C stack (Go allows 100000 levels of nesting); for go1.26.5, `math.Exp`/`math.Log` in the
printer's alignment heuristic reproduce Go's amd64 (FMA) assembly bit for bit (assumption: the
buf.build remote plugin runs on amd64; arm64 Go could differ in rare alignment decisions; go1.27
uses `log2ish`/`exp2ish`, the same on every architecture).

## Tests

- `tests/*_test.c`: unit tests (cases from Go's own tests, `tests/testdata/comment` are Go's
  `go/doc/comment` testdata files).
- `testdata/make-corpus.sh [out]` records a corpus into `build/gofmt-corpus` (not committed; needs
  git, patch, go, buf and network access). `cases/`: the former Go implementation of
  protoc-gen-uni is extracted from git revision `c9b2e00` (`UNI_GO_REV` overrides) and patched with
  `testdata/protogen-dump.patch`, so that its `protogen` records every file it formats
  (`UNI_DUMP_GOFMT=<dir>`: input, imports, gofmt output, declaration identifiers); it then runs its
  protocolbuffers/go golden tests and its binary via buf over every `testdata/proto/*` with several
  option sets. `gosrc/`: Go 1.26.5's own sources formatted by the Go 1.26.5 `go/printer` with the
  protogen configuration. `tests/corpus_test.c` (ctest `unit/go_std_corpus`,
  `unit/go_std_corpus_gosrc`, `unit/go_std_corpus_mutate`; skipped without the corpus,
  `-DUNI_GOFMT_CORPUS=<dir>/cases` points elsewhere) compares byte for byte with go1.26.5 (`-n`:
  the newest release) and also feeds mutated inputs (`--mutate N seed`) to check that bad input
  never crashes or hangs. The go1.27.1 port was checked the same way against both corpora
  re-recorded with Go 1.27.1 (`go/printer` of the protogen configuration, import block inserted)
  and Go 1.27.1's own sources (8 of the 126 protogen cases and 3 of the sources format differently
  from Go 1.26.5).

A new Go version: this directory implements the newest Go version in use, so port the upstream diff
of the files above file by file, regenerate the tables, and rerun the corpus against that Go
version. What differs for an older version a generator still needs goes to its `go_std_release`
(data in its `go<version>/` directory, conditions on `minor` in place), see docs/RULES.md.
