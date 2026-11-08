/* pkg/languages/swift/swift-protobuf: expected values from the tests of swift-protobuf 1.39.0
 * (Tests/SwiftProtobufPluginLibraryTests: Test_NamingUtils.swift, Test_SwiftLanguage.swift) and
 * from the behavior of CodePrinter, CodeGeneratorParameter and SourceCodeInfo comments. */
#include "swift_protobuf.h"
#include "test.h"

extern const PbMsgDef pbd_FileOptions, pbd_Location;

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static void test_type_prefix(void)
{
	static const struct {
		const char *package, *swift_prefix, *want;
	} cases[] = {
		{"", NULL, ""}, {"", "", ""}, {"foo", NULL, "Foo_"}, {"FOO", NULL, "FOO_"},
		{"fooBar", NULL, "FooBar_"}, {"FooBar", NULL, "FooBar_"}, {"foo.bar.baz", NULL, "Foo_Bar_Baz_"},
		{"foo_bar_baz", NULL, "FooBarBaz_"}, {"foo.bar_baz", NULL, "Foo_BarBaz_"},
		{"foo_bar__baz", NULL, "FooBarBaz_"}, {"foo.bar_baz_", NULL, "Foo_BarBaz_"},
		{"foo._bar_baz", NULL, "Foo_BarBaz_"}, {"foo.BAR_baz", NULL, "Foo_BARBaz_"},
		{"foo.bar_bAZ", NULL, "Foo_BarBAZ_"}, {"FOO.BAR_BAZ", NULL, "FOO_BARBAZ_"}, {"_foo", NULL, "Foo_"},
		{"__foo", NULL, "Foo_"}, {"_1foo", NULL, "_1foo_"}, {"__1foo", NULL, "_1foo_"},
		{"_1foo.2bar._3baz", NULL, "_1foo_2bar_3baz_"}, {"_1foo_2bar_3baz", NULL, "_1foo2bar3baz_"},
		{"foo.bar.baz", "", ""}, {"", "ABC", "ABC"}, {"foo.bar.baz", "ABC", "ABC"},
		{"foo.bar.baz", "abc", "abc"}, {"foo.bar.baz", "aBc", "aBc"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		PbMsg *options = pb_new(&pbd_FileOptions);
		if (cases[i].swift_prefix)
			pb_set_str(options, 39, cases[i].swift_prefix);
		CHECK_STR(sw_type_prefix(cases[i].package, options), cases[i].want);
	}
}

static void test_prefix_stripper(void)
{
	static const struct {
		const char *prefix, *s, *want;
	} cases[] = {
		{"", "", NULL}, {"FOO", "FOO", NULL}, {"fOo", "FOO", NULL}, {"foo_", "FOO", NULL},
		{"_foo", "FOO", NULL}, {"_foo_", "FOO", NULL}, {"foo", "FOO_", NULL}, {"foo", "_FOO", NULL},
		{"foo", "_FOO_", NULL}, {"foo_", "FOObar", "bar"}, {"_foo", "FOObar", "bar"},
		{"_foo_", "FOObar", "bar"}, {"foo", "FOO_bar", "bar"}, {"foo", "_FOObar", "bar"},
		{"foo", "_FOO_bar", "bar"}, {"FOO_bar", "foo_BAR_baz", "baz"}, {"FooBar", "foo_bar_Baz", "Baz"},
		{"foo_bar", "foobar_bAZ", "bAZ"}, {"_foo_bar", "foobar_bAZ", "bAZ"},
		{"foo__bar_", "_foo_bar__baz", "baz"}, {"FooBar", "foo_bar_1", NULL},
		{"FooBar", "foo_bar_1foo", NULL}, {"FooBar", "foo_bar_foo1", "foo1"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		char *got = sw_strip_prefix(cases[i].prefix, cases[i].s);
		if (cases[i].want)
			CHECK(got && str_eq(got, cases[i].want));
		else
			CHECK(!got);
	}
}

static void test_sanitize_type_names(void)
{
	static const struct {
		const char *in, *message, *enum_, *oneof;
	} cases[] = {
		{"", "", "", ""},
		{"Foo", "Foo", "Foo", "Foo"},
		{"FooBar", "FooBar", "FooBar", "FooBar"},
		{"foo_bar", "foo_bar", "foo_bar", "foo_bar"},
		{"RenamedSwiftProtobuf", "RenamedSwiftProtobufMessage", "RenamedSwiftProtobufEnum", "RenamedSwiftProtobufOneof"},
		{"isInitialized", "isInitializedMessage", "isInitializedEnum", "isInitializedOneof"},
		{"associatedtype", "associatedtypeMessage", "associatedtypeEnum", "associatedtypeOneof"},
		{"class", "classMessage", "classEnum", "classOneof"},
		{"break", "breakMessage", "breakEnum", "breakOneof"},
		{"do", "doMessage", "doEnum", "doOneof"},
		{"_", "_Message", "_Enum", "_Oneof"},
		{"___", "___Message", "___Enum", "___Oneof"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		CHECK_STR(sw_sanitize_message_name(cases[i].in, "RenamedSwiftProtobuf"), cases[i].message);
		CHECK_STR(sw_sanitize_enum_name(cases[i].in, "RenamedSwiftProtobuf"), cases[i].enum_);
		CHECK_STR(sw_sanitize_oneof_name(cases[i].in, "RenamedSwiftProtobuf"), cases[i].oneof);
	}
	CHECK_STR(sw_sanitize_message_name("SwiftProtobuf", "RenamedSwiftProtobuf"), "SwiftProtobufMessage");
	CHECK_STR(sw_sanitize_enum_name("SwiftProtobuf", "RenamedSwiftProtobuf"), "SwiftProtobufEnum");
	CHECK_STR(sw_sanitize_message_name("classMessage", "X"), "classMessageMessage");
	CHECK_STR(sw_sanitize_message_name("classMessageMessage", "X"), "classMessageMessageMessage");
	CHECK_STR(sw_sanitize_enum_name("classEnumEnum", "X"), "classEnumEnumEnum");
	CHECK_STR(sw_sanitize_oneof_name("classOneofOneof", "X"), "classOneofOneofOneof");
}

static void test_sanitize_field_name(void)
{
	static const struct {
		const char *in, *want;
	} cases[] = {
		{"", ""}, {"Foo", "Foo"}, {"FooBar", "FooBar"}, {"foo_bar", "foo_bar"},
		{"debugDescription", "debugDescription_p"}, {"isInitialized", "isInitialized_p"},
		{"associatedtype", "`associatedtype`"}, {"class", "`class`"}, {"break", "`break`"}, {"do", "`do`"},
		{"hasFoo", "hasFoo_p"}, {"clearFoo", "clearFoo_p"}, {"hashtag", "hashtag"},
		{"clearable", "clearable"}, {"has911", "has911"}, {"has", "has"}, {"clear", "clear"},
		{"_", "___"}, {"___", "_____"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		CHECK_STR(sw_sanitize_field_name(cases[i].in, cases[i].in), cases[i].want);
		char *prefixed = xcat("XX", sw_uppercase_first(cases[i].in));
		char *want2 = xcat("XX", sw_uppercase_first(sw_trim_backticks(cases[i].want)));
		CHECK_STR(sw_sanitize_field_name(prefixed, cases[i].in), want2);
	}
}

static void test_sanitize_enum_case_name(void)
{
	static const struct {
		const char *in, *want;
	} cases[] = {
		{"", ""}, {"Foo", "Foo"}, {"FooBar", "FooBar"}, {"foo_bar", "foo_bar"},
		{"debugDescription", "debugDescription_"}, {"dynamicType", "dynamicType_"},
		{"associatedtype", "`associatedtype`"}, {"class", "`class`"}, {"break", "`break`"}, {"do", "`do`"},
		{"_", "___"}, {"___", "_____"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		CHECK_STR(sw_sanitize_enum_case_name(cases[i].in), cases[i].want);
		CHECK_STR(sw_sanitize_message_scoped_extension_name(cases[i].in), cases[i].want);
	}
}

static void test_camel_case(void)
{
	static const struct {
		const char *in, *lower, *upper;
	} cases[] = {
		{"", "", ""}, {"foo", "foo", "Foo"}, {"FOO", "foo", "Foo"}, {"foO", "foO", "FoO"},
		{"foo_bar", "fooBar", "FooBar"}, {"foo_bAr_BaZ", "fooBArBaZ", "FooBArBaZ"},
		{"foo1bar", "foo1Bar", "Foo1Bar"}, {"foo2bAr3BaZ", "foo2BAr3BaZ", "Foo2BAr3BaZ"},
		{"foo_1bar", "foo1Bar", "Foo1Bar"}, {"foo_2bAr_3BaZ", "foo2BAr3BaZ", "Foo2BAr3BaZ"},
		{"_0foo_1bar", "_0Foo1Bar", "_0Foo1Bar"}, {"_0foo_2bAr_3BaZ", "_0Foo2BAr3BaZ", "_0Foo2BAr3BaZ"},
		{"url", "url", "URL"}, {"http", "http", "HTTP"}, {"https", "https", "HTTPS"}, {"id", "id", "ID"},
		{"the_url", "theURL", "TheURL"}, {"use_http", "useHTTP", "UseHTTP"},
		{"use_https", "useHTTPS", "UseHTTPS"}, {"request_id", "requestID", "RequestID"},
		{"url_number", "urlNumber", "URLNumber"}, {"http_needed", "httpNeeded", "HTTPNeeded"},
		{"https_needed", "httpsNeeded", "HTTPSNeeded"}, {"id_number", "idNumber", "IDNumber"},
		{"is_url_number", "isURLNumber", "IsURLNumber"}, {"is_http_needed", "isHTTPNeeded", "IsHTTPNeeded"},
		{"is_https_needed", "isHTTPSNeeded", "IsHTTPSNeeded"}, {"the_id_number", "theIDNumber", "TheIDNumber"},
		{"url_foo_http_id", "urlFooHTTPID", "URLFooHTTPID"},
		{"g\xc3\xb6\xc3\x9f", "g\xc3\xb6\xc3\x9f", "G\xc3\xb6\xc3\x9f"},
		{"g\xc3\xb6o", "g\xc3\xb6O", "G\xc3\xb6O"},
		{"g\xc3\xb6_o", "g\xc3\xb6O", "G\xc3\xb6O"},
		{"g_\xf0\x9f\x8e\x89_o", "g\xf0\x9f\x8e\x89O", "G\xf0\x9f\x8e\x89O"},
		{"g\xf0\x9f\x8e\x89o", "g\xf0\x9f\x8e\x89O", "G\xf0\x9f\x8e\x89O"},
		{"m\xc2\xabn", "m_u171N", "M_u171N"}, {"m\xc2\xab_n", "m_u171N", "M_u171N"},
		{"m_\xc2\xab_n", "m_u171N", "M_u171N"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		CHECK_STR(sw_to_lower_camel(cases[i].in), cases[i].lower);
		CHECK_STR(sw_to_upper_camel(cases[i].in), cases[i].upper);
	}
	CHECK_STR(sw_to_json_field_name("foo_bar_baz"), "fooBarBaz");
	CHECK_STR(sw_to_json_field_name("_foo__bar_"), "FooBar");
	CHECK_STR(sw_trim_backticks("`do`"), "do");
	CHECK_STR(sw_trim_backticks("do"), "do");
	CHECK_STR(sw_periods_to_underscores("a.b.c"), "a_b_c");
}

static void test_valid_swift_identifier(void)
{
	static const char *const valid[] = {"H9000", "\xf0\x9f\x90\xb6\xf0\x9f\x90\xb1"};
	static const char *const invalid[] = {"_", "$0", "$f00", "12Hour", "This is bad"};
	static const char *const bad_quotes[] = {"`H9000", "H9000`", "``H9000", "H9000``", "``H9000`", "``H9000``"};
	for (size_t i = 0; i < LEN(valid); i++) {
		char *quoted = xcat("`", valid[i], "`");
		CHECK(sw_is_valid_swift_identifier(valid[i], false));
		CHECK(!sw_is_valid_swift_identifier(quoted, false));
		CHECK(sw_is_valid_swift_identifier(valid[i], true));
		CHECK(sw_is_valid_swift_identifier(quoted, true));
	}
	for (size_t i = 0; i < LEN(invalid); i++) {
		char *quoted = xcat("`", invalid[i], "`");
		CHECK(!sw_is_valid_swift_identifier(invalid[i], false));
		CHECK(!sw_is_valid_swift_identifier(quoted, false));
		CHECK(!sw_is_valid_swift_identifier(invalid[i], true));
		CHECK(!sw_is_valid_swift_identifier(quoted, true));
	}
	for (size_t i = 0; i < LEN(bad_quotes); i++)
		CHECK(!sw_is_valid_swift_identifier(bad_quotes[i], true));
	CHECK(!sw_is_valid_swift_identifier("", false));
}

static void test_printer(void)
{
	SwPrinter p;
	sw_printer_init(&p, true);
	sw_print(&p, "a {");
	sw_indent(&p);
	sw_print(&p, "b\n\nc");
	sw_print_blank(&p);
	sw_print_nl(&p, STR_LIT("d = "), false);
	sw_print(&p, "e");
	sw_print_indented(&p, "f");
	sw_outdent(&p);
	sw_print(&p, "}");
	CHECK_STR(p.content.p, "a {\n  b\n\n  c\n\n  d = e\n    f\n}\n");

	SwPrinter q;
	sw_printer_init(&q, true);
	sw_print(&q, "x\ny");
	SwPrinter r;
	sw_printer_init(&r, true);
	sw_indent(&r);
	sw_append(&r, &q, true);
	sw_append(&r, &q, false);
	CHECK_STR(r.content.p, "  x\n  y\nx\ny\n");
	CHECK(sw_printer_is_empty(&(SwPrinter){0}));
}

static void test_parse_parameter(void)
{
	SwParamPair *pairs;
	CHECK(sw_parse_parameter(STR_LIT(""), &pairs) == 0);
	int n = sw_parse_parameter(STR_LIT(" a = b ,c,=d,e=f=g,\xe2\x80\x83h\xc2\x85"), &pairs);
	CHECK(n == 5);
	CHECK(str_eqc(pairs[0].key, "a") && str_eqc(pairs[0].value, "b"));
	CHECK(str_eqc(pairs[1].key, "c") && str_eqc(pairs[1].value, ""));
	CHECK(str_eqc(pairs[2].key, "") && str_eqc(pairs[2].value, "d"));
	CHECK(str_eqc(pairs[3].key, "e") && str_eqc(pairs[3].value, "f=g"));
	CHECK(str_eqc(pairs[4].key, "h") && str_eqc(pairs[4].value, ""));
}

static void test_source_comment(void)
{
	PbMsg *loc = pb_new(&pbd_Location);
	pb_set_str(loc, 4, " trailing\n");
	CHECK_STR(sw_as_source_comment(loc, "///", NULL).p, "/// trailing\n");
	pb_set_str(loc, 3, " a\r\n b\xe2\x80\xa8" "c\xc2\x85\rd\n \t\xc2\xa0\n\n");
	CHECK_STR(sw_as_source_comment(loc, "///", "//").p, "/// a\n/// b\n///c\n///\n///d\n");
	pb_set_str(loc, 3, "  \n");
	CHECK_STR(sw_as_source_comment(loc, "///", NULL).p, "");
}

TESTS(TEST(test_type_prefix), TEST(test_prefix_stripper), TEST(test_sanitize_type_names),
      TEST(test_sanitize_field_name), TEST(test_sanitize_enum_case_name), TEST(test_camel_case),
      TEST(test_valid_swift_identifier), TEST(test_printer), TEST(test_parse_parameter),
      TEST(test_source_comment))
