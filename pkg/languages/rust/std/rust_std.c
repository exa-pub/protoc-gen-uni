/* Copyright The Rust Project Developers. Licensed under the Apache License, Version 2.0 or the MIT
 * license, at your option.
 *
 * Ports of library/core/src/char/methods.rs (is_whitespace, escape_default),
 * library/core/src/str/mod.rs (trim, lines, escape_default) and library/core/src/ascii.rs
 * (escape_default), the slice error panic of library/core/src/str/mod.rs (slice_error_fail_rt) of
 * Rust 1.91.1; char::escape_debug and the float Display of
 * library/core/src/fmt/float.rs (float_to_decimal_common_shortest) and num/flt2dec (Rust 1.98.0,
 * the toolchain of the anthropics/buffa remote plugin; unchanged since 1.91). */
#include "rust_std.h"

#include "num.h"

#include <math.h>

bool rust_is_whitespace(uint32_t c)
{
	switch (c) {
	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ': case 0x85: case 0xa0:
	case 0x1680: case 0x2028: case 0x2029: case 0x202f: case 0x205f: case 0x3000:
		return true;
	}
	return c >= 0x2000 && c <= 0x200a;
}

Str rust_trim(Str s)
{
	while (s.n) {
		uint32_t r;
		int w = utf8_decode(s.p, s.n, &r);
		if (!rust_is_whitespace(r))
			break;
		s.p += w, s.n -= (size_t)w;
	}
	while (s.n) {
		/* the start of the last char: skip continuation bytes (at most 3) */
		size_t k = s.n - 1;
		while (k > 0 && s.n - k < 4 && ((unsigned char)s.p[k] & 0xc0) == 0x80)
			k--;
		uint32_t r;
		int w = utf8_decode(s.p + k, s.n - k, &r);
		if ((size_t)w != s.n - k) /* invalid: a single byte decoded as U+FFFD */
			k = s.n - 1, r = 0xfffd;
		if (!rust_is_whitespace(r))
			break;
		s.n = k;
	}
	return s;
}

int rust_lines(Str s, Str **out)
{
	Vec v = {0};
	size_t i = 0;
	while (i < s.n) {
		size_t j = i;
		while (j < s.n && s.p[j] != '\n')
			j++;
		Str *line = xalloc(sizeof *line);
		*line = (Str){s.p + i, j - i};
		if (j < s.n && line->n && line->p[line->n - 1] == '\r')
			line->n--;
		vec_push(&v, line);
		i = j < s.n ? j + 1 : j;
	}
	Str *arr = xalloc(sizeof *arr * (size_t)(v.n ? v.n : 1));
	for (int k = 0; k < v.n; k++)
		arr[k] = *VEC_AT(v, Str, k);
	*out = arr;
	return v.n;
}

void rust_char_escape_default(Buf *b, uint32_t c)
{
	switch (c) {
	case '\t': buf_adds(b, "\\t"); return;
	case '\r': buf_adds(b, "\\r"); return;
	case '\n': buf_adds(b, "\\n"); return;
	case '\'': buf_adds(b, "\\'"); return;
	case '"': buf_adds(b, "\\\""); return;
	case '\\': buf_adds(b, "\\\\"); return;
	}
	if (c >= 0x20 && c <= 0x7e) {
		buf_addc(b, (char)c);
		return;
	}
	buf_fmt(b, "\\u{%x}", (unsigned)c);
}

void rust_str_escape_default(Buf *b, Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		i += (size_t)utf8_decode(s.p + i, s.n - i, &r);
		rust_char_escape_default(b, r);
	}
}

void rust_str_debug(Buf *b, Str s)
{
	buf_addc(b, '"');
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		switch (c) {
		case '"': buf_adds(b, "\\\""); continue;
		case '\\': buf_adds(b, "\\\\"); continue;
		case '\t': buf_adds(b, "\\t"); continue;
		case '\r': buf_adds(b, "\\r"); continue;
		case '\n': buf_adds(b, "\\n"); continue;
		case 0: buf_adds(b, "\\0"); continue;
		}
		if (c < 0x20 || c == 0x7f)
			buf_fmt(b, "\\u{%x}", (unsigned)c);
		else
			buf_addc(b, (char)c);
	}
	buf_addc(b, '"');
}

void rust_ascii_escape_default(Buf *b, uint8_t c)
{
	switch (c) {
	case '\t': buf_adds(b, "\\t"); return;
	case '\r': buf_adds(b, "\\r"); return;
	case '\n': buf_adds(b, "\\n"); return;
	case '\'': buf_adds(b, "\\'"); return;
	case '"': buf_adds(b, "\\\""); return;
	case '\\': buf_adds(b, "\\\\"); return;
	}
	if (c >= 0x20 && c <= 0x7e) {
		buf_addc(b, (char)c);
		return;
	}
	buf_fmt(b, "\\x%02x", (unsigned)c);
}

