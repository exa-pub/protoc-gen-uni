/* pkg/languages/ecmascript/protoplugin: localeCompare, import paths and patterns, generated
 * files, JSDoc blocks; cases of protoplugin_test.go of the Go port. */
#include "test.h"
#include "internal.h"

static void test_locale_compare(void)
{
	static const char *const sorted[] = {
		"_a", "$a", "1a", "a", "A", "a_b", "a$b", "a1", "ab", "aB", "Ab", "AB", "abc",
		"file_foo", "fileDesc", "FileDesc", "GenEnum", "GenFile", "Item_Id", "ItemSchema", "message",
		"Message", "messageDesc",
	};
	size_t n = sizeof sorted / sizeof *sorted;
	for (size_t i = 0; i < n; i++)
		for (size_t j = 0; j < n; j++) {
			int c = locale_compare(sorted[i], sorted[j]);
			CHECK(i < j ? c < 0 : i > j ? c > 0 : c == 0);
		}
}

static void test_import_paths(void)
{
	static const char *const derive[][2] = {
		{"foo/bar_pb.ts", "./foo/bar_pb.js"}, {"foo/bar_pb.d.ts", "./foo/bar_pb.js"},
		{"foo/bar_pb.js", "./foo/bar_pb.js"}, {"./foo.ts", "./foo.js"},
		{"../foo.ts", "../foo.js"},           {"foo.txt", "./foo.txt"},
		{"a.dxts", "./a.js"},                 {"a.d\nts", "./a.d\nts"},
	};
	for (size_t i = 0; i < sizeof derive / sizeof *derive; i++)
		CHECK_STR(derive_import_path(derive[i][0]), derive[i][1]);
	static const char *const rel[][3] = {
		{"./foo/foo.js", "./baz.js", "../baz.js"},
		{"./foo/foo.js", "./foo/bar.js", "./bar.js"},
		{"./a/b/c.js", "./a/d/e.js", "../d/e.js"},
		{"./a.js", "./b/c.js", "./b/c.js"},
		{"./a/b.js", "@bufbuild/protobuf", "@bufbuild/protobuf"},
	};
	for (size_t i = 0; i < sizeof rel / sizeof *rel; i++)
		CHECK_STR(make_import_path_relative(rel[i][0], rel[i][1]), rel[i][2]);

	Schema s = {0};
	PatternTarget p1 = {"./foo/**/*_pb.js", "@scope/pkg"}, p2 = {"@scope/pkg", "npm:@scope/pkg"};
	vec_push(&s.opts.rewrite_imports, &p1);
	vec_push(&s.opts.rewrite_imports, &p2);
	static const char *const rw[][3] = {
		{"./foo/bar_pb.js", "none", "@scope/pkg/foo/bar_pb.js"},
		{"./foo/bar/baz_pb.js", "ts", "@scope/pkg/foo/bar/baz_pb.js"},
		{"./local_pb.js", "none", "./local_pb"},
		{"./local_pb.js", "ts", "./local_pb.ts"},
		{"./local_pb.js", "js", "./local_pb.js"},
		{"@scope/pkg", "none", "npm:@scope/pkg"},
	};
	for (size_t i = 0; i < sizeof rw / sizeof *rw; i++) {
		s.opts.import_extension = rw[i][1];
		CHECK_STR(rewrite_import_path(&s, rw[i][0]), rw[i][2]);
	}
}

static void test_patterns(void)
{
	/* map_imports (glob) */
	static const struct {
		const char *pattern, *s;
		bool glob, want;
	} tests[] = {
		{"google/rpc/", "google/rpc/status.proto", true, true},
		{"google/rpc/", "google/rpc/x/y.proto", true, true},
		{"google/rpc/", "google/rpc\n/x", true, false},
		{"google/rpc/", "google/rpc/x\ny", true, false},
		{"a/*.proto", "a/x.proto", true, true},
		{"a/*.proto", "a/x/y.proto", true, false},
		{"b/**", "b/x/y.proto", true, true},
		{"b/**", "b/x\n", true, false},
		{"c/**/x.proto", "c/x.proto", true, true},
		{"c/**/x.proto", "c/d/e/x.proto", true, true},
		{"c/**/x.proto", "c//x.proto", true, false},
		{"other", "other.proto", true, false},
		/* rewrite_imports (star): "**" without "/" is two stars */
		{"./foo/**/*_pb.js", "./foo/a/b/c_pb.js", false, true},
		{"./foo/**", "./foo/a/b", false, false},
		{"./foo/**", "./foo/ab", false, true},
		{"a.b", "axb", false, false},
		{"", "", false, true},
		{"*", "", false, true},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++)
		CHECK(import_pattern_match(tests[i].pattern, tests[i].s, tests[i].glob) == tests[i].want);
	/* no exponential backtracking */
	Buf pat = {0}, s = {0};
	for (int i = 0; i < 2000; i++)
		buf_adds(&pat, "**/*a");
	for (int i = 0; i < 4000; i++)
		buf_adds(&s, "a/");
	CHECK(!import_pattern_match(buf_str(&pat), buf_str(&s), true));
}

