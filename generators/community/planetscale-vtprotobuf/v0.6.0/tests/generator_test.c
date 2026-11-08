/* community/planetscale-vtprotobuf v0.6.0 helpers: the pool= pattern matcher
 * (generator/pattern), KeySize, the flag set of main.go, findFeatures, unexport and the
 * unsupported-input error. Expected
 * values from the upstream code (vtprotobuf v0.6.0, Go 1.21.6). Includes the unity build to reach
 * its static functions. */
#include "../generator.c"
#include "test.h"

static void test_pattern(void)
{
	static const struct {
		const char *pattern, *name;
		bool match, bad, valid;
	} tests[] = {
		{"example.com/gen/*", "example.com/gen/proto3.Scalars", true, false, true},
		{"example.com/gen/*", "example.com/gen/a/b.C", false, false, true},
		{"example.com/gen/**", "example.com/gen/a/b.C", true, false, true},
		{"example.com/gen/**/v1.*", "example.com/gen/imports/common/v1.Item", true, false, true},
		{"example.com/gen/**/v1.*", "example.com/gen/v1.Item", true, false, true},
		{"example.com/gen/**/v1.*", "example.com/gen/v2.Item", false, false, true},
		{"**", "", true, false, true},
		{"*", "", true, false, true},
		{"", "", true, false, true},
		{"", "a", false, false, true},
		{"a/**", "a", true, false, true},
		{"a/**/", "a", true, false, true},
		{"a/**/b", "a/b", true, false, true},
		{"a/**/b", "a/x/y/b", true, false, true},
		{"a**b", "axxb", true, false, true},
		{"a**b", "ax/xb", false, false, true},
		{"?.Msg", "x.Msg", true, false, true},
		{"?", "/", false, false, true},
		{"[a-c]x", "bx", true, false, true},
		{"[a-c]x", "dx", false, false, true},
		{"[!a-c]x", "dx", true, false, true},
		{"[^a-c]x", "ax", false, false, true},
		{"[\\]]x", "]x", true, false, true},
		{"[a-]x", "-x", true, false, true},
		{"[", "a", false, true, false},
		{"[]", "a", false, true, false},
		{"[!]", "a", false, true, false},
		{"a[", "a", false, true, false},
		{"a[b", "ab", false, true, false},
		{"a[b", "ac", false, true, false},
		{"\\*x", "*x", true, false, true},
		{"\\*x", "ax", false, false, true},
		{"\\", "a", false, true, false},
		{"a\\", "a", false, true, false},
		{"{a,b}c", "bc", true, false, true},
		{"{a,b}c", "dc", false, false, true},
		{"{a,{b,c}}d", "cd", true, false, true},
		{"{a", "a", false, true, false},
		{"}", "}", true, false, false},
		{"a{,b}", "a", true, false, true},
		{"a{b,}", "a", true, false, true},
		{"x*y*z", "x1y2z", true, false, true},
		{"x*y*z", "x1y2", false, false, true},
		{"*.Pooled*", "vtpool/v1.Pooled_Inner", false, false, true},
		{"*.Pooled*", "v1.Pooled_Inner", true, false, true},
		{"\xc3\xa9?", "\xc3\xa9\xc3\xbc", true, false, true},
		{"[\xc3\xa9-\xc3\xbc]", "\xc3\xb6", true, false, true},
		{"a?", "a\xff", true, false, true},
		{"[\xff]", "\xff", true, false, true},
		{"*/*/*.*", "a/b/c.D", true, false, true},
		{"**/[Ss]*.?*", "x/y/v1.Sx", false, false, true},
		{"a/**/**/b", "a/b", true, false, true},
		{"/**", "/", true, false, true},
		{"**/", "", true, false, true},
		{"/**/", "", true, false, true},
		{"{*,x}", "", true, false, true},
		{"{x,y}", "", false, false, true},
		{"{x,y", "", false, true, false},
		{"[a", "", false, true, false},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		int r = pat_match(str_c(tests[i].pattern), str_c(tests[i].name));
		if (r != (tests[i].bad ? PAT_ERR_BAD_PATTERN : tests[i].match))
			fprintf(stderr, "Match(%s, %s) = %d\n", quote_go(str_c(tests[i].pattern)), quote_go(str_c(tests[i].name)), r);
		CHECK(r == (tests[i].bad ? PAT_ERR_BAD_PATTERN : tests[i].match));
		CHECK(pat_validate(str_c(tests[i].pattern)) == tests[i].valid);
	}
}

/* Alternatives (`{...}`; not reachable through pool=, whose values have no commas): results of
 * vtprotobuf v0.6.0 generator/pattern.Match (1, 0, -1: ErrBadPattern). */
