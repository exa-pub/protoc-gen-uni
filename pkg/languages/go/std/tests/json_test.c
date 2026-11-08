/* Unit tests of encoding/json (json.c), strconv.ParseFloat (atof.c) and time.Parse (format.c):
 * cases from go1.27.3 src/encoding/json (encode_test.go, indent), src/internal/strconv/atof_test.go and
 * src/time/format_test.go. Copyright 2010 The Go Authors. All rights reserved. BSD-3-Clause (see
 * LICENSE in Go). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "go_std.c"

#include "test.h"

static const char *quote(const char *s, size_t n)
{
	Buf b = {0};
	go_std_json_quote(&b, (Str){s, n});
	return buf_str(&b);
}

static void test_quote(void)
{
	CHECK_STR(quote("abc", 3), "\"abc\"");
	CHECK_STR(quote("<a&b>", 5), "\"\\u003ca\\u0026b\\u003e\"");
	CHECK_STR(quote("\"\\\b\f\n\r\t\x01", 8), "\"\\\"\\\\\\b\\f\\n\\r\\t\\u0001\"");
	CHECK_STR(quote("\xe2\x80\xa8\xe2\x80\xa9", 6), "\"\\u2028\\u2029\"");
	CHECK_STR(quote("a\xffz", 3), "\"a\xef\xbf\xbdz\"");
	CHECK_STR(quote("\xef\xbf\xbd", 3), "\"\xef\xbf\xbd\"");
}

static const char *ffmt(double f)
{
	Buf b = {0};
	go_std_json_float(&b, f, 64);
	return buf_str(&b);
}

static void test_float(void)
{
	CHECK_STR(ffmt(0), "0");
	CHECK_STR(ffmt(1.5), "1.5");
	CHECK_STR(ffmt(1e20), "100000000000000000000");
	CHECK_STR(ffmt(1e21), "1e+21");
	CHECK_STR(ffmt(1e-7), "1e-7");
	CHECK_STR(ffmt(0.000001), "0.000001");
	CHECK_STR(ffmt(-2.5e-9), "-2.5e-9");
}

static const char *raw(const char *s)
{
	Buf b = {0};
	if (!go_std_json_write_raw(&b, str_c(s)))
		return "<invalid>";
	return buf_str(&b);
}

static void test_write_raw(void)
{
	CHECK_STR(raw(" { \"a\" : [1, 2.5e3, true, null] , \"b\":{} } "), "{\"a\":[1,2.5e3,true,null],\"b\":{}}");
	CHECK_STR(raw("\"<x>\\u003c\""), "\"\\u003cx\\u003e\\u003c\"");
	CHECK_STR(raw("[]"), "[]");
	CHECK_STR(raw("{\"a\":1,}"), "<invalid>");
	CHECK_STR(raw("[1 2]"), "<invalid>");
	CHECK_STR(raw("01"), "<invalid>");
	CHECK_STR(raw("1 2"), "<invalid>");
	CHECK_STR(raw("\"a\nb\""), "<invalid>");
	CHECK_STR(raw("fiction"), "<invalid>");
	CHECK_STR(raw(""), "<invalid>");
}

static void test_indent(void)
{
	Buf b = {0};
	go_std_json_indent(&b, STR_LIT("{\"a\":[1,{}],\"b\":[],\"c\":\"x,:{\"}"), "", "  ");
	CHECK_STR(buf_str(&b), "{\n  \"a\": [\n    1,\n    {}\n  ],\n  \"b\": [],\n  \"c\": \"x,:{\"\n}");
}

static void test_unmarshal(void)
{
	const char *err;
	go_std_json_value *v = go_std_json_unmarshal_any(STR_LIT("{\"b\":1,\"a\":[\"x\\u00e9\",null],\"b\":2.5}"), &err);
	CHECK(v && v->kind == GO_JSON_OBJECT && v->n == 2);
	if (v && v->n == 2) {
		CHECK_MEM(v->items[0].key.p, v->items[0].key.n, "a", 1);
		CHECK(v->items[0].value->kind == GO_JSON_ARRAY && v->items[0].value->n == 2);
		CHECK_MEM(v->items[0].value->items[0].value->s.p, v->items[0].value->items[0].value->s.n, "x\xc3\xa9", 3);
		CHECK(v->items[1].value->kind == GO_JSON_NUMBER && v->items[1].value->f == 2.5);
	}
	CHECK(!go_std_json_unmarshal_any(STR_LIT("1e999"), &err));
	CHECK(!go_std_json_unmarshal_any(STR_LIT("[1,"), &err));
}

static void test_parse_float(void)
{
	static const struct {
		const char *in;
		double out;
		int err;
	} tests[] = {
		{"1", 1, 0},
		{"+1", 1, 0},
		{"1e23", 1e23, 0},
		{"1E23", 1e23, 0},
		{"100000000000000000000000", 1e23, 0},
		{"1e-100", 1e-100, 0},
		{"-1", -1, 0},
		{"-0.1", -0.1, 0},
		{".5", 0.5, 0},
		{"1_000", 1000, 0},
		{"1_23.50_0_0e+1_2", 1.235e+14, 0},
		{"1__23.5e+12", 0, GO_STD_ERR_SYNTAX},
		{"_0x12.345p+12", 0, GO_STD_ERR_SYNTAX},
		{"0x1p-2", 0.25, 0},
		{"0x_1p-2", 0.25, 0},
		{"0x1", 0, GO_STD_ERR_SYNTAX},
		{"inf", HUGE_VAL, 0},
		{"-Infinity", -HUGE_VAL, 0},
		{"infinit", 0, GO_STD_ERR_SYNTAX},
		{"1e", 0, GO_STD_ERR_SYNTAX},
		{"1e-", 0, GO_STD_ERR_SYNTAX},
		{".e-1", 0, GO_STD_ERR_SYNTAX},
		{"1e400", HUGE_VAL, GO_STD_ERR_RANGE},
		{"-1e400", -HUGE_VAL, GO_STD_ERR_RANGE},
		{"", 0, GO_STD_ERR_SYNTAX},
		{"1x", 0, GO_STD_ERR_SYNTAX},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		double f;
		int err = go_std_parse_float(tests[i].in, strlen(tests[i].in), 64, &f);
		if (err != tests[i].err || (!err && f != tests[i].out))
			fprintf(stderr, "ParseFloat(%s): %g %d\n", tests[i].in, f, err);
		CHECK(err == tests[i].err);
		CHECK(err == GO_STD_ERR_SYNTAX || f == tests[i].out);
	}
	double nan;
	CHECK(go_std_parse_float("NaN", 3, 64, &nan) == 0 && nan != nan);
}

static void test_time_parse(void)
{
	static const char *const rfc3339ok[] = {
		"2006-01-02T15:04:05Z",	     "2006-01-02T15:04:05+07:00", "2006-01-02T15:04:05.123-07:00",
		"2000-02-29T00:00:00Z",	     "2006-01-02T1:04:05Z",	  "2006-01-02T15:04:05,5Z",
	};
	static const char *const rfc3339bad[] = {
		"2006-01-02",	"2006-01-02T15:04:05",	"2006-01-02T24:00:00Z", "2001-02-29T00:00:00Z",
		"2006-01-02t15:04:05Z", "2006-1-02T15:04:05Z", "2006-01-02T15:04:05Zx", "2006-01-02T15:04:05+0700",
	};
	for (size_t i = 0; i < sizeof rfc3339ok / sizeof *rfc3339ok; i++)
		CHECK(go_std_time_parse("2006-01-02T15:04:05Z07:00", str_c(rfc3339ok[i])));
	for (size_t i = 0; i < sizeof rfc3339bad / sizeof *rfc3339bad; i++)
		CHECK(!go_std_time_parse("2006-01-02T15:04:05Z07:00", str_c(rfc3339bad[i])));
	CHECK(go_std_time_parse("2006-01-02", STR_LIT("2024-02-29")));
	CHECK(!go_std_time_parse("2006-01-02", STR_LIT("2023-02-29")));
	CHECK(!go_std_time_parse("2006-01-02", STR_LIT("2023-02-1")));
	CHECK(!go_std_time_parse("2006-01-02", STR_LIT("2023-13-01")));
}

TESTS(TEST(test_quote), TEST(test_float), TEST(test_write_raw), TEST(test_indent), TEST(test_unmarshal),
      TEST(test_parse_float), TEST(test_time_parse))
