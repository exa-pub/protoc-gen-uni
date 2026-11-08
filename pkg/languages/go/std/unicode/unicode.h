/* Package unicode, ported from go1.27.1 src/unicode (Unicode 17.0.0 tables of Go 1.27.1; older
 * releases: go_std_release.unicode; the subset of predicates and case mappings used by the port). Copyright 2009 The Go Authors. All rights reserved.
 * BSD-3-Clause (see LICENSE in Go). */
#ifndef GO_STD_UNICODE_H
#define GO_STD_UNICODE_H

#define unicode_MaxRune 0x10FFFF
#define unicode_ReplacementChar 0xFFFD
#define unicode_MaxASCII 0x7F
#define unicode_MaxLatin1 0xFF

/* Indices into the Delta arrays inside CaseRanges for case mapping. */
enum { unicode_UpperCase, unicode_LowerCase };

GO_HELPER bool unicode_IsLetter(rune r);
GO_HELPER bool unicode_IsDigit(rune r);
GO_HELPER bool unicode_IsUpper(rune r);
GO_HELPER bool unicode_IsLower(rune r);
GO_HELPER bool unicode_IsSpace(rune r);
GO_HELPER bool unicode_IsPunct(rune r);
GO_HELPER bool unicode_IsGraphic(rune r);
GO_HELPER bool unicode_IsPrint(rune r);
GO_HELPER bool unicode_IsControl(rune r);
GO_HELPER bool unicode_IsMark(rune r);
GO_HELPER bool unicode_IsNumber(rune r);
GO_HELPER bool unicode_IsSymbol(rune r);
GO_HELPER rune unicode_ToUpper(rune r);
GO_HELPER rune unicode_ToLower(rune r);

#endif
