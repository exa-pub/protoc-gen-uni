/* Unit tests of pkg/languages/rust. Expected values are the upstream tests: heck 0.5.0
 * (the tests in src/), prost-build 0.14.1 (src/ident.rs, src/ast.rs) and protoc-gen-prost 0.5.0
 * (src/lib.rs), plus Rust std behavior. */
#include "heck.h"
#include "proc_macro2.h"
#include "prost.h"
#include "rust_std.h"
#include "test.h"

typedef struct {
	const char *in, *want;
} Case;

#define N(a) (sizeof(a) / sizeof *(a))

static void check_cases(char *(*fn)(const char *), const Case *cases, size_t n)
{
	for (size_t i = 0; i < n; i++)
		CHECK_STR(fn(cases[i].in), cases[i].want);
}

static void test_heck_snake(void)
{
	static const Case cases[] = {
		{"CamelCase", "camel_case"},
		{"This is Human case.", "this_is_human_case"},
		{"MixedUP CamelCase, with some Spaces", "mixed_up_camel_case_with_some_spaces"},
		{"mixed_up_ snake_case with some _spaces", "mixed_up_snake_case_with_some_spaces"},
		{"kebab-case", "kebab_case"},
		{"SHOUTY_SNAKE_CASE", "shouty_snake_case"},
		{"snake_case", "snake_case"},
		{"this-contains_ ALLKinds OfWord_Boundaries", "this_contains_all_kinds_of_word_boundaries"},
		{"XMLHttpRequest", "xml_http_request"},
		{"FIELD_NAME11", "field_name11"},
		{"99BOTTLES", "99bottles"},
		{"FieldNamE11", "field_nam_e11"},
		{"abc123def456", "abc123def456"},
		{"abc123DEF456", "abc123_def456"},
		{"abc123Def456", "abc123_def456"},
		{"abc123DEf456", "abc123_d_ef456"},
		{"ABC123def456", "abc123def456"},
		{"ABC123DEF456", "abc123def456"},
		{"ABC123Def456", "abc123_def456"},
		{"ABC123DEf456", "abc123d_ef456"},
		{"ABC123dEEf456FOO", "abc123d_e_ef456_foo"},
		{"abcDEF", "abc_def"},
		{"ABcDE", "a_bc_de"},
		{"", ""},
	};
	check_cases(heck_snake, cases, N(cases));
}

static void test_heck_camel(void)
{
	static const Case upper[] = {
		{"CamelCase", "CamelCase"},
		{"This is Human case.", "ThisIsHumanCase"},
		{"MixedUP_CamelCase, with some Spaces", "MixedUpCamelCaseWithSomeSpaces"},
		{"mixed_up_ snake_case, with some _spaces", "MixedUpSnakeCaseWithSomeSpaces"},
		{"kebab-case", "KebabCase"},
		{"SHOUTY_SNAKE_CASE", "ShoutySnakeCase"},
		{"snake_case", "SnakeCase"},
		{"this-contains_ ALLKinds OfWord_Boundaries", "ThisContainsAllKindsOfWordBoundaries"},
		{"XMLHttpRequest", "XmlHttpRequest"},
	};
	check_cases(heck_upper_camel, upper, N(upper));
	static const Case lower[] = {
		{"CamelCase", "camelCase"},
		{"This is Human case.", "thisIsHumanCase"},
		{"MixedUP CamelCase, with some Spaces", "mixedUpCamelCaseWithSomeSpaces"},
		{"mixed_up_ snake_case, with some _spaces", "mixedUpSnakeCaseWithSomeSpaces"},
		{"kebab-case", "kebabCase"},
		{"SHOUTY_SNAKE_CASE", "shoutySnakeCase"},
		{"snake_case", "snakeCase"},
		{"this-contains_ ALLKinds OfWord_Boundaries", "thisContainsAllKindsOfWordBoundaries"},
		{"XMLHttpRequest", "xmlHttpRequest"},
	};
	check_cases(heck_lower_camel, lower, N(lower));
}

