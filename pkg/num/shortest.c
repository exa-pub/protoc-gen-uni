/* Shortest round-trip digits: a port of the multiprecision path of Go's strconv (go1.26.5
 * src/internal/strconv/decimal.go and ftoa.go: decimal, roundShortest), which yields the same
 * digits as Ryū and as the shortest algorithms of the other runtimes.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "num.h"

#include <string.h>

/* ---- decimal.go ---- */

typedef struct {
	uint8_t d[800]; /* digits, big-endian representation */
	int nd;         /* number of digits used */
	int dp;         /* decimal point */
	bool trunc;     /* discarded nonzero digits beyond d[:nd] */
} decimal;

static void trim(decimal *a)
{
	while (a->nd > 0 && a->d[a->nd - 1] == '0')
		a->nd--;
	if (a->nd == 0)
		a->dp = 0;
}

static void assign(decimal *a, uint64_t v)
{
	uint8_t buf[24];
	int n = 0;
	while (v > 0) {
		uint64_t v1 = v / 10;
		v -= 10 * v1;
		buf[n++] = (uint8_t)(v + '0');
		v = v1;
	}
	a->nd = 0;
	for (n--; n >= 0; n--)
		a->d[a->nd++] = buf[n];
	a->dp = a->nd;
	trim(a);
}

/* uintSize - 4 for a 64-bit uint. */
#define MAX_SHIFT 60

static void right_shift(decimal *a, unsigned k)
{
	int r = 0, w = 0;
	uint64_t n = 0;
	for (; n >> k == 0; r++) {
		if (r >= a->nd) {
			if (n == 0) {
				a->nd = 0;
				return;
			}
			while (n >> k == 0) {
				n = n * 10;
				r++;
			}
			break;
		}
		uint64_t c = a->d[r];
		n = n * 10 + c - '0';
	}
	a->dp -= r - 1;
	uint64_t mask = ((uint64_t)1 << k) - 1;
	for (; r < a->nd; r++) {
		uint64_t c = a->d[r];
		uint64_t dig = n >> k;
		n &= mask;
		a->d[w++] = (uint8_t)(dig + '0');
		n = n * 10 + c - '0';
	}
	while (n > 0) {
		uint64_t dig = n >> k;
		n &= mask;
		if (w < (int)sizeof a->d)
			a->d[w++] = (uint8_t)(dig + '0');
		else if (dig > 0)
			a->trunc = true;
		n = n * 10;
	}
	a->nd = w;
	trim(a);
}

/* Number of new digits and the 5^k cutoff of a left shift by k (decimal.go leftcheats). */
static const struct {
	int delta;
	const char *cutoff;
} leftcheats[] = {
	{0, ""},
	{1, "5"},
	{1, "25"},
	{1, "125"},
	{2, "625"},
	{2, "3125"},
	{2, "15625"},
	{3, "78125"},
	{3, "390625"},
	{3, "1953125"},
	{4, "9765625"},
	{4, "48828125"},
	{4, "244140625"},
	{4, "1220703125"},
	{5, "6103515625"},
	{5, "30517578125"},
	{5, "152587890625"},
	{6, "762939453125"},
	{6, "3814697265625"},
	{6, "19073486328125"},
	{7, "95367431640625"},
	{7, "476837158203125"},
	{7, "2384185791015625"},
	{7, "11920928955078125"},
	{8, "59604644775390625"},
	{8, "298023223876953125"},
	{8, "1490116119384765625"},
	{9, "7450580596923828125"},
	{9, "37252902984619140625"},
	{9, "186264514923095703125"},
	{10, "931322574615478515625"},
	{10, "4656612873077392578125"},
	{10, "23283064365386962890625"},
	{10, "116415321826934814453125"},
	{11, "582076609134674072265625"},
	{11, "2910383045673370361328125"},
	{11, "14551915228366851806640625"},
	{12, "72759576141834259033203125"},
	{12, "363797880709171295166015625"},
	{12, "1818989403545856475830078125"},
	{13, "9094947017729282379150390625"},
	{13, "45474735088646411895751953125"},
	{13, "227373675443232059478759765625"},
	{13, "1136868377216160297393798828125"},
	{14, "5684341886080801486968994140625"},
	{14, "28421709430404007434844970703125"},
	{14, "142108547152020037174224853515625"},
	{15, "710542735760100185871124267578125"},
	{15, "3552713678800500929355621337890625"},
	{15, "17763568394002504646778106689453125"},
	{16, "88817841970012523233890533447265625"},
	{16, "444089209850062616169452667236328125"},
	{16, "2220446049250313080847263336181640625"},
	{16, "11102230246251565404236316680908203125"},
	{17, "55511151231257827021181583404541015625"},
	{17, "277555756156289135105907917022705078125"},
	{17, "1387778780781445675529539585113525390625"},
	{18, "6938893903907228377647697925567626953125"},
	{18, "34694469519536141888238489627838134765625"},
	{18, "173472347597680709441192448139190673828125"},
	{19, "867361737988403547205962240695953369140625"},
};

