/* Unit tests of the strconv (quote.go) and unicode ports: tables of go1.26.5
 * src/strconv/quote_test.go (converted mechanically; strings as C literals) and
 * src/unicode/{letter,digit}_test.go. Copyright 2009 The Go Authors. All rights reserved.
 * BSD-3-Clause (see LICENSE in Go). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "internal.h"

#include "strings/strings.c"
#include "unicode/utf8/utf8.c"
#include "unicode/tables.c"
#include "unicode/unicode.c"
#include "strconv/quote.c"

#include "test.h"

#define CHECK_GOSTR(got, want) CHECK_MEM((got).p, (size_t)(got).n, (want), strlen(want))

/* ---- unicode ---- */

static const rune upperTest[] = {0x41,   0xc0,   0xd8,   0x100,  0x139,  0x14a,   0x178,   0x181,
				 0x376,  0x3cf,  0x13bd, 0x1f2a, 0x2102, 0x2c00,  0x2c10,  0x2c20,
				 0xa650, 0xa722, 0xff3a, 0x10400, 0x1d400, 0x1d7ca};
static const rune notupperTest[] = {0x40, 0x5b, 0x61, 0x185, 0x1b0, 0x377, 0x387, 0x2150, 0xab7d, 0xffff, 0x10000};
static const rune letterTest[] = {0x41,   0x61,   0xaa,   0xba,   0xc8,    0xdb,    0xf9,    0x2ec,
				  0x535,  0x620,  0x6e6,  0x93d,  0xa15,   0xb99,   0xdc0,   0xedd,
				  0x1000, 0x1200, 0x1312, 0x1401, 0x2c00,  0xa800,  0xf900,  0xfa30,
				  0xffda, 0xffdc, 0x10000, 0x10300, 0x10400, 0x20000, 0x2f800, 0x2fa1d};
static const rune notletterTest[] = {0x20, 0x35, 0x375, 0x619, 0x700, 0x1885, 0xfffe, 0x1ffff, 0x10ffff};
static const rune spaceTest[] = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x20, 0x85, 0xA0, 0x2000, 0x3000};
static const rune testDigit[] = {
	0x0030, 0x0039, 0x0661, 0x06F1, 0x07C9, 0x0966, 0x09EF, 0x0A66, 0x0AEF, 0x0B66, 0x0B6F, 0x0BE6,
	0x0BEF, 0x0C66, 0x0CEF, 0x0D66, 0x0D6F, 0x0E50, 0x0E59, 0x0ED0, 0x0ED9, 0x0F20, 0x0F29, 0x1040,
	0x1049, 0x1090, 0x1091, 0x1099, 0x17E0, 0x17E9, 0x1810, 0x1819, 0x1946, 0x194F, 0x19D0, 0x19D9,
	0x1B50, 0x1B59, 0x1BB0, 0x1BB9, 0x1C40, 0x1C49, 0x1C50, 0x1C59, 0xA620, 0xA629, 0xA8D0, 0xA8D9,
	0xA900, 0xA909, 0xAA50, 0xAA59, 0xFF10, 0xFF19, 0x104A1, 0x1D7CE,
};

#define LEN(a) (sizeof(a) / sizeof *(a))

static void test_unicode(void)
{
	for (size_t i = 0; i < LEN(upperTest); i++)
		CHECK(unicode_IsUpper(upperTest[i]));
	for (size_t i = 0; i < LEN(notupperTest); i++)
		CHECK(!unicode_IsUpper(notupperTest[i]));
	for (size_t i = 0; i < LEN(letterTest); i++)
		CHECK(unicode_IsLetter(letterTest[i]));
	for (size_t i = 0; i < LEN(notletterTest); i++)
		CHECK(!unicode_IsLetter(notletterTest[i]));
	for (size_t i = 0; i < LEN(spaceTest); i++)
		CHECK(unicode_IsSpace(spaceTest[i]));
	for (size_t i = 0; i < LEN(testDigit); i++)
		CHECK(unicode_IsDigit(testDigit[i]));
	CHECK(unicode_IsLower('a') && !unicode_IsLower('A') && unicode_IsLower(0x3b1) && !unicode_IsLower(0x391));
	CHECK(unicode_IsPunct('!') && unicode_IsPunct(0x2026) && !unicode_IsPunct('a'));
	CHECK(!unicode_IsSpace(0x200b) && !unicode_IsDigit(0x00b2) && !unicode_IsDigit('a'));
	/* negative runes (TestNegativeRune) */
	CHECK(!unicode_IsLetter(-1) && !unicode_IsUpper(-1) && !unicode_IsDigit(-1) && !unicode_IsSpace(-1));
	CHECK(!unicode_IsPrint(-1) && !unicode_IsGraphic(-1) && !unicode_IsPunct(-0x10000));
	/* LatinOffset (TestLatinOffset) for the tables of tables.c */
	static const unicode_RangeTable *const tabs[] = {
		unicode_L, unicode_Ll, unicode_Lu, unicode_M, unicode_N, unicode_Nd, unicode_P, unicode_S, unicode_Zs,
		unicode_White_Space,
	};
	for (size_t t = 0; t < LEN(tabs); t++) {
		int i = 0;
		while (i < tabs[t]->nR16 && tabs[t]->R16[i].Hi <= unicode_MaxLatin1)
			i++;
		CHECK(tabs[t]->LatinOffset == i);
	}
}

