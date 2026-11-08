/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.27.1 src/go/parser/parser.go.
 *
 * Package parser implements a parser for Go source files. The parser accepts a larger language
 * than is syntactically permitted by the Go spec, for simplicity, and for improved robustness in
 * the presence of syntax errors.
 *
 * not ported: Trace mode (printTrace/trace/un), debugging output only.
 * not ported: object resolution (resolver.go): File.Scope/Unresolved/Ident.Obj are not in our
 *   AST; the parser always behaves as with SkipObjectResolution.
 * not ported: File.GoVersion from //go:build lines (needs go/build/constraint.GoVersion); the
 *   printer does not use it.
 *
 * Go's panic(bailout{}) is a longjmp to parser_parser.bailout (set in ParseFile). */
#include "../../internal.h"

/* The parser structure holds the parser's internal state. */
typedef struct {
	token_File *file;
	scanner_ErrorList errors;
	scanner_Scanner scanner;

	/* Tracing/debugging */
	int mode; /* parsing mode */
	/* not ported: trace, indent */

	/* Comments */
	Vec comments; /* ast_CommentGroup * */
	ast_CommentGroup *leadComment; /* last lead comment */
	ast_CommentGroup *lineComment; /* last line comment */
	bool top; /* in top of file (before package clause) */
	/* not ported: goVersion (minimum Go version found in //go:build comment) */

	/* Next token */
	token_Pos pos; /* token position */
	token_Token tok; /* one token look-ahead */
	GoStr lit; /* token literal */
	token_Pos stringEnd; /* position immediately after token; STRING only */

	/* Error recovery (used to limit the number of calls to parser.advance w/o making scanning
	 * progress - avoids potential endless loops across multiple parser functions during error
	 * recovery) */
	token_Pos syncPos; /* last synchronization position */
	int syncCnt; /* number of parser.advance calls without progress */

	/* Non-syntactic parser control */
	int exprLev; /* < 0: in control clause, >= 0: in expression */
	bool inRhs; /* if set, the parser is parsing a rhs expression */

	Vec imports; /* ast_ImportSpec *: list of imports */

	/* nestLev is used to track and limit the recursion depth during parsing. */
	int nestLev;

	/* C port: target of panic(bailout{}), and the C stack position at the start of parsing
	 * (see incNestLev). */
	jmp_buf bailout;
	uintptr_t stackBase;
} parser_parser;

static void parser_next(parser_parser *p);
static ast_Expr *parser_parseExpr(parser_parser *p);
static ast_Expr *parser_parseRhs(parser_parser *p);
static ast_Expr *parser_parseType(parser_parser *p);
static ast_Expr *parser_tryIdentOrType(parser_parser *p);
static ast_Expr *parser_parseTypeInstance(parser_parser *p, ast_Expr *typ);
static ast_ArrayType *parser_parseArrayType(parser_parser *p, token_Pos lbrack, ast_Expr *len_);
static ast_FuncType *parser_parseFuncType(parser_parser *p);
static ast_FieldList *parser_parseParameters(parser_parser *p, bool result);
static Vec parser_parseParameterList(parser_parser *p, ast_Ident *name0, ast_Expr *typ0, token_Token closing,
				     bool dddok);
static ast_Expr *parser_embeddedElem(parser_parser *p, ast_Expr *x);
static ast_Expr *parser_embeddedTerm(parser_parser *p);
static ast_BlockStmt *parser_parseBlockStmt(parser_parser *p);
static ast_Expr *parser_parseLiteralValue(parser_parser *p, ast_Expr *typ);
static ast_Expr *parser_parsePrimaryExpr(parser_parser *p, ast_Expr *x);
static ast_Expr *parser_parseUnaryExpr(parser_parser *p);
static ast_Expr *parser_parseBinaryExpr(parser_parser *p, ast_Expr *x, int prec1);
static ast_Stmt *parser_parseStmt(parser_parser *p);
static ast_Decl *parser_parseDecl(parser_parser *p, const bool *sync);
static ast_Expr *parser_packIndexExpr(ast_Expr *x, token_Pos lbrack, Vec exprs, token_Pos rbrack);

/* Scanner error handler: eh := func(pos token.Position, msg string) { p.errors.Add(pos, msg) } */
static void parser_eh(void *ctx, token_Position pos, GoStr msg)
{
	parser_parser *p = ctx;
	scanner_ErrorList_Add(&p->errors, pos, msg);
}

static void parser_init(parser_parser *p, token_File *file, GoStr src, int mode)
{
	p->file = file;
	scanner_Scanner_Init(&p->scanner, p->file, src, parser_eh, p, scanner_ScanComments);

	p->top = true;
	p->mode = mode;
	parser_next(p);
}

/* ----------------------------------------------------------------------------
 * Parsing support */

/* not ported: printTrace, trace, un (Trace mode) */

/* maxNestLev is the deepest we're willing to recurse during parsing */
enum { parser_maxNestLev = 100000 };

/* C port: Go's goroutine stacks grow on demand, a C stack does not. Besides Go's maxNestLev
 * limit, recursion is cut off when the parser has used this many bytes of C stack (deeply nested
 * input only; real code is far below). The error is the same as for maxNestLev. */
#ifndef PARSER_STACK_BUDGET
#define PARSER_STACK_BUDGET (512 * 1024)
#endif

static uintptr_t parser_stackPos(void)
{
#if defined(__GNUC__)
	return (uintptr_t)__builtin_frame_address(0);
#else
	volatile char c = 0;
	return (uintptr_t)&c;
#endif
}

static void parser_error(parser_parser *p, token_Pos pos, GoStr msg);

static void parser_incNestLev(parser_parser *p)
{
	p->nestLev++;
	uintptr_t sp = parser_stackPos();
	uintptr_t used = sp < p->stackBase ? p->stackBase - sp : sp - p->stackBase;
	if (p->nestLev > parser_maxNestLev || used > PARSER_STACK_BUDGET) {
		parser_error(p, p->pos, GOSTR("exceeded max nesting depth"));
		longjmp(p->bailout, 1);
	}
}

/* decNestLev is used to track nesting depth during parsing to prevent stack exhaustion. It is
 * used along with incNestLev in a similar fashion to how un and trace are used. */
static void parser_decNestLev(parser_parser *p) { p->nestLev--; }

/* Advance to the next token. */
static void parser_next0(parser_parser *p)
{
	/* not ported: trace output of the previous token */
	for (;;) {
		p->tok = scanner_Scanner_Scan(&p->scanner, &p->pos, &p->lit);
		if (p->tok == token_COMMENT) {
			/* not ported: if p.top && strings.HasPrefix(p.lit, "//go:build") { p.goVersion =
			 * constraint.GoVersion(...) } (File.GoVersion is not used by the printer) */
			if ((p->mode & parser_ParseComments) == 0)
				continue;
		} else {
			if (p->tok == token_STRING)
				p->stringEnd = p->scanner.stringEnd; /* scannerhooks.StringEnd */

			/* Found a non-comment; top of file is over. */
			p->top = false;
		}
		break;
	}
}

/* lineFor returns the line of pos, ignoring line directive adjustments. */
static int parser_lineFor(parser_parser *p, token_Pos pos) { return token_File_PositionFor(p->file, pos, false).Line; }

/* Consume a comment and return it and the line on which it ends. */
static ast_Comment *parser_consumeComment(parser_parser *p, int *endline)
{
	/* General (block) comments may end on a different line than where they start. Scan the comment
	 * for '\n' chars and adjust endline accordingly. */
	*endline = parser_lineFor(p, p->pos);
	if (p->lit.p[1] == '*') {
		for (int i = 0; i < p->lit.n; i++) {
			if (p->lit.p[i] == '\n')
				(*endline)++;
		}
	}

	ast_Comment *comment = AST_NEW(Comment);
	comment->Slash = p->pos;
	comment->Text = p->lit;
	parser_next0(p);

	return comment;
}

/* Consume a group of adjacent comments, add it to the parser's comments list, and return it
 * together with the line at which the last comment in the group ends. A non-comment token or n
 * empty lines terminate a comment group. */
static ast_CommentGroup *parser_consumeCommentGroup(parser_parser *p, int n, int *endline)
{
	Vec list = {0};
	*endline = parser_lineFor(p, p->pos);
	while (p->tok == token_COMMENT && parser_lineFor(p, p->pos) <= *endline + n) {
		ast_Comment *comment = parser_consumeComment(p, endline);
		vec_push(&list, comment);
	}

	/* add comment group to the comments list */
	ast_CommentGroup *comments = AST_NEW(CommentGroup);
	comments->List = list;
	vec_push(&p->comments, comments);

	return comments;
}

/* Advance to the next non-comment token. In the process, collect any comment groups
 * encountered, and remember the last lead and line comments.
 *
 * A lead comment is a comment group that starts and ends in a line without any other tokens and
 * that is followed by a non-comment token on the line immediately after the comment group.
 *
 * A line comment is a comment group that follows a non-comment token on the same line, and that
 * has no tokens after it on the line where it ends.
 *
 * Lead and line comments may be considered documentation that is stored in the AST. */
static void parser_next(parser_parser *p)
{
	p->leadComment = NULL;
	p->lineComment = NULL;
	token_Pos prev = p->pos;
	parser_next0(p);

	if (p->tok == token_COMMENT) {
		ast_CommentGroup *comment = NULL;
		int endline;

		if (parser_lineFor(p, p->pos) == parser_lineFor(p, prev)) {
			/* The comment is on same line as the previous token; it cannot be a lead
			 * comment but may be a line comment. */
			comment = parser_consumeCommentGroup(p, 0, &endline);
			if (parser_lineFor(p, p->pos) != endline || p->tok == token_SEMICOLON || p->tok == token_EOF) {
				/* The next token is on a different line, thus the last comment group
				 * is a line comment. */
				p->lineComment = comment;
			}
		}

		/* consume successor comments, if any */
		endline = -1;
		while (p->tok == token_COMMENT)
			comment = parser_consumeCommentGroup(p, 1, &endline);

		if (endline + 1 == parser_lineFor(p, p->pos)) {
			/* The next token is following on the line immediately after the comment group,
			 * thus the last comment group is a lead comment. */
			p->leadComment = comment;
		}
	}
}

/* A bailout panic is raised to indicate early termination: longjmp(p->bailout).
 * not ported: bailout.pos/msg (only populated when bailing out of object resolution). */

static void parser_error(parser_parser *p, token_Pos pos, GoStr msg)
{
	token_Position epos = token_File_Position(p->file, pos);

	/* If AllErrors is not set, discard errors reported on the same line as the last recorded
	 * error and stop parsing if there are more than 10 errors. */
	if ((p->mode & parser_AllErrors) == 0) {
		int n = p->errors.list.n;
		if (n > 0 && VEC_AT(p->errors.list, scanner_Error, n - 1)->Pos.Line == epos.Line)
			return; /* discard - likely a spurious error */
		if (n > 10)
			longjmp(p->bailout, 1);
	}

	scanner_ErrorList_Add(&p->errors, epos, msg);
}

static void parser_errorExpected(parser_parser *p, token_Pos pos, GoStr msg)
{
	msg = gostr_cat(GOSTR("expected "), msg);
	if (pos == p->pos) {
		/* the error happened at the current position; make the error message more specific */
		if (p->tok == token_SEMICOLON && gostr_eqc(p->lit, "\n")) {
			msg = gostr_cat(msg, GOSTR(", found newline"));
		} else if (token_Token_IsLiteral(p->tok)) {
			/* print 123 rather than 'INT', etc. */
			msg = gostr_cat(gostr_cat(msg, GOSTR(", found ")), p->lit);
		} else {
			msg = gostr_cat(gostr_cat(gostr_cat(msg, GOSTR(", found '")), token_Token_String(p->tok)),
					GOSTR("'"));
		}
	}
	parser_error(p, pos, msg);
}

/* "'" + tok.String() + "'" */
static GoStr parser_quoteTok(token_Token tok)
{
	return gostr_cat(gostr_cat(GOSTR("'"), token_Token_String(tok)), GOSTR("'"));
}

static token_Pos parser_expect(parser_parser *p, token_Token tok)
{
	token_Pos pos = p->pos;
	if (p->tok != tok)
		parser_errorExpected(p, pos, parser_quoteTok(tok));
	parser_next(p); /* make progress */
	return pos;
}

/* expect2 is like expect, but it returns an invalid position if the expected token is not
 * found. */
static token_Pos parser_expect2(parser_parser *p, token_Token tok)
{
	token_Pos pos = token_NoPos;
	if (p->tok == tok)
		pos = p->pos;
	else
		parser_errorExpected(p, p->pos, parser_quoteTok(tok));
	parser_next(p); /* make progress */
	return pos;
}

/* expectClosing is like expect but provides a better error message for the common case of a
 * missing comma before a newline. */
static token_Pos parser_expectClosing(parser_parser *p, token_Token tok, GoStr context)
{
	if (p->tok != tok && p->tok == token_SEMICOLON && gostr_eqc(p->lit, "\n")) {
		parser_error(p, p->pos, gostr_cat(GOSTR("missing ',' before newline in "), context));
		parser_next(p);
	}
	return parser_expect(p, tok);
}

static void parser_advance(parser_parser *p, const bool *to);
static const bool parser_stmtStart[token_additional_end];

/* expectSemi consumes a semicolon and returns the applicable line comment. */
static ast_CommentGroup *parser_expectSemi(parser_parser *p)
{
	ast_CommentGroup *comment;
	switch (p->tok) {
	case token_RPAREN:
	case token_RBRACE:
		return NULL; /* semicolon is optional before a closing ')' or '}' */
	case token_COMMA:
		/* permit a ',' instead of a ';' but complain */
		parser_errorExpected(p, p->pos, GOSTR("';'"));
		/* fallthrough */
	case token_SEMICOLON:
		if (gostr_eqc(p->lit, ";")) {
			/* explicit semicolon */
			parser_next(p);
			comment = p->lineComment; /* use following comments */
		} else {
			/* artificial semicolon */
			comment = p->lineComment; /* use preceding comments */
			parser_next(p);
		}
		return comment;
	default:
		parser_errorExpected(p, p->pos, GOSTR("';'"));
		parser_advance(p, parser_stmtStart);
		return NULL;
	}
}

