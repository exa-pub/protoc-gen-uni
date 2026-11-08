/* Copyright 2020 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.27.1 src/go/build/constraint/expr.go: only what go/printer needs (Parse,
 * IsGoBuild, IsPlusBuild, Expr.String, PlusBuildLines).
 * not ported: Expr.Eval, GoVersion (vers.go): go/printer does not use them. */
#include "../../../internal.h"

/* maxSize is a limit used to control the complexity of expressions, in order to prevent stack
 * exhaustion issues due to recursion. */
#define constraint_maxSize 1000

static constraint_Expr *constraint_newExpr(constraint_ExprKind kind)
{
	constraint_Expr *x = xalloc(sizeof *x);
	x->kind = kind;
	return x;
}

static constraint_Expr *constraint_tag(GoStr tag)
{
	constraint_Expr *x = constraint_newExpr(constraint_TagExprKind);
	x->Tag = tag;
	return x;
}

static constraint_Expr *constraint_not(constraint_Expr *x)
{
	constraint_Expr *n = constraint_newExpr(constraint_NotExprKind);
	n->X = x;
	return n;
}

static GoStr constraint_andArg(const constraint_Expr *x);
static GoStr constraint_orArg(const constraint_Expr *x);

static GoStr constraint_paren(GoStr s) { return gostr_cat(gostr_cat(GOSTR("("), s), GOSTR(")")); }

static GoStr constraint_Expr_String(const constraint_Expr *x)
{
	switch (x->kind) {
	case constraint_TagExprKind:
		return x->Tag;
	case constraint_NotExprKind: {
		GoStr s = constraint_Expr_String(x->X);
		if (x->X->kind == constraint_AndExprKind || x->X->kind == constraint_OrExprKind)
			s = constraint_paren(s);
		return gostr_cat(GOSTR("!"), s);
	}
	case constraint_AndExprKind:
		return gostr_cat(gostr_cat(constraint_andArg(x->X), GOSTR(" && ")), constraint_andArg(x->Y));
	case constraint_OrExprKind:
		return gostr_cat(gostr_cat(constraint_orArg(x->X), GOSTR(" || ")), constraint_orArg(x->Y));
	}
	fail("go/build/constraint: invalid Expr");
}

static GoStr constraint_andArg(const constraint_Expr *x)
{
	GoStr s = constraint_Expr_String(x);
	if (x->kind == constraint_OrExprKind)
		s = constraint_paren(s);
	return s;
}

static constraint_Expr *constraint_and(constraint_Expr *x, constraint_Expr *y)
{
	constraint_Expr *a = constraint_newExpr(constraint_AndExprKind);
	a->X = x;
	a->Y = y;
	return a;
}

static GoStr constraint_orArg(const constraint_Expr *x)
{
	GoStr s = constraint_Expr_String(x);
	if (x->kind == constraint_AndExprKind)
		s = constraint_paren(s);
	return s;
}

static constraint_Expr *constraint_or(constraint_Expr *x, constraint_Expr *y)
{
	constraint_Expr *o = constraint_newExpr(constraint_OrExprKind);
	o->X = x;
	o->Y = y;
	return o;
}

/* A SyntaxError reports a syntax error in a parsed build expression (Offset is not needed). */

#define constraint_errNotConstraint GOSTR("not a build constraint")

static bool constraint_splitGoBuild(GoStr line, GoStr *expr);
static constraint_Expr *constraint_parseExpr(GoStr text, GoStr *err);
static bool constraint_splitPlusBuild(GoStr line, GoStr *expr);
static constraint_Expr *constraint_parsePlusBuildExpr(GoStr text, GoStr *err);

static constraint_Expr *constraint_Parse(GoStr line, GoStr *err)
{
	GoStr text;
	if (constraint_splitGoBuild(line, &text))
		return constraint_parseExpr(text, err);
	if (constraint_splitPlusBuild(line, &text))
		return constraint_parsePlusBuildExpr(text, err);
	if (err)
		*err = constraint_errNotConstraint;
	return NULL;
}

/* IsGoBuild reports whether the line of text is a "//go:build" constraint. It only checks the
 * prefix of the text, not that the expression itself parses. */
static bool constraint_IsGoBuild(GoStr line)
{
	GoStr expr;
	return constraint_splitGoBuild(line, &expr);
}

/* splitGoBuild splits apart the leading //go:build prefix in line from the build expression
 * itself. It returns false if the input is not a //go:build line or if the input contains
 * multiple lines. */