static void test_heck_other(void)
{
	static const Case shouty[] = {
		{"CamelCase", "CAMEL_CASE"},
		{"This is Human case.", "THIS_IS_HUMAN_CASE"},
		{"MixedUP CamelCase, with some Spaces", "MIXED_UP_CAMEL_CASE_WITH_SOME_SPACES"},
		{"XMLHttpRequest", "XML_HTTP_REQUEST"},
	};
	check_cases(heck_shouty_snake, shouty, N(shouty));
	static const Case kebab[] = {
		{"MixedUP CamelCase, with some Spaces", "mixed-up-camel-case-with-some-spaces"},
		{"\xd9\x84\xd9\x90\xd9\x86\xd9\x8e \xd8\xa5\xd9\x90", "\xd9\x84\xd9\x90\xd9\x86\xd9\x8e-\xd8\xa5\xd9\x90"},
		{"\xe7\xa5\x9d\xe4\xbd\xa0\xe4\xb8\x80", "\xe7\xa5\x9d\xe4\xbd\xa0\xe4\xb8\x80"},
	};
	check_cases(heck_kebab, kebab, N(kebab));
	static const Case train[] = {
		{"ABC123dEEf456FOO", "Abc123d-E-Ef456-Foo"},
		{"ABcDE", "A-Bc-De"},
		{"99BOTTLES", "99bottles"},
	};
	check_cases(heck_train, train, N(train));
	CHECK_STR(heck_title("XMLHttpRequest"), "Xml Http Request");
	CHECK_STR(heck_shouty_kebab("SHOUTY-KEBAB-CASE"), "SHOUTY-KEBAB-CASE");
}

static void test_prost_ident(void)
{
	CHECK_STR(prost_sanitize_identifier("as"), "r#as");
	CHECK_STR(prost_sanitize_identifier("gen"), "r#gen");
	CHECK_STR(prost_sanitize_identifier("self"), "self_");
	CHECK_STR(prost_sanitize_identifier("crate"), "crate_");
	CHECK_STR(prost_sanitize_identifier("_"), "__");
	CHECK_STR(prost_sanitize_identifier("0foo"), "_0foo");
	CHECK_STR(prost_sanitize_identifier("foo0"), "foo0");
	CHECK_STR(prost_sanitize_identifier("_foo"), "_foo");
	static const Case snake[] = {
		{"FooBar", "foo_bar"}, {"FooBarBAZ", "foo_bar_baz"}, {"XMLHttpRequest", "xml_http_request"},
		{"While", "r#while"}, {"FUZZ_BUSTER", "fuzz_buster"}, {"foo_bar_baz", "foo_bar_baz"},
		{"FUZZ_buster", "fuzz_buster"}, {"_FUZZ", "fuzz"}, {"_fuzz", "fuzz"}, {"_Fuzz", "fuzz"},
		{"FUZZ_", "fuzz"}, {"fuzz_", "fuzz"}, {"Fuzz_", "fuzz"}, {"FuzZ_", "fuz_z"},
		{"fieldname1", "fieldname1"}, {"field_name2", "field_name2"}, {"_field_name3", "field_name3"},
		{"field__name4_", "field_name4"}, {"field0name5", "field0name5"}, {"field_0_name6", "field_0_name6"},
		{"fieldName7", "field_name7"}, {"FieldName8", "field_name8"}, {"field_Name9", "field_name9"},
		{"Field_Name10", "field_name10"}, {"FIELD_NAME11", "field_name11"}, {"FIELD_name12", "field_name12"},
		{"__field_name13", "field_name13"}, {"__Field_name14", "field_name14"},
		{"field__name15", "field_name15"}, {"field__Name16", "field_name16"},
		{"field_name17__", "field_name17"}, {"Field_name18__", "field_name18"},
		{"self", "self_"}, {"super", "super_"}, {"extern", "extern_"}, {"type", "r#type"},
	};
	check_cases(prost_to_snake, snake, N(snake));
	static const Case camel[] = {
		{"", ""}, {"F", "F"}, {"FOO", "Foo"}, {"FOO_BAR", "FooBar"}, {"_FOO_BAR", "FooBar"},
		{"FOO_BAR_", "FooBar"}, {"_FOO_BAR_", "FooBar"}, {"fuzzBuster", "FuzzBuster"},
		{"FuzzBuster", "FuzzBuster"}, {"self", "Self_"},
	};
	check_cases(prost_to_upper_camel, camel, N(camel));
	CHECK_STR(prost_strip_enum_prefix("Foo", "FooBar"), "Bar");
	CHECK_STR(prost_strip_enum_prefix("Foo", "Foobar"), "Foobar");
	CHECK_STR(prost_strip_enum_prefix("Foo", "Foo"), "Foo");
	CHECK_STR(prost_strip_enum_prefix("Foo", "Bar"), "Bar");
	CHECK_STR(prost_strip_enum_prefix("Foo", "Foo1"), "Foo1");
	CHECK_STR(prost_strip_enum_prefix("Foo", "FooSelf"), "Self_");
}