static bool parser_atComma(parser_parser *p, GoStr context, token_Token follow)
{
	if (p->tok == token_COMMA)
		return true;
	if (p->tok != follow) {
		GoStr msg = GOSTR("missing ','");
		if (p->tok == token_SEMICOLON && gostr_eqc(p->lit, "\n"))
			msg = gostr_cat(msg, GOSTR(" before newline"));
		parser_error(p, p->pos, gostr_cat(gostr_cat(msg, GOSTR(" in ")), context));
		return true; /* "insert" comma and continue */
	}
	return false;
}

static void parser_assert(bool cond, const char *msg)
{
	if (!cond)
		fail("go/parser internal error: %s", msg);
}

/* advance consumes tokens until the current token p.tok is in the 'to' set, or token.EOF. For
 * error recovery. */
static void parser_advance(parser_parser *p, const bool *to)
{
	for (; p->tok != token_EOF; parser_next(p)) {
		if (to[p->tok]) {
			/* Return only if parser made some progress since last sync or if it has not
			 * reached 10 advance calls without progress. Otherwise consume at least one
			 * token to avoid an endless parser loop (it is possible that both
			 * parseOperand and parseStmt call advance and correctly do not advance, thus
			 * the need for the invocation limit p.syncCnt). */
			if (p->pos == p->syncPos && p->syncCnt < 10) {
				p->syncCnt++;
				return;
			}
			if (p->pos > p->syncPos) {
				p->syncPos = p->pos;
				p->syncCnt = 0;
				return;
			}
			/* Reaching here indicates a parser bug, likely an incorrect token list in this
			 * function, but it only leads to skipping of possibly correct code if a previous
			 * error is present, and thus is preferred over a non-terminating parse. */
		}
	}
}

static const bool parser_stmtStart[token_additional_end] = {
	[token_BREAK] = true, [token_CONST] = true, [token_CONTINUE] = true, [token_DEFER] = true,
	[token_FALLTHROUGH] = true, [token_FOR] = true, [token_GO] = true, [token_GOTO] = true,
	[token_IF] = true, [token_RETURN] = true, [token_SELECT] = true, [token_SWITCH] = true,
	[token_TYPE] = true, [token_VAR] = true,
};

static const bool parser_declStart[token_additional_end] = {
	[token_IMPORT] = true,
	[token_CONST] = true,
	[token_TYPE] = true,
	[token_VAR] = true,
};

static const bool parser_exprEnd[token_additional_end] = {
	[token_COMMA] = true,  [token_COLON] = true,  [token_SEMICOLON] = true,
	[token_RPAREN] = true, [token_RBRACK] = true, [token_RBRACE] = true,
};

/* ----------------------------------------------------------------------------
 * Identifiers */

static ast_Ident *parser_parseIdent(parser_parser *p)
{
	token_Pos pos = p->pos;
	GoStr name = GOSTR("_");
	if (p->tok == token_IDENT) {
		name = p->lit;
		parser_next(p);
	} else {
		parser_expect(p, token_IDENT); /* use expect() error handling */
	}
	ast_Ident *id = AST_NEW(Ident);
	id->NamePos = pos;
	id->Name = name;
	return id;
}

static Vec parser_parseIdentList(parser_parser *p)
{
	Vec list = {0};
	vec_push(&list, parser_parseIdent(p));
	while (p->tok == token_COMMA) {
		parser_next(p);
		vec_push(&list, parser_parseIdent(p));
	}
	return list;
}

/* ----------------------------------------------------------------------------
 * Common productions */

/* If lhs is set, result list elements which are identifiers are not resolved. */
static Vec parser_parseExprList(parser_parser *p)
{
	Vec list = {0};
	vec_push(&list, parser_parseExpr(p));
	while (p->tok == token_COMMA) {
		parser_next(p);
		vec_push(&list, parser_parseExpr(p));
	}
	return list;
}

static Vec parser_parseList(parser_parser *p, bool inRhs)
{
	bool old = p->inRhs;
	p->inRhs = inRhs;
	Vec list = parser_parseExprList(p);
	p->inRhs = old;
	return list;
}

/* ----------------------------------------------------------------------------
 * Types */

static ast_BadExpr *parser_badExpr(token_Pos from, token_Pos to)
{
	ast_BadExpr *x = AST_NEW(BadExpr);
	x->From = from;
	x->To = to;
	return x;
}

static ast_Expr *parser_parseType(parser_parser *p)
{
	ast_Expr *typ = parser_tryIdentOrType(p);

	if (typ == NULL) {
		token_Pos pos = p->pos;
		parser_errorExpected(p, pos, GOSTR("type"));
		parser_advance(p, parser_exprEnd);
		return (ast_Expr *)parser_badExpr(pos, p->pos);
	}

	return typ;
}

static ast_Expr *parser_parseTypeName(parser_parser *p, ast_Ident *ident);

static ast_Expr *parser_parseQualifiedIdent(parser_parser *p, ast_Ident *ident)
{
	ast_Expr *typ = parser_parseTypeName(p, ident);
	if (p->tok == token_LBRACK)
		typ = parser_parseTypeInstance(p, typ);

	return typ;
}

/* If the result is an identifier, it is not resolved. */
static ast_Expr *parser_parseTypeName(parser_parser *p, ast_Ident *ident)
{
	if (ident == NULL)
		ident = parser_parseIdent(p);

	if (p->tok == token_PERIOD) {
		/* ident is a package name */
		parser_next(p);
		ast_Ident *sel = parser_parseIdent(p);
		ast_SelectorExpr *x = AST_NEW(SelectorExpr);
		x->X = (ast_Expr *)ident;
		x->Sel = sel;
		return (ast_Expr *)x;
	}

	return (ast_Expr *)ident;
}

/* "[" has already been consumed, and lbrack is its position. If len != nil it is the already
 * consumed array length. */
static ast_ArrayType *parser_parseArrayType(parser_parser *p, token_Pos lbrack, ast_Expr *len_)
{
	if (len_ == NULL) {
		p->exprLev++;
		/* always permit ellipsis for more fault-tolerant parsing */
		if (p->tok == token_ELLIPSIS) {
			ast_Ellipsis *e = AST_NEW(Ellipsis);
			e->Ellipsis = p->pos;
			len_ = (ast_Expr *)e;
			parser_next(p);
		} else if (p->tok != token_RBRACK) {
			len_ = parser_parseRhs(p);
		}
		p->exprLev--;
	}
	if (p->tok == token_COMMA) {
		/* Trailing commas are accepted in type parameter lists but not in array type
		 * declarations. Accept for better error handling but complain. */
		parser_error(p, p->pos, GOSTR("unexpected comma; expecting ]"));
		parser_next(p);
	}
	parser_expect(p, token_RBRACK);
	ast_Expr *elt = parser_parseType(p);
	ast_ArrayType *t = AST_NEW(ArrayType);
	t->Lbrack = lbrack;
	t->Len = len_;
	t->Elt = elt;
	return t;
}

/* Returns the type; *name receives x or NULL (Go: (*ast.Ident, ast.Expr)). */
static ast_Expr *parser_parseArrayFieldOrTypeInstance(parser_parser *p, ast_Ident *x, ast_Ident **name)
{
	token_Pos lbrack = parser_expect(p, token_LBRACK);
	token_Pos trailingComma = token_NoPos; /* if valid, the position of a trailing comma preceding the ']' */
	Vec args = {0};
	if (p->tok != token_RBRACK) {
		p->exprLev++;
		vec_push(&args, parser_parseRhs(p));
		while (p->tok == token_COMMA) {
			token_Pos comma = p->pos;
			parser_next(p);
			if (p->tok == token_RBRACK) {
				trailingComma = comma;
				break;
			}
			vec_push(&args, parser_parseRhs(p));
		}
		p->exprLev--;
	}
	token_Pos rbrack = parser_expect(p, token_RBRACK);

	if (args.n == 0) {
		/* x []E */
		ast_Expr *elt = parser_parseType(p);
		ast_ArrayType *t = AST_NEW(ArrayType);
		t->Lbrack = lbrack;
		t->Elt = elt;
		*name = x;
		return (ast_Expr *)t;
	}

	/* x [P]E or x[P] */
	if (args.n == 1) {
		ast_Expr *elt = parser_tryIdentOrType(p);
		if (elt != NULL) {
			/* x [P]E */
			if (token_Pos_IsValid(trailingComma)) {
				/* Trailing commas are invalid in array type fields. */
				parser_error(p, trailingComma, GOSTR("unexpected comma; expecting ]"));
			}
			ast_ArrayType *t = AST_NEW(ArrayType);
			t->Lbrack = lbrack;
			t->Len = args.v[0];
			t->Elt = elt;
			*name = x;
			return (ast_Expr *)t;
		}
	}

	/* x[P], x[P1, P2], ... */
	*name = NULL;
	return parser_packIndexExpr((ast_Expr *)x, lbrack, args, rbrack);
}

static ast_Field *parser_parseFieldDecl(parser_parser *p)
{
	ast_CommentGroup *doc = p->leadComment;

	Vec names = {0};
	ast_Expr *typ = NULL;
	switch (p->tok) {
	case token_IDENT: {
		ast_Ident *name = parser_parseIdent(p);
		if (p->tok == token_PERIOD || p->tok == token_STRING || p->tok == token_SEMICOLON ||
		    p->tok == token_RBRACE) {
			/* embedded type */
			typ = (ast_Expr *)name;
			if (p->tok == token_PERIOD)
				typ = parser_parseQualifiedIdent(p, name);
		} else {
			/* name1, name2, ... T */
			vec_push(&names, name);
			while (p->tok == token_COMMA) {
				parser_next(p);
				vec_push(&names, parser_parseIdent(p));
			}
			/* Careful dance: We don't know if we have an embedded instantiated type
			 * T[P1, P2, ...] or a field T of array type []E or [P]E. */
			if (names.n == 1 && p->tok == token_LBRACK) {
				typ = parser_parseArrayFieldOrTypeInstance(p, name, &name);
				if (name == NULL)
					names = (Vec){0};
			} else {
				/* T P */
				typ = parser_parseType(p);
			}
		}
		break;
	}
	case token_MUL: {
		token_Pos star = p->pos;
		parser_next(p);
		if (p->tok == token_LPAREN) {
			/* *(T) */
			parser_error(p, p->pos, GOSTR("cannot parenthesize embedded type"));
			parser_next(p);
			typ = parser_parseQualifiedIdent(p, NULL);
			/* expect closing ')' but no need to complain if missing */
			if (p->tok == token_RPAREN)
				parser_next(p);
		} else {
			/* *T */
			typ = parser_parseQualifiedIdent(p, NULL);
		}
		ast_StarExpr *s = AST_NEW(StarExpr);
		s->Star = star;
		s->X = typ;
		typ = (ast_Expr *)s;
		break;
	}
	case token_LPAREN:
		parser_error(p, p->pos, GOSTR("cannot parenthesize embedded type"));
		parser_next(p);
		if (p->tok == token_MUL) {
			/* (*T) */
			token_Pos star = p->pos;
			parser_next(p);
			ast_StarExpr *s = AST_NEW(StarExpr);
			s->Star = star;
			s->X = parser_parseQualifiedIdent(p, NULL);
			typ = (ast_Expr *)s;
		} else {
			/* (T) */
			typ = parser_parseQualifiedIdent(p, NULL);
		}
		/* expect closing ')' but no need to complain if missing */
		if (p->tok == token_RPAREN)
			parser_next(p);
		break;
	default: {
		token_Pos pos = p->pos;
		parser_errorExpected(p, pos, GOSTR("field name or embedded type"));
		parser_advance(p, parser_exprEnd);
		typ = (ast_Expr *)parser_badExpr(pos, p->pos);
	}
	}

	ast_BasicLit *tag = NULL;
	if (p->tok == token_STRING) {
		tag = AST_NEW(BasicLit);
		tag->ValuePos = p->pos;
		tag->ValueEnd = p->stringEnd;
		tag->Kind = p->tok;
		tag->Value = p->lit;
		parser_next(p);
	}

	ast_CommentGroup *comment = parser_expectSemi(p);

	ast_Field *field = AST_NEW(Field);
	field->Doc = doc;
	field->Names = names;
	field->Type = typ;
	field->Tag = tag;
	field->Comment = comment;
	return field;
}

static ast_StructType *parser_parseStructType(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_STRUCT);
	token_Pos lbrace = parser_expect(p, token_LBRACE);
	Vec list = {0};
	while (p->tok == token_IDENT || p->tok == token_MUL || p->tok == token_LPAREN) {
		/* a field declaration cannot start with a '(' but we accept it here for more robust
		 * parsing and better error messages (parseFieldDecl will check and complain if
		 * necessary) */
		vec_push(&list, parser_parseFieldDecl(p));
	}
	token_Pos rbrace = parser_expect(p, token_RBRACE);

	ast_StructType *t = AST_NEW(StructType);
	t->Struct = pos;
	t->Fields = AST_NEW(FieldList);
	t->Fields->Opening = lbrace;
	t->Fields->List = list;
	t->Fields->Closing = rbrace;
	return t;
}

static ast_StarExpr *parser_parsePointerType(parser_parser *p)
{
	token_Pos star = parser_expect(p, token_MUL);
	ast_Expr *base = parser_parseType(p);

	ast_StarExpr *s = AST_NEW(StarExpr);
	s->Star = star;
	s->X = base;
	return s;
}

static ast_Ellipsis *parser_parseDotsType(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_ELLIPSIS);
	ast_Expr *elt = parser_parseType(p);

	ast_Ellipsis *e = AST_NEW(Ellipsis);
	e->Ellipsis = pos;
	e->Elt = elt;
	return e;
}

typedef struct {
	ast_Ident *name;
	ast_Expr *typ;
} parser_field;

typedef struct {
	parser_field *v;
	int n, cap;
} parser_fieldVec;

