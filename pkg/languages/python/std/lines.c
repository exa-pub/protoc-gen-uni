/* str.splitlines() without keepends (Objects/unicodeobject.c PyUnicode_Splitlines): the line
 * boundaries are \n, \r, \r\n, \v, \f, \x1c, \x1d, \x1e, \x85, U+2028 and U+2029. */
#include "python.h"

/* Width of the line boundary at s[i], or 0. */
static size_t boundary(Str s, size_t i)
{
	unsigned char c = (unsigned char)s.p[i];
	switch (c) {
	case '\r':
		return i + 1 < s.n && s.p[i + 1] == '\n' ? 2 : 1;
	case '\n': case 0x0b: case 0x0c: case 0x1c: case 0x1d: case 0x1e:
		return 1;
	case 0xc2: /* U+0085 */
		return i + 1 < s.n && (unsigned char)s.p[i + 1] == 0x85 ? 2 : 0;
	case 0xe2: /* U+2028, U+2029 */
		return i + 2 < s.n && (unsigned char)s.p[i + 1] == 0x80 &&
				((unsigned char)s.p[i + 2] == 0xa8 || (unsigned char)s.p[i + 2] == 0xa9)
			? 3 : 0;
	}
	return 0;
}

int py_splitlines(Str s, Str **out)
{
	int n = 0, cap = 0;
	Str *v = NULL;
	size_t start = 0, i = 0;
	while (i < s.n) {
		size_t w = boundary(s, i);
		if (!w) {
			i++;
			continue;
		}
		if (n == cap) {
			int c2 = cap ? cap * 2 : 8;
			v = xrealloc(v, sizeof *v * (size_t)cap, sizeof *v * (size_t)c2);
			cap = c2;
		}
		v[n++] = (Str){s.p + start, i - start};
		i += w;
		start = i;
	}
	if (start < s.n) {
		if (n == cap)
			v = xrealloc(v, sizeof *v * (size_t)cap, sizeof *v * (size_t)(cap + 1));
		v[n++] = (Str){s.p + start, s.n - start};
	}
	*out = v;
	return n;
}
