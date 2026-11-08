/* Node fixtures of the black 24.3.0 port: each test builds the blib2to3 tree of a source and
 * checks the output of black.format_str(src, mode=Mode(target_versions={PY37})) for it,
 * recorded with black 24.3.0 on Python 3.11.4. */
#include "black.h"
#include "test.h"

#define A(...) ARGS(__VA_ARGS__)
#define ARGS(...) (int)(sizeof((BkNode *const[]){__VA_ARGS__}) / sizeof(BkNode *)), ((BkNode *const[]){__VA_ARGS__})

static BkNode *field(const char *prefix, const char *name, BkNode *type, const char *ctor, int n, BkNode *const *args)
{
	return bk_stmt(prefix, bk_annassign(bk_name(name), type, bk_call(bk_attr(bk_name("betterproto"), ctor), n, args)));
}

static void test_simple(void)
{
	/* # c\nfrom a import b\n\nimport betterproto\n\n\n@dataclass\nclass X(betterproto.Message):\n    x: int = betterproto.int32_field(1)\n\n\n */
	const char *names[] = {"b"};
	BkNode *cls = bk_decorated("\n\n", "dataclass",
		bk_classdef("", "X", bk_attr(bk_name("betterproto"), "Message"),
			    bk_suite(1, (BkNode *const[]){field("    ", "x", bk_name("int"), "int32_field", ARGS(bk_number("1")))})));
	BkNode *file = bk_file(3, (BkNode *const[]){
		bk_stmt("# c\n", bk_import_from(0, "a", 1, names)),
		bk_stmt("\n", bk_import("betterproto")),
		cls}, "\n\n");
	CHECK_STR(bk_format_str(file),
		  "# c\nfrom a import b\n\nimport betterproto\n\n\n@dataclass\nclass X(betterproto.Message):\n"
		  "    x: int = betterproto.int32_field(1)\n");
}

static BkNode *sub(const char *base, int n, BkNode *const *items) { return bk_subscript(bk_name(base), n, items); }

static BkNode *unary(const char *fn, const char *route, const char *out)
{
	return bk_call_trailing_comma(bk_attr(bk_name("self"), fn),
				      ARGS(bk_string(route), bk_name("request"), bk_dotted(out)));
}

/* The splits black applies to the betterproto template: rhs with and without the optional
 * parentheses, delimiter_split with an added trailing comma (kept by the second pass), imports,
 * class lines, def lines (lhs, with the trailing comma after a lone `self`), magic trailing
 * commas, docstrings and comments. */
