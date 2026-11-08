/* Parts of syn 2.0.109 (https://github.com/dtolnay/syn, MIT OR Apache-2.0, Copyright David Tolnay):
 * Attribute::parse_outer and Meta (src/attr.rs), the attributes of DeriveInput (src/derive.rs),
 * Path, PathSegment and GenericArgument (src/path.rs), the Type variants of README.md (src/ty.rs),
 * Lit and LitStr::value (src/lit.rs) and Ident::parse (src/ident.rs, accept_as_ident). Tokens come
 * from the proc-macro2 lexer (pm2_token_stream); the nodes are those of prettyplease.h. */
#include "syn.h"

#include "proc_macro2.h"

#include <string.h>

#define UNSUPPORTED "unsupported by protoc-gen-uni: "
#define MAX_DEPTH 64

typedef struct {
	const PpList *l;
	int i;
	Str src;          /* for errors */
	const char *what; /* "attribute" or "type" */
} In;

static NORETURN COLD void unsupported(const In *in)
{
	fail(UNSUPPORTED "Rust %s %s (README.md of pkg/languages/rust/syn lists what is parsed)", in->what,
	     quote_json(in->src));
}

static const PpNode *tok(const In *in, int k)
{
	int j = in->i + k;
	return j < in->l->n ? in->l->v[j] : NULL;
}

static bool is_punct(const PpNode *t, char c) { return t && t->kind == PP_TT_PUNCT && t->aux == (unsigned char)c; }
static bool is_word(const PpNode *t, const char *s) { return t && t->kind == PP_TT_IDENT && str_eq(t->text, s); }
static bool is_group(const PpNode *t, int delim) { return t && t->kind == PP_TT_GROUP && t->aux == delim; }

/* Token![::] */
static bool peek_colon2(const In *in, int k)
{
	const PpNode *a = tok(in, k);
	return is_punct(a, ':') && (a->flags & PP_F_JOINT) && is_punct(tok(in, k + 1), ':');
}

/* ident.rs accept_as_ident: identifiers that are not keywords (raw identifiers are). */
static bool accept_as_ident(const char *s)
{
	static const char *const keywords[] = {
		"_", "abstract", "as", "async", "await", "become", "box", "break", "const", "continue", "crate", "do",
		"dyn", "else", "enum", "extern", "false", "final", "fn", "for", "if", "impl", "in", "let", "loop",
		"macro", "match", "mod", "move", "mut", "override", "priv", "pub", "ref", "return", "Self", "self",
		"static", "struct", "super", "trait", "true", "try", "type", "typeof", "unsafe", "unsized", "use",
		"virtual", "where", "while", "yield"};
	for (size_t i = 0; i < sizeof keywords / sizeof *keywords; i++)
		if (str_eq(s, keywords[i]))
			return false;
	return true;
}

static bool is_path_keyword(const char *s)
{
	return str_eq(s, "super") || str_eq(s, "self") || str_eq(s, "Self") || str_eq(s, "crate") || str_eq(s, "try");
}

/* A path segment identifier: Ident, or a keyword that PathSegment accepts. */
static bool peek_segment(const In *in)
{
	const PpNode *t = tok(in, 0);
	return t && t->kind == PP_TT_IDENT && (accept_as_ident(t->text) || is_path_keyword(t->text));
}

/* Lifetime: `'` (joint) then an identifier. */
static bool peek_lifetime(const In *in, int k)
{
	const PpNode *t = tok(in, k), *u = tok(in, k + 1);
	return is_punct(t, '\'') && (t->flags & PP_F_JOINT) && u && u->kind == PP_TT_IDENT;
}

static PpNode *parse_type(In *in, int depth);

