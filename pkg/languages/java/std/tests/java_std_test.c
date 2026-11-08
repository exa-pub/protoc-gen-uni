/* pkg/languages/java/std: Java Double.toString / Float.toString (expected values: JDK 19+), String
 * decoding and compareTo, sorting. */
#include "java_std.h"
#include "test.h"

#include <float.h>
#include <math.h>

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static void test_java_double(void)
{
	static const struct {
		double v;
		const char *want;
	} cases[] = {
		{0.0, "0.0"},
		{1.0, "1.0"},
		{-1.5, "-1.5"},
		{0.1, "0.1"},
		{100.0, "100.0"},
		{1e7, "1.0E7"},
		{9999999.0, "9999999.0"},
		{1234567.0, "1234567.0"},
		{12345678.9, "1.23456789E7"},
		{0.001, "0.001"},
		{0.0001, "1.0E-4"},
		{0.00123, "0.00123"},
		{1e23, "1.0E23"},
		{2e23, "2.0E23"},
		{1e21, "1.0E21"},
		{123456.789, "123456.789"},
		{1.0 / 3, "0.3333333333333333"},
		{2.0 / 3, "0.6666666666666666"},
		{DBL_MAX, "1.7976931348623157E308"},
		{DBL_MIN, "2.2250738585072014E-308"},
		{5e-324, "4.9E-324"},
		{-5e-324, "-4.9E-324"},
		{1e-323, "9.9E-324"},
		{1.5e-5, "1.5E-5"},
		{3.0e10, "3.0E10"},
		{-273.15, "-273.15"},
	};
	for (size_t i = 0; i < LEN(cases); i++)
		CHECK_STR(java_double_to_string(cases[i].v), cases[i].want);
	CHECK_STR(java_double_to_string(-0.0), "-0.0");
	CHECK_STR(java_double_to_string(NAN), "NaN");
	CHECK_STR(java_double_to_string(INFINITY), "Infinity");
	CHECK_STR(java_double_to_string(-INFINITY), "-Infinity");
}

static void test_java_float(void)
{
	static const struct {
		float v;
		const char *want;
	} cases[] = {
		{0.0f, "0.0"},
		{1.0f, "1.0"},
		{0.1f, "0.1"},
		{1.1f, "1.1"},
		{33.3f, "33.3"},
		{3.14159f, "3.14159"},
		{-2.5f, "-2.5"},
		{1e10f, "1.0E10"},
		{16777216.0f, "1.6777216E7"},
		{0.001f, "0.001"},
		{1e-4f, "1.0E-4"},
		{FLT_MAX, "3.4028235E38"},
		{FLT_MIN, "1.1754944E-38"}, /* JDK < 19: 1.17549435E-38 */
		{1.4e-45f, "1.4E-45"},
	};
	for (size_t i = 0; i < LEN(cases); i++)
		CHECK_STR(java_float_to_string(cases[i].v), cases[i].want);
	CHECK_STR(java_float_to_string(-0.0f), "-0.0");
	CHECK_STR(java_float_to_string(NAN), "NaN");
	CHECK_STR(java_float_to_string(-INFINITY), "-Infinity");
}

static void test_java_string(void)
{
	/* valid UTF-8 is unchanged */
	Str s = java_string(STR_LIT("a\xc3\xa9\xf0\x9f\x98\x80"));
	CHECK_MEM(s.p, s.n, "a\xc3\xa9\xf0\x9f\x98\x80", (size_t)7);
	/* a lone continuation byte; an encoded surrogate (one U+FFFD); an overlong encoding (per byte) */
	s = java_string(STR_LIT("x\x80y"));
	CHECK_MEM(s.p, s.n, "x\xef\xbf\xbdy", (size_t)5);
	s = java_string(STR_LIT("\xed\xa0\x80"));
	CHECK_MEM(s.p, s.n, "\xef\xbf\xbd", (size_t)3);
	s = java_string(STR_LIT("\xc0\xaf"));
	CHECK_MEM(s.p, s.n, "\xef\xbf\xbd\xef\xbf\xbd", (size_t)6);
	/* a truncated sequence at the end is one U+FFFD */
	s = java_string(STR_LIT("a\xe2\x82"));
	CHECK_MEM(s.p, s.n, "a\xef\xbf\xbd", (size_t)4);
	CHECK_STR(java_cstr(STR_LIT("abc")), "abc");
}

static int cmp_str(const void *a, const void *b) { return java_strcmp(a, b) ? (java_strcmp(a, b) < 0 ? -1 : 1) : 0; }
static int cmp_first(const void *a, const void *b) { return *(const char *)a - *(const char *)b; }

static void test_java_strcmp_sort(void)
{
	CHECK(java_strcmp("a", "b") < 0);
	CHECK(java_strcmp("b", "a") > 0);
	CHECK(java_strcmp("ab", "ab") == 0);
	CHECK(java_strcmp("a", "ab") < 0);
	/* UTF-16 order: a supplementary character (surrogates D800-DFFF) sorts before U+FFFD */
	CHECK(java_strcmp("\xf0\x9f\x98\x80", "\xef\xbf\xbd") < 0);
	CHECK(java_strcmp("\xe0\xa0\x80", "\xf0\x9f\x98\x80") < 0);
	void *v[] = {"c", "a", "b"};
	java_sort(v, 3, cmp_str);
	CHECK_STR((const char *)v[0], "a");
	CHECK_STR((const char *)v[2], "c");
	/* stable */
	void *w[] = {"b1", "a1", "b2", "a2"};
	java_sort(w, 4, cmp_first);
	CHECK_STR((const char *)w[0], "a1");
	CHECK_STR((const char *)w[1], "a2");
	CHECK_STR((const char *)w[2], "b1");
	CHECK_STR((const char *)w[3], "b2");
}

TESTS(TEST(test_java_double), TEST(test_java_float), TEST(test_java_string), TEST(test_java_strcmp_sort))
