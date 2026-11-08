// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd
//
// Copyright 2017 The Abseil Authors.
// Licensed under the Apache License, Version 2.0.

/* Ports of src/google/protobuf/io/strtod.cc (SimpleDtoa, SimpleFtoa, NoLocaleStrtod,
 * SafeDoubleToFloat) and of the Abseil string helpers libprotoc uses
 * (absl/strings/escaping.cc CEscape, CHexEscape, CUnescape; absl::StrCat of integers;
 * absl::SNPrintF("%.*g") through an exact decimal expansion, rounding ties to even like glibc;
 * absl/strings/numbers.cc SixDigitsToBuffer). */

/* ---- absl/strings/escaping.cc ---- */

static const char hex_char[] = "0123456789abcdef";

static bool ascii_isprint(unsigned char c) { return c >= 0x20 && c < 0x7f; }
static bool ascii_isxdigit(unsigned char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static bool ascii_isdigit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool ascii_isalpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool ascii_isalnum(unsigned char c) { return ascii_isalpha(c) || ascii_isdigit(c); }
static char ascii_toupper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c; }
static char ascii_tolower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
static int hex_digit_to_int(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	return (c | 0x20) - 'a' + 10;
}

/* CEscapeInternal(src, use_hex, utf8_safe) */
static char *cescape_internal(Str src, bool use_hex, bool utf8_safe)
{
	Buf b = {0};
	bool seen_hex_escape = false;
	for (size_t i = 0; i < src.n; i++) {
		unsigned char c = (unsigned char)src.p[i];
		bool is_hex_escape = false;
		switch (c) {
		case '\n': buf_adds(&b, "\\n"); break;
		case '\r': buf_adds(&b, "\\r"); break;
		case '\t': buf_adds(&b, "\\t"); break;
		case '\"': buf_adds(&b, "\\\""); break;
		case '\'': buf_adds(&b, "\\'"); break;
		case '\\': buf_adds(&b, "\\\\"); break;
		default:
			if ((!utf8_safe || c < 0x80) && (!ascii_isprint(c) || (seen_hex_escape && ascii_isxdigit(c)))) {
				if (use_hex) {
					buf_adds(&b, "\\x");
					buf_addc(&b, hex_char[c / 16]);
					buf_addc(&b, hex_char[c % 16]);
					is_hex_escape = true;
				} else {
					buf_addc(&b, '\\');
					buf_addc(&b, hex_char[c / 64]);
					buf_addc(&b, hex_char[(c % 64) / 8]);
					buf_addc(&b, hex_char[c % 8]);
				}
			} else {
				buf_addc(&b, (char)c);
			}
		}
		seen_hex_escape = is_hex_escape;
	}
	return xstrndup(b.p ? b.p : "", b.n);
}

char *lp_cescape(Str s) { return cescape_internal(s, false, false); }
char *lp_chexescape(Str s) { return cescape_internal(s, true, false); }
char *lp_utf8_safe_cescape(Str s) { return cescape_internal(s, false, true); }