static char *sanitize(const char *line)
{
	Buf b = {0};
	prost_sanitize_line(str_c(line), &b);
	return xstrdup(buf_str(&b));
}

static void test_prost_sanitize_line(void)
{
	static const Case cases[] = {
		/* test_comment_append_with_indent_leaves_prespaced_lines */
		{" A line with a single leading space.", " A line with a single leading space."},
		{"A line without a single leading space.", " A line without a single leading space."},
		{"", ""},
		{"  a line with several leading spaces, such as in a markdown list",
		 "   a line with several leading spaces, such as in a markdown list"},
		/* ..._sanitizes_comment_doc_url */
		{"See https://www.rust-lang.org/", " See <https://www.rust-lang.org/>"},
		{"See (https://www.rust-lang.org/)", " See (<https://www.rust-lang.org/>)"},
		{"See note://abc", " See note://abc"},
		{"http:// x", " http:// x"},
		{"httpss://a https://b", " httpss://a <https://b>"},
		/* ..._sanitizes_square_brackets */
		{"foo [bar] baz", " foo \\[bar\\] baz"},
		{"foo [= baz", " foo [= baz"},
		{"foo =] baz", " foo =] baz"},
		{"[0, 9)", " [0, 9)"},
		{"foo [bar](bar) baz", " foo [bar](bar) baz"},
		{"foo [bar]", " foo \\[bar\\]"},
		{"foo [bar]baz", " foo \\[bar\\]baz"},
		{"foo []", " foo \\[\\]"},
		{"foo []()", " foo []()"},
		{"foo [bar][bar] baz", " foo [bar][bar] baz"},
		{"foo [bar][baz]", " foo [bar][baz]"},
		{"[bar][baz]", " [bar][baz]"},
		{"\\[bar\\]\\[baz\\]", " \\[bar\\]\\[baz\\]"},
		{"\\[\\]\\[\\]", " \\[\\]\\[\\]"},
		/* more cases checked against the regex crate */
		{"[a]", " \\[a\\]"},
		{"[a]]", " \\[a]\\]"},
		{"x[a] [b] c", " x\\[a\\] [b] c"}, /* the space before [b] is group 4 of the first match */
	};
	check_cases(sanitize, cases, N(cases));
}

static void test_prost_comments(void)
{
	Buf b = {0};
	Str *lines;
	int n = rust_lines(STR_LIT(" a\r\n\nb\r"), &lines);
	CHECK(n == 3);
	if (n == 3) {
		CHECK_MEM(lines[0].p, lines[0].n, " a", 2);
		CHECK(lines[1].n == 0);
		CHECK_MEM(lines[2].p, lines[2].n, "b\r", 2);
	}
	CHECK(rust_lines(STR_LIT(""), &lines) == 0);
	CHECK(rust_lines(STR_LIT("\n"), &lines) == 1);
	ProstComments c = {0};
	Str leading[] = {STR_LIT(" lead")}, trailing[] = {STR_LIT("trail")};
	c.leading = leading, c.n_leading = 1;
	c.trailing = trailing, c.n_trailing = 1;
	prost_comments_append_with_indent(&c, 1, &b);
	CHECK_STR(buf_str(&b), "    /// lead\n    ///\n    /// trail\n");
}

