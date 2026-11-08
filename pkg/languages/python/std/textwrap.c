/* textwrap.wrap(text, width) of CPython 3.11 (Lib/textwrap.py, default TextWrapper options):
 * expand_tabs, replace_whitespace, drop_whitespace, break_long_words and break_on_hyphens.
 * The chunks come from wordsep_re; its \w and \d match like the re module of CPython 3.11
 * (wordclass.h, Unicode 14.0.0).
 *
 * Copyright (C) 1999-2001 Gregory P. Ward.
 * Copyright (C) 2002, 2003 Python Software Foundation.
 * Written by Greg Ward <gward@python.net>
 * (CPython is under the Python Software Foundation License.) */
#include "python.h"

#include <string.h>

#include "wordclass.h"

/* Whether c (>= 0x80) is in a range table of wordclass.h. */
static bool in_table(const uint8_t *t, size_t n, uint32_t c)
{
	uint32_t prev = 0x7f;
	for (size_t i = 0; i < n;) {
		uint32_t v[2] = {0, 0};
		for (int k = 0; k < 2; k++)
			for (int shift = 0; i < n; shift += 7) {
				uint8_t b = t[i++];
				v[k] |= (uint32_t)(b & 0x7f) << shift;
				if (!(b & 0x80))
					break;
			}
		uint32_t lo = prev + 1 + v[0], hi = lo + v[1];
		if (c < lo)
			return false;
		if (c <= hi)
			return true;
		prev = hi;
	}
	return false;
}

typedef struct {
	const uint32_t *t;
	size_t n;
} Text;

/* \w ('w'), word_punct [\w!"'&.,?] ('p'), letter [^\d\W] ('l') at index i (false outside) */
static bool at(const Text *x, ptrdiff_t i, int kind)
{
	if (i < 0 || (size_t)i >= x->n)
		return false;
	uint32_t c = x->t[i];
	bool word, digit;
	if (c < 0x80) {
		digit = c >= '0' && c <= '9';
		word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || digit || c == '_';
	} else {
		word = in_table(py311_word, sizeof py311_word, c);
		digit = word && in_table(py311_digit, sizeof py311_digit, c);
	}
	switch (kind) {
	case 'w': return word;
	case 'p': return word || (c && c < 0x80 && strchr("!\"'&.,?", (int)c));
	case 'l': return word && !digit;
	}
	return false;
}

static bool ch(const Text *x, ptrdiff_t i, uint32_t c) { return i >= 0 && (size_t)i < x->n && x->t[i] == c; }