static size_t encode_utf8(char *out, uint32_t r)
{
	if (r < 0x80) {
		out[0] = (char)r;
		return 1;
	}
	if (r < 0x800) {
		out[0] = (char)(0xc0 | (r >> 6));
		out[1] = (char)(0x80 | (r & 0x3f));
		return 2;
	}
	if (r < 0x10000) {
		out[0] = (char)(0xe0 | (r >> 12));
		out[1] = (char)(0x80 | ((r >> 6) & 0x3f));
		out[2] = (char)(0x80 | (r & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | (r >> 18));
	out[1] = (char)(0x80 | ((r >> 12) & 0x3f));
	out[2] = (char)(0x80 | ((r >> 6) & 0x3f));
	out[3] = (char)(0x80 | (r & 0x3f));
	return 4;
}

/* CUnescapeInternal(source, leave_nulls_escaped = false): only success matters here (upstream
 * reports "Invalid escaping in default value."). */
bool lp_cunescape(Str s, Str *out)
{
	char *d0 = xalloc(s.n + 1), *d = d0;
	const char *p = s.p, *end = s.p + s.n, *last_byte = end - 1;
	while (p < end) {
		if (*p != '\\') {
			*d++ = *p++;
			continue;
		}
		if (++p > last_byte)
			return false; /* String cannot end with \ */
		switch (*p) {
		case 'a': *d++ = '\a'; break;
		case 'b': *d++ = '\b'; break;
		case 'f': *d++ = '\f'; break;
		case 'n': *d++ = '\n'; break;
		case 'r': *d++ = '\r'; break;
		case 't': *d++ = '\t'; break;
		case 'v': *d++ = '\v'; break;
		case '\\': *d++ = '\\'; break;
		case '?': *d++ = '\?'; break;
		case '\'': *d++ = '\''; break;
		case '"': *d++ = '\"'; break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
			unsigned ch = (unsigned)(*p - '0');
			if (p < last_byte && p[1] >= '0' && p[1] <= '7')
				ch = ch * 8 + (unsigned)(*++p - '0');
			if (p < last_byte && p[1] >= '0' && p[1] <= '7')
				ch = ch * 8 + (unsigned)(*++p - '0');
			if (ch > 0xff)
				return false;
			*d++ = (char)ch;
			break;
		}
		case 'x': case 'X': {
			if (p >= last_byte || !ascii_isxdigit((unsigned char)p[1]))
				return false;
			unsigned ch = 0;
			while (p < last_byte && ascii_isxdigit((unsigned char)p[1])) {
				ch = (ch << 4) + (unsigned)hex_digit_to_int(*++p);
				if (ch > 0xff)
					return false;
			}
			*d++ = (char)ch;
			break;
		}
		case 'u': case 'U': {
			int n = *p == 'u' ? 4 : 8;
			uint32_t rune = 0;
			if (p + n >= end)
				return false;
			for (int i = 0; i < n; i++) {
				if (!ascii_isxdigit((unsigned char)p[1]))
					return false;
				uint32_t nr = (rune << 4) + (uint32_t)hex_digit_to_int(*++p);
				if (nr > 0x10FFFF)
					return false;
				rune = nr;
			}
			if (rune >= 0xD800 && rune <= 0xDFFF)
				return false;
			d += encode_utf8(d, rune);
			break;
		}
		default:
			return false; /* Unknown escape sequence */
		}
		p++;
	}
	*d = 0;
	out->p = d0;
	out->n = (size_t)(d - d0);
	return true;
}

/* absl::Base64Escape: the standard alphabet with padding. */
char *lp_base64_escape(Str s)
{
	char *raw = base64_raw_std((const uint8_t *)s.p, s.n);
	size_t n = strlen(raw);
	return n % 4 ? xcat(raw, n % 4 == 2 ? "==" : "=") : raw;
}

/* ---- absl::StrCat of integers ---- */

static char *lp_itoa(int64_t v) { return xfmt("%lld", (long long)v); }

static char *lp_utoa(uint64_t v)
{
	char t[24];
	int i = 23;
	t[i] = 0;
	do {
		t[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	return xstrdup(t + i);
}

/* ---- exact decimal expansion for %.*g ---- */

/* A big natural number in base 1e9, least significant limb first. */
typedef struct {
	uint32_t *l;
	int n;
} BigDec;

static void bigdec_mul_small(BigDec *b, uint32_t m)
{
	uint64_t carry = 0;
	for (int i = 0; i < b->n; i++) {
		uint64_t x = (uint64_t)b->l[i] * m + carry;
		b->l[i] = (uint32_t)(x % 1000000000u);
		carry = x / 1000000000u;
	}
	while (carry) {
		b->l[b->n++] = (uint32_t)(carry % 1000000000u);
		carry /= 1000000000u;
	}
}

/* The exact decimal digits of |v| (finite, non-zero): v = 0.digits x 10^dp. */
static char *exact_digits(double v, int *dp)
{
	uint64_t bits;
	memcpy(&bits, &v, 8);
	int bexp = (int)((bits >> 52) & 0x7ff);
	uint64_t mant = bits & ((1ull << 52) - 1);
	if (bexp == 0)
		bexp = 1;
	else
		mant |= 1ull << 52;
	int e = bexp - 1075; /* v = mant * 2^e */
	BigDec b;
	b.l = xalloc(sizeof(uint32_t) * 130);
	b.n = 0;
	while (mant) {
		b.l[b.n++] = (uint32_t)(mant % 1000000000u);
		mant /= 1000000000u;
	}
	int frac = 0; /* number of decimal digits after the point */
	if (e >= 0) {
		for (int i = 0; i < e; i++)
			bigdec_mul_small(&b, 2);
	} else {
		for (int i = 0; i < -e; i++)
			bigdec_mul_small(&b, 5);
		frac = -e;
	}
	Buf s = {0};
	buf_fmt(&s, "%d", (int)b.l[b.n - 1]);
	for (int i = b.n - 2; i >= 0; i--)
		buf_fmt(&s, "%09d", (int)b.l[i]);
	/* strip leading zeros (none) and trailing zeros */
	size_t n = s.n;
	while (n > 1 && s.p[n - 1] == '0')
		n--;
	*dp = (int)s.n - frac;
	return xstrndup(s.p, n);
}

char *lp_format_g(double v, int prec)
{
	if (prec == 0)
		prec = 1;
	bool neg = signbit(v) != 0;
	if (v == 0)
		return xstrdup(neg ? "-0" : "0");
	int dp;
	char *d = exact_digits(v, &dp);
	int n = (int)strlen(d);
	if (n > prec) {
		/* round to prec digits, ties to even */
		bool up;
		if (d[prec] > '5')
			up = true;
		else if (d[prec] < '5')
			up = false;
		else {
			up = false;
			for (int i = prec + 1; i < n; i++)
				if (d[i] != '0')
					up = true;
			if (!up)
				up = (d[prec - 1] - '0') % 2 == 1;
		}
		d[prec] = 0;
		n = prec;
		if (up) {
			int i = prec - 1;
			while (i >= 0 && d[i] == '9')
				d[i--] = '0';
			if (i < 0) {
				memmove(d + 1, d, (size_t)prec);
				d[0] = '1';
				d[prec] = 0;
				dp++;
			} else {
				d[i]++;
			}
		}
		while (n > 1 && d[n - 1] == '0')
			d[--n] = 0;
	}
	int x = dp - 1; /* the exponent of the first digit */
	Buf b = {0};
	if (neg)
		buf_addc(&b, '-');
	if (x < -4 || x >= prec) {
		buf_addc(&b, d[0]);
		if (n > 1) {
			buf_addc(&b, '.');
			buf_adds(&b, d + 1);
		}
		buf_fmt(&b, "e%c%02d", x < 0 ? '-' : '+', x < 0 ? -x : x);
	} else if (x < 0) {
		buf_adds(&b, "0.");
		for (int i = 0; i < -x - 1; i++)
			buf_addc(&b, '0');
		buf_adds(&b, d);
	} else {
		/* x + 1 integer digits */
		for (int i = 0; i <= x; i++)
			buf_addc(&b, i < n ? d[i] : '0');
		if (n > x + 1) {
			buf_addc(&b, '.');
			buf_adds(&b, d + x + 1);
		}
	}
	return xstrdup(buf_str(&b));
}

/* ---- absl/strings/numbers.cc SixDigitsToBuffer (absl::StrCat(double)) ---- */

/* A 128-bit number, high half first. */
typedef struct {
	uint64_t hi, lo;
} SixU128;

/* Mul32: num * mul, divided by 2 until it fits in 128 bits. */
static SixU128 six_mul32(SixU128 num, uint32_t mul)
{
	uint64_t bits0_31 = num.lo & 0xFFFFFFFF, bits32_63 = num.lo >> 32;
	uint64_t bits64_95 = num.hi & 0xFFFFFFFF, bits96_127 = num.hi >> 32;
	bits0_31 *= mul;
	bits32_63 *= mul;
	bits64_95 *= mul;
	bits96_127 *= mul;
	uint64_t bits0_63 = bits0_31 + (bits32_63 << 32);
	uint64_t bits64_127 = bits64_95 + (bits96_127 << 32) + (bits32_63 >> 32) + (bits0_63 < bits0_31);
	uint64_t bits128_up = (bits96_127 >> 32) + (bits64_127 < bits64_95);
	if (bits128_up == 0)
		return (SixU128){bits64_127, bits0_63};
	unsigned shift = 64u - (unsigned)__builtin_clzll(bits128_up); /* bit_width */
	uint64_t lo = (bits0_63 >> shift) + (bits64_127 << (64 - shift));
	uint64_t hi = (bits64_127 >> shift) + (bits128_up << (64 - shift));
	return (SixU128){hi, lo};
}

/* PowFive: the first 128 bits of num * 5^expfive, the first bit always one. */
static SixU128 six_pow_five(uint64_t num, int expfive)
{
	static const uint32_t powers_of_five[13] = {
		1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625, 48828125, 244140625,
	};
	SixU128 result = {num, 0};
	while (expfive >= 13) {
		result = six_mul32(result, 1220703125u); /* 5^13 */
		expfive -= 13;
	}
	result = six_mul32(result, powers_of_five[expfive & 15]);
	int shift = result.hi ? __builtin_clzll(result.hi) : 64;
	if (shift != 0) {
		result.hi = (result.hi << shift) + (result.lo >> (64 - shift));
		result.lo = result.lo << shift;
	}
	return result;
}

/* SplitToSix: value > 0 as 6 digits (the first never zero) and a base-10 exponent, rounding to
 * even. */
static int six_split(double value, char digits[6])
{
	int exp = 5;
	double d = value;
	if (d >= 999999.5) {
		if (d >= 1e+261) exp += 256, d *= 1e-256;
		if (d >= 1e+133) exp += 128, d *= 1e-128;
		if (d >= 1e+69) exp += 64, d *= 1e-64;
		if (d >= 1e+37) exp += 32, d *= 1e-32;
		if (d >= 1e+21) exp += 16, d *= 1e-16;
		if (d >= 1e+13) exp += 8, d *= 1e-8;
		if (d >= 1e+9) exp += 4, d *= 1e-4;
		if (d >= 1e+7) exp += 2, d *= 1e-2;
		if (d >= 1e+6) exp += 1, d *= 1e-1;
	} else {
		if (d < 1e-250) exp -= 256, d *= 1e256;
		if (d < 1e-122) exp -= 128, d *= 1e128;
		if (d < 1e-58) exp -= 64, d *= 1e64;
		if (d < 1e-26) exp -= 32, d *= 1e32;
		if (d < 1e-10) exp -= 16, d *= 1e16;
		if (d < 1e-2) exp -= 8, d *= 1e8;
		if (d < 1e+2) exp -= 4, d *= 1e4;
		if (d < 1e+4) exp -= 2, d *= 1e2;
		if (d < 1e+5) exp -= 1, d *= 1e1;
	}
	uint64_t d64k = (uint64_t)(d * 65536);
	uint32_t dddddd;
	if ((d64k % 65536) == 32767 || (d64k % 65536) == 32768) {
		/* close to the edge: redo the rounding with 128-bit numbers */
		dddddd = (uint32_t)(d64k / 65536);
		int exp2;
		double m = frexp(value, &exp2);
		uint64_t mantissa = (uint64_t)(m * (32768.0 * 65536.0 * 65536.0 * 65536.0));
		mantissa <<= 1;
		SixU128 edge, val;
		if (exp >= 6) {
			edge = six_pow_five(2 * (uint64_t)dddddd + 1, exp - 5);
			val = (SixU128){mantissa, 0};
		} else {
			edge = six_pow_five(2 * (uint64_t)dddddd + 1, 0);
			val = six_pow_five(mantissa, 5 - exp);
		}
		if (val.hi > edge.hi || (val.hi == edge.hi && val.lo > edge.lo))
			dddddd++;
		else if (val.hi == edge.hi && val.lo == edge.lo)
			dddddd += (dddddd & 1);
	} else {
		dddddd = (uint32_t)((d64k + 32768) / 65536);
	}
	if (dddddd == 1000000) {
		dddddd = 100000;
		exp += 1;
	}
	for (int i = 5; i >= 0; i--, dddddd /= 10)
		digits[i] = (char)('0' + dddddd % 10);
	return exp;
}

char *lp_six_digits(double d)
{
	if (isnan(d))
		return xstrdup("nan");
	if (d == 0)
		return xstrdup(signbit(d) ? "-0" : "0");
	Buf b = {0};
	if (d < 0) {
		buf_addc(&b, '-');
		d = -d;
	}
	if (d > DBL_MAX) {
		buf_adds(&b, "inf");
		return (char *)buf_str(&b);
	}
	char digits[6];
	int exp = six_split(d, digits);
	if (exp >= 0 && exp <= 5) {
		/* exp + 1 integer digits, then the others without trailing zeros */
		buf_add(&b, digits, (size_t)exp + 1);
		int n = 6;
		while (n > exp + 1 && digits[n - 1] == '0')
			n--;
		if (n > exp + 1) {
			buf_addc(&b, '.');
			buf_add(&b, digits + exp + 1, (size_t)(n - exp - 1));
		}
	} else if (exp >= -4 && exp < 0) {
		buf_adds(&b, "0.");
		for (int i = -1; i > exp; i--)
			buf_addc(&b, '0');
		int n = 6;
		while (digits[n - 1] == '0')
			n--;
		buf_add(&b, digits, (size_t)n);
	} else {
		buf_addc(&b, digits[0]);
		int n = 6;
		while (n > 1 && digits[n - 1] == '0')
			n--;
		if (n > 1) {
			buf_addc(&b, '.');
			buf_add(&b, digits + 1, (size_t)(n - 1));
		}
		buf_fmt(&b, "e%c%02d", exp > 0 ? '+' : '-', exp > 0 ? exp : -exp);
	}
	return (char *)buf_str(&b);
}

/* ---- io/strtod.cc ---- */

/* absl::from_chars(double, chars_format::general) syntax: [-] (inf|infinity|nan[(...)] |
 * digits[.digits][e[+-]digits]) without leading whitespace or '+'. Returns the end of the
 * longest valid prefix (s if none). */
static const char *from_chars_end(const char *s, bool *special)
{
	const char *p = s;
	*special = false;
	if (*p == '-')
		p++;
	static const char *const words[] = {"infinity", "inf", "nan"};
	for (int w = 0; w < 3; w++) {
		size_t k = strlen(words[w]), i;
		for (i = 0; i < k && ascii_tolower(p[i]) == words[w][i]; i++)
			;
		if (i == k) {
			*special = true;
			p += k;
			if (w == 2 && *p == '(') {
				const char *q = p + 1;
				while (ascii_isalnum((unsigned char)*q) || *q == '_')
					q++;
				if (*q == ')')
					p = q + 1;
			}
			return p;
		}
	}
	const char *m = p;
	int digits = 0;
	while (ascii_isdigit((unsigned char)*p))
		p++, digits++;
	if (*p == '.') {
		p++;
		while (ascii_isdigit((unsigned char)*p))
			p++, digits++;
	}
	if (!digits)
		return s;
	(void)m;
	if (*p == 'e' || *p == 'E') {
		const char *q = p + 1;
		if (*q == '+' || *q == '-')
			q++;
		if (ascii_isdigit((unsigned char)*q)) {
			while (ascii_isdigit((unsigned char)*q))
				q++;
			p = q;
		}
	}
	return p;
}

double lp_no_locale_strtod(const char *s, const char **end)
{
	bool special;
	const char *e = from_chars_end(s, &special);
	double ret = 0.0;
	if (e != s) {
		char *t = xstrndup(s, (size_t)(e - s));
		if (special) {
			bool neg = t[0] == '-';
			ret = ascii_tolower(t[neg]) == 'n' ? NAN : INFINITY;
			if (neg)
				ret = -ret;
		} else {
			ret = strtod(t, NULL); /* the syntax is checked, strtod rounds correctly */
		}
	}
	if (end)
		*end = e;
	return ret;
}

/* SafeDoubleToFloat */
static float safe_double_to_float(double value)
{
	const double max_float_as_double_rounded = 3.4028235677973366e+38;
	if (value > FLT_MAX) {
		if (value <= max_float_as_double_rounded)
			return FLT_MAX;
		return INFINITY;
	} else if (value < -FLT_MAX) {
		if (value >= -max_float_as_double_rounded)
			return -FLT_MAX;
		return -INFINITY;
	}
	return (float)value;
}

char *lp_simple_dtoa(double value)
{
	if (value == INFINITY)
		return xstrdup("inf");
	if (value == -INFINITY)
		return xstrdup("-inf");
	if (isnan(value))
		return xstrdup("nan");
	char *s = lp_format_g(value, DBL_DIG);
	if (lp_no_locale_strtod(s, NULL) != value)
		s = lp_format_g(value, DBL_DIG + 2);
	return s;
}

char *lp_simple_ftoa(float value)
{
	if (value == INFINITY)
		return xstrdup("inf");
	if (value == -INFINITY)
		return xstrdup("-inf");
	if (isnan(value))
		return xstrdup("nan");
	char *s = lp_format_g(value, FLT_DIG);
	/* safe_strtof: glibc's strtof sets ERANGE for (inexact) subnormal results, and a short
	 * decimal never denotes a subnormal float exactly */
	char *end;
	float parsed = strtof(s, &end);
	if (*end != 0 || parsed != value || fpclassify(parsed) == FP_SUBNORMAL)
		s = lp_format_g(value, FLT_DIG + 3);
	return s;
}
