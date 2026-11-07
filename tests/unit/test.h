/* Minimal unit test helpers. A test file defines test functions and a table:
 *
 *	static void test_varint(void) { CHECK(...); CHECK_STR(got, "want"); }
 *	TESTS(TEST(test_varint), TEST(test_other))
 *
 * and is built as its own executable (uni_add_unit_test in CMake). A failed check prints
 * file:line and marks the test failed; the executable exits 1 if any test failed. */
#ifndef UNI_TEST_H
#define UNI_TEST_H

#include <stdio.h>
#include <string.h>

typedef struct {
	const char *name;
	void (*fn)(void);
} UniTest;

static int uni_test_failed;

#define CHECK(cond)                                                                                \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);   \
			uni_test_failed = 1;                                                       \
		}                                                                                  \
	} while (0)

#define CHECK_MEM(got, got_n, want, want_n)                                                        \
	do {                                                                                       \
		size_t gn_ = (got_n), wn_ = (want_n);                                              \
		if (gn_ != wn_ || memcmp((got), (want), gn_) != 0) {                               \
			fprintf(stderr, "%s:%d: got\n\t%.*s\nwant\n\t%.*s\n", __FILE__, __LINE__,  \
				(int)gn_, (const char *)(got), (int)wn_, (const char *)(want));    \
			uni_test_failed = 1;                                                       \
		}                                                                                  \
	} while (0)

#define CHECK_STR(got, want)                                                                       \
	do {                                                                                       \
		const char *g_ = (got), *w_ = (want);                                              \
		CHECK_MEM(g_, strlen(g_), w_, strlen(w_));                                         \
	} while (0)

#define TEST(fn) {#fn, fn}

#define TESTS(...)                                                                                 \
	int main(int argc, char **argv)                                                            \
	{                                                                                          \
		static const UniTest tests[] = {__VA_ARGS__};                                      \
		int failed = 0;                                                                    \
		for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {                        \
			if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)                       \
				continue;                                                          \
			uni_test_failed = 0;                                                       \
			tests[i].fn();                                                             \
			printf("%s %s\n", uni_test_failed ? "FAIL" : "ok  ", tests[i].name);      \
			failed |= uni_test_failed;                                                 \
		}                                                                                  \
		return failed;                                                                     \
	}

#endif
