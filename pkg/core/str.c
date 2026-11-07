/* String helpers: NUL-terminated strings, Str (pointer + length) and UTF-8. */
#include "util.h"

#include <string.h>

bool str_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

bool has_prefix(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

bool has_suffix(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && memcmp(s + n - m, suffix, m) == 0;
}

char *str_trim_suffix(const char *s, const char *suffix)
{
	if (has_suffix(s, suffix))
		return xstrndup(s, strlen(s) - strlen(suffix));
	return (char *)s;
}

char *str_replace_all(const char *s, const char *from, const char *to)
{
	size_t fl = strlen(from);
	if (!fl || !strstr(s, from))
		return (char *)s;
	Buf b = {0};
	for (const char *p; (p = strstr(s, from)); s = p + fl) {
		buf_add(&b, s, (size_t)(p - s));
		buf_adds(&b, to);
	}
	buf_adds(&b, s);
	return (char *)buf_str(&b);
}

Vec str_split(const char *s, char sep)
{
	Vec v = {0};
	for (;;) {
		const char *p = strchr(s, sep);
		if (!p) {
			vec_push(&v, xstrdup(s));
			return v;
		}
		vec_push(&v, xstrndup(s, (size_t)(p - s)));
		s = p + 1;
	}
}

char *str_join(const Vec *parts, const char *sep)
{
	Buf b = {0};
	for (int i = 0; i < parts->n; i++) {
		if (i)
			buf_adds(&b, sep);
		buf_adds(&b, parts->v[i]);
	}
	return (char *)buf_str(&b);
}

/* ---- Str ---- */

Str str_c(const char *s) { return (Str){s, strlen(s)}; }

bool str_equal(Str a, Str b) { return a.n == b.n && (!a.n || memcmp(a.p, b.p, a.n) == 0); }

bool str_eqc(Str a, const char *b) { return str_equal(a, str_c(b)); }

bool str_has_prefix(Str s, Str prefix) { return s.n >= prefix.n && (!prefix.n || memcmp(s.p, prefix.p, prefix.n) == 0); }

bool str_has_suffix(Str s, Str suffix)
{
	return s.n >= suffix.n && (!suffix.n || memcmp(s.p + s.n - suffix.n, suffix.p, suffix.n) == 0);
}

Str str_trim_suffix_s(Str s, Str suffix)
{
	if (str_has_suffix(s, suffix))
		s.n -= suffix.n;
	return s;
}

int str_split_s(Str s, char sep, Str **out)
{
	int n = 1;
	for (size_t i = 0; i < s.n; i++)
		n += s.p[i] == sep;
	Str *v = xalloc(sizeof(Str) * (size_t)n);
	int k = 0;
	size_t start = 0;
	for (size_t i = 0; i < s.n; i++)
		if (s.p[i] == sep) {
			v[k++] = (Str){s.p + start, i - start};
			start = i + 1;
		}
	v[k] = (Str){s.p + start, s.n - start};
	*out = v;
	return n;
}

void buf_add_str(Buf *b, Str s) { buf_add(b, s.p, s.n); }

Str buf_view(Buf *b) { return (Str){buf_str(b), b->n}; }

/* ---- UTF-8 ---- */

/* Ported from Go's unicode/utf8.DecodeRune (Copyright 2009 The Go Authors, BSD license). */
int utf8_decode(const char *s, size_t n, uint32_t *rune)
{
	const unsigned char *p = (const unsigned char *)s;
	unsigned char b0 = p[0];
	*rune = 0xfffd;
	if (b0 < 0x80) {
		*rune = b0;
		return 1;
	}
	int size;
	unsigned char lo = 0x80, hi = 0xbf;
	uint32_t r;
	if (b0 >= 0xc2 && b0 <= 0xdf) {
		size = 2, r = b0 & 0x1f;
	} else if (b0 >= 0xe0 && b0 <= 0xef) {
		size = 3, r = b0 & 0x0f;
		if (b0 == 0xe0)
			lo = 0xa0; /* overlong */
		else if (b0 == 0xed)
			hi = 0x9f; /* surrogates */
	} else if (b0 >= 0xf0 && b0 <= 0xf4) {
		size = 4, r = b0 & 0x07;
		if (b0 == 0xf0)
			lo = 0x90; /* overlong */
		else if (b0 == 0xf4)
			hi = 0x8f; /* > U+10FFFF */
	} else {
		return 1;
	}
	if (n < (size_t)size || p[1] < lo || p[1] > hi)
		return 1;
	r = r << 6 | (p[1] & 0x3f);
	for (int i = 2; i < size; i++) {
		if (p[i] < 0x80 || p[i] > 0xbf)
			return 1;
		r = r << 6 | (p[i] & 0x3f);
	}
	*rune = r;
	return size;
}

/* unicode.IsSpace */
static bool go_is_space(uint32_t r)
{
	switch (r) {
	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ': case 0x85: case 0xa0:
	case 0x1680: case 0x2028: case 0x2029: case 0x202f: case 0x205f: case 0x3000:
		return true;
	}
	return r >= 0x2000 && r <= 0x200a;
}

/* Length of the UTF-8 encoding of a space rune that s ends with, or 0. A valid encoding at the
 * end is exactly what Go's utf8.DecodeLastRune decodes there. */
static size_t trailing_space(Str s)
{
	for (size_t w = 1; w <= 3 && w <= s.n; w++) {
		uint32_t r;
		if (utf8_decode(s.p + s.n - w, w, &r) == (int)w && (w == 1 || r >= 0x80) && go_is_space(r))
			return w;
	}
	return 0;
}

/* Ported from Go's strings.TrimSpace (Copyright 2009 The Go Authors, BSD license). */
Str go_trim_space(Str s)
{
	while (s.n) {
		uint32_t r;
		int w = utf8_decode(s.p, s.n, &r);
		if (!go_is_space(r))
			break;
		s.p += w, s.n -= (size_t)w;
	}
	for (size_t w; s.n && (w = trailing_space(s));)
		s.n -= w;
	return s;
}
