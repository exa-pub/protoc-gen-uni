/* int(s) and float(s) of a str (Objects/longobject.c PyLong_FromUnicodeObject, Python/pystrtod.c
 * and _Py_string_to_number_with_underscores): surrounding whitespace, a sign, digits with single
 * underscores between them; float() also takes a fraction, an exponent, inf, infinity and nan in
 * any case. Unicode decimal digits other than 0-9 (accepted by CPython) are reported as
 * unsupported. */
#include "python.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The str without surrounding whitespace; PY_NUM_UNSUPPORTED if a non-ASCII character remains. */
static PyNum trim(Str *s)
{
	*s = py_strip(*s);
	for (size_t i = 0; i < s->n; i++)
		if ((unsigned char)s->p[i] >= 0x80)
			return PY_NUM_UNSUPPORTED;
	return PY_NUM_OK;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* Digits with single underscores between them, starting at s[*i]; appends the digits to b.
 * Returns the number of digits (0: none or a misplaced underscore). */
static size_t digits(Str s, size_t *i, Buf *b)
{
	size_t n = 0;
	while (*i < s.n) {
		char c = s.p[*i];
		if (is_digit(c)) {
			buf_addc(b, c);
			n++;
			(*i)++;
		} else if (c == '_' && n && *i + 1 < s.n && is_digit(s.p[*i + 1])) {
			(*i)++;
		} else {
			break;
		}
	}
	return n;
}

#define INT_MAX_STR_DIGITS 4300 /* sys.int_info.default_max_str_digits */

PyNum py_parse_int(Str s, char **repr)
{
	PyNum st = trim(&s);
	if (st != PY_NUM_OK)
		return st;
	size_t i = 0;
	bool neg = false;
	if (i < s.n && (s.p[i] == '+' || s.p[i] == '-'))
		neg = s.p[i++] == '-';
	Buf b = {0};
	size_t n = digits(s, &i, &b);
	if (!n || i != s.n || n > INT_MAX_STR_DIGITS)
		return PY_NUM_ERROR;
	const char *d = b.p;
	while (*d == '0' && d[1])
		d++;
	*repr = neg && *d != '0' ? xcat("-", d) : xstrdup(d);
	return PY_NUM_OK;
}

static bool word(Str s, size_t i, const char *w)
{
	size_t n = strlen(w);
	if (s.n - i != n)
		return false;
	for (size_t k = 0; k < n; k++)
		if ((s.p[i + k] | 0x20) != w[k])
			return false;
	return true;
}

PyNum py_parse_float(Str s, double *out)
{
	PyNum st = trim(&s);
	if (st != PY_NUM_OK)
		return st;
	size_t i = 0;
	bool neg = false;
	if (i < s.n && (s.p[i] == '+' || s.p[i] == '-'))
		neg = s.p[i++] == '-';
	if (word(s, i, "inf") || word(s, i, "infinity")) {
		*out = neg ? -(double)INFINITY : (double)INFINITY;
		return PY_NUM_OK;
	}
	if (word(s, i, "nan")) {
		*out = neg ? -(double)NAN : (double)NAN;
		return PY_NUM_OK;
	}
	Buf b = {0};
	buf_addc(&b, neg ? '-' : '+');
	size_t n = digits(s, &i, &b);
	if (i < s.n && s.p[i] == '.') {
		buf_addc(&b, '.');
		i++;
		n += digits(s, &i, &b);
	}
	if (!n)
		return PY_NUM_ERROR;
	if (i < s.n && (s.p[i] | 0x20) == 'e') {
		buf_addc(&b, 'e');
		i++;
		if (i < s.n && (s.p[i] == '+' || s.p[i] == '-'))
			buf_addc(&b, s.p[i++]);
		if (!digits(s, &i, &b))
			return PY_NUM_ERROR;
	}
	if (i != s.n)
		return PY_NUM_ERROR;
	*out = strtod(b.p, NULL); /* correctly rounded, like _Py_dg_strtod */
	return PY_NUM_OK;
}
