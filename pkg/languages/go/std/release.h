/* What an older Go release does differently from the newest one this port implements (Go 1.27.1).
 * Shared by the unity build go_std.c and the versioned parts of an older release (go1.26.5/,
 * library go_std_go1_26_5), which a generator links and selects with go_std_use() (go_std.h).
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO_STD_RELEASE_H
#define GO_STD_RELEASE_H

#include <stdbool.h>
#include <stdint.h>

/* Runes as sorted, disjoint [v[2i], v[2i+1]] pairs (n pairs). */
typedef struct {
	const uint32_t *v;
	int n;
} go_std_runeRanges;

/* unicode predicates, in the order of go_std_release.unicode */
enum {
	GO_STD_IsLetter, GO_STD_IsDigit, GO_STD_IsUpper, GO_STD_IsLower, GO_STD_IsSpace, GO_STD_IsPunct,
	GO_STD_IsGraphic, GO_STD_IsPrint, GO_STD_IsMark, GO_STD_IsNumber, GO_STD_IsSymbol, GO_STD_UNICODE_N
};

struct go_std_release {
	const char *version; /* "go1.26.5" */
	int minor;           /* 26: the Go 1.x language/library release */
	/* The runes above U+00FF for which the unicode predicate (Unicode tables of the release)
	 * answers differently from the newest one. */
	go_std_runeRanges unicode[GO_STD_UNICODE_N];
	/* The same for strconv.IsPrint and strconv's isGraphic list. */
	go_std_runeRanges strconv_isprint, strconv_isgraphic;
	/* go/printer exprList: the geometric mean of the key sizes with math.Log and math.Exp
	 * (before 1.27; 1.27 uses log2ish/exp2ish of go/printer/math.go). */
	double (*log)(double);
	double (*exp)(double);
};

#endif