static bool prefix_is_less_than(const uint8_t *b, int n, const char *s)
{
	for (int i = 0; s[i]; i++) {
		if (i >= n)
			return true;
		if (b[i] != (uint8_t)s[i])
			return b[i] < (uint8_t)s[i];
	}
	return false;
}

static void left_shift(decimal *a, unsigned k)
{
	int delta = leftcheats[k].delta;
	if (prefix_is_less_than(a->d, a->nd, leftcheats[k].cutoff))
		delta--;
	int r = a->nd, w = a->nd + delta;
	uint64_t n = 0;
	for (r--; r >= 0; r--) {
		n += ((uint64_t)a->d[r] - '0') << k;
		uint64_t quo = n / 10, rem = n - 10 * quo;
		w--;
		if (w < (int)sizeof a->d)
			a->d[w] = (uint8_t)(rem + '0');
		else if (rem != 0)
			a->trunc = true;
		n = quo;
	}
	while (n > 0) {
		uint64_t quo = n / 10, rem = n - 10 * quo;
		w--;
		if (w < (int)sizeof a->d)
			a->d[w] = (uint8_t)(rem + '0');
		else if (rem != 0)
			a->trunc = true;
		n = quo;
	}
	a->nd += delta;
	if (a->nd >= (int)sizeof a->d)
		a->nd = (int)sizeof a->d;
	a->dp += delta;
	trim(a);
}

static void shift(decimal *a, int k)
{
	if (a->nd == 0) {
	} else if (k > 0) {
		while (k > MAX_SHIFT) {
			left_shift(a, MAX_SHIFT);
			k -= MAX_SHIFT;
		}
		left_shift(a, (unsigned)k);
	} else if (k < 0) {
		while (k < -MAX_SHIFT) {
			right_shift(a, MAX_SHIFT);
			k += MAX_SHIFT;
		}
		right_shift(a, (unsigned)-k);
	}
}

static bool should_round_up(const decimal *a, int nd)
{
	if (nd < 0 || nd >= a->nd)
		return false;
	if (a->d[nd] == '5' && nd + 1 == a->nd) { /* exactly halfway - round to even */
		if (a->trunc)
			return true;
		return nd > 0 && (a->d[nd - 1] - '0') % 2 != 0;
	}
	return a->d[nd] >= '5';
}

static void round_down(decimal *a, int nd)
{
	if (nd < 0 || nd >= a->nd)
		return;
	a->nd = nd;
	trim(a);
}

static void round_up(decimal *a, int nd)
{
	if (nd < 0 || nd >= a->nd)
		return;
	for (int i = nd - 1; i >= 0; i--) {
		if (a->d[i] < '9') {
			a->d[i]++;
			a->nd = i + 1;
			return;
		}
	}
	a->d[0] = '1';
	a->nd = 1;
	a->dp++;
}

static void round_nearest(decimal *a, int nd)
{
	if (nd < 0 || nd >= a->nd)
		return;
	if (should_round_up(a, nd))
		round_up(a, nd);
	else
		round_down(a, nd);
}

/* ---- ftoa.go ---- */

typedef struct {
	unsigned mantbits, expbits;
	int bias;
} float_info;

static const float_info float32info = {23, 8, -127};
static const float_info float64info = {52, 11, -1023};

/* The rounding interval of mant x 2^(exp-mantbits): its bounds as exact decimals (halfway to the
 * neighbours) and whether they round to the value (even mantissa). From roundShortest. */
static void bounds(uint64_t mant, int exp, const float_info *flt, decimal *upper, decimal *lower, bool *inclusive)
{
	int minexp = flt->bias + 1;
	memset(upper, 0, sizeof *upper);
	memset(lower, 0, sizeof *lower);
	assign(upper, mant * 2 + 1);
	shift(upper, exp - (int)flt->mantbits - 1);
	uint64_t mantlo;
	int explo;
	if (mant > (uint64_t)1 << flt->mantbits || exp == minexp) {
		mantlo = mant - 1;
		explo = exp;
	} else {
		mantlo = mant * 2 - 1;
		explo = exp - 1;
	}
	assign(lower, mantlo * 2 + 1);
	shift(lower, explo - (int)flt->mantbits - 1);
	*inclusive = mant % 2 == 0;
}

