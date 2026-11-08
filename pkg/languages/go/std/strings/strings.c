/* GoStr helpers and the subset of package strings used by the port.
 * Semantics ported from go1.27.1 src/strings/strings.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../internal.h"

GO_HELPER GoStr gostr(const char *p, int n) { return (GoStr){p, n}; }
GO_HELPER GoStr gostr_c(const char *s) { return (GoStr){s, (int)strlen(s)}; }
GO_HELPER GoStr gostr_buf(const Buf *b) { return (GoStr){b->p, (int)b->n}; }

GO_HELPER GoStr gostr_sub(GoStr s, int i, int j)
{
	if (i < 0 || j < i || j > s.n)
		fail("go: slice bounds out of range [%d:%d] with length %d", i, j, s.n);
	return (GoStr){s.p ? s.p + i : s.p, j - i}; /* no pointer arithmetic on NULL (empty) */
}

GO_HELPER GoStr gostr_from(GoStr s, int i) { return gostr_sub(s, i, s.n); }

GO_HELPER bool gostr_eq(GoStr a, GoStr b) { return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, (size_t)a.n) == 0); }
GO_HELPER bool gostr_eqc(GoStr a, const char *b) { return gostr_eq(a, gostr_c(b)); }

GO_HELPER int gostr_cmp(GoStr a, GoStr b)
{
	int n = a.n < b.n ? a.n : b.n;
	int c = n ? memcmp(a.p, b.p, (size_t)n) : 0;
	if (c != 0)
		return c < 0 ? -1 : 1;
	return a.n < b.n ? -1 : a.n > b.n ? 1 : 0;
}

GO_HELPER char *gostr_cstr(GoStr s) { return xstrndup(s.p, (size_t)s.n); }
GO_HELPER GoStr gostr_dup(GoStr s) { return (GoStr){xstrndup(s.p, (size_t)s.n), s.n}; }

GO_HELPER GoStr gostr_cat(GoStr a, GoStr b)
{
	char *p = xalloc((size_t)a.n + (size_t)b.n + 1);
	if (a.n)
		memcpy(p, a.p, (size_t)a.n);
	if (b.n)
		memcpy(p + a.n, b.p, (size_t)b.n);
	return (GoStr){p, a.n + b.n};
}

GO_HELPER void buf_addg(Buf *b, GoStr s) { buf_add(b, s.p, (size_t)s.n); }

GO_HELPER void *go_grow(void *p, int *cap, int min, size_t elem)
{
	int old = *cap;
	if (old < 0 || old > INT32_MAX / 2)
		fail("go: slice too large");
	int n = old ? 2 * old : min;
	*cap = n;
	return xrealloc(p, elem * (size_t)old, elem * (size_t)n);
}

GO_HELPER void gostrvec_push(GoStrVec *v, GoStr s)
{
	if (v->n == v->cap) {
		v->v = go_grow(v->v, &v->cap, 8, sizeof *v->v);
	}
	v->v[v->n++] = s;
}

GO_HELPER void intvec_push(IntVec *v, int x)
{
	if (v->n == v->cap) {
		v->v = go_grow(v->v, &v->cap, 16, sizeof *v->v);
	}
	v->v[v->n++] = x;
}

GO_HELPER bool strings_HasPrefix(GoStr s, GoStr prefix)
{
	return s.n >= prefix.n && (prefix.n == 0 || memcmp(s.p, prefix.p, (size_t)prefix.n) == 0);
}

GO_HELPER bool strings_HasSuffix(GoStr s, GoStr suffix)
{
	return s.n >= suffix.n &&
	       (suffix.n == 0 || memcmp(s.p + s.n - suffix.n, suffix.p, (size_t)suffix.n) == 0);
}

GO_HELPER int strings_IndexByte(GoStr s, char c)
{
	const char *q = s.n ? memchr(s.p, c, (size_t)s.n) : NULL;
	return q ? (int)(q - s.p) : -1;
}

