/* Unit tests of the tonic-build 0.14.2 port: the upstream tests of src/lib.rs (match_name,
 * naive_snake_case), and the syn parsing of option text (attributes, types) checked with the
 * prettyplease 0.2.37 output of the same source. */
#include "syn.h"
#include "test.h"
#include "tonic_build.h"

static void test_match_name(void)
{
	CHECK(tonic_match_name(".", ".my.protos"));
	CHECK(tonic_match_name(".", ".protos"));
	CHECK(tonic_match_name(".my", ".my"));
	CHECK(tonic_match_name(".my", ".my.protos"));
	CHECK(tonic_match_name(".my.protos.Service", ".my.protos.Service"));
	CHECK(tonic_match_name("Service", ".my.protos.Service"));
	CHECK(!tonic_match_name(".m", ".my.protos"));
	CHECK(!tonic_match_name(".p", ".protos"));
	CHECK(!tonic_match_name(".my", ".myy"));
	CHECK(!tonic_match_name(".protos", ".my.protos"));
	CHECK(!tonic_match_name(".Service", ".my.protos.Service"));
	CHECK(!tonic_match_name("service", ".my.protos.Service"));
	/* tonic-build matches names without a leading dot */
	CHECK(!tonic_match_name(".my", "my.protos"));
	CHECK(tonic_match_name("protos", "my.protos"));
	CHECK(!tonic_match_name("", ""));
	CHECK(tonic_match_name(".", ""));
}

static void run_match(void *pattern) { tonic_match_name(pattern, ".a.b"); }

/* &pattern[..1] on a non-ASCII first char: the panic text of Rust 1.91.1 (slice_error_fail_rt) */
static void test_match_name_panic(void)
{
	CHECK_STR(try_run(run_match, "\xc3\xa9" "a.b"),
		  "byte index 1 is not a char boundary; it is inside '\xc3\xa9' (bytes 0..2) of `\xc3\xa9" "a.b`");
	CHECK_STR(try_run(run_match, "\xe2\x80\x8bx"),
		  "byte index 1 is not a char boundary; it is inside '\\u{200b}' (bytes 0..3) of `\xe2\x80\x8bx`");
	Buf b = {0}, want = {0};
	buf_adds(&b, "\xc3\xb1");
	for (int i = 0; i < 300; i++)
		buf_addc(&b, 'a');
	buf_adds(&want, "byte index 1 is not a char boundary; it is inside '\xc3\xb1' (bytes 0..2) of `\xc3\xb1");
	for (int i = 0; i < 254; i++)
		buf_addc(&want, 'a');
	buf_adds(&want, "`[...]");
	CHECK_STR(try_run(run_match, (void *)buf_str(&b)), buf_str(&want));
}

static void test_snake_case(void)
{
	CHECK_STR(tonic_naive_snake_case("Service"), "service");
	CHECK_STR(tonic_naive_snake_case("ThatHasALongName"), "that_has_a_long_name");
	CHECK_STR(tonic_naive_snake_case("greeter"), "greeter");
	CHECK_STR(tonic_naive_snake_case("ABCServiceX"), "a_b_c_service_x");
}

/* The unit struct `struct S;` with the attributes of attr, as prettyplease prints it. */
static char *with_attrs(const char *attr)
{
	Vec attrs = {0};
	syn_parse_attrs_of_fake_struct(str_c(attr), &attrs);
	PpNode *st = pp_item_struct(0, "S", 0);
	for (int i = 0; i < attrs.n; i++)
		pp_push_attr(st, attrs.v[i]);
	return pp_unparse_item(st);
}

static void test_attributes(void)
{
	CHECK_STR(with_attrs("/// client module"), "/// client module\nstruct S;\n");
	CHECK_STR(with_attrs("#[cfg(feature = \"client\")]"), "#[cfg(feature = \"client\")]\nstruct S;\n");
	CHECK_STR(with_attrs("#[derive(Debug)] #[must_use]"), "#[derive(Debug)]\n#[must_use]\nstruct S;\n");
	CHECK_STR(with_attrs("#[doc = \"a\\tb\"]"), "///a\tb\nstruct S;\n");
	CHECK_STR(with_attrs("#[serde(rename_all = \"camelCase\", default)]"),
		  "#[serde(rename_all = \"camelCase\", default)]\nstruct S;\n");
	CHECK_STR(with_attrs("#[a::b = c::d] // x"), "#[a::b = c::d]\nstruct S;\n");
	CHECK_STR(with_attrs(""), "struct S;\n");
}