static void round_shortest(decimal *d, uint64_t mant, int exp, const float_info *flt)
{
	if (mant == 0) {
		d->nd = 0;
		return;
	}
	int minexp = flt->bias + 1;
	if (exp > minexp && 332 * (d->dp - d->nd) >= 100 * (exp - (int)flt->mantbits))
		return; /* already shortest */
	decimal upper, lower;
	bool inclusive;
	bounds(mant, exp, flt, &upper, &lower, &inclusive);
	int upperdelta = 0;
	for (int ui = 0;; ui++) {
		int mi = ui - upper.dp + d->dp;
		if (mi >= d->nd)
			break;
		int li = ui - upper.dp + lower.dp;
		uint8_t l = '0';
		if (li >= 0 && li < lower.nd)
			l = lower.d[li];
		uint8_t m = '0';
		if (mi >= 0)
			m = d->d[mi];
		uint8_t u = '0';
		if (ui < upper.nd)
			u = upper.d[ui];
		bool okdown = l != m || (inclusive && li + 1 == lower.nd);
		if (upperdelta == 0 && m + 1 < u)
			upperdelta = 2;
		else if (upperdelta == 0 && m != u)
			upperdelta = 1;
		else if (upperdelta == 1 && (m != '9' || u != '0'))
			upperdelta = 2;
		bool okup = upperdelta > 0 && (inclusive || upperdelta > 1 || ui + 1 < upper.nd);
		if (okdown && okup) {
			round_nearest(d, mi + 1);
			return;
		} else if (okdown) {
			round_down(d, mi + 1);
			return;
		} else if (okup) {
			round_up(d, mi + 1);
			return;
		}
	}
}

/* The exact decimal value of |v| and its binary mantissa and exponent (genericFtoa). */
static void exact(double v, int bits, decimal *d, uint64_t *mant_out, int *exp_out, const float_info **flt_out)
{
	const float_info *flt;
	uint64_t raw;
	if (bits == 32) {
		float f = (float)v;
		uint32_t b;
		memcpy(&b, &f, sizeof b);
		raw = b;
		flt = &float32info;
	} else {
		memcpy(&raw, &v, sizeof raw);
		flt = &float64info;
	}
	int exp = (int)(raw >> flt->mantbits) & ((1 << flt->expbits) - 1);
	uint64_t mant = raw & (((uint64_t)1 << flt->mantbits) - 1);
	if (exp == 0)
		exp++; /* denormalized */
	else
		mant |= (uint64_t)1 << flt->mantbits;
	exp += flt->bias;
	memset(d, 0, sizeof *d);
	assign(d, mant);
	shift(d, exp - (int)flt->mantbits);
	*mant_out = mant;
	*exp_out = exp;
	*flt_out = flt;
}

static void to_out(const decimal *d, NumDecimal *out)
{
	memset(out, 0, sizeof *out);
	int nd = d->nd < (int)sizeof out->d ? d->nd : (int)sizeof out->d - 1;
	memcpy(out->d, d->d, (size_t)nd);
	out->nd = nd;
	out->dp = nd ? d->dp : 0;
}

void num_shortest(double v, int bits, NumDecimal *out)
{
	decimal d;
	uint64_t mant;
	int exp;
	const float_info *flt;
	exact(v, bits, &d, &mant, &exp, &flt);
	round_shortest(&d, mant, exp, flt);
	to_out(&d, out);
}

/* Compares two trimmed decimals (both nonzero). */
static int cmp_decimal(const decimal *a, const decimal *b)
{
	if (a->dp != b->dp)
		return a->dp < b->dp ? -1 : 1;
	for (int i = 0; i < a->nd || i < b->nd; i++) {
		uint8_t x = i < a->nd ? a->d[i] : '0', y = i < b->nd ? b->d[i] : '0';
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

void num_shortest_java(double v, int bits, NumDecimal *out)
{
	decimal d;
	uint64_t mant;
	int exp;
	const float_info *flt;
	exact(v, bits, &d, &mant, &exp, &flt);
	if (mant == 0) {
		to_out(&d, out);
		return;
	}
	decimal two = d; /* the exact value rounded to two digits */
	round_nearest(&two, 2);
	round_shortest(&d, mant, exp, flt);
	if (d.nd == 1 && two.nd == 2) {
		/* JDK: with a shortest length of 1, the closest decimal of length 1 or 2 that rounds to v */
		decimal upper, lower;
		bool inclusive;
		bounds(mant, exp, flt, &upper, &lower, &inclusive);
		int cl = cmp_decimal(&two, &lower), cu = cmp_decimal(&two, &upper);
		if ((cl > 0 || (inclusive && cl == 0)) && (cu < 0 || (inclusive && cu == 0)))
			d = two;
	}
	to_out(&d, out);
}
