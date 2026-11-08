/* Copyright 2009 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* strconv.ParseFloat (atof.go): the syntax of readFloat and special, then the value. The value
 * of a valid number comes from strtod, which rounds correctly like Go (decimal and hexadecimal
 * mantissas); out of range is ErrRange with ±Inf, as Go returns it. strconv_lower and
 * strconv_underscoreOK are in atoi.c. */

#include <math.h>
#include <stdlib.h>

/* commonPrefixLenIgnoreCase returns the length of the common prefix of s and prefix, with the
 * comparison of ASCII letters case-insensitive (prefix is lower case). */
static size_t strconv_commonPrefixLenIgnoreCase(const char *s, size_t n, const char *prefix)
{
	size_t k = strlen(prefix);
	if (k > n)
		k = n;
	for (size_t i = 0; i < k; i++) {
		char c = s[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 'a' - 'A');
		if (c != prefix[i])
			return i;
	}
	return k;
}

/* special: infinity and NaN; the length consumed, 0 if none */
static size_t strconv_special(const char *s, size_t n, double *f)
{
	if (n == 0)
		return 0;
	int sign = 1;
	size_t nsign = 0;
	switch (s[0]) {
	case '+':
	case '-': {
		if (s[0] == '-')
			sign = -1;
		nsign = 1;
		s++, n--;
	}
		/* fallthrough */
	case 'i':
	case 'I': {
		size_t k = strconv_commonPrefixLenIgnoreCase(s, n, "infinity");
		/* Anything longer than "inf" is ok, but if we dont have "infinity", only consume
		 * "inf". */
		if (3 < k && k < 8)
			k = 3;
		if (k == 3 || k == 8) {
			*f = sign > 0 ? HUGE_VAL : -HUGE_VAL;
			return nsign + k;
		}
		break;
	}
	case 'n':
	case 'N':
		if (strconv_commonPrefixLenIgnoreCase(s, n, "nan") == 3) {
			*f = NAN;
			return 3;
		}
		break;
	}
	return 0;
}

/* readFloat: the length of the number at s, 0 if it is not one */
static size_t strconv_readFloat(const char *s, size_t n)
{
	size_t i = 0;
	bool underscores = false;
	if (i >= n)
		return 0;
	if (s[i] == '+' || s[i] == '-')
		i++;
	int base = 10;
	char expChar = 'e';
	if (i + 2 < n && s[i] == '0' && strconv_lower(s[i + 1]) == 'x') {
		base = 16;
		i += 2;
		expChar = 'p';
	}
	bool sawdot = false, sawdigits = false;
	for (; i < n; i++) {
		char c = s[i];
		if (c == '_') {
			underscores = true;
			continue;
		}
		if (c == '.') {
			if (sawdot)
				break;
			sawdot = true;
			continue;
		}
		if ((c >= '0' && c <= '9') || (base == 16 && strconv_lower(c) >= 'a' && strconv_lower(c) <= 'f')) {
			sawdigits = true;
			continue;
		}
		break;
	}
	if (!sawdigits)
		return 0;
	if (i < n && strconv_lower(s[i]) == expChar) {
		i++;
		if (i >= n)
			return 0;
		if (s[i] == '+' || s[i] == '-')
			i++;
		if (i >= n || s[i] < '0' || s[i] > '9')
			return 0;
		for (; i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == '_'); i++)
			if (s[i] == '_')
				underscores = true;
	} else if (base == 16) {
		return 0; /* Must have exponent. */
	}
	if (underscores && !strconv_underscoreOK(s, i))
		return 0;
	return i;
}

int go_std_parse_float(const char *s, size_t n, int bit_size, double *out)
{
	*out = 0;
	size_t k = strconv_special(s, n, out);
	if (k) {
		if (k != n) {
			*out = 0;
			return GO_STD_ERR_SYNTAX;
		}
		return 0;
	}
	k = strconv_readFloat(s, n);
	if (k == 0 || k != n)
		return GO_STD_ERR_SYNTAX;
	char *t = xalloc(n + 1);
	size_t m = 0;
	for (size_t i = 0; i < n; i++)
		if (s[i] != '_')
			t[m++] = s[i];
	t[m] = 0;
	double f = strtod(t, NULL);
	if (bit_size == 32)
		f = (double)(float)f;
	*out = f;
	if (f == HUGE_VAL || f == -HUGE_VAL)
		return GO_STD_ERR_RANGE;
	return 0;
}
