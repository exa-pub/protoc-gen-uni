/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.27.1 src/go/printer/printer.go.
 *
 * Package printer implements printing of AST nodes.
 *
 * Differences from Go (API only, the formatting is identical):
 *   - not ported: CommentedNode, []ast.Stmt and []ast.Decl nodes (and getLastComment, used only
 *     for CommentedNode): protogen prints whole files; Fprint accepts an ast_File, Expr, Stmt,
 *     Decl or Spec node;
 *   - the variadic print(args ...any) takes printer_arg values built with the P_* macros;
 *   - printerPool (sync.Pool) is a free list in the per-Fprint nodeSizes context. */
#include "../../internal.h"

#define printer_maxNewlines 2 /* max. number of newlines between source text */
#define printer_infinity (1 << 30)

typedef unsigned char printer_whiteSpace;

enum {
	printer_ignore = 0,
	printer_blank = ' ',
	printer_vtab = '\v',
	printer_newline = '\n',
	printer_formfeed = '\f',
	printer_indent = '>',
	printer_unindent = '<',
};

/* A pmode value represents the current printer mode. */
typedef int printer_pmode;

enum {
	printer_noExtraBlank = 1 << 0, /* disables extra blank after / *-style comment */
	printer_noExtraLinebreak = 1 << 1, /* disables extra line break after / *-style comment */
};

typedef struct {
	int cindex; /* index of the next comment */
	ast_CommentGroup *comment; /* = printer.comments[cindex-1]; or NULL */
	int commentOffset; /* = printer.posFor(printer.comments[cindex-1].List[0].Pos()).Offset; or infinity */
	bool commentNewline; /* true if the comment group contains newlines */
} printer_commentInfo;

typedef struct printer_printer printer_printer;

/* nodeSizes: map[ast.Node]int (open addressing, keyed by node pointer), shared by the nested
 * printers of one Fprint call; it also holds the printer free list (Go: printerPool). */
typedef struct {
	const ast_Node **keys;
	int *vals;
	int cap, n;
	printer_printer *pool;
} printer_nodeSizeMap;

#define printer_wsbufCap 16 /* whitespace sequences are short */

struct printer_printer {
	/* Configuration (does not change after initialization) */
	printer_Config Config;
	token_FileSet *fset;

	/* Current state */
	Buf output; /* raw printer result */
	int indent; /* current indentation */
	int level; /* level == 0: outside composite literal; level > 0: inside composite literal */
	printer_pmode mode; /* current printer mode */
	bool endAlignment; /* if set, terminate alignment immediately */
	bool impliedSemi; /* if set, a linebreak implies a semicolon */
	token_Token lastTok; /* last token printed (token.ILLEGAL if it's whitespace) */
	token_Token prevOpen; /* previous non-brace "open" token (, [, or token.ILLEGAL */
	printer_whiteSpace wsbuf[printer_wsbufCap]; /* delayed white space */
	int nwsbuf;
	IntVec goBuild; /* start index of all //go:build comments in output */
	IntVec plusBuild; /* start index of all // +build comments in output */

	/* Positions. The out position differs from the pos position when the result formatting
	 * differs from the source formatting (in the amount of white space). If there's a
	 * difference and SourcePos is set in ConfigMode, //line directives are used in the output
	 * to restore original source positions for a reader. */
	token_Position pos; /* current position in AST (source) space */
	token_Position out; /* current position in output space */
	token_Position last; /* value of pos after calling writeString */
	int *linePtr; /* if set, record out.Line for the next token in *linePtr */
	const char *sourcePosErr; /* if non-NULL, the first error emitting a //line directive */

	/* The list of all source comments, in order of appearance (comments.v == NULL: nil). */
	Vec comments;
	bool useNodeComments; /* if not set, ignore lead and line comments of nodes */

	/* Information about p.comments[p.cindex]; set up by nextComment. */
	int cindex;
	ast_CommentGroup *comment;
	int commentOffset;
	bool commentNewline;

	/* Cache of already computed node sizes. */
	printer_nodeSizeMap *nodeSizes;

	/* Cache of most recently computed line position. */
	token_Pos cachedPos;
	int cachedLine; /* line corresponding to cachedPos */

	printer_printer *nextFree; /* printerPool */
};

/* ---- print arguments (Go: print(args ...any)) ---- */

typedef enum {
	printer_argPmode,
	printer_argWhiteSpace,
	printer_argIdent,
	printer_argBasicLit,
	printer_argToken,
	printer_argString, /* incorrect AST - print error message */
} printer_argKind;

typedef struct {
	printer_argKind kind;
	int v; /* pmode, whiteSpace, token */
	const void *node; /* *ast.Ident, *ast.BasicLit */
	GoStr s; /* string */
} printer_arg;

#define P_MODE(m) ((printer_arg){printer_argPmode, (m), NULL, {0}})
#define P_WS(w) ((printer_arg){printer_argWhiteSpace, (w), NULL, {0}})
#define P_TOK(t) ((printer_arg){printer_argToken, (int)(t), NULL, {0}})
#define P_IDENT(x) ((printer_arg){printer_argIdent, 0, (x), {0}})
#define P_LIT(x) ((printer_arg){printer_argBasicLit, 0, (x), {0}})
#define P_STR(str) ((printer_arg){printer_argString, 0, NULL, (str)})
#define P_ignore P_WS(printer_ignore)
#define P_blank P_WS(printer_blank)
#define P_vtab P_WS(printer_vtab)
#define P_newline P_WS(printer_newline)
#define P_formfeed P_WS(printer_formfeed)
#define P_indent P_WS(printer_indent)
#define P_unindent P_WS(printer_unindent)

static void printer_print_(printer_printer *p, const printer_arg *args, int n);
#define printer_print(p, ...)                                                                      \
	printer_print_((p), (const printer_arg[]){__VA_ARGS__},                                    \
		       (int)(sizeof((printer_arg[]){__VA_ARGS__}) / sizeof(printer_arg)))

/* nodes.c */
static void printer_expr(printer_printer *p, ast_Expr *x);
static void printer_stmt(printer_printer *p, ast_Stmt *stmt, bool nextIsRBrace);
static void printer_decl(printer_printer *p, ast_Decl *decl);
static void printer_spec(printer_printer *p, ast_Spec *spec, int n, bool doIndent);
static void printer_file(printer_printer *p, ast_File *src);
/* comment.c */
static Vec printer_formatDocComment(Vec list);
/* gobuild.c */
static void printer_fixGoBuildLines(printer_printer *p);

