/* str.isspace, str.isprintable and str.strip of CPython 3.13 (Objects/unicodectype.c,
 * Objects/unicodeobject.c do_strip; Unicode 15.1.0). */
#include "python.h"

#include "isprint.h"

bool py_isspace(uint32_t c)
{
	/* _PyUnicode_IsWhitespace (Objects/unicodetype_db.h) */
	switch (c) {
	case 0x20: case 0x85: case 0xa0: case 0x1680: case 0x2028: case 0x2029: case 0x202f: case 0x205f:
	case 0x3000:
		return true;
	}
	return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x1f) || (c >= 0x2000 && c <= 0x200a);
}

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

bool py_isprintable(uint32_t c)
{
	/* binary search for the last range with lo <= c */
	int lo = 0, hi;
	if (c <= 0xffff) {
		hi = N(py_print16) / 2;
		while (lo < hi) {
			int m = (lo + hi) / 2;
			if (py_print16[2 * m] <= c)
				lo = m + 1;
			else
				hi = m;
		}
		return lo > 0 && c <= py_print16[2 * (lo - 1) + 1];
	}
	hi = N(py_print32) / 2;
	while (lo < hi) {
		int m = (lo + hi) / 2;
		if (py_print32[2 * m] <= c)
			lo = m + 1;
		else
			hi = m;
	}
	return lo > 0 && c <= py_print32[2 * (lo - 1) + 1];
}

/* Width of a valid UTF-8 whitespace character at the start of s, or 0. */
static size_t leading_space(Str s)
{
	uint32_t r;
	int w = utf8_decode(s.p, s.n, &r);
	if (r == 0xfffd && w == 1)
		return 0; /* invalid byte (U+FFFD itself is not a space) */
	return py_isspace(r) ? (size_t)w : 0;
}

/* Width of a valid UTF-8 whitespace character at the end of s, or 0. */
static size_t trailing_space(Str s)
{
	for (size_t w = 1; w <= 3 && w <= s.n; w++) {
		uint32_t r;
		if (utf8_decode(s.p + s.n - w, w, &r) == (int)w && (w == 1 || r >= 0x80) && py_isspace(r))
			return w;
	}
	return 0;
}

Str py_lstrip(Str s)
{
	for (size_t w; s.n && (w = leading_space(s));)
		s.p += w, s.n -= w;
	return s;
}

Str py_rstrip(Str s)
{
	for (size_t w; s.n && (w = trailing_space(s));)
		s.n -= w;
	return s;
}

Str py_strip(Str s) { return py_rstrip(py_lstrip(s)); }