static void parser_fieldVec_push(parser_fieldVec *v, parser_field f)
{
	if (v->n == v->cap) {
		v->v = go_grow(v->v, &v->cap, 8, sizeof *v->v);
	}
	v->v[v->n++] = f;
}

static parser_field parser_parseParamDecl(parser_parser *p, ast_Ident *name, bool typeSetsOK)
{
	parser_field f = {0};
	token_Token ptok = p->tok;
	if (name != NULL) {
		p->tok = token_IDENT; /* force token.IDENT case in switch below */
	} else if (typeSetsOK && p->tok == token_TILDE) {
		/* "~" ... */
		return (parser_field){NULL, parser_embeddedElem(p, NULL)};
	}

	switch (p->tok) {
	case token_IDENT:
		/* name */
		if (name != NULL) {
			f.name = name;
			p->tok = ptok;
		} else {
			f.name = parser_parseIdent(p);
		}
		switch (p->tok) {
		case token_IDENT:
		case token_MUL:
		case token_ARROW:
		case token_FUNC:
		case token_CHAN:
		case token_MAP:
		case token_STRUCT:
		case token_INTERFACE:
		case token_LPAREN:
			/* name type */
			f.typ = parser_parseType(p);
			break;

		case token_LBRACK:
			/* name "[" type1, ..., typeN "]" or name "[" n "]" type */
			f.typ = parser_parseArrayFieldOrTypeInstance(p, f.name, &f.name);
			break;

		case token_ELLIPSIS:
			/* name "..." type */
			f.typ = (ast_Expr *)parser_parseDotsType(p);
			return f; /* don't allow ...type "|" ... */

		case token_PERIOD:
			/* name "." ... */
			f.typ = parser_parseQualifiedIdent(p, f.name);
			f.name = NULL;
			break;

		case token_TILDE:
			if (typeSetsOK) {
				f.typ = parser_embeddedElem(p, NULL);
				return f;
			}
			break;

		case token_OR:
			if (typeSetsOK) {
				/* name "|" typeset */
				f.typ = parser_embeddedElem(p, (ast_Expr *)f.name);
				f.name = NULL;
				return f;
			}
			break;
		default:
			break;
		}
		break;

	case token_MUL:
	case token_ARROW:
	case token_FUNC:
	case token_LBRACK:
	case token_CHAN:
	case token_MAP:
	case token_STRUCT:
	case token_INTERFACE:
	case token_LPAREN:
		/* type */
		f.typ = parser_parseType(p);
		break;

	case token_ELLIPSIS:
		/* "..." type (always accepted) */
		f.typ = (ast_Expr *)parser_parseDotsType(p);
		return f; /* don't allow ...type "|" ... */

	default:
		/* TODO(rfindley): this is incorrect in the case of type parameter lists (should be
		 * "']'" in that case) */
		parser_errorExpected(p, p->pos, GOSTR("')'"));
		parser_advance(p, parser_exprEnd);
	}

	/* [name] type "|" */
	if (typeSetsOK && p->tok == token_OR && f.typ != NULL)
		f.typ = parser_embeddedElem(p, f.typ);

	return f;
}

static ast_Field *parser_newField(Vec names, ast_Expr *typ)
{
	ast_Field *f = AST_NEW(Field);
	f->Names = names;
	f->Type = typ;
	return f;
}

static Vec parser_parseParameterList(parser_parser *p, ast_Ident *name0, ast_Expr *typ0, token_Token closing,
				     bool dddok)
{
	Vec params = {0};

	/* Type parameters are the only parameter list closed by ']'. */
	bool tparams = closing == token_RBRACK;

	token_Pos pos0 = p->pos;
	if (name0 != NULL)
		pos0 = ast_Pos(name0);
	else if (typ0 != NULL)
		pos0 = ast_Pos(typ0);

	/* Note: The code below matches the corresponding code in the syntax parser closely.
	 * Changes must be reflected in either parser. For the code to match, we use the local
	 * []field list that corresponds to []syntax.Field. At the end, the list must be converted
	 * into an []*ast.Field. */

	parser_fieldVec list = {0};
	int named = 0; /* number of parameters that have an explicit name and type */
	int typed = 0; /* number of parameters that have an explicit type */

	while (name0 != NULL || (p->tok != closing && p->tok != token_EOF)) {
		parser_field par;
		if (typ0 != NULL) {
			if (tparams)
				typ0 = parser_embeddedElem(p, typ0);
			par = (parser_field){name0, typ0};
		} else {
			par = parser_parseParamDecl(p, name0, tparams);
		}
		name0 = NULL; /* 1st name was consumed if present */
		typ0 = NULL; /* 1st typ was consumed if present */
		if (par.name != NULL || par.typ != NULL) {
			parser_fieldVec_push(&list, par);
			if (par.name != NULL && par.typ != NULL)
				named++;
			if (par.typ != NULL)
				typed++;
		}
		if (!parser_atComma(p, GOSTR("parameter list"), closing))
			break;
		parser_next(p);
	}

	if (list.n == 0)
		return params; /* not uncommon */

	/* distribute parameter types (len(list) > 0) */
	if (named == 0) {
		/* all unnamed => found names are type names */
		for (int i = 0; i < list.n; i++) {
			parser_field *par = &list.v[i];
			if (par->name != NULL) {
				par->typ = (ast_Expr *)par->name;
				par->name = NULL;
			}
		}
		if (tparams) {
			/* This is the same error handling as below, adjusted for type parameters
			 * only. See comment below for details. (go.dev/issue/64534) */
			token_Pos errPos;
			GoStr msg;
			if (named == typed /* same as typed == 0 */) {
				errPos = p->pos; /* position error at closing ] */
				msg = GOSTR("missing type constraint");
			} else {
				errPos = pos0; /* position at opening [ or first name */
				msg = GOSTR("missing type parameter name");
				if (list.n == 1)
					msg = gostr_cat(msg, GOSTR(" or invalid array length"));
			}
			parser_error(p, errPos, msg);
		}
	} else if (named != list.n) {
		/* some named or we're in a type parameter list => all must be named */
		token_Pos errPos = token_NoPos; /* left-most error position (or invalid) */
		ast_Expr *typ = NULL; /* current type (from right to left) */
		for (int i = 0; i < list.n; i++) {
			parser_field *par = &list.v[list.n - i - 1];
			if (par->typ != NULL) {
				typ = par->typ;
				if (par->name == NULL) {
					errPos = ast_Pos(typ);
					ast_Ident *n = ast_NewIdent(GOSTR("_"));
					n->NamePos = errPos; /* correct position */
					par->name = n;
				}
			} else if (typ != NULL) {
				par->typ = typ;
			} else {
				/* par.typ == nil && typ == nil => we only have a par.name */
				errPos = ast_Pos(par->name);
				par->typ = (ast_Expr *)parser_badExpr(errPos, p->pos);
			}
		}
		if (token_Pos_IsValid(errPos)) {
			/* Not all parameters are named because named != len(list). If named == typed,
			 * there must be parameters that have no types. They must be at the end of the
			 * parameter list, otherwise types would have been filled in by the
			 * right-to-left sweep above and there would be no error. If tparams is set, the
			 * parameter list is a type parameter list. */
			GoStr msg;
			if (named == typed) {
				errPos = p->pos; /* position error at closing token ) or ] */
				if (tparams)
					msg = GOSTR("missing type constraint");
				else
					msg = GOSTR("missing parameter type");
			} else {
				if (tparams) {
					msg = GOSTR("missing type parameter name");
					/* go.dev/issue/60812 */
					if (list.n == 1)
						msg = gostr_cat(msg, GOSTR(" or invalid array length"));
				} else {
					msg = GOSTR("missing parameter name");
				}
			}
			parser_error(p, errPos, msg);
		}
	}

	/* check use of ... */
	bool first = true; /* only report first occurrence */
	for (int i = 0; i < list.n; i++) {
		parser_field *f = &list.v[i];
		if (f->typ != NULL && f->typ->kind == AST_Ellipsis && (!dddok || i + 1 < list.n)) {
			ast_Ellipsis *t = (ast_Ellipsis *)f->typ;
			if (first) {
				first = false;
				if (dddok)
					parser_error(p, t->Ellipsis, GOSTR("can only use ... with final parameter"));
				else
					parser_error(p, t->Ellipsis, GOSTR("invalid use of ..."));
			}
			/* use T instead of invalid ...T */
			f->typ = (ast_Expr *)parser_badExpr(ast_Pos(t), ast_End(t));
		}
	}

	/* Convert list to []*ast.Field. If list contains types only, each type gets its own
	 * ast.Field. */
	if (named == 0) {
		/* parameter list consists of types only */
		for (int i = 0; i < list.n; i++) {
			parser_assert(list.v[i].typ != NULL, "nil type in unnamed parameter list");
			vec_push(&params, parser_newField((Vec){0}, list.v[i].typ));
		}
		return params;
	}

	/* If the parameter list consists of named parameters with types, collect all names with
	 * the same types into a single ast.Field. */
	Vec names = {0};
	ast_Expr *typ = NULL;
	for (int i = 0; i < list.n; i++) {
		parser_field par = list.v[i];
		if (par.typ != typ) {
			if (names.n > 0) {
				/* addParams() */
				parser_assert(typ != NULL, "nil type in named parameter list");
				vec_push(&params, parser_newField(names, typ));
				names = (Vec){0};
			}
			typ = par.typ;
		}
		vec_push(&names, par.name);
	}
	if (names.n > 0) {
		/* addParams() */
		parser_assert(typ != NULL, "nil type in named parameter list");
		vec_push(&params, parser_newField(names, typ));
	}
	return params;
}

static ast_FieldList *parser_parseTypeParameters(parser_parser *p)
{
	token_Pos lbrack = parser_expect(p, token_LBRACK);
	Vec list = {0};
	if (p->tok != token_RBRACK)
		list = parser_parseParameterList(p, NULL, NULL, token_RBRACK, false);
	token_Pos rbrack = parser_expect(p, token_RBRACK);

	if (list.n == 0) {
		parser_error(p, rbrack, GOSTR("empty type parameter list"));
		return NULL; /* avoid follow-on errors */
	}

	ast_FieldList *fl = AST_NEW(FieldList);
	fl->Opening = lbrack;
	fl->List = list;
	fl->Closing = rbrack;
	return fl;
}

static ast_FieldList *parser_parseParameters(parser_parser *p, bool result)
{
	if (!result || p->tok == token_LPAREN) {
		token_Pos lparen = parser_expect(p, token_LPAREN);
		Vec list = {0};
		if (p->tok != token_RPAREN)
			list = parser_parseParameterList(p, NULL, NULL, token_RPAREN, !result);
		token_Pos rparen = parser_expect(p, token_RPAREN);
		ast_FieldList *fl = AST_NEW(FieldList);
		fl->Opening = lparen;
		fl->List = list;
		fl->Closing = rparen;
		return fl;
	}

	ast_Expr *typ = parser_tryIdentOrType(p);
	if (typ != NULL) {
		ast_FieldList *fl = AST_NEW(FieldList);
		vec_push(&fl->List, parser_newField((Vec){0}, typ));
		return fl;
	}

	return NULL;
}

static ast_FuncType *parser_parseFuncType(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_FUNC);
	/* accept type parameters for more tolerant parsing but complain */
	if (p->tok == token_LBRACK) {
		ast_FieldList *tparams = parser_parseTypeParameters(p);
		if (tparams != NULL)
			parser_error(p, tparams->Opening, GOSTR("function type must have no type parameters"));
	}
	ast_FieldList *params = parser_parseParameters(p, false);
	ast_FieldList *results = parser_parseParameters(p, true);

	ast_FuncType *t = AST_NEW(FuncType);
	t->Func = pos;
	t->Params = params;
	t->Results = results;
	return t;
}

static ast_Field *parser_parseMethodSpec(parser_parser *p)
{
	ast_CommentGroup *doc = p->leadComment;
	Vec idents = {0};
	ast_Expr *typ;
	ast_Expr *x = parser_parseTypeName(p, NULL);
	if (x->kind == AST_Ident) {
		ast_Ident *ident = (ast_Ident *)x;
		if (p->tok == token_LBRACK) {
			/* generic method or embedded instantiated type */
			token_Pos lbrack = p->pos;
			parser_next(p);
			p->exprLev++;
			ast_Expr *x2 = parser_parseExpr(p);
			p->exprLev--;
			if (x2->kind == AST_Ident && p->tok != token_COMMA && p->tok != token_RBRACK) {
				/* generic method m[T any]
				 *
				 * Interface methods do not have type parameters. We parse them for a
				 * better error message and improved error recovery. */
				(void)parser_parseParameterList(p, (ast_Ident *)x2, NULL, token_RBRACK, false);
				(void)parser_expect(p, token_RBRACK);
				parser_error(p, lbrack, GOSTR("interface method must have no type parameters"));

				/* TODO(rfindley) refactor to share code with parseFuncType. */
				ast_FieldList *params = parser_parseParameters(p, false);
				ast_FieldList *results = parser_parseParameters(p, true);
				vec_push(&idents, ident);
				ast_FuncType *ft = AST_NEW(FuncType);
				ft->Func = token_NoPos;
				ft->Params = params;
				ft->Results = results;
				typ = (ast_Expr *)ft;
			} else {
				/* embedded instantiated type
				 * TODO(rfindley) should resolve all identifiers in x. */
				Vec list = {0};
				vec_push(&list, x2);
				if (parser_atComma(p, GOSTR("type argument list"), token_RBRACK)) {
					p->exprLev++;
					parser_next(p);
					while (p->tok != token_RBRACK && p->tok != token_EOF) {
						vec_push(&list, parser_parseType(p));
						if (!parser_atComma(p, GOSTR("type argument list"), token_RBRACK))
							break;
						parser_next(p);
					}
					p->exprLev--;
				}
				token_Pos rbrack = parser_expectClosing(p, token_RBRACK, GOSTR("type argument list"));
				typ = parser_packIndexExpr((ast_Expr *)ident, lbrack, list, rbrack);
			}
		} else if (p->tok == token_LPAREN) {
			/* ordinary method
			 * TODO(rfindley) refactor to share code with parseFuncType. */
			ast_FieldList *params = parser_parseParameters(p, false);
			ast_FieldList *results = parser_parseParameters(p, true);
			vec_push(&idents, ident);
			ast_FuncType *ft = AST_NEW(FuncType);
			ft->Func = token_NoPos;
			ft->Params = params;
			ft->Results = results;
			typ = (ast_Expr *)ft;
		} else {
			/* embedded type */
			typ = x;
		}
	} else {
		/* embedded, possibly instantiated type */
		typ = x;
		if (p->tok == token_LBRACK) {
			/* embedded instantiated interface */
			typ = parser_parseTypeInstance(p, typ);
		}
	}

	/* Comment is added at the callsite: the field below may joined with additional type specs
	 * using '|'. */
	ast_Field *f = AST_NEW(Field);
	f->Doc = doc;
	f->Names = idents;
	f->Type = typ;
	return f;
}