/* AngleBracketedGenericArguments after `<`: lifetimes and types. */
static PpNode *parse_angle_args(In *in, int depth)
{
	PpNode *args = pp_node(PP_ANGLE_ARGS);
	for (;;) {
		if (is_punct(tok(in, 0), '>'))
			break;
		if (peek_lifetime(in, 0) && !is_punct(tok(in, 2), '+')) {
			pp_push(args, pp_lifetime(xcat("'", tok(in, 1)->text)));
			in->i += 2;
		} else {
			pp_push(args, parse_type(in, depth + 1));
			const PpNode *t = tok(in, 0);
			if (is_punct(t, '=') || (is_punct(t, ':') && !peek_colon2(in, 0)))
				unsupported(in); /* associated types and constraints */
		}
		if (is_punct(tok(in, 0), '>'))
			break;
		if (!is_punct(tok(in, 0), ','))
			unsupported(in);
		in->i++;
	}
	in->i++;
	return args;
}

/* Path::parse (expr_style false) and Path::parse_mod_style (no arguments). */
static PpNode *parse_path(In *in, bool mod_style, int depth)
{
	PpNode *path = pp_node(PP_PATH);
	if (peek_colon2(in, 0)) {
		path->flags |= PP_F_LEADING_COLON;
		in->i += 2;
	}
	for (;;) {
		if (!peek_segment(in))
			unsupported(in);
		const char *ident = tok(in, 0)->text;
		in->i++;
		PpNode *args = NULL;
		if (!mod_style && !(is_path_keyword(ident) && !str_eq(ident, "Self"))) {
			const PpNode *t = tok(in, 0);
			bool lt = is_punct(t, '<') && !((t->flags & PP_F_JOINT) && is_punct(tok(in, 1), '='));
			if (lt || (peek_colon2(in, 0) && is_punct(tok(in, 2), '<'))) {
				in->i += lt ? 1 : 3;
				args = parse_angle_args(in, depth);
			}
		}
		pp_path_push(path, ident, args);
		if (!peek_colon2(in, 0))
			return path;
		in->i += 2;
	}
}

/* A group's content as its own input. */
static In sub(const In *in, const PpNode *group) { return (In){&group->list, 0, in->src, in->what}; }

static bool at_end(const In *in) { return in->i >= in->l->n; }

/* ty.rs ambig_ty, for the README subset. */
static PpNode *parse_type(In *in, int depth)
{
	if (depth > MAX_DEPTH)
		unsupported(in);
	const PpNode *t = tok(in, 0);
	if (is_group(t, PP_DELIM_PAREN)) {
		in->i++;
		In c = sub(in, t);
		PpNode *tuple = pp_ty_tuple(NULL);
		if (at_end(&c))
			return tuple;
		PpNode *first = parse_type(&c, depth + 1);
		if (at_end(&c))
			return pp_ty_paren(first);
		pp_push(tuple, first);
		while (!at_end(&c)) {
			if (!is_punct(tok(&c, 0), ','))
				unsupported(in);
			c.i++;
			if (at_end(&c))
				break;
			pp_push(tuple, parse_type(&c, depth + 1));
		}
		return tuple;
	}
	if (is_punct(t, '&')) {
		in->i++;
		const char *lifetime = NULL;
		if (peek_lifetime(in, 0)) {
			lifetime = xcat("'", tok(in, 1)->text);
			in->i += 2;
		}
		bool mut = is_word(tok(in, 0), "mut");
		in->i += mut;
		return pp_ty_ref(lifetime, mut, parse_type(in, depth + 1));
	}
	if (is_punct(t, '*')) {
		bool mut = is_word(tok(in, 1), "mut");
		if (!mut && !is_word(tok(in, 1), "const"))
			unsupported(in);
		in->i += 2;
		return pp_ty_ptr(mut, parse_type(in, depth + 1));
	}
	if (is_group(t, PP_DELIM_BRACKET)) {
		in->i++;
		In c = sub(in, t);
		PpNode *elem = parse_type(&c, depth + 1);
		if (at_end(&c))
			return pp_ty_slice(elem);
		const PpNode *len = tok(&c, 1);
		if (!is_punct(tok(&c, 0), ';') || !len || len->kind != PP_TT_LIT || c.i + 2 != c.l->n ||
		    !(len->text[0] >= '0' && len->text[0] <= '9'))
			unsupported(in);
		return pp_ty_array(elem, pp_e_lit_int(len->text));
	}
	if (is_punct(t, '!')) {
		in->i++;
		return pp_ty_never();
	}
	if (peek_colon2(in, 0) || peek_segment(in))
		return pp_ty_path(parse_path(in, false, depth));
	unsupported(in);
}