static bool printer_posEq(token_Position a, token_Position b)
{
	return gostr_eq(a.Filename, b.Filename) && a.Offset == b.Offset && a.Line == b.Line && a.Column == b.Column;
}

static int printer_lineFor(printer_printer *p, token_Pos pos);
static token_Position printer_posFor(printer_printer *p, token_Pos pos);

static void printer_internalError(printer_printer *p, const char *msg)
{
	/* debug == false: nothing to do */
}

/* commentsHaveNewline reports whether a list of comments belonging to an *ast.CommentGroup
 * contains newlines. Because the position information may only be partially correct, we also
 * have to read the comment text. */
static bool printer_commentsHaveNewline(printer_printer *p, Vec list)
{
	/* len(list) > 0 */
	int line = printer_lineFor(p, ast_Pos(list.v[0]));
	for (int i = 0; i < list.n; i++) {
		ast_Comment *c = list.v[i];
		if (i > 0 && printer_lineFor(p, ast_Pos(c)) != line) {
			/* not all comments on the same line */
			return true;
		}
		GoStr t = c->Text;
		if (t.n >= 2 && (t.p[1] == '/' || strings_Contains(t, GOSTR("\n"))))
			return true;
	}
	return false;
}

static void printer_nextComment(printer_printer *p)
{
	while (p->cindex < p->comments.n) {
		ast_CommentGroup *c = p->comments.v[p->cindex];
		p->cindex++;
		Vec list = c->List;
		if (list.n > 0) {
			p->comment = c;
			p->commentOffset = printer_posFor(p, ast_Pos(list.v[0])).Offset;
			p->commentNewline = printer_commentsHaveNewline(p, list);
			return;
		}
		/* we should not reach here (correct ASTs don't have empty ast.CommentGroup nodes),
		 * but be conservative and try again */
	}
	/* no more comments */
	p->commentOffset = printer_infinity;
}

/* commentBefore reports whether the current comment group occurs before the next position in
 * the source code and printing it does not introduce implicit semicolons. */
static bool printer_commentBefore(printer_printer *p, token_Position next)
{
	return p->commentOffset < next.Offset && (!p->impliedSemi || !p->commentNewline);
}

static printer_commentInfo printer_saveCommentInfo(printer_printer *p)
{
	return (printer_commentInfo){p->cindex, p->comment, p->commentOffset, p->commentNewline};
}

static void printer_restoreCommentInfo(printer_printer *p, printer_commentInfo info)
{
	p->cindex = info.cindex;
	p->comment = info.comment;
	p->commentOffset = info.commentOffset;
	p->commentNewline = info.commentNewline;
}

/* commentSizeBefore returns the estimated size of the comments on the same line before the
 * next position. */
static int printer_commentSizeBefore(printer_printer *p, token_Position next)
{
	/* save/restore current p.commentInfo (p.nextComment() modifies it) */
	printer_commentInfo info = printer_saveCommentInfo(p);

	int size = 0;
	while (printer_commentBefore(p, next)) {
		for (int i = 0; i < p->comment->List.n; i++)
			size += VEC_AT(p->comment->List, ast_Comment, i)->Text.n;
		printer_nextComment(p);
	}
	printer_restoreCommentInfo(p, info);
	return size;
}

/* recordLine records the output line number for the next non-whitespace token in *linePtr. It
 * is used to compute an accurate line number for a formatted construct, independent of pending
 * (not yet emitted) whitespace or comments. */
static void printer_recordLine(printer_printer *p, int *linePtr) { p->linePtr = linePtr; }

/* linesFrom returns the number of output lines between the current output line and the line
 * argument, ignoring any pending (not yet emitted) whitespace or comments. It is used to
 * compute an accurate size (in number of lines) for a formatted construct. */
static int printer_linesFrom(printer_printer *p, int line) { return p->out.Line - line; }

static token_Position printer_posFor(printer_printer *p, token_Pos pos)
{
	/* not used frequently enough to cache entire token.Position */
	return token_FileSet_PositionFor(p->fset, pos, false /* absolute position */);
}

static int printer_lineFor(printer_printer *p, token_Pos pos)
{
	if (pos != p->cachedPos) {
		p->cachedPos = pos;
		p->cachedLine = token_FileSet_PositionFor(p->fset, pos, false /* absolute position */).Line;
	}
	return p->cachedLine;
}

/* writeLineDirective writes a //line directive if necessary. */
static void printer_writeLineDirective(printer_printer *p, token_Position pos)
{
	if (token_Position_IsValid(&pos) && (p->out.Line != pos.Line || !gostr_eq(p->out.Filename, pos.Filename))) {
		if (strings_ContainsAny(pos.Filename, GOSTR("\r\n"))) {
			if (p->sourcePosErr == NULL)
				p->sourcePosErr = gostr_cstr(gostr_cat(
					GOSTR("go/printer: source filename contains unexpected newline character: "),
					strconv_Quote(pos.Filename)));
			return;
		}

		buf_addc(&p->output, tabwriter_Escape); /* protect '\n' in //line from tabwriter interpretation */
		buf_adds(&p->output, "//line ");
		buf_addg(&p->output, pos.Filename);
		buf_fmt(&p->output, ":%d\n", pos.Line);
		buf_addc(&p->output, tabwriter_Escape);
		/* p.out must match the //line directive */
		p->out.Filename = pos.Filename;
		p->out.Line = pos.Line;
	}
}

/* writeIndent writes indentation. */
static void printer_writeIndent(printer_printer *p)
{
	/* use "hard" htabs - indentation columns must not be discarded by the tabwriter */
	int n = p->Config.Indent + p->indent; /* include base indentation */
	for (int i = 0; i < n; i++)
		buf_addc(&p->output, '\t');

	/* update positions */
	p->pos.Offset += n;
	p->pos.Column += n;
	p->out.Column += n;
}

/* writeByte writes ch n times to p.output and updates p.pos. Only used to write formatting
 * (white space) characters. */
static void printer_writeByte(printer_printer *p, char ch, int n)
{
	if (p->endAlignment) {
		/* Ignore any alignment control character; and at the end of the line, break with a
		 * formfeed to indicate termination of existing columns. */
		switch (ch) {
		case '\t':
		case '\v':
			ch = ' ';
			break;
		case '\n':
		case '\f':
			ch = '\f';
			p->endAlignment = false;
			break;
		}
	}

	if (p->out.Column == 1) {
		/* no need to write line directives before white space */
		printer_writeIndent(p);
	}

	for (int i = 0; i < n; i++)
		buf_addc(&p->output, ch);

	/* update positions */
	p->pos.Offset += n;
	if (ch == '\n' || ch == '\f') {
		p->pos.Line += n;
		p->out.Line += n;
		p->pos.Column = 1;
		p->out.Column = 1;
		return;
	}
	p->pos.Column += n;
	p->out.Column += n;
}

