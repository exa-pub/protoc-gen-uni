/* Port of proc-macro2 1.0.103 src/parse.rs (Copyright David Tolnay, MIT OR Apache-2.0): the
 * fallback lexer behind `TokenStream::from_str` outside a procedural macro, which `str::parse` and
 * `syn::parse_str` run over text that comes from plugin options. Tokens are the token tree nodes of
 * prettyplease.h.
 *
 * Identifiers are Unicode XID upstream; the port classifies ASCII exactly and fails with
 * "unsupported by protoc-gen-uni" where a non-ASCII char would be classified as an identifier char
 * (white space, strings and comments may hold any char). The input is valid UTF-8 (prost checks the
 * request). src/parse.rs is the same in 1.0.106 (buffa) but for imports. */
#include "proc_macro2.h"

#include "prettyplease.h"
#include "rust_std.h"

#include <string.h>

typedef struct {
	const char *p, *end;
} Cur;

static bool starts(Cur c, const char *s)
{
	size_t n = strlen(s);
	return (size_t)(c.end - c.p) >= n && !memcmp(c.p, s, n);
}

static int peekb(Cur c, size_t i) { return c.p + i < c.end ? (unsigned char)c.p[i] : -1; }

static NORETURN COLD void non_ascii_ident(Cur c)
{
	fail("unsupported by protoc-gen-uni: non-ASCII identifier character in Rust tokens %s",
	     quote_json((Str){c.p, (size_t)(c.end - c.p)}));
}

/* is_ident_start / is_ident_continue of the char at c (false at the end). */
static bool ident_start(Cur c)
{
	int b = peekb(c, 0);
	if (b >= 0x80)
		non_ascii_ident(c);
	return b == '_' || ((b | 0x20) >= 'a' && (b | 0x20) <= 'z');
}

static bool ident_continue(Cur c)
{
	int b = peekb(c, 0);
	return ident_start(c) || (b >= '0' && b <= '9');
}

static bool is_whitespace(uint32_t ch) { return rust_is_whitespace(ch) || ch == 0x200e || ch == 0x200f; }

/* take_until_newline_or_eof: *s is the text before the newline. */
static Cur take_until_newline_or_eof(Cur in, Str *s)
{
	for (const char *q = in.p; q < in.end; q++) {
		if (*q == '\n') {
			*s = (Str){in.p, (size_t)(q - in.p)};
			return (Cur){q, in.end};
		}
		if (*q == '\r' && q + 1 < in.end && q[1] == '\n') {
			*s = (Str){in.p, (size_t)(q - in.p)};
			return (Cur){q + 1, in.end};
		}
	}
	*s = (Str){in.p, (size_t)(in.end - in.p)};
	return (Cur){in.end, in.end};
}

/* block_comment: false on Reject. */
static bool block_comment(Cur in, Cur *rest, Str *s)
{
	if (!starts(in, "/*"))
		return false;
	size_t depth = 0, i = 0, upper = (size_t)(in.end - in.p) - 1;
	const char *b = in.p;
	while (i < upper) {
		if (b[i] == '/' && b[i + 1] == '*') {
			depth++;
			i++;
		} else if (b[i] == '*' && b[i + 1] == '/') {
			depth--;
			if (!depth) {
				*rest = (Cur){in.p + i + 2, in.end};
				*s = (Str){in.p, i + 2};
				return true;
			}
			i++;
		}
		i++;
	}
	return false;
}

static Cur skip_whitespace(Cur s)
{
	while (s.p < s.end) {
		unsigned char b = (unsigned char)*s.p;
		if (b == '/') {
			if (starts(s, "//") && (!starts(s, "///") || starts(s, "////")) && !starts(s, "//!")) {
				Str t;
				s = take_until_newline_or_eof(s, &t);
				continue;
			} else if (starts(s, "/**/")) {
				s.p += 4;
				continue;
			} else if (starts(s, "/*") && (!starts(s, "/**") || starts(s, "/***")) && !starts(s, "/*!")) {
				Str t;
				Cur rest;
				if (!block_comment(s, &rest, &t))
					return s;
				s = rest;
				continue;
			}
		}
		if (b == ' ' || (b >= 0x09 && b <= 0x0d)) {
			s.p++;
			continue;
		}
		if (b >= 0x80) {
			uint32_t ch;
			int w = utf8_decode(s.p, (size_t)(s.end - s.p), &ch);
			if (is_whitespace(ch)) {
				s.p += w;
				continue;
			}
		}
		return s;
	}
	return s;
}

/* ident_not_raw: false on Reject. */
static bool ident_not_raw(Cur in, Cur *rest)
{
	if (!ident_start(in))
		return false;
	in.p++;
	while (in.p < in.end && ident_continue(in))
		in.p++;
	*rest = in;
	return true;
}