static ast_Expr *parser_embeddedElem(parser_parser *p, ast_Expr *x)
{
	if (x == NULL)
		x = parser_embeddedTerm(p);
	while (p->tok == token_OR) {
		ast_BinaryExpr *t = AST_NEW(BinaryExpr);
		t->OpPos = p->pos;
		t->Op = token_OR;
		parser_next(p);
		t->X = x;
		t->Y = parser_embeddedTerm(p);
		x = (ast_Expr *)t;
	}
	return x;
}

static ast_Expr *parser_embeddedTerm(parser_parser *p)
{
	if (p->tok == token_TILDE) {
		ast_UnaryExpr *t = AST_NEW(UnaryExpr);
		t->OpPos = p->pos;
		t->Op = token_TILDE;
		parser_next(p);
		t->X = parser_parseType(p);
		return (ast_Expr *)t;
	}

	ast_Expr *t = parser_tryIdentOrType(p);
	if (t == NULL) {
		token_Pos pos = p->pos;
		parser_errorExpected(p, pos, GOSTR("~ term or type"));
		parser_advance(p, parser_exprEnd);
		return (ast_Expr *)parser_badExpr(pos, p->pos);
	}

	return t;
}

static ast_InterfaceType *parser_parseInterfaceType(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_INTERFACE);
	token_Pos lbrace = parser_expect(p, token_LBRACE);

	Vec list = {0};

	for (;;) { /* parseElements: */
		if (p->tok == token_IDENT) {
			ast_Field *f = parser_parseMethodSpec(p);
			if (f->Names.n == 0)
				f->Type = parser_embeddedElem(p, f->Type);
			f->Comment = parser_expectSemi(p);
			vec_push(&list, f);
		} else if (p->tok == token_TILDE) {
			ast_Expr *typ = parser_embeddedElem(p, NULL);
			ast_CommentGroup *comment = parser_expectSemi(p);
			ast_Field *f = parser_newField((Vec){0}, typ);
			f->Comment = comment;
			vec_push(&list, f);
		} else {
			ast_Expr *t = parser_tryIdentOrType(p);
			if (t != NULL) {
				ast_Expr *typ = parser_embeddedElem(p, t);
				ast_CommentGroup *comment = parser_expectSemi(p);
				ast_Field *f = parser_newField((Vec){0}, typ);
				f->Comment = comment;
				vec_push(&list, f);
			} else {
				break; /* break parseElements */
			}
		}
	}

	/* TODO(rfindley): the error produced here could be improved, since we could accept an
	 * identifier, 'type', or a '}' at this point. */
	token_Pos rbrace = parser_expect(p, token_RBRACE);

	ast_InterfaceType *t = AST_NEW(InterfaceType);
	t->Interface = pos;
	t->Methods = AST_NEW(FieldList);
	t->Methods->Opening = lbrace;
	t->Methods->List = list;
	t->Methods->Closing = rbrace;
	return t;
}

static ast_MapType *parser_parseMapType(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_MAP);
	parser_expect(p, token_LBRACK);
	ast_Expr *key = parser_parseType(p);
	parser_expect(p, token_RBRACK);
	ast_Expr *value = parser_parseType(p);

	ast_MapType *t = AST_NEW(MapType);
	t->Map = pos;
	t->Key = key;
	t->Value = value;
	return t;
}

static ast_ChanType *parser_parseChanType(parser_parser *p)
{
	token_Pos pos = p->pos;
	int dir = ast_SEND | ast_RECV;
	token_Pos arrow = token_NoPos;
	if (p->tok == token_CHAN) {
		parser_next(p);
		if (p->tok == token_ARROW) {
			arrow = p->pos;
			parser_next(p);
			dir = ast_SEND;
		}
	} else {
		arrow = parser_expect(p, token_ARROW);
		parser_expect(p, token_CHAN);
		dir = ast_RECV;
	}
	ast_Expr *value = parser_parseType(p);

	ast_ChanType *t = AST_NEW(ChanType);
	t->Begin = pos;
	t->Arrow = arrow;
	t->Dir = dir;
	t->Value = value;
	return t;
}

static ast_Expr *parser_parseTypeInstance(parser_parser *p, ast_Expr *typ)
{
	token_Pos opening = parser_expect(p, token_LBRACK);
	p->exprLev++;
	Vec list = {0};
	while (p->tok != token_RBRACK && p->tok != token_EOF) {
		vec_push(&list, parser_parseType(p));
		if (!parser_atComma(p, GOSTR("type argument list"), token_RBRACK))
			break;
		parser_next(p);
	}
	p->exprLev--;

	token_Pos closing = parser_expectClosing(p, token_RBRACK, GOSTR("type argument list"));

	if (list.n == 0) {
		parser_errorExpected(p, closing, GOSTR("type argument list"));
		ast_IndexExpr *x = AST_NEW(IndexExpr);
		x->X = typ;
		x->Lbrack = opening;
		x->Index = (ast_Expr *)parser_badExpr(opening + 1, closing);
		x->Rbrack = closing;
		return (ast_Expr *)x;
	}

	return parser_packIndexExpr(typ, opening, list, closing);
}

static ast_Expr *parser_tryIdentOrType0(parser_parser *p)
{
	switch (p->tok) {
	case token_IDENT: {
		ast_Expr *typ = parser_parseTypeName(p, NULL);
		if (p->tok == token_LBRACK)
			typ = parser_parseTypeInstance(p, typ);
		return typ;
	}
	case token_LBRACK: {
		token_Pos lbrack = parser_expect(p, token_LBRACK);
		return (ast_Expr *)parser_parseArrayType(p, lbrack, NULL);
	}
	case token_STRUCT:
		return (ast_Expr *)parser_parseStructType(p);
	case token_MUL:
		return (ast_Expr *)parser_parsePointerType(p);
	case token_FUNC:
		return (ast_Expr *)parser_parseFuncType(p);
	case token_INTERFACE:
		return (ast_Expr *)parser_parseInterfaceType(p);
	case token_MAP:
		return (ast_Expr *)parser_parseMapType(p);
	case token_CHAN:
	case token_ARROW:
		return (ast_Expr *)parser_parseChanType(p);
	case token_LPAREN: {
		token_Pos lparen = p->pos;
		parser_next(p);
		ast_Expr *typ = parser_parseType(p);
		token_Pos rparen = parser_expect(p, token_RPAREN);
		ast_ParenExpr *x = AST_NEW(ParenExpr);
		x->Lparen = lparen;
		x->X = typ;
		x->Rparen = rparen;
		return (ast_Expr *)x;
	}
	default:
		break;
	}

	/* no type found */
	return NULL;
}

static ast_Expr *parser_tryIdentOrType(parser_parser *p)
{
	parser_incNestLev(p); /* defer decNestLev(incNestLev(p)) */
	ast_Expr *x = parser_tryIdentOrType0(p);
	parser_decNestLev(p);
	return x;
}

/* ----------------------------------------------------------------------------
 * Blocks */

static Vec parser_parseStmtList(parser_parser *p)
{
	Vec list = {0};
	while (p->tok != token_CASE && p->tok != token_DEFAULT && p->tok != token_RBRACE && p->tok != token_EOF)
		vec_push(&list, parser_parseStmt(p));
	return list;
}

static ast_BlockStmt *parser_parseBody(parser_parser *p)
{
	token_Pos lbrace = parser_expect(p, token_LBRACE);
	Vec list = parser_parseStmtList(p);
	token_Pos rbrace = parser_expect2(p, token_RBRACE);

	ast_BlockStmt *b = AST_NEW(BlockStmt);
	b->Lbrace = lbrace;
	b->List = list;
	b->Rbrace = rbrace;
	return b;
}

static ast_BlockStmt *parser_parseBlockStmt(parser_parser *p)
{
	token_Pos lbrace = parser_expect(p, token_LBRACE);
	Vec list = parser_parseStmtList(p);
	token_Pos rbrace = parser_expect2(p, token_RBRACE);

	ast_BlockStmt *b = AST_NEW(BlockStmt);
	b->Lbrace = lbrace;
	b->List = list;
	b->Rbrace = rbrace;
	return b;
}

/* ----------------------------------------------------------------------------
 * Expressions */

static ast_Expr *parser_parseFuncTypeOrLit(parser_parser *p)
{
	ast_FuncType *typ = parser_parseFuncType(p);
	if (p->tok != token_LBRACE) {
		/* function type only */
		return (ast_Expr *)typ;
	}

	p->exprLev++;
	ast_BlockStmt *body = parser_parseBody(p);
	p->exprLev--;

	ast_FuncLit *f = AST_NEW(FuncLit);
	f->Type = typ;
	f->Body = body;
	return (ast_Expr *)f;
}

/* parseOperand may return an expression or a raw type (incl. array types of the form [...]T).
 * Callers must verify the result. */
static ast_Expr *parser_parseOperand(parser_parser *p)
{
	switch (p->tok) {
	case token_IDENT:
		return (ast_Expr *)parser_parseIdent(p);

	case token_INT:
	case token_FLOAT:
	case token_IMAG:
	case token_CHAR:
	case token_STRING: {
		token_Pos end = p->pos + p->lit.n;
		if (p->tok == token_STRING)
			end = p->stringEnd;
		ast_BasicLit *x = AST_NEW(BasicLit);
		x->ValuePos = p->pos;
		x->ValueEnd = end;
		x->Kind = p->tok;
		x->Value = p->lit;
		parser_next(p);
		return (ast_Expr *)x;
	}

	case token_LPAREN: {
		token_Pos lparen = p->pos;
		parser_next(p);
		p->exprLev++;
		ast_Expr *x = parser_parseRhs(p); /* types may be parenthesized: (some type) */
		p->exprLev--;
		token_Pos rparen = parser_expect(p, token_RPAREN);
		ast_ParenExpr *e = AST_NEW(ParenExpr);
		e->Lparen = lparen;
		e->X = x;
		e->Rparen = rparen;
		return (ast_Expr *)e;
	}

	case token_FUNC:
		return parser_parseFuncTypeOrLit(p);
	default:
		break;
	}

	ast_Expr *typ = parser_tryIdentOrType(p);
	if (typ != NULL) { /* do not consume trailing type parameters */
		/* could be type for composite literal or conversion */
		parser_assert(typ->kind != AST_Ident, "type cannot be identifier");
		return typ;
	}

	/* we have an error */
	token_Pos pos = p->pos;
	parser_errorExpected(p, pos, GOSTR("operand"));
	parser_advance(p, parser_stmtStart);
	return (ast_Expr *)parser_badExpr(pos, p->pos);
}

static ast_Expr *parser_parseSelector(parser_parser *p, ast_Expr *x)
{
	ast_Ident *sel = parser_parseIdent(p);

	ast_SelectorExpr *s = AST_NEW(SelectorExpr);
	s->X = x;
	s->Sel = sel;
	return (ast_Expr *)s;
}

static ast_Expr *parser_parseTypeAssertion(parser_parser *p, ast_Expr *x)
{
	token_Pos lparen = parser_expect(p, token_LPAREN);
	ast_Expr *typ = NULL;
	if (p->tok == token_TYPE) {
		/* type switch: typ == nil */
		parser_next(p);
	} else {
		typ = parser_parseType(p);
	}
	token_Pos rparen = parser_expect(p, token_RPAREN);

	ast_TypeAssertExpr *t = AST_NEW(TypeAssertExpr);
	t->X = x;
	t->Type = typ;
	t->Lparen = lparen;
	t->Rparen = rparen;
	return (ast_Expr *)t;
}

static ast_Expr *parser_parseIndexOrSliceOrInstance(parser_parser *p, ast_Expr *x)
{
	token_Pos lbrack = parser_expect(p, token_LBRACK);
	if (p->tok == token_RBRACK) {
		/* empty index, slice or index expressions are not permitted; accept them for parsing
		 * tolerance, but complain */
		parser_errorExpected(p, p->pos, GOSTR("operand"));
		token_Pos rbrack = p->pos;
		parser_next(p);
		ast_IndexExpr *e = AST_NEW(IndexExpr);
		e->X = x;
		e->Lbrack = lbrack;
		e->Index = (ast_Expr *)parser_badExpr(rbrack, rbrack);
		e->Rbrack = rbrack;
		return (ast_Expr *)e;
	}
	p->exprLev++;

	enum { N = 3 }; /* change the 3 to 2 to disable 3-index slices */
	Vec args = {0};
	ast_Expr *index[N] = {0};
	token_Pos colons[N - 1] = {0};
	if (p->tok != token_COLON) {
		/* We can't know if we have an index expression or a type instantiation; so even if
		 * we see a (named) type we are not going to be in type context. */
		index[0] = parser_parseRhs(p);
	}
	int ncolons = 0;
	switch (p->tok) {
	case token_COLON:
		/* slice expression */
		while (p->tok == token_COLON && ncolons < N - 1) {
			colons[ncolons] = p->pos;
			ncolons++;
			parser_next(p);
			if (p->tok != token_COLON && p->tok != token_RBRACK && p->tok != token_EOF)
				index[ncolons] = parser_parseRhs(p);
		}
		break;
	case token_COMMA:
		/* instance expression */
		vec_push(&args, index[0]);
		while (p->tok == token_COMMA) {
			parser_next(p);
			if (p->tok != token_RBRACK && p->tok != token_EOF)
				vec_push(&args, parser_parseType(p));
		}
		break;
	default:
		break;
	}

	p->exprLev--;
	token_Pos rbrack = parser_expect(p, token_RBRACK);

	if (ncolons > 0) {
		/* slice expression */
		bool slice3 = false;
		if (ncolons == 2) {
			slice3 = true;
			/* Check presence of middle and final index here rather than during
			 * type-checking to prevent erroneous programs from passing through gofmt (was
			 * go.dev/issue/7305). */
			if (index[1] == NULL) {
				parser_error(p, colons[0], GOSTR("middle index required in 3-index slice"));
				index[1] = (ast_Expr *)parser_badExpr(colons[0] + 1, colons[1]);
			}
			if (index[2] == NULL) {
				parser_error(p, colons[1], GOSTR("final index required in 3-index slice"));
				index[2] = (ast_Expr *)parser_badExpr(colons[1] + 1, rbrack);
			}
		}
		ast_SliceExpr *s = AST_NEW(SliceExpr);
		s->X = x;
		s->Lbrack = lbrack;
		s->Low = index[0];
		s->High = index[1];
		s->Max = index[2];
		s->Slice3 = slice3;
		s->Rbrack = rbrack;
		return (ast_Expr *)s;
	}

	if (args.n == 0) {
		/* index expression */
		ast_IndexExpr *e = AST_NEW(IndexExpr);
		e->X = x;
		e->Lbrack = lbrack;
		e->Index = index[0];
		e->Rbrack = rbrack;
		return (ast_Expr *)e;
	}

	/* instance expression */
	return parser_packIndexExpr(x, lbrack, args, rbrack);
}

