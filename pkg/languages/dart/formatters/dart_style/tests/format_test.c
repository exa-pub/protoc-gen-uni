/* Unit tests of the dart_style 3.1.13 port: node fixtures (trees built with the node API of
 * dart_style.h) with the source text the linearizer writes and the output of dart_style 3.1.13
 * on the Dart SDK 3.13.3 for that text (DartFormatter(languageVersion: 3.7.0).format). */
#include <stdarg.h>

#include "dart_style.h"
#include "test.h"

#define L(...) ds_list_of(__VA_ARGS__, (Node *)0)

static NodeList ds_list_of(Node *first, ...)
{
	NodeList l = {0};
	va_list ap;
	va_start(ap, first);
	for (Node *n = first; n; n = va_arg(ap, Node *))
		ds_push(&l, n);
	va_end(ap);
	return l;
}

static const NodeList none;

/* Linearizes and formats unit; checks the source text and the formatted output. */
static void check(Node *unit, const char *text, const char *formatted)
{
	Buf src = {0}, out = {0};
	ds_linearize(unit, &src);
	CHECK_STR(buf_str(&src), text);
	const char *err = ds_format(unit, buf_view(&src), &out);
	CHECK_STR(err ? err : buf_str(&out), formatted);
}

static Node *header(Node *unit)
{
	ds_comment(unit, "// @dart = 3.3");
	return unit;
}

static void test_class(void)
{
	Node *f = ds_field(none, false, KW_NONE, ds_type(NULL, "int", NULL, false), "x", ds_int(1));
	Node *c = ds_class(none, false, "A", NULL, none, L(f));
	Node *u = header(ds_unit(none, L(c)));
	ds_nl(c, 2, 0);
	check(u, "// @dart = 3.3\n\nclass A {\n  int x = 1;\n}\n", "// @dart = 3.3\n\nclass A {\n  int x = 1;\n}\n");
}

/* A call that does not fit in 80 columns splits its arguments. */
static void test_long_call(void)
{
	Node *call = ds_call(ds_id("$pb"), "BuilderInfo", NULL,
			     ds_args(ds_cond(ds_id("_omitMessageNames"), ds_str("''"), ds_str("'AVeryLongMessageNameThatGoesOnAndOn'")),
				     ds_named("createEmptyInstance", ds_path("AVeryLongMessageNameThatGoesOnAndOn", "$_createMessage", (char *)0)),
				     (Node *)0));
	Node *f = ds_toplevel(none, KW_FINAL, ds_type("$pb", "BuilderInfo", NULL, false), "_i", call);
	Node *u = header(ds_unit(none, L(f)));
	ds_nl(f, 2, 0);
	check(u,
	      "// @dart = 3.3\n\nfinal $pb.BuilderInfo _i = $pb.BuilderInfo(_omitMessageNames ? '' : "
	      "'AVeryLongMessageNameThatGoesOnAndOn', createEmptyInstance: AVeryLongMessageNameThatGoesOnAndOn.$_createMessage);\n",
	      "// @dart = 3.3\n\nfinal $pb.BuilderInfo _i = $pb.BuilderInfo(\n"
	      "    _omitMessageNames ? '' : 'AVeryLongMessageNameThatGoesOnAndOn',\n"
	      "    createEmptyInstance: AVeryLongMessageNameThatGoesOnAndOn.$_createMessage);\n");
}

static Node *core(const char *name, Node *args) { return ds_type("$core", name, args, false); }

/* Nested type arguments, closures, conditionals, block and expression bodies (recorded from the
 * "generics" case: the same tree from text). */
