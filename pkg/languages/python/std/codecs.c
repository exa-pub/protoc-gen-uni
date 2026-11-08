/* The texts of UnicodeDecodeError and UnicodeEncodeError and the error positions of the strict
 * UTF-8 decoder of CPython 3.13 (Objects/exceptions.c UnicodeDecodeError_str and
 * UnicodeEncodeError_str; Objects/stringlib/codecs.h utf8_decode and Objects/unicodeobject.c
 * unicode_decode_utf8).
 *
 * Copyright (c) 2001 Python Software Foundation; All Rights Reserved.
 * Licensed under the Python Software Foundation License Version 2. */
#include "python.h"

COLD char *py_unicode_decode_error(const char *encoding, Str object, size_t start, size_t end, const char *reason)
{
	if (start < object.n && end == start + 1)
		return xfmt("UnicodeDecodeError: '%s' codec can't decode byte 0x%02x in position %lld: %s", encoding,
			(unsigned char)object.p[start], (long long)start, reason);
	return xfmt("UnicodeDecodeError: '%s' codec can't decode bytes in position %lld-%lld: %s", encoding,
		(long long)start, (long long)end - 1, reason);
}

COLD char *py_unicode_encode_error(const char *encoding, const uint32_t *object, size_t n, size_t start, size_t end,
	const char *reason)
{
	if (start < n && end == start + 1) {
		uint32_t c = object[start];
		const char *pre = xfmt("UnicodeEncodeError: '%s' codec can't encode character ", encoding);
		const char *ch = c <= 0xff ? xfmt("'\\x%02x'", (int)c) : c <= 0xffff ? xfmt("'\\u%04x'", (int)c) :
			xfmt("'\\U%08x'", (int)c);
		return xfmt("%s%s in position %lld: %s", pre, ch, (long long)start, reason);
	}
	return xfmt("UnicodeEncodeError: '%s' codec can't encode characters in position %lld-%lld: %s", encoding,
		(long long)start, (long long)end - 1, reason);
}

static bool cont(uint8_t c) { return c >= 0x80 && c <= 0xbf; }

char *py_utf8_decode_error(Str s)
{
	const uint8_t *p = (const uint8_t *)s.p;
	size_t n = s.n;
	for (size_t i = 0; i < n;) {
		uint8_t c = p[i];
		size_t left = n - i, bad; /* bad: the length of the invalid sequence */
		const char *reason = "invalid continuation byte";
		if (c < 0x80) {
			i++;
			continue;
		}
		if (c < 0xc2 || c >= 0xf5) {
			bad = 1, reason = "invalid start byte";
		} else if (c < 0xe0) {
			if (left < 2)
				goto truncated;
			if (cont(p[i + 1])) {
				i += 2;
				continue;
			}
			bad = 1;
		} else if (c < 0xf0) {
			bool second = left >= 2 && cont(p[i + 1]) && !(p[i + 1] < 0xa0 ? c == 0xe0 : c == 0xed);
			if (left < 2 || (left < 3 && second))
				goto truncated;
			if (!second)
				bad = 1;
			else if (!cont(p[i + 2]))
				bad = 2;
			else {
				i += 3;
				continue;
			}
		} else {
			bool second = left >= 2 && cont(p[i + 1]) && !(p[i + 1] < 0x90 ? c == 0xf0 : c == 0xf4);
			if (left < 2)
				goto truncated;
			if (!second)
				bad = 1;
			else if (left < 3)
				goto truncated;
			else if (!cont(p[i + 2]))
				bad = 2;
			else if (left < 4)
				goto truncated;
			else if (!cont(p[i + 3]))
				bad = 3;
			else {
				i += 4;
				continue;
			}
		}
		return py_unicode_decode_error("utf-8", s, i, i + bad, reason);
	truncated:
		return py_unicode_decode_error("utf-8", s, i, n, "unexpected end of data");
	}
	return NULL;
}
