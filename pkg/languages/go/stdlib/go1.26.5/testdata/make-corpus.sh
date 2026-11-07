#!/usr/bin/env bash
# Builds the dev-only test corpus of the Go formatter port (not committed):
#
#   pkg/languages/go/stdlib/go1.26.5/testdata/make-corpus.sh [out]    default: build/gofmt-corpus
#
# Cases come from the former Go implementation of protoc-gen-uni (git revision UNI_GO_REV, default
# c9b2e00, the last one with it), patched with testdata/protogen-dump.patch: its protogen records
# every file it formats when UNI_DUMP_GOFMT=<dir> is set: <hash>.in.go, <hash>.imports,
# <hash>.out.go, <hash>.idents (index.txt lists the hashes). Sources of the cases:
#   1. its golden tests of protocolbuffers/go (recorded upstream requests), and
#   2. its binary run by buf over every testdata/proto/* with several option sets.
# Identical inputs collapse into one case (the name is a hash of the input). Needs git, patch, go,
# buf (scripts/get-buf.sh, BUF overrides) and network access for buf dependencies (es-validate;
# skipped with a warning when it fails).
#
# A second, independent corpus <out>/gosrc covers the full Go syntax: Go 1.26.5's own sources
# (GOROOT of GOTOOLCHAIN=go1.26.5, downloaded by the go command) formatted by the Go 1.26.5
# go/printer with the protogen configuration (no imports inserted).
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../../.." && pwd)
out=${1:-$root/build/gofmt-corpus}
out=$(mkdir -p "$out" && cd "$out" && pwd)
cases=$out/cases
rm -rf "$cases" "$out/work"
mkdir -p "$cases" "$out/work"

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
legacy=$out/work/legacy
mkdir -p "$legacy"
git -C "$root" archive "${UNI_GO_REV:-c9b2e00}" | tar -x -C "$legacy"
patch -s -d "$legacy" -p1 <"$here/protogen-dump.patch"

echo "1. Go implementation: golden tests" >&2
(cd "$legacy" && UNI_DUMP_GOFMT=$cases go test -count=1 ./generators/protocolbuffers/... >&2)

echo "2. Go implementation: binary over testdata/proto/*" >&2
bin=$out/work/protoc-gen-uni
(cd "$legacy" && go build -o "$bin" .)
buf=${BUF:-$("$root/scripts/get-buf.sh")}
groups=(
	"paths=source_relative"
	"paths=import"
	"paths=source_relative,default_api_level=API_OPEN"
	"paths=source_relative,default_api_level=API_HYBRID"
	"paths=source_relative,default_api_level=API_OPAQUE"
	"paths=import,annotate_code"
	"paths=source_relative,annotate_code,default_api_level=API_OPAQUE"
	"module=example.com/gen"
	"module=example.com/gen,annotate_code,default_api_level=API_HYBRID"
)
# One buf call per (test, option set), so that a failing combination (e.g. module= with files
# outside the module prefix) skips only itself.
i=0
for g in "${groups[@]}"; do
	tmpl=$out/work/g$i.buf.gen.yaml
	cat >"$tmpl" <<YAML
version: v2
managed:
  enabled: true
  override:
    - file_option: go_package_prefix
      value: example.com/gen
