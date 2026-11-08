/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/httprule/parse.go. Errors are their Go text (fmt's %q
 * of strings and runes is strconv.Quote/QuoteRune, go_std). */
#include "go_std.h"
#include "internal.h"

#include <string.h>

typedef struct {
	Str *v;
	int n, cap;
} strVec;

static void push(strVec *v, Str s)
{
	if (v->n == v->cap) {
		int cap = v->cap ? 2 * v->cap : 8;
		v->v = xrealloc(v->v, sizeof *v->v * (size_t)v->cap, sizeof *v->v * (size_t)cap);
		v->cap = cap;
	}
	v->v[v->n++] = s;
}

static Str q(Str s) /* %q */
{
	size_t n;
	char *p = go_std_quote(s.p, s.n, &n);
	return (Str){p, n};
}

static Str errf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static Str errf(const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	return buf_view(&b);
}

/* Concatenation of Str and C strings (errors with arbitrary bytes). */
static Str cat(Str a, const char *b, Str c)
{
	Buf o = {0};
	buf_add_str(&o, a);
	buf_adds(&o, b);
	buf_add_str(&o, c);
	return buf_view(&o);
}

/* strings.IndexAny for ASCII chars */
static long index_any(Str s, const char *chars)
{
	for (size_t i = 0; i < s.n; i++)
		if (s.p[i] && strchr(chars, s.p[i]))
			return (long)i;
	return -1;
}

static Str sub(Str s, size_t i, size_t j) { return (Str){s.p + i, j - i}; }

/* utf8 decoding as `for _, r := range s` does (U+FFFD for invalid bytes, width 1). */
static int next_rune(Str s, size_t i, uint32_t *r) { return utf8_decode(s.p + i, s.n - i, r); }

/* %c of a rune */
static Str rune_c(uint32_t r)
{
	char *p = xalloc(5);
	if (r > 0x10FFFF || (r >= 0xD800 && r <= 0xDFFF))
		r = 0xFFFD;
	int n;
	if (r < 0x80) {
		p[0] = (char)r;
		n = 1;
	} else if (r < 0x800) {
		p[0] = (char)(0xC0 | (r >> 6));
		p[1] = (char)(0x80 | (r & 0x3F));
		n = 2;
	} else if (r < 0x10000) {
		p[0] = (char)(0xE0 | (r >> 12));
		p[1] = (char)(0x80 | ((r >> 6) & 0x3F));
		p[2] = (char)(0x80 | (r & 0x3F));
		n = 3;
	} else {
		p[0] = (char)(0xF0 | (r >> 18));
		p[1] = (char)(0x80 | ((r >> 12) & 0x3F));
		p[2] = (char)(0x80 | ((r >> 6) & 0x3F));
		p[3] = (char)(0x80 | (r & 0x3F));
		n = 4;
	}
	return (Str){p, (size_t)n};
}

/* %U of a rune: "U+" and at least four upper case hex digits */
static const char *rune_U(uint32_t r)
{
	char t[16];
	int n = 0;
	do
		t[n++] = "0123456789ABCDEF"[r % 16];
	while ((r /= 16));
	while (n < 4)
		t[n++] = '0';
	char *s = xalloc((size_t)n + 3);
	s[0] = 'U', s[1] = '+';
	for (int i = 0; i < n; i++)
		s[2 + i] = t[n - 1 - i];
	return s;
}