void rust_char_escape_debug(Buf *b, uint32_t c)
{
	switch (c) {
	case 0: buf_adds(b, "\\0"); return;
	case '\t': buf_adds(b, "\\t"); return;
	case '\r': buf_adds(b, "\\r"); return;
	case '\n': buf_adds(b, "\\n"); return;
	case '\\': buf_adds(b, "\\\\"); return;
	case '"': buf_adds(b, "\\\""); return;
	case '\'': buf_adds(b, "\\'"); return;
	}
	/* Approximation of !is_printable || is_grapheme_extended for non-ASCII: controls, Zs/Zl/Zp
	 * spaces, common Cf format chars, surrogates, private use, noncharacters, combining marks. */
	bool escape = c < 0x20 || (c >= 0x7f && c <= 0xa0) || c == 0xad || (c >= 0x300 && c <= 0x36f) ||
		      (c >= 0x600 && c <= 0x605) || c == 0x61c || c == 0x6dd || c == 0x70f || c == 0x1680 ||
		      c == 0x180e || (c >= 0x2000 && c <= 0x200f) || (c >= 0x2028 && c <= 0x202f) ||
		      (c >= 0x205f && c <= 0x206f) || c == 0x3000 || (c >= 0xd800 && c <= 0xf8ff) ||
		      c == 0xfeff || (c >= 0xfff9 && c <= 0xfffb) || c == 0xfffe || c == 0xffff || c > 0x10ffff;
	if (!escape) {
		char u[4];
		int n;
		if (c < 0x80) {
			u[0] = (char)c, n = 1;
		} else if (c < 0x800) {
			u[0] = (char)(0xc0 | (c >> 6)), u[1] = (char)(0x80 | (c & 0x3f)), n = 2;
		} else if (c < 0x10000) {
			u[0] = (char)(0xe0 | (c >> 12)), u[1] = (char)(0x80 | ((c >> 6) & 0x3f));
			u[2] = (char)(0x80 | (c & 0x3f)), n = 3;
		} else {
			u[0] = (char)(0xf0 | (c >> 18)), u[1] = (char)(0x80 | ((c >> 12) & 0x3f));
			u[2] = (char)(0x80 | ((c >> 6) & 0x3f)), u[3] = (char)(0x80 | (c & 0x3f)), n = 4;
		}
		buf_add(b, u, (size_t)n);
		return;
	}
	buf_fmt(b, "\\u{%x}", (unsigned)c);
}

static bool is_utf8_cont(Str s, size_t i)
{
	return i < s.n && ((unsigned char)s.p[i] & 0xc0) == 0x80;
}

void rust_str_slice_end_fail(Str s, size_t index)
{
	size_t trunc = s.n < 256 ? s.n : 256; /* floor_char_boundary(MAX_DISPLAY_LENGTH) */
	while (trunc && is_utf8_cont(s, trunc))
		trunc--;
	const char *ellipsis = trunc < s.n ? "[...]" : "";
	Buf b = {0};
	if (index > s.n) {
		buf_fmt(&b, "byte index %lld is out of bounds of `", (long long)index);
	} else {
		size_t start = index; /* floor_char_boundary(index) */
		while (start && is_utf8_cont(s, start))
			start--;
		uint32_t c;
		int w = utf8_decode(s.p + start, s.n - start, &c);
		buf_fmt(&b, "byte index %lld is not a char boundary; it is inside '", (long long)index);
		rust_char_escape_debug(&b, c);
		buf_fmt(&b, "' (bytes %lld..%lld) of `", (long long)start, (long long)(start + (size_t)w));
	}
	buf_add(&b, s.p, trunc);
	buf_adds(&b, "`");
	buf_adds(&b, ellipsis);
	fail_str(buf_view(&b));
}

void rust_float_display(Buf *b, double v, int bits)
{
	if (v != v) {
		buf_adds(b, "NaN");
		return;
	}
	if (signbit(v))
		buf_addc(b, '-');
	if (v == INFINITY || v == -INFINITY) {
		buf_adds(b, "inf");
		return;
	}
	if (v == 0) {
		buf_addc(b, '0');
		return;
	}
	NumDecimal d;
	num_shortest(v, bits, &d);
	/* digits_to_dec_str(digits, exp, frac_digits = 0): value = 0.d1d2... x 10^exp */
	if (d.dp <= 0) {
		buf_adds(b, "0.");
		for (int i = 0; i < -d.dp; i++)
			buf_addc(b, '0');
		buf_add(b, d.d, (size_t)d.nd);
	} else if (d.dp < d.nd) {
		buf_add(b, d.d, (size_t)d.dp);
		buf_addc(b, '.');
		buf_add(b, d.d + d.dp, (size_t)(d.nd - d.dp));
	} else {
		buf_add(b, d.d, (size_t)d.nd);
		for (int i = d.nd; i < d.dp; i++)
			buf_addc(b, '0');
	}
}