static ast_CallExpr *parser_parseCallOrConversion(parser_parser *p, ast_Expr *fun)
{
	token_Pos lparen = parser_expect(p, token_LPAREN);
	p->exprLev++;
	Vec list = {0};
	token_Pos ellipsis = token_NoPos;
	while (p->tok != token_RPAREN && p->tok != token_EOF && !token_Pos_IsValid(ellipsis)) {
		vec_push(&list, parser_parseRhs(p)); /* builtins may expect a type: make(some type, ...) */
		if (p->tok == token_ELLIPSIS) {
			ellipsis = p->pos;
			parser_next(p);
		}
		if (!parser_atComma(p, GOSTR("argument list"), token_RPAREN))
			break;
		parser_next(p);
	}
	p->exprLev--;
	token_Pos rparen = parser_expectClosing(p, token_RPAREN, GOSTR("argument list"));

	ast_CallExpr *c = AST_NEW(CallExpr);
	c->Fun = fun;
	c->Lparen = lparen;
	c->Args = list;
	c->Ellipsis = ellipsis;
	c->Rparen = rparen;
	return c;
}

static ast_Expr *parser_parseValue(parser_parser *p)
{
	if (p->tok == token_LBRACE)
		return parser_parseLiteralValue(p, NULL);

	return parser_parseExpr(p);
}

static ast_Expr *parser_parseElement(parser_parser *p)
{
	ast_Expr *x = parser_parseValue(p);
	if (p->tok == token_COLON) {
		token_Pos colon = p->pos;
		parser_next(p);
		ast_KeyValueExpr *kv = AST_NEW(KeyValueExpr);
		kv->Key = x;
		kv->Colon = colon;
		kv->Value = parser_parseValue(p);
		x = (ast_Expr *)kv;
	}

	return x;
}

static Vec parser_parseElementList(parser_parser *p)
{
	Vec list = {0};
	while (p->tok != token_RBRACE && p->tok != token_EOF) {
		vec_push(&list, parser_parseElement(p));
		if (!parser_atComma(p, GOSTR("composite literal"), token_RBRACE))
			break;
		parser_next(p);
	}
	return list;
}

static ast_Expr *parser_parseLiteralValue(parser_parser *p, ast_Expr *typ)
{
	parser_incNestLev(p); /* defer decNestLev(incNestLev(p)) */

	token_Pos lbrace = parser_expect(p, token_LBRACE);
	Vec elts = {0};
	p->exprLev++;
	if (p->tok != token_RBRACE)
		elts = parser_parseElementList(p);
	p->exprLev--;
	token_Pos rbrace = parser_expectClosing(p, token_RBRACE, GOSTR("composite literal"));
	ast_CompositeLit *c = AST_NEW(CompositeLit);
	c->Type = typ;
	c->Lbrace = lbrace;
	c->Elts = elts;
	c->Rbrace = rbrace;

	parser_decNestLev(p);
	return (ast_Expr *)c;
}

static ast_Expr *parser_parsePrimaryExpr(parser_parser *p, ast_Expr *x)
{
	if (x == NULL)
		x = parser_parseOperand(p);
	/* We track the nesting here rather than at the entry for the function, since it can
	 * iteratively produce a nested output, and we want to limit how deep a structure we
	 * generate. (Go: defer func() { p.nestLev -= n }(); every return below subtracts n.) */
	int n;
	for (n = 1;; n++) {
		parser_incNestLev(p);
		switch (p->tok) {
		case token_PERIOD:
			parser_next(p);
			switch (p->tok) {
			case token_IDENT:
				x = parser_parseSelector(p, x);
				break;
			case token_LPAREN:
				x = parser_parseTypeAssertion(p, x);
				break;
			default: {
				token_Pos pos = p->pos;
				parser_errorExpected(p, pos, GOSTR("selector or type assertion"));
				/* TODO(rFindley) The check for token.RBRACE below is a targeted fix to
				 * error recovery sufficient to make the x/tools tests to pass with the
				 * new parsing logic introduced for type parameters. Remove this once
				 * error recovery has been more generally reconsidered. */
				if (p->tok != token_RBRACE)
					parser_next(p); /* make progress */
				ast_Ident *sel = AST_NEW(Ident);
				sel->NamePos = pos;
				sel->Name = GOSTR("_");
				ast_SelectorExpr *s = AST_NEW(SelectorExpr);
				s->X = x;
				s->Sel = sel;
				x = (ast_Expr *)s;
			}
			}
			break;
		case token_LBRACK:
			x = parser_parseIndexOrSliceOrInstance(p, x);
			break;
		case token_LPAREN:
			x = (ast_Expr *)parser_parseCallOrConversion(p, x);
			break;
		case token_LBRACE: {
			/* operand may have returned a parenthesized complit type; accept it but complain
			 * if we have a complit */
			ast_Expr *t = ast_Unparen(x);
			/* determine if '{' belongs to a composite literal or a block statement */
			switch (t->kind) {
			case AST_BadExpr:
			case AST_Ident:
			case AST_SelectorExpr:
				if (p->exprLev < 0) {
					p->nestLev -= n;
					return x;
				}
				/* x is possibly a composite literal type */
				break;
			case AST_IndexExpr:
			case AST_IndexListExpr:
				if (p->exprLev < 0) {
					p->nestLev -= n;
					return x;
				}
				/* x is possibly a composite literal type */
				break;
			case AST_ArrayType:
			case AST_StructType:
			case AST_MapType:
				/* x is a composite literal type */
				break;
			default:
				p->nestLev -= n;
				return x;
			}
			if (t != x) {
				parser_error(p, ast_Pos(t), GOSTR("cannot parenthesize type in composite literal"));
				/* already progressed, no need to advance */
			}
			x = parser_parseLiteralValue(p, x);
			break;
		}
		default:
			p->nestLev -= n;
			return x;
		}
	}
}

static ast_Expr *parser_parseUnaryExpr0(parser_parser *p)
{
	switch (p->tok) {
	case token_ADD:
	case token_SUB:
	case token_NOT:
	case token_XOR:
	case token_AND:
	case token_TILDE: {
		token_Pos pos = p->pos;
		token_Token op = p->tok;
		parser_next(p);
		ast_Expr *x = parser_parseUnaryExpr(p);
		ast_UnaryExpr *u = AST_NEW(UnaryExpr);
		u->OpPos = pos;
		u->Op = op;
		u->X = x;
		return (ast_Expr *)u;
	}

	case token_ARROW: {
		/* channel type or receive expression */
		token_Pos arrow = p->pos;
		parser_next(p);

		/* If the next token is token.CHAN we still don't know if it is a channel type or a
		 * receive operation - we only know once we have found the end of the unary
		 * expression. There are two cases:
		 *
		 *   <- type  => (<-type) must be channel type
		 *   <- expr  => <-(expr) is a receive from an expression
		 *
		 * In the first case, the arrow must be re-associated with the channel type parsed
		 * already:
		 *
		 *   <- (chan type)    =>  (<-chan type)
		 *   <- (chan<- type)  =>  (<-chan (<-type)) */

		ast_Expr *x = parser_parseUnaryExpr(p);

		/* determine which case we have */
		if (x->kind == AST_ChanType) {
			/* (<-type) */
			ast_ChanType *typ = (ast_ChanType *)x;
			bool ok = true;

			/* re-associate position info and <- */
			int dir = ast_SEND;
			while (ok && dir == ast_SEND) {
				if (typ->Dir == ast_RECV) {
					/* error: (<-type) is (<-(<-chan T)) */
					parser_errorExpected(p, typ->Arrow, GOSTR("'chan'"));
				}
				/* arrow, typ.Begin, typ.Arrow = typ.Arrow, arrow, arrow */
				token_Pos a = typ->Arrow;
				typ->Begin = arrow;
				typ->Arrow = arrow;
				arrow = a;
				/* dir, typ.Dir = typ.Dir, ast.RECV */
				dir = typ->Dir;
				typ->Dir = ast_RECV;
				/* typ, ok = typ.Value.(*ast.ChanType) */
				ok = typ->Value != NULL && typ->Value->kind == AST_ChanType;
				typ = ok ? (ast_ChanType *)typ->Value : NULL;
			}
			if (dir == ast_SEND)
				parser_errorExpected(p, arrow, GOSTR("channel type"));

			return x;
		}

		/* <-(expr) */
		ast_UnaryExpr *u = AST_NEW(UnaryExpr);
		u->OpPos = arrow;
		u->Op = token_ARROW;
		u->X = x;
		return (ast_Expr *)u;
	}

	case token_MUL: {
		/* pointer type or unary "*" expression */
		token_Pos pos = p->pos;
		parser_next(p);
		ast_Expr *x = parser_parseUnaryExpr(p);
		ast_StarExpr *s = AST_NEW(StarExpr);
		s->Star = pos;
		s->X = x;
		return (ast_Expr *)s;
	}
	default:
		break;
	}

	return parser_parsePrimaryExpr(p, NULL);
}

static ast_Expr *parser_parseUnaryExpr(parser_parser *p)
{
	parser_incNestLev(p); /* defer decNestLev(incNestLev(p)) */
	ast_Expr *x = parser_parseUnaryExpr0(p);
	parser_decNestLev(p);
	return x;
}

static token_Token parser_tokPrec(parser_parser *p, int *prec)
{
	token_Token tok = p->tok;
	if (p->inRhs && tok == token_ASSIGN)
		tok = token_EQL;
	*prec = token_Token_Precedence(tok);
	return tok;
}

/* parseBinaryExpr parses a (possibly) binary expression. If x is non-nil, it is used as the
 * left operand. */
static ast_Expr *parser_parseBinaryExpr(parser_parser *p, ast_Expr *x, int prec1)
{
	if (x == NULL)
		x = parser_parseUnaryExpr(p);
	/* We track the nesting here rather than at the entry for the function, since it can
	 * iteratively produce a nested output, and we want to limit how deep a structure we
	 * generate. (Go: defer func() { p.nestLev -= n }().) */
	int n;
	for (n = 1;; n++) {
		parser_incNestLev(p);
		int oprec;
		token_Token op = parser_tokPrec(p, &oprec);
		if (oprec < prec1) {
			p->nestLev -= n;
			return x;
		}
		token_Pos pos = parser_expect(p, op);
		ast_Expr *y = parser_parseBinaryExpr(p, NULL, oprec + 1);
		ast_BinaryExpr *b = AST_NEW(BinaryExpr);
		b->X = x;
		b->OpPos = pos;
		b->Op = op;
		b->Y = y;
		x = (ast_Expr *)b;
	}
}

/* The result may be a type or even a raw type ([...]int). */
static ast_Expr *parser_parseExpr(parser_parser *p) { return parser_parseBinaryExpr(p, NULL, token_LowestPrec + 1); }

static ast_Expr *parser_parseRhs(parser_parser *p)
{
	bool old = p->inRhs;
	p->inRhs = true;
	ast_Expr *x = parser_parseExpr(p);
	p->inRhs = old;
	return x;
}

/* ----------------------------------------------------------------------------
 * Statements */

/* Parsing modes for parseSimpleStmt. */
enum {
	parser_basic,
	parser_labelOk,
	parser_rangeOk,
};

static ast_BadStmt *parser_badStmt(token_Pos from, token_Pos to)
{
	ast_BadStmt *s = AST_NEW(BadStmt);
	s->From = from;
	s->To = to;
	return s;
}

/* parseSimpleStmt returns true as 2nd result (*isRange, may be NULL) if it parsed the assignment
 * of a range clause (with mode == rangeOk). The returned statement is an assignment with a
 * right-hand side that is a single unary expression of the form "range x". No guarantees are
 * given for the left-hand side. */
