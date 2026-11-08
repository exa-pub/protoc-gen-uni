/* Unit tests of pkg/languages/python/protobuf-py. The expected values were printed by protobuf-py
 * 0.7.0 on CPython 3.13 (_glob_to_regex(g).fullmatch, _c_unescape, the escape functions of
 * _sanitization.py, proto_camel_case). */
#include "plugin/internal.h"

#include "test.h"

#include <string.h>

static void test_map_imports(void)
{
	static const struct { const char *glob, *name; bool match; } cases[] = {
		{"**/*.proto", "a/b/c.proto", true},
		{"**/*.proto", "c.proto", true},
		{"**/*.proto", "/c.proto", false},
		{"a/", "a/b/c", true},
		{"a/", "a", false},
		{"*.proto", "a/b.proto", false},
		{"**", "a\nb", false},
		{"a/**/b.proto", "a/b.proto", true},
		{"a/**/b.proto", "a/x/y/b.proto", true},
		{"a*b", "ab", true},
		{"a*b", "a/b", false},
		{"x.proto", "xxproto", false},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		Vec keys = {0}, values = {0};
		vec_push(&keys, (void *)cases[i].glob);
		vec_push(&values, "t");
		PbpyMapImports m = pbpy_compile_map_imports(&keys, &values);
		CHECK((pbpy_map_import_target(&m, cases[i].name) != NULL) == cases[i].match);
	}
}

static void unescape(void *arg) { pbpy_c_unescape(str_c(*(const char **)arg)); }

static void test_c_unescape(void)
{
	static const struct { const char *in, *want; size_t n; } cases[] = {
		{"\\x1\\x41\\101\\7\\0xff", "\x01" "AA\x07" "\x00" "xff", 8},
		{"\\\\x1", "\\x1", 3},
		{"\\\\\\x1", "\\\x01", 2},
		{"a\\'b", "a'b", 3},
		{"\\u00e9", "\xc3\xa9", 2},
		{"\\z", "\\z", 2},
		{"\\777", "\x1f" "f", 2},
		{"\\1234", "S4", 2},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		Str got = pbpy_c_unescape(str_c(cases[i].in));
		CHECK_MEM(got.p, got.n, cases[i].want, cases[i].n);
	}
	static const struct { const char *in, *want; } errors[] = {
		{"\\xg", "UnicodeDecodeError: 'unicodeescape' codec can't decode bytes in position 0-1: truncated \\xXX escape"},
		{"\xc3\xa9\\xq", "UnicodeDecodeError: 'unicodeescape' codec can't decode bytes in position 2-3: truncated \\xXX escape"},
		{"x\\", "UnicodeDecodeError: 'unicodeescape' codec can't decode byte 0x5c in position 1: \\ at end of string"},
		{"ab\\u12", "UnicodeDecodeError: 'rawunicodeescape' codec can't decode bytes in position 2-5: truncated \\uXXXX escape"},
		{"\\\\\\u1", "UnicodeDecodeError: 'rawunicodeescape' codec can't decode bytes in position 2-4: truncated \\uXXXX escape"},
		{"\\U00110000", "UnicodeDecodeError: 'rawunicodeescape' codec can't decode bytes in position 0-9: \\Uxxxxxxxx out of range"},
		{"\\ud800\\udc00", "UnicodeEncodeError: 'utf-8' codec can't encode characters in position 0-1: surrogates not allowed"},
		{"a\\ud800", "UnicodeEncodeError: 'utf-8' codec can't encode character '\\ud800' in position 1: surrogates not allowed"},
		{"\\u005cu0100\\u005cu0101", "UnicodeEncodeError: 'latin-1' codec can't encode characters in position 0-1: ordinal not in range(256)"},
		{"a\\u005cU0001F600", "UnicodeEncodeError: 'latin-1' codec can't encode character '\\U0001f600' in position 1: ordinal not in range(256)"},
	};
	for (size_t i = 0; i < sizeof errors / sizeof *errors; i++) {
		const char *in = errors[i].in;
		CHECK(try_run(unescape, &in) != NULL);
		Str msg = fail_message();
		CHECK_MEM(msg.p, msg.n, errors[i].want, strlen(errors[i].want));
	}
}