GO_HELPER int strings_LastIndexByte(GoStr s, char c)
{
	for (int i = s.n - 1; i >= 0; i--)
		if (s.p[i] == c)
			return i;
	return -1;
}

GO_HELPER int strings_Index(GoStr s, GoStr sub)
{
	if (sub.n == 0)
		return 0;
	for (int i = 0; i + sub.n <= s.n; i++)
		if (s.p[i] == sub.p[0] && memcmp(s.p + i, sub.p, (size_t)sub.n) == 0)
			return i;
	return -1;
}

GO_HELPER int strings_LastIndex(GoStr s, GoStr sub)
{
	for (int i = s.n - sub.n; i >= 0; i--)
		if (sub.n == 0 || memcmp(s.p + i, sub.p, (size_t)sub.n) == 0)
			return i;
	return -1;
}

GO_HELPER int strings_IndexRune(GoStr s, rune r)
{
	for (int i = 0; i < s.n;) {
		int sz;
		rune c = utf8_DecodeRune(s.p + i, s.n - i, &sz);
		if (c == r) {
			/* Go: an invalid byte matches RuneError only if r == RuneError */
			return i;
		}
		i += sz;
	}
	return -1;
}

GO_HELPER int strings_IndexAny(GoStr s, GoStr chars)
{
	for (int i = 0; i < s.n;) {
		int sz;
		rune c = utf8_DecodeRune(s.p + i, s.n - i, &sz);
		if (strings_IndexRune(chars, c) >= 0)
			return i;
		i += sz;
	}
	return -1;
}

GO_HELPER bool strings_Contains(GoStr s, GoStr sub) { return strings_Index(s, sub) >= 0; }
GO_HELPER bool strings_ContainsAny(GoStr s, GoStr chars) { return strings_IndexAny(s, chars) >= 0; }
GO_HELPER bool strings_ContainsRune(GoStr s, rune r) { return strings_IndexRune(s, r) >= 0; }

GO_HELPER int strings_Count(GoStr s, GoStr sub)
{
	if (sub.n == 0)
		return utf8_RuneCountInString(s) + 1;
	int n = 0;
	for (;;) {
		int i = strings_Index(s, sub);
		if (i < 0)
			return n;
		n++;
		s = gostr_from(s, i + sub.n);
	}
}

GO_HELPER bool strings_Cut(GoStr s, GoStr sep, GoStr *before, GoStr *after)
{
	int i = strings_Index(s, sep);
	if (i >= 0) {
		if (before)
			*before = gostr_sub(s, 0, i);
		if (after)
			*after = gostr_from(s, i + sep.n);
		return true;
	}
	if (before)
		*before = s;
	if (after)
		*after = (GoStr){0};
	return false;
}

GO_HELPER bool strings_CutPrefix(GoStr s, GoStr prefix, GoStr *after)
{
	if (!strings_HasPrefix(s, prefix)) {
		if (after)
			*after = s;
		return false;
	}
	if (after)
		*after = gostr_from(s, prefix.n);
	return true;
}

GO_HELPER bool strings_CutSuffix(GoStr s, GoStr suffix, GoStr *before)
{
	if (!strings_HasSuffix(s, suffix)) {
		if (before)
			*before = s;
		return false;
	}
	if (before)
		*before = gostr_sub(s, 0, s.n - suffix.n);
	return true;
}

GO_HELPER GoStr strings_TrimPrefix(GoStr s, GoStr prefix)
{
	return strings_HasPrefix(s, prefix) ? gostr_from(s, prefix.n) : s;
}

GO_HELPER GoStr strings_TrimSuffix(GoStr s, GoStr suffix)
{
	return strings_HasSuffix(s, suffix) ? gostr_sub(s, 0, s.n - suffix.n) : s;
}

GO_HELPER int strings_IndexFunc(GoStr s, bool (*f)(rune))
{
	for (int i = 0; i < s.n;) {
		int sz;
		rune r = utf8_DecodeRune(s.p + i, s.n - i, &sz);
		if (f(r))
			return i;
		i += sz;
	}
	return -1;
}

