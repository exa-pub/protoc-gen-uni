# Go 1.26.5 standard library: `go/printer`, `go/doc/comment`

Verbatim copies of `src/go/printer` and `src/go/doc/comment` (non-test files) from the Go 1.26.5
source release (<https://go.dev/dl/go1.26.5.src.tar.gz>). BSD-3-Clause, Copyright The Go Authors.

## Why

`compiler/protogen` formats every generated Go file with `go/parser` + `go/printer`, so plugin output
depends on the standard library of the toolchain that *built the plugin binary*. The buf.build remote
plugin `protocolbuffers/go:v1.36.12` is built with Go 1.26.5 (bufbuild/plugins Dockerfile
`golang:1.26.5-trixie`). Go 1.27 changed `go/printer` (comment rewriting before identifiers,
key-alignment heuristics, `indentList`), which changes the output for some comments (the `comments`
golden test). Pinning the printer keeps protoc-gen-uni output independent of the Go version used to
build it.

`go/doc/comment` is vendored because the printer reformats doc comments with it. `go/ast`,
`go/token` and `go/parser` are still taken from the building toolchain (the printer API is built on
`go/ast` types); their changes so far do not affect formatting.

## Changes from upstream

- The `go/doc/comment` import in `printer/comment.go` points to the vendored copy.
- `//go:generate ./mkstd.sh` is removed from `doc/comment/std.go` (the script is not vendored).
- Comments added: upstream source link in every file, vendoring note in package docs, file-level
  `//nolint` directives for linters that flag the verbatim code.

Users: `pkg/languages/go/protogen` (fork of `compiler/protogen` v1.36.12).