static void test_prost_params(void)
{
	ProstParam *p;
	int n = prost_params_parse(
		str_c("flat_output_dir,enable_type_names,compile_well_known_types,disable_comments=.,skip_debug=.,"
		      "extern_path=.google.protobuf=::pbjson_types,"
		      "type_attribute=.=#[cfg(all(feature = \"test\"\\, feature = \"orange\"))]"),
		&p);
	CHECK(n == 7);
	if (n != 7)
		return;
	CHECK(p[0].kind == PROST_PARAMETER && str_eq(p[0].param, "flat_output_dir"));
	CHECK(p[2].kind == PROST_PARAMETER && str_eq(p[2].param, "compile_well_known_types"));
	CHECK(p[3].kind == PROST_VALUE && str_eq(p[3].param, "disable_comments") && str_eq(p[3].value, "."));
	CHECK(p[5].kind == PROST_KEY_VALUE && str_eq(p[5].key, ".google.protobuf") && str_eq(p[5].value, "::pbjson_types"));
	CHECK(p[6].kind == PROST_KEY_VALUE && str_eq(p[6].param, "type_attribute") && str_eq(p[6].key, "."));
	CHECK_STR(p[6].value, "#[cfg(all(feature = \"test\", feature = \"orange\"))]");
	CHECK_STR(prost_invalid_parameter(&p[6]),
		  "invalid parameter: type_attribute=.=#[cfg(all(feature = \"test\"\\, feature = \"orange\"))]");
	n = prost_params_parse(str_c(" a ,b=c=,=d,e==f,g=h=i\\j"), &p);
	CHECK(n == 7);
	if (n != 7)
		return;
	CHECK(p[0].kind == PROST_PARAMETER && str_eq(p[0].param, "a"));
	CHECK(p[1].kind == PROST_VALUE && str_eq(p[1].param, "b") && str_eq(p[1].value, "c"));
	CHECK(p[2].kind == PROST_PARAMETER && str_eq(p[2].param, "d"));
	CHECK(p[3].kind == PROST_PARAMETER && str_eq(p[3].param, "e"));
	CHECK(p[4].kind == PROST_PARAMETER && str_eq(p[4].param, "f"));
	CHECK(p[5].kind == PROST_KEY_VALUE && str_eq(p[5].key, "h") && str_eq(p[5].value, "i"));
	CHECK(p[6].kind == PROST_PARAMETER && str_eq(p[6].param, "\\j"));
	CHECK_STR(prost_invalid_parameter(&p[1]), "invalid parameter: b=c");
	bool on = false;
	CHECK(prost_param_flag(&p[0], "a", &on) && on);
	n = prost_params_parse(str_c("x=false,x=true,x=maybe"), &p);
	on = false;
	CHECK(n == 3 && prost_param_flag(&p[0], "x", &on) && !on);
	CHECK(prost_param_flag(&p[1], "x", &on) && on);
	CHECK(!prost_param_flag(&p[2], "x", &on));
}

static void test_prost_module(void)
{
	ProstModule *m = prost_module_from_package("foo.Bar..type");
	CHECK(m->n == 3);
	CHECK_STR(prost_module_display(m), "foo::bar::r#type");
	CHECK_STR(prost_module_to_file_name_or(m, "_"), "foo.bar.r#type.rs");
	CHECK_STR(prost_module_to_file_name_or(prost_module_from_package(""), "_"), "_.rs");
	CHECK(prost_module_cmp(prost_module_from_package("a"), prost_module_from_package("a.b")) < 0);
	CHECK(prost_module_cmp(prost_module_from_package("b"), prost_module_from_package("a.b")) > 0);
}

#define DECODE_ERR "failed to decode Protobuf message: "
#define NOT_UTF8 "invalid string value: data is not UTF-8 encoded"

