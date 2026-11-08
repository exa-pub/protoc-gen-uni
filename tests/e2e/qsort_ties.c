/* UNI_QSORT_CHECK (cmake/flags.cmake): linked with -Wl,--wrap=qsort, it replaces qsort in every
 * executable with a sort that orders equal elements unlike any libc does: a stable merge sort,
 * then every run of equal elements reversed. A golden test that fails with it depends on the order
 * of ties, which the C library leaves open, while the upstream sort fixes it. */
#include <stdlib.h>
#include <string.h>

typedef int (*Cmp)(const void *, const void *);

static void merge_sort(char *a, char *tmp, size_t n, size_t w, Cmp cmp)
{
	if (n < 2)
		return;
	size_t h = n / 2, i = 0, j = h, k = 0;
	merge_sort(a, tmp, h, w, cmp);
	merge_sort(a + h * w, tmp, n - h, w, cmp);
	while (i < h && j < n)
		memcpy(tmp + k++ * w, cmp(a + j * w, a + i * w) < 0 ? a + j++ * w : a + i++ * w, w);
	while (i < h)
		memcpy(tmp + k++ * w, a + i++ * w, w);
	while (j < n)
		memcpy(tmp + k++ * w, a + j++ * w, w);
	memcpy(a, tmp, n * w);
}

void __wrap_qsort(void *base, size_t n, size_t w, Cmp cmp);

void __wrap_qsort(void *base, size_t n, size_t w, Cmp cmp)
{
	if (n < 2 || !w)
		return;
	char *a = base, *tmp = malloc(n * w), *t = malloc(w);
	if (!tmp || !t)
		abort();
	merge_sort(a, tmp, n, w, cmp);
	for (size_t s = 0; s < n;) {
		size_t e = s + 1;
		while (e < n && cmp(a + s * w, a + e * w) == 0)
			e++;
		for (size_t l = s, r = e - 1; l < r; l++, r--) {
			memcpy(t, a + l * w, w);
			memcpy(a + l * w, a + r * w, w);
			memcpy(a + r * w, t, w);
		}
		s = e;
	}
	free(tmp);
	free(t);
}