static bool constraint_splitGoBuild(GoStr line, GoStr *expr)
{
	*expr = GOSTR("");
	/* A single trailing newline is OK; otherwise multiple lines are not. */
	if (line.n > 0 && line.p[line.n - 1] == '\n')
		line = gostr_sub(line, 0, line.n - 1);
	if (strings_Contains(line, GOSTR("\n")))
		return false;

	if (!strings_HasPrefix(line, GOSTR("//go:build")))
		return false;

	line = strings_TrimSpace(line);
	line = gostr_from(line, (int)sizeof("//go:build") - 1);

	/* If strings.TrimSpace finds more to trim after removing the //go:build prefix, it means
	 * that the prefix was followed by a space, making this a //go:build line (as opposed to a
	 * //go:buildsomethingelse line). If line is empty, we had "//go:build" by itself, which
	 * also counts. */
	GoStr trim = strings_TrimSpace(line);
	if (line.n == trim.n && line.n != 0)
		return false;

	*expr = trim;
	return true;
}

/* An exprParser holds state for parsing a build expression. */
typedef struct {
	GoStr s; /* input string */
	int i; /* next read location in s */

	GoStr tok; /* last token read */
	bool isTag;
	int pos; /* position (start) of last token */

	int size;

	/* panic(&SyntaxError{...}) -> longjmp to the innermost recover point */
	jmp_buf *recover;
	GoStr err;
	constraint_Expr *x; /* the named result x of parseExpr */
} constraint_exprParser;

NORETURN static void constraint_panic(constraint_exprParser *p, GoStr err)
{
	p->err = err;
	longjmp(*p->recover, 1);
}

static constraint_Expr *constraint_exprParser_or(constraint_exprParser *p);

/* parseExpr body (outside of the setjmp frame) */
static constraint_Expr *constraint_parseExpr1(constraint_exprParser *p)
{
	constraint_Expr *x = p->x = constraint_exprParser_or(p);
	if (p->tok.n != 0)
		constraint_panic(p, gostr_cat(GOSTR("unexpected token "), p->tok));
	return x;
}

/* parseExpr parses a boolean build tag expression. On a syntax error it returns the error and its
 * named result x: nil, or the expression parsed before an unexpected trailing token ("a b"
 * returns a), which fixGoBuildLines uses since it ignores the error. */
static constraint_Expr *constraint_parseExpr(GoStr text, GoStr *err)
{
	constraint_exprParser p = {.s = text};
	jmp_buf jb;
	p.recover = &jb;
	if (setjmp(jb)) {
		if (err)
			*err = p.err;
		return p.x;
	}
	return constraint_parseExpr1(&p);
}

static constraint_Expr *constraint_exprParser_and(constraint_exprParser *p);
static constraint_Expr *constraint_exprParser_not(constraint_exprParser *p);
static constraint_Expr *constraint_exprParser_atom(constraint_exprParser *p);
static void constraint_exprParser_lex(constraint_exprParser *p);

/* or parses a sequence of || expressions. On entry, the next input token has not yet been
 * lexed. On exit, the next input token has been lexed and is in p.tok. */
static constraint_Expr *constraint_exprParser_or(constraint_exprParser *p)
{
	constraint_Expr *x = constraint_exprParser_and(p);
	while (gostr_eqc(p->tok, "||"))
		x = constraint_or(x, constraint_exprParser_and(p));
	return x;
}

/* and parses a sequence of && expressions. */
static constraint_Expr *constraint_exprParser_and(constraint_exprParser *p)
{
	constraint_Expr *x = constraint_exprParser_not(p);
	while (gostr_eqc(p->tok, "&&"))
		x = constraint_and(x, constraint_exprParser_not(p));
	return x;
}

/* not parses a ! expression. */
static constraint_Expr *constraint_exprParser_not(constraint_exprParser *p)
{
	p->size++;
	if (p->size > constraint_maxSize)
		constraint_panic(p, GOSTR("build expression too large"));
	constraint_exprParser_lex(p);
	if (gostr_eqc(p->tok, "!")) {
		constraint_exprParser_lex(p);
		if (gostr_eqc(p->tok, "!"))
			constraint_panic(p, GOSTR("double negation not allowed"));
		return constraint_not(constraint_exprParser_atom(p));
	}
	return constraint_exprParser_atom(p);
}

/* the parenthesized expression of atom (outside of the setjmp frame) */
static constraint_Expr *constraint_exprParser_paren(constraint_exprParser *p)
{
	constraint_Expr *x = constraint_exprParser_or(p);
	if (!gostr_eqc(p->tok, ")"))
		constraint_panic(p, GOSTR("missing close paren"));
	constraint_exprParser_lex(p);
	return x;
}