static void test_splits(void)
{
	const char *sd[] = {"shared_definitions"}, *typing[] = {"Dict", "List"};
	BkNode *cls1 = bk_classdef("\n\n\n", "AnExtremelyLongMessageNameThatMakesTheClassLineLongerThanTheLimitOfEightyEight",
		bk_attr(bk_name("betterproto"), "Message"), bk_suite(A(
			field("    # first comment in the block\n    ", "a_field_name_that_is_long_enough_to_push_the_call_over_the_limit_x",
			      bk_name("str"), "string_field", ARGS(bk_number("1"))),
			field("    ", "wrapped_integer_value_in_a_oneof_with_a_long_group", sub("Optional", ARGS(bk_name("int"))),
			      "message_field", ARGS(bk_number("7"),
				bk_kwarg("group", bk_string("\"a_oneof_group_with_a_rather_long_name_that_is_repeated_in_every_field\"")),
				bk_kwarg("wraps", bk_dotted("betterproto.TYPE_INT64")))),
			field("\n\n    ", "map_of_imported_messages_by_key",
			      sub("Dict", ARGS(bk_name("str"), bk_dotted("shared_definitions.AnExtremelyLongMessageNameThatMakesTheClassLineLongerThanTheLimit"))),
			      "map_field", ARGS(bk_number("4"), bk_dotted("betterproto.TYPE_STRING"), bk_dotted("betterproto.TYPE_MESSAGE"))),
			field("    ", "x", bk_name("int"), "int32_field", ARGS(bk_neg(bk_number("5")))),
			bk_stmt("    ", bk_assign(bk_name("NEGATIVE"), bk_neg(bk_number("2147483648")))))));
	BkNode *if_body = bk_stmt("            ", bk_assign(bk_attr(bk_name("request"), "first_parameter_name"), bk_name("first_parameter_name")));
	BkNode *m1 = bk_async("    ", bk_funcdef("", "method_with_a_very_long_name_that_needs_to_be_split_over_lines_xxxxxx",
		ARGS(bk_name("self")), bk_name("X"), bk_suite(A(
			bk_stmt("        ", bk_string("\"\"\"\\\"quoted\\\" docstring ending with a backslash\\\\\"\"\"")),
			bk_stmt("\n        ", bk_assign(bk_name("request"), bk_call(bk_dotted("protobuf.Empty"), 0, NULL))),
			bk_if("        ", bk_is_not(bk_name("first_parameter_name"), bk_name("None")), bk_suite(1, &if_body)),
			bk_stmt("\n        ", bk_return(bk_await(unary("_unary_unary", "\"/a.B/C\"", "X"))))))));
	BkNode *y = bk_stmt("            ", bk_yield(bk_name("response")));
	BkNode *m2 = bk_async("    ", bk_funcdef("", "m",
		ARGS(bk_name("self"), bk_star_param(), bk_param("first_parameter_name", bk_name("str"), bk_string("\"\"")),
		     bk_param("message_parameter", sub("Optional", ARGS(bk_string("\"LongFields\""))), bk_name("None")),
		     bk_param("flag", bk_name("bool"), bk_name("False"))),
		sub("AsyncGenerator", ARGS(bk_dotted("shared_definitions.Long"), bk_name("None"))),
		bk_suite(A(bk_stmt("        ", bk_assign(bk_name("request"), bk_call(bk_name("R"), 0, NULL))),
			   bk_async("\n        ", bk_for("", bk_name("response"), unary("_unary_stream", "\"/a.B/C\"", "X"), bk_suite(1, &y)))))));
	BkNode *cls2 = bk_classdef("", "Stub", bk_attr(bk_name("betterproto"), "ServiceStub"), bk_suite(A(
		bk_stmt("    ", bk_string("\"\"\"\n       Indented\tdocstring with \"quotes\" and tabs   \n         second line\n    \"\"\"")),
		m1, m2)));
	BkNode *file = bk_file(5, (BkNode *const[]){
		bk_stmt("#!shebang-like comment\n#comment without space\n",
			bk_import_from(1, "a_very_long_package_name_for_imports.of_the_python_formatter", 1, sd)),
		bk_stmt("", bk_import_from(0, "typing", 2, typing)),
		bk_stmt("", bk_import("betterproto")),
		cls1, cls2}, "");
	CHECK_STR(bk_format_str(file),
		"#!shebang-like comment\n"
		"# comment without space\n"
		"from .a_very_long_package_name_for_imports.of_the_python_formatter import (\n"
		"    shared_definitions,\n"
		")\n"
		"from typing import Dict, List\n"
		"import betterproto\n"
		"\n"
		"\n"
		"class AnExtremelyLongMessageNameThatMakesTheClassLineLongerThanTheLimitOfEightyEight(\n"
		"    betterproto.Message\n"
		"):\n"
		"    # first comment in the block\n"
		"    a_field_name_that_is_long_enough_to_push_the_call_over_the_limit_x: str = (\n"
		"        betterproto.string_field(1)\n"
		"    )\n"
		"    wrapped_integer_value_in_a_oneof_with_a_long_group: Optional[int] = (\n"
		"        betterproto.message_field(\n"
		"            7,\n"
		"            group=\"a_oneof_group_with_a_rather_long_name_that_is_repeated_in_every_field\",\n"
		"            wraps=betterproto.TYPE_INT64,\n"
		"        )\n"
		"    )\n"
		"\n"
		"    map_of_imported_messages_by_key: Dict[\n"
		"        str,\n"
		"        shared_definitions.AnExtremelyLongMessageNameThatMakesTheClassLineLongerThanTheLimit,\n"
		"    ] = betterproto.map_field(4, betterproto.TYPE_STRING, betterproto.TYPE_MESSAGE)\n"
		"    x: int = betterproto.int32_field(-5)\n"
		"    NEGATIVE = -2147483648\n"
		"\n"
		"\n"
		"class Stub(betterproto.ServiceStub):\n"
		"    \"\"\"\n"
		"    Indented\tdocstring with \"quotes\" and tabs\n"
		"      second line\n"
		"    \"\"\"\n"
		"\n"
		"    async def method_with_a_very_long_name_that_needs_to_be_split_over_lines_xxxxxx(\n"
		"        self,\n"
		"    ) -> X:\n"
		"        \"\"\"\\\"quoted\\\" docstring ending with a backslash\\\\\"\"\"\n"
		"\n"
		"        request = protobuf.Empty()\n"
		"        if first_parameter_name is not None:\n"
		"            request.first_parameter_name = first_parameter_name\n"
		"\n"
		"        return await self._unary_unary(\n"
		"            \"/a.B/C\",\n"
		"            request,\n"
		"            X,\n"
		"        )\n"
		"\n"
		"    async def m(\n"
		"        self,\n"
		"        *,\n"
		"        first_parameter_name: str = \"\",\n"
		"        message_parameter: Optional[\"LongFields\"] = None,\n"
		"        flag: bool = False,\n"
		"    ) -> AsyncGenerator[shared_definitions.Long, None]:\n"
		"        request = R()\n"
		"\n"
		"        async for response in self._unary_stream(\n"
		"            \"/a.B/C\",\n"
		"            request,\n"
		"            X,\n"
		"        ):\n"
		"            yield response\n");
}

static void test_checks(void)
{
	CHECK(bk_docstring_ok(STR_LIT("a \\\"\"\" b")));
	CHECK(!bk_docstring_ok(STR_LIT("a \"\"\" b")));
	CHECK(!bk_docstring_ok(STR_LIT("ends with \\")));
	CHECK(bk_docstring_ok(STR_LIT("ends with \\\\")));
	CHECK(bk_comment_ok(STR_LIT("# fmt: onward")));
	CHECK(!bk_comment_ok(STR_LIT("# fmt: off")));
	CHECK(!bk_comment_ok(STR_LIT("#fmt:skip")));
	CHECK(!bk_comment_ok(STR_LIT("# noqa # fmt: skip")));
	CHECK(!bk_comment_ok(STR_LIT("# pylint: x; fmt: skip")));
	CHECK(!bk_comment_ok(STR_LIT("# yapf: disable")));
	CHECK(bk_comment_ok(STR_LIT("# yapf: enable")));
}

TESTS(TEST(test_simple), TEST(test_splits), TEST(test_checks))
