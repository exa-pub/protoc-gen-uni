/* Package strconv, ported from go1.26.5 src/internal/strconv/decimal.go and ftoa.go: FormatFloat
 * with the 'e', 'f' and 'g' formats through the multiprecision path (bigFtoa; Go's Ryū fast
 * path produces the same digits). Copyright 2009 The Go Authors. All rights reserved.
 * BSD-3-Clause (see LICENSE in Go). */

/* ---- decimal.go ---- */

typedef struct {
	uint8_t d[800]; /* digits, big-endian representation */
	int nd;         /* number of digits used */
	int dp;         /* decimal point */
	bool trunc;     /* discarded nonzero digits beyond d[:nd] */
} strconv_decimal;

static void strconv_trim(strconv_decimal *a)
{
	while (a->nd > 0 && a->d[a->nd - 1] == '0')
		a->nd--;
	if (a->nd == 0)
		a->dp = 0;
}

static void strconv_decimal_Assign(strconv_decimal *a, uint64_t v)
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
	strconv_trim(a);
}

/* uintSize - 4 for a 64-bit uint: the output must not depend on the platform. */
#define strconv_maxShift 60

static void strconv_rightShift(strconv_decimal *a, unsigned k)
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
	strconv_trim(a);
}

static const struct {
	int delta;
	const char *cutoff;
} strconv_leftcheats[] = {
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

static bool strconv_prefixIsLessThan(const uint8_t *b, int n, const char *s)
{
	for (int i = 0; s[i]; i++) {
		if (i >= n)
			return true;
		if (b[i] != (uint8_t)s[i])
			return b[i] < (uint8_t)s[i];
	}
	return false;
}

static void strconv_leftShift(strconv_decimal *a, unsigned k)
{
	int delta = strconv_leftcheats[k].delta;
	if (strconv_prefixIsLessThan(a->d, a->nd, strconv_leftcheats[k].cutoff))
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
	strconv_trim(a);
}

static void strconv_decimal_Shift(strconv_decimal *a, int k)
{
	if (a->nd == 0) {
	} else if (k > 0) {
		while (k > strconv_maxShift) {
			strconv_leftShift(a, strconv_maxShift);
			k -= strconv_maxShift;
		}
		strconv_leftShift(a, (unsigned)k);
	} else if (k < 0) {
		while (k < -strconv_maxShift) {
			strconv_rightShift(a, strconv_maxShift);
			k += strconv_maxShift;
		}
		strconv_rightShift(a, (unsigned)-k);
	}
}

static bool strconv_shouldRoundUp(const strconv_decimal *a, int nd)
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

static void strconv_decimal_RoundDown(strconv_decimal *a, int nd)
{
	if (nd < 0 || nd >= a->nd)
		return;
	a->nd = nd;
	strconv_trim(a);
}

static void strconv_decimal_RoundUp(strconv_decimal *a, int nd)
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

static void strconv_decimal_Round(strconv_decimal *a, int nd)
{
	if (nd < 0 || nd >= a->nd)
		return;
	if (strconv_shouldRoundUp(a, nd))
		strconv_decimal_RoundUp(a, nd);
	else
		strconv_decimal_RoundDown(a, nd);
}

/* ---- ftoa.go ---- */

typedef struct {
	unsigned mantbits, expbits;
	int bias;
} strconv_floatInfo;

static const strconv_floatInfo strconv_float32info = {23, 8, -127};
static const strconv_floatInfo strconv_float64info = {52, 11, -1023};

typedef struct {
	const uint8_t *d;
	int nd, dp;
} strconv_decimalSlice;

static void strconv_roundShortest(strconv_decimal *d, uint64_t mant, int exp, const strconv_floatInfo *flt)
{
	if (mant == 0) {
		d->nd = 0;
		return;
	}
	int minexp = flt->bias + 1;
	if (exp > minexp && 332 * (d->dp - d->nd) >= 100 * (exp - (int)flt->mantbits))
		return; /* already shortest */
	static strconv_decimal upper, lower;
	memset(&upper, 0, sizeof upper);
	memset(&lower, 0, sizeof lower);
	strconv_decimal_Assign(&upper, mant * 2 + 1);
	strconv_decimal_Shift(&upper, exp - (int)flt->mantbits - 1);
	uint64_t mantlo;
	int explo;
	if (mant > (uint64_t)1 << flt->mantbits || exp == minexp) {
		mantlo = mant - 1;
		explo = exp;
	} else {
		mantlo = mant * 2 - 1;
		explo = exp - 1;
	}
	strconv_decimal_Assign(&lower, mantlo * 2 + 1);
	strconv_decimal_Shift(&lower, explo - (int)flt->mantbits - 1);
	bool inclusive = mant % 2 == 0;
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
			strconv_decimal_Round(d, mi + 1);
			return;
		} else if (okdown) {
			strconv_decimal_RoundDown(d, mi + 1);
			return;
		} else if (okup) {
			strconv_decimal_RoundUp(d, mi + 1);
			return;
		}
	}
}