/* atom parses a tag or a parenthesized expression. On entry, the next input token HAS been
 * lexed. On exit, the next input token has been lexed and is in p.tok. */
static constraint_Expr *constraint_exprParser_atom(constraint_exprParser *p)
{
	/* first token already in p.tok */
	if (gostr_eqc(p->tok, "(")) {
		/* deferred recover: "unexpected end of expression" becomes "missing close paren" */
		jmp_buf *outer = p->recover;
		jmp_buf jb;
		p->recover = &jb;
		if (setjmp(jb)) {
			p->recover = outer;
			if (gostr_eqc(p->err, "unexpected end of expression"))
				p->err = GOSTR("missing close paren");
			longjmp(*outer, 1);
		}
		constraint_Expr *x = constraint_exprParser_paren(p);
		p->recover = outer;
		return x;
	}

	if (!p->isTag) {
		if (p->tok.n == 0)
			constraint_panic(p, GOSTR("unexpected end of expression"));
		constraint_panic(p, gostr_cat(GOSTR("unexpected token "), p->tok));
	}
	GoStr tok = p->tok;
	constraint_exprParser_lex(p);
	return constraint_tag(tok);
}

static bool constraint_isTagRune(rune c) { return unicode_IsLetter(c) || unicode_IsDigit(c) || c == '_' || c == '.'; }

/* lex finds and consumes the next token in the input stream. On return, p.tok is set to the
 * token text, p.isTag reports whether the token was a tag, and p.pos records the byte offset of
 * the start of the token in the input stream. If lex reaches the end of the input, p.tok is set
 * to the empty string. For any other syntax error, lex panics with a SyntaxError. */
static void constraint_exprParser_lex(constraint_exprParser *p)
{
	p->isTag = false;
	while (p->i < p->s.n && (p->s.p[p->i] == ' ' || p->s.p[p->i] == '\t'))
		p->i++;
	if (p->i >= p->s.n) {
		p->tok = GOSTR("");
		p->pos = p->i;
		return;
	}
	switch (p->s.p[p->i]) {
	case '(':
	case ')':
	case '!':
		p->pos = p->i;
		p->i++;
		p->tok = gostr_sub(p->s, p->pos, p->i);
		return;

	case '&':
	case '|':
		if (p->i + 1 >= p->s.n || p->s.p[p->i + 1] != p->s.p[p->i])
			constraint_panic(p, gostr_cat(GOSTR("invalid syntax at "), gostr_sub(p->s, p->i, p->i + 1)));
		p->pos = p->i;
		p->i += 2;
		p->tok = gostr_sub(p->s, p->pos, p->i);
		return;
	}

	GoStr tag = gostr_from(p->s, p->i);
	for (int i = 0; i < tag.n;) {
		int size;
		rune c = utf8_DecodeRuneInString(gostr_from(tag, i), &size);
		if (!constraint_isTagRune(c)) {
			tag = gostr_sub(tag, 0, i);
			break;
		}
		i += size;
	}
	if (tag.n == 0) {
		rune c = utf8_DecodeRuneInString(gostr_from(p->s, p->i), NULL);
		Buf b = {0};
		buf_adds(&b, "invalid syntax at ");
		utf8_AppendRune(&b, c);
		constraint_panic(p, gostr_buf(&b));
	}

	p->pos = p->i;
	p->i += tag.n;
	p->tok = gostr_sub(p->s, p->pos, p->i);
	p->isTag = true;
}

/* IsPlusBuild reports whether the line of text is a "// +build" constraint. It only checks the
 * prefix of the text, not that the expression itself parses. */
static bool constraint_IsPlusBuild(GoStr line)
{
	GoStr expr;
	return constraint_splitPlusBuild(line, &expr);
}

/* splitPlusBuild splits apart the leading // +build prefix in line from the build expression
 * itself. It returns false if the input is not a // +build line or if the input contains
 * multiple lines. */
