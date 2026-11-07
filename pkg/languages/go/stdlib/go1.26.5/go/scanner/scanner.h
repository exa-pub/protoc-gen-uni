/* Package go/scanner, ported from go1.26.5 src/go/scanner/{scanner,errors}.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO1_26_5_SCANNER_H
#define GO1_26_5_SCANNER_H

/* ErrorHandler: called with a position and an error message for every syntax error. */
typedef void (*scanner_ErrorHandler)(void *ctx, token_Position pos, GoStr msg);

/* Mode */
enum {
	scanner_ScanComments = 1 << 0, /* return comments as COMMENT tokens */
	scanner_dontInsertSemis = 1 << 1, /* do not automatically insert semicolons - for testing only */
};

typedef struct {
	/* immutable state */
	token_File *file; /* source file handle */
	GoStr dir; /* directory portion of file.Name() */
	GoStr src; /* source */
	scanner_ErrorHandler err; /* error reporting; or NULL */
	void *errctx; /* first argument of err */
	int mode; /* scanning mode */

	/* scanning state */
	rune ch; /* current character */
	int offset; /* character offset */
	int rdOffset; /* reading offset (position after current character) */
	int lineOffset; /* current line offset */
	bool insertSemi; /* insert a semicolon before next newline */
	token_Pos nlPos; /* position of newline in preceding comment */
	token_Pos stringEnd; /* end position; defined only for STRING tokens (parser backdoor: Go's
				scannerhooks.StringEnd; read the field directly) */

	/* public state - ok to modify */
	int ErrorCount; /* number of errors encountered */
} scanner_Scanner;

static void scanner_Scanner_Init(scanner_Scanner *s, token_File *file, GoStr src, scanner_ErrorHandler err,
				 void *errctx, int mode);
/* Scan returns the next token; *pos and *lit receive its position and literal string. */
static token_Token scanner_Scanner_Scan(scanner_Scanner *s, token_Pos *pos, GoStr *lit);

typedef struct {
	token_Position Pos;
	GoStr Msg;
} scanner_Error;

/* ErrorList: list of scanner_Error *. */
typedef struct {
	Vec list;
} scanner_ErrorList;

static GoStr scanner_Error_Error(const scanner_Error *e);
static void scanner_ErrorList_Add(scanner_ErrorList *p, token_Position pos, GoStr msg);
static void scanner_ErrorList_Sort(scanner_ErrorList *p);
GO_HELPER void scanner_ErrorList_Reset(scanner_ErrorList *p);
GO_HELPER int scanner_ErrorList_Len(const scanner_ErrorList *p);
GO_HELPER void scanner_ErrorList_RemoveMultiples(scanner_ErrorList *p);
/* Error returns the error message of a non-empty list ("first (and N more errors)"). */
static GoStr scanner_ErrorList_Error(const scanner_ErrorList *p);

#endif