static void run_attr(void *src) { with_attrs(src); }

static void test_attribute_errors(void)
{
	CHECK_STR(try_run(run_attr, "#[a"), "called `Result::unwrap()` on an `Err` value: Error(\"lex error\")");
	CHECK(try_run(run_attr, "#![a]") != NULL);
	CHECK(try_run(run_attr, "#[a] pub") != NULL);
	CHECK(try_run(run_attr, "#[a = 1 + 2]") != NULL);
	CHECK(try_run(run_attr, "#[type]") != NULL);
	CHECK(try_run(run_attr, "#[\xc3\xa9]") != NULL);
	/* 255 nested groups print; 256 are unsupported (upstream: no limit) */
	for (int n = 255; n <= 256; n++) {
		Buf b = {0};
		buf_adds(&b, "#[a");
		for (int i = 0; i < n; i++)
			buf_addc(&b, '(');
		for (int i = 0; i < n; i++)
			buf_addc(&b, ')');
		buf_adds(&b, "]");
		const char *err = try_run(run_attr, (void *)buf_str(&b));
		if (n == 255)
			CHECK(err == NULL);
		else
			CHECK_STR(err, "unsupported by protoc-gen-uni: attribute tokens nested more than 255 levels deep");
	}
}

static void indent(Buf *b, int n)
{
	for (int i = 0; i < n; i++)
		buf_addc(b, ' ');
}

/* prettyplease 0.2.37 output of `#[a((...(x)...))] struct S;` with 60 nested groups: 30 levels
 * break, the inner 29 fit on one line. */
static void test_nested_attribute(void)
{
	Buf src = {0}, want = {0};
	buf_adds(&src, "#[a");
	for (int i = 0; i < 60; i++)
		buf_addc(&src, '(');
	buf_addc(&src, 'x');
	for (int i = 0; i < 60; i++)
		buf_addc(&src, ')');
	buf_adds(&src, "]");
	buf_adds(&want, "#[a(\n");
	for (int i = 1; i <= 30; i++)
		indent(&want, 4 * i), buf_adds(&want, "(\n");
	indent(&want, 4 * 31);
	for (int i = 0; i < 29; i++)
		buf_addc(&want, '(');
	buf_addc(&want, 'x');
	for (int i = 0; i < 29; i++)
		buf_addc(&want, ')');
	buf_addc(&want, '\n');
	for (int i = 30; i >= 1; i--)
		indent(&want, 4 * i), buf_adds(&want, ")\n");
	buf_adds(&want, ")]\nstruct S;\n");
	CHECK_STR(with_attrs(buf_str(&src)), buf_str(&want));
}

static char *type_text(const char *src, bool path_only)
{
	PpNode *t = pp_item_type(0, "T", syn_parse_type(str_c(src), path_only));
	return pp_unparse_item(t);
}

static void test_types(void)
{
	CHECK_STR(type_text("::prost::alloc::vec::Vec<u8>", false), "type T = ::prost::alloc::vec::Vec<u8>;\n");
	CHECK_STR(type_text("()", false), "type T = ();\n");
	CHECK_STR(type_text("super::super::r#type::Foo", true), "type T = super::super::r#type::Foo;\n");
	CHECK_STR(type_text("&'static [u8]", false), "type T = &'static [u8];\n");
	CHECK_STR(type_text("(i32, Vec<Option<u8>>,)", false), "type T = (i32, Vec<Option<u8>>);\n");
	CHECK_STR(type_text("[u8; 4]", false), "type T = [u8; 4];\n");
}

static void run_type(void *src) { syn_parse_type(str_c(src), false); }
static void run_path(void *src) { syn_parse_type(str_c(src), true); }

static void test_type_errors(void)
{
	CHECK(try_run(run_type, "Vec<u8") != NULL);
	CHECK_STR(try_run(run_type, "\"a"), "called `Result::unwrap()` on an `Err` value: LexError { span: Span }");
	CHECK(try_run(run_type, "dyn Foo") != NULL);
	CHECK(try_run(run_type, "a + b") != NULL);
	CHECK(try_run(run_path, "()") != NULL);
	CHECK(try_run(run_path, "super::1abc") != NULL);
	CHECK(try_run(run_path, "super::") != NULL);
}

TESTS(TEST(test_match_name), TEST(test_match_name_panic), TEST(test_snake_case), TEST(test_attributes),
      TEST(test_attribute_errors), TEST(test_nested_attribute), TEST(test_types), TEST(test_type_errors))