static ast_Stmt *parser_parseSimpleStmt(parser_parser *p, int mode, bool *isRangeOut)
{
	bool dummy;
	if (!isRangeOut)
		isRangeOut = &dummy;
	*isRangeOut = false;

	Vec x = parser_parseList(p, false);

	switch (p->tok) {
	case token_DEFINE:
	case token_ASSIGN:
	case token_ADD_ASSIGN:
	case token_SUB_ASSIGN:
	case token_MUL_ASSIGN:
	case token_QUO_ASSIGN:
	case token_REM_ASSIGN:
	case token_AND_ASSIGN:
	case token_OR_ASSIGN:
	case token_XOR_ASSIGN:
	case token_SHL_ASSIGN:
	case token_SHR_ASSIGN:
	case token_AND_NOT_ASSIGN: {
		/* assignment statement, possibly part of a range clause */
		token_Pos pos = p->pos;
		token_Token tok = p->tok;
		parser_next(p);
		Vec y = {0};
		bool isRange = false;
		if (mode == parser_rangeOk && p->tok == token_RANGE && (tok == token_DEFINE || tok == token_ASSIGN)) {
			token_Pos rpos = p->pos;
			parser_next(p);
			ast_UnaryExpr *u = AST_NEW(UnaryExpr);
			u->OpPos = rpos;
			u->Op = token_RANGE;
			u->X = parser_parseRhs(p);
			vec_push(&y, u);
			isRange = true;
		} else {
			y = parser_parseList(p, true);
		}
		ast_AssignStmt *as = AST_NEW(AssignStmt);
		as->Lhs = x;
		as->TokPos = pos;
		as->Tok = tok;
		as->Rhs = y;
		*isRangeOut = isRange;
		return (ast_Stmt *)as;
	}
	default:
		break;
	}

	if (x.n > 1) {
		parser_errorExpected(p, ast_Pos(x.v[0]), GOSTR("1 expression"));
		/* continue with first expression */
	}

	switch (p->tok) {
	case token_COLON: {
		/* labeled statement */
		token_Pos colon = p->pos;
		parser_next(p);
		ast_Expr *x0 = x.v[0];
		if (mode == parser_labelOk && x0->kind == AST_Ident) {
			/* Go spec: The scope of a label is the body of the function in which it is
			 * declared and excludes the body of any nested function. */
			ast_LabeledStmt *stmt = AST_NEW(LabeledStmt);
			stmt->Label = (ast_Ident *)x0;
			stmt->Colon = colon;
			stmt->Stmt = parser_parseStmt(p);
			return (ast_Stmt *)stmt;
		}
		/* The label declaration typically starts at x[0].Pos(), but the label declaration
		 * may be erroneous due to a token after that position (and before the ':'). If
		 * SpuriousErrors is not set, the (only) error reported for the line is the illegal
		 * label error instead of the token before the ':' that caused the problem. Thus, use
		 * the (latest) colon position for error reporting. */
		parser_error(p, colon, GOSTR("illegal label declaration"));
		return (ast_Stmt *)parser_badStmt(ast_Pos(x0), colon + 1);
	}

	case token_ARROW: {
		/* send statement */
		token_Pos arrow = p->pos;
		parser_next(p);
		ast_Expr *y = parser_parseRhs(p);
		ast_SendStmt *s = AST_NEW(SendStmt);
		s->Chan = x.v[0];
		s->Arrow = arrow;
		s->Value = y;
		return (ast_Stmt *)s;
	}

	case token_INC:
	case token_DEC: {
		/* increment or decrement */
		ast_IncDecStmt *s = AST_NEW(IncDecStmt);
		s->X = x.v[0];
		s->TokPos = p->pos;
		s->Tok = p->tok;
		parser_next(p);
		return (ast_Stmt *)s;
	}
	default:
		break;
	}

	/* expression */
	ast_ExprStmt *s = AST_NEW(ExprStmt);
	s->X = x.v[0];
	return (ast_Stmt *)s;
}

static ast_CallExpr *parser_parseCallExpr(parser_parser *p, GoStr callType)
{
	ast_Expr *x = parser_parseRhs(p); /* could be a conversion: (some type)(x) */
	ast_Expr *t = ast_Unparen(x);
	if (t != x) {
		parser_error(p, ast_Pos(x),
			     gostr_cat(gostr_cat(GOSTR("expression in "), callType), GOSTR(" must not be parenthesized")));
		x = t;
	}
	if (x->kind == AST_CallExpr)
		return (ast_CallExpr *)x;
	if (x->kind != AST_BadExpr) {
		/* only report error if it's a new one */
		parser_error(p, ast_End(x),
			     gostr_cat(gostr_cat(GOSTR("expression in "), callType), GOSTR(" must be function call")));
	}
	return NULL;
}

static ast_Stmt *parser_parseGoStmt(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_GO);
	ast_CallExpr *call = parser_parseCallExpr(p, GOSTR("go"));
	parser_expectSemi(p);
	if (call == NULL)
		return (ast_Stmt *)parser_badStmt(pos, pos + 2); /* len("go") */

	ast_GoStmt *s = AST_NEW(GoStmt);
	s->Go = pos;
	s->Call = call;
	return (ast_Stmt *)s;
}

static ast_Stmt *parser_parseDeferStmt(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_DEFER);
	ast_CallExpr *call = parser_parseCallExpr(p, GOSTR("defer"));
	parser_expectSemi(p);
	if (call == NULL)
		return (ast_Stmt *)parser_badStmt(pos, pos + 5); /* len("defer") */

	ast_DeferStmt *s = AST_NEW(DeferStmt);
	s->Defer = pos;
	s->Call = call;
	return (ast_Stmt *)s;
}

static ast_ReturnStmt *parser_parseReturnStmt(parser_parser *p)
{
	token_Pos pos = p->pos;
	parser_expect(p, token_RETURN);
	Vec x = {0};
	if (p->tok != token_SEMICOLON && p->tok != token_RBRACE)
		x = parser_parseList(p, true);
	parser_expectSemi(p);

	ast_ReturnStmt *s = AST_NEW(ReturnStmt);
	s->Return = pos;
	s->Results = x;
	return s;
}

static ast_BranchStmt *parser_parseBranchStmt(parser_parser *p, token_Token tok)
{
	token_Pos pos = parser_expect(p, tok);
	ast_Ident *label = NULL;
	if (tok == token_GOTO || ((tok == token_CONTINUE || tok == token_BREAK) && p->tok == token_IDENT))
		label = parser_parseIdent(p);
	parser_expectSemi(p);

	ast_BranchStmt *s = AST_NEW(BranchStmt);
	s->TokPos = pos;
	s->Tok = tok;
	s->Label = label;
	return s;
}

static ast_Expr *parser_makeExpr(parser_parser *p, ast_Stmt *s, GoStr want)
{
	if (s == NULL)
		return NULL;
	if (s->kind == AST_ExprStmt)
		return ((ast_ExprStmt *)s)->X;
	GoStr found = GOSTR("simple statement");
	if (s->kind == AST_AssignStmt)
		found = GOSTR("assignment");
	GoStr msg = gostr_cat(gostr_cat(gostr_cat(gostr_cat(GOSTR("expected "), want), GOSTR(", found ")), found),
			      GOSTR(" (missing parentheses around composite literal?)"));
	parser_error(p, ast_Pos(s), msg);
	return (ast_Expr *)parser_badExpr(ast_Pos(s), ast_End(s));
}

/* parseIfHeader is an adjusted version of parser.header in
 * cmd/compile/internal/syntax/parser.go, which has been tuned for better error handling. */
static void parser_parseIfHeader(parser_parser *p, ast_Stmt **initOut, ast_Expr **condOut)
{
	ast_Stmt *init = NULL;
	ast_Expr *cond = NULL;
	if (p->tok == token_LBRACE) {
		parser_error(p, p->pos, GOSTR("missing condition in if statement"));
		*initOut = NULL;
		*condOut = (ast_Expr *)parser_badExpr(p->pos, p->pos);
		return;
	}
	/* p.tok != token.LBRACE */

	int prevLev = p->exprLev;
	p->exprLev = -1;

	if (p->tok != token_SEMICOLON) {
		/* accept potential variable declaration but complain */
		if (p->tok == token_VAR) {
			parser_next(p);
			parser_error(p, p->pos, GOSTR("var declaration not allowed in if initializer"));
		}
		init = parser_parseSimpleStmt(p, parser_basic, NULL);
	}

	ast_Stmt *condStmt = NULL;
	struct {
		token_Pos pos;
		GoStr lit; /* ";" or "\n"; valid if pos.IsValid() */
	} semi = {0};
	if (p->tok != token_LBRACE) {
		if (p->tok == token_SEMICOLON) {
			semi.pos = p->pos;
			semi.lit = p->lit;
			parser_next(p);
		} else {
			parser_expect(p, token_SEMICOLON);
		}
		if (p->tok != token_LBRACE)
			condStmt = parser_parseSimpleStmt(p, parser_basic, NULL);
	} else {
		condStmt = init;
		init = NULL;
	}

	if (condStmt != NULL) {
		cond = parser_makeExpr(p, condStmt, GOSTR("boolean expression"));
	} else if (token_Pos_IsValid(semi.pos)) {
		if (gostr_eqc(semi.lit, "\n"))
			parser_error(p, semi.pos, GOSTR("unexpected newline, expecting { after if clause"));
		else
			parser_error(p, semi.pos, GOSTR("missing condition in if statement"));
	}

	/* make sure we have a valid AST */
	if (cond == NULL)
		cond = (ast_Expr *)parser_badExpr(p->pos, p->pos);

	p->exprLev = prevLev;
	*initOut = init;
	*condOut = cond;
}

static ast_IfStmt *parser_parseIfStmt(parser_parser *p);

static ast_IfStmt *parser_parseIfStmt0(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_IF);

	ast_Stmt *init;
	ast_Expr *cond;
	parser_parseIfHeader(p, &init, &cond);
	ast_BlockStmt *body = parser_parseBlockStmt(p);

	ast_Stmt *else_ = NULL;
	if (p->tok == token_ELSE) {
		parser_next(p);
		switch (p->tok) {
		case token_IF:
			else_ = (ast_Stmt *)parser_parseIfStmt(p);
			break;
		case token_LBRACE:
			else_ = (ast_Stmt *)parser_parseBlockStmt(p);
			parser_expectSemi(p);
			break;
		default:
			parser_errorExpected(p, p->pos, GOSTR("if statement or block"));
			else_ = (ast_Stmt *)parser_badStmt(p->pos, p->pos);
		}
	} else {
		parser_expectSemi(p);
	}

	ast_IfStmt *s = AST_NEW(IfStmt);
	s->If = pos;
	s->Init = init;
	s->Cond = cond;
	s->Body = body;
	s->Else = else_;
	return s;
}

/* (parseIfStmt0 is the body of parseIfStmt.) */
static ast_IfStmt *parser_parseIfStmt(parser_parser *p)
{
	parser_incNestLev(p); /* defer decNestLev(incNestLev(p)) */
	ast_IfStmt *s = parser_parseIfStmt0(p);
	parser_decNestLev(p);
	return s;
}

static ast_CaseClause *parser_parseCaseClause(parser_parser *p)
{
	token_Pos pos = p->pos;
	Vec list = {0};
	if (p->tok == token_CASE) {
		parser_next(p);
		list = parser_parseList(p, true);
	} else {
		parser_expect(p, token_DEFAULT);
	}

	token_Pos colon = parser_expect(p, token_COLON);
	Vec body = parser_parseStmtList(p);

	ast_CaseClause *c = AST_NEW(CaseClause);
	c->Case = pos;
	c->List = list;
	c->Colon = colon;
	c->Body = body;
	return c;
}

static bool parser_isTypeSwitchAssert(ast_Expr *x)
{
	return x->kind == AST_TypeAssertExpr && ((ast_TypeAssertExpr *)x)->Type == NULL;
}

static bool parser_isTypeSwitchGuard(parser_parser *p, ast_Stmt *s)
{
	if (s == NULL)
		return false;
	switch (s->kind) {
	case AST_ExprStmt:
		/* x.(type) */
		return parser_isTypeSwitchAssert(((ast_ExprStmt *)s)->X);
	case AST_AssignStmt: {
		/* v := x.(type) */
		ast_AssignStmt *t = (ast_AssignStmt *)s;
		if (t->Lhs.n == 1 && t->Rhs.n == 1 && parser_isTypeSwitchAssert(t->Rhs.v[0])) {
			switch (t->Tok) {
			case token_ASSIGN:
				/* permit v = x.(type) but complain */
				parser_error(p, t->TokPos, GOSTR("expected ':=', found '='"));
				/* fallthrough */
			case token_DEFINE:
				return true;
			default:
				break;
			}
		}
		break;
	}
	default:
		break;
	}
	return false;
}

static ast_Stmt *parser_parseSwitchStmt(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_SWITCH);

	ast_Stmt *s1 = NULL, *s2 = NULL;
	if (p->tok != token_LBRACE) {
		int prevLev = p->exprLev;
		p->exprLev = -1;
		if (p->tok != token_SEMICOLON)
			s2 = parser_parseSimpleStmt(p, parser_basic, NULL);
		if (p->tok == token_SEMICOLON) {
			parser_next(p);
			s1 = s2;
			s2 = NULL;
			if (p->tok != token_LBRACE) {
				/* A TypeSwitchGuard may declare a variable in addition to the variable
				 * declared in the initial SimpleStmt. Introduce extra scope to avoid
				 * redeclaration errors:
				 *
				 *	switch t := 0; t := x.(T) { ... }
				 *
				 * (this code is not valid Go because the first t cannot be accessed and
				 * thus is never used, the extra scope is needed for the correct error
				 * message).
				 *
				 * If we don't have a type switch, s2 must be an expression. Having the
				 * extra nested but empty scope won't affect it. */
				s2 = parser_parseSimpleStmt(p, parser_basic, NULL);
			}
		}
		p->exprLev = prevLev;
	}

	bool typeSwitch = parser_isTypeSwitchGuard(p, s2);
	token_Pos lbrace = parser_expect(p, token_LBRACE);
	Vec list = {0};
	while (p->tok == token_CASE || p->tok == token_DEFAULT)
		vec_push(&list, parser_parseCaseClause(p));
	token_Pos rbrace = parser_expect(p, token_RBRACE);
	parser_expectSemi(p);
	ast_BlockStmt *body = AST_NEW(BlockStmt);
	body->Lbrace = lbrace;
	body->List = list;
	body->Rbrace = rbrace;

	if (typeSwitch) {
		ast_TypeSwitchStmt *ts = AST_NEW(TypeSwitchStmt);
		ts->Switch = pos;
		ts->Init = s1;
		ts->Assign = s2;
		ts->Body = body;
		return (ast_Stmt *)ts;
	}

	ast_SwitchStmt *ss = AST_NEW(SwitchStmt);
	ss->Switch = pos;
	ss->Init = s1;
	ss->Tag = parser_makeExpr(p, s2, GOSTR("switch expression"));
	ss->Body = body;
	return (ast_Stmt *)ss;
}

