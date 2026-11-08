/* pkg/core: quoting (against Go's strconv.Quote), UTF-8, strings, base64, formatter, map, arena. */
#include "test.h"
#include "util.h"

#include "go_cases.h"

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static void test_quote_go(void)
{
	for (size_t i = 0; i < LEN(quote_cases); i++)
		CHECK_STR(quote_go((Str){quote_cases[i].in, quote_cases[i].n}), quote_cases[i].want);
}

static void test_quote_json(void)
{
	/* JSON.stringify */
	CHECK_STR(quote_json(STR_LIT("")), "\"\"");
	CHECK_STR(quote_json(STR_LIT("a\"b\\c/")), "\"a\\\"b\\\\c/\"");
	CHECK_STR(quote_json(STR_LIT("\b\f\n\r\t\x01\x1f\x7f")), "\"\\b\\f\\n\\r\\t\\u0001\\u001f\x7f\"");
	CHECK_STR(quote_json(STR_LIT("x\0y")), "\"x\\u0000y\"");
	CHECK_STR(quote_json(STR_LIT("é\xe2\x80\xa8<>&")), "\"é\xe2\x80\xa8<>&\"");
}

static void test_utf8_decode(void)
{
	for (size_t i = 0; i < LEN(decode_cases); i++) {
		uint32_t r;
		int w = utf8_decode(decode_cases[i].in, decode_cases[i].n, &r);
		CHECK(w == decode_cases[i].width && r == decode_cases[i].rune);
	}
}

static void test_go_trim_space(void)
{
	for (size_t i = 0; i < LEN(trim_cases); i++) {
		Str t = go_trim_space((Str){trim_cases[i].in, trim_cases[i].n});
		CHECK_MEM(t.p, t.n, trim_cases[i].want, trim_cases[i].want_n);
	}
}

static void test_base64(void)
{
	/* base64.RawStdEncoding */
	CHECK_STR(base64_raw_std((const uint8_t *)"", 0), "");
	CHECK_STR(base64_raw_std((const uint8_t *)"f", 1), "Zg");
	CHECK_STR(base64_raw_std((const uint8_t *)"fo", 2), "Zm8");
	CHECK_STR(base64_raw_std((const uint8_t *)"foo", 3), "Zm9v");
	CHECK_STR(base64_raw_std((const uint8_t *)"foob", 4), "Zm9vYg");
	CHECK_STR(base64_raw_std((const uint8_t *)"\xff\xfe\xfd\x00", 4), "//79AA");
	CHECK_STR(base64_raw_std((const uint8_t *)"\xfb\xff", 2), "+/8");
}

static void test_split(void)
{
	Str *p;
	CHECK(str_split_s(STR_LIT(""), ',', &p) == 1 && p[0].n == 0);
	CHECK(str_split_s(STR_LIT(",a,,b\0c,"), ',', &p) == 5);
	CHECK(p[0].n == 0 && str_eqc(p[1], "a") && p[2].n == 0 && str_equal(p[3], STR_LIT("b\0c")) && p[4].n == 0);
	Vec v = str_split("a/b", '/');
	CHECK(v.n == 2 && str_eq(v.v[0], "a") && str_eq(v.v[1], "b"));
	CHECK_STR(str_join(&v, ", "), "a, b");
	CHECK_STR(str_replace_all("a*/b*/", "*/", "*\\/"), "a*\\/b*\\/");
	CHECK_STR(str_trim_suffix("x.proto", ".proto"), "x");
	CHECK(str_has_suffix(STR_LIT("a\0b"), STR_LIT("\0b")) && !str_has_prefix(STR_LIT("a"), STR_LIT("ab")));
}

static void test_fmt(void)
{
	CHECK_STR(xfmt("%s=%d %d %lld %x %04d %c%%", "a", 0, -12, -9223372036854775807LL - 1, 255u, 7, 'z'),
		  "a=0 -12 -9223372036854775808 ff 0007 z%");
	CHECK_STR(xcat("a", "", "bc"), "abc");
}

static void test_map(void)
{
	Map m = {0};
	char keys[200][8];
	for (int i = 0; i < 200; i++) {
		for (int j = 0; j < 7; j++)
			keys[i][j] = (char)('a' + (i >> j) % 26);
		keys[i][7] = 0;
		map_put(&m, keys[i], (void *)(intptr_t)(i + 1));
	}
	for (int i = 0; i < 200; i++)
		CHECK(map_get(&m, keys[i]) == (void *)(intptr_t)(i + 1));
	CHECK(!map_has(&m, "nope") && map_get(&m, "nope") == NULL);
}

static void fail_fn(void *arg) { fail("boom %d", *(int *)arg); }
static void ok_fn(void *arg) { *(int *)arg = 1; }

static void test_fail(void)
{
	int x = 7;
	CHECK_STR(try_run(fail_fn, &x), "boom 7");
	CHECK(try_run(ok_fn, &x) == NULL && x == 1);
}

static void test_arena(void)
{
	for (int round = 0; round < 3; round++) {
		char *big = xalloc(3 << 20); /* larger than a chunk */
		big[(3 << 20) - 1] = 1;
		Buf b = {0};
		for (int i = 0; i < 100000; i++)
			buf_addc(&b, 'x');
		CHECK(b.n == 100000 && strlen(buf_str(&b)) == 100000);
		arena_reset();
	}
}

/* arena_mark / arena_release / xalloc_keep: memory after the mark is reused, kept memory and memory
 * before the mark stay valid */
static void test_arena_mark(void)
{
	char *before = xstrdup("before");
	for (int round = 0; round < 3; round++) {
		ArenaMark m = arena_mark();
		char *scratch = xalloc(64);
		char *big = xalloc(3 << 20);
		big[0] = 1;
		char *keep = xalloc_keep(16);
		memcpy(keep, "kept", 5);
		memcpy(scratch, "scratch", 8);
		arena_release(m);
		char *again = xalloc(64);
#ifndef UNI_NO_ARENA
		CHECK(again == scratch); /* the bump pointer was restored */
#else
		(void)again;
		(void)scratch;
#endif
		CHECK_STR(keep, "kept");
		CHECK_STR(before, "before");
	}
	arena_reset();
}

TESTS(TEST(test_quote_go), TEST(test_quote_json), TEST(test_utf8_decode), TEST(test_go_trim_space),
      TEST(test_base64), TEST(test_split), TEST(test_fmt), TEST(test_map), TEST(test_fail), TEST(test_arena),
      TEST(test_arena_mark))
