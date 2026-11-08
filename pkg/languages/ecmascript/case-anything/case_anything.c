/* case-anything 2.1.13 dist/index.js: splitAndPrefix, capitaliseWord and camelCase.
 *
 * case-anything: Copyright (c) 2020 Luca Ban - Mesqueeb, MIT License.
 *
 * The regular expressions work on Latin-1 letters (magicSplit:
 * /^[a-zà-öø-ÿ]+|[A-ZÀ-ÖØ-ß][a-zà-öø-ÿ]+|[a-zà-öø-ÿ]+|[0-9]+|[A-ZÀ-ÖØ-ß]+(?![a-zà-öø-ÿ])/g), so the
 * port decodes UTF-8 and classifies code points in that range. Words keep only
 * [a-zA-ZØßø0-9] after NFD (the accents of Latin-1 letters are dropped); String.normalize("NFC")
 * of the input is not ported (it only differs for text with combining marks). */
#include "case_anything.h"

#include <string.h>

#include "util.h"

typedef struct {
	uint32_t *c;
	int n;
} Runes;

static Runes decode(const char *s)
{
	size_t n = strlen(s);
	Runes r = {xalloc(sizeof(uint32_t) * (n + 1)), 0};
	for (size_t i = 0; i < n;) {
		uint32_t c;
		i += (size_t)utf8_decode(s + i, n - i, &c);
		r.c[r.n++] = c;
	}
	return r;
}

static void put_rune(Buf *b, uint32_t c)
{
	if (c < 0x80) {
		buf_addc(b, (char)c);
	} else if (c < 0x800) {
		buf_addc(b, (char)(0xC0 | (c >> 6)));
		buf_addc(b, (char)(0x80 | (c & 0x3F)));
	} else if (c < 0x10000) {
		buf_addc(b, (char)(0xE0 | (c >> 12)));
		buf_addc(b, (char)(0x80 | ((c >> 6) & 0x3F)));
		buf_addc(b, (char)(0x80 | (c & 0x3F)));
	} else {
		buf_addc(b, (char)(0xF0 | (c >> 18)));
		buf_addc(b, (char)(0x80 | ((c >> 12) & 0x3F)));
		buf_addc(b, (char)(0x80 | ((c >> 6) & 0x3F)));
		buf_addc(b, (char)(0x80 | (c & 0x3F)));
	}
}

static bool is_lower(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xF6) || (c >= 0xF8 && c <= 0xFF); }
static bool is_upper(uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xDF); }
static bool is_digit(uint32_t c) { return c >= '0' && c <= '9'; }

/* The match of magicSplit starting exactly at i (0 if none). */
static int match_at(const Runes *r, int i)
{
	int k;
	if (i == 0 && is_lower(r->c[0])) { /* ^[a-z…]+ */
		for (k = 1; k < r->n && is_lower(r->c[k]); k++)
			;
		return k;
	}
	if (is_upper(r->c[i]) && i + 1 < r->n && is_lower(r->c[i + 1])) { /* [A-Z…][a-z…]+ */
		for (k = i + 2; k < r->n && is_lower(r->c[k]); k++)
			;
		return k - i;
	}
	if (is_lower(r->c[i])) {
		for (k = i + 1; k < r->n && is_lower(r->c[k]); k++)
			;
		return k - i;
	}
	if (is_digit(r->c[i])) {
		for (k = i + 1; k < r->n && is_digit(r->c[k]); k++)
			;
		return k - i;
	}
	if (is_upper(r->c[i])) { /* [A-Z…]+(?![a-z…]): backtrack until the next is not lower */
		for (k = i + 1; k < r->n && is_upper(r->c[k]); k++)
			;
		for (; k > i; k--)
			if (k >= r->n || !is_lower(r->c[k]))
				return k - i;
	}
	return 0;
}

static bool is_space(uint32_t c)
{
	return c == ' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
	       c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

/* part.normalize("NFD").replace(/[^a-zA-ZØßø0-9]/g, "") for a Latin-1 part */
static uint32_t strip_accent(uint32_t c)
{
	static const char base[] = "AAAAAA\0CEEEEIIII\0NOOOOO\0\0UUUUY\0\0aaaaaa\0ceeeeiiii\0nooooo\0\0uuuuy\0y";
	if (c < 0x80)
		return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c)) ? c : 0;
	if (c == 0xD8 || c == 0xDF || c == 0xF8)
		return c;
	if (c >= 0xC0 && c <= 0xFF)
		return (uint8_t)base[c - 0xC0];
	return 0;
}

static uint32_t to_lower(uint32_t c)
{
	if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
		return c + 0x20;
	return c;
}

static void put_upper(Buf *b, uint32_t c)
{
	if (c == 0xDF) {
		buf_adds(b, "SS");
		return;
	}
	if (c == 0xFF)
		c = 0x178;
	else if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
		c -= 0x20;
	put_rune(b, c);
}

char *ca_camel_case(const char *s)
{
	Runes all = decode(s);
	int lo = 0, hi = all.n;
	while (lo < hi && is_space(all.c[lo]))
		lo++;
	while (hi > lo && is_space(all.c[hi - 1]))
		hi--;
	Runes r = {all.c + lo, hi - lo};
	bool has_spaces = false;
	for (int i = 0; i < r.n; i++)
		has_spaces |= r.c[i] == ' ';
	Buf out = {0};
	int word = 0;
	for (int i = 0; i < r.n;) {
		int m;
		if (has_spaces) { /* \S+ */
			if (is_space(r.c[i])) {
				i++;
				continue;
			}
			for (m = 1; i + m < r.n && !is_space(r.c[i + m]); m++)
				;
		} else {
			m = match_at(&r, i);
			if (!m) {
				i++;
				continue;
			}
		}
		/* the part without special characters; prefixes are dropped */
		uint32_t *p = xalloc(sizeof(uint32_t) * (size_t)m);
		int np = 0;
		for (int k = 0; k < m; k++) {
			uint32_t c = strip_accent(r.c[i + k]);
			if (c)
				p[np++] = c;
		}
		i += m;
		if (!np)
			continue;
		/* camelCase: the first word lower case, the others capitalised (all their characters
		 * match magicSplit, so capitaliseWord upper-cases the first one) */
		for (int k = 0; k < np; k++) {
			if (word > 0 && k == 0)
				put_upper(&out, p[k]);
			else
				put_rune(&out, to_lower(p[k]));
		}
		word++;
	}
	return xstrdup(buf_str(&out));
}