/* writeString writes the string s to p.output and updates p.pos, p.out, and p.last. If isLit
 * is set, s is escaped w/ tabwriter.Escape characters to protect s from being interpreted by
 * the tabwriter.
 *
 * Note: writeString is only used to write Go tokens, literals, and comments, all of which must
 * be written literally. Thus, it is correct to always set isLit = true. However, setting it
 * explicitly only when needed (i.e., when we don't know that s contains no tabs or line breaks)
 * avoids processing extra escape characters and reduces run time of the printer benchmark by up
 * to 10%. */
static void printer_writeString(printer_printer *p, token_Position pos, GoStr s, bool isLit)
{
	if (p->out.Column == 1) {
		if (p->Config.Mode & printer_SourcePos)
			printer_writeLineDirective(p, pos);
		printer_writeIndent(p);
	}

	if (token_Position_IsValid(&pos)) {
		/* update p.pos (if pos is invalid, continue with existing p.pos). Note: Must do this
		 * after handling line beginnings because writeIndent updates p.pos if there's
		 * indentation, but p.pos is the position of s. */
		p->pos = pos;
	}

	if (isLit) {
		/* Protect s such that is passes through the tabwriter unchanged. Note that valid Go
		 * programs cannot contain tabwriter.Escape bytes since they do not appear in legal
		 * UTF-8 sequences. */
		buf_addc(&p->output, tabwriter_Escape);
	}

	buf_addg(&p->output, s);

	/* update positions */
	int nlines = 0;
	int li = 0; /* index of last newline; valid if nlines > 0 */
	for (int i = 0; i < s.n; i++) {
		/* Raw string literals may contain any character except back quote (`). */
		char ch = s.p[i];
		if (ch == '\n' || ch == '\f') {
			/* account for line break */
			nlines++;
			li = i;
			/* A line break inside a literal will break whatever column formatting is in
			 * place; ignore any further alignment through the end of the line. */
			p->endAlignment = true;
		}
	}
	p->pos.Offset += s.n;
	if (nlines > 0) {
		p->pos.Line += nlines;
		p->out.Line += nlines;
		int c = s.n - li;
		p->pos.Column = c;
		p->out.Column = c;
	} else {
		p->pos.Column += s.n;
		p->out.Column += s.n;
	}

	if (isLit)
		buf_addc(&p->output, tabwriter_Escape);

	p->last = p->pos;
}

static void printer_writeWhitespace(printer_printer *p, int n);

/* writeCommentPrefix writes the whitespace before a comment. If there is any pending
 * whitespace, it consumes as much of it as is likely to help position the comment nicely. pos
 * is the comment position, next the position of the item after all pending comments, prev is
 * the previous comment in a group of comments (or NULL), and tok is the next token. */
static void printer_writeCommentPrefix(printer_printer *p, token_Position pos, token_Position next, ast_Comment *prev,
				       token_Token tok)
{
	if (p->output.n == 0) {
		/* the comment is the first item to be printed - don't write any whitespace */
		return;
	}

	if (token_Position_IsValid(&pos) && !gostr_eq(pos.Filename, p->last.Filename)) {
		/* comment in a different file - separate with newlines */
		printer_writeByte(p, '\f', printer_maxNewlines);
		return;
	}

	if (pos.Line == p->last.Line && (prev == NULL || prev->Text.p[1] != '/')) {
		/* comment on the same line as last item: separate with at least one separator */
		bool hasSep = false;
		if (prev == NULL) {
			/* first comment of a comment group */
			int j = 0;
			for (int i = 0; i < p->nwsbuf; i++) {
				printer_whiteSpace ch = p->wsbuf[i];
				if (ch == printer_blank) {
					/* ignore any blanks before a comment */
					p->wsbuf[i] = printer_ignore;
					continue;
				} else if (ch == printer_vtab) {
					/* respect existing tabs - important for proper formatting of
					 * commented structs */
					hasSep = true;
					continue;
				} else if (ch == printer_indent) {
					/* apply pending indentation */
					continue;
				}
				j = i;
				break;
			}
			printer_writeWhitespace(p, j);
		}
		/* make sure there is at least one separator */
		if (!hasSep) {
			char sep = '\t';
			if (pos.Line == next.Line) {
				/* next item is on the same line as the comment (which must be a
				 * / *-style comment): separate with a blank instead of a tab */
				sep = ' ';
			}
			printer_writeByte(p, sep, 1);
		}

	} else {
		/* comment on a different line: separate with at least one line break */
		bool droppedLinebreak = false;
		int j = 0;
		for (int i = 0; i < p->nwsbuf; i++) {
			printer_whiteSpace ch = p->wsbuf[i];
			if (ch == printer_blank || ch == printer_vtab) {
				/* ignore any horizontal whitespace before line breaks */
				p->wsbuf[i] = printer_ignore;
				continue;
			} else if (ch == printer_indent) {
				/* apply pending indentation */
				continue;
			} else if (ch == printer_unindent) {
				/* if this is not the last unindent, apply it as it is (likely) belonging
				 * to the last construct (e.g., a multi-line expression list) and is not
				 * part of closing a block */
				if (i + 1 < p->nwsbuf && p->wsbuf[i + 1] == printer_unindent)
					continue;
				/* if the next token is not a closing }, apply the unindent if it appears
				 * that the comment is aligned with the token; otherwise assume the
				 * unindent is part of a closing block and stop (this scenario appears
				 * with comments before a case label where the comments apply to the next
				 * case instead of the current one) */
				if (tok != token_RBRACE && pos.Column == next.Column)
					continue;
			} else if (ch == printer_newline || ch == printer_formfeed) {
				p->wsbuf[i] = printer_ignore;
				droppedLinebreak = prev == NULL; /* record only if first comment of a group */
			}
			j = i;
			break;
		}
		printer_writeWhitespace(p, j);

		/* determine number of linebreaks before the comment */
		int n = 0;
		if (token_Position_IsValid(&pos) && token_Position_IsValid(&p->last)) {
			n = pos.Line - p->last.Line;
			if (n < 0) /* should never happen */
				n = 0;
		}

		/* at the package scope level only (p.indent == 0), add an extra newline if we
		 * dropped one before: this preserves a blank line before documentation comments at
		 * the package scope level (issue 2570) */
		if (p->indent == 0 && droppedLinebreak)
			n++;

		/* make sure there is at least one line break if the previous comment was a line
		 * comment */
		if (n == 0 && prev != NULL && prev->Text.p[1] == '/')
			n = 1;

		if (n > 0) {
			/* use formfeeds to break columns before a comment; this is analogous to using
			 * formfeeds to separate individual lines of / *-style comments */
			printer_writeByte(p, '\f', n < printer_maxNewlines ? n : printer_maxNewlines);
		}
	}
}

