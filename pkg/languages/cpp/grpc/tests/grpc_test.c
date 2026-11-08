/* Unit tests of grpc_plugin_support (grpc.h). Expected values follow the C++ of
 * src/compiler/generator_helpers.h and proto_parser_helper.cc (grpc v1.85.1): std::getline,
 * std::string::find_first_of and the "C" locale. */
#include "grpc.h"

#include <string.h>

#include "test.h"

static char *joined(const Vec *v, bool str)
{
	Buf b = {0};
	for (int i = 0; i < v->n; i++) {
		buf_addc(&b, '[');
		if (str)
			buf_add_str(&b, *(const Str *)v->v[i]);
		else
			buf_adds(&b, v->v[i]);
		buf_addc(&b, ']');
	}
	return (char *)buf_str(&b);
}

static void test_tokenize(void)
{
	Vec t = grpc_tokenize("a/b//c/", "/");
	CHECK_STR(joined(&t, false), "[a][b][][c][]");
	t = grpc_tokenize("", "_");
	CHECK_STR(joined(&t, false), "[]");
	CHECK_STR(grpc_lower_underscore_to_upper_camel("foo_bar__baz_"), "FooBarBaz");
	LpFile file = {.name = "dir/sub/my_file.PROTO"};
	CHECK_STR(grpc_file_name_in_upper_camel(&file, true), "dir/sub/MyFile");
	CHECK_STR(grpc_file_name_in_upper_camel(&file, false), "MyFile");
}

static void test_strings(void)
{
	CHECK_STR(grpc_strip_proto("a.protodevel"), "a");
	CHECK_STR(grpc_strip_proto("a.Proto"), "a");
	CHECK_STR(grpc_strip_proto("a.pb"), "a.pb");
	char *s = "prefix_rest";
	CHECK(grpc_strip_prefix(&s, "prefix_") && str_eq(s, "rest"));
	CHECK(!grpc_strip_prefix(&s, "x"));
	CHECK_STR(grpc_string_replace("a.b.c", ".", "::", true), "a::b::c");
	CHECK_STR(grpc_string_replace("a.b.c", ".", "::", false), "a::b.c");
	CHECK_STR(grpc_capitalize_first_letter("abc"), "Abc");
	CHECK_STR(grpc_lowercase_first_letter("ABC"), "aBC");
	CHECK_STR(grpc_to_lower("AbC1"), "abc1");
}

static void test_comments(void)
{
	Vec lines = {0};
	grpc_split(STR_LIT(" one\n\ntwo $x$\n"), &lines);
	CHECK_STR(joined(&lines, true), "[ one][][two $x$]");
	lines.n = 0;
	grpc_split(STR_LIT("\n"), &lines);
	grpc_split(STR_LIT(""), &lines);
	grpc_split(STR_LIT("last"), &lines);
	CHECK_STR(joined(&lines, true), "[][last]");
	Vec in = {0};
	grpc_split(STR_LIT(" one\n\ntwo $x$\n"), &in);
	Str out = grpc_generate_comments_with_prefix(&in, "#");
	CHECK_STR(out.p, "# one\n#\n# two $$x$$\n");
	Str e = grpc_escape_variable_delimiters(STR_LIT("$a\0$"));
	CHECK_MEM(e.p, e.n, "$$a\0$$", 6);
}

TESTS(TEST(test_tokenize), TEST(test_strings), TEST(test_comments))