/* tokenize */
static void tokenize(Str path, strVec *tokens, Str *verb)
{
	*verb = (Str){"", 0};
	if (path.n == 0) {
		push(tokens, HTTPRULE_EOF);
		return;
	}

	enum { init, field, nested };
	int st = init;
	while (path.n) {
		long idx;
		switch (st) {
		case init:
			idx = index_any(path, "/{");
			break;
		case field:
			idx = index_any(path, ".=}");
			break;
		default:
			idx = index_any(path, "/}");
			break;
		}
		if (idx < 0) {
			push(tokens, path);
			break;
		}
		switch (path.p[idx]) {
		case '/':
		case '.':
			break;
		case '{':
			st = field;
			break;
		case '=':
			st = nested;
			break;
		case '}':
			st = init;
			break;
		}
		if (idx == 0) {
			push(tokens, sub(path, (size_t)idx, (size_t)idx + 1));
		} else {
			push(tokens, sub(path, 0, (size_t)idx));
			push(tokens, sub(path, (size_t)idx, (size_t)idx + 1));
		}
		path = sub(path, (size_t)idx + 1, path.n);
	}

	int l = tokens->n;
	/* See https://github.com/grpc-ecosystem/grpc-gateway/pull/1947#issuecomment-774523693 ;
	 * although normal and backwards-compat logic here is to use the last index of a colon, if
	 * the final segment is a variable followed by a colon, the part following the colon must be
	 * a verb. Hence if the previous token is an end var marker, we switch the index we're
	 * looking for to Index instead of LastIndex, so that we correctly grab the remaining part of
	 * the path as the verb. */
	bool penultimateTokenIsEndVar = false;
	if (l >= 2)
		penultimateTokenIsEndVar = str_eqc(tokens->v[l - 2], "}");
	Str t = tokens->v[l - 1];
	long idx = -1;
	if (penultimateTokenIsEndVar) {
		const char *c = memchr(t.p, ':', t.n);
		idx = c ? c - t.p : -1;
	} else {
		for (size_t i = t.n; i > 0; i--)
			if (t.p[i - 1] == ':') {
				idx = (long)i - 1;
				break;
			}
	}
	if (idx == 0) {
		tokens->n = l - 1;
		*verb = sub(t, 1, t.n);
	} else if (idx > 0) {
		tokens->v[l - 1] = sub(t, 0, (size_t)idx);
		*verb = sub(t, (size_t)idx + 1, t.n);
	}
	push(tokens, HTTPRULE_EOF);
}

/* parser is a parser of the template syntax defined in
 * github.com/googleapis/googleapis/google/api/http.proto. */
typedef struct {
	Str *tokens;
	int ntokens;
	strVec accepted;
} parser;

/* A termType is a type of terminal symbols: "/", "*", "**", ".", "=", "{", "}" or one of: */
#define typeIdent "ident"
#define typeLiteral "literal"
#define typeEOF "$"

static Str expectPChars(Str t);
static Str expectIdent(Str ident);

/* accept tries to accept a token in "p". This function consumes a token and returns it if it
 * matches to the specified "term". If it doesn't match, the function does not consume any tokens
 * and return an error (err.p != NULL). */
static Str accept(parser *p, const char *term, Str *err)
{
	Str t = p->tokens[0];
	*err = (Str){NULL, 0};
	if (strchr("/*.={}", term[0]) && (!term[1] || str_equal(str_c(term), STR_LIT("**")))) {
		if (!str_eqc(t, term) && !str_eqc(t, "/")) {
			*err = cat(cat(STR_LIT("expected "), "", q(str_c(term))), " but got ", q(t));
			return t;
		}
	} else if (strcmp(term, typeEOF) == 0) {
		if (!str_equal(t, HTTPRULE_EOF)) {
			*err = cat(STR_LIT("expected EOF but got "), "", q(t));
			return t;
		}
	} else if (strcmp(term, typeIdent) == 0) {
		Str e = expectIdent(t);
		if (e.p) {
			*err = e;
			return t;
		}
	} else {
		Str e = expectPChars(t);
		if (e.p) {
			*err = e;
			return t;
		}
	}
	p->tokens++;
	p->ntokens--;
	push(&p->accepted, t);
	return t;
}

static Str segments(parser *p, segment ***out, int *nout);

static segment *new_seg(int kind)
{
	segment *s = xalloc(sizeof *s);
	s->kind = kind;
	return s;
}

static Str fieldPath(parser *p, Str *path)
{
	Str err;
	Str c = accept(p, typeIdent, &err);
	if (err.p)
		return err;
	Buf b = {0};
	buf_add_str(&b, c);
	for (;;) {
		accept(p, ".", &err);
		if (err.p) {
			*path = buf_view(&b);
			return (Str){NULL, 0};
		}
		c = accept(p, typeIdent, &err);
		if (err.p)
			return cat(STR_LIT("invalid field path component: "), "", err);
		buf_addc(&b, '.');
		buf_add_str(&b, c);
	}
}

