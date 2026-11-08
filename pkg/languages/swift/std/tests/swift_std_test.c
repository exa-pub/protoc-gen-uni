/* pkg/languages/swift/std: expected values from Swift 6 (String.split, dropFirst, isNewline). */
#include "swift_std.h"
#include "test.h"

static void test_drop_first(void)
{
	CHECK(swift_drop_first(STR_LIT("/// x"), 4, "t") == 4);
	CHECK(swift_drop_first(STR_LIT("///"), 4, "t") == 3);
	CHECK(swift_drop_first(STR_LIT("///\r\nx"), 4, "t") == 5);
	/* "e" + U+0301 is one Character; so is "/" + U+0301. */
	CHECK(swift_drop_first(STR_LIT("///e\xcc\x81x"), 4, "t") == 6);
	CHECK(swift_drop_first(STR_LIT("///\xcc\x81 x"), 4, "t") == 6);
	/* A control never joins a combining mark (GB4). */
	CHECK(swift_drop_first(STR_LIT("///\t\xcc\x81x"), 4, "t") == 4);
	CHECK(swift_drop_first(STR_LIT("///\xe4\xbd\xa0\xe5\xa5\xbd"), 4, "t") == 6);
	CHECK(swift_drop_first(STR_LIT("/// \xf0\x9f\x8e\x89"), 4, "t") == 4);
}

static Str parts_str(Str *parts, int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		buf_addc(&b, '[');
		buf_add_str(&b, parts[i]);
		buf_addc(&b, ']');
	}
	buf_str(&b);
	return buf_view(&b);
}

static void test_split(void)
{
	static const struct {
		const char *s;
		int max;
		const char *want;
	} cases[] = {
		{"macOS 15.0", 1, "[macOS][15.0]"},
		{"macOS  15.0", 1, "[macOS][ 15.0]"},
		{" a b c", 1, "[a][b c]"},
		{"a b c", -1, "[a][b][c]"},
		{"  ", 1, ""},
		{"", 1, ""},
		{"abc", 0, "[abc]"},
		{"a \xcc\x81" "b", 1, "[a \xcc\x81" "b]"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		Str *parts;
		int n = swift_split(str_c(cases[i].s), ' ', cases[i].max, &parts, "t");
		Str got = parts_str(parts, n);
		CHECK_STR(got.p ? got.p : "", cases[i].want);
	}
}

static void test_lines(void)
{
	Str *lines;
	int n = swift_lines(STR_LIT("a\r\nb\rc\nd\x0b" "e\xc2\x85" "f\xe2\x80\xa8g\n"), &lines);
	CHECK_STR(parts_str(lines, n).p, "[a][b][c][d][e][f][g][]");
	n = swift_lines(STR_LIT(""), &lines);
	CHECK(n == 1 && lines[0].n == 0);
}

static void run_unsupported(void *arg)
{
	(void)arg;
	swift_drop_first(STR_LIT("///\xe0\xa4\x95"), 4, "a comment");
}

static void test_unsupported(void)
{
	CHECK(try_run(run_unsupported, NULL) != NULL);
	CHECK(has_prefix(fail_message().p, "unsupported by protoc-gen-uni: "));
}

TESTS(TEST(test_drop_first), TEST(test_split), TEST(test_lines), TEST(test_unsupported))