static void test_sanitization(void)
{
	static const struct { const char *in, *plain, *hash; } modules[] = {
		{"foo", "foo", "foo"},
		{"class", "class_", "class_"},
		{"1abc", "pb__1abc", "pb__1abc"},
		{"_x", "pb__x", "pb__x"},
		{"pb_x", "pb_pb_x", "pb_pb_x"},
		{"foo_pb", "foo_pb_", "foo_pb_"},
		{"a-b", "a_b", "a_b_d44362d6"},
		{"a.b", "a_b", "a_b_2e7336dc"},
		{"\xc3\xa9", "pb__", "pb___4a99557e"},
	};
	for (size_t i = 0; i < sizeof modules / sizeof *modules; i++) {
		CHECK_STR(pbpy_escape_proto_module_component(modules[i].in, false), modules[i].plain);
		CHECK_STR(pbpy_escape_proto_module_component(modules[i].in, true), modules[i].hash);
	}
	static const struct { const char *in, *attr, *cls, *enm; } names[] = {
		{"self", "self_", "self_", "self"},
		{"int_", "int__", "int__", "int_"},
		{"ext_a", "pb_ext_a", "pb_ext_a", "ext_a"},
		{"_a", "pb__a", "pb__a", "pb__a"},
		{"to_json__", "to_json___", "to_json___", "to_json__"},
		{"from", "from_", "from_", "from_"},
		{"name", "name", "name", "name_"},
	};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
		CHECK_STR(pbpy_escape_message_attr(names[i].in), names[i].attr);
		CHECK_STR(pbpy_escape_class_name(names[i].in), names[i].cls);
		CHECK_STR(pbpy_escape_enum_attr(names[i].in), names[i].enm);
	}
	static const struct { const char *in, *want; } camel[] = {
		{"foo_bar", "fooBar"}, {"foo__bar", "fooBar"}, {"_foo", "Foo"}, {"foo_", "foo"}, {"a_b_c1_d", "aBC1D"},
	};
	for (size_t i = 0; i < sizeof camel / sizeof *camel; i++)
		CHECK_STR(pbpy_proto_camel_case(camel[i].in), camel[i].want);
}

static const PbpyOptMember KINDS[] = {{"KIND", "kind"}, {"IO", "io"}, {NULL, NULL}};
static const PbpyOptDef KIND_OPTS[] = {{.name = "k", .type = PBPY_OPT_STR_ENUM, .enum_name = "_Kind", .members = KINDS, .optional = true}};

static void parse_kind(void *arg)
{
	PbpyOptValue v[1] = {{0}};
	pbpy_parse_options(KIND_OPTS, 1, str_c(*(const char **)arg), v, "");
	*(const char **)arg = v[0].s;
}

/* StrEnum options: a member by name or value, compared after str.lower() */
static void test_options_str_enum(void)
{
	static const struct { const char *param, *want; } cases[] = {
		{"", NULL}, {"k=KIND", "kind"}, {"k=Kind", "kind"}, {"k=io", "io"}, {"k=\xe2\x84\xaaind", "kind"},
		{"k=\xc4\xb0o", "\x01"}, {"k=kinds", "\x01"}, {"k=", "\x01"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		const char *arg = cases[i].param;
		const char *err = try_run(parse_kind, &arg);
		if (cases[i].want && str_eq(cases[i].want, "\x01")) {
			CHECK(err != NULL);
			CHECK(strstr(err, "\n    return _parse_scalar(name, inner[0], raw_value)\n") != NULL);
			CHECK(strstr(err, xcat("ValueError: option 'k': '", cases[i].param + 2, "' is not a valid _Kind; allowed: KIND, IO")) != NULL);
		} else {
			CHECK(err == NULL);
			CHECK(cases[i].want ? arg && str_eq(arg, cases[i].want) : !arg);
		}
	}
}

TESTS(TEST(test_map_imports), TEST(test_c_unescape), TEST(test_sanitization), TEST(test_options_str_enum))
