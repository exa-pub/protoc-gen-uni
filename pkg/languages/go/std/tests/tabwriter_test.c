/* Unit tests of the text/tabwriter port: the table of go1.26.5 src/text/tabwriter/tabwriter_test.go
 * (converted mechanically; strings as C literals), each written all at once, byte by byte and in
 * Fibonacci slices, like Go's check(). Copyright 2009 The Go Authors. All rights reserved.
 * BSD-3-Clause (see LICENSE in Go). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "internal.h"

#include "strings/strings.c"
#include "unicode/utf8/utf8.c"
#include "unicode/tables.c"
#include "unicode/unicode.c"
#include "text/tabwriter/tabwriter.c"

#include "test.h"

static void buf_writer(void *self, const char *p, int n) { buf_add(self, p, (size_t)n); }

#define PFX_FilterHTML tabwriter_FilterHTML
#define PFX_StripEscape tabwriter_StripEscape
#define PFX_AlignRight tabwriter_AlignRight
#define PFX_DiscardEmptyColumns tabwriter_DiscardEmptyColumns
#define PFX_TabIndent tabwriter_TabIndent
#define PFX_Debug tabwriter_Debug

static const struct {
	const char *testname;
	int minwidth, tabwidth, padding;
	char padchar;
	unsigned flags;
	const char *src, *expected;
} tw_tests[] = {
	{"1a", 8, 0, 1, '.', 0, "", ""},
	{"1a debug", 8, 0, 1, '.', PFX_Debug, "", ""},
	{"1b esc stripped", 8, 0, 1, '.', PFX_StripEscape, "\377\377", ""},
	{"1b esc", 8, 0, 1, '.', 0, "\377\377", "\377\377"},
	{"1c esc stripped", 8, 0, 1, '.', PFX_StripEscape, "\377\t\377", "\t"},
	{"1c esc", 8, 0, 1, '.', 0, "\377\t\377", "\377\t\377"},
	{"1d esc stripped", 8, 0, 1, '.', PFX_StripEscape, "\377\"foo\t\n\tbar\"\377", "\"foo\t\n\tbar\""},
	{"1d esc", 8, 0, 1, '.', 0, "\377\"foo\t\n\tbar\"\377", "\377\"foo\t\n\tbar\"\377"},
	{"1e esc stripped", 8, 0, 1, '.', PFX_StripEscape, "abc\377\tdef", "abc\tdef"},
	{"1e esc", 8, 0, 1, '.', 0, "abc\377\tdef", "abc\377\tdef"},
	{"2", 8, 0, 1, '.', 0, "\n\n\n", "\n\n\n"},
	{"3", 8, 0, 1, '.', 0, "a\nb\nc", "a\nb\nc"},
	{"4a", 8, 0, 1, '.', 0, "\t", ""},
	{"4b", 8, 0, 1, '.', PFX_AlignRight, "\t", ""},
	{"5", 8, 0, 1, '.', 0, "*\t*", "*.......*"},
	{"5b", 8, 0, 1, '.', 0, "*\t*\n", "*.......*\n"},
	{"5c", 8, 0, 1, '.', 0, "*\t*\t", "*.......*"},
	{"5c debug", 8, 0, 1, '.', PFX_Debug, "*\t*\t", "*.......|*"},
	{"5d", 8, 0, 1, '.', PFX_AlignRight, "*\t*\t", ".......**"},
	{"6", 8, 0, 1, '.', 0, "\t\n", "........\n"},
	{"7a", 8, 0, 1, '.', 0, "a) foo", "a) foo"},
	{"7b", 8, 0, 1, ' ', 0, "b) foo\tbar", "b) foo  bar"},
	{"7c", 8, 0, 1, '.', 0, "c) foo\tbar\t", "c) foo..bar"},
	{"7d", 8, 0, 1, '.', 0, "d) foo\tbar\n", "d) foo..bar\n"},
	{"7e", 8, 0, 1, '.', 0, "e) foo\tbar\t\n", "e) foo..bar.....\n"},
	{"7f", 8, 0, 1, '.', PFX_FilterHTML, "f) f&lt;o\t<b>bar</b>\t\n", "f) f&lt;o..<b>bar</b>.....\n"},
	{"7g", 8, 0, 1, '.', PFX_FilterHTML, "g) f&lt;o\t<b>bar</b>\t non-terminated entity &amp",
	 "g) f&lt;o..<b>bar</b>..... non-terminated entity &amp"},
	{"7g debug", 8, 0, 1, '.', PFX_FilterHTML | PFX_Debug, "g) f&lt;o\t<b>bar</b>\t non-terminated entity &amp",
	 "g) f&lt;o..|<b>bar</b>.....| non-terminated entity &amp"},
	{"8", 8, 0, 1, '*', 0, "Hello, world!\n", "Hello, world!\n"},
	{"9a", 1, 0, 0, '.', 0, "1\t2\t3\t4\n11\t222\t3333\t44444\n", "1.2..3...4\n11222333344444\n"},
	{"9b", 1, 0, 0, '.', PFX_FilterHTML, "1\t2<!---\f--->\t3\t4\n"
		"11\t222\t3333\t44444\n",
	 "1.2<!---\f--->..3...4\n"
		"11222333344444\n"},
	{"9c", 1, 0, 0, '.', 0, "1\t2\t3\t4\f11\t222\t3333\t44444\n", "1234\n11222333344444\n"},
	{"9c debug", 1, 0, 0, '.', PFX_Debug, "1\t2\t3\t4\f11\t222\t3333\t44444\n",
	 "1|2|3|4\n"
		"---\n"
		"11|222|3333|44444\n"},
	{"10a", 5, 0, 0, '.', 0, "1\t2\t3\t4\n", "1....2....3....4\n"},
	{"10b", 5, 0, 0, '.', 0, "1\t2\t3\t4\t\n", "1....2....3....4....\n"},
	{"11", 8, 0, 1, '.', 0, "\346\234\254\tb\tc\n"
		"aa\t\346\234\254\346\234\254\346\234\254\tcccc\tddddd\n"
		"aaa\tbbbb\n",
	 "\346\234\254.......b.......c\n"
		"aa......\346\234\254\346\234\254\346\234\254.....cccc....ddddd\n"
		"aaa.....bbbb\n"},
	{"12a", 8, 0, 1, ' ', PFX_AlignRight, "a\t\303\250\tc\t\n"
		"aa\t\303\250\303\250\303\250\tcccc\tddddd\t\n"
		"aaa\t\303\250\303\250\303\250\303\250\t\n",
	 "       a       \303\250       c\n"
		"      aa     \303\250\303\250\303\250    cccc   ddddd\n"
		"     aaa    \303\250\303\250\303\250\303\250\n"},
	{"12b", 2, 0, 0, ' ', 0, "a\tb\tc\naa\tbbb\tcccc\naaa\tbbbb\n", "a  b  c\naa bbbcccc\naaabbbb\n"},
	{"12c", 8, 0, 1, '_', 0, "a\tb\tc\n"
		"aa\tbbb\tcccc\n"
		"aaa\tbbbb\n",
	 "a_______b_______c\n"
		"aa______bbb_____cccc\n"
		"aaa_____bbbb\n"},
	{"13a", 4, 0, 1, '-', 0, "4444\t\346\227\245\346\234\254\350\252\236\t22\t1\t333\n"
		"999999999\t22\n"
		"7\t22\n"
		"\t\t\t88888888\n"
		"\n"
		"666666\t666666\t666666\t4444\n"
		"1\t1\t999999999\t0000000000\n",
	 "4444------\346\227\245\346\234\254\350\252\236-22--1---333\n"
		"999999999-22\n"
		"7---------22\n"
		"------------------88888888\n"
		"\n"
		"666666-666666-666666----4444\n"
		"1------1------999999999-0000000000\n"},
	{"13b", 4, 0, 3, '.', 0, "4444\t333\t22\t1\t333\n"
		"999999999\t22\n"
		"7\t22\n"
		"\t\t\t88888888\n"
		"\n"
		"666666\t666666\t666666\t4444\n"
		"1\t1\t999999999\t0000000000\n",
	 "4444........333...22...1...333\n"
		"999999999...22\n"
		"7...........22\n"
		"....................88888888\n"
		"\n"
		"666666...666666...666666......4444\n"
		"1........1........999999999...0000000000\n"},
	{"13c", 8, 8, 1, '\t', PFX_FilterHTML, "4444\t333\t22\t1\t333\n"
		"999999999\t22\n"
		"7\t22\n"
		"\t\t\t88888888\n"
		"\n"
		"666666\t666666\t666666\t4444\n"
		"1\t1\t<font color=red attr=\346\227\245\346\234\254\350\252\236>999999999</font>\t0000000000\n",
	 "4444\t\t333\t22\t1\t333\n"
		"999999999\t22\n"
		"7\t\t22\n"
		"\t\t\t\t88888888\n"
		"\n"
		"666666\t666666\t666666\t\t4444\n"
		"1\t1\t<font color=red attr=\346\227\245\346\234\254\350\252\236>999999999</font>\t0000000000\n"},
	{"14", 1, 0, 2, ' ', PFX_AlignRight, ".0\t.3\t2.4\t-5.1\t\n"
		"23.0\t12345678.9\t2.4\t-989.4\t\n"
		"5.1\t12.0\t2.4\t-7.0\t\n"
		".0\t0.0\t332.0\t8908.0\t\n"
		".0\t-.3\t456.4\t22.1\t\n"
		".0\t1.2\t44.4\t-13.3\t\t",
	 "    .0          .3    2.4    -5.1\n"
		"  23.0  12345678.9    2.4  -989.4\n"
		"   5.1        12.0    2.4    -7.0\n"
		"    .0         0.0  332.0  8908.0\n"
		"    .0         -.3  456.4    22.1\n"
		"    .0         1.2   44.4   -13.3"},
	{"14 debug", 1, 0, 2, ' ', PFX_AlignRight | PFX_Debug, ".0\t.3\t2.4\t-5.1\t\n"
		"23.0\t12345678.9\t2.4\t-989.4\t\n"
		"5.1\t12.0\t2.4\t-7.0\t\n"
		".0\t0.0\t332.0\t8908.0\t\n"
		".0\t-.3\t456.4\t22.1\t\n"
		".0\t1.2\t44.4\t-13.3\t\t",
	 "    .0|          .3|    2.4|    -5.1|\n"
		"  23.0|  12345678.9|    2.4|  -989.4|\n"
		"   5.1|        12.0|    2.4|    -7.0|\n"
		"    .0|         0.0|  332.0|  8908.0|\n"
		"    .0|         -.3|  456.4|    22.1|\n"
		"    .0|         1.2|   44.4|   -13.3|"},
	{"15a", 4, 0, 0, '.', 0, "a\t\tb", "a.......b"},
	{"15b", 4, 0, 0, '.', PFX_DiscardEmptyColumns, "a\t\tb", "a.......b"},
	{"15c", 4, 0, 0, '.', PFX_DiscardEmptyColumns, "a\v\vb", "a...b"},
	{"15d", 4, 0, 0, '.', PFX_AlignRight | PFX_DiscardEmptyColumns, "a\v\vb", "...ab"},
	{"16a", 100, 100, 0, '\t', 0, "a\tb\t\td\n"
		"a\tb\t\td\te\n"
		"a\n"
		"a\tb\tc\td\n"
		"a\tb\tc\td\te\n",
	 "a\tb\t\td\n"
		"a\tb\t\td\te\n"
		"a\n"
		"a\tb\tc\td\n"
		"a\tb\tc\td\te\n"},
	{"16b", 100, 100, 0, '\t', PFX_DiscardEmptyColumns, "a\vb\v\vd\n"
		"a\vb\v\vd\ve\n"
		"a\n"
		"a\vb\vc\vd\n"
		"a\vb\vc\vd\ve\n",
	 "a\tb\td\n"
		"a\tb\td\te\n"
		"a\n"
		"a\tb\tc\td\n"
		"a\tb\tc\td\te\n"},
	{"16b debug", 100, 100, 0, '\t', PFX_DiscardEmptyColumns | PFX_Debug, "a\vb\v\vd\n"
		"a\vb\v\vd\ve\n"
		"a\n"
		"a\vb\vc\vd\n"
		"a\vb\vc\vd\ve\n",
	 "a\t|b\t||d\n"
		"a\t|b\t||d\t|e\n"
		"a\n"
		"a\t|b\t|c\t|d\n"
		"a\t|b\t|c\t|d\t|e\n"},
	{"16c", 100, 100, 0, '\t', PFX_DiscardEmptyColumns, "a\tb\t\td\n"
		"a\tb\t\td\te\n"
		"a\n"
		"a\tb\tc\td\n"
		"a\tb\tc\td\te\n",
	 "a\tb\t\td\n"
		"a\tb\t\td\te\n"
		"a\n"
		"a\tb\tc\td\n"
		"a\tb\tc\td\te\n"},
	{"16c debug", 100, 100, 0, '\t', PFX_DiscardEmptyColumns | PFX_Debug, "a\tb\t\td\n"
		"a\tb\t\td\te\n"
		"a\n"
		"a\tb\tc\td\n"
		"a\tb\tc\td\te\n",
	 "a\t|b\t|\t|d\n"
		"a\t|b\t|\t|d\t|e\n"
		"a\n"
		"a\t|b\t|c\t|d\n"
		"a\t|b\t|c\t|d\t|e\n"},
};

static void verify(const char *testname, const char *how, tabwriter_Writer *w, Buf *b, const char *expected)
{
	tabwriter_Writer_Flush(w);
	size_t n = strlen(expected);
	if (b->n != n || (n && memcmp(b->p, expected, n) != 0)) {
		fprintf(stderr, "--- test: %s (%s)\n--- found:\n%.*s\n--- expected:\n%s\n", testname, how, (int)b->n,
			b->p ? b->p : "", expected);
		uni_test_failed = 1;
	}
}

static void test_table(void)
{
	for (size_t k = 0; k < sizeof tw_tests / sizeof *tw_tests; k++) {
		Buf b = {0};
		tabwriter_Writer *w =
			tabwriter_NewWriter((io_Writer){buf_writer, &b}, tw_tests[k].minwidth, tw_tests[k].tabwidth,
					    tw_tests[k].padding, tw_tests[k].padchar, tw_tests[k].flags);
		const char *src = tw_tests[k].src;
		int n = (int)strlen(src);

		/* write all at once */
		b.n = 0;
		tabwriter_Writer_Write(w, src, n);
		verify(tw_tests[k].testname, "written all at once", w, &b, tw_tests[k].expected);

		/* write byte-by-byte */
		b.n = 0;
		for (int i = 0; i < n; i++)
			tabwriter_Writer_Write(w, src + i, 1);
		verify(tw_tests[k].testname, "written byte-by-byte", w, &b, tw_tests[k].expected);

		/* write using Fibonacci slice sizes */
		b.n = 0;
		for (int i = 0, d = 0; i < n;) {
			tabwriter_Writer_Write(w, src + i, d);
			i += d, d++;
			if (i + d > n)
				d = n - i;
		}
		verify(tw_tests[k].testname, "written in fibonacci slices", w, &b, tw_tests[k].expected);
	}
}

/* The configuration go/printer uses (TabIndent | DiscardEmptyColumns, padchar ' ', tabwidth 8),
 * including escaped text and a formfeed; expected output from Go 1.26.5. */
static void test_printer_config(void)
{
	Buf b = {0};
	tabwriter_Writer *w = tabwriter_NewWriter((io_Writer){buf_writer, &b}, 0, 8, 1, ' ',
						  tabwriter_TabIndent | tabwriter_DiscardEmptyColumns);
	static const char src[] = "\tx\v= 1\t// c\n\tlonger\v= 22\t// d\n\f\t\377\"a\tb\"\377\v// e\n";
	tabwriter_Writer_Write(w, src, (int)sizeof src - 1);
	tabwriter_Writer_Flush(w);
	CHECK_MEM(b.p, b.n, "\tx      = 1  // c\n\tlonger = 22 // d\n\n\t\377\"a\tb\"\377 // e\n",
		  sizeof "\tx      = 1  // c\n\tlonger = 22 // d\n\n\t\377\"a\tb\"\377 // e\n" - 1);
}

TESTS(TEST(test_table), TEST(test_printer_config))
