/* Port of src/ast.rs of prost-build 0.14.1 (https://github.com/tokio-rs/prost, Apache-2.0):
 * Comments, without the cleanup-markdown feature (off in protoc-gen-prost). The two regexes of
 * sanitize_line are matched by hand with the leftmost-first semantics of the regex crate. */
#include "prost.h"
#include "rust_std.h"

#include <string.h>

static int lines_of(Str s, Str **out) { return rust_lines(s, out); }

ProstComments prost_comments_from_location(const PbMsg *loc)
{
	ProstComments c = {0};
	c.n_detached = pb_count(loc, LOC_LEADING_DETACHED);
	c.detached = xalloc(sizeof *c.detached * (size_t)(c.n_detached ? c.n_detached : 1));
	c.n_detached_lines = xalloc(sizeof *c.n_detached_lines * (size_t)(c.n_detached ? c.n_detached : 1));
	for (int i = 0; i < c.n_detached; i++)
		c.n_detached_lines[i] = lines_of(pb_bytes_at(loc, LOC_LEADING_DETACHED, i), &c.detached[i]);
	c.n_leading = lines_of(pb_bytes(loc, LOC_LEADING), &c.leading);
	c.n_trailing = lines_of(pb_bytes(loc, LOC_TRAILING), &c.trailing);
	return c;
}

static void indent(Buf *buf, int level)
{
	for (int i = 0; i < level; i++)
		buf_adds(buf, "    ");
}

static void line(Buf *buf, int level, const char *prefix, Str l)
{
	indent(buf, level);
	buf_adds(buf, prefix);
	prost_sanitize_line(l, buf);
	buf_addc(buf, '\n');
}

void prost_comments_append_with_indent(const ProstComments *c, int level, Buf *buf)
{
	/* Blocks of detached comments. */
	for (int i = 0; i < c->n_detached; i++) {
		for (int j = 0; j < c->n_detached_lines[i]; j++)
			line(buf, level, "//", c->detached[i][j]);
		buf_addc(buf, '\n');
	}
	for (int i = 0; i < c->n_leading; i++)
		line(buf, level, "///", c->leading[i]);
	/* An empty comment line between leading and trailing comments. */
	if (c->n_leading && c->n_trailing) {
		indent(buf, level);
		buf_adds(buf, "///\n");
	}
	for (int i = 0; i < c->n_trailing; i++)
		line(buf, level, "///", c->trailing[i]);
}

static size_t width_at(const char *s, size_t i, size_t n)
{
	uint32_t r;
	return (size_t)utf8_decode(s + i, n - i, &r);
}

/* RULE_URL `https?://[^\s)]+` replaced with `<$0>`. */
static void replace_urls(Str s, Buf *out)
{
	size_t last = 0;
	for (size_t i = 0; i < s.n;) {
		size_t j = 0;
		if (s.n - i >= 8 && !memcmp(s.p + i, "https://", 8))
			j = i + 8;
		else if (s.n - i >= 7 && !memcmp(s.p + i, "http://", 7))
			j = i + 7;
		size_t k = j;
		while (j && k < s.n) {
			uint32_t r;
			int w = utf8_decode(s.p + k, s.n - k, &r);
			if (r == ')' || rust_is_whitespace(r))
				break;
			k += (size_t)w;
		}
		if (!j || k == j) {
			i++;
			continue;
		}
		buf_add(out, s.p + last, i - last);
		buf_addc(out, '<');
		buf_add(out, s.p + i, k - i);
		buf_addc(out, '>');
		last = i = k;
	}
	buf_add(out, s.p + last, s.n - last);
}

/* The match of RULE_BRACKETS `(^|[^\]\\])\[(([^\]]*[^\\])?)\]([^(\[]|$)` at s[start:]. */
typedef struct {
	size_t g1, g2s, g2e, end; /* group 1 is s[start:g1], group 4 is s[g2e+1:end] */
} BracketMatch;

/* `\]([^(\[]|$)` at s[u:]: returns the end or 0 (a match never ends at 0). */
static size_t bracket_tail(Str s, size_t u)
{
	if (u >= s.n || s.p[u] != ']')
		return 0;
	u++;
	if (u < s.n && s.p[u] != '(' && s.p[u] != '[')
		return u + width_at(s.p, u, s.n);
	return u == s.n ? u : 0;
}

static bool bracket_rest(Str s, size_t p, BracketMatch *m)
{
	if (p >= s.n || s.p[p] != '[')
		return false;
	size_t q = p + 1;
	/* `([^\]]*[^\\])?`: greedy, the star backtracks one char at a time */
	size_t r = q;
	while (r < s.n && s.p[r] != ']')
		r += width_at(s.p, r, s.n);
	for (size_t t = r;;) {
		if (t < s.n && s.p[t] != '\\') {
			size_t u = t + width_at(s.p, t, s.n);
			size_t end = bracket_tail(s, u);
			if (end) {
				*m = (BracketMatch){p, q, u, end};
				return true;
			}
		}
		if (t == q)
			break;
		/* the previous char boundary */
		do
			t--;
		while (t > q && ((unsigned char)s.p[t] & 0xc0) == 0x80);
	}
	size_t end = bracket_tail(s, q);
	if (end) {
		*m = (BracketMatch){p, q, q, end};
		return true;
	}
	return false;
}

static bool bracket_at(Str s, size_t start, BracketMatch *m)
{
	if (start == 0 && bracket_rest(s, 0, m))
		return true;
	if (start < s.n && s.p[start] != ']' && s.p[start] != '\\')
		return bracket_rest(s, start + width_at(s.p, start, s.n), m);
	return false;
}

/* RULE_BRACKETS replaced with `$1\[$2\]$4`. */
static void replace_brackets(Str s, Buf *out)
{
	size_t last = 0;
	for (size_t i = 0; i <= s.n;) {
		BracketMatch m;
		if (!bracket_at(s, i, &m)) {
			if (i == s.n)
				break;
			i += width_at(s.p, i, s.n);
			continue;
		}
		buf_add(out, s.p + last, m.g1 - last);
		buf_adds(out, "\\[");
		buf_add(out, s.p + m.g2s, m.g2e - m.g2s);
		buf_adds(out, "\\]");
		buf_add(out, s.p + m.g2e + 1, m.end - m.g2e - 1);
		last = i = m.end;
	}
	buf_add(out, s.p + last, s.n - last);
}

void prost_sanitize_line(Str l, Buf *out)
{
	Buf urls = {0}, b = {0};
	replace_urls(l, &urls);
	replace_brackets(buf_view(&urls), &b);
	Str s = buf_view(&b);
	/* should_indent: non-empty, and not starting with exactly one space */
	if (s.n && (s.p[0] != ' ' || (s.n > 1 && s.p[1] == ' ')))
		buf_addc(out, ' ');
	buf_add_str(out, s);
}
