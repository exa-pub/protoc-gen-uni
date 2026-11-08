/* repr() of str, bytes, int, bool and float as CPython 3.13 prints them: Objects/unicodeobject.c
 * unicode_repr, Objects/bytesobject.c PyBytes_Repr, Python/pystrtod.c format_float_short with
 * float_repr_style "short" (the digits of Python/dtoa.c mode 0: the shortest string that rounds
 * to the value, the closest one if there are several; computed here with the free-format
 * algorithm of Burger and Dybvig on exact big integers). */
#include "python.h"

#include <string.h>

static const char hexdig[] = "0123456789abcdef";

static void add_hex(Buf *b, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--)
		buf_addc(b, hexdig[(v >> (4 * i)) & 15]);
}

static char quote_for(Str s)
{
	bool single = memchr(s.p, '\'', s.n) != NULL, dbl = memchr(s.p, '"', s.n) != NULL;
	return single && !dbl ? '"' : '\'';
}

char *py_repr_str(Str s)
{
	char q = quote_for(s);
	Buf b = {0};
	buf_addc(&b, q);
	for (size_t i = 0; i < s.n;) {
		uint32_t c;
		int w = utf8_decode(s.p + i, s.n - i, &c);
		bool invalid = c == 0xfffd && w == 1;
		if (invalid)
			c = (unsigned char)s.p[i]; /* shown as \xHH */
		i += (size_t)w;
		if (c == (uint32_t)q || c == '\\') {
			buf_addc(&b, '\\');
			buf_addc(&b, (char)c);
		} else if (c == '\t') {
			buf_adds(&b, "\\t");
		} else if (c == '\n') {
			buf_adds(&b, "\\n");
		} else if (c == '\r') {
			buf_adds(&b, "\\r");
		} else if (c < ' ' || c == 0x7f || invalid) {
			buf_adds(&b, "\\x");
			add_hex(&b, c, 2);
		} else if (c < 0x7f || py_isprintable(c)) {
			buf_add(&b, s.p + i - (size_t)w, (size_t)w);
		} else if (c <= 0xff) {
			buf_adds(&b, "\\x");
			add_hex(&b, c, 2);
		} else if (c <= 0xffff) {
			buf_adds(&b, "\\u");
			add_hex(&b, c, 4);
		} else {
			buf_adds(&b, "\\U");
			add_hex(&b, c, 8);
		}
	}
	buf_addc(&b, q);
	return (char *)buf_str(&b);
}

char *py_repr_bytes(Str s)
{
	char q = quote_for(s);
	Buf b = {0};
	buf_addc(&b, 'b');
	buf_addc(&b, q);
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		if (c == (unsigned char)q || c == '\\') {
			buf_addc(&b, '\\');
			buf_addc(&b, (char)c);
		} else if (c == '\t') {
			buf_adds(&b, "\\t");
		} else if (c == '\n') {
			buf_adds(&b, "\\n");
		} else if (c == '\r') {
			buf_adds(&b, "\\r");
		} else if (c < ' ' || c >= 0x7f) {
			buf_adds(&b, "\\x");
			add_hex(&b, c, 2);
		} else {
			buf_addc(&b, (char)c);
		}
	}
	buf_addc(&b, q);
	return (char *)buf_str(&b);
}

