/* printf-style formatting of str with one str argument: fmt % arg (Objects/unicodeobject.c
 * PyUnicode_Format), with its TypeError and ValueError texts. */
#include "python.h"

#include <string.h>

/* ascii(s): repr(s) with the non-ASCII characters escaped. */
static char *py_ascii_str(Str s)
{
	const char *r = py_repr_str(s);
	Buf b = {0};
	for (size_t i = 0, n = strlen(r); i < n;) {
		uint32_t c;
		int w = utf8_decode(r + i, n - i, &c);
		if (c < 0x80)
			buf_addc(&b, r[i]);
		else if (c < 0x100)
			buf_fmt(&b, "\\x%02x", (int)c);
		else if (c < 0x10000)
			buf_fmt(&b, "\\u%04x", (int)c);
		else
			buf_fmt(&b, "\\U%08x", (int)c);
		i += (size_t)w;
	}
	return (char *)buf_str(&b);
}

static size_t cp_count(Str s)
{
	size_t n = 0;
	for (size_t i = 0; i < s.n; i++)
		n += ((unsigned char)s.p[i] & 0xc0) != 0x80;
	return n;
}

/* The first n code points of s. */
static Str cp_prefix(Str s, size_t n)
{
	size_t i = 0;
	for (; i < s.n && n; n--) {
		i++;
		while (i < s.n && ((unsigned char)s.p[i] & 0xc0) == 0x80)
			i++;
	}
	return (Str){s.p, i};
}

PyFormat py_percent_format(Str fmt, Str arg, Buf *out, char **msg)
{
	bool used = false;
	size_t cp = 0; /* code point index of fmt[i] */
	*msg = NULL;
	for (size_t i = 0; i < fmt.n;) {
		if (fmt.p[i] != '%') {
			size_t start = i++;
			while (i < fmt.n && ((unsigned char)fmt.p[i] & 0xc0) == 0x80)
				i++;
			buf_add(out, fmt.p + start, i - start);
			cp++;
			continue;
		}
		i++;
		cp++;
		if (i < fmt.n && fmt.p[i] == '%') {
			buf_addc(out, '%');
			i++;
			cp++;
			continue;
		}
		if (i < fmt.n && fmt.p[i] == '(') {
			*msg = "format requires a mapping";
			return PY_FORMAT_TYPE_ERROR;
		}
		bool left = false;
		for (; i < fmt.n && strchr("-+ #0", fmt.p[i]); i++, cp++)
			if (fmt.p[i] == '-')
				left = true;
		size_t width = 0, prec = (size_t)-1;
		if (i < fmt.n && fmt.p[i] == '*') {
			*msg = used ? "not enough arguments for format string" : "* wants int";
			return PY_FORMAT_TYPE_ERROR;
		}
		for (; i < fmt.n && fmt.p[i] >= '0' && fmt.p[i] <= '9'; i++, cp++) {
			width = width * 10 + (size_t)(fmt.p[i] - '0');
			if (width > 0x7fffffff) {
				*msg = "width too big";
				return PY_FORMAT_VALUE_ERROR;
			}
		}
		if (i < fmt.n && fmt.p[i] == '.') {
			i++;
			cp++;
			prec = 0;
			if (i < fmt.n && fmt.p[i] == '*') {
				*msg = used ? "not enough arguments for format string" : "* wants int";
				return PY_FORMAT_TYPE_ERROR;
			}
			for (; i < fmt.n && fmt.p[i] >= '0' && fmt.p[i] <= '9'; i++, cp++) {
				prec = prec * 10 + (size_t)(fmt.p[i] - '0');
				if (prec > 0x7fffffff) {
					*msg = "precision too big";
					return PY_FORMAT_VALUE_ERROR;
				}
			}
		}
		if (i < fmt.n && strchr("hlL", fmt.p[i]) && fmt.p[i]) {
			i++;
			cp++;
		}
		if (i >= fmt.n) {
			*msg = "incomplete format";
			return PY_FORMAT_VALUE_ERROR;
		}
		uint32_t c;
		int w = utf8_decode(fmt.p + i, fmt.n - i, &c);
		i += (size_t)w;
		cp++;
		if (used) {
			*msg = "not enough arguments for format string";
			return PY_FORMAT_TYPE_ERROR;
		}
		used = true;
		if (!strchr("srabdiuoxXeEfFgGc", (int)c) || c == 'b' || !c) {
			*msg = xfmt("unsupported format character '%c' (0x%x) at index %d", c >= 32 && c <= 126 ? (int)c : '?',
				(int)c, (int)(cp - 1));
			return PY_FORMAT_VALUE_ERROR;
		}
		Str v;
		switch (c) {
		case 's': v = arg; break;
		case 'r': v = str_c(py_repr_str(arg)); break;
		case 'a': v = str_c(py_ascii_str(arg)); break;
		case 'c':
			if (cp_count(arg) != 1) {
				*msg = "%c requires int or char";
				return PY_FORMAT_TYPE_ERROR;
			}
			v = arg;
			break;
		case 'd': case 'i': case 'u':
			*msg = xfmt("%%%c format: a real number is required, not str", (int)c);
			return PY_FORMAT_TYPE_ERROR;
		case 'o': case 'x': case 'X':
			*msg = xfmt("%%%c format: an integer is required, not str", (int)c);
			return PY_FORMAT_TYPE_ERROR;
		default:
			*msg = "must be real number, not str";
			return PY_FORMAT_TYPE_ERROR;
		}
		if (prec != (size_t)-1 && c != 'c')
			v = cp_prefix(v, prec);
		size_t len = cp_count(v);
		if (!left)
			for (size_t k = len; k < width; k++)
				buf_addc(out, ' ');
		buf_add_str(out, v);
		if (left)
			for (size_t k = len; k < width; k++)
				buf_addc(out, ' ');
	}
	if (!used) {
		*msg = "not all arguments converted during string formatting";
		return PY_FORMAT_TYPE_ERROR;
	}
	return PY_FORMAT_OK;
}