/* TestIsPrint, TestIsGraphic: strconv agrees with unicode for every rune. */
static void test_isprint(void)
{
	int bad = 0;
	for (rune r = 0; r <= unicode_MaxRune && bad < 10; r++) {
		if (strconv_IsPrint(r) != unicode_IsPrint(r) || strconv_IsGraphic(r) != unicode_IsGraphic(r)) {
			fprintf(stderr, "IsPrint/IsGraphic(U+%04X) incorrect\n", (unsigned)r);
			bad++;
		}
	}
	CHECK(bad == 0);
}

/* ---- quote ---- */

static const struct {
	const char *in, *out, *ascii, *graphic;
} quotetests[] = {
	{"\007\010\f\r\n\t\v", "\"\\a\\b\\f\\r\\n\\t\\v\"", "\"\\a\\b\\f\\r\\n\\t\\v\"", "\"\\a\\b\\f\\r\\n\\t\\v\""},
	{"\\", "\"\\\\\"", "\"\\\\\"", "\"\\\\\""},
	{"abc\377def", "\"abc\\xffdef\"", "\"abc\\xffdef\"", "\"abc\\xffdef\""},
	{"\342\230\272", "\"\342\230\272\"", "\"\\u263a\"", "\"\342\230\272\""},
	{"\364\217\277\277", "\"\\U0010ffff\"", "\"\\U0010ffff\"", "\"\\U0010ffff\""},
	{"\004", "\"\\x04\"", "\"\\x04\"", "\"\\x04\""},
	{"!\302\240!\342\200\200!\343\200\200!",
	 "\"!\\u00a0!\\u2000!\\u3000!\"",
	 "\"!\\u00a0!\\u2000!\\u3000!\"",
	 "\"!\302\240!\342\200\200!\343\200\200!\""},
	{"\177", "\"\\x7f\"", "\"\\x7f\"", "\"\\x7f\""},
};

static void test_quote(void)
{
	for (size_t i = 0; i < LEN(quotetests); i++) {
		GoStr in = gostr_c(quotetests[i].in);
		CHECK_GOSTR(strconv_Quote(in), quotetests[i].out);
		CHECK_GOSTR(strconv_QuoteToASCII(in), quotetests[i].ascii);
		CHECK_GOSTR(strconv_QuoteToGraphic(in), quotetests[i].graphic);
		Buf b = {0};
		buf_adds(&b, "x");
		strconv_AppendQuote(&b, in);
		CHECK(b.n == 1 + strlen(quotetests[i].out) && memcmp(b.p + 1, quotetests[i].out, b.n - 1) == 0);
	}
	/* invalid UTF-8 and embedded NUL; expected values from Go 1.26.5 strconv.Quote */
	CHECK_GOSTR(strconv_Quote(GOSTR("a\0b")), "\"a\\x00b\"");
	CHECK_GOSTR(strconv_Quote(GOSTR("\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80")),
		    "\"\\xc0\\x80\\xed\\xa0\\x80\\xf4\\x90\\x80\\x80\"");
	CHECK_GOSTR(strconv_Quote(GOSTR("\xe2\x82")), "\"\\xe2\\x82\"");
	CHECK_GOSTR(strconv_Quote(GOSTR("\xef\xbf\xbd\xc2\xad\xe2\x80\x8b\xef\xbb\xbf")),
		    "\"\xef\xbf\xbd\\u00ad\\u200b\\ufeff\"");
	CHECK_GOSTR(strconv_Quote(GOSTR("\xf3\xa0\x80\x81\xf0\x9f\x98\x80\xf0\x9f\x84\xaf")),
		    "\"\\U000e0001\xf0\x9f\x98\x80\xf0\x9f\x84\xaf\"");
}