static bool word_break(Cur in) { return !(in.p < in.end && ident_continue(in)); }

static Cur literal_suffix(Cur in)
{
	Cur r;
	return ident_not_raw(in, &r) ? r : in;
}

/* trailing_backslash: *in starts after the escaped newline (last). */
static bool trailing_backslash(Cur *in, int last)
{
	const char *q = in->p;
	for (;;) {
		if (last == '\r' && (q >= in->end || *q != '\n'))
			return false;
		if (q >= in->end)
			return false;
		char b = *q;
		if (b == ' ' || b == '\t' || b == '\n' || b == '\r') {
			last = b;
			q++;
			continue;
		}
		in->p = q;
		return true;
	}
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
		return (c | 0x20) - 'a' + 10;
	return -1;
}

/* backslash_u after "\u": returns the char or -1. */
static int64_t backslash_u(Cur *in)
{
	if (peekb(*in, 0) != '{')
		return -1;
	in->p++;
	uint32_t value = 0;
	int len = 0;
	while (in->p < in->end) {
		int ch = (unsigned char)*in->p++;
		int d = hexval(ch);
		if (d < 0) {
			if (ch == '_' && len > 0)
				continue;
			if (ch == '}' && len > 0)
				return (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) ? -1 : (int64_t)value;
			return -1;
		}
		if (len == 6)
			return -1;
		value = value * 16 + (uint32_t)d;
		len++;
	}
	return -1;
}

enum { CHARS, BYTES, CSTR };

/* cooked_string / cooked_byte_string / cooked_c_string after the opening quote. */
static bool cooked(Cur in, int kind, Cur *rest)
{
	while (in.p < in.end) {
		unsigned char ch = (unsigned char)*in.p;
		if (ch == '"') {
			in.p++;
			*rest = literal_suffix(in);
			return true;
		}
		if (ch == '\r') {
			if (peekb(in, 1) != '\n')
				return false;
			in.p += 2;
		} else if (ch == '\\') {
			int e = peekb(in, 1);
			in.p += e < 0 ? 1 : 2;
			if (e == 'x') {
				int a = hexval(peekb(in, 0)), b = hexval(peekb(in, 1));
				if (a < 0 || b < 0 || (kind == CHARS && a > 7) || (kind == CSTR && !a && !b))
					return false;
				in.p += 2;
			} else if (e == 'n' || e == 'r' || e == 't' || e == '\\' || e == '\'' || e == '"' ||
				   (e == '0' && kind != CSTR)) {
			} else if (e == 'u' && kind != BYTES) {
				int64_t c = backslash_u(&in);
				if (c < 0 || (kind == CSTR && c == 0))
					return false;
			} else if (e == '\n' || e == '\r') {
				if (!trailing_backslash(&in, e))
					return false;
			} else {
				return false;
			}
		} else if ((kind == BYTES && ch >= 0x80) || (kind == CSTR && ch == 0)) {
			return false;
		} else {
			in.p++;
		}
	}
	return false;
}

/* delimiter_of_raw_string + raw_string / raw_byte_string / raw_c_string after the `r`. */
static bool raw(Cur in, int kind, Cur *rest)
{
	size_t hashes = 0;
	while (peekb(in, hashes) == '#')
		hashes++;
	if (peekb(in, hashes) != '"' || hashes > 255)
		return false;
	in.p += hashes + 1;
	while (in.p < in.end) {
		unsigned char b = (unsigned char)*in.p;
		size_t k = 0;
		while (b == '"' && k < hashes && peekb(in, 1 + k) == '#')
			k++;
		if (b == '"' && k == hashes) {
			in.p += 1 + hashes;
			*rest = literal_suffix(in);
			return true;
		}
		if (b == '\r') {
			if (peekb(in, 1) != '\n')
				return false;
			in.p += 2;
			continue;
		}
		if ((kind == BYTES && b >= 0x80) || (kind == CSTR && b == 0))
			return false;
		in.p++;
	}
	return false;
}

/* byte (b'x') and character ('x'), at the opening quote. */
static bool quoted_char(Cur in, bool is_byte, Cur *rest)
{
	in.p++;
	if (in.p >= in.end)
		return false;
	if (*in.p == '\\') {
		int e = peekb(in, 1);
		in.p += 2;
		if (e == 'x') {
			int a = hexval(peekb(in, 0)), b = hexval(peekb(in, 1));
			if (a < 0 || b < 0 || (!is_byte && a > 7))
				return false;
			in.p += 2;
		} else if (e == 'u' && !is_byte) {
			if (backslash_u(&in) < 0)
				return false;
		} else if (!(e == 'n' || e == 'r' || e == 't' || e == '\\' || e == '0' || e == '\'' || e == '"')) {
			return false;
		}
	} else if (is_byte) {
		/* any byte, but the next one must start a char */
		in.p++;
		if (in.p < in.end && ((unsigned char)*in.p & 0xc0) == 0x80)
			return false;
	} else {
		uint32_t ch;
		in.p += utf8_decode(in.p, (size_t)(in.end - in.p), &ch);
	}
	if (in.p >= in.end || *in.p != '\'')
		return false;
	in.p++;
	*rest = literal_suffix(in);
	return true;
}