static void test_pattern_alternatives(void)
{
	static const struct {
		const char *pattern, *name;
		int want;
	} tests[] = {
		{"{a,{b,c}d}", "cd", 1},
		{"{a,{b,c}d}", "a", 1},
		{"{a,{b,c}d}", "bd", 1},
		{"{a,{b,c}d}", "b", 0},
		{"x{a,b}{c,d}y", "xbdy", 1},
		{"x{a,b}{c,d}y", "xbcz", 0},
		{"{a,b}/**/{c,d}", "b/x/c", 1},
		{"*{x}", "ax", 1},
		{"a{**/}b", "ab", 1},
		{"a{**/}b", "a/x/b", 1},
		{"a{**/}b", "ax/b", 1},
		{"{{{x}}}", "x", 1},
		{"{{{x}}}y", "xy", 1},
		{"a{b}*{c}d", "abxxcd", 1},
		{"*{x}*{y}", "axby", 1},
		{"{*}{,}", "q", 1},
		{"x{}", "x", 1},
		{"x{/**}", "x", 1},
		{"x{,/**}", "x", 1},
		{"x{y,/**}", "x", 1},
		{"{[}]}", "}", -1},
		{"{a,b", "a", -1},
		{"a\\{b}", "a{b}", 1},
		{"{a\\,b,c}", "a,b", 1},
		{"{a\\}b,c}", "a}b", 1},
		{"{{a,b},{c,d}}e", "de", 1},
		{"**/{a,b}.{C,D}", "x/y/b.D", 1},
		{"{x/,}**/z", "z", 1},
		{"{x/,}**/z", "x/z", 1},
		{"a{,b{,c{,d}}}", "abc", 1},
		{"a{,b{,c{,d}}}", "abd", 0},
		{"[{]x}", "{x}", 1},
		{"{x[}]}", "x}", -1},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		int r = pat_match(str_c(tests[i].pattern), str_c(tests[i].name));
		if (r != tests[i].want)
			fprintf(stderr, "Match(%s, %s) = %d\n", quote_go(str_c(tests[i].pattern)), quote_go(str_c(tests[i].name)), r);
		CHECK(r == tests[i].want);
	}
}

typedef struct {
	Str pattern, name;
	int r;
} MatchCtx;

static void run_match(void *arg)
{
	MatchCtx *c = arg;
	c->r = pat_match(c->pattern, c->name);
}

/* The limits of pattern.c: deep nesting matches in memory linear in the pattern; nesting of
 * non-last alternatives over PAT_MAX_DEPTH and work over the budget are "unsupported" errors. */
static void test_pattern_limits(void)
{
	/* {{{...x...}}}: single alternatives, spliced in place */
	enum { D = 5000 };
	char *tail = xalloc(2 * D + 2);
	memset(tail, '{', D);
	tail[D] = 'x';
	memset(tail + D + 1, '}', D);
	pat_work = 0;
	ArenaMark mark = arena_mark();
	CHECK(pat_match((Str){tail, 2 * D + 1}, STR_LIT("x")) == 1);
	CHECK(pat_match((Str){tail, 2 * D + 1}, STR_LIT("y")) == 0);
	arena_release(mark);

	/* P(k) = {P(k-1),y}, P(0) = x: k levels of non-last alternatives */
	for (int k = PAT_MAX_DEPTH; k <= PAT_MAX_DEPTH + 1; k++) {
		Buf b = {0};
		for (int i = 0; i < k; i++)
			buf_addc(&b, '{');
		buf_addc(&b, 'x');
		for (int i = 0; i < k; i++)
			buf_adds(&b, ",y}");
		pat_work = 0;
		pat_unsupported = false;
		MatchCtx c = {buf_view(&b), STR_LIT("x"), -2};
		const char *err = try_run(run_match, &c);
		if (k == PAT_MAX_DEPTH) {
			CHECK(err == NULL && c.r == 1 && !pat_unsupported);
		} else {
			CHECK(err != NULL && pat_unsupported);
			CHECK_STR(err ? err : "", "unsupported by protoc-gen-uni: pool or pool-exclude pattern with alternatives nested over 100 levels");
		}
	}

	/* exponential backtracking upstream: stopped by the budget (lowered here) */
	Buf b = {0};
	for (int i = 0; i < 40; i++)
		buf_adds(&b, "{*,a}");
	buf_addc(&b, 'c');
	pat_work = 0;
	pat_unsupported = false;
	pat_budget = 1 << 22;
	MatchCtx c = {buf_view(&b), STR_LIT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), -2};
	const char *err = try_run(run_match, &c);
	pat_budget = PAT_BUDGET;
	CHECK(err != NULL && pat_unsupported);
	CHECK_STR(err ? err : "", "unsupported by protoc-gen-uni: pool or pool-exclude patterns that take over 1073 million steps to match");
	pat_work = 0;
	pat_unsupported = false;
}

