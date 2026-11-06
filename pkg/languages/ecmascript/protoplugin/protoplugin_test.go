package protoplugin

import (
	"errors"
	"slices"
	"strings"
	"testing"
)

func TestParseParameter(t *testing.T) {
	parseExtra := func(opts []RawOption) ([]RawOption, error) {
		for _, o := range opts {
			if o.Key == "bad" {
				return nil, errors.New("please provide true or false")
			}
			if o.Key == "unknown" {
				return nil, errors.New("")
			}
		}
		return opts, nil
	}
	p, err := ParseParameter("target=ts+js+ts,import_extension=.js,rewrite_imports=./a/*:@x/y,foo=1,bar,ts_nocheck=1", parseExtra)
	if err != nil {
		t.Fatal(err)
	}
	if !slices.Equal(p.Parsed.Targets, []Target{TargetTs, TargetJs}) || p.Parsed.ImportExtension != "js" || !p.Parsed.TsNocheck {
		t.Errorf("parsed = %+v", p.Parsed)
	}
	if want := []RawOption{{"foo", "1"}, {"bar", ""}}; !slices.Equal(p.Extra, want) {
		t.Errorf("extra = %v, want %v", p.Extra, want)
	}
	if want := "target=ts+js+ts,import_extension=.js,foo=1,bar,ts_nocheck=1"; p.Sanitized != want {
		t.Errorf("sanitized = %q, want %q", p.Sanitized, want)
	}

	errs := map[string]string{
		"target=java":               `invalid option "target=java"`,
		"keep_empty_files=yes":      `invalid option "keep_empty_files=yes"`,
		"js_import_style=cjs":       `invalid option "js_import_style=cjs"`,
		"import_extension=mjs":      `invalid option "import_extension=mjs"`,
		"rewrite_imports=foo":       `invalid option "rewrite_imports=foo": must be in the form of <pattern>:<target>`,
		"map_imports=:foo":          `invalid option "map_imports=:foo": must be in the form of <pattern>:<target>`,
		"map_imports=foo:":          `invalid option "map_imports=foo:": must be in the form of <pattern>:<target>`,
		"target=ts,x=1,bad=2,y":     `invalid option "x=1,bad=2,y": please provide true or false`,
		"target=ts,unknown=bar":     `invalid option "unknown=bar"`,
		"elide_plugin_version=TRUE": `invalid option "elide_plugin_version=TRUE"`,
	}
	for param, want := range errs {
		_, err := ParseParameter(param, parseExtra)
		if err == nil || err.Error() != want || !IsPluginOptionError(err) {
			t.Errorf("ParseParameter(%q) error = %v, want %q", param, err, want)
		}
	}
	if _, err := ParseParameter[any]("foo", nil); err == nil || err.Error() != `invalid option "foo"` {
		t.Errorf("unknown option without parser: %v", err)
	}
}

func TestLocaleCompare(t *testing.T) {
	sorted := []string{
		"_a", "$a", "1a", "a", "A", "a_b", "a$b", "a1", "ab", "aB", "Ab", "AB", "abc",
		"file_foo", "fileDesc", "FileDesc", "GenEnum", "GenFile", "Item_Id", "ItemSchema", "message",
		"Message", "messageDesc",
	}
	shuffled := slices.Clone(sorted)
	slices.Reverse(shuffled)
	slices.SortFunc(shuffled, LocaleCompare)
	if !slices.Equal(shuffled, sorted) {
		t.Errorf("sorted = %v\nwant %v", shuffled, sorted)
	}
}

func TestImportPaths(t *testing.T) {
	for _, tt := range []struct{ in, want string }{
		{"foo/bar_pb.ts", "./foo/bar_pb.js"},
		{"foo/bar_pb.d.ts", "./foo/bar_pb.js"},
		{"foo/bar_pb.js", "./foo/bar_pb.js"},
		{"./foo.ts", "./foo.js"},
		{"../foo.ts", "../foo.js"},
		{"foo.txt", "./foo.txt"},
	} {
		if got := DeriveImportPath(tt.in); got != tt.want {
			t.Errorf("DeriveImportPath(%q) = %q, want %q", tt.in, got, tt.want)
		}
	}
	for _, tt := range []struct{ importer, path, want string }{
		{"./foo/foo.js", "./baz.js", "../baz.js"},
		{"./foo/foo.js", "./foo/bar.js", "./bar.js"},
		{"./a/b/c.js", "./a/d/e.js", "../d/e.js"},
		{"./a.js", "./b/c.js", "./b/c.js"},
		{"./a/b.js", "@bufbuild/protobuf", "@bufbuild/protobuf"},
	} {
		if got := MakeImportPathRelative(tt.importer, tt.path); got != tt.want {
			t.Errorf("MakeImportPathRelative(%q, %q) = %q, want %q", tt.importer, tt.path, got, tt.want)
		}
	}
	ri := compileRewriteImports([]PatternTarget{{"./foo/**/*_pb.js", "@scope/pkg"}, {"@scope/pkg", "npm:@scope/pkg"}})
	for _, tt := range []struct {
		path string
		ext  ImportExtension
		want string
	}{
		{"./foo/bar_pb.js", "none", "@scope/pkg/foo/bar_pb.js"},
		{"./foo/bar/baz_pb.js", "ts", "@scope/pkg/foo/bar/baz_pb.js"},
		{"./local_pb.js", "none", "./local_pb"},
		{"./local_pb.js", "ts", "./local_pb.ts"},
		{"./local_pb.js", "js", "./local_pb.js"},
		{"@scope/pkg", "none", "npm:@scope/pkg"},
	} {
		if got := RewriteImportPath(tt.path, ri, tt.ext); got != tt.want {
			t.Errorf("RewriteImportPath(%q, %q) = %q, want %q", tt.path, tt.ext, got, tt.want)
		}
	}
	mi := compileMapImports([]PatternTarget{{"google/rpc/", "@scope/rpc/"}, {"a/*.proto", "@a"}, {"b/**", "@b"}, {"c/**/x.proto", "@c"}})
	for _, tt := range []struct{ file, want string }{
		{"google/rpc/status.proto", "@scope/rpc"},
		{"google/rpc/x/y.proto", "@scope/rpc"},
		{"a/x.proto", "@a"},
		{"a/x/y.proto", ""},
		{"b/x/y.proto", "@b"},
		{"c/x.proto", "@c"},
		{"c/d/e/x.proto", "@c"},
		{"other.proto", ""},
	} {
		if got, _ := mapImportTarget(tt.file, mi); got != tt.want {
			t.Errorf("mapImportTarget(%q) = %q, want %q", tt.file, got, tt.want)
		}
	}
}