static Str variable(parser *p, segment **out)
{
	Str err;
	accept(p, "{", &err);
	if (err.p)
		return err;

	Str path;
	err = fieldPath(p, &path);
	if (err.p)
		return err;

	segment *v = new_seg(SEG_VARIABLE);
	v->path = path;
	accept(p, "=", &err);
	if (!err.p) {
		err = segments(p, &v->segs, &v->nsegs);
		if (err.p)
			return cat(cat(STR_LIT("invalid segment in variable "), "", q(path)), ": ", err);
	} else {
		v->segs = xalloc(sizeof *v->segs);
		v->segs[0] = new_seg(SEG_WILDCARD);
		v->nsegs = 1;
	}

	accept(p, "}", &err);
	if (err.p)
		return cat(STR_LIT("unterminated variable segment: "), "", path);
	*out = v;
	return (Str){NULL, 0};
}

static Str segment_(parser *p, segment **out)
{
	Str err;
	accept(p, "*", &err);
	if (!err.p) {
		*out = new_seg(SEG_WILDCARD);
		return err;
	}
	accept(p, "**", &err);
	if (!err.p) {
		*out = new_seg(SEG_DEEP_WILDCARD);
		return err;
	}
	Str lit = accept(p, typeLiteral, &err);
	if (!err.p) {
		*out = new_seg(SEG_LITERAL);
		(*out)->lit = lit;
		return err;
	}
	err = variable(p, out);
	if (err.p)
		return cat(STR_LIT("segment neither wildcards, literal or variable: "), "", err);
	return err;
}

static Str segments(parser *p, segment ***out, int *nout)
{
	Vec segs = {0};
	segment *s;
	Str err = segment_(p, &s);
	if (err.p)
		return err;
	vec_push(&segs, s);
	for (;;) {
		accept(p, "/", &err);
		if (err.p) {
			err = (Str){NULL, 0};
			break;
		}
		err = segment_(p, &s);
		if (err.p)
			break;
		vec_push(&segs, s);
	}
	*out = (segment **)segs.v;
	*nout = segs.n;
	return err;
}

/* topLevelSegments is the target of this parser. */
static Str topLevelSegments(parser *p, segment ***out, int *nout)
{
	Str err;
	accept(p, typeEOF, &err);
	if (!err.p) {
		p->ntokens = 0;
		*out = xalloc(sizeof **out);
		(*out)[0] = new_seg(SEG_LITERAL);
		(*out)[0]->lit = HTTPRULE_EOF;
		*nout = 1;
		return err;
	}
	err = segments(p, out, nout);
	if (err.p)
		return err;
	accept(p, typeEOF, &err);
	if (err.p) {
		Buf b = {0};
		for (int i = 0; i < p->accepted.n; i++)
			buf_add_str(&b, p->accepted.v[i]);
		return cat(cat(STR_LIT("unexpected token "), "", q(p->tokens[0])), " after segments ", q(buf_view(&b)));
	}
	return err;
}

static bool isHexDigit(uint32_t r)
{
	return ('0' <= r && r <= '9') || ('A' <= r && r <= 'F') || ('a' <= r && r <= 'f');
}

/* expectPChars determines if "t" consists of only pchars defined in RFC3986.
 *
 * https://www.ietf.org/rfc/rfc3986.txt, P.49
 *
 *	pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
 *	unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
 *	sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
 *	              / "*" / "+" / "," / ";" / "="
 *	pct-encoded   = "%" HEXDIG HEXDIG */
