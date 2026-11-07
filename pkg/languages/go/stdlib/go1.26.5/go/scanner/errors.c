/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Package go/scanner: ported from go1.26.5 src/go/scanner/errors.go.
 * not ported: ErrorList.Err (the C API passes the list itself), PrintError (no io). */
#include "../../internal.h"

/* Error implements the error interface. */
static GoStr scanner_Error_Error(const scanner_Error *e)
{
	if (e->Pos.Filename.n != 0 || token_Position_IsValid(&e->Pos)) {
		/* don't print "<unknown position>" */
		return gostr_cat(gostr_cat(token_Position_String(e->Pos), GOSTR(": ")), e->Msg);
	}
	return e->Msg;
}

/* Add adds an Error with given position and error message to an ErrorList. */
static void scanner_ErrorList_Add(scanner_ErrorList *p, token_Position pos, GoStr msg)
{
	scanner_Error *e = xalloc(sizeof *e);
	e->Pos = pos;
	e->Msg = msg;
	vec_push(&p->list, e);
}

/* Reset resets an ErrorList to no errors. */
GO_HELPER void scanner_ErrorList_Reset(scanner_ErrorList *p) { p->list.n = 0; }

GO_HELPER int scanner_ErrorList_Len(const scanner_ErrorList *p) { return p->list.n; }

static bool scanner_errorLess(const scanner_Error *ei, const scanner_Error *ej)
{
	const token_Position *e = &ei->Pos;
	const token_Position *f = &ej->Pos;
	/* Note that it is not sufficient to simply compare file offsets because the offsets do not
	 * reflect modified line information (through //line comments). */
	if (!gostr_eq(e->Filename, f->Filename))
		return gostr_cmp(e->Filename, f->Filename) < 0;
	if (e->Line != f->Line)
		return e->Line < f->Line;
	if (e->Column != f->Column)
		return e->Column < f->Column;
	return gostr_cmp(ei->Msg, ej->Msg) < 0;
}

/* Sort sorts an ErrorList. Error entries are sorted by position (filename, line, column), then
 * by message. Go uses sort.Sort (not stable); this is a stable merge sort. Elements that compare
 * equal agree in everything Error() and RemoveMultiples look at (they may differ only in
 * Pos.Offset), so the order of equal elements is not observable. */
static void scanner_ErrorList_Sort(scanner_ErrorList *p)
{
	int n = p->list.n;
	if (n < 2)
		return;
	void **a = p->list.v;
	void **tmp = xalloc(sizeof *tmp * (size_t)n);
	for (int width = 1; width < n; width *= 2) {
		for (int lo = 0; lo < n; lo += 2 * width) {
			int mid = lo + width < n ? lo + width : n;
			int hi = lo + 2 * width < n ? lo + 2 * width : n;
			int i = lo, j = mid, k = lo;
			while (i < mid && j < hi) {
				if (scanner_errorLess(a[j], a[i]))
					tmp[k++] = a[j++];
				else
					tmp[k++] = a[i++];
			}
			while (i < mid)
				tmp[k++] = a[i++];
			while (j < hi)
				tmp[k++] = a[j++];
		}
		memcpy(a, tmp, sizeof *a * (size_t)n);
	}
}

/* RemoveMultiples sorts an ErrorList and removes all but the first error per line. */
GO_HELPER void scanner_ErrorList_RemoveMultiples(scanner_ErrorList *p)
{
	scanner_ErrorList_Sort(p);
	token_Position last = {0}; /* initial last.Line is != any legal error line */
	int i = 0;
	for (int k = 0; k < p->list.n; k++) {
		scanner_Error *e = p->list.v[k];
		if (!gostr_eq(e->Pos.Filename, last.Filename) || e->Pos.Line != last.Line) {
			last = e->Pos;
			p->list.v[i++] = e;
		}
	}
	p->list.n = i;
}

/* An ErrorList implements the error interface. */
static GoStr scanner_ErrorList_Error(const scanner_ErrorList *p)
{
	switch (p->list.n) {
	case 0:
		return GOSTR("no errors");
	case 1:
		return scanner_Error_Error(p->list.v[0]);
	}
	Buf b = {0};
	buf_addg(&b, scanner_Error_Error(p->list.v[0]));
	buf_fmt(&b, " (and %d more errors)", p->list.n - 1);
	return gostr_buf(&b);
}
