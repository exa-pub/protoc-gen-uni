/* Go strings: GoStr plus the subset of package strings used by the port.
 * Ported from go1.26.5 src/strings (semantics only; Copyright The Go Authors, BSD-3-Clause). */
#ifndef GO1_26_5_STRINGS_H
#define GO1_26_5_STRINGS_H

/* A Go string: n bytes at p (p may be NULL when n == 0); not NUL-terminated. */
typedef struct {
	const char *p;
	int n;
} GoStr;

#define GOSTR(lit) ((GoStr){(lit), (int)sizeof(lit) - 1})
#define GOSTR_FMT(s) (s).n, (s).p /* buf_fmt has no %.*s; use gostr_* or buf_add */

/* Growable slice of strings ([]string). */
typedef struct {
	GoStr *v;
	int n, cap;
} GoStrVec;

/* Growable slice of ints ([]int). */
typedef struct {
	int *v;
	int n, cap;
} IntVec;

GO_HELPER GoStr gostr(const char *p, int n);
GO_HELPER GoStr gostr_c(const char *s); /* NUL-terminated C string */
GO_HELPER GoStr gostr_buf(const Buf *b); /* aliases the buffer contents */
GO_HELPER GoStr gostr_sub(GoStr s, int i, int j); /* s[i:j] */
GO_HELPER GoStr gostr_from(GoStr s, int i); /* s[i:] */
GO_HELPER bool gostr_eq(GoStr a, GoStr b);
GO_HELPER bool gostr_eqc(GoStr a, const char *b);
GO_HELPER int gostr_cmp(GoStr a, GoStr b); /* like strings.Compare */
GO_HELPER char *gostr_cstr(GoStr s); /* NUL-terminated arena copy */
GO_HELPER GoStr gostr_dup(GoStr s); /* arena copy */
GO_HELPER GoStr gostr_cat(GoStr a, GoStr b);
GO_HELPER void buf_addg(Buf *b, GoStr s);

/* Grows a slice backing array of *cap elements of size elem (to min elements if empty, else
 * twice as many) and returns the new array; *cap is updated. */
GO_HELPER void *go_grow(void *p, int *cap, int min, size_t elem);

GO_HELPER void gostrvec_push(GoStrVec *v, GoStr s);
GO_HELPER void intvec_push(IntVec *v, int x);

GO_HELPER bool strings_HasPrefix(GoStr s, GoStr prefix);
GO_HELPER bool strings_HasSuffix(GoStr s, GoStr suffix);
GO_HELPER int strings_IndexByte(GoStr s, char c);
GO_HELPER int strings_LastIndexByte(GoStr s, char c);
GO_HELPER int strings_Index(GoStr s, GoStr sub);
GO_HELPER int strings_LastIndex(GoStr s, GoStr sub);
GO_HELPER int strings_IndexRune(GoStr s, rune r);
GO_HELPER int strings_IndexAny(GoStr s, GoStr chars);
GO_HELPER bool strings_Contains(GoStr s, GoStr sub);
GO_HELPER bool strings_ContainsAny(GoStr s, GoStr chars);
GO_HELPER bool strings_ContainsRune(GoStr s, rune r);
GO_HELPER int strings_Count(GoStr s, GoStr sub);
/* strings.Cut: returns found; before/after may be NULL. */
GO_HELPER bool strings_Cut(GoStr s, GoStr sep, GoStr *before, GoStr *after);
GO_HELPER bool strings_CutPrefix(GoStr s, GoStr prefix, GoStr *after);
GO_HELPER bool strings_CutSuffix(GoStr s, GoStr suffix, GoStr *before);
GO_HELPER GoStr strings_TrimPrefix(GoStr s, GoStr prefix);
GO_HELPER GoStr strings_TrimSuffix(GoStr s, GoStr suffix);
GO_HELPER GoStr strings_TrimSpace(GoStr s);
GO_HELPER GoStr strings_TrimLeft(GoStr s, GoStr cutset);
GO_HELPER GoStr strings_TrimRight(GoStr s, GoStr cutset);
GO_HELPER GoStr strings_Trim(GoStr s, GoStr cutset);
GO_HELPER GoStr strings_TrimLeftFunc(GoStr s, bool (*f)(rune));
GO_HELPER GoStr strings_TrimRightFunc(GoStr s, bool (*f)(rune));
GO_HELPER int strings_IndexFunc(GoStr s, bool (*f)(rune));
GO_HELPER GoStr strings_Repeat(GoStr s, int count);
GO_HELPER GoStrVec strings_Split(GoStr s, GoStr sep);
GO_HELPER GoStr strings_Join(const GoStr *elems, int n, GoStr sep);
GO_HELPER GoStr strings_ReplaceAll(GoStr s, GoStr old, GoStr new_);
GO_HELPER GoStr strings_ToLower(GoStr s); /* ASCII-only callers */

#endif
