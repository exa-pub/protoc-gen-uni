/* Unit tests of str.splitlines, int() and float() of str, sys.stdlib_module_names and
 * hashlib.sha256 (pkg/languages/python/std). The expected values were printed by CPython. */
#include "python.h"

#include "test.h"

#include <math.h>
#include <string.h>

static void test_splitlines(void)
{
	static const struct { const char *in; const char *want; } cases[] = {
		{"a\nb", "a|b|"},
		{"a\r\nb\rc", "a|b|c|"},
		{"x\x0by\x0cz\x1cw\x1dv\x1eu\xc2\x85t\xe2\x80\xa8s\xe2\x80\xa9r", "x|y|z|w|v|u|t|s|r|"},
		{"end\n", "end|"},
		{"\n\n", "||"},
		{"", ""},
		{"one", "one|"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		Str *lines;
		int n = py_splitlines(str_c(cases[i].in), &lines);
		Buf b = {0};
		for (int k = 0; k < n; k++) {
			buf_add_str(&b, lines[k]);
			buf_addc(&b, '|');
		}
		CHECK_STR(buf_str(&b), cases[i].want);
	}
}

static void test_parse_int(void)
{
	static const struct { const char *in, *want; } cases[] = {
		{"0", "0"}, {" 12 ", "12"}, {"+7", "7"}, {"-0", "0"}, {"007", "7"}, {"1_000", "1000"},
		{"-12", "-12"}, {"18446744073709551616", "18446744073709551616"}, {"\xc2\xa0" "12\xe3\x80\x80", "12"},
		{"1__0", NULL}, {"_1", NULL}, {"1_", NULL}, {"", NULL}, {"- 1", NULL}, {"0x10", NULL},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		char *repr = NULL;
		PyNum st = py_parse_int(str_c(cases[i].in), &repr);
		if (cases[i].want) {
			CHECK(st == PY_NUM_OK);
			if (st == PY_NUM_OK)
				CHECK_STR(repr, cases[i].want);
		} else {
			CHECK(st == PY_NUM_ERROR);
		}
	}
	char *repr;
	CHECK(py_parse_int(str_c("\xd9\xa1"), &repr) == PY_NUM_UNSUPPORTED); /* ARABIC-INDIC DIGIT ONE */
}

static void test_parse_float(void)
{
	static const struct { const char *in, *want; } cases[] = {
		{"1", "1.0"}, {"1.5", "1.5"}, {"-0", "-0.0"}, {"-0.0", "-0.0"}, {"1e10", "10000000000.0"},
		{"1E-5", "1e-05"}, {"inf", "inf"}, {"-Infinity", "-inf"}, {"nan", "nan"}, {"-nan", "nan"},
		{".5", "0.5"}, {"5.", "5.0"}, {"1_0.5", "10.5"}, {"1e1_0", "10000000000.0"}, {" 3.25 ", "3.25"},
		{"1e400", "inf"},
		{"1._5", NULL}, {"1e", NULL}, {".", NULL}, {"infx", NULL}, {"0x1p3", NULL}, {"1_e5", NULL},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		double d = 0;
		PyNum st = py_parse_float(str_c(cases[i].in), &d);
		if (cases[i].want) {
			CHECK(st == PY_NUM_OK);
			if (st == PY_NUM_OK)
				CHECK_STR(py_repr_float(d), cases[i].want);
		} else {
			CHECK(st == PY_NUM_ERROR);
		}
	}
}

static void test_stdlib_names(void)
{
	CHECK(py_is_stdlib_module(str_c("typing")));
	CHECK(py_is_stdlib_module(str_c("enum")));
	CHECK(py_is_stdlib_module(str_c("__future__")));
	CHECK(py_is_stdlib_module(str_c("_pyrepl"))); /* new in 3.13 */
	CHECK(py_is_stdlib_module(str_c("zoneinfo")));
	CHECK(!py_is_stdlib_module(str_c("typing_extensions")));
	CHECK(!py_is_stdlib_module(str_c("protobuf")));
	CHECK(!py_is_stdlib_module(str_c("cgi")));       /* removed in 3.13 */
	CHECK(!py_is_stdlib_module(str_c("telnetlib"))); /* removed in 3.13 */
	CHECK(!py_is_stdlib_module(str_c("")));
}

static void test_sha256(void)
{
	char a55[56], a56[57], a64[65];
	memset(a55, 'a', 55), a55[55] = 0;
	memset(a56, 'a', 56), a56[56] = 0;
	memset(a64, 'a', 64), a64[64] = 0;
	CHECK_STR(py_sha256_hex(str_c("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK_STR(py_sha256_hex(str_c("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_STR(py_sha256_hex(str_c("filename-with-dash")), "b300812f1eee44ff07e9b87c25d41f56512d745face49df18f0c152afc6c938d");
	CHECK_STR(py_sha256_hex(str_c(a55)), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
	CHECK_STR(py_sha256_hex(str_c(a56)), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
	CHECK_STR(py_sha256_hex(str_c(a64)), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
	CHECK_STR(py_sha256_hex(str_c("\xc3\xa9")), "4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c");
}

TESTS(TEST(test_splitlines), TEST(test_parse_int), TEST(test_parse_float), TEST(test_stdlib_names), TEST(test_sha256))