/* [\t\n\x0b\x0c\r ] */
static bool ws(uint32_t c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

/* (?=-{2,}\w) at q */
static bool dashes_then_word(const Text *x, size_t q)
{
	size_t k = q;
	while (k < x->n && x->t[k] == '-')
		k++;
	return k - q >= 2 && at(x, (ptrdiff_t)k, 'w');
}

/* The length of the wordsep_re match at p (there is always one). */
static size_t match_at(const Text *x, size_t p)
{
	if (ws(x->t[p])) {
		size_t k = p;
		while (k < x->n && ws(x->t[k]))
			k++;
		return k - p;
	}
	/* (?<=wp) -{2,} (?=\w) */
	if (x->t[p] == '-' && at(x, (ptrdiff_t)p - 1, 'p') && dashes_then_word(x, p)) {
		size_t k = p;
		while (k < x->n && x->t[k] == '-')
			k++;
		return k - p;
	}
	/* nws+? (?: hyphenated | end of word | em-dash ) */
	for (size_t k = 1;; k++) {
		size_t q = p + k;
		ptrdiff_t h = (ptrdiff_t)q;
		/* -(?: (?<=lt{2}-) | (?<=lt-lt-)) (?= lt -? lt) */
		if (ch(x, h, '-') &&
		    ((at(x, h - 2, 'l') && at(x, h - 1, 'l')) || (at(x, h - 3, 'l') && ch(x, h - 2, '-') && at(x, h - 1, 'l'))) &&
		    at(x, h + 1, 'l') && (at(x, h + 2, 'l') || (ch(x, h + 2, '-') && at(x, h + 3, 'l'))))
			return k + 1;
		if (q == x->n || ws(x->t[q]))
			return k;
		/* (?<=wp) (?=-{2,}\w) */
		if (at(x, h - 1, 'p') && dashes_then_word(x, q))
			return k;
	}
}

static bool all_space(const uint32_t *c, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (!py_isspace(c[i]))
			return false;
	return true;
}

typedef struct {
	const uint32_t *p;
	size_t n;
} Chunk;

static int utf8_len(uint32_t c) { return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4; }

static Str encode(const Chunk *parts, int np)
{
	size_t len = 0;
	for (int i = 0; i < np; i++)
		for (size_t j = 0; j < parts[i].n; j++)
			len += (size_t)utf8_len(parts[i].p[j]);
	char *r = xalloc(len + 1), *o = r;
	for (int i = 0; i < np; i++)
		for (size_t j = 0; j < parts[i].n; j++) {
			uint32_t c = parts[i].p[j];
			switch (utf8_len(c)) {
			case 1: *o++ = (char)c; break;
			case 2: *o++ = (char)(0xc0 | (c >> 6)); *o++ = (char)(0x80 | (c & 0x3f)); break;
			case 3:
				*o++ = (char)(0xe0 | (c >> 12)); *o++ = (char)(0x80 | ((c >> 6) & 0x3f));
				*o++ = (char)(0x80 | (c & 0x3f));
				break;
			default:
				*o++ = (char)(0xf0 | (c >> 18)); *o++ = (char)(0x80 | ((c >> 12) & 0x3f));
				*o++ = (char)(0x80 | ((c >> 6) & 0x3f)); *o++ = (char)(0x80 | (c & 0x3f));
			}
		}
	return (Str){r, len};
}

/* text.expandtabs(8).translate(whitespace to spaces) as code points; returns the count (t may
 * be NULL to count only). */
static size_t munge(Str text, uint32_t *t)
{
	size_t n = 0, col = 0;
	for (size_t i = 0; i < text.n;) {
		uint32_t r;
		i += (size_t)utf8_decode(text.p + i, text.n - i, &r);
		if (r == '\t') {
			size_t inc = 8 - col % 8;
			for (size_t k = 0; k < inc; k++, n++)
				if (t)
					t[n] = ' ';
			col += inc;
			continue;
		}
		col = (r == '\n' || r == '\r') ? 0 : col + 1;
		if (t)
			t[n] = ws(r) ? ' ' : r;
		n++;
	}
	return n;
}

int py_textwrap_wrap(Str text, int width, Str **out)
{
	*out = NULL;
	size_t n = munge(text, NULL);
	uint32_t *t = xalloc(sizeof *t * (n + 1));
	munge(text, t);
	Text x = {t, n};
	/* _split */
	size_t nc = 0;
	for (size_t p = 0; p < n; nc++)
		p += match_at(&x, p);
	Chunk *chunks = xalloc(sizeof *chunks * (nc + 1));
	nc = 0;
	for (size_t p = 0; p < n;) {
		size_t len = match_at(&x, p);
		chunks[nc++] = (Chunk){t + p, len};
		p += len;
	}
	/* _wrap_chunks: chunks[pos] is the chunks[-1] of the reversed list */
	size_t pos = 0, w = (size_t)width;
	Buf lines = {0};
	int nlines = 0;
	Chunk *cur = xalloc(sizeof *cur * (w + 2)); /* at most w chunks fit, plus a long word part */
	while (pos < nc) {
		int ncur = 0;
		size_t cur_len = 0;
		if (all_space(chunks[pos].p, chunks[pos].n) && nlines)
			pos++;
		while (pos < nc) {
			size_t l = chunks[pos].n;
			if (cur_len + l <= w) {
				cur[ncur++] = chunks[pos++];
				cur_len += l;
			} else {
				break;
			}
		}
		if (pos < nc && chunks[pos].n > w) {
			/* _handle_long_word (3.11: also with space_left == 0) */
			size_t space_left = w < 1 ? 1 : w - cur_len;
			size_t end = space_left;
			Chunk c = chunks[pos];
			if (c.n > space_left) {
				ptrdiff_t hyphen = -1;
				for (size_t i = space_left; i-- > 0;)
					if (c.p[i] == '-') {
						hyphen = (ptrdiff_t)i;
						break;
					}
				if (hyphen > 0) {
					bool other = false;
					for (ptrdiff_t i = 0; i < hyphen; i++)
						if (c.p[i] != '-')
							other = true;
					if (other)
						end = (size_t)hyphen + 1;
				}
			}
			cur[ncur++] = (Chunk){c.p, end};
			chunks[pos] = (Chunk){c.p + end, c.n - end};
			cur_len = 0;
			for (int i = 0; i < ncur; i++)
				cur_len += cur[i].n;
		}
		if (ncur && all_space(cur[ncur - 1].p, cur[ncur - 1].n)) {
			cur_len -= cur[ncur - 1].n;
			ncur--;
		}
		if (ncur) {
			Str s = encode(cur, ncur);
			buf_add(&lines, &s, sizeof s);
			nlines++;
		}
	}
	*out = (Str *)lines.p;
	return nlines;
}