/* Returns true if s contains only white space (only tabs and blanks can appear in the
 * printer's context). */
static bool printer_isBlank(GoStr s)
{
	for (int i = 0; i < s.n; i++)
		if ((unsigned char)s.p[i] > ' ')
			return false;
	return true;
}

/* commonPrefix returns the common prefix of a and b. */
static GoStr printer_commonPrefix(GoStr a, GoStr b)
{
	int i = 0;
	while (i < a.n && i < b.n && a.p[i] == b.p[i] && ((unsigned char)a.p[i] <= ' ' || a.p[i] == '*'))
		i++;
	return gostr_sub(a, 0, i);
}

/* trimRight returns s with trailing whitespace removed. */
static GoStr printer_trimRight(GoStr s) { return strings_TrimRightFunc(s, unicode_IsSpace); }

/* stripCommonPrefix removes a common prefix from / *-style comment lines (unless no comment line
 * is indented, all but the first line have some form of space prefix). The prefix is computed
 * using heuristics such that is likely that the comment contents are nicely laid out after
 * re-printing each line using the printer's current indentation. */
static void printer_stripCommonPrefix(GoStrVec lines)
{
	if (lines.n <= 1)
		return; /* at most one line - nothing to do */
	/* len(lines) > 1 */

	/* The heuristic in this function tries to handle a few common patterns of / *-style
	 * comments: Comments where the opening / * and closing * / are aligned and the rest of the
	 * comment text is aligned and indented with blanks or tabs, cases with a vertical "line of
	 * stars" on the left, and cases where the closing * / is on the same line as the last
	 * comment text.
	 *
	 * Compute maximum common white prefix of all but the first, last, and blank lines, and
	 * replace blank lines with empty lines (the first line starts with / * and has no prefix).
	 * In cases where only the first and last lines are not blank, such as two-line comments,
	 * or comments where all inner lines are blank, consider the last line for the prefix
	 * computation since otherwise the prefix would be empty.
	 *
	 * Note that the first and last line are never empty (they contain the opening / * and
	 * closing * / respectively) and thus they can be ignored by the blank line check. */
	GoStr prefix = GOSTR("");
	bool prefixSet = false;
	if (lines.n > 2) {
		for (int i = 1; i < lines.n - 1; i++) {
			GoStr line = lines.v[i];
			if (printer_isBlank(line)) {
				lines.v[i] = GOSTR("");
			} else {
				if (!prefixSet) {
					prefix = line;
					prefixSet = true;
				}
				prefix = printer_commonPrefix(prefix, line);
			}
		}
	}
	/* If we don't have a prefix yet, consider the last line. */
	if (!prefixSet) {
		GoStr line = lines.v[lines.n - 1];
		prefix = printer_commonPrefix(line, line);
	}

	/* Check for vertical "line of stars" and correct prefix accordingly. */
	bool lineOfStars = false;
	GoStr before;
	if (strings_Cut(prefix, GOSTR("*"), &before, NULL)) {
		/* remove trailing blank from prefix so stars remain aligned */
		prefix = strings_TrimSuffix(before, GOSTR(" "));
		lineOfStars = true;
	} else {
		/* No line of stars present. Determine the white space on the first line after the
		 * / * and before the beginning of the comment text, assume two blanks instead of the
		 * / * unless the first character after the / * is a tab. If the first comment line is
		 * empty but for the opening / *, assume up to 3 blanks or a tab. This whitespace may
		 * be found as suffix in the common prefix. */
		GoStr first = lines.v[0];
		if (printer_isBlank(gostr_from(first, 2))) {
			/* no comment text on the first line: reduce prefix by up to 3 blanks or a tab
			 * if present - this keeps comment text indented relative to the / * and * /'s if
			 * it was indented in the first place */
			int i = prefix.n;
			for (int n = 0; n < 3 && i > 0 && prefix.p[i - 1] == ' '; n++)
				i--;
			if (i == prefix.n && i > 0 && prefix.p[i - 1] == '\t')
				i--;
			prefix = gostr_sub(prefix, 0, i);
		} else {
			/* comment text on the first line */
			char *suffix = xalloc((size_t)first.n + 1);
			int n = 2; /* start after opening / * */
			while (n < first.n && (unsigned char)first.p[n] <= ' ') {
				suffix[n] = first.p[n];
				n++;
			}
			GoStr sfx;
			if (n > 2 && suffix[2] == '\t') {
				/* assume the '\t' compensates for the / * */
				sfx = gostr(suffix + 2, n - 2);
			} else {
				/* otherwise assume two blanks */
				suffix[0] = ' ';
				suffix[1] = ' ';
				sfx = gostr(suffix, n);
			}
			/* Shorten the computed common prefix by the length of suffix, if it is found as
			 * suffix of the prefix. */
			prefix = strings_TrimSuffix(prefix, sfx);
		}
	}

	/* Handle last line: If it only contains a closing * /, align it with the opening / *,
	 * otherwise align the text with the other lines. */
	GoStr last = lines.v[lines.n - 1];
	GoStr closing = GOSTR("*/");
	strings_Cut(last, closing, &before, NULL); /* closing always present */
	if (printer_isBlank(before)) {
		/* last line only contains closing * / */
		if (lineOfStars)
			closing = GOSTR(" */"); /* add blank to align final star */
		lines.v[lines.n - 1] = gostr_cat(prefix, closing);
	} else {
		/* last line contains more comment text - assume it is aligned like the other lines
		 * and include in prefix computation */
		prefix = printer_commonPrefix(prefix, last);
	}

	/* Remove the common prefix from all but the first and empty lines. */
	for (int i = 0; i < lines.n; i++) {
		GoStr line = lines.v[i];
		if (i > 0 && line.n != 0)
			lines.v[i] = gostr_from(line, prefix.n);
	}
}