static void strconv_fmtE(Buf *dst, bool neg, strconv_decimalSlice d, int prec, char fmt)
{
	if (neg)
		buf_addc(dst, '-');
	buf_addc(dst, d.nd != 0 ? (char)d.d[0] : '0');
	if (prec > 0) {
		buf_addc(dst, '.');
		int i = 1;
		int m = d.nd < prec + 1 ? d.nd : prec + 1;
		if (i < m) {
			buf_add(dst, d.d + i, (size_t)(m - i));
			i = m;
		}
		for (; i <= prec; i++)
			buf_addc(dst, '0');
	}
	buf_addc(dst, fmt);
	int exp = d.dp - 1;
	if (d.nd == 0)
		exp = 0;
	char ch = '+';
	if (exp < 0) {
		ch = '-';
		exp = -exp;
	}
	buf_addc(dst, ch);
	if (exp < 10) {
		buf_addc(dst, '0');
		buf_addc(dst, (char)(exp + '0'));
	} else if (exp < 100) {
		buf_addc(dst, (char)(exp / 10 + '0'));
		buf_addc(dst, (char)(exp % 10 + '0'));
	} else {
		buf_addc(dst, (char)(exp / 100 + '0'));
		buf_addc(dst, (char)(exp / 10 % 10 + '0'));
		buf_addc(dst, (char)(exp % 10 + '0'));
	}
}

static void strconv_fmtF(Buf *dst, bool neg, strconv_decimalSlice d, int prec)
{
	if (neg)
		buf_addc(dst, '-');
	if (d.dp > 0) {
		int m = d.nd < d.dp ? d.nd : d.dp;
		buf_add(dst, d.d, (size_t)m);
		for (; m < d.dp; m++)
			buf_addc(dst, '0');
	} else {
		buf_addc(dst, '0');
	}
	if (prec > 0) {
		buf_addc(dst, '.');
		for (int i = 0; i < prec; i++) {
			char ch = '0';
			int j = d.dp + i;
			if (0 <= j && j < d.nd)
				ch = (char)d.d[j];
			buf_addc(dst, ch);
		}
	}
}

static void strconv_formatDigits(Buf *dst, bool shortest, bool neg, strconv_decimalSlice digs, int prec, char fmt)
{
	switch (fmt) {
	case 'e':
	case 'E':
		strconv_fmtE(dst, neg, digs, prec, fmt);
		return;
	case 'f':
		strconv_fmtF(dst, neg, digs, prec);
		return;
	case 'g':
	case 'G': {
		int eprec = prec;
		if (eprec > digs.nd && digs.nd >= digs.dp)
			eprec = digs.nd;
		if (shortest)
			eprec = 6;
		int exp = digs.dp - 1;
		if (exp < -4 || exp >= eprec) {
			if (prec > digs.nd)
				prec = digs.nd;
			strconv_fmtE(dst, neg, digs, prec - 1, (char)(fmt + 'e' - 'g'));
			return;
		}
		if (prec > digs.dp)
			prec = digs.nd;
		strconv_fmtF(dst, neg, digs, prec - digs.dp > 0 ? prec - digs.dp : 0);
		return;
	}
	}
	buf_addc(dst, '%');
	buf_addc(dst, fmt);
}

static void strconv_bigFtoa(Buf *dst, int prec, char fmt, bool neg, uint64_t mant, int exp, const strconv_floatInfo *flt)
{
	static strconv_decimal d;
	memset(&d, 0, sizeof d);
	strconv_decimal_Assign(&d, mant);
	strconv_decimal_Shift(&d, exp - (int)flt->mantbits);
	strconv_decimalSlice digs;
	bool shortest = prec < 0;
	if (shortest) {
		strconv_roundShortest(&d, mant, exp, flt);
		digs = (strconv_decimalSlice){d.d, d.nd, d.dp};
		switch (fmt) {
		case 'e': case 'E': prec = digs.nd - 1; break;
		case 'f': prec = digs.nd - digs.dp > 0 ? digs.nd - digs.dp : 0; break;
		case 'g': case 'G': prec = digs.nd; break;
		}
	} else {
		switch (fmt) {
		case 'e': case 'E': strconv_decimal_Round(&d, prec + 1); break;
		case 'f': strconv_decimal_Round(&d, d.dp + prec); break;
		case 'g': case 'G':
			if (prec == 0)
				prec = 1;
			strconv_decimal_Round(&d, prec);
			break;
		}
		digs = (strconv_decimalSlice){d.d, d.nd, d.dp};
	}
	strconv_formatDigits(dst, shortest, neg, digs, prec, fmt);
}

/* FormatFloat for fmt 'e', 'E', 'f', 'g', 'G'. */
GO_HELPER void strconv_AppendFloat(Buf *dst, double val, char fmt, int prec, int bitSize)
{
	uint64_t bits;
	const strconv_floatInfo *flt;
	if (bitSize == 32) {
		float f = (float)val;
		uint32_t b32;
		memcpy(&b32, &f, sizeof b32);
		bits = b32;
		flt = &strconv_float32info;
	} else {
		memcpy(&bits, &val, sizeof bits);
		flt = &strconv_float64info;
	}
	bool neg = bits >> (flt->expbits + flt->mantbits) != 0;
	int exp = (int)(bits >> flt->mantbits) & ((1 << flt->expbits) - 1);
	uint64_t mant = bits & (((uint64_t)1 << flt->mantbits) - 1);
	if (exp == (1 << flt->expbits) - 1) {
		buf_adds(dst, mant != 0 ? "NaN" : neg ? "-Inf" : "+Inf");
		return;
	}
	if (exp == 0)
		exp++; /* denormalized */
	else
		mant |= (uint64_t)1 << flt->mantbits;
	exp += flt->bias;
	strconv_bigFtoa(dst, prec, fmt, neg, mant, exp, flt);
}