static bool is_digit(int b) { return b >= '0' && b <= '9'; }

/* float_digits */
static bool float_digits(Cur in, Cur *rest)
{
	if (!is_digit(peekb(in, 0)))
		return false;
	size_t len = 1;
	bool has_dot = false, has_exp = false;
	for (;;) {
		int ch = peekb(in, len);
		if (is_digit(ch) || ch == '_') {
			len++;
		} else if (ch == '.') {
			if (has_dot)
				break;
			Cur after = {in.p + len + 1, in.end};
			if (peekb(after, 0) == '.' || (after.p < after.end && ident_start(after)))
				return false;
			len++;
			has_dot = true;
		} else if (ch == 'e' || ch == 'E') {
			len++;
			has_exp = true;
			break;
		} else {
			break;
		}
	}
	if (!(has_dot || has_exp))
		return false;
	if (has_exp) {
		size_t before_exp = len - 1;
		bool has_sign = false, has_exp_value = false;
		for (;;) {
			int ch = peekb(in, len);
			if (ch == '+' || ch == '-') {
				if (has_exp_value)
					break;
				if (has_sign) {
					if (!has_dot)
						return false;
					*rest = (Cur){in.p + before_exp, in.end};
					return true;
				}
				len++;
				has_sign = true;
			} else if (is_digit(ch)) {
				len++;
				has_exp_value = true;
			} else if (ch == '_') {
				len++;
			} else {
				break;
			}
		}
		if (!has_exp_value) {
			if (!has_dot)
				return false;
			*rest = (Cur){in.p + before_exp, in.end};
			return true;
		}
	}
	*rest = (Cur){in.p + len, in.end};
	return true;
}

/* float and int: digits, an optional suffix, then a word break. */
static bool number_end(Cur r, Cur *rest)
{
	if (r.p < r.end && ident_start(r) && !ident_not_raw(r, &r))
		return false;
	if (!word_break(r))
		return false;
	*rest = r;
	return true;
}