static void printer_writeComment(printer_printer *p, ast_Comment *comment)
{
	GoStr text = comment->Text;
	token_Position pos = printer_posFor(p, ast_Pos(comment));

	int savedIndent = p->indent;
	if (strings_HasPrefix(text, GOSTR("//line ")) && (!token_Position_IsValid(&pos) || pos.Column == 1)) {
		/* Possibly a //-style line directive. Suspend indentation temporarily to keep line
		 * directive valid (restored at the end: Go defer). */
		p->indent = 0;
	}

	/* shortcut common case of //-style comments */
	if (text.p[1] == '/') {
		if (constraint_IsGoBuild(text))
			intvec_push(&p->goBuild, (int)p->output.n);
		else if (constraint_IsPlusBuild(text))
			intvec_push(&p->plusBuild, (int)p->output.n);
		printer_writeString(p, pos, printer_trimRight(text), true);
		p->indent = savedIndent;
		return;
	}

	/* for / *-style comments, print line by line and let the write function take care of the
	 * proper indentation */
	GoStrVec lines = strings_Split(text, GOSTR("\n"));

	/* The comment started in the first column but is going to be indented. For an idempotent
	 * result, add indentation to all lines such that they look like they were indented before -
	 * this will make sure the common prefix computation is the same independent of how many
	 * times formatting is applied (was issue 1835). */
	if (token_Position_IsValid(&pos) && pos.Column == 1 && p->indent > 0) {
		for (int i = 1; i < lines.n; i++)
			lines.v[i] = gostr_cat(GOSTR("   "), lines.v[i]);
	}

	printer_stripCommonPrefix(lines);

	/* write comment lines, separated by formfeed, without a line break after the last line */
	for (int i = 0; i < lines.n; i++) {
		GoStr line = lines.v[i];
		if (i > 0) {
			printer_writeByte(p, '\f', 1);
			pos = p->pos;
		}
		if (line.n > 0)
			printer_writeString(p, pos, printer_trimRight(line), true);
	}
	p->indent = savedIndent;
}

/* writeCommentSuffix writes a line break after a comment if indicated and processes any
 * leftover indentation information. If a line break is needed, the kind of break (newline vs
 * formfeed) depends on the pending whitespace. The writeCommentSuffix result indicates if a
 * newline was written or if a formfeed was dropped from the whitespace buffer. */
static void printer_writeCommentSuffix(printer_printer *p, bool needsLinebreak, bool *wroteNewline, bool *droppedFF)
{
	*wroteNewline = false;
	*droppedFF = false;
	for (int i = 0; i < p->nwsbuf; i++) {
		printer_whiteSpace ch = p->wsbuf[i];
		switch (ch) {
		case printer_blank:
		case printer_vtab:
			/* ignore trailing whitespace */
			p->wsbuf[i] = printer_ignore;
			break;
		case printer_indent:
		case printer_unindent:
			/* don't lose indentation information */
			break;
		case printer_newline:
		case printer_formfeed:
			/* if we need a line break, keep exactly one but remember if we dropped any
			 * formfeeds */
			if (needsLinebreak) {
				needsLinebreak = false;
				*wroteNewline = true;
			} else {
				if (ch == printer_formfeed)
					*droppedFF = true;
				p->wsbuf[i] = printer_ignore;
			}
			break;
		}
	}
	printer_writeWhitespace(p, p->nwsbuf);

	/* make sure we have a line break */
	if (needsLinebreak) {
		printer_writeByte(p, '\n', 1);
		*wroteNewline = true;
	}
}

/* containsLinebreak reports whether the whitespace buffer contains any line breaks. */
static bool printer_containsLinebreak(printer_printer *p)
{
	for (int i = 0; i < p->nwsbuf; i++)
		if (p->wsbuf[i] == printer_newline || p->wsbuf[i] == printer_formfeed)
			return true;
	return false;
}

/* intersperseComments consumes all comments that appear before the next token tok and prints
 * it together with the buffered whitespace (i.e., the whitespace that needs to be written
 * before the next token). A heuristic is used to mix the comments and whitespace. The
 * intersperseComments result indicates if a newline was written or if a formfeed was dropped
 * from the whitespace buffer. */
static void printer_intersperseComments(printer_printer *p, token_Position next, token_Token tok, bool *wroteNewline,
					bool *droppedFF)
{
	ast_Comment *last = NULL;
	while (printer_commentBefore(p, next)) {
		Vec list = p->comment->List;
		bool changed = false;
		/* (go1.27: not before an identifier, e.g. a struct field, go.dev/issue/52605) */
		if ((go_std_rel->minor < 27 || tok != token_IDENT) &&
		    p->lastTok != token_IMPORT && /* do not rewrite cgo's import "C" comments */
		    printer_posFor(p, ast_Pos(p->comment)).Column == 1 &&
		    printer_posEq(printer_posFor(p, ast_End(p->comment) + 1), next)) {
			/* Unindented comment abutting next token position: a top-level doc comment. */
			list = printer_formatDocComment(list);
			changed = true;

			if (p->comment->List.n > 0 && list.n == 0) {
				/* The doc comment was removed entirely. Keep preceding whitespace. */
				printer_writeCommentPrefix(p, printer_posFor(p, ast_Pos(p->comment)), next, last, tok);
				/* Change print state to continue at next. */
				p->pos = next;
				p->last = next;
				/* There can't be any more comments. */
				printer_nextComment(p);
				printer_writeCommentSuffix(p, false, wroteNewline, droppedFF);
				return;
			}
		}
		for (int i = 0; i < list.n; i++) {
			ast_Comment *c = list.v[i];
			printer_writeCommentPrefix(p, printer_posFor(p, ast_Pos(c)), next, last, tok);
			printer_writeComment(p, c);
			last = c;
		}
		/* In case list was rewritten, change print state to where the original list would
		 * have ended. */
		if (p->comment->List.n > 0 && changed) {
			last = VEC_AT(p->comment->List, ast_Comment, p->comment->List.n - 1);
			p->pos = printer_posFor(p, ast_End(last));
			p->last = p->pos;
		}
		printer_nextComment(p);
	}

	if (last != NULL) {
		/* If the last comment is a / *-style comment and the next item follows on the same
		 * line but is not a comma, and not a "closing" token immediately following its
		 * corresponding "opening" token, add an extra separator unless explicitly disabled.
		 * Use a blank as separator unless we have pending linebreaks, they are not disabled,
		 * and we are outside a composite literal, in which case we want a linebreak (issue
		 * 15137). */
		bool needsLinebreak = false;
		if ((p->mode & printer_noExtraBlank) == 0 && last->Text.p[1] == '*' &&
		    printer_lineFor(p, ast_Pos(last)) == next.Line && tok != token_COMMA &&
		    (tok != token_RPAREN || p->prevOpen == token_LPAREN) &&
		    (tok != token_RBRACK || p->prevOpen == token_LBRACK)) {
			if (printer_containsLinebreak(p) && (p->mode & printer_noExtraLinebreak) == 0 && p->level == 0)
				needsLinebreak = true;
			else
				printer_writeByte(p, ' ', 1);
		}
		/* Ensure that there is a line break after a //-style comment, before EOF, and before
		 * a closing '}' unless explicitly disabled. */
		if (last->Text.p[1] == '/' || tok == token_EOF ||
		    (tok == token_RBRACE && (p->mode & printer_noExtraLinebreak) == 0))
			needsLinebreak = true;
		printer_writeCommentSuffix(p, needsLinebreak, wroteNewline, droppedFF);
		return;
	}

	/* no comment was written - we should never reach here since intersperseComments should
	 * not be called in that case */
	printer_internalError(p, "intersperseComments called without pending comments");
	*wroteNewline = false;
	*droppedFF = false;
}