static bool constraint_splitPlusBuild(GoStr line, GoStr *expr)
{
	*expr = GOSTR("");
	/* A single trailing newline is OK; otherwise multiple lines are not. */
	if (line.n > 0 && line.p[line.n - 1] == '\n')
		line = gostr_sub(line, 0, line.n - 1);
	if (strings_Contains(line, GOSTR("\n")))
		return false;

	if (!strings_HasPrefix(line, GOSTR("//")))
		return false;
	line = gostr_from(line, 2);
	/* Note the space is optional; "//+build" is recognized too. */
	line = strings_TrimSpace(line);

	if (!strings_HasPrefix(line, GOSTR("+build")))
		return false;
	line = gostr_from(line, (int)sizeof("+build") - 1);

	/* If strings.TrimSpace finds more to trim after removing the +build prefix, it means that
	 * the prefix was followed by a space, making this a +build line (as opposed to a
	 * +buildsomethingelse line). If line is empty, we had "// +build" by itself, which also
	 * counts. */
	GoStr trim = strings_TrimSpace(line);
	if (line.n == trim.n && line.n != 0)
		return false;

	*expr = trim;
	return true;
}

static bool constraint_isValidTag(GoStr word);

#define constraint_errComplex GOSTR("expression too complex for // +build lines")

/* Go's strings.FieldsSeq: next field of s starting at *i (unicode.IsSpace separators). */
static bool constraint_nextField(GoStr s, int *i, GoStr *field)
{
	int size;
	while (*i < s.n && unicode_IsSpace(utf8_DecodeRuneInString(gostr_from(s, *i), &size)))
		*i += size;
	if (*i >= s.n)
		return false;
	int start = *i;
	while (*i < s.n && !unicode_IsSpace(utf8_DecodeRuneInString(gostr_from(s, *i), &size)))
		*i += size;
	*field = gostr_sub(s, start, *i);
	return true;
}

/* parsePlusBuildExpr parses a legacy build tag expression (as used with "// +build"). */
static constraint_Expr *constraint_parsePlusBuildExpr(GoStr text, GoStr *err)
{
	/* Only allow up to 100 AND/OR operators for "old" syntax. This is much less than the
	 * limit for "new" syntax, but uses of old syntax were always very simple. */
	const int maxOldSize = 100;
	int size = 0;

	constraint_Expr *x = NULL;
	GoStr clause;
	for (int fi = 0; constraint_nextField(text, &fi, &clause);) {
		constraint_Expr *y = NULL;
		GoStrVec lits = strings_Split(clause, GOSTR(","));
		for (int li = 0; li < lits.n; li++) {
			GoStr lit = lits.v[li];
			constraint_Expr *z;
			bool neg = false;
			if (strings_HasPrefix(lit, GOSTR("!!")) || gostr_eqc(lit, "!")) {
				z = constraint_tag(GOSTR("ignore"));
			} else {
				if (strings_HasPrefix(lit, GOSTR("!"))) {
					neg = true;
					lit = gostr_from(lit, 1);
				}
				if (constraint_isValidTag(lit))
					z = constraint_tag(lit);
				else
					z = constraint_tag(GOSTR("ignore"));
				if (neg)
					z = constraint_not(z);
			}
			if (y == NULL) {
				y = z;
			} else {
				if (++size > maxOldSize) {
					if (err)
						*err = constraint_errComplex;
					return NULL;
				}
				y = constraint_and(y, z);
			}
		}
		if (x == NULL) {
			x = y;
		} else {
			if (++size > maxOldSize) {
				if (err)
					*err = constraint_errComplex;
				return NULL;
			}
			x = constraint_or(x, y);
		}
	}
	if (x == NULL)
		x = constraint_tag(GOSTR("ignore"));
	return x;
}

/* isValidTag reports whether the word is a valid build tag. Tags must be letters, digits,
 * underscores or dots. Unlike in Go identifiers, all digits are fine (e.g., "386"). */
static bool constraint_isValidTag(GoStr word)
{
	if (word.n == 0)
		return false;
	for (int i = 0; i < word.n;) {
		int size;
		rune c = utf8_DecodeRuneInString(gostr_from(word, i), &size);
		if (!constraint_isTagRune(c))
			return false;
		i += size;
	}
	return true;
}

static constraint_Expr *constraint_pushNot(constraint_Expr *x, bool not_);
static void constraint_appendSplitAnd(Vec *list, constraint_Expr *x);
static void constraint_appendSplitOr(Vec *list, constraint_Expr *x);

/* PlusBuildLines returns a sequence of "// +build" lines that evaluate to the build expression
 * x. If the expression is too complex to convert directly to "// +build" lines, PlusBuildLines
 * returns an error. */