static void test_key_size(void)
{
	static const int tests[][3] = {
		{1, 0, 1}, {15, 2, 1}, {16, 0, 2}, {2047, 2, 2}, {2048, 2, 3}, {262143, 5, 3}, {262144, 5, 4}, {536870911, 4, 5},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++)
		CHECK(key_size(tests[i][0], tests[i][1]) == tests[i][2]);
}

static void test_flags(void)
{
	static const struct {
		const char *name, *value, *err;
	} tests[] = {
		{"allow-empty", "", "parse error"},
		{"allow-empty", "TRUE", NULL},
		{"wrap", "yes", "parse error"},
		{"pool", "[", "syntax error in pattern"},
		{"pool-exclude", "a{", "syntax error in pattern"},
		{"pool", "ok*", NULL},
		{"bogus", "x", "no such flag -bogus"},
		{"-wrap", "1", "no such flag --wrap"},
		{"features", "", NULL},
		{"buildTag", "a b", NULL},
		{"Wrap", "1", "no such flag -Wrap"},
	};
	Flags fl = {0};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		Str err = set_flag(&fl, str_c(tests[i].name), str_c(tests[i].value));
		if (tests[i].err) {
			CHECK(err.p != NULL);
			if (err.p)
				CHECK_MEM(err.p, err.n, tests[i].err, strlen(tests[i].err));
		} else {
			CHECK(err.p == NULL);
		}
	}
	CHECK(fl.cfg.AllowEmpty && !fl.cfg.Wrap && fl.cfg.Poolable.patterns.n == 1 && fl.cfg.PoolableExclude.patterns.n == 0);
	CHECK(str_eqc(fl.cfg.BuildTag, "a b") && fl.features.n == 0);
}

static void test_find_features(void)
{
	bool sel[N_FEATURES] = {0};
	CHECK(find_features(STR_LIT("all+bogus"), sel).p == NULL);
	for (int k = 0; k < N_FEATURES; k++)
		CHECK(sel[k]);
	bool sel2[N_FEATURES] = {0};
	Str err = find_features(STR_LIT("size+bogus+all"), sel2);
	CHECK(err.p && str_eqc(err, "unknown feature: \"bogus\""));
	bool sel3[N_FEATURES] = {0};
	err = find_features(STR_LIT(""), sel3);
	CHECK(err.p && str_eqc(err, "unknown feature: \"\""));
	bool sel4[N_FEATURES] = {0};
	CHECK(find_features(STR_LIT("unmarshal_unsafe+clone+size+size"), sel4).p == NULL);
	CHECK(sel4[0] && !sel4[1] && sel4[6] && sel4[8] && !sel4[7]);
	/* the order of generation is the sorted feature names */
	for (int k = 1; k < N_FEATURES; k++)
		CHECK(strcmp(default_features[k - 1].name, default_features[k].name) < 0);
}

static void test_unexport(void)
{
	CHECK_STR(unexport("Foo"), "foo");
	CHECK_STR(unexport("foo"), "foo");
	CHECK_STR(unexport("FOO"), "fOO");
	CHECK_STR(unexport("X"), "x");
	CHECK_STR(unexport("\xc3\x89" "cole"), "\xef\xbf\xbd\x89" "cole");
}

/* A NUL byte in a file name: upstream accepts it, the port reports it as unsupported. */
static void test_unsupported(void)
{
	static const char raw[] = "\x7a\x13"                          /* proto_file */
				  "\x0a\x09" "a\0b.proto" "\x62\x06" "proto3"; /* name, syntax */
	Str r = {raw, sizeof raw - 1};
	const char *perr;
	PbMsg *uni_req = pg_parse_request((const uint8_t *)r.p, r.n, &perr);
	CHECK(uni_req != NULL);
	if (!uni_req)
		return;
	Buf out = {0};
	CHECK(generate(uni_req, r, &out) == NULL);
	static const char want[] = "unsupported by protoc-gen-uni: NUL byte in file name \"a\\x00b.proto\"";
	CHECK(out.n == sizeof want + 1 && out.p[0] == 0x0a && out.p[1] == (char)(sizeof want - 1));
	if (out.n == sizeof want + 1)
		CHECK_MEM(out.p + 2, out.n - 2, want, sizeof want - 1);
}

TESTS(TEST(test_pattern), TEST(test_pattern_alternatives), TEST(test_pattern_limits), TEST(test_key_size), TEST(test_flags), TEST(test_find_features), TEST(test_unexport),
      TEST(test_unsupported))