static Str expectPChars(Str t)
{
	enum { init, pct1, pct2 };
	int st = init;
	for (size_t i = 0; i < t.n;) {
		uint32_t r;
		i += (size_t)next_rune(t, i, &r);
		if (st != init) {
			if (!isHexDigit(r))
				return cat(cat(STR_LIT("invalid hexdigit: "), "", rune_c(r)), xfmt("(%s)", rune_U(r)), (Str){"", 0});
			st = st == pct1 ? pct2 : init;
			continue;
		}

		/* unreserved */
		if (('A' <= r && r <= 'Z') || ('a' <= r && r <= 'z') || ('0' <= r && r <= '9'))
			continue;
		switch (r) {
		case '-': case '.': case '_': case '~':
			/* unreserved */
			break;
		case '!': case '$': case '&': case '\'': case '(': case ')': case '*': case '+': case ',': case ';': case '=':
			/* sub-delims */
			break;
		case ':': case '@':
			/* rest of pchar */
			break;
		case '%':
			/* pct-encoded */
			st = pct1;
			break;
		default:
			return errf("invalid character in path segment: %s(%s)", go_std_quote_rune((int32_t)r), rune_U(r));
		}
	}
	if (st != init)
		return cat(STR_LIT("invalid percent-encoding in "), "", q(t));
	return (Str){NULL, 0};
}

/* expectIdent determines if "ident" is a valid identifier in .proto schema
 * ([[:alpha:]_][[:alphanum:]_]*). */
static Str expectIdent(Str ident)
{
	if (ident.n == 0)
		return STR_LIT("empty identifier");
	for (size_t pos = 0; pos < ident.n;) {
		uint32_t r;
		size_t at = pos;
		pos += (size_t)next_rune(ident, pos, &r);
		if ('0' <= r && r <= '9') {
			if (at == 0)
				return cat(STR_LIT("identifier starting with digit: "), "", ident);
			continue;
		}
		if (('A' <= r && r <= 'Z') || ('a' <= r && r <= 'z') || r == '_')
			continue;
		return cat(STR_LIT(""), xfmt("invalid character %s(%s) in identifier: ", go_std_quote_rune((int32_t)r), rune_U(r)), ident);
	}
	return (Str){NULL, 0};
}

static GwHttpruleTemplate *parse(Str tmpl, Str *err)
{
	if (!str_has_prefix(tmpl, STR_LIT("/"))) {
		*err = cat(STR_LIT("no leading /: "), "", tmpl);
		return NULL;
	}
	strVec tokens = {0};
	Str verb;
	tokenize(sub(tmpl, 1, tmpl.n), &tokens, &verb);

	parser p = {tokens.v, tokens.n, {0}};
	GwHttpruleTemplate *t = xalloc(sizeof *t);
	Str e = topLevelSegments(&p, &t->segments, &t->nsegments);
	if (e.p) {
		*err = cat(e, ": ", tmpl);
		return NULL;
	}
	e = expectPChars(verb);
	if (e.p) {
		*err = cat(cat(STR_LIT("invalid verb: "), "", e), ": ", tmpl);
		return NULL;
	}
	t->verb = verb;
	t->tmpl = tmpl;
	return t;
}

/* Parse parses the string representation of path template */
GwHttpruleTemplate *gw_httprule_Parse(Str tmpl, Str *err) { return parse(tmpl, err); }

/* SplitTemplate parses the string representation of path template and returns its top-level
 * segments and its verb. The root template "/" has no segments. */
bool gw_httprule_SplitTemplate(Str tmpl, GwSegment **segments_out, int *nsegments, Str *verb, Str *err)
{
	GwHttpruleTemplate *t = parse(tmpl, err);
	if (!t)
		return false;
	GwSegment *segs = xalloc(sizeof *segs * (size_t)t->nsegments);
	int n = 0;
	for (int i = 0; i < t->nsegments; i++) {
		const segment *s = t->segments[i];
		switch (s->kind) {
		case SEG_LITERAL:
			if (str_equal(s->lit, HTTPRULE_EOF))
				continue;
			segs[n++] = (GwSegment){{"", 0}, s->lit};
			break;
		case SEG_VARIABLE: {
			Buf b = {0};
			for (int k = 0; k < s->nsegs; k++) {
				if (k)
					buf_addc(&b, '/');
				buf_add_str(&b, gw_httprule_segment_String(s->segs[k]));
			}
			segs[n++] = (GwSegment){s->path, b.n ? buf_view(&b) : (Str){"", 0}};
			break;
		}
		default:
			segs[n++] = (GwSegment){{"", 0}, gw_httprule_segment_String(s)};
			break;
		}
	}
	*segments_out = n ? segs : NULL;
	*nsegments = n;
	*verb = t->verb;
	return true;
}