static void test_generics(void)
{
	Node *mapType = core("Map", ds_targs(core("String", NULL),
					     core("Map", ds_targs(core("String", NULL), core("List", ds_targs(core("int", NULL), (Node *)0)), (Node *)0)),
					     (Node *)0));
	Node *m = ds_method(none, KW_NONE, mapType, KW_GET, "m", NULL, ds_arrow(ds_call(NULL, "$_getMap", NULL, ds_args(ds_int(0), (Node *)0))));
	Node *byValue = ds_field(none, true, KW_FINAL, core("Map", ds_targs(core("int", NULL), ds_type(NULL, "G", NULL, false), (Node *)0)),
				 "_byValue", ds_call(ds_path("$pb", "ProtobufEnum", (char *)0), "initByValue", NULL, ds_args(ds_id("values"), (Node *)0)));
	Node *updates = ds_param(ds_fntype(ds_type(NULL, "void", NULL, false), ds_params(L(ds_param(ds_type(NULL, "G", NULL, false), NULL, NULL)), none, false)),
				 "updates", NULL);
	Node *closure = ds_fnexpr(ds_params(L(ds_param(NULL, "message", NULL)), none, false),
				  ds_arrow(ds_call(NULL, "updates", NULL, ds_args(ds_as(ds_id("message"), ds_type(NULL, "G", NULL, false)), (Node *)0))));
	Node *copyWith = ds_method(none, KW_NONE, ds_type(NULL, "G", NULL, false), KW_NONE, "copyWith", ds_params(L(updates), none, false),
				   ds_arrow(ds_as(ds_call(ds_super(), "copyWith", NULL, ds_args(closure, (Node *)0)), ds_type(NULL, "G", NULL, false))));
	Node *getDefault = ds_method(none, KW_STATIC, ds_type(NULL, "G", NULL, false), KW_NONE, "getDefault", ds_params(none, none, false),
				     ds_arrow(ds_assign(ds_id("_defaultInstance"), TT_QUESTION_QUESTION_EQ, "?\?=",
							ds_call(ds_path("$pb", "GeneratedMessage", (char *)0), "$_defaultFor",
								ds_targs(ds_type(NULL, "G", NULL, false), (Node *)0),
								ds_args(ds_path("G", "$_createMessage", (char *)0), (Node *)0)))));
	Node *cond = ds_cond(ds_bin(ds_bin(ds_id("value"), TT_LT, "<", ds_int(0)), TT_BAR_BAR, "||",
				    ds_bin(ds_id("value"), TT_GT_EQ, ">=", ds_path("_byValue", "length", (char *)0))),
			     ds_lit(N_NullLiteral, TT_IDENTIFIER, "null"), ds_index(ds_id("_byValue"), ds_id("value")));
	Node *valueOf = ds_method(none, KW_STATIC, ds_type(NULL, "G", NULL, true), KW_NONE, "valueOf",
				  ds_params(L(ds_param(core("int", NULL), "value", NULL)), none, false), ds_arrow(cond));
	Node *setter = ds_method(none, KW_NONE, NULL, KW_SET, "x", ds_params(L(ds_param(core("int", NULL), "v", NULL)), none, false),
				 ds_bbody(KW_NONE, false, ds_block(L(ds_expr_stmt(ds_call(NULL, "$_setSignedInt32", NULL, ds_args(ds_int(0), ds_id("v"), (Node *)0)))))));
	Node *hasX = ds_method(none, KW_NONE, core("bool", NULL), KW_NONE, "hasX", ds_params(none, none, false),
			       ds_arrow(ds_call(NULL, "$_has", NULL, ds_args(ds_int(0), (Node *)0))));
	Node *c = ds_class(none, false, "G", NULL, none, L(m, byValue, copyWith, getDefault, valueOf, setter, hasX));
	Node *u = header(ds_unit(none, L(c)));
	Buf src = {0}, out = {0};
	ds_linearize(u, &src);
	const char *err = ds_format(u, buf_view(&src), &out);
	CHECK_STR(err ? err : buf_str(&out),
		  "// @dart = 3.3\n"
		  "class G {\n"
		  "  $core.Map<$core.String, $core.Map<$core.String, $core.List<$core.int>>>\n"
		  "      get m => $_getMap(0);\n"
		  "  static final $core.Map<$core.int, G> _byValue =\n"
		  "      $pb.ProtobufEnum.initByValue(values);\n"
		  "  G copyWith(void Function(G) updates) =>\n"
		  "      super.copyWith((message) => updates(message as G)) as G;\n"
		  "  static G getDefault() => _defaultInstance ?\?=\n"
		  "      $pb.GeneratedMessage.$_defaultFor<G>(G.$_createMessage);\n"
		  "  static G? valueOf($core.int value) =>\n"
		  "      value < 0 || value >= _byValue.length ? null : _byValue[value];\n"
		  "  set x($core.int v) {\n"
		  "    $_setSignedInt32(0, v);\n"
		  "  }\n"
		  "\n"
		  "  $core.bool hasX() => $_has(0);\n"
		  "}\n");
}