func TestGeneratedFileImports(t *testing.T) {
	runtime := createRuntimeImports(false)
	f := &GeneratedFile{
		importPath:    "./foo/bar_pb.js",
		rewriteImport: func(s string) string { return RewriteImportPath(s, nil, "none") },
		Runtime:       runtime,
	}
	local := f.Import("Message", "./foo/bar_pb.js", false)
	other := f.Import("Message", "./other_pb.js", true)
	f.Print(f.Export("type", "Foo"), " = ", runtime.Message, " | ", other, " | ", local, ";")
	f.Print("const x = ", f.Import("b", "./other_pb.js", false), ", ", f.Import("a", "./other_pb.js", false), ", ", f.Import("Foo", "@x/y", false), ";")
	f.Print(f.String("a\"b\\c\nd"), " ", []byte{1, 0xab}, " ", []byte{}, " ", 1.5, " ", 1e21, " ", int32(-3))
	f.Print(LiteralProtoInt64{Value: "0"}, " ", LiteralProtoInt64{Value: "-5"}, " ", LiteralProtoInt64{Type: 4, Value: "5"}, " ", LiteralProtoInt64{LongAsString: true, Value: "5"})
	want := strings.Join([]string{
		`import type { Message as Message$1 } from "@bufbuild/protobuf";`,
		`import { protoInt64 } from "@bufbuild/protobuf";`,
		`import type { Message as Message$2 } from "../other_pb";`,
		`import { a, b } from "../other_pb";`,
		`import { Foo as Foo$1 } from "@x/y";`,
		``,
		`export type Foo = Message$1 | Message$2 | Message;`,
		`const x = b, a, Foo$1;`,
		`"a\"b\\c\nd" new Uint8Array([0x01, 0xAB]) new Uint8Array(0) 1.5 1e+21 -3`,
		`protoInt64.zero protoInt64.parse("-5") protoInt64.uParse("5") "5"`,
		``,
	}, "\n")
	if got := f.FileInfo().Content; got != want {
		t.Errorf("content:\n%s\nwant:\n%s", got, want)
	}

	cjs := &GeneratedFile{
		importPath:    "./foo_pb.js",
		jsImportStyle: JSImportStyleLegacyCommonJS,
		rewriteImport: func(s string) string { return s },
		Runtime:       runtime,
	}
	cjs.Print(cjs.Export("const", "X"), " = ", cjs.Import("y", "./y_pb.js", false), ", ", cjs.Import("y", "./z_pb.js", false), ";")
	want = strings.Join([]string{
		`"use strict";`,
		`Object.defineProperty(exports, "__esModule", { value: true });`,
		``,
		`const { y } = require("./y_pb.js");`,
		`const { y: y$1 } = require("./z_pb.js");`,
		``,
		`const X = y, y$1;`,
		``,
		`exports.X = X;`,
		``,
	}, "\n")
	if got := cjs.FileInfo().Content; got != want {
		t.Errorf("content:\n%s\nwant:\n%s", got, want)
	}
}

func TestFormatJSDocBlock(t *testing.T) {
	if got := FormatJSDocBlock(" \n ", ""); got != "" {
		t.Errorf("blank = %q", got)
	}
	want := "  /**\n   * a *\\/ b\n   *\n   *  c\n   */"
	if got := FormatJSDocBlock("a */ b\n\n c", "  "); got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestEscapeValue(t *testing.T) {
	if got, want := escapeValue("a\\b\"c\nd\re f "), `a\\b\"c\nd\re f `; got != want {
		t.Errorf("escapeValue = %q, want %q", got, want)
	}
}