/* prost_decode_request: the texts follow prost 0.14.1 (src/error.rs Display, src/encoding.rs). */
static void test_prost_decode(void)
{
	CHECK(!prost_decode_request(STR_LIT("")));
	CHECK(!prost_decode_request(STR_LIT("\x0a\x03\x61\xc3\xa9")));
	CHECK_STR(prost_decode_request(STR_LIT("\x0a\x01\xff")),
		  DECODE_ERR "CodeGeneratorRequest.file_to_generate: " NOT_UTF8);
	CHECK_STR(prost_decode_request(STR_LIT("\x0a\x03\xed\xa0\x80")), /* a surrogate */
		  DECODE_ERR "CodeGeneratorRequest.file_to_generate: " NOT_UTF8);
	CHECK_STR(prost_decode_request(STR_LIT("\x08\x01")),
		  DECODE_ERR "CodeGeneratorRequest.file_to_generate: invalid wire type: Varint (expected "
			     "LengthDelimited)");
	CHECK_STR(prost_decode_request(STR_LIT("\x00")), DECODE_ERR "invalid tag value: 0");
	CHECK_STR(prost_decode_request(STR_LIT("\x0f")), DECODE_ERR "invalid wire type value: 7");
	CHECK_STR(prost_decode_request(STR_LIT("\x80")), DECODE_ERR "invalid varint");
	CHECK_STR(prost_decode_request(STR_LIT("\x80\x80\x80\x80\x80\x10")),
		  DECODE_ERR "invalid key value: 549755813888");
	CHECK_STR(prost_decode_request(STR_LIT("\xff\xff\xff\xff\xff\xff\xff\xff\xff\x02")),
		  DECODE_ERR "invalid varint");
	CHECK_STR(prost_decode_request(STR_LIT("\x0a\x05\x61")),
		  DECODE_ERR "CodeGeneratorRequest.file_to_generate: buffer underflow");
	/* unknown fields are skipped, groups to the matching end */
	CHECK(!prost_decode_request(STR_LIT("\x9b\x06\x08\x01\x9c\x06\x88\x01\x05")));
	CHECK_STR(prost_decode_request(STR_LIT("\x9b\x06\x08\x01\x94\x06")), DECODE_ERR "unexpected end group tag");
	CHECK_STR(prost_decode_request(STR_LIT("\x9c\x06")), DECODE_ERR "unexpected end group tag");
	CHECK_STR(prost_decode_request(STR_LIT("\x9d\x06\x01")), DECODE_ERR "buffer underflow");
	/* nested values may read past their message, then the message is too long */
	CHECK_STR(prost_decode_request(STR_LIT("\x7a\x02\x0a\x05\x61\x62\x63\x64\x65")),
		  DECODE_ERR "CodeGeneratorRequest.proto_file: delimited length exceeded");
	CHECK_STR(prost_decode_request(STR_LIT("\x7a\x04\x12\x02\xc3\x28")),
		  DECODE_ERR "FileDescriptorProto.package: CodeGeneratorRequest.proto_file: invalid string value: data "
			     "is not UTF-8 encoded");
	/* fields unknown to prost-types 0.14.1: FileDescriptorProto.option_dependency, edition */
	CHECK(!prost_decode_request(STR_LIT("\x7a\x05\x7a\x01\xff\x70\x02")));
	/* packed and unpacked repeated varints: Location.path */
	CHECK(!prost_decode_request(STR_LIT("\x7a\x08\x4a\x06\x0a\x04\x0a\x02\x01\x02")));
	CHECK(!prost_decode_request(STR_LIT("\x7a\x06\x4a\x04\x0a\x02\x08\x01")));
	CHECK_STR(prost_decode_request(STR_LIT("\x7a\x07\x4a\x05\x0a\x03\x0a\x01\x80")),
		  DECODE_ERR "Location.path: SourceCodeInfo.location: FileDescriptorProto.source_code_info: "
			     "CodeGeneratorRequest.proto_file: invalid varint");
	CHECK_STR(prost_decode_request(STR_LIT("\x7a\x06\x4a\x04\x0a\x02\x0d\x01")),
		  DECODE_ERR "Location.path: SourceCodeInfo.location: FileDescriptorProto.source_code_info: "
			     "CodeGeneratorRequest.proto_file: invalid wire type: ThirtyTwoBit (expected Varint)");
	/* the field name of FieldDescriptorProto.type is a raw identifier */
	CHECK_STR(prost_decode_request(STR_LIT("\x7a\x06\x22\x04\x12\x02\x2a\x00")),
		  DECODE_ERR "FieldDescriptorProto.r#type: DescriptorProto.field: FileDescriptorProto.message_type: "
			     "CodeGeneratorRequest.proto_file: invalid wire type: LengthDelimited (expected Varint)");
	/* recursion limit: proto_file (99 left), message_type (98), then nested_type */
	for (int depth = 97; depth <= 99; depth++) {
		Buf m = {0};
		buf_adds(&m, "\x0a\x01M");
		for (int i = 0; i < depth; i++) {
			Buf o = {0};
			buf_adds(&o, "\x0a\x01M\x1a");
			pb_put_varint(&o, m.n);
			buf_add(&o, m.p, m.n);
			m = o;
		}
		Buf f = {0}, r = {0};
		pb_put_bytes(&f, 4, buf_view(&m));
		pb_put_bytes(&r, 15, buf_view(&f));
		const char *e = prost_decode_request(buf_view(&r));
		if (depth < 99)
			CHECK(!e);
		else
			CHECK(e && has_suffix(e, "DescriptorProto.nested_type: FileDescriptorProto.message_type: "
						 "CodeGeneratorRequest.proto_file: recursion limit reached"));
	}
}

