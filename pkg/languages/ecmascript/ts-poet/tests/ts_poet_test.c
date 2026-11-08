/* Unit tests of ts-poet (Import.from, aliases, emitImports, toString), Node.js `path` (std/path.c)
 * and case-anything's camelCase. Expected values from ts-poet 6.12.0, Node.js 22 and
 * case-anything 2.1.13. */
#include "test.h"

#include "case_anything.h"
#include "es_std.h"
#include "ts_poet.h"

static void test_path(void)
{
	CHECK_STR(node_path_normalize("a/b/../c"), "a/c");
	CHECK_STR(node_path_normalize("./x/./y/"), "x/y/");
	CHECK_STR(node_path_normalize("../../a"), "../../a");
	CHECK_STR(node_path_normalize("/a/../.."), "/");
	CHECK_STR(node_path_normalize(""), ".");
	CHECK_STR(node_path_normalize("a//b"), "a/b");
	CHECK_STR(node_path_normalize("./"), "./");
	CHECK_STR(node_path_relative("foo/bar", "foo/baz/qux"), "../baz/qux");
	CHECK_STR(node_path_relative("a/b/c", "a"), "../..");
	CHECK_STR(node_path_relative("a", "a/b"), "b");
	CHECK_STR(node_path_relative("x/y", "z"), "../../z");
	CHECK_STR(node_path_relative("", ""), "");
	CHECK_STR(node_path_relative("google/protobuf", "google/protobuf/timestamp"), "timestamp");
	CHECK_STR(node_path_dirname("a/b/c.ts"), "a/b");
	CHECK_STR(node_path_dirname("a"), ".");
	CHECK_STR(node_path_dirname("/a"), "/");
	CHECK_STR(node_path_dirname("a/b/"), "a");
	CHECK_STR(node_path_basename("a/b/c.ts", ".ts"), "c");
	CHECK_STR(node_path_basename("index.foo.ts", ".ts"), "index.foo");
	CHECK_STR(node_path_basename("a/b/", NULL), "b");
	CHECK_STR(node_path_basename("x.ts", "x.ts"), "");
}

static void test_camel_case(void)
{
	CHECK_STR(ca_camel_case("GetAPIValue"), "getApiValue");
	CHECK_STR(ca_camel_case("Reset"), "reset");
	CHECK_STR(ca_camel_case("PingStream"), "pingStream");
	CHECK_STR(ca_camel_case("get_user_id"), "getUserId");
	CHECK_STR(ca_camel_case("HTTPServer"), "httpServer");
	CHECK_STR(ca_camel_case("\xc3\x9c" "n" "\xc3\xaf" "c" "\xc3\xb6" "d" "\xc3\xa9"), "unicode");
	CHECK_STR(ca_camel_case("ListV2Items"), "listV2Items");
	CHECK_STR(ca_camel_case("a b  c"), "aBC");
}

static void test_import_spec(void)
{
	TpImport *i = tp_imp("t:Src:Name.Child@./m");
	CHECK(i->kind == IMP_NAME && i->type_import);
	CHECK_STR(i->symbol, "Name");
	CHECK_STR(i->source_symbol, "Src");
	CHECK_STR(i->child_symbol, "Child");
	CHECK_STR(i->source, "./m");
	CHECK(tp_imp("Long=long")->kind == IMP_DEFAULT);
	CHECK(tp_imp("x*mod")->kind == IMP_ALL);
	CHECK(tp_imp("y+mod")->kind == IMP_SIDE_EFFECT);
	CHECK(tp_imp("BinaryReader@@bufbuild/protobuf/wire")->kind == IMP_NAME);
	CHECK_STR(tp_imp("BinaryReader@@bufbuild/protobuf/wire")->source, "@bufbuild/protobuf/wire");
	CHECK(tp_imp("not a spec")->kind == IMP_IMPLICIT);
	CHECK(tp_imp("_@x")->kind == IMP_IMPLICIT); /* `_` alone is not an identifier of the pattern */
	CHECK(tp_same_module("./a/b.ts", "a/b"));
}