/* ---- lit.rs ---- */

static void add_utf8(Buf *b, uint32_t c)
{
	if (c < 0x80) {
		buf_addc(b, (char)c);
	} else if (c < 0x800) {
		buf_addc(b, (char)(0xc0 | c >> 6));
		buf_addc(b, (char)(0x80 | (c & 0x3f)));
	} else if (c < 0x10000) {
		buf_addc(b, (char)(0xe0 | c >> 12));
		buf_addc(b, (char)(0x80 | (c >> 6 & 0x3f)));
		buf_addc(b, (char)(0x80 | (c & 0x3f)));
	} else {
		buf_addc(b, (char)(0xf0 | c >> 18));
		buf_addc(b, (char)(0x80 | (c >> 12 & 0x3f)));
		buf_addc(b, (char)(0x80 | (c >> 6 & 0x3f)));
		buf_addc(b, (char)(0x80 | (c & 0x3f)));
	}
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	return (c | 0x20) - 'a' + 10;
}

/* parse_lit_str: the value of a string literal that the lexer accepted. */
static Str lit_str_value(const char *repr)
{
	Buf b = {0};
	if (repr[0] == 'r') {
		const char *s = repr + 1;
		size_t pounds = strspn(s, "#");
		const char *close = strrchr(s, '"');
		buf_add(&b, s + pounds + 1, (size_t)(close - (s + pounds + 1)));
		return buf_view(&b);
	}
	const char *s = repr + 1;
	while (*s != '"') {
		if (*s == '\\') {
			char e = s[1];
			s += 2;
			switch (e) {
			case 'x':
				buf_addc(&b, (char)(hexval(s[0]) * 16 + hexval(s[1])));
				s += 2;
				break;
			case 'u': {
				uint32_t c = 0;
				for (s++; *s != '}'; s++)
					if (*s != '_')
						c = c * 16 + (uint32_t)hexval(*s);
				s++;
				add_utf8(&b, c);
				break;
			}
			case 'n': buf_addc(&b, '\n'); break;
			case 'r': buf_addc(&b, '\r'); break;
			case 't': buf_addc(&b, '\t'); break;
			case '0': buf_addc(&b, '\0'); break;
			case '\r':
			case '\n':
				while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
					s++;
				break;
			default: buf_addc(&b, e); /* \\ \' \" */
			}
		} else if (*s == '\r' && s[1] == '\n') {
			buf_addc(&b, '\n');
			s += 2;
		} else {
			buf_addc(&b, *s++);
		}
	}
	return buf_view(&b);
}

/* Lit::new: the literal node of a literal token (repr). */
static PpNode *lit_node(const In *in, const char *repr)
{
	char c = repr[0] == '-' ? repr[1] : repr[0];
	if (c == '"' || (c == 'r' && (repr[1] == '"' || repr[1] == '#')))
		return pp_lit_str_repr(repr, lit_str_value(repr));
	int kind;
	if (c == 'b')
		kind = repr[1] == '\'' ? PP_LIT_BYTE : PP_LIT_BYTE_STR;
	else if (c == 'c')
		kind = PP_LIT_C_STR;
	else if (c == '\'')
		kind = PP_LIT_CHAR;
	else if (c >= '0' && c <= '9')
		kind = (strpbrk(repr, ".eE") && !strstr(repr, "0x") && !strstr(repr, "0b")) || strstr(repr, "f32") ||
				       strstr(repr, "f64")
			       ? PP_LIT_FLOAT
			       : PP_LIT_INT;
	else
		unsupported(in); /* the ERROR literal of proc-macro2 */
	return pp_lit(kind, repr);
}

