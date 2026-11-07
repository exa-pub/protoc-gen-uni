/* Package unicode/utf8, ported from go1.26.5 src/unicode/utf8/utf8.go (same results; the
 * table-driven fast paths are replaced by direct checks).
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../internal.h"

/* decode returns the rune at p[0:n] (n > 0), its size, and whether the encoding is complete
 * (for FullRune). Invalid or short encodings yield (RuneError, 1). */
static rune utf8_decodeRune_(const unsigned char *p, int n, int *size, bool *full)
{
	unsigned c0 = p[0];
	*full = true;
	if (c0 < 0x80) {
		*size = 1;
		return (rune)c0;
	}
	int need;
	rune r, min;
	unsigned lo = 0x80, hi = 0xBF; /* accepted range of the second byte */
	if (c0 >= 0xC2 && c0 <= 0xDF) {
		need = 2, r = c0 & 0x1F, min = 0x80;
	} else if (c0 >= 0xE0 && c0 <= 0xEF) {
		need = 3, r = c0 & 0x0F, min = 0x800;
		if (c0 == 0xE0)
			lo = 0xA0;
		else if (c0 == 0xED)
			hi = 0x9F; /* surrogates */
	} else if (c0 >= 0xF0 && c0 <= 0xF4) {
		need = 4, r = c0 & 0x07, min = 0x10000;
		if (c0 == 0xF0)
			lo = 0x90;
		else if (c0 == 0xF4)
			hi = 0x8F;
	} else {
		*size = 1;
		return utf8_RuneError;
	}
	(void)min;
	for (int i = 1; i < need; i++) {
		if (i >= n) {
			*full = false;
			*size = 1;
			return utf8_RuneError;
		}
		unsigned c = p[i];
		unsigned l = i == 1 ? lo : 0x80, h = i == 1 ? hi : 0xBF;
		if (c < l || c > h) {
			*size = 1;
			return utf8_RuneError;
		}
		r = (r << 6) | (rune)(c & 0x3F);
	}
	*size = need;
	return r;
}

GO_HELPER rune utf8_DecodeRune(const char *p, int n, int *size)
{
	int sz = 0;
	rune r = utf8_RuneError;
	if (n > 0) {
		bool full;
		r = utf8_decodeRune_((const unsigned char *)p, n, &sz, &full);
	}
	if (size)
		*size = sz;
	return r;
}

GO_HELPER rune utf8_DecodeRuneInString(GoStr s, int *size) { return utf8_DecodeRune(s.p, s.n, size); }

GO_HELPER bool utf8_RuneStart(char b) { return ((unsigned char)b & 0xC0) != 0x80; }

GO_HELPER rune utf8_DecodeLastRuneInString(GoStr s, int *size)
{
	int end = s.n;
	if (end == 0) {
		if (size)
			*size = 0;
		return utf8_RuneError;
	}
	int start = end - 1;
	if ((unsigned char)s.p[start] < utf8_RuneSelf) {
		if (size)
			*size = 1;
		return (rune)(unsigned char)s.p[start];
	}
	/* guard against O(n^2) behavior when traversing backwards through invalid UTF-8 */
	int lim = end - utf8_UTFMax;
	if (lim < 0)
		lim = 0;
	for (start--; start >= lim; start--) {
		if (utf8_RuneStart(s.p[start]))
			break;
	}
	if (start < 0)
		start = 0;
	int sz;
	rune r = utf8_DecodeRune(s.p + start, end - start, &sz);
	if (start + sz != end) {
		r = utf8_RuneError;
		sz = 1;
	}
	if (size)
		*size = sz;
	return r;
}

GO_HELPER int utf8_RuneLen(rune r)
{
	if (r < 0)
		return -1;
	if (r < 0x80)
		return 1;
	if (r < 0x800)
		return 2;
	if (r >= 0xD800 && r <= 0xDFFF)
		return -1;
	if (r < 0x10000)
		return 3;
	if (r <= utf8_MaxRune)
		return 4;
	return -1;
}

GO_HELPER int utf8_EncodeRune(char *p, rune r)
{
	uint32_t u = (uint32_t)r;
	if (u < 0x80) {
		p[0] = (char)u;
		return 1;
	}
	if (u < 0x800) {
		p[0] = (char)(0xC0 | (u >> 6));
		p[1] = (char)(0x80 | (u & 0x3F));
		return 2;
	}
	if (u > utf8_MaxRune || (u >= 0xD800 && u <= 0xDFFF))
		u = utf8_RuneError;
	if (u < 0x10000) {
		p[0] = (char)(0xE0 | (u >> 12));
		p[1] = (char)(0x80 | ((u >> 6) & 0x3F));
		p[2] = (char)(0x80 | (u & 0x3F));
		return 3;
	}
	p[0] = (char)(0xF0 | (u >> 18));
	p[1] = (char)(0x80 | ((u >> 12) & 0x3F));
	p[2] = (char)(0x80 | ((u >> 6) & 0x3F));
	p[3] = (char)(0x80 | (u & 0x3F));
	return 4;
}

GO_HELPER void utf8_AppendRune(Buf *b, rune r)
{
	char tmp[utf8_UTFMax];
	buf_add(b, tmp, (size_t)utf8_EncodeRune(tmp, r));
}

GO_HELPER int utf8_RuneCount(const char *p, int n)
{
	int count = 0;
	for (int i = 0; i < n; count++) {
		int sz;
		utf8_DecodeRune(p + i, n - i, &sz);
		i += sz;
	}
	return count;
}

GO_HELPER int utf8_RuneCountInString(GoStr s) { return utf8_RuneCount(s.p, s.n); }

GO_HELPER bool utf8_ValidString(GoStr s)
{
	for (int i = 0; i < s.n;) {
		int sz;
		rune r = utf8_DecodeRune(s.p + i, s.n - i, &sz);
		if (r == utf8_RuneError && sz == 1)
			return false;
		i += sz;
	}
	return true;
}

GO_HELPER bool utf8_ValidRune(rune r)
{
	return (r >= 0 && r < 0xD800) || (r > 0xDFFF && r <= utf8_MaxRune);
}

GO_HELPER bool utf8_FullRune(const char *p, int n)
{
	if (n == 0)
		return false;
	int sz;
	bool full;
	utf8_decodeRune_((const unsigned char *)p, n, &sz, &full);
	return full;
}
