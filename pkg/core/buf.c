/* Growable buffers and the minimal formatter. */
#include "sys.h"

#include <string.h>

void buf_grow(Buf *b, size_t extra)
{
	if (extra >= SIZE_MAX / 2 - b->n)
		sys_die("protoc-gen-uni: out of memory\n");
	if (b->n + extra + 1 <= b->cap)
		return;
	size_t cap = b->cap ? b->cap : 64;
	while (cap < b->n + extra + 1)
		cap *= 2;
	b->p = xrealloc(b->p, b->cap, cap);
	b->cap = cap;
}

void buf_add(Buf *b, const void *p, size_t n)
{
	buf_grow(b, n);
	if (n)
		memcpy(b->p + b->n, p, n);
	b->n += n;
	b->p[b->n] = 0;
}

void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
void buf_addc(Buf *b, char c) { buf_add(b, &c, 1); }

void buf_vfmt(Buf *b, const char *fmt, va_list ap)
{
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			buf_addc(b, *fmt);
			continue;
		}
		fmt++;
		bool zero = *fmt == '0';
		int width = 0;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		int longs = 0;
		while (*fmt == 'l')
			longs++, fmt++;
		if (*fmt == 's') {
			buf_adds(b, va_arg(ap, const char *));
		} else if (*fmt == 'c') {
			buf_addc(b, (char)va_arg(ap, int));
		} else if (*fmt == 'd' || *fmt == 'x') {
			long long v = longs ? va_arg(ap, long long) : va_arg(ap, int);
			unsigned base = *fmt == 'x' ? 16 : 10;
			unsigned long long u = v < 0 && base == 10 ? 0ull - (unsigned long long)v : (unsigned long long)v;
			if (base == 16 && !longs)
				u = (unsigned)v;
			char t[24];
			int n = 0;
			do
				t[n++] = "0123456789abcdef"[u % base];
			while ((u /= base) && n < 24);
			if (v < 0 && base == 10)
				buf_addc(b, '-');
			for (int i = n; i < width; i++)
				buf_addc(b, zero ? '0' : ' ');
			while (n)
				buf_addc(b, t[--n]);
		} else if (*fmt == '%') {
			buf_addc(b, '%');
		} else {
			buf_addc(b, '%');
			if (!*fmt)
				break;
			buf_addc(b, *fmt);
		}
	}
	buf_str(b);
}

void buf_fmt(Buf *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(b, fmt, ap);
	va_end(ap);
}

const char *buf_str(Buf *b)
{
	if (!b->p)
		buf_grow(b, 0), b->p[0] = 0;
	return b->p;
}

char *xfmt(const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	return (char *)buf_str(&b);
}

char *xcat_(const char *first, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, first);
	for (const char *s = first; s; s = va_arg(ap, const char *))
		buf_adds(&b, s);
	va_end(ap);
	return (char *)buf_str(&b);
}