/* writeWhitespace writes the first n whitespace entries. */
static void printer_writeWhitespace(printer_printer *p, int n)
{
	/* write entries */
	for (int i = 0; i < n; i++) {
		printer_whiteSpace ch = p->wsbuf[i];
		switch (ch) {
		case printer_ignore:
			/* ignore! */
			break;
		case printer_indent:
			p->indent++;
			break;
		case printer_unindent:
			p->indent--;
			if (p->indent < 0) {
				printer_internalError(p, "negative indentation");
				p->indent = 0;
			}
			break;
		case printer_newline:
		case printer_formfeed:
			/* A line break immediately followed by a "correcting" unindent is swapped with
			 * the unindent - this permits proper label positioning. If a comment is between
			 * the line break and the label, the unindent is not part of the comment
			 * whitespace prefix and the comment will be positioned correctly indented. */
			if (i + 1 < n && p->wsbuf[i + 1] == printer_unindent) {
				/* Use a formfeed to terminate the current section. Otherwise, a long label
				 * name on the next line leading to a wide column may increase the
				 * indentation column of lines before the label; effectively leading to
				 * wrong indentation. */
				p->wsbuf[i] = printer_unindent;
				p->wsbuf[i + 1] = printer_formfeed;
				i--; /* do it again */
				continue;
			}
			/* fallthrough */
		default:
			printer_writeByte(p, (char)ch, 1);
			break;
		}
	}

	/* shift remaining entries down */
	int l = p->nwsbuf - n;
	memmove(p->wsbuf, p->wsbuf + n, (size_t)l);
	p->nwsbuf = l;
}

/* ---- Printing interface ---- */

/* nlimit limits n to maxNewlines. */
static int printer_nlimit(int n) { return n < printer_maxNewlines ? n : printer_maxNewlines; }

static bool printer_mayCombine(token_Token prev, char next)
{
	switch (prev) {
	case token_INT:
		return next == '.'; /* 1. */
	case token_ADD:
		return next == '+'; /* ++ */
	case token_SUB:
		return next == '-'; /* -- */
	case token_QUO:
		return next == '*'; /* / * */
	case token_LSS:
		return next == '-' || next == '<'; /* <- or << */
	case token_AND:
		return next == '&' || next == '^'; /* && or &^ */
	default:
		return false;
	}
}

static void printer_setPos(printer_printer *p, token_Pos pos)
{
	if (token_Pos_IsValid(pos))
		p->pos = printer_posFor(p, pos); /* accurate position of next item */
}

static void printer_flush(printer_printer *p, token_Position next, token_Token tok, bool *wroteNewline,
			  bool *droppedFF);

/* print prints a list of "items" (roughly corresponding to syntactic tokens, but also
 * including whitespace and formatting information). It is the only print function that should
 * be called directly from any of the AST printing functions in nodes.c.
 *
 * Whitespace is accumulated until a non-whitespace token appears. Any comments that need to
 * appear before that token are printed first, taking into account the amount and structure of
 * any pending white- space for best comment placement. Then, any leftover whitespace is
 * printed, followed by the actual token. */
static void printer_print_(printer_printer *p, const printer_arg *args, int nargs)
{
	for (int a = 0; a < nargs; a++) {
		const printer_arg *arg = &args[a];
		/* information about the current arg */
		GoStr data = {0};
		bool isLit = false;
		bool impliedSemi = false; /* value for p.impliedSemi after this arg */

		/* record previous opening token, if any */
		switch (p->lastTok) {
		case token_ILLEGAL:
			/* ignore (white space) */
			break;
		case token_LPAREN:
		case token_LBRACK:
			p->prevOpen = p->lastTok;
			break;
		default:
			/* other tokens followed any opening token */
			p->prevOpen = token_ILLEGAL;
			break;
		}

		switch (arg->kind) {
		case printer_argPmode:
			/* toggle printer mode */
			p->mode ^= arg->v;
			continue;

		case printer_argWhiteSpace: {
			printer_whiteSpace x = (printer_whiteSpace)arg->v;
			if (x == printer_ignore) {
				/* don't add ignore's to the buffer; they may screw up "correcting" unindents
				 * (see LabeledStmt) */
				continue;
			}
			int i = p->nwsbuf;
			if (i == printer_wsbufCap) {
				/* Whitespace sequences are very short so this should never happen. Handle
				 * gracefully (but possibly with bad comment placement) if it does happen. */
				printer_writeWhitespace(p, i);
				i = 0;
			}
			p->nwsbuf = i + 1;
			p->wsbuf[i] = x;
			if (x == printer_newline || x == printer_formfeed) {
				/* newlines affect the current state (p.impliedSemi) and not the state
				 * after printing arg (impliedSemi) because comments can be interspersed
				 * before the arg in this case */
				p->impliedSemi = false;
			}
			p->lastTok = token_ILLEGAL;
			continue;
		}

		case printer_argIdent:
			data = ((const ast_Ident *)arg->node)->Name;
			impliedSemi = true;
			p->lastTok = token_IDENT;
			break;

		case printer_argBasicLit:
			data = ((const ast_BasicLit *)arg->node)->Value;
			isLit = true;
			impliedSemi = true;
			p->lastTok = ((const ast_BasicLit *)arg->node)->Kind;
			break;

		case printer_argToken: {
			token_Token x = (token_Token)arg->v;
			GoStr s = token_Token_String(x);
			if (printer_mayCombine(p->lastTok, s.n > 0 ? s.p[0] : 0)) {
				/* the previous and the current token must be separated by a blank
				 * otherwise they combine into a different incorrect token sequence (except
				 * for token.INT followed by a '.' this should never happen because it is
				 * taken care of via binary expression formatting) */
				if (p->nwsbuf != 0)
					printer_internalError(p, "whitespace buffer not empty");
				p->nwsbuf = 1;
				p->wsbuf[0] = ' ';
			}
			data = s;
			/* some keywords followed by a newline imply a semicolon */
			switch (x) {
			case token_BREAK:
			case token_CONTINUE:
			case token_FALLTHROUGH:
			case token_RETURN:
			case token_INC:
			case token_DEC:
			case token_RPAREN:
			case token_RBRACK:
			case token_RBRACE:
				impliedSemi = true;
				break;
			default:
				break;
			}
			p->lastTok = x;
			break;
		}

		case printer_argString:
			/* incorrect AST - print error message */
			data = arg->s;
			isLit = true;
			impliedSemi = true;
			p->lastTok = token_STRING;
			break;
		}
		/* data != "" */

		token_Position next = p->pos; /* estimated/accurate position of next item */
		bool wroteNewline, droppedFF;
		printer_flush(p, next, p->lastTok, &wroteNewline, &droppedFF);

		/* intersperse extra newlines if present in the source and if they don't cause extra
		 * semicolons (don't do this in flush as it will cause extra newlines at the end of a
		 * file) */
		if (!p->impliedSemi) {
			int n = printer_nlimit(next.Line - p->pos.Line);
			/* don't exceed maxNewlines if we already wrote one */
			if (wroteNewline && n == printer_maxNewlines)
				n = printer_maxNewlines - 1;
			if (n > 0) {
				char ch = '\n';
				if (droppedFF)
					ch = '\f'; /* use formfeed since we dropped one before */
				printer_writeByte(p, ch, n);
				impliedSemi = false;
			}
		}

		/* the next token starts now - record its line number if requested */
		if (p->linePtr != NULL) {
			*p->linePtr = p->out.Line;
			p->linePtr = NULL;
		}

		printer_writeString(p, next, data, isLit);
		p->impliedSemi = impliedSemi;
	}
}

