/* sorted() of str values (CPython 3.13 unicode_compare: code point order). UTF-8 preserves code
 * point order, so comparing the bytes is enough. */
#include "python.h"

#include <stdlib.h>
#include <string.h>

int py_str_cmp(Str a, Str b)
{
	size_t n = a.n < b.n ? a.n : b.n;
	int c = n ? memcmp(a.p, b.p, n) : 0;
	if (c)
		return c;
	return a.n < b.n ? -1 : a.n > b.n;
}

static int cmp_cstr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static int cmp_str(const void *a, const void *b) { return py_str_cmp(*(const Str *)a, *(const Str *)b); }

/* Equal str values are indistinguishable, so the stability of sorted() does not matter. */
void py_sort_cstrs(const char **v, int n)
{
	if (n > 1)
		qsort(v, (size_t)n, sizeof *v, cmp_cstr);
}

void py_sort_strs(Str *v, int n)
{
	if (n > 1)
		qsort(v, (size_t)n, sizeof *v, cmp_str);
}
