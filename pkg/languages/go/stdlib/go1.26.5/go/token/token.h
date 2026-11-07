/* Package go/token, ported from go1.26.5 src/go/token/{token,position}.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO1_26_5_TOKEN_H
#define GO1_26_5_TOKEN_H

/* Token: same order as Go (values are compared and indexed). */
typedef enum {
	token_ILLEGAL,
	token_EOF,
	token_COMMENT,

	token_literal_beg,
	token_IDENT,
	token_INT,
	token_FLOAT,
	token_IMAG,
	token_CHAR,
	token_STRING,
	token_literal_end,

	token_operator_beg,
	token_ADD,
	token_SUB,
	token_MUL,
	token_QUO,
	token_REM,

	token_AND,
	token_OR,
	token_XOR,
	token_SHL,
	token_SHR,
	token_AND_NOT,

	token_ADD_ASSIGN,
	token_SUB_ASSIGN,
	token_MUL_ASSIGN,
	token_QUO_ASSIGN,
	token_REM_ASSIGN,

	token_AND_ASSIGN,
	token_OR_ASSIGN,
	token_XOR_ASSIGN,
	token_SHL_ASSIGN,
	token_SHR_ASSIGN,
	token_AND_NOT_ASSIGN,

	token_LAND,
	token_LOR,
	token_ARROW,
	token_INC,
	token_DEC,

	token_EQL,
	token_LSS,
	token_GTR,
	token_ASSIGN,
	token_NOT,

	token_NEQ,
	token_LEQ,
	token_GEQ,
	token_DEFINE,
	token_ELLIPSIS,

	token_LPAREN,
	token_LBRACK,
	token_LBRACE,
	token_COMMA,
	token_PERIOD,

	token_RPAREN,
	token_RBRACK,
	token_RBRACE,
	token_SEMICOLON,
	token_COLON,
	token_operator_end,

	token_keyword_beg,
	token_BREAK,
	token_CASE,
	token_CHAN,
	token_CONST,
	token_CONTINUE,

	token_DEFAULT,
	token_DEFER,
	token_ELSE,
	token_FALLTHROUGH,
	token_FOR,

	token_FUNC,
	token_GO,
	token_GOTO,
	token_IF,
	token_IMPORT,

	token_INTERFACE,
	token_MAP,
	token_PACKAGE,
	token_RANGE,
	token_RETURN,

	token_SELECT,
	token_STRUCT,
	token_SWITCH,
	token_TYPE,
	token_VAR,
	token_keyword_end,

	token_additional_beg,
	token_TILDE,
	token_additional_end,
} token_Token;

enum {
	token_LowestPrec = 0,
	token_UnaryPrec = 6,
	token_HighestPrec = 7,
};

static GoStr token_Token_String(token_Token tok);
static int token_Token_Precedence(token_Token op);
static token_Token token_Lookup(GoStr ident);
GO_HELPER bool token_Token_IsLiteral(token_Token tok);
GO_HELPER bool token_Token_IsOperator(token_Token tok);
GO_HELPER bool token_Token_IsKeyword(token_Token tok);
GO_HELPER bool token_IsExported(GoStr name);
GO_HELPER bool token_IsKeyword(GoStr name);
GO_HELPER bool token_IsIdentifier(GoStr name);

/* Pos: offset in a FileSet; 0 is NoPos. */
typedef int token_Pos;
#define token_NoPos 0
#define token_Pos_IsValid(p) ((p) != token_NoPos)

typedef struct {
	GoStr Filename;
	int Offset; /* starting at 0 */
	int Line; /* starting at 1 */
	int Column; /* starting at 1 (byte count) */
} token_Position;

#define token_Position_IsValid(pos) ((pos)->Line > 0)
GO_HELPER GoStr token_Position_String(token_Position pos);

typedef struct {
	int Offset;
	GoStr Filename;
	int Line, Column;
} token_lineInfo;

typedef struct {
	GoStr name;
	int base, size;
	IntVec lines; /* offset of the first character of each line; lines.v[0] == 0 */
	int ninfos, capinfos;
	token_lineInfo *infos;
} token_File;

typedef struct {
	int base;
	Vec files; /* token_File *, ascending base */
	token_File *last;
} token_FileSet;

GO_HELPER token_FileSet *token_NewFileSet(void);
GO_HELPER int token_FileSet_Base(token_FileSet *s);
GO_HELPER token_File *token_FileSet_AddFile(token_FileSet *s, GoStr filename, int base, int size);
GO_HELPER token_File *token_FileSet_File(token_FileSet *s, token_Pos p);
GO_HELPER token_Position token_FileSet_PositionFor(token_FileSet *s, token_Pos p, bool adjusted);
GO_HELPER token_Position token_FileSet_Position(token_FileSet *s, token_Pos p);

GO_HELPER int token_File_Base(token_File *f);
GO_HELPER int token_File_Size(token_File *f);
GO_HELPER int token_File_LineCount(token_File *f);
GO_HELPER void token_File_AddLine(token_File *f, int offset);
GO_HELPER void token_File_AddLineColumnInfo(token_File *f, int offset, GoStr filename, int line, int column);
GO_HELPER token_Pos token_File_Pos(token_File *f, int offset);
GO_HELPER int token_File_Offset(token_File *f, token_Pos p);
GO_HELPER int token_File_Line(token_File *f, token_Pos p);
GO_HELPER token_Pos token_File_LineStart(token_File *f, int line);
GO_HELPER token_Position token_File_PositionFor(token_File *f, token_Pos p, bool adjusted);
GO_HELPER token_Position token_File_Position(token_File *f, token_Pos p);

#endif