/* export const Status = [Item, Item1, Status2, Long, Observable, Observable, x, Knex.Transaction, Name];
 * with a def Status, Item from two modules, a type import colliding with the def, a default, a
 * value and a type import of the same name, a star import, a child symbol and a renamed import */
static void test_to_string(void)
{
	tp_file_begin();
	TpCode *c = tp_code(NULL);
	Node *def = tp_defn(c, tp_def("Status"));
	Node *arr = dts_arr(NULL);
	const char *specs[] = {"Item@./common/v1/common", "Item@./other/v1/item", "t:Status@./common/v1/common", "Long=long",
			       "Observable@rxjs", "t:Observable@rxjs", "x*mod", "Knex.Transaction@knex", "Src:Name@./m"};
	for (size_t k = 0; k < sizeof specs / sizeof specs[0]; k++) {
		Node *w = dts_new(N_EXPR_OR_SPREAD);
		w->a = tp_ref(c, tp_imp(specs[k]));
		dts_add(&arr->list, w);
	}
	Node *stmt = dts_export(dts_var(VAR_CONST, dts_bindingn(def->text, NULL, false), arr));
	NodeList body = {0};
	dts_add(&body, stmt);
	NodeComment prefix = {STR_LIT("// prefix"), false, 0};
	TpOpts o = {.path = "app/v1/app.ts", .prefix = &prefix, .nprefix = 1};
	Buf out = {0};
	tp_to_string(tp_join(&c, 1), body, NULL, 0, &o, &out);
	CHECK_STR(buf_str(&out), "// prefix\n"
				 "import { Knex } from \"knex\";\n"
				 "import Long from \"long\";\n"
				 "import * as x from \"mod\";\n"
				 "import { Observable } from \"rxjs\";\n"
				 "import { Item, type Status as Status2 } from \"../../common/v1/common\";\n"
				 "import { Src as Name } from \"../../m\";\n"
				 "import { Item as Item1 } from \"../../other/v1/item\";\n"
				 "\n"
				 "export const Status = [Item, Item1, Status2, Long, Observable, Observable, x, Knex.Transaction, Name];\n");
}

static void raw_body(void *arg, Buf *out) { buf_adds(out, arg); }

/* const class = [Long, A, B, m]; does not parse: ts-poet returns its text, prefix + "\n" + the
 * imports (emitImports, in the order found) + "\n" + the body */
static void test_to_string_raw(void)
{
	tp_file_begin();
	TpCode *c = tp_code(NULL);
	Node *arr = dts_arr(NULL);
	const char *specs[] = {"Long=long", "t:A@./a", "B@x", "m*mod", "y+side"};
	for (size_t k = 0; k < sizeof specs / sizeof specs[0]; k++) {
		Node *w = dts_new(N_EXPR_OR_SPREAD);
		w->a = tp_ref(c, tp_imp(specs[k]));
		dts_add(&arr->list, w);
	}
	NodeList body = {0};
	dts_add(&body, dts_var(VAR_CONST, dts_binding("class", NULL, false), arr));
	static const char *const force_require[] = {"long"};
	TpOpts o = {.path = "app.ts", .force_require = force_require, .nforce_require = 1, .raw_prefix = "// prefix",
		    .raw_body = raw_body, .raw_arg = "const class = [Long, A, B, m];"};
	Buf out = {0};
	tp_to_string(tp_join(&c, 1), body, NULL, 0, &o, &out);
	CHECK_STR(buf_str(&out), "// prefix\n"
				 "import Long = require('long');\n"
				 "import type { A } from './a';\n"
				 "import { B } from 'x';\n"
				 "import * as m from 'mod';\n"
				 "import 'side';\n"
				 "\n"
				 "const class = [Long, A, B, m];");
}

TESTS(TEST(test_path), TEST(test_camel_case), TEST(test_import_spec), TEST(test_to_string), TEST(test_to_string_raw))