static bool constraint_PlusBuildLines(constraint_Expr *x, GoStrVec *lines, GoStr *err)
{
	/* Push all NOTs to the expression leaves, so that //go:build !(x && y) can be treated as
	 * !x || !y. */
	x = constraint_pushNot(x, false);

	/* Split into AND of ORs of ANDs of literals (tag or NOT tag). split: Vec of Vec (ors) of
	 * Vec (ands) of constraint_Expr *. */
	Vec split = {0};
	Vec ors = {0};
	constraint_appendSplitAnd(&ors, x);
	for (int i = 0; i < ors.n; i++) {
		Vec *ands = xalloc(sizeof *ands);
		Vec orl = {0};
		constraint_appendSplitOr(&orl, ors.v[i]);
		for (int j = 0; j < orl.n; j++) {
			Vec *lits = xalloc(sizeof *lits);
			Vec andl = {0};
			constraint_appendSplitAnd(&andl, orl.v[j]);
			for (int k = 0; k < andl.n; k++) {
				constraint_Expr *lit = andl.v[k];
				switch (lit->kind) {
				case constraint_TagExprKind:
				case constraint_NotExprKind:
					vec_push(lits, lit);
					break;
				default:
					*err = constraint_errComplex;
					return false;
				}
			}
			vec_push(ands, lits);
		}
		vec_push(&split, ands);
	}

	/* If all the ORs have length 1 (no actual OR'ing going on), push the top-level ANDs to the
	 * bottom level, so that we get one // +build line instead of many. */
	int maxOr = 0;
	for (int i = 0; i < split.n; i++) {
		Vec *or_ = split.v[i];
		if (maxOr < or_->n)
			maxOr = or_->n;
	}
	if (maxOr == 1) {
		Vec *lits = xalloc(sizeof *lits);
		for (int i = 0; i < split.n; i++) {
			Vec *first = ((Vec *)split.v[i])->v[0];
			for (int k = 0; k < first->n; k++)
				vec_push(lits, first->v[k]);
		}
		Vec *one = xalloc(sizeof *one);
		vec_push(one, lits);
		split = (Vec){0};
		vec_push(&split, one);
	}

	/* Prepare the +build lines. */
	*lines = (GoStrVec){0};
	for (int i = 0; i < split.n; i++) {
		Vec *or_ = split.v[i];
		Buf line = {0};
		buf_adds(&line, "// +build");
		for (int j = 0; j < or_->n; j++) {
			Vec *and_ = or_->v[j];
			buf_adds(&line, " ");
			for (int k = 0; k < and_->n; k++) {
				if (k > 0)
					buf_adds(&line, ",");
				buf_addg(&line, constraint_Expr_String(and_->v[k]));
			}
		}
		gostrvec_push(lines, gostr_buf(&line));
	}
	return true;
}

/* pushNot applies DeMorgan's law to push negations down the expression, so that only tags are
 * negated in the result. */
static constraint_Expr *constraint_pushNot(constraint_Expr *x, bool not_)
{
	switch (x->kind) {
	default:
		/* unreachable */
		return x;
	case constraint_NotExprKind:
		if (x->X->kind == constraint_TagExprKind && !not_)
			return x;
		return constraint_pushNot(x->X, !not_);
	case constraint_TagExprKind:
		if (not_)
			return constraint_not(x);
		return x;
	case constraint_AndExprKind: {
		constraint_Expr *x1 = constraint_pushNot(x->X, not_);
		constraint_Expr *y1 = constraint_pushNot(x->Y, not_);
		if (not_)
			return constraint_or(x1, y1);
		if (x1 == x->X && y1 == x->Y)
			return x;
		return constraint_and(x1, y1);
	}
	case constraint_OrExprKind: {
		constraint_Expr *x1 = constraint_pushNot(x->X, not_);
		constraint_Expr *y1 = constraint_pushNot(x->Y, not_);
		if (not_)
			return constraint_and(x1, y1);
		if (x1 == x->X && y1 == x->Y)
			return x;
		return constraint_or(x1, y1);
	}
	}
}

/* appendSplitAnd appends x to list while splitting apart any top-level && expressions. */
static void constraint_appendSplitAnd(Vec *list, constraint_Expr *x)
{
	if (x->kind == constraint_AndExprKind) {
		constraint_appendSplitAnd(list, x->X);
		constraint_appendSplitAnd(list, x->Y);
		return;
	}
	vec_push(list, x);
}

/* appendSplitOr appends x to list while splitting apart any top-level || expressions. */
static void constraint_appendSplitOr(Vec *list, constraint_Expr *x)
{
	if (x->kind == constraint_OrExprKind) {
		constraint_appendSplitOr(list, x->X);
		constraint_appendSplitOr(list, x->Y);
		return;
	}
	vec_push(list, x);
}
