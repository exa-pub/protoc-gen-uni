/* Package unicode/utf8, ported from go1.27.1 src/unicode/utf8/utf8.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO_STD_UTF8_H
#define GO_STD_UTF8_H

#define utf8_RuneError 0xFFFD
#define utf8_RuneSelf 0x80
#define utf8_MaxRune 0x10FFFF
#define utf8_UTFMax 4

/* DecodeRune: returns the first rune of p[0:n] and its width in *size (RuneError, 1 for invalid
 * encodings; RuneError, 0 for empty input). size may be NULL. */
GO_HELPER rune utf8_DecodeRune(const char *p, int n, int *size);
GO_HELPER rune utf8_DecodeRuneInString(GoStr s, int *size);
GO_HELPER rune utf8_DecodeLastRuneInString(GoStr s, int *size);
GO_HELPER int utf8_RuneLen(rune r);
/* EncodeRune writes into p (at least utf8_UTFMax bytes) and returns the number of bytes. */
GO_HELPER int utf8_EncodeRune(char *p, rune r);
GO_HELPER void utf8_AppendRune(Buf *b, rune r);
GO_HELPER int utf8_RuneCountInString(GoStr s);
GO_HELPER int utf8_RuneCount(const char *p, int n);
GO_HELPER bool utf8_ValidString(GoStr s);
GO_HELPER bool utf8_ValidRune(rune r);
GO_HELPER bool utf8_FullRune(const char *p, int n);
GO_HELPER bool utf8_RuneStart(char b);

#endif
