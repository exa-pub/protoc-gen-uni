/* Port of stringcase.py of stringcase 1.2.0 (Copyright (c) 2015 Taka Okunishi, MIT): camelcase,
 * capitalcase, pascalcase and snakecase for ASCII strings. */
#include "stringcase.h"

#include <string.h>

/* Python re \w and \s on ASCII characters */
static bool is_word(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }

char *sc_camelcase(const char *s)
{
	/* re.sub(r"\w[\s\W]+\w", '', s): [\s\W] is \W on ASCII */
	size_t n = strlen(s);
	Buf b = {0};
	for (size_t i = 0; i < n;) {
		if (is_word(s[i]) && i + 1 < n && !is_word(s[i + 1])) {
			size_t j = i + 1;
			while (j < n && !is_word(s[j]))
				j++;
			if (j < n) { /* the run ends at a word character */
				i = j + 1;
				continue;
			}
		}
		buf_addc(&b, s[i++]);
	}
	const char *t = buf_str(&b);
	if (!*t)
		return "";
	/* lowercase(string[0]) + re.sub(r"[\-_\.\s]([a-z])", upper, string[1:]) */
	Buf r = {0};
	buf_addc(&r, lower(t[0]));
	for (size_t i = 1; t[i]; i++) {
		char c = t[i];
		bool sep = c == '-' || c == '_' || c == '.' || c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1c && c <= 0x1f);
		if (sep && t[i + 1] >= 'a' && t[i + 1] <= 'z') {
			buf_addc(&r, upper(t[i + 1]));
			i++;
			continue;
		}
		buf_addc(&r, c);
	}
	return (char *)buf_str(&r);
}

char *sc_capitalcase(const char *s)
{
	if (!*s)
		return "";
	char *r = xstrdup(s);
	r[0] = upper(r[0]);
	return r;
}

char *sc_pascalcase(const char *s) { return sc_capitalcase(sc_camelcase(s)); }

char *sc_snakecase(const char *s)
{
	/* re.sub(r"[\-\.\s]", '_', s) */
	char *t = xstrdup(s);
	for (char *p = t; *p; p++)
		if (*p == '-' || *p == '.' || *p == ' ' || (*p >= '\t' && *p <= '\r') || (*p >= 0x1c && *p <= 0x1f))
			*p = '_';
	if (!*t)
		return t;
	/* lowercase(string[0]) + re.sub(r"[A-Z]", '_' + lower, string[1:]) */
	Buf r = {0};
	buf_addc(&r, lower(t[0]));
	for (size_t i = 1; t[i]; i++) {
		if (t[i] >= 'A' && t[i] <= 'Z') {
			buf_addc(&r, '_');
			buf_addc(&r, lower(t[i]));
		} else {
			buf_addc(&r, t[i]);
		}
	}
	return (char *)buf_str(&r);
}