/* Line widths are measured in UTF-16 code units (recorded from the "unicode" case). */
static void test_unicode(void)
{
	Node *greeting = ds_toplevel(none, KW_CONST, NULL, "greeting",
				     ds_call(ds_id("$pb"), "f", NULL,
					     ds_args(ds_str("'\320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200! \320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200! \320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200!'"),
						     ds_str("'\320\237\321\200\320\270\320\262\320\265\321\202'"), ds_str("'x'"), (Node *)0)));
	ds_comment(greeting, "/// \303\234n\303\257c\303\266d\303\251 d\303\263c c\303\266mm\303\253nt w\303\257th \303\244ccents \303\244nd an emoji \360\237\230\200\360\237\230\200\360\237\230\200 that is quite long.");
	const char *smileys = "'\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200'";
	Node *emoji = ds_toplevel(none, KW_CONST, NULL, "emoji",
				  ds_call(ds_id("$pb"), "f", NULL, ds_args(ds_str(smileys), ds_str("'abcdefghijk'"), ds_str("'xy'"), (Node *)0)));
	Node *ascii = ds_toplevel(none, KW_CONST, NULL, "ascii",
				  ds_call(ds_id("$pb"), "f", NULL,
					  ds_args(ds_str("'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'"), ds_str("'abcdefghijk'"), ds_str("'xy'"), (Node *)0)));
	Node *u = header(ds_unit(none, L(greeting, emoji, ascii)));
	Buf src = {0}, out = {0};
	ds_linearize(u, &src);
	const char *err = ds_format(u, buf_view(&src), &out);
	CHECK_STR(err ? err : buf_str(&out),
		  "// @dart = 3.3\n"
		  "/// \303\234n\303\257c\303\266d\303\251 d\303\263c c\303\266mm\303\253nt w\303\257th \303\244ccents \303\244nd an emoji \360\237\230\200\360\237\230\200\360\237\230\200 that is quite long.\n"
		  "const greeting = $pb.f('\320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200! \320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200! \320\237\321\200\320\270\320\262\320\265\321\202, \320\274\320\270\321\200!', '\320\237\321\200\320\270\320\262\320\265\321\202', 'x');\n"
		  "const emoji = $pb.f(\n"
		  "    '\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200\360\237\230\200', 'abcdefghijk', 'xy');\n"
		  "const ascii = $pb.f(\n"
		  "    'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', 'abcdefghijk', 'xy');\n");
}

/* Vertical layout hints: a trailing comma forces a split collection, blank lines between members
 * are kept, the linearized text has the layout of the hints. */
static void test_layout_hints(void)
{
	NodeList items = {0};
	ds_push(&items, ds_entry(ds_str("'1'"), ds_str("'Status'")));
	ds_push(&items, ds_entry(ds_str("'2'"), ds_int(-3)));
	Node *map = ds_map(NULL, NULL, items);
	map->flags |= DS_VERTICAL | DS_TRAILING_COMMA;
	Node *a = ds_toplevel(none, KW_CONST, NULL, "Status$json", map);
	Node *b = ds_toplevel(none, KW_CONST, NULL, "b", ds_int(1));
	ds_nl(b, 3, 0);
	Node *u = header(ds_unit(none, L(a, b)));
	check(u,
	      "// @dart = 3.3\nconst Status$json = {\n  '1': 'Status',\n  '2': -3,\n};\n\n\nconst b = 1;\n",
	      "// @dart = 3.3\nconst Status$json = {\n  '1': 'Status',\n  '2': -3,\n};\n\nconst b = 1;\n");
}

/* Without a `// @dart` comment below 3.7 the tall style would be used, which is not ported. */
static void test_language_version(void)
{
	Node *c = ds_class(none, false, "A", NULL, none, none);
	Buf src = {0}, out = {0};
	Node *u = ds_unit(none, L(c));
	ds_linearize(u, &src);
	const char *err = ds_format(u, buf_view(&src), &out);
	CHECK(err && strstr(err, "tall style"));
}

TESTS(TEST(test_class), TEST(test_long_call), TEST(test_generics), TEST(test_unicode), TEST(test_layout_hints),
      TEST(test_language_version))
