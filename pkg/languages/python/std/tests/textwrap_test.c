/* textwrap.wrap(text, width) cases with the output of CPython 3.11.4 (betterproto's Python). */
#include "python.h"
#include "test.h"

static void check(const char *text, int width, int n, const char *const *want)
{
	Str *lines;
	int got = py_textwrap_wrap(str_c(text), width, &lines);
	CHECK(got == n);
	for (int i = 0; i < got && i < n; i++)
		CHECK_MEM(lines[i].p, lines[i].n, want[i], strlen(want[i]));
}

static void test_wrap(void)
{
	check("Look, goof-ball -- use the -b option!", 10, 4, (const char *const[]){"Look,", "goof-ball", "-- use the", "-b option!"});
	check("a well-known self-describing e-mail address in the text that wraps around", 20, 4, (const char *const[]){"a well-known self-", "describing e-mail", "address in the text", "that wraps around"});
	check("caf\xc3""\xa9""-bar Z\xc3""\xbc""rich-based na\xc3""\xaf""ve-ly \xc3""\xbc""ber-cool 3-4 x-1 ab-cd-ef -- --- a--b \xc3""\xa9""--b", 12, 7, (const char *const[]){"caf\xc3""\xa9""-bar", "Z\xc3""\xbc""rich-based", "na\xc3""\xaf""ve-ly", "\xc3""\xbc""ber-cool", "3-4 x-1 ab-", "cd-ef -- ---", "a--b \xc3""\xa9""--b"});
	check("supercalifragilisticexpialidocious-and-more-hyphenated-words", 15, 5, (const char *const[]){"supercalifragil", "isticexpialidoc", "ious-and-more-", "hyphenated-", "words"});
	check("tabs\there\tand\tthere, spaces   and\xc2""\xa0""nbsp\xc2""\xa0""chunks", 16, 4, (const char *const[]){"tabs    here", "and     there,", "spaces", "and\xc2""\xa0""nbsp\xc2""\xa0""chunks"});
	check("\xe6""\x97""\xa5""\xe6""\x9c""\xac""\xe8""\xaa""\x9e""\xe3""\x81""\xae""\xe3""\x83""\x86""\xe3""\x82""\xad""\xe3""\x82""\xb9""\xe3""\x83""\x88""-\xe3""\x81""\xa8""\xe3""\x83""\x8f""\xe3""\x82""\xa4""\xe3""\x83""\x95""\xe3""\x83""\xb3"" \xce""\xb5""\xce""\xbb""\xce""\xbb""\xce""\xb7""\xce""\xbd""\xce""\xb9""\xce""\xba""\xce""\xac""-\xce""\xbb""\xce""\xad""\xce""\xbe""\xce""\xb7"" \xd9""\xa3""-\xd9""\xa4"" x\xd9""\xa3""-y", 10, 5, (const char *const[]){"\xe6""\x97""\xa5""\xe6""\x9c""\xac""\xe8""\xaa""\x9e""\xe3""\x81""\xae""\xe3""\x83""\x86""\xe3""\x82""\xad""\xe3""\x82""\xb9""\xe3""\x83""\x88""-", "\xe3""\x81""\xa8""\xe3""\x83""\x8f""\xe3""\x82""\xa4""\xe3""\x83""\x95""\xe3""\x83""\xb3""", "\xce""\xb5""\xce""\xbb""\xce""\xbb""\xce""\xb7""\xce""\xbd""\xce""\xb9""\xce""\xba""\xce""\xac""-", "\xce""\xbb""\xce""\xad""\xce""\xbe""\xce""\xb7"" \xd9""\xa3""-\xd9""\xa4""", "x\xd9""\xa3""-y"});
	check("---leading hyphens and trailing---", 8, 5, (const char *const[]){"---leadi", "ng", "hyphens", "and trai", "ling---"});
	check("12345678901234567890", 7, 3, (const char *const[]){"1234567", "8901234", "567890"});
	check("", 10, 0, (const char *const[]){NULL});
	check("   ", 10, 0, (const char *const[]){NULL});
	check("\xc2""\xa0""x", 3, 1, (const char *const[]){"\xc2""\xa0""x"});
}

TESTS(TEST(test_wrap))
