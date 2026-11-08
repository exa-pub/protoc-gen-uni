/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 internal/casing/camel.go. */
#include "grpcgateway.h"

#include <string.h>

/* Is c an ASCII lower-case letter? */
static bool isASCIILower(char c) { return 'a' <= c && c <= 'z'; }
/* Is c an ASCII digit? */
static bool isASCIIDigit(char c) { return '0' <= c && c <= '9'; }

/* Camel returns the CamelCased name.
 *
 * This was moved from the now deprecated github.com/golang/protobuf/protoc-gen-go/generator
 * package.
 *
 * If there is an interior underscore followed by a lower case letter, drop the underscore and
 * convert the letter to upper case. There is a remote possibility of this rewrite causing a name
 * collision, but it's so remote we're prepared to pretend it's nonexistent - since the C++
 * generator lowercases names, it's extremely unlikely to have two fields with different
 * capitalizations. In short, _my_field_name_2 becomes XMyFieldName_2. */
const char *gw_casing_Camel(const char *s)
{
	size_t n = strlen(s);
	if (n == 0)
		return "";
	char *t = xalloc(n + 2);
	size_t k = 0, i = 0;
	if (s[0] == '_') {
		/* Need a capital letter; drop the '_'. */
		t[k++] = 'X';
		i++;
	}
	/* Invariant: if the next letter is lower case, it must be converted to upper case. That
	 * is, we process a word at a time, where words are marked by _ or upper case letter. Digits
	 * are treated as words. */
	for (; i < n; i++) {
		char c = s[i];
		if (c == '_' && i + 1 < n && isASCIILower(s[i + 1]))
			continue; /* Skip the underscore in s. */
		if (isASCIIDigit(c)) {
			t[k++] = c;
			continue;
		}
		/* Assume we have a letter now - if not, it's a bogus identifier. The next word is a
		 * sequence of characters that must start upper case. */
		if (isASCIILower(c))
			c ^= ' '; /* Make it a capital letter. */
		t[k++] = c; /* Guaranteed not lower case. */
		/* Accept lower case sequence that follows. */
		while (i + 1 < n && isASCIILower(s[i + 1]))
			t[k++] = s[++i];
	}
	t[k] = 0;
	return t;
}

/* CamelIdentifier returns the CamelCased identifier without affecting the package name/path if
 * any. */
const char *gw_casing_CamelIdentifier(const char *s)
{
	if (!strchr(s, '.'))
		return gw_casing_Camel(s);
	/* filepath.Ext: from the last '.' of the last path element */
	size_t n = strlen(s), i = n;
	const char *ext = "";
	while (i > 0 && s[i - 1] != '/') {
		i--;
		if (s[i] == '.') {
			ext = s + i;
			break;
		}
	}
	size_t plen = n - strlen(ext);
	return xcat(xstrndup(s, plen), ".", gw_casing_Camel(*ext ? ext + 1 : ext));
}

/* JSONCamelCase converts a snake_case identifier to a camelCase identifier, according to the
 * protobuf JSON specification. */
const char *gw_casing_JSONCamelCase(const char *s)
{
	size_t n = strlen(s), k = 0;
	char *b = xalloc(n + 1);
	bool wasUnderscore = false;
	for (size_t i = 0; i < n; i++) { /* proto identifiers are always ASCII */
		char c = s[i];
		if (c != '_') {
			if (wasUnderscore && isASCIILower(c))
				c -= 'a' - 'A'; /* convert to uppercase */
			b[k++] = c;
		}
		wasUnderscore = c == '_';
	}
	b[k] = 0;
	return b;
}