static ast_CommClause *parser_parseCommClause(parser_parser *p)
{
	token_Pos pos = p->pos;
	ast_Stmt *comm = NULL;
	if (p->tok == token_CASE) {
		parser_next(p);
		Vec lhs = parser_parseList(p, false);
		if (p->tok == token_ARROW) {
			/* SendStmt */
			if (lhs.n > 1) {
				parser_errorExpected(p, ast_Pos(lhs.v[0]), GOSTR("1 expression"));
				/* continue with first expression */
			}
			token_Pos arrow = p->pos;
			parser_next(p);
			ast_Expr *rhs = parser_parseRhs(p);
			ast_SendStmt *s = AST_NEW(SendStmt);
			s->Chan = lhs.v[0];
			s->Arrow = arrow;
			s->Value = rhs;
			comm = (ast_Stmt *)s;
		} else {
			/* RecvStmt */
			token_Token tok = p->tok;
			if (tok == token_ASSIGN || tok == token_DEFINE) {
				/* RecvStmt with assignment */
				if (lhs.n > 2) {
					parser_errorExpected(p, ast_Pos(lhs.v[0]), GOSTR("1 or 2 expressions"));
					/* continue with first two expressions */
					lhs.n = 2;
				}
				token_Pos apos = p->pos;
				parser_next(p);
				ast_Expr *rhs = parser_parseRhs(p);
				ast_AssignStmt *as = AST_NEW(AssignStmt);
				as->Lhs = lhs;
				as->TokPos = apos;
				as->Tok = tok;
				vec_push(&as->Rhs, rhs);
				comm = (ast_Stmt *)as;
			} else {
				/* lhs must be single receive operation */
				if (lhs.n > 1) {
					parser_errorExpected(p, ast_Pos(lhs.v[0]), GOSTR("1 expression"));
					/* continue with first expression */
				}
				ast_ExprStmt *es = AST_NEW(ExprStmt);
				es->X = lhs.v[0];
				comm = (ast_Stmt *)es;
			}
		}
	} else {
		parser_expect(p, token_DEFAULT);
	}

	token_Pos colon = parser_expect(p, token_COLON);
	Vec body = parser_parseStmtList(p);

	ast_CommClause *c = AST_NEW(CommClause);
	c->Case = pos;
	c->Comm = comm;
	c->Colon = colon;
	c->Body = body;
	return c;
}

static ast_SelectStmt *parser_parseSelectStmt(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_SELECT);
	token_Pos lbrace = parser_expect(p, token_LBRACE);
	Vec list = {0};
	while (p->tok == token_CASE || p->tok == token_DEFAULT)
		vec_push(&list, parser_parseCommClause(p));
	token_Pos rbrace = parser_expect(p, token_RBRACE);
	parser_expectSemi(p);
	ast_BlockStmt *body = AST_NEW(BlockStmt);
	body->Lbrace = lbrace;
	body->List = list;
	body->Rbrace = rbrace;

	ast_SelectStmt *s = AST_NEW(SelectStmt);
	s->Select = pos;
	s->Body = body;
	return s;
}

static ast_Stmt *parser_parseForStmt(parser_parser *p)
{
	token_Pos pos = parser_expect(p, token_FOR);

	ast_Stmt *s1 = NULL, *s2 = NULL, *s3 = NULL;
	bool isRange = false;
	if (p->tok != token_LBRACE) {
		int prevLev = p->exprLev;
		p->exprLev = -1;
		if (p->tok != token_SEMICOLON) {
			if (p->tok == token_RANGE) {
				/* "for range x" (nil lhs in assignment) */
				token_Pos rpos = p->pos;
				parser_next(p);
				ast_UnaryExpr *u = AST_NEW(UnaryExpr);
				u->OpPos = rpos;
				u->Op = token_RANGE;
				u->X = parser_parseRhs(p);
				ast_AssignStmt *as = AST_NEW(AssignStmt);
				vec_push(&as->Rhs, u);
				s2 = (ast_Stmt *)as;
				isRange = true;
			} else {
				s2 = parser_parseSimpleStmt(p, parser_rangeOk, &isRange);
			}
		}
		if (!isRange && p->tok == token_SEMICOLON) {
			parser_next(p);
			s1 = s2;
			s2 = NULL;
			if (p->tok != token_SEMICOLON)
				s2 = parser_parseSimpleStmt(p, parser_basic, NULL);
			parser_expectSemi(p);
			if (p->tok != token_LBRACE)
				s3 = parser_parseSimpleStmt(p, parser_basic, NULL);
		}
		p->exprLev = prevLev;
	}

	ast_BlockStmt *body = parser_parseBlockStmt(p);
	parser_expectSemi(p);

	if (isRange) {
		ast_AssignStmt *as = (ast_AssignStmt *)s2;
		/* check lhs */
		ast_Expr *key = NULL, *value = NULL;
		switch (as->Lhs.n) {
		case 0:
			/* nothing to do */
			break;
		case 1:
			key = as->Lhs.v[0];
			break;
		case 2:
			key = as->Lhs.v[0];
			value = as->Lhs.v[1];
			break;
		default:
			parser_errorExpected(p, ast_Pos(as->Lhs.v[as->Lhs.n - 1]), GOSTR("at most 2 expressions"));
			return (ast_Stmt *)parser_badStmt(pos, ast_End(body));
		}
		/* parseSimpleStmt returned a right-hand side that is a single unary expression of
		 * the form "range x" */
		ast_Expr *x = ((ast_UnaryExpr *)as->Rhs.v[0])->X;
		ast_RangeStmt *rs = AST_NEW(RangeStmt);
		rs->For = pos;
		rs->Key = key;
		rs->Value = value;
		rs->TokPos = as->TokPos;
		rs->Tok = as->Tok;
		rs->Range = ast_Pos(as->Rhs.v[0]);
		rs->X = x;
		rs->Body = body;
		return (ast_Stmt *)rs;
	}

	/* regular for statement */
	ast_ForStmt *fs = AST_NEW(ForStmt);
	fs->For = pos;
	fs->Init = s1;
	fs->Cond = parser_makeExpr(p, s2, GOSTR("boolean or range expression"));
	fs->Post = s3;
	fs->Body = body;
	return (ast_Stmt *)fs;
}

static ast_Stmt *parser_parseStmt0(parser_parser *p)
{
	ast_Stmt *s;
	switch (p->tok) {
	case token_CONST:
	case token_TYPE:
	case token_VAR: {
		ast_DeclStmt *d = AST_NEW(DeclStmt);
		d->Decl = parser_parseDecl(p, parser_stmtStart);
		s = (ast_Stmt *)d;
		break;
	}
	/* tokens that may start an expression */
	case token_IDENT:
	case token_INT:
	case token_FLOAT:
	case token_IMAG:
	case token_CHAR:
	case token_STRING:
	case token_FUNC:
	case token_LPAREN: /* operands */
	case token_LBRACK:
	case token_STRUCT:
	case token_MAP:
	case token_CHAN:
	case token_INTERFACE: /* composite types */
	case token_ADD:
	case token_SUB:
	case token_MUL:
	case token_AND:
	case token_XOR:
	case token_ARROW:
	case token_NOT: /* unary operators */
		s = parser_parseSimpleStmt(p, parser_labelOk, NULL);
		/* because of the required look-ahead, labeled statements are parsed by
		 * parseSimpleStmt - don't expect a semicolon after them */
		if (s->kind != AST_LabeledStmt)
			parser_expectSemi(p);
		break;
	case token_GO:
		s = parser_parseGoStmt(p);
		break;
	case token_DEFER:
		s = parser_parseDeferStmt(p);
		break;
	case token_RETURN:
		s = (ast_Stmt *)parser_parseReturnStmt(p);
		break;
	case token_BREAK:
	case token_CONTINUE:
	case token_GOTO:
	case token_FALLTHROUGH:
		s = (ast_Stmt *)parser_parseBranchStmt(p, p->tok);
		break;
	case token_LBRACE:
		s = (ast_Stmt *)parser_parseBlockStmt(p);
		parser_expectSemi(p);
		break;
	case token_IF:
		s = (ast_Stmt *)parser_parseIfStmt(p);
		break;
	case token_SWITCH:
		s = parser_parseSwitchStmt(p);
		break;
	case token_SELECT:
		s = (ast_Stmt *)parser_parseSelectStmt(p);
		break;
	case token_FOR:
		s = parser_parseForStmt(p);
		break;
	case token_SEMICOLON: {
		/* Is it ever possible to have an implicit semicolon producing an empty statement in
		 * a valid program? (handle correctly anyway) */
		ast_EmptyStmt *e = AST_NEW(EmptyStmt);
		e->Semicolon = p->pos;
		e->Implicit = gostr_eqc(p->lit, "\n");
		s = (ast_Stmt *)e;
		parser_next(p);
		break;
	}
	case token_RBRACE: {
		/* a semicolon may be omitted before a closing "}" */
		ast_EmptyStmt *e = AST_NEW(EmptyStmt);
		e->Semicolon = p->pos;
		e->Implicit = true;
		s = (ast_Stmt *)e;
		break;
	}
	default: {
		/* no statement found */
		token_Pos pos = p->pos;
		parser_errorExpected(p, pos, GOSTR("statement"));
		parser_advance(p, parser_stmtStart);
		s = (ast_Stmt *)parser_badStmt(pos, p->pos);
	}
	}

	return s;
}

static ast_Stmt *parser_parseStmt(parser_parser *p)
{
	parser_incNestLev(p); /* defer decNestLev(incNestLev(p)) */
	ast_Stmt *s = parser_parseStmt0(p);
	parser_decNestLev(p);
	return s;
}

/* ----------------------------------------------------------------------------
 * Declarations */

typedef ast_Spec *(*parser_parseSpecFunction)(parser_parser *p, ast_CommentGroup *doc, token_Token keyword,
					       int iota);

static ast_Spec *parser_parseImportSpec(parser_parser *p, ast_CommentGroup *doc, token_Token keyword, int iota)
{
	(void)keyword;
	(void)iota;
	ast_Ident *ident = NULL;
	switch (p->tok) {
	case token_IDENT:
		ident = parser_parseIdent(p);
		break;
	case token_PERIOD:
		ident = AST_NEW(Ident);
		ident->NamePos = p->pos;
		ident->Name = GOSTR(".");
		parser_next(p);
		break;
	default:
		break;
	}

	token_Pos pos = p->pos;
	token_Pos end = p->pos;
	GoStr path = {0};
	if (p->tok == token_STRING) {
		path = p->lit;
		end = p->stringEnd;
		parser_next(p);
	} else if (token_Token_IsLiteral(p->tok)) {
		parser_error(p, pos, GOSTR("import path must be a string"));
		parser_next(p);
	} else {
		parser_error(p, pos, GOSTR("missing import path"));
		parser_advance(p, parser_exprEnd);
	}
	ast_CommentGroup *comment = parser_expectSemi(p);

	/* collect imports */
	ast_ImportSpec *spec = AST_NEW(ImportSpec);
	spec->Doc = doc;
	spec->Name = ident;
	spec->Path = AST_NEW(BasicLit);
	spec->Path->ValuePos = pos;
	spec->Path->ValueEnd = end;
	spec->Path->Kind = token_STRING;
	spec->Path->Value = path;
	spec->Comment = comment;
	vec_push(&p->imports, spec);

	return (ast_Spec *)spec;
}

static ast_Spec *parser_parseValueSpec(parser_parser *p, ast_CommentGroup *doc, token_Token keyword, int iota)
{
	(void)iota;
	Vec idents = parser_parseIdentList(p);
	ast_Expr *typ = NULL;
	Vec values = {0};
	switch (keyword) {
	case token_CONST:
		/* always permit optional type and initialization for more tolerant parsing */
		if (p->tok != token_EOF && p->tok != token_SEMICOLON && p->tok != token_RPAREN) {
			typ = parser_tryIdentOrType(p);
			if (p->tok == token_ASSIGN) {
				parser_next(p);
				values = parser_parseList(p, true);
			}
		}
		break;
	case token_VAR:
		if (p->tok != token_ASSIGN)
			typ = parser_parseType(p);
		if (p->tok == token_ASSIGN) {
			parser_next(p);
			values = parser_parseList(p, true);
		}
		break;
	default:
		fail("go/parser: unreachable");
	}
	ast_CommentGroup *comment = parser_expectSemi(p);

	ast_ValueSpec *spec = AST_NEW(ValueSpec);
	spec->Doc = doc;
	spec->Names = idents;
	spec->Type = typ;
	spec->Values = values;
	spec->Comment = comment;
	return (ast_Spec *)spec;
}

static void parser_parseGenericType(parser_parser *p, ast_TypeSpec *spec, token_Pos openPos, ast_Ident *name0,
				    ast_Expr *typ0)
{
	Vec list = parser_parseParameterList(p, name0, typ0, token_RBRACK, false);
	token_Pos closePos = parser_expect(p, token_RBRACK);
	spec->TypeParams = AST_NEW(FieldList);
	spec->TypeParams->Opening = openPos;
	spec->TypeParams->List = list;
	spec->TypeParams->Closing = closePos;
	if (p->tok == token_ASSIGN) {
		/* type alias */
		spec->Assign = p->pos;
		parser_next(p);
	}
	spec->Type = parser_parseType(p);
}

static ast_Expr *parser_extractName(ast_Expr *x, bool force, ast_Ident **name);

