/* shlex (Lib/shlex.py of CPython 3.13): the shlex.shlex lexer with whitespace_split = True and
 * no commenters, in POSIX mode (shlex.split) and in non-POSIX mode with a custom whitespace set.
 * Every character the lexer treats specially is ASCII, so the bytes of UTF-8 text are processed
 * one by one with the same result as Python's code points. */
#include "python.h"

#include <string.h>

static bool in_set(const char *set, char c)
{
	return c && strchr(set, c) != NULL;
}

static void push(Str **v, int *n, int *cap, Buf *tok)
{
	if (*n == *cap) {
		int c2 = *cap ? *cap * 2 : 8;
		*v = xrealloc(*v, sizeof **v * (size_t)*cap, sizeof **v * (size_t)c2);
		*cap = c2;
	}
	Buf copy = {0};
	buf_add(&copy, tok->p ? tok->p : "", tok->n);
	(*v)[(*n)++] = buf_view(&copy);
	tok->n = 0;
}

int py_shlex(Str s, bool posix, const char *whitespace, Str **out, const char **err)
{
	const char *quotes = "'\"";
	Str *v = NULL;
	int n = 0, cap = 0;
	size_t i = 0;
	*err = NULL;
	/* read_token() calls until EOF: state ' ' between tokens, 'a' in a word, a quote or the
	 * escape character while inside one. */
	char state = ' ';
	for (;;) {
		Buf tok = {0};
		bool quoted = false;
		char escapedstate = ' ';
		bool eof = false;
		for (;;) {
			bool have = i < s.n;
			char c = have ? s.p[i++] : 0;
			if (state == 0) {
				tok.n = 0; /* past end of file */
				break;
			} else if (state == ' ') {
				if (!have) {
					state = 0;
					break;
				} else if (in_set(whitespace, c)) {
					if (tok.n || (posix && quoted))
						break;
				} else if (posix && c == '\\') {
					escapedstate = 'a';
					state = c;
				} else if (in_set(quotes, c)) {
					if (!posix)
						buf_addc(&tok, c);
					state = c;
				} else {
					/* wordchars, or any character with whitespace_split */
					tok.n = 0;
					buf_addc(&tok, c);
					state = 'a';
				}
			} else if (in_set(quotes, state)) {
				quoted = true;
				if (!have) {
					*err = "No closing quotation";
					return 0;
				}
				if (c == state) {
					if (!posix) {
						buf_addc(&tok, c);
						state = ' ';
						break;
					}
					state = 'a';
				} else if (posix && c == '\\' && state == '"') {
					escapedstate = state;
					state = c;
				} else {
					buf_addc(&tok, c);
				}
			} else if (state == '\\') {
				if (!have) {
					*err = "No escaped character";
					return 0;
				}
				/* in quotes, only the quote itself or the escape character may be escaped */
				if (in_set(quotes, escapedstate) && c != state && c != escapedstate)
					buf_addc(&tok, state);
				buf_addc(&tok, c);
				state = escapedstate;
			} else { /* 'a' */
				if (!have) {
					state = 0;
					break;
				} else if (in_set(whitespace, c)) {
					state = ' ';
					if (tok.n || (posix && quoted))
						break;
				} else if (posix && in_set(quotes, c)) {
					state = c;
				} else if (posix && c == '\\') {
					escapedstate = 'a';
					state = c;
				} else {
					buf_addc(&tok, c);
				}
			}
		}
		/* POSIX: an unquoted empty token is EOF; non-POSIX: the empty token is EOF */
		if (!tok.n && (!posix || !quoted))
			eof = true;
		if (eof)
			break;
		push(&v, &n, &cap, &tok);
	}
	*out = v;
	return n;
}