static void test_rust_std(void)
{
	Buf b = {0};
	rust_str_escape_default(&b, str_c("a\"\t\\\x01\xc3\xa9'"));
	CHECK_STR(buf_str(&b), "a\\\"\\t\\\\\\u{1}\\u{e9}\\'");
	b = (Buf){0};
	rust_ascii_escape_default(&b, 0xff);
	rust_ascii_escape_default(&b, 0);
	rust_ascii_escape_default(&b, '\'');
	CHECK_STR(buf_str(&b), "\\xff\\x00\\'");
	b = (Buf){0};
	rust_str_debug(&b, str_c("a::B\"\x01'"));
	CHECK_STR(buf_str(&b), "\"a::B\\\"\\u{1}'\"");
	Str t = rust_trim(str_c("\xe3\x80\x80 x \xc2\xa0"));
	CHECK_MEM(t.p, t.n, "x", 1);
}

/* proc-macro2 1.0.106 fallback literals; float Display as Rust prints `{}`. */
static void test_proc_macro2(void)
{
	CHECK_STR(pm2_f64_suffixed(1.0), "1f64");
	CHECK_STR(pm2_f64_suffixed(0.1), "0.1f64");
	CHECK_STR(pm2_f64_suffixed(1e20), "100000000000000000000f64");
	CHECK_STR(pm2_f64_suffixed(1.5e-7), "0.00000015f64");
	CHECK_STR(pm2_f64_suffixed(-0.0), "-0f64");
	CHECK_STR(pm2_f64_suffixed(-2.5), "-2.5f64");
	CHECK_STR(pm2_f32_suffixed(0.1f), "0.1f32");
	CHECK_STR(pm2_f32_suffixed(16777216.0f), "16777216f32");
	CHECK_STR(pm2_f32_suffixed(3.4028235e38f), "340282350000000000000000000000000000000f32");
	CHECK_STR(pm2_int(-3, "i32"), "-3i32");
	CHECK_STR(pm2_int(INT64_MIN, "i64"), "-9223372036854775808i64");
	CHECK_STR(pm2_uint(UINT64_MAX, "u64"), "18446744073709551615u64");
	CHECK_STR(pm2_string(STR_LIT("a'b\"c\\\t\n")), "\"a'b\\\"c\\\\\\t\\n\"");
	CHECK_STR(pm2_string(STR_LIT("\0" "1\0x\x7f\xc2\xa0\xe2\x80\x94")), "\"\\x001\\0x\\u{7f}\\u{a0}\xe2\x80\x94\"");
	CHECK_STR(pm2_byte_string(STR_LIT("\0" "7\0a\xff\"")), "b\"\\x007\\0a\\xFF\\\"\"");
	CHECK_STR(pm2_character('"'), "'\"'");
	CHECK_STR(pm2_character('\''), "'\\''");
}

TESTS(TEST(test_heck_snake), TEST(test_heck_camel), TEST(test_heck_other), TEST(test_prost_ident),
      TEST(test_prost_sanitize_line), TEST(test_prost_comments), TEST(test_prost_params),
      TEST(test_prost_module), TEST(test_prost_decode), TEST(test_rust_std), TEST(test_proc_macro2))