char *py_repr_uint(uint64_t v)
{
	char tmp[24];
	int i = (int)sizeof tmp;
	tmp[--i] = 0;
	do {
		tmp[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	return xstrdup(tmp + i);
}

char *py_repr_int(int64_t v)
{
	if (v >= 0)
		return py_repr_uint((uint64_t)v);
	return xcat("-", py_repr_uint(-(uint64_t)v));
}

const char *py_repr_bool(bool v) { return v ? "True" : "False"; }

/* ---- float ---- */

/* Non-negative big integers, little-endian 32-bit words. 2^1080 * 10^330 < 2^2200. */
#define BIG_WORDS 72
typedef struct {
	uint32_t w[BIG_WORDS];
	int n;
} Big;

static void big_set(Big *a, uint64_t v)
{
	a->n = 0;
	while (v) {
		a->w[a->n++] = (uint32_t)v;
		v >>= 32;
	}
}

static void big_mul_small(Big *a, uint32_t m)
{
	uint64_t carry = 0;
	for (int i = 0; i < a->n; i++) {
		uint64_t t = (uint64_t)a->w[i] * m + carry;
		a->w[i] = (uint32_t)t;
		carry = t >> 32;
	}
	if (carry)
		a->w[a->n++] = (uint32_t)carry;
}

static void big_shl(Big *a, int bits)
{
	if (!a->n)
		return;
	int words = bits / 32, rem = bits % 32;
	if (rem) {
		uint32_t carry = 0;
		for (int i = 0; i < a->n; i++) {
			uint32_t t = a->w[i];
			a->w[i] = t << rem | carry;
			carry = t >> (32 - rem);
		}
		if (carry)
			a->w[a->n++] = carry;
	}
	if (words) {
		memmove(a->w + words, a->w, sizeof(uint32_t) * (size_t)a->n);
		memset(a->w, 0, sizeof(uint32_t) * (size_t)words);
		a->n += words;
	}
}

static void big_mul_pow10(Big *a, int k)
{
	for (; k >= 9; k -= 9)
		big_mul_small(a, 1000000000u);
	static const uint32_t p10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
	if (k)
		big_mul_small(a, p10[k]);
}

static int big_cmp(const Big *a, const Big *b)
{
	if (a->n != b->n)
		return a->n < b->n ? -1 : 1;
	for (int i = a->n - 1; i >= 0; i--)
		if (a->w[i] != b->w[i])
			return a->w[i] < b->w[i] ? -1 : 1;
	return 0;
}

static void big_add(Big *out, const Big *a, const Big *b)
{
	const Big *x = a->n >= b->n ? a : b, *y = a->n >= b->n ? b : a;
	uint64_t carry = 0;
	int n = x->n;
	for (int i = 0; i < n; i++) {
		uint64_t t = (uint64_t)x->w[i] + (i < y->n ? y->w[i] : 0) + carry;
		out->w[i] = (uint32_t)t;
		carry = t >> 32;
	}
	out->n = n;
	if (carry)
		out->w[out->n++] = (uint32_t)carry;
}

/* a -= b, a >= b */
static void big_sub(Big *a, const Big *b)
{
	int64_t borrow = 0;
	for (int i = 0; i < a->n; i++) {
		int64_t t = (int64_t)a->w[i] - (i < b->n ? b->w[i] : 0) - borrow;
		borrow = t < 0;
		a->w[i] = (uint32_t)(t + (borrow << 32));
	}
	while (a->n && !a->w[a->n - 1])
		a->n--;
}

/* Compares a + b with c. */
static int big_cmp_sum(const Big *a, const Big *b, const Big *c)
{
	Big t;
	big_add(&t, a, b);
	return big_cmp(&t, c);
}

/* Shortest round-trip digits of v > 0 (finite): writes the digits (no NUL) and returns their
 * count; *decpt receives the decimal exponent (v = 0.d1d2... * 10^decpt). */
static int shortest_digits(double v, char *digits, int *decpt)
{
	uint64_t bits;
	memcpy(&bits, &v, sizeof bits);
	int be = (int)(bits >> 52 & 0x7ff);
	uint64_t mant = bits & ((1ull << 52) - 1), f;
	int e;
	if (be == 0) {
		f = mant;
		e = -1074;
	} else {
		f = mant | 1ull << 52;
		e = be - 1075;
	}
	bool even = (f & 1) == 0;
	bool closer = mant == 0 && be > 1; /* the lower neighbour is closer (power of two) */
	Big r, s, mp, mm;
	big_set(&r, f);
	if (e >= 0) {
		big_shl(&r, e + (closer ? 2 : 1));
		big_set(&s, closer ? 4 : 2);
		big_set(&mp, 1);
		big_shl(&mp, e + (closer ? 1 : 0));
		big_set(&mm, 1);
		big_shl(&mm, e);
	} else {
		big_shl(&r, closer ? 2 : 1);
		big_set(&s, 1);
		big_shl(&s, (closer ? 2 : 1) - e);
		big_set(&mp, closer ? 2 : 1);
		big_set(&mm, 1);
	}
	/* k = ceil(log10(v)) estimate from the binary exponent: floor(t * log10(2)) is
	 * (t * 78913) >> 18 for |t| < 1650; it is never too high, the loop below fixes it up. */
	int bitlen = 64 - __builtin_clzll(f);
	int t = e + bitlen - 1;
	int k = t == 0 ? 0 : (int)(((int64_t)t * 78913) >> 18) + 1;
	if (k >= 0) {
		big_mul_pow10(&s, k);
	} else {
		big_mul_pow10(&r, -k);
		big_mul_pow10(&mp, -k);
		big_mul_pow10(&mm, -k);
	}
	for (;;) {
		int c = big_cmp_sum(&r, &mp, &s);
		if (even ? c < 0 : c <= 0)
			break;
		big_mul_small(&s, 10);
		k++;
	}
	*decpt = k;
	int n = 0;
	for (;;) {
		big_mul_small(&r, 10);
		big_mul_small(&mp, 10);
		big_mul_small(&mm, 10);
		int d = 0;
		while (big_cmp(&r, &s) >= 0) {
			big_sub(&r, &s);
			d++;
		}
		int c1 = big_cmp(&r, &mm), c2 = big_cmp_sum(&r, &mp, &s);
		bool tc1 = even ? c1 <= 0 : c1 < 0, tc2 = even ? c2 >= 0 : c2 > 0;
		if (!tc1 && !tc2) {
			digits[n++] = (char)('0' + d);
			continue;
		}
		if (tc1 && tc2) {
			Big r2 = r;
			big_shl(&r2, 1);
			int c = big_cmp(&r2, &s);
			if (c > 0 || (c == 0 && (d & 1)))
				d++;
		} else if (tc2) {
			d++;
		}
		digits[n++] = (char)('0' + d);
		return n;
	}
}

char *py_repr_float(double v)
{
	if (v != v)
		return xstrdup("nan");
	uint64_t bits;
	memcpy(&bits, &v, sizeof bits);
	bool neg = bits >> 63;
	Buf b = {0};
	if (neg)
		buf_addc(&b, '-');
	if ((bits & ~(1ull << 63)) == 0x7ffull << 52) {
		buf_adds(&b, "inf");
		return (char *)buf_str(&b);
	}
	if (!(bits << 1)) {
		buf_adds(&b, "0.0");
		return (char *)buf_str(&b);
	}
	char d[32];
	int decpt, n = shortest_digits(neg ? -v : v, d, &decpt);
	if (decpt <= -4 || decpt > 16) {
		buf_addc(&b, d[0]);
		if (n > 1) {
			buf_addc(&b, '.');
			buf_add(&b, d + 1, (size_t)n - 1);
		}
		int x = decpt - 1;
		buf_adds(&b, x < 0 ? "e-" : "e+");
		if (x < 0)
			x = -x;
		buf_fmt(&b, "%02d", x);
	} else if (decpt <= 0) {
		buf_adds(&b, "0.");
		for (int i = 0; i < -decpt; i++)
			buf_addc(&b, '0');
		buf_add(&b, d, (size_t)n);
	} else if (decpt >= n) {
		buf_add(&b, d, (size_t)n);
		for (int i = n; i < decpt; i++)
			buf_addc(&b, '0');
		buf_adds(&b, ".0");
	} else {
		buf_add(&b, d, (size_t)decpt);
		buf_addc(&b, '.');
		buf_add(&b, d + decpt, (size_t)(n - decpt));
	}
	return (char *)buf_str(&b);
}