GO_HELPER GoStr strings_TrimLeftFunc(GoStr s, bool (*f)(rune))
{
	int start = 0;
	while (start < s.n) {
		int sz;
		rune r = utf8_DecodeRune(s.p + start, s.n - start, &sz);
		if (!f(r))
			break;
		start += sz;
	}
	return gostr_from(s, start);
}

GO_HELPER GoStr strings_TrimRightFunc(GoStr s, bool (*f)(rune))
{
	int end = s.n;
	while (end > 0) {
		int sz;
		rune r = utf8_DecodeLastRuneInString(gostr_sub(s, 0, end), &sz);
		if (!f(r))
			break;
		end -= sz;
	}
	return gostr_sub(s, 0, end);
}

GO_HELPER GoStr strings_TrimSpace(GoStr s) { return strings_TrimRightFunc(strings_TrimLeftFunc(s, unicode_IsSpace), unicode_IsSpace); }

GO_HELPER GoStr strings_TrimLeft(GoStr s, GoStr cutset)
{
	while (s.n > 0) {
		int sz;
		rune r = utf8_DecodeRune(s.p, s.n, &sz);
		if (strings_IndexRune(cutset, r) < 0)
			break;
		s = gostr_from(s, sz);
	}
	return s;
}

GO_HELPER GoStr strings_TrimRight(GoStr s, GoStr cutset)
{
	while (s.n > 0) {
		int sz;
		rune r = utf8_DecodeLastRuneInString(s, &sz);
		if (strings_IndexRune(cutset, r) < 0)
			break;
		s = gostr_sub(s, 0, s.n - sz);
	}
	return s;
}

GO_HELPER GoStr strings_Trim(GoStr s, GoStr cutset) { return strings_TrimRight(strings_TrimLeft(s, cutset), cutset); }

GO_HELPER GoStr strings_Repeat(GoStr s, int count)
{
	if (count <= 0 || s.n == 0)
		return (GoStr){"", 0};
	Buf b = {0};
	for (int i = 0; i < count; i++)
		buf_addg(&b, s);
	return gostr_buf(&b);
}

GO_HELPER GoStrVec strings_Split(GoStr s, GoStr sep)
{
	GoStrVec v = {0};
	if (sep.n == 0) { /* explode into UTF-8 sequences */
		while (s.n > 0) {
			int sz;
			utf8_DecodeRune(s.p, s.n, &sz);
			gostrvec_push(&v, gostr_sub(s, 0, sz));
			s = gostr_from(s, sz);
		}
		return v;
	}
	for (;;) {
		int i = strings_Index(s, sep);
		if (i < 0)
			break;
		gostrvec_push(&v, gostr_sub(s, 0, i));
		s = gostr_from(s, i + sep.n);
	}
	gostrvec_push(&v, s);
	return v;
}

GO_HELPER GoStr strings_Join(const GoStr *elems, int n, GoStr sep)
{
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		if (i > 0)
			buf_addg(&b, sep);
		buf_addg(&b, elems[i]);
	}
	return b.n ? gostr_buf(&b) : (GoStr){"", 0};
}

GO_HELPER GoStr strings_ReplaceAll(GoStr s, GoStr old, GoStr new_)
{
	if (old.n == 0 || strings_Index(s, old) < 0)
		return s;
	Buf b = {0};
	for (;;) {
		int i = strings_Index(s, old);
		if (i < 0)
			break;
		buf_add(&b, s.p, (size_t)i);
		buf_addg(&b, new_);
		s = gostr_from(s, i + old.n);
	}
	buf_addg(&b, s);
	return gostr_buf(&b);
}

GO_HELPER GoStr strings_ToLower(GoStr s)
{
	char *p = xalloc((size_t)s.n + 1);
	for (int i = 0; i < s.n; i++) {
		char c = s.p[i];
		p[i] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
	}
	return (GoStr){p, s.n};
}