/* flush prints any pending comments and whitespace occurring textually before the position of
 * the next token tok. The flush result indicates if a newline was written or if a formfeed was
 * dropped from the whitespace buffer. */
static void printer_flush(printer_printer *p, token_Position next, token_Token tok, bool *wroteNewline,
			  bool *droppedFF)
{
	*wroteNewline = false;
	*droppedFF = false;
	if (printer_commentBefore(p, next)) {
		/* if there are comments before the next item, intersperse them */
		printer_intersperseComments(p, next, tok, wroteNewline, droppedFF);
	} else {
		/* otherwise, write any leftover whitespace */
		printer_writeWhitespace(p, p->nwsbuf);
	}
}

/* getDoc returns the ast.CommentGroup associated with n, if any. */
static ast_CommentGroup *printer_getDoc(ast_Node *n)
{
	switch (n->kind) {
	case AST_Field:
		return ((ast_Field *)n)->Doc;
	case AST_ImportSpec:
		return ((ast_ImportSpec *)n)->Doc;
	case AST_ValueSpec:
		return ((ast_ValueSpec *)n)->Doc;
	case AST_TypeSpec:
		return ((ast_TypeSpec *)n)->Doc;
	case AST_GenDecl:
		return ((ast_GenDecl *)n)->Doc;
	case AST_FuncDecl:
		return ((ast_FuncDecl *)n)->Doc;
	case AST_File:
		return ((ast_File *)n)->Doc;
	default:
		return NULL;
	}
}

/* not ported: getLastComment (used only for CommentedNode). */

static const char *printer_typeName(const ast_Node *n)
{
	if (n == NULL)
		return "<nil>";
	switch (n->kind) {
	case AST_Comment:
		return "*ast.Comment";
	case AST_CommentGroup:
		return "*ast.CommentGroup";
	case AST_Field:
		return "*ast.Field";
	case AST_FieldList:
		return "*ast.FieldList";
	default:
		return "ast.Node";
	}
}

static const char *printer_printNode(printer_printer *p, ast_Node *node)
{
	/* not ported: *CommentedNode (see the file comment) */
	if (node != NULL && node->kind == AST_File) {
		/* use ast.File comments, if any */
		p->comments = ((ast_File *)node)->Comments;
	}

	/* if there are no comments, use node comments */
	p->useNodeComments = p->comments.v == NULL;

	/* get comments ready for use */
	printer_nextComment(p);

	printer_print(p, P_MODE(0));

	/* format node */
	if (node == NULL)
		goto unsupported;
	if (AST_IS_EXPR(node)) {
		printer_expr(p, node);
	} else if (AST_IS_STMT(node)) {
		/* A labeled statement will un-indent to position the label. Set p.indent to 1 so we
		 * don't get indent "underflow". */
		if (node->kind == AST_LabeledStmt)
			p->indent = 1;
		printer_stmt(p, node, false);
	} else if (AST_IS_DECL(node)) {
		printer_decl(p, node);
	} else if (AST_IS_SPEC(node)) {
		printer_spec(p, node, 1, false);
	} else if (node->kind == AST_File) {
		printer_file(p, (ast_File *)node);
	} else {
		goto unsupported;
	}

	return p->sourcePosErr;

unsupported:
	return xcat("go/printer: unsupported node type ", printer_typeName(node));
}

/* ---- Trimmer ---- */

/* A trimmer is an io.Writer filter for stripping tabwriter.Escape characters, trailing blanks
 * and tabs, and for converting formfeed and vtab characters into newlines and htabs (in case no
 * tabwriter is used). Text bracketed by tabwriter.Escape characters is passed through
 * unchanged. */
typedef struct {
	io_Writer output;
	int state;
	Buf space;
} printer_trimmer;

/* trimmer is implemented as a state machine. It can be in one of the following states: */
enum {
	printer_inSpace, /* inside space */
	printer_inEscape, /* inside text bracketed by tabwriter.Escapes */
	printer_inText, /* inside text */
};

static void printer_trimmer_resetSpace(printer_trimmer *p)
{
	p->state = printer_inSpace;
	p->space.n = 0;
}

/* Design note: It is tempting to eliminate extra blanks occurring in whitespace in this
 * function as it could simplify some of the blanks logic in the node printing functions.
 * However, this would mess up any formatting done by the tabwriter. */