static void test_generated_file(void)
{
	Schema s = {0};
	s.target = TARGET_TS;
	s.opts.import_extension = "none";
	s.runtime = create_runtime_imports(false);
	GeneratedFile *f = gf_new(&s, "foo/bar_pb.ts");
	ImportSymbol *local = new_import_symbol("Message", "./foo/bar_pb.js", false);
	ImportSymbol *other = new_import_symbol("Message", "./other_pb.js", true);
	gf_export(f, "type", "Foo");
	gf_s(f, " = ");
	gf_sym(f, s.runtime->message);
	gf_s(f, " | ");
	gf_sym(f, other);
	gf_s(f, " | ");
	gf_sym(f, local);
	gf_s(f, ";\nconst x = ");
	gf_sym(f, new_import_symbol("b", "./other_pb.js", false));
	gf_s(f, ", ");
	gf_sym(f, new_import_symbol("a", "./other_pb.js", false));
	gf_s(f, ", ");
	gf_sym(f, new_import_symbol("Foo", "@x/y", false));
	gf_s(f, ";\n");
	gf_lit(f, STR_LIT("a\"b\\c\nd\0"));
	gf_nl(f);
	Str c = generated_file_content(f);
	static const char want[] = "import type { Message as Message$1 } from \"@bufbuild/protobuf\";\n"
				   "import type { Message as Message$2 } from \"../other_pb\";\n"
				   "import { a, b } from \"../other_pb\";\n"
				   "import { Foo as Foo$1 } from \"@x/y\";\n"
				   "\n"
				   "export type Foo = Message$1 | Message$2 | Message;\n"
				   "const x = b, a, Foo$1;\n"
				   "\"a\\\"b\\\\c\\nd\0\"\n";
	CHECK_MEM(c.p, c.n, want, sizeof want - 1);

	s.target = TARGET_JS;
	s.opts.legacy_commonjs = true;
	f = gf_new(&s, "foo_pb.js");
	gf_export(f, "const", "X");
	gf_s(f, " = ");
	gf_sym(f, new_import_symbol("y", "./y_pb.js", false));
	gf_s(f, ", ");
	gf_sym(f, new_import_symbol("y", "./z_pb.js", false));
	gf_s(f, ";\n");
	c = generated_file_content(f);
	static const char want_cjs[] = "\"use strict\";\n"
				       "Object.defineProperty(exports, \"__esModule\", { value: true });\n"
				       "\n"
				       "const { y } = require(\"./y_pb\");\n"
				       "const { y: y$1 } = require(\"./z_pb\");\n"
				       "\n"
				       "const X = y, y$1;\n"
				       "\n"
				       "exports.X = X;\n";
	CHECK_MEM(c.p, c.n, want_cjs, sizeof want_cjs - 1);
}

static void test_jsdoc_block(void)
{
	Str got = format_jsdoc_block(STR_LIT(" \n "), "");
	CHECK(got.n == 0);
	got = format_jsdoc_block(STR_LIT("a */ b\n\n c\0*/"), "  ");
	static const char want[] = "  /**\n   * a *\\/ b\n   *\n   *  c\0*\\/\n   */";
	CHECK_MEM(got.p, got.n, want, sizeof want - 1);
}

TESTS(TEST(test_locale_compare), TEST(test_import_paths), TEST(test_patterns), TEST(test_generated_file),
      TEST(test_jsdoc_block))
