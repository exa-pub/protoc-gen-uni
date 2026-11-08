/* Copyright (c) 2015 The Rust Project Developers. Licensed under the MIT license or the Apache
 * License, Version 2.0, at your option.
 *
 * Port of heck 0.5.0: src/lib.rs (transform, lowercase, uppercase, capitalize) and the case
 * modules (snake.rs, shouty_snake.rs, kebab.rs, shouty_kebab.rs, upper_camel.rs, lower_camel.rs,
 * title.rs, train.rs). */
#include "heck.h"

#include <string.h>

/* char::is_alphanumeric, is_lowercase, is_uppercase for one char starting at p (see heck.h for
 * non-ASCII chars). */
static bool is_alnum(unsigned char c) { return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }
static bool is_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }

/* Width of the char at s[i] (s is NUL-terminated). */
static size_t char_width(const char *s, size_t i, size_t n)
{
	uint32_t r;
	return (size_t)utf8_decode(s + i, n - i, &r);
}

enum { W_LOWER, W_UPPER, W_CAPITAL, W_LOWER_CAMEL };

static void lowercase(Buf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		buf_addc(b, is_upper((unsigned char)s[i]) ? (char)(s[i] - 'A' + 'a') : s[i]);
}

static void uppercase(Buf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		buf_addc(b, is_lower((unsigned char)s[i]) ? (char)(s[i] - 'a' + 'A') : s[i]);
}

static void capitalize(Buf *b, const char *s, size_t n)
{
	if (!n)
		return;
	size_t w = char_width(s, 0, n);
	uppercase(b, s, w);
	lowercase(b, s + w, n - w);
}

typedef struct {
	Buf b;
	int mode;
	const char *boundary;
	int words;
} Transform;

static void with_word(Transform *t, const char *s, size_t n)
{
	switch (t->mode) {
	case W_LOWER: lowercase(&t->b, s, n); break;
	case W_UPPER: uppercase(&t->b, s, n); break;
	case W_CAPITAL: capitalize(&t->b, s, n); break;
	default:
		if (!t->words)
			lowercase(&t->b, s, n);
		else
			capitalize(&t->b, s, n);
	}
	t->words++;
}

enum { M_BOUNDARY, M_LOWER, M_UPPER };

/* heck's transform: splits s at non-alphanumeric chars into words, and words further at
 * lowercase-to-uppercase transitions and before the last uppercase char of an uppercase run
 * followed by a lowercase char ("XMLHttp" -> "XML", "Http"). */
static char *transform(const char *s, int mode, const char *boundary)
{
	Transform t = {{0}, mode, boundary, 0};
	size_t n = strlen(s), i = 0;
	bool first_word = true;
	while (i < n) {
		size_t start = i;
		while (i < n && is_alnum((unsigned char)s[i]))
			i += char_width(s, i, n);
		const char *word = s + start;
		size_t wn = i - start, init = 0;
		int m = M_BOUNDARY;
		for (size_t k = 0; k < wn;) {
			unsigned char c = (unsigned char)word[k];
			size_t next_k = k + char_width(word, k, wn);
			if (next_k < wn) {
				unsigned char next = (unsigned char)word[next_k];
				int next_mode = is_lower(c) ? M_LOWER : is_upper(c) ? M_UPPER : m;
				if (next_mode == M_LOWER && is_upper(next)) {
					if (!first_word)
						buf_adds(&t.b, boundary);
					with_word(&t, word + init, next_k - init);
					first_word = false;
					init = next_k;
					m = M_BOUNDARY;
				} else if (m == M_UPPER && is_upper(c) && is_lower(next)) {
					if (!first_word)
						buf_adds(&t.b, boundary);
					else
						first_word = false;
					with_word(&t, word + init, k - init);
					init = k;
					m = M_BOUNDARY;
				} else {
					m = next_mode;
				}
			} else {
				if (!first_word)
					buf_adds(&t.b, boundary);
				else
					first_word = false;
				with_word(&t, word + init, wn - init);
				break;
			}
			k = next_k;
		}
		if (i < n) /* the separator */
			i += char_width(s, i, n);
	}
	return xstrdup(buf_str(&t.b));
}

char *heck_snake(const char *s) { return transform(s, W_LOWER, "_"); }
char *heck_shouty_snake(const char *s) { return transform(s, W_UPPER, "_"); }
char *heck_kebab(const char *s) { return transform(s, W_LOWER, "-"); }
char *heck_shouty_kebab(const char *s) { return transform(s, W_UPPER, "-"); }
char *heck_upper_camel(const char *s) { return transform(s, W_CAPITAL, ""); }
char *heck_lower_camel(const char *s) { return transform(s, W_LOWER_CAMEL, ""); }
char *heck_title(const char *s) { return transform(s, W_CAPITAL, " "); }
char *heck_train(const char *s) { return transform(s, W_CAPITAL, "-"); }
