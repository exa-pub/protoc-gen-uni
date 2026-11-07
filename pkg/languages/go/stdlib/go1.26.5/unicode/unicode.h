/* Package unicode, ported from go1.26.5 src/unicode (Unicode 15.0.0 tables of Go 1.26.5; the
 * subset of predicates used by the port). Copyright 2009 The Go Authors. All rights reserved.
 * BSD-3-Clause (see LICENSE in Go). */
#ifndef GO1_26_5_UNICODE_H
#define GO1_26_5_UNICODE_H

#define unicode_MaxRune 0x10FFFF
#define unicode_ReplacementChar 0xFFFD
#define unicode_MaxASCII 0x7F
#define unicode_MaxLatin1 0xFF

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

#endif