static void printer_trimmer_Write(void *self, const char *data, int len)
{
	printer_trimmer *p = self;
	/* invariants:
	 * p.state == inSpace:
	 *	p.space is unwritten
	 * p.state == inEscape, inText:
	 *	data[m:n] is unwritten */
	int m = 0;
	int n;
	for (n = 0; n < len; n++) {
		char b = data[n];
		if (b == '\v')
			b = '\t'; /* convert to htab */
		switch (p->state) {
		case printer_inSpace:
			switch (b) {
			case '\t':
			case ' ':
				buf_addc(&p->space, b);
				break;
			case '\n':
			case '\f':
				printer_trimmer_resetSpace(p); /* discard trailing space */
				p->output.Write(p->output.self, "\n", 1);
				break;
			case tabwriter_Escape:
				p->output.Write(p->output.self, p->space.p, (int)p->space.n);
				p->state = printer_inEscape;
				m = n + 1; /* +1: skip tabwriter.Escape */
				break;
			default:
				p->output.Write(p->output.self, p->space.p, (int)p->space.n);
				p->state = printer_inText;
				m = n;
				break;
			}
			break;
		case printer_inEscape:
			if (b == tabwriter_Escape) {
				p->output.Write(p->output.self, data + m, n - m);
				printer_trimmer_resetSpace(p);
			}
			break;
		case printer_inText:
			switch (b) {
			case '\t':
			case ' ':
				p->output.Write(p->output.self, data + m, n - m);
				printer_trimmer_resetSpace(p);
				buf_addc(&p->space, b);
				break;
			case '\n':
			case '\f':
				p->output.Write(p->output.self, data + m, n - m);
				printer_trimmer_resetSpace(p);
				p->output.Write(p->output.self, "\n", 1);
				break;
			case tabwriter_Escape:
				p->output.Write(p->output.self, data + m, n - m);
				p->state = printer_inEscape;
				m = n + 1; /* +1: skip tabwriter.Escape */
				break;
			}
			break;
		default:
			fail("go/printer: unreachable");
		}
	}
	n = len;

	switch (p->state) {
	case printer_inEscape:
	case printer_inText:
		p->output.Write(p->output.self, data + m, n - m);
		printer_trimmer_resetSpace(p);
		break;
	}
}

/* ---- Public interface ---- */

static printer_printer *printer_newPrinter(const printer_Config *cfg, token_FileSet *fset,
					   printer_nodeSizeMap *nodeSizes)
{
	printer_printer *p = nodeSizes->pool;
	Buf output = {0};
	IntVec goBuild = {0}, plusBuild = {0};
	if (p != NULL) {
		nodeSizes->pool = p->nextFree;
		output = p->output;
		output.n = 0;
		if (output.p)
			output.p[0] = 0;
		goBuild = (IntVec){p->goBuild.v, 0, p->goBuild.cap};
		plusBuild = (IntVec){p->plusBuild.v, 0, p->plusBuild.cap};
	} else {
		p = xalloc(sizeof *p);
	}
	*p = (printer_printer){
		.Config = *cfg,
		.fset = fset,
		.pos = {.Line = 1, .Column = 1},
		.out = {.Line = 1, .Column = 1},
		.nodeSizes = nodeSizes,
		.cachedPos = -1,
		.output = output,
		.goBuild = goBuild,
		.plusBuild = plusBuild,
	};
	return p;
}

static void printer_free(printer_printer *p)
{
	/* Hard limit on buffer size; see https://golang.org/issue/23199. */
	if (p->output.cap > 64 << 10)
		return;
	p->nextFree = p->nodeSizes->pool;
	p->nodeSizes->pool = p;
}

static void printer_bufWrite(void *self, const char *p, int n)
{
	if (n > 0)
		buf_add(self, p, (size_t)n);
}

static void printer_tabwriterWrite(void *self, const char *p, int n) { tabwriter_Writer_Write(self, p, n); }

/* fprint implements Fprint and takes a nodesSizes map for setting up the printer state. */
static const char *printer_Config_fprint(const printer_Config *cfg, io_Writer output, token_FileSet *fset,
					 ast_Node *node, printer_nodeSizeMap *nodeSizes)
{
	/* print node */
	printer_printer *p = printer_newPrinter(cfg, fset, nodeSizes);
	const char *err = printer_printNode(p, node);
	if (err != NULL) {
		printer_free(p);
		return err;
	}
	/* print outstanding comments */
	p->impliedSemi = false; /* EOF acts like a newline */
	bool wroteNewline, droppedFF;
	printer_flush(p, (token_Position){.Offset = printer_infinity, .Line = printer_infinity}, token_EOF,
		      &wroteNewline, &droppedFF);

	/* output is buffered in p.output now. fix //go:build and // +build comments if needed. */
	printer_fixGoBuildLines(p);

	/* redirect output through a trimmer to eliminate trailing whitespace (Input to a
	 * tabwriter must be untrimmed since trailing tabs provide formatting information. The
	 * tabwriter could provide trimming functionality but no tabwriter is used when RawFormat
	 * is set.) */
	printer_trimmer *trimmer = xalloc(sizeof *trimmer);
	trimmer->output = output;
	output = (io_Writer){printer_trimmer_Write, trimmer};

	/* redirect output through a tabwriter if necessary */
	tabwriter_Writer *tw = NULL;
	if ((cfg->Mode & printer_RawFormat) == 0) {
		int minwidth = cfg->Tabwidth;

		char padchar = '\t';
		if (cfg->Mode & printer_UseSpaces)
			padchar = ' ';

		unsigned twmode = tabwriter_DiscardEmptyColumns;
		if (cfg->Mode & printer_TabIndent) {
			minwidth = 0;
			twmode |= tabwriter_TabIndent;
		}

		tw = tabwriter_NewWriter(output, minwidth, cfg->Tabwidth, 1, padchar, twmode);
		output = (io_Writer){printer_tabwriterWrite, tw};
	}

	/* write printer result via tabwriter/trimmer to output */
	output.Write(output.self, p->output.p, (int)p->output.n);

	/* flush tabwriter, if any */
	if (tw != NULL)
		tabwriter_Writer_Flush(tw);

	printer_free(p);
	return NULL;
}

/* Fprint "pretty-prints" an AST node to output for a given configuration cfg. Position
 * information is interpreted relative to the file set fset. The node type must be an ast_File,
 * or an ast_Expr, ast_Decl, ast_Spec, or ast_Stmt. */
static const char *printer_Config_Fprint(const printer_Config *cfg, Buf *out, token_FileSet *fset, ast_Node *node)
{
	printer_nodeSizeMap *nodeSizes = xalloc(sizeof *nodeSizes);
	return printer_Config_fprint(cfg, (io_Writer){printer_bufWrite, out}, fset, node, nodeSizes);
}

/* not ported: the package-level Fprint (default Config{Tabwidth: 8}); use printer_Config_Fprint. */
