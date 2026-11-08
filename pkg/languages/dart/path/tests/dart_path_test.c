/* pkg/languages/dart/path posix Context: expected values from package:path 1.9.1 on Dart 3.13
 * (Context(style: Style.posix)). */
#include "dart_path.h"
#include "test.h"

static void test_posix(void)
{
	static const struct {
		const char *path, *dirname, *basenameWithoutExtension, *withoutExtension, *joinNormalize;
	} cases[] = {
		{"a/b.proto", "a", "b", "a/b", "a/z"},
		{"x.proto", ".", "x", "x", "z"},
		{"x", ".", "x", "x", "z"},
		{"", ".", "", "", "z"},
		{".proto", ".", ".proto", ".proto", "z"},
		{"a/.b", "a", ".b", "a/.b", "a/z"},
		{"a/b/", "a", "b", "a/b/", "a/z"},
		{"a//b.proto", "a", "b", "a//b", "a/z"},
		{"/abs/x.proto", "/abs", "x", "/abs/x", "/abs/z"},
		{"/", "/", "", "/", "/z"},
		{"//x", "/", "x", "//x", "/z"},
		{"../x.proto", "..", "x", "../x", "../z"},
		{"../../a/b.proto", "../../a", "b", "../../a/b", "../../a/z"},
		{"a/../b.proto", "a/..", "b", "a/../b", "z"},
		{"./x.proto", ".", "x", "./x", "z"},
		{".", ".", ".", ".", "z"},
		{"..", ".", "..", "..", "z"},
		{"a/b.c/d", "a/b.c", "d", "a/b.c/d", "a/b.c/z"},
		{"a.b.c", ".", "a.b", "a.b", "z"},
		{"a/./b/../c.d.e", "a/./b/..", "c.d", "a/./b/../c.d", "a/z"},
		{"/..", "/", "..", "/..", "/z"},
		{"a/b/../..", "a/b/..", "..", "a/b/../..", "a/z"},
		{"a/b/../../..", "a/b/../..", "..", "a/b/../../..", "z"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		CHECK_STR(dpath_posix_dirname(cases[i].path), cases[i].dirname);
		CHECK_STR(dpath_posix_basename_without_extension(cases[i].path), cases[i].basenameWithoutExtension);
		CHECK_STR(dpath_posix_without_extension(cases[i].path), cases[i].withoutExtension);
		CHECK_STR(dpath_posix_normalize(dpath_posix_join(dpath_posix_dirname(cases[i].path), "z")), cases[i].joinNormalize);
	}
}

static void test_posix_relative(void)
{
	static const struct {
		const char *path, *from, *want, *err;
	} cases[] = {
		{"a/b.proto", "a", "b.proto", NULL},
		{"a/b.proto", ".", "a/b.proto", NULL},
		{"a/b.proto", "c/d", "../../a/b.proto", NULL},
		{"x.proto", "..", NULL, "PathException: Unable to find a path to \"./x.proto\" from \"./..\"."},
		{"../x.proto", "..", "x.proto", NULL},
		{"/a/x.proto", "/a", "x.proto", NULL},
		{"/a/x.proto", "/b/c", "../../a/x.proto", NULL},
		{"a/x.proto", "/a", NULL, "PathException: Unable to find a path to \"./a/x.proto\" from \"/a\"."},
		{"/a/x.proto", "a", "/a/x.proto", NULL},
		{"a/b/c.proto", "a/b", "c.proto", NULL},
		{"a/b.proto", "a/b.proto", ".", NULL},
		{"a", "a/b", "..", NULL},
		{".", "a", "..", NULL},
		{"a/./b", "a/c/..", "b", NULL},
		{"x", "", "x", NULL},
		{"", "a", "..", NULL},
		{"a//b", "a/", "b", NULL},
		{"../../q", "../a", "../../q", NULL},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		const char *err = NULL;
		const char *got = dpath_posix_relative(cases[i].path, cases[i].from, &err);
		if (cases[i].want) {
			CHECK_STR(got ? got : "(null)", cases[i].want);
		} else {
			CHECK(!got);
			CHECK_STR(err ? err : "(null)", cases[i].err);
		}
	}
}

TESTS(TEST(test_posix), TEST(test_posix_relative))