static ast_Spec *parser_parseTypeSpec(parser_parser *p, ast_CommentGroup *doc, token_Token keyword, int iota)
{
	(void)keyword;
	(void)iota;
	ast_Ident *name = parser_parseIdent(p);
	ast_TypeSpec *spec = AST_NEW(TypeSpec);
	spec->Doc = doc;
	spec->Name = name;

	if (p->tok == token_LBRACK) {
		/* spec.Name "[" ...
		 * array/slice type or type parameter list */
		token_Pos lbrack = p->pos;
		parser_next(p);
		if (p->tok == token_IDENT) {
			/* We may have an array type or a type parameter list. In either case we expect
			 * an expression x (which may just be a name, or a more complex expression)
			 * which we can analyze further.
			 *
			 * A type parameter list may have a type bound starting with a "[" as in: P []E.
			 * In that case, simply parsing an expression would lead to an error: P[] is
			 * invalid. But since index or slice expressions are never constant and thus
			 * invalid array length expressions, if the name is followed by "[" it must be
			 * the start of an array or slice constraint. Only if we don't see a "[" do we
			 * need to parse a full expression. Notably, name <- x is not a concern because
			 * name <- x is a statement and not an expression. */
			ast_Expr *x = (ast_Expr *)parser_parseIdent(p);
			if (p->tok != token_LBRACK) {
				/* To parse the expression starting with name, expand the call sequence
				 * we would get by passing in name to parser.expr, and pass in name to
				 * parsePrimaryExpr. */
				p->exprLev++;
				ast_Expr *lhs = parser_parsePrimaryExpr(p, x);
				x = parser_parseBinaryExpr(p, lhs, token_LowestPrec + 1);
				p->exprLev--;
			}
			/* Analyze expression x. If we can split x into a type parameter name, possibly
			 * followed by a type parameter type, we consider this the start of a type
			 * parameter list, with some caveats: a single name followed by "]" tilts the
			 * decision towards an array declaration; a type parameter type that could also
			 * be an ordinary expression but which is followed by a comma tilts the decision
			 * towards a type parameter list. */
			ast_Ident *pname;
			ast_Expr *ptype = parser_extractName(x, p->tok == token_COMMA, &pname);
			if (pname != NULL && (ptype != NULL || p->tok != token_RBRACK)) {
				/* spec.Name "[" pname ...
				 * spec.Name "[" pname ptype ...
				 * spec.Name "[" pname ptype "," ... */
				parser_parseGenericType(p, spec, lbrack, pname, ptype); /* ptype may be nil */
			} else {
				/* spec.Name "[" pname "]" ...
				 * spec.Name "[" x ... */
				spec->Type = (ast_Expr *)parser_parseArrayType(p, lbrack, x);
			}
		} else {
			/* array type */
			spec->Type = (ast_Expr *)parser_parseArrayType(p, lbrack, NULL);
		}
	} else {
		/* no type parameters */
		if (p->tok == token_ASSIGN) {
			/* type alias */
			spec->Assign = p->pos;
			parser_next(p);
		}
		spec->Type = parser_parseType(p);
	}

	spec->Comment = parser_expectSemi(p);

	return (ast_Spec *)spec;
}

static bool parser_isTypeElem(ast_Expr *x);

/* extractName for the non-OR cases (the switch of Go's extractName minus case token.OR). */
static ast_Expr *parser_extractName1(ast_Expr *x, bool force, ast_Ident **name)
{
	*name = NULL;
	switch (x->kind) {
	case AST_Ident:
		*name = (ast_Ident *)x;
		return NULL;
	case AST_BinaryExpr: {
		ast_BinaryExpr *b = (ast_BinaryExpr *)x;
		if (b->Op == token_MUL) {
			if (b->X->kind == AST_Ident && (force || parser_isTypeElem(b->Y))) {
				/* x = name *x.Y */
				ast_StarExpr *s = AST_NEW(StarExpr);
				s->Star = b->OpPos;
				s->X = b->Y;
				*name = (ast_Ident *)b->X;
				return (ast_Expr *)s;
			}
		}
		break;
	}
	case AST_CallExpr: {
		ast_CallExpr *c = (ast_CallExpr *)x;
		if (c->Fun->kind == AST_Ident) {
			if (c->Args.n == 1 && c->Ellipsis == token_NoPos && (force || parser_isTypeElem(c->Args.v[0]))) {
				/* x = name (x.Args[0])
				 * (Note that the cmd/compile/internal/syntax parser does not care about
				 * syntax tree fidelity and does not preserve parentheses here.) */
				ast_ParenExpr *pe = AST_NEW(ParenExpr);
				pe->Lparen = c->Lparen;
				pe->X = c->Args.v[0];
				pe->Rparen = c->Rparen;
				*name = (ast_Ident *)c->Fun;
				return (ast_Expr *)pe;
			}
		}
		break;
	}
	default:
		break;
	}
	return x;
}

/* extractName splits the expression x into (name, expr) if syntactically x can be written as
 * name expr. The split only happens if expr is a type element (per the isTypeElem predicate) or
 * if force is set. If x is just a name, the result is (name, nil). If the split succeeds, the
 * result is (name, expr). Otherwise the result is (nil, x). Examples:
 *
 *	x           force    name    expr
 *	------------------------------------
 *	P*[]int     T/F      P       *[]int
 *	P*E         T        P       *E
 *	P*E         F        nil     P*E
 *	P([]int)    T/F      P       ([]int)
 *	P(E)        T        P       (E)
 *	P(E)        F        nil     P(E)
 *	P*E|F|~G    T/F      P       *E|F|~G
 *	P*E|F|G     T        P       *E|F|G
 *	P*E|F|G     F        nil     P*E|F|G
 *
 * C port: Go recurses on x.X for each '|' operand; the left-deep chain is walked iteratively
 * here (it can be up to maxNestLev deep, too deep for the C stack). Same results. */
static ast_Expr *parser_extractName(ast_Expr *x, bool force, ast_Ident **name)
{
	/* descend: case token.OR: extractName(x.X, force || isTypeElem(x.Y)) */
	Vec chain = {0}; /* the '|' nodes, outermost first */
	while (x->kind == AST_BinaryExpr && ((ast_BinaryExpr *)x)->Op == token_OR) {
		ast_BinaryExpr *b = (ast_BinaryExpr *)x;
		vec_push(&chain, b);
		force = force || parser_isTypeElem(b->Y);
		x = b->X;
	}
	ast_Expr *r = parser_extractName1(x, force, name);
	/* ascend */
	for (int i = chain.n - 1; i >= 0; i--) {
		ast_BinaryExpr *b = chain.v[i];
		if (*name != NULL && r != NULL) {
			/* x = name lhs|x.Y */
			ast_BinaryExpr *op = AST_NEW(BinaryExpr);
			*op = *b;
			op->X = r;
			r = (ast_Expr *)op;
		} else {
			*name = NULL;
			r = (ast_Expr *)b;
		}
	}
	return r;
}

/* isTypeElem reports whether x is a (possibly parenthesized) type element expression. The
 * result is false if x could be a type element OR an ordinary (value) expression.
 * C port: the left operand of a binary expression is followed in a loop (see extractName). */
static bool parser_isTypeElem(ast_Expr *x)
{
	for (;;) {
		switch (x->kind) {
		case AST_ArrayType:
		case AST_StructType:
		case AST_FuncType:
		case AST_InterfaceType:
		case AST_MapType:
		case AST_ChanType:
			return true;
		case AST_BinaryExpr: {
			/* return isTypeElem(x.X) || isTypeElem(x.Y) */
			ast_BinaryExpr *b = (ast_BinaryExpr *)x;
			if (parser_isTypeElem(b->Y))
				return true;
			x = b->X;
			continue;
		}
		case AST_UnaryExpr:
			return ((ast_UnaryExpr *)x)->Op == token_TILDE;
		case AST_ParenExpr:
			x = ((ast_ParenExpr *)x)->X;
			continue;
		default:
			return false;
		}
	}
}

static ast_GenDecl *parser_parseGenDecl(parser_parser *p, token_Token keyword, parser_parseSpecFunction f)
{
	ast_CommentGroup *doc = p->leadComment;
	token_Pos pos = parser_expect(p, keyword);
	token_Pos lparen = token_NoPos, rparen = token_NoPos;
	Vec list = {0};
	if (p->tok == token_LPAREN) {
		lparen = p->pos;
		parser_next(p);
		for (int iota = 0; p->tok != token_RPAREN && p->tok != token_EOF; iota++)
			vec_push(&list, f(p, p->leadComment, keyword, iota));
		rparen = parser_expect(p, token_RPAREN);
		parser_expectSemi(p);
	} else {
		vec_push(&list, f(p, NULL, keyword, 0));
	}

	ast_GenDecl *d = AST_NEW(GenDecl);
	d->Doc = doc;
	d->TokPos = pos;
	d->Tok = keyword;
	d->Lparen = lparen;
	d->Specs = list;
	d->Rparen = rparen;
	return d;
}

static ast_FuncDecl *parser_parseFuncDecl(parser_parser *p)
{
	ast_CommentGroup *doc = p->leadComment;
	token_Pos pos = parser_expect(p, token_FUNC);

	ast_FieldList *recv = NULL;
	if (p->tok == token_LPAREN)
		recv = parser_parseParameters(p, false);

	ast_Ident *ident = parser_parseIdent(p);

	ast_FieldList *tparams = NULL;
	if (p->tok == token_LBRACK) {
		tparams = parser_parseTypeParameters(p);
		if (go_std_rel->minor < 27 && recv != NULL && tparams != NULL) {
			/* Method declarations do not have type parameters (before go1.27). We parse them
			 * for a better error message and improved error recovery. */
			parser_error(p, tparams->Opening, GOSTR("method must have no type parameters"));
			tparams = NULL;
		}
	}
	ast_FieldList *params = parser_parseParameters(p, false);
	ast_FieldList *results = parser_parseParameters(p, true);

	ast_BlockStmt *body = NULL;
	switch (p->tok) {
	case token_LBRACE:
		body = parser_parseBody(p);
		parser_expectSemi(p);
		break;
	case token_SEMICOLON:
		parser_next(p);
		if (p->tok == token_LBRACE) {
			/* opening { of function declaration on next line */
			parser_error(p, p->pos, GOSTR("unexpected semicolon or newline before {"));
			body = parser_parseBody(p);
			parser_expectSemi(p);
		}
		break;
	default:
		parser_expectSemi(p);
	}

	ast_FuncDecl *decl = AST_NEW(FuncDecl);
	decl->Doc = doc;
	decl->Recv = recv;
	decl->Name = ident;
	decl->Type = AST_NEW(FuncType);
	decl->Type->Func = pos;
	decl->Type->TypeParams = tparams;
	decl->Type->Params = params;
	decl->Type->Results = results;
	decl->Body = body;
	return decl;
}

static ast_Decl *parser_parseDecl(parser_parser *p, const bool *sync)
{
	parser_parseSpecFunction f;
	switch (p->tok) {
	case token_IMPORT:
		f = parser_parseImportSpec;
		break;

	case token_CONST:
	case token_VAR:
		f = parser_parseValueSpec;
		break;

	case token_TYPE:
		f = parser_parseTypeSpec;
		break;

	case token_FUNC:
		return (ast_Decl *)parser_parseFuncDecl(p);

	default: {
		token_Pos pos = p->pos;
		parser_errorExpected(p, pos, GOSTR("declaration"));
		parser_advance(p, sync);
		ast_BadDecl *d = AST_NEW(BadDecl);
		d->From = pos;
		d->To = p->pos;
		return (ast_Decl *)d;
	}
	}

	return (ast_Decl *)parser_parseGenDecl(p, p->tok, f);
}

/* ----------------------------------------------------------------------------
 * Source files */

static ast_File *parser_parseFile(parser_parser *p)
{
	/* Don't bother parsing the rest if we had errors scanning the first token. Likely not a Go
	 * source file at all. */
	if (p->errors.list.n != 0)
		return NULL;

	/* package clause */
	ast_CommentGroup *doc = p->leadComment;
	token_Pos pos = parser_expect(p, token_PACKAGE);
	/* Go spec: The package clause is not a declaration; the package name does not appear in
	 * any scope. */
	ast_Ident *ident = parser_parseIdent(p);
	if (gostr_eqc(ident->Name, "_") && (p->mode & parser_DeclarationErrors) != 0)
		parser_error(p, p->pos, GOSTR("invalid package name _"));
	parser_expectSemi(p);

	/* Don't bother parsing the rest if we had errors parsing the package clause. Likely not a
	 * Go source file at all. */
	if (p->errors.list.n != 0)
		return NULL;

	Vec decls = {0};
	if ((p->mode & parser_PackageClauseOnly) == 0) {
		/* import decls */
		while (p->tok == token_IMPORT)
			vec_push(&decls, parser_parseGenDecl(p, token_IMPORT, parser_parseImportSpec));

		if ((p->mode & parser_ImportsOnly) == 0) {
			/* rest of package body */
			token_Token prev = token_IMPORT;
			while (p->tok != token_EOF) {
				/* Continue to accept import declarations for error tolerance, but
				 * complain. */
				if (p->tok == token_IMPORT && prev != token_IMPORT)
					parser_error(p, p->pos, GOSTR("imports must appear before other declarations"));
				prev = p->tok;

				vec_push(&decls, parser_parseDecl(p, parser_declStart));
			}
		}
	}

	ast_File *f = AST_NEW(File);
	f->Doc = doc;
	f->Package = pos;
	f->Name = ident;
	f->Decls = decls;
	/* File{Start,End} are set by the caller (ParseFile). */
	f->Imports = p->imports;
	f->Comments = p->comments;
	/* not ported: GoVersion (see next0) */
	/* not ported: resolveFile (object resolution; always skipped) */

	return f;
}

/* packIndexExpr returns an IndexExpr x[expr0] or IndexListExpr x[expr0, ...]. */
static ast_Expr *parser_packIndexExpr(ast_Expr *x, token_Pos lbrack, Vec exprs, token_Pos rbrack)
{
	switch (exprs.n) {
	case 0:
		fail("internal error: packIndexExpr with empty expr slice");
	case 1: {
		ast_IndexExpr *e = AST_NEW(IndexExpr);
		e->X = x;
		e->Lbrack = lbrack;
		e->Index = exprs.v[0];
		e->Rbrack = rbrack;
		return (ast_Expr *)e;
	}
	default: {
		ast_IndexListExpr *e = AST_NEW(IndexListExpr);
		e->X = x;
		e->Lbrack = lbrack;
		e->Indices = exprs;
		e->Rbrack = rbrack;
		return (ast_Expr *)e;
	}
	}
}