/* ---- attr.rs ---- */

/* parse_meta_name_value_after_path: a literal, or an expression that is a path. */
static PpNode *parse_meta_value(In *c)
{
	const PpNode *t = tok(c, 0);
	if (t && t->kind == PP_TT_LIT && c->i + 1 == c->l->n) {
		c->i++;
		return pp_e_lit(lit_node(c, t->text));
	}
	const PpNode *u = tok(c, 1);
	if (is_punct(t, '-') && u && u->kind == PP_TT_LIT && u->text[0] >= '0' && u->text[0] <= '9' &&
	    c->i + 2 == c->l->n) {
		c->i += 2;
		return pp_e_lit(lit_node(c, xcat("-", u->text)));
	}
	if ((is_word(t, "true") || is_word(t, "false")) && c->i + 1 == c->l->n) {
		c->i++;
		return pp_e_lit_bool(str_eq(t->text, "true"));
	}
	PpNode *path = parse_path(c, true, 0);
	return pp_e_path(path);
}

/* single_parse_outer after `#`: the bracketed Meta. */
static PpNode *parse_meta(In *in, const PpNode *group)
{
	In c = sub(in, group);
	PpNode *path;
	if (is_word(tok(&c, 0), "unsafe")) {
		path = pp_path_ident("unsafe");
		c.i++;
	} else {
		path = parse_path(&c, true, 0);
	}
	const PpNode *t = tok(&c, 0);
	PpNode *attr;
	if (t && t->kind == PP_TT_GROUP && t->aux != PP_DELIM_NONE) {
		attr = pp_attr_list(path, t->aux, false);
		attr->list = t->list;
		c.i++;
	} else if (is_punct(t, '=')) {
		c.i++;
		attr = pp_attr_name_value(path, parse_meta_value(&c), false);
	} else {
		attr = pp_attr_path(path, false);
	}
	if (!at_end(&c))
		unsupported(in);
	return attr;
}

void syn_parse_attrs_of_fake_struct(Str attr, Vec *attrs)
{
	Str src = str_c(xcat(xstrndup(attr.p, attr.n), "\nstruct fake;"));
	PpNode *tokens = pp_tt_group(PP_DELIM_NONE);
	if (!pm2_token_stream(src, tokens))
		fail("called `Result::unwrap()` on an `Err` value: Error(\"lex error\")");
	In in = {&tokens->list, 0, attr, "attribute"};
	while (is_punct(tok(&in, 0), '#')) {
		const PpNode *g = tok(&in, 1);
		if (!is_group(g, PP_DELIM_BRACKET))
			unsupported(&in);
		vec_push(attrs, parse_meta(&in, g));
		in.i += 2;
	}
	if (!(is_word(tok(&in, 0), "struct") && is_word(tok(&in, 1), "fake") && is_punct(tok(&in, 2), ';') &&
	      in.i + 3 == in.l->n))
		unsupported(&in);
}

PpNode *syn_parse_type(Str src, bool path_only)
{
	PpNode *tokens = pp_tt_group(PP_DELIM_NONE);
	if (!pm2_token_stream(src, tokens))
		fail(path_only ? "called `Result::unwrap()` on an `Err` value: Error(\"lex error\")"
			       : "called `Result::unwrap()` on an `Err` value: LexError { span: Span }");
	In in = {&tokens->list, 0, src, "type"};
	PpNode *ty = path_only ? pp_ty_path(parse_path(&in, false, 0)) : parse_type(&in, 0);
	if (!at_end(&in))
		unsupported(&in);
	return ty;
}
