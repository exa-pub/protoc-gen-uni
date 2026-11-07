/* Package strconv, ported from go1.26.5 src/strconv/quote.go (Quote, Unquote, IsPrint,
 * IsGraphic). Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO1_26_5_STRCONV_H
#define GO1_26_5_STRCONV_H

GO_HELPER GoStr strconv_Quote(GoStr s);
/* Unquote: returns false (Go: ErrSyntax) if s is not a valid quoted string; *out may be NULL. */
GO_HELPER bool strconv_Unquote(GoStr s, GoStr *out);
GO_HELPER bool strconv_IsPrint(rune r);
GO_HELPER bool strconv_IsGraphic(rune r);
GO_HELPER void strconv_AppendQuote(Buf *dst, GoStr s);
GO_HELPER GoStr strconv_QuoteToASCII(GoStr s);
GO_HELPER GoStr strconv_QuoteToGraphic(GoStr s);
GO_HELPER GoStr strconv_QuoteRune(rune r);
GO_HELPER GoStr strconv_QuoteRuneToASCII(rune r);
GO_HELPER GoStr strconv_QuoteRuneToGraphic(rune r);
GO_HELPER bool strconv_CanBackquote(GoStr s);
/* UnquoteChar: returns false (Go: ErrSyntax) on a syntax error. */
GO_HELPER bool strconv_UnquoteChar(GoStr s, char quote, rune *value, bool *multibyte, GoStr *tail);
GO_HELPER bool strconv_QuotedPrefix(GoStr s, GoStr *out);
/* ftoa.c: FormatFloat for the e, E, f, g, G formats */
GO_HELPER void strconv_AppendFloat(Buf *dst, double val, char fmt, int prec, int bitSize);

#endif
