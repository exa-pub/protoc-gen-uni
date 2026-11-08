/* Package go/token, ported from go1.27.1 src/go/token/token.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../internal.h"

static const char *const token_tokens[token_additional_end] = {
	[token_ILLEGAL] = "ILLEGAL",
	[token_EOF] = "EOF",
	[token_COMMENT] = "COMMENT",

	[token_IDENT] = "IDENT",
	[token_INT] = "INT",
	[token_FLOAT] = "FLOAT",
	[token_IMAG] = "IMAG",
	[token_CHAR] = "CHAR",
	[token_STRING] = "STRING",

	[token_ADD] = "+",
	[token_SUB] = "-",
	[token_MUL] = "*",
	[token_QUO] = "/",
	[token_REM] = "%",

	[token_AND] = "&",
	[token_OR] = "|",
	[token_XOR] = "^",
	[token_SHL] = "<<",
	[token_SHR] = ">>",
	[token_AND_NOT] = "&^",

	[token_ADD_ASSIGN] = "+=",
	[token_SUB_ASSIGN] = "-=",
	[token_MUL_ASSIGN] = "*=",
	[token_QUO_ASSIGN] = "/=",
	[token_REM_ASSIGN] = "%=",

	[token_AND_ASSIGN] = "&=",
	[token_OR_ASSIGN] = "|=",
	[token_XOR_ASSIGN] = "^=",
	[token_SHL_ASSIGN] = "<<=",
	[token_SHR_ASSIGN] = ">>=",
	[token_AND_NOT_ASSIGN] = "&^=",

	[token_LAND] = "&&",
	[token_LOR] = "||",
	[token_ARROW] = "<-",
	[token_INC] = "++",
	[token_DEC] = "--",

	[token_EQL] = "==",
	[token_LSS] = "<",
	[token_GTR] = ">",
	[token_ASSIGN] = "=",
	[token_NOT] = "!",

	[token_NEQ] = "!=",
	[token_LEQ] = "<=",
	[token_GEQ] = ">=",
	[token_DEFINE] = ":=",
	[token_ELLIPSIS] = "...",

	[token_LPAREN] = "(",
	[token_LBRACK] = "[",
	[token_LBRACE] = "{",
	[token_COMMA] = ",",
	[token_PERIOD] = ".",

	[token_RPAREN] = ")",
	[token_RBRACK] = "]",
	[token_RBRACE] = "}",
	[token_SEMICOLON] = ";",
	[token_COLON] = ":",

	[token_BREAK] = "break",
	[token_CASE] = "case",
	[token_CHAN] = "chan",
	[token_CONST] = "const",
	[token_CONTINUE] = "continue",

	[token_DEFAULT] = "default",
	[token_DEFER] = "defer",
	[token_ELSE] = "else",
	[token_FALLTHROUGH] = "fallthrough",
	[token_FOR] = "for",

	[token_FUNC] = "func",
	[token_GO] = "go",
	[token_GOTO] = "goto",
	[token_IF] = "if",
	[token_IMPORT] = "import",

	[token_INTERFACE] = "interface",
	[token_MAP] = "map",
	[token_PACKAGE] = "package",
	[token_RANGE] = "range",
	[token_RETURN] = "return",

	[token_SELECT] = "select",
	[token_STRUCT] = "struct",
	[token_SWITCH] = "switch",
	[token_TYPE] = "type",
	[token_VAR] = "var",

	[token_TILDE] = "~",
};

static GoStr token_Token_String(token_Token tok)
{
	const char *s = NULL;
	if ((int)tok >= 0 && tok < token_additional_end)
		s = token_tokens[tok];
	if (!s)
		return gostr_c(xfmt("token(%d)", (int)tok));
	return gostr_c(s);
}

static int token_Token_Precedence(token_Token op)
{
	switch (op) {
	case token_LOR:
		return 1;
	case token_LAND:
		return 2;
	case token_EQL:
	case token_NEQ:
	case token_LSS:
	case token_LEQ:
	case token_GTR:
	case token_GEQ:
		return 3;
	case token_ADD:
	case token_SUB:
	case token_OR:
	case token_XOR:
		return 4;
	case token_MUL:
	case token_QUO:
	case token_REM:
	case token_SHL:
	case token_SHR:
	case token_AND:
	case token_AND_NOT:
		return 5;
	default:
		return token_LowestPrec;
	}
}

/* Lookup maps an identifier to its keyword token or IDENT (if not a keyword). */
static token_Token token_Lookup(GoStr ident)
{
	/* keywords are 2..11 lowercase ASCII letters */
	if (ident.n < 2 || ident.n > 11 || ident.p[0] < 'b' || ident.p[0] > 'v')
		return token_IDENT;
	for (int i = token_keyword_beg + 1; i < token_keyword_end; i++) {
		const char *k = token_tokens[i];
		if (k[0] == ident.p[0] && gostr_eqc(ident, k))
			return (token_Token)i;
	}
	return token_IDENT;
}

GO_HELPER bool token_Token_IsLiteral(token_Token tok) { return token_literal_beg < tok && tok < token_literal_end; }

GO_HELPER bool token_Token_IsOperator(token_Token tok)
{
	return (token_operator_beg < tok && tok < token_operator_end) || tok == token_TILDE;
}

GO_HELPER bool token_Token_IsKeyword(token_Token tok) { return token_keyword_beg < tok && tok < token_keyword_end; }

GO_HELPER bool token_IsExported(GoStr name) { return unicode_IsUpper(utf8_DecodeRuneInString(name, NULL)); }

GO_HELPER bool token_IsKeyword(GoStr name) { return token_Lookup(name) != token_IDENT; }

GO_HELPER bool token_IsIdentifier(GoStr name)
{
	if (name.n == 0 || token_IsKeyword(name))
		return false;
	for (int i = 0; i < name.n;) {
		int sz;
		rune c = utf8_DecodeRune(name.p + i, name.n - i, &sz);
		if (!unicode_IsLetter(c) && c != '_' && (i == 0 || !unicode_IsDigit(c)))
			return false;
		i += sz;
	}
	return true;
}