static const struct {
	rune in;
	const char *out, *ascii, *graphic;
} quoterunetests[] = {
	{'a', "'a'", "'a'", "'a'"},
	{'\a', "'\\a'", "'\\a'", "'\\a'"},
	{'\\', "'\\\\'", "'\\\\'", "'\\\\'"},
	{0xFF, "'\303\277'", "'\\u00ff'", "'\303\277'"},
	{0x263a, "'\342\230\272'", "'\\u263a'", "'\342\230\272'"},
	{0xdead, "'\357\277\275'", "'\\ufffd'", "'\357\277\275'"},
	{0xfffd, "'\357\277\275'", "'\\ufffd'", "'\357\277\275'"},
	{0x0010ffff, "'\\U0010ffff'", "'\\U0010ffff'", "'\\U0010ffff'"},
	{0x0010ffff + 1, "'\357\277\275'", "'\\ufffd'", "'\357\277\275'"},
	{0x04, "'\\x04'", "'\\x04'", "'\\x04'"},
	/* Some differences between graphic and printable. */
	{0x00a0, "'\\u00a0'", "'\\u00a0'", "'\302\240'"},
	{0x2000, "'\\u2000'", "'\\u2000'", "'\342\200\200'"},
	{0x3000, "'\\u3000'", "'\\u3000'", "'\343\200\200'"},
};

static void test_quote_rune(void)
{
	for (size_t i = 0; i < LEN(quoterunetests); i++) {
		CHECK_GOSTR(strconv_QuoteRune(quoterunetests[i].in), quoterunetests[i].out);
		CHECK_GOSTR(strconv_QuoteRuneToASCII(quoterunetests[i].in), quoterunetests[i].ascii);
		CHECK_GOSTR(strconv_QuoteRuneToGraphic(quoterunetests[i].in), quoterunetests[i].graphic);
	}
}

static void test_can_backquote(void)
{
	for (int c = 0; c < 32; c++) {
		char s[1] = {(char)c};
		CHECK(strconv_CanBackquote(gostr(s, 1)) == (c == '\t'));
	}
	CHECK(!strconv_CanBackquote(GOSTR("`")));
	CHECK(!strconv_CanBackquote(GOSTR("\x7f")));
	CHECK(strconv_CanBackquote(GOSTR("' !\"#$%&'()*+,-./:;<=>?@[\\]^_{|}~")));
	CHECK(strconv_CanBackquote(GOSTR("0123456789")));
	CHECK(strconv_CanBackquote(GOSTR("\342\230\272")));
	CHECK(!strconv_CanBackquote(GOSTR("\x80")));
	CHECK(!strconv_CanBackquote(GOSTR("a\xe0\xa0z")));
	CHECK(!strconv_CanBackquote(GOSTR("\xef\xbb\xbf" "abc")));
	CHECK(!strconv_CanBackquote(GOSTR("a\xef\xbb\xbfz")));
}

/* ---- unquote ---- */

static const struct {
	const char *in, *out;
} unquotetests[] = {
	{"\"\"", ""},
	{"\"a\"", "a"},
	{"\"abc\"", "abc"},
	{"\"\342\230\272\"", "\342\230\272"},
	{"\"hello world\"", "hello world"},
	{"\"\\xFF\"", "\377"},
	{"\"\\377\"", "\377"},
	{"\"\\u1234\"", "\341\210\264"},
	{"\"\\U00010111\"", "\360\220\204\221"},
	{"\"\\U0001011111\"", "\360\220\204\22111"},
	{"\"\\a\\b\\f\\n\\r\\t\\v\\\\\\\"\"", "\007\010\f\n\r\t\v\\\""},
	{"\"'\"", "'"},
	{"'a'", "a"},
	{"'\342\230\271'", "\342\230\271"},
	{"'\\a'", "\007"},
	{"'\\x10'", "\020"},
	{"'\\377'", "\377"},
	{"'\\u1234'", "\341\210\264"},
	{"'\\U00010111'", "\360\220\204\221"},
	{"'\\t'", "\t"},
	{"' '", " "},
	{"'\\''", "'"},
	{"'\"'", "\""},
	{"``", ""},
	{"`a`", "a"},
	{"`abc`", "abc"},
	{"`\342\230\272`", "\342\230\272"},
	{"`hello world`", "hello world"},
	{"`\\xFF`", "\\xFF"},
	{"`\\377`", "\\377"},
	{"`\\`", "\\"},
	{"`\n`", "\n"},
	{"`\t`", "\t"},
	{"` `", " "},
	{"`a\rb`", "ab"},
};