plugins:
  - local: $bin
    strategy: all
    out: .
    opt: [uni-name=protocolbuffers/go, uni-version=v1.36.12, ${g//,/, }]
YAML
	for dir in "$root"/testdata/proto/*/; do
		test=$(basename "$dir")
		if ! (cd "$root" && UNI_DUMP_GOFMT=$cases "$buf" generate "testdata/proto/$test" \
			--template "$tmpl" -o "$out/work/gen/g$i/$test" 2>"$out/work/err"); then
			echo "warning: skipped $test [$g]: $(head -c 200 "$out/work/err")" >&2
		fi
	done
	i=$((i + 1))
done
index() { (cd "$1" && find . -name '*.in.go' | sed 's|^\./||; s|\.in\.go$||' | LC_ALL=C sort >index.txt); }
index "$cases"
echo "$(wc -l <"$cases/index.txt") cases in $cases" >&2

echo "3. Go 1.26.5 sources" >&2
gosrc=$out/gosrc
rm -rf "$gosrc"
mkdir -p "$gosrc" "$out/work/gofmt-dump"
cat >"$out/work/gofmt-dump/go.mod" <<'GO'
module gofmtdump

go 1.26
GO
cat >"$out/work/gofmt-dump/main.go" <<'GO'
// Formats Go files like protogen (without the import block) and records corpus cases.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"go/ast"
	"go/parser"
	"go/printer"
	"go/token"
	"os"
	"path/filepath"
)

func main() {
	dir := os.Args[1]
	n := 0
	for _, name := range os.Args[2:] {
		src, err := os.ReadFile(name)
		if err != nil {
			panic(err)
		}
		fset := token.NewFileSet()
		file, err := parser.ParseFile(fset, "", src, parser.ParseComments)
		if err != nil {
			continue // testdata with deliberate errors
		}
		var out bytes.Buffer
		if err := (&printer.Config{Mode: printer.TabIndent | printer.UseSpaces, Tabwidth: 8}).Fprint(&out, fset, file); err != nil {
			panic(err)
		}
		idents, ok := dumpIdents(out.Bytes())
		if !ok {
			continue
		}
		h := sha256.Sum256(append(append([]byte{}, src...), 0))
		base := filepath.Join(dir, hex.EncodeToString(h[:])[:16])
		write(base+".in.go", src)
		write(base+".imports", nil)
		write(base+".out.go", out.Bytes())
		write(base+".idents", idents)
		n++
	}
	fmt.Fprintf(os.Stderr, "%d cases\n", n)
}

func write(name string, data []byte) {
	if err := os.WriteFile(name, data, 0o644); err != nil {
		panic(err)
	}
}

// dumpIdents mirrors protogen's generatedCodeInfo traversal (see protogen_dump.go).
func dumpIdents(content []byte) ([]byte, bool) {
	fset := token.NewFileSet()
	astFile, err := parser.ParseFile(fset, "", content, 0)
	if err != nil {
		return nil, false
	}
	var b bytes.Buffer
	annotate := func(s string, ident *ast.Ident) {
		fmt.Fprintf(&b, "%s\t%d\t%d\n", s, fset.Position(ident.Pos()).Offset, fset.Position(ident.End()).Offset)
	}
	for _, decl := range astFile.Decls {
		switch decl := decl.(type) {
		case *ast.GenDecl:
			for _, spec := range decl.Specs {
				switch spec := spec.(type) {
				case *ast.TypeSpec:
					annotate(spec.Name.Name, spec.Name)
					switch st := spec.Type.(type) {
					case *ast.StructType:
						for _, field := range st.Fields.List {
							for _, name := range field.Names {
								annotate(spec.Name.Name+"."+name.Name, name)
							}
						}
					case *ast.InterfaceType:
						for _, field := range st.Methods.List {
							for _, name := range field.Names {
								annotate(spec.Name.Name+"."+name.Name, name)
							}
						}
					}
				case *ast.ValueSpec:
					for _, name := range spec.Names {
						annotate(name.Name, name)
					}
				}
			}
		case *ast.FuncDecl:
			if decl.Recv == nil {
				annotate(decl.Name.Name, decl.Name)
			} else if len(decl.Recv.List) > 0 {
				recv := decl.Recv.List[0].Type
				if s, ok := recv.(*ast.StarExpr); ok {
					recv = s.X
				}
				if id, ok := recv.(*ast.Ident); ok {
					annotate(id.Name+"."+decl.Name.Name, decl.Name)
				}
			}
		}
	}
	return b.Bytes(), true
}
GO
goroot=$(cd "$out/work/gofmt-dump" && GOTOOLCHAIN=go1.26.5 go env GOROOT)
(cd "$out/work/gofmt-dump" && GOTOOLCHAIN=go1.26.5 go build -o ../gofmt-dump-bin .)
find "$goroot/src/go" "$goroot/src/net" "$goroot/src/strconv" "$goroot/src/text" \
	"$goroot/src/encoding" "$goroot/src/cmd/compile/internal/types2" "$goroot/src/cmd/gofmt" \
	"$goroot/src/internal/types" "$goroot/src/slices" "$goroot/src/maps" "$goroot/src/iter" \
	-name '*.go' -type f -print0 | xargs -0 "$out/work/gofmt-dump-bin" "$gosrc"
rm -rf "$out/work"
index "$gosrc"
echo "$(wc -l <"$gosrc/index.txt") cases in $gosrc" >&2
