/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from google.golang.org/protobuf v1.36.12 internal/strs/strings.go. */
#include "strs.h"

#include "go_std.h"

#include <string.h>

static bool is_ascii_lower(char c) { return c >= 'a' && c <= 'z'; }
static bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

char *strs_go_camel_case(const char *s)
{
	/* Invariant: if the next letter is lower case, it must be converted to upper case. That
	 * is, we process a word at a time, where words are marked by _ or upper case letter.
	 * Digits are treated as words. */
	Buf b = {0};
	size_t n = strlen(s);
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (c == '.' && i + 1 < n && is_ascii_lower(s[i + 1])) {
			/* Skip over '.' in ".{{lowercase}}". */
		} else if (c == '.') {
			buf_addc(&b, '_'); /* convert '.' to '_' */
		} else if (c == '_' && (i == 0 || s[i - 1] == '.')) {
			/* Convert initial '_' to ensure we start with a capital letter. Do the same
			 * for '_' after '.' to match historic behavior. */
			buf_addc(&b, 'X');
		} else if (c == '_' && i + 1 < n && is_ascii_lower(s[i + 1])) {
			/* Skip over '_' in "_{{lowercase}}". */
		} else if (is_ascii_digit(c)) {
			buf_addc(&b, c);
		} else {
			/* Assume we have a letter now - if not, it's a bogus identifier. The next word
			 * is a sequence of characters that must start upper case. */
			if (is_ascii_lower(c))
				c = (char)(c - 'a' + 'A');
			buf_addc(&b, c);
			/* Accept lower case sequence that follows. */
			for (; i + 1 < n && is_ascii_lower(s[i + 1]); i++)
				buf_addc(&b, s[i + 1]);
		}
	}
	return (char *)buf_str(&b);
}

static void add_rune(Buf *b, uint32_t r)
{
	char u[4];
	int w;
	if (r < 0x80)
		u[0] = (char)r, w = 1;
	else if (r < 0x800)
		u[0] = (char)(0xc0 | r >> 6), u[1] = (char)(0x80 | (r & 0x3f)), w = 2;
	else if (r < 0x10000)
		u[0] = (char)(0xe0 | r >> 12), u[1] = (char)(0x80 | ((r >> 6) & 0x3f)), u[2] = (char)(0x80 | (r & 0x3f)), w = 3;
	else
		u[0] = (char)(0xf0 | r >> 18), u[1] = (char)(0x80 | ((r >> 12) & 0x3f)), u[2] = (char)(0x80 | ((r >> 6) & 0x3f)),
		u[3] = (char)(0x80 | (r & 0x3f)), w = 4;
	buf_add(b, u, (size_t)w);
}

char *strs_go_sanitized(const char *s, size_t n)
{
	/* Sanitize the input to the set of valid characters, which must be '_' or be in the
	 * Unicode L or N categories. */
	Buf b = {0};
	for (size_t i = 0; i < n;) {
		uint32_t r;
		int w = utf8_decode(s + i, n - i, &r);
		i += (size_t)w;
		if (go_std_is_letter((int32_t)r) || go_std_is_digit((int32_t)r))
			add_rune(&b, r);
		else
			buf_addc(&b, '_');
	}
	/* Prepend '_' in the event of a Go keyword conflict or if the identifier is invalid (does
	 * not start in the Unicode L category). */
	uint32_t r = 0xfffd;
	if (b.n)
		utf8_decode(b.p, b.n, &r);
	if (go_std_is_keyword(b.p ? b.p : "", b.n) || !go_std_is_letter((int32_t)r))
		return xcat("_", buf_str(&b));
	return (char *)buf_str(&b);
}

static char to_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

const char *strs_trim_enum_prefix(const char *s0, const char *prefix)
{
	const char *s = s0;
	while (*s && *prefix) {
		if (*s == '_') {
			s++;
			continue;
		}
		/* unicode.ToLower(rune(s[0])): names are ASCII */
		if (to_lower(*s) != *prefix)
			return s0; /* no prefix match */
		s++, prefix++;
	}
	if (*prefix)
		return s0; /* no prefix match */
	while (*s == '_')
		s++;
	if (!*s)
		return s0; /* avoid returning empty string */
	return s;
}

char *strs_json_camel_case(const char *s)
{
	Buf b = {0};
	bool was_underscore = false;
	for (; *s; s++) { /* proto identifiers are always ASCII */
		char c = *s;
		if (c != '_') {
			if (was_underscore && is_ascii_lower(c))
				c = (char)(c - 'a' + 'A');
			buf_addc(&b, c);
		}
		was_underscore = c == '_';
	}
	return (char *)buf_str(&b);
}