static bool digits(Cur in, Cur *rest)
{
	int base = 10;
	if (starts(in, "0x"))
		base = 16;
	else if (starts(in, "0o"))
		base = 8;
	else if (starts(in, "0b"))
		base = 2;
	if (base != 10)
		in.p += 2;
	size_t len = 0;
	bool empty = true;
	for (;;) {
		int b = peekb(in, len);
		int d = is_digit(b) ? b - '0' : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : (b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
		if (b == '_') {
			if (empty && base == 10)
				return false;
			len++;
			continue;
		}
		if (d < 0)
			break;
		if (d >= base) {
			if (is_digit(b))
				return false;
			break;
		}
		len++;
		empty = false;
	}
	if (empty)
		return false;
	*rest = (Cur){in.p + len, in.end};
	return true;
}

/* literal_nocapture */
static bool literal(Cur in, Cur *rest)
{
	Cur r;
	if (starts(in, "\""))
		return cooked((Cur){in.p + 1, in.end}, CHARS, rest);
	if (starts(in, "r") && raw((Cur){in.p + 1, in.end}, CHARS, rest))
		return true;
	if (starts(in, "b\""))
		return cooked((Cur){in.p + 2, in.end}, BYTES, rest);
	if (starts(in, "br") && raw((Cur){in.p + 2, in.end}, BYTES, rest))
		return true;
	if (starts(in, "c\""))
		return cooked((Cur){in.p + 2, in.end}, CSTR, rest);
	if (starts(in, "cr") && raw((Cur){in.p + 2, in.end}, CSTR, rest))
		return true;
	if (starts(in, "b'") && quoted_char((Cur){in.p + 1, in.end}, true, rest))
		return true;
	if (starts(in, "'") && quoted_char(in, false, rest))
		return true;
	if (float_digits(in, &r) && number_end(r, rest))
		return true;
	return digits(in, &r) && number_end(r, rest);
}

static bool punct_char(Cur in)
{
	if (starts(in, "//") || starts(in, "/*") || in.p >= in.end)
		return false;
	return strchr("~!@#$%^&*-=+|;:,<.>/?'", *in.p) != NULL;
}

/* ident_any: false on Reject. */
static bool ident_any(Cur in, Cur *rest)
{
	bool is_raw = starts(in, "r#");
	Cur s = {in.p + (is_raw ? 2 : 0), in.end};
	if (!ident_not_raw(s, rest))
		return false;
	if (is_raw) {
		Str sym = {s.p, (size_t)(rest->p - s.p)};
		static const char *const no_raw[] = {"_", "super", "self", "Self", "crate"};
		for (size_t i = 0; i < sizeof no_raw / sizeof *no_raw; i++)
			if (str_eqc(sym, no_raw[i]))
				return false;
	}
	return true;
}

static bool ident(Cur in, Cur *rest)
{
	static const char *const prefixes[] = {"r\"", "r#\"", "r##", "b\"", "b'", "br\"", "br#", "c\"", "cr\"", "cr#"};
	for (size_t i = 0; i < sizeof prefixes / sizeof *prefixes; i++)
		if (starts(in, prefixes[i]))
			return false;
	return ident_any(in, rest);
}

static const char ERROR_LIT[] = "(/*ERROR*/)";

/* doc_comment: pushes `#` [`!`] `[doc = "..."]`. */
static bool doc_comment(Cur in, Cur *rest, PpNode *out)
{
	Str s;
	bool inner;
	if (starts(in, "//!")) {
		*rest = take_until_newline_or_eof((Cur){in.p + 3, in.end}, &s);
		inner = true;
	} else if (starts(in, "/*!")) {
		if (!block_comment(in, rest, &s))
			return false;
		s = (Str){s.p + 3, s.n - 5};
		inner = true;
	} else if (starts(in, "///")) {
		if (peekb(in, 3) == '/')
			return false;
		*rest = take_until_newline_or_eof((Cur){in.p + 3, in.end}, &s);
		inner = false;
	} else if (starts(in, "/**") && peekb(in, 3) != '*') {
		if (!block_comment(in, rest, &s))
			return false;
		s = (Str){s.p + 3, s.n - 5};
		inner = false;
	} else {
		return false;
	}
	for (size_t i = 0; i < s.n; i++)
		if (s.p[i] == '\r' && (i + 1 >= s.n || s.p[i + 1] != '\n'))
			return false;
	pp_push(out, pp_tt_punct('#', false));
	if (inner)
		pp_push(out, pp_tt_punct('!', false));
	PpNode *g = pp_tt_group(PP_DELIM_BRACKET);
	pp_push(g, pp_tt_ident("doc"));
	pp_push(g, pp_tt_punct('=', false));
	pp_push(g, pp_tt_lit(pm2_string(s)));
	pp_push(out, g);
	return true;
}

bool pm2_token_stream(Str src, PpNode *out)
{
	Cur in = {src.p, src.p + src.n};
	if (starts(in, "\xef\xbb\xbf"))
		in.p += 3;
	Vec stack = {0}; /* the enclosing groups */
	PpNode *trees = out;
	for (;;) {
		in = skip_whitespace(in);
		Cur rest;
		if (doc_comment(in, &rest, trees)) {
			in = rest;
			continue;
		}
		if (in.p >= in.end)
			return stack.n == 0;
		char first = *in.p;
		int open = first == '(' && !starts(in, ERROR_LIT) ? PP_DELIM_PAREN
			   : first == '['                         ? PP_DELIM_BRACKET
			   : first == '{'                         ? PP_DELIM_BRACE
								  : -1;
		int close = first == ')' ? PP_DELIM_PAREN : first == ']' ? PP_DELIM_BRACKET : first == '}' ? PP_DELIM_BRACE : -1;
		if (open >= 0) {
			in.p++;
			vec_push(&stack, trees);
			trees = pp_tt_group(open);
		} else if (close >= 0) {
			if (!stack.n || trees->aux != close)
				return false;
			in.p++;
			PpNode *g = trees;
			trees = stack.v[--stack.n];
			pp_push(trees, g);
		} else if (literal(in, &rest)) {
			pp_push(trees, pp_tt_lit(xstrndup(in.p, (size_t)(rest.p - in.p))));
			in = rest;
		} else if (punct_char(in)) {
			char ch = *in.p;
			Cur after = {in.p + 1, in.end};
			bool joint;
			if (ch == '\'') {
				Cur r;
				if (!ident_any(after, &r) || peekb(r, 0) == '\'' || (peekb(r, 0) == '#' && !starts(after, "r#")))
					return false;
				joint = true;
			} else {
				joint = punct_char(after);
			}
			pp_push(trees, pp_tt_punct(ch, joint));
			in = after;
		} else if (ident(in, &rest)) {
			pp_push(trees, pp_tt_ident(xstrndup(in.p, (size_t)(rest.p - in.p))));
			in = rest;
		} else if (starts(in, ERROR_LIT)) {
			pp_push(trees, pp_tt_lit(ERROR_LIT));
			in.p += sizeof ERROR_LIT - 1;
		} else {
			return false;
		}
	}
}