static const char *const misquoted[] = {
	"",
	"\"",
	"\"a",
	"\"'",
	"b\"",
	"\"\\\"",
	"\"\\9\"",
	"\"\\19\"",
	"\"\\129\"",
	"'\\'",
	"'\\9'",
	"'\\19'",
	"'\\129'",
	"'ab'",
	"\"\\x1!\"",
	"\"\\U12345678\"",
	"\"\\z\"",
	"`",
	"`xxx",
	"``x\r",
	"`\"",
	"\"\\'\"",
	"'\\\"'",
	"\"\n\"",
	"\"\\n\n\"",
	"'\n'",
	"\"\\udead\"",
	"\"\\ud83d\\ude4f\"",
};

/* testUnquote: Unquote, then QuotedPrefix with an arbitrary suffix appended. */
static void testUnquote(const char *in_, const char *want_, bool wantErr)
{
	GoStr in = gostr_c(in_), want = gostr_c(want_ ? want_ : "");
	GoStr got;
	bool ok = strconv_Unquote(in, &got);
	if (ok == wantErr || (!wantErr && !gostr_eq(got, want)) || (wantErr && got.n != 0)) {
		fprintf(stderr, "Unquote(%s) failed\n", in_);
		uni_test_failed = 1;
	}

	/* Adding an arbitrary suffix should not change the result of QuotedPrefix. */
	if (ok)
		want = in;
	GoStr suffix = GOSTR("\n\r\\\"`'"); /* special characters for quoted strings */
	if (in.n > 0) {
		char c[1] = {in.p[0]};
		suffix = strings_ReplaceAll(suffix, gostr(c, 1), GOSTR(""));
	}
	in = gostr_cat(in, suffix);
	ok = strconv_QuotedPrefix(in, &got);
	bool err = !ok;
	if (ok && wantErr) {
		/* original input had trailing junk, reparse with only valid prefix */
		wantErr = !strconv_Unquote(got, NULL);
		want = got;
	}
	if (err != wantErr || (ok && !gostr_eq(got, want))) {
		fprintf(stderr, "QuotedPrefix(%s...) failed\n", in_);
		uni_test_failed = 1;
	}
}

static void test_unquote(void)
{
	for (size_t i = 0; i < LEN(unquotetests); i++)
		testUnquote(unquotetests[i].in, unquotetests[i].out, false);
	for (size_t i = 0; i < LEN(quotetests); i++)
		testUnquote(quotetests[i].out, quotetests[i].in, false);
	for (size_t i = 0; i < LEN(misquoted); i++)
		testUnquote(misquoted[i], "", true);
	/* TestUnquoteInvalidUTF8 (issue 23685: invalid UTF-8 should not go through the fast path) */
	testUnquote("\"foo\"", "foo", false);
	testUnquote("\"foo", NULL, true);
	testUnquote("\"\xc0\"", "\xef\xbf\xbd", false);
	testUnquote("\"a\xc0\"", "a\xef\xbf\xbd", false);
	testUnquote("\"\\t\xc0\"", "\t\xef\xbf\xbd", false);
	/* UnquoteChar */
	rune v;
	bool mb;
	GoStr tail;
	CHECK(strconv_UnquoteChar(GOSTR("\\u263a!"), '"', &v, &mb, &tail) && v == 0x263a && mb && gostr_eqc(tail, "!"));
	CHECK(strconv_UnquoteChar(GOSTR("\\xffz"), '"', &v, &mb, &tail) && v == 0xff && !mb && gostr_eqc(tail, "z"));
	CHECK(!strconv_UnquoteChar(GOSTR("\\'"), '"', &v, &mb, &tail));
	CHECK(strconv_UnquoteChar(GOSTR("'"), 0, &v, &mb, &tail) && v == '\'');
}

TESTS(TEST(test_unicode), TEST(test_isprint), TEST(test_quote), TEST(test_quote_rune), TEST(test_can_backquote),
      TEST(test_unquote))
