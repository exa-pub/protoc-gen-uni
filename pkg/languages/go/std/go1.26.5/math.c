/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * math.Exp and math.Log of go1.26.5 on amd64, which go/printer's exprList alignment heuristic
 * used before go1.27 (go1.27 uses log2ish/exp2ish of go/printer/math.go, which give the same
 * results on every architecture). Ported from the amd64 assembly Go uses (src/math/exp_amd64.s
 * with the AVX/FMA path, src/math/log_amd64.s), so that the alignment decisions match Go on
 * amd64 bit for bit (assumption: the buf.build remote plugin runs on amd64; arm64 Go could
 * differ in rare alignment decisions). */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- math (Go: package math on amd64) ---- */

/* Every floating-point operation below must be rounded separately (no implicit FMA contraction;
 * GCC does not contract in ISO C mode, clang does within expressions unless told not to). */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#endif

static uint64_t printer_float64bits(double f)
{
	uint64_t u;
	memcpy(&u, &f, sizeof u);
	return u;
}

static double printer_float64frombits(uint64_t u)
{
	double f;
	memcpy(&f, &u, sizeof f);
	return f;
}

typedef struct {
	uint64_t hi, lo;
} printer_u128;

static printer_u128 printer_mul64(uint64_t a, uint64_t b)
{
	uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
	uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
	uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
	return (printer_u128){p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (mid << 32) | (p00 & 0xffffffffu)};
}

static printer_u128 printer_shl128(printer_u128 x, int s)
{
	if (s == 0)
		return x;
	if (s >= 64)
		return (printer_u128){x.lo << (s - 64), 0};
	return (printer_u128){(x.hi << s) | (x.lo >> (64 - s)), x.lo << s};
}

/* x >> s; *sticky |= any bit shifted out */
static printer_u128 printer_shr128(printer_u128 x, int s, bool *sticky)
{
	if (s == 0)
		return x;
	if (s >= 128) {
		*sticky |= (x.hi | x.lo) != 0;
		return (printer_u128){0, 0};
	}
	if (s >= 64) {
		*sticky |= x.lo != 0 || (s > 64 && (x.hi << (128 - s)) != 0);
		return (printer_u128){0, x.hi >> (s - 64)};
	}
	*sticky |= (x.lo << (64 - s)) != 0;
	return (printer_u128){x.hi >> s, (x.lo >> s) | (x.hi << (64 - s))};
}

static int printer_msb128(printer_u128 x)
{
	int n = 127;
	uint64_t w = x.hi;
	if (w == 0) {
		w = x.lo;
		n = 63;
	}
	while (!(w >> 63)) {
		w <<= 1;
		n--;
	}
	return n;
}

/* decompose finite nonzero f into m * 2^e */
static void printer_decompose(double f, uint64_t *m, int *e, bool *neg)
{
	uint64_t b = printer_float64bits(f);
	*neg = b >> 63;
	int be = (int)(b >> 52) & 0x7ff;
	uint64_t frac = b & ((UINT64_C(1) << 52) - 1);
	if (be == 0) {
		*m = frac;
		*e = -1074;
	} else {
		*m = frac | (UINT64_C(1) << 52);
		*e = be - 1075;
	}
}

/* printer_fma returns x*y + z, computed with only one rounding (the FMA instructions of Go's
 * exp_amd64.s). Exact for finite operands and normal results; other cases (never reached by
 * the printer: all values are moderate) fall back to x*y + z. */
static double printer_fma(double x, double y, double z)
{
	if (x == 0 || y == 0 || z == 0 || x - x != 0 || y - y != 0 || z - z != 0)
		return x * y + z;
	uint64_t mx, my, mz;
	int ex, ey, ez;
	bool nx, ny, nz;
	printer_decompose(x, &mx, &ex, &nx);
	printer_decompose(y, &my, &ey, &ny);
	printer_decompose(z, &mz, &ez, &nz);
	/* A = x*y, B = z as 128-bit mantissas with the most significant bit at bit 125 */
	printer_u128 ma = printer_mul64(mx, my), mb = {0, mz};
	int sa = 125 - printer_msb128(ma), sb = 125 - printer_msb128(mb);
	ma = printer_shl128(ma, sa);
	mb = printer_shl128(mb, sb);
	int ea = ex + ey - sa, eb = ez - sb;
	bool na = nx != ny, nb = nz;
	if (eb > ea || (eb == ea && (mb.hi > ma.hi || (mb.hi == ma.hi && mb.lo > ma.lo)))) {
		printer_u128 tm = ma;
		ma = mb;
		mb = tm;
		int te = ea;
		ea = eb;
		eb = te;
		bool tn = na;
		na = nb;
		nb = tn;
	}
	/* |A| >= |B|: align B */
	bool sticky = false;
	mb = printer_shr128(mb, ea - eb, &sticky);
	printer_u128 r;
	if (na == nb) {
		r.lo = ma.lo + mb.lo;
		r.hi = ma.hi + mb.hi + (r.lo < ma.lo);
	} else {
		r.lo = ma.lo - mb.lo;
		r.hi = ma.hi - mb.hi - (ma.lo < mb.lo);
		if (sticky) { /* the exact B is a bit larger than the truncated one */
			r.hi -= r.lo == 0;
			r.lo--;
		}
	}
	if (sticky)
		r.lo |= 1;
	if (r.hi == 0 && r.lo == 0)
		return 0;
	/* round to 53 bits, to nearest even */
	int k = printer_msb128(r);
	int e = ea;
	uint64_t mant;
	if (k > 52) {
		int shift = k - 52;
		bool below = false;
		printer_u128 q = printer_shr128(r, shift - 1, &below);
		bool round = q.lo & 1, roundBit = false;
		q = printer_shr128(q, 1, &roundBit);
		mant = q.lo;
		if (round && (below || (mant & 1)))
			mant++;
		if (mant == UINT64_C(1) << 53) {
			mant >>= 1;
			shift++;
		}
		e += shift;
	} else {
		mant = r.lo << (52 - k);
		e -= 52 - k;
	}
	int biased = e + 1075;
	if (biased < 1 || biased > 2046)
		return x * y + z;
	return printer_float64frombits((uint64_t)na << 63 | (uint64_t)biased << 52 | (mant & ((UINT64_C(1) << 52) - 1)));
}

/* CVTSD2SL: convert to int32 rounding to nearest even; out of range or NaN: 0x80000000 */
static int32_t printer_cvtsd2sl(double v)
{
	if (!(v > -2147483648.5 && v < 2147483647.5))
		return INT32_MIN;
	int64_t t = (int64_t)v; /* truncates */
	double frac = v - (double)t;
	if (frac > 0.5 || (frac == 0.5 && (t & 1)))
		t++;
	else if (frac < -0.5 || (frac == -0.5 && (t & 1)))
		t--;
	if (t > INT32_MAX || t < INT32_MIN)
		return INT32_MIN;
	return (int32_t)t;
}

/* Go: useFMA = cpu.X86.HasAVX && cpu.X86.HasFMA (true on all current amd64 servers). */
static const bool printer_useFMA = true;

/* math.Exp: port of archExp, src/math/exp_amd64.s (Naoki Shibata's method). */
static double printer_exp(double x)
{
	const double LN2U = 0.69314718055966295651160180568695068359375; /* upper half LN2 */
	const double LN2L = 0.28235290563031577122588448175013436025525412068e-12; /* lower half LN2 */
	const double LOG2E = 1.4426950408889634073599246810018920; /* 1/LN2 */
	const double Overflow = 7.09782712893384e+02;
	const uint64_t PosInf = UINT64_C(0x7FF0000000000000), NegInf = UINT64_C(0xFFF0000000000000);
	static const double exprodata[] = {
		0.5,
		1.0,
		2.0,
		1.6666666666666666667e-1,
		4.1666666666666666667e-2,
		8.3333333333333333333e-3,
		1.3888888888888888889e-3,
		1.9841269841269841270e-4,
		2.4801587301587301587e-5,
	};
	uint64_t bx = printer_float64bits(x);
	/* test bits for not-finite */
	if ((int64_t)PosInf <= (int64_t)(bx & ~(UINT64_C(1) << 63))) {
		if (bx == NegInf)
			return 0; /* -Inf, return 0 */
		return x; /* NaN or +Inf, return x */
	}
	/* check if argument will overflow */
	if (x > Overflow)
		return printer_float64frombits(PosInf);
	double x1 = LOG2E * x;
	int32_t k = printer_cvtsd2sl(x1); /* exponent */
	x1 = (double)k;
	double x0 = x;
	if (printer_useFMA) {
		x0 = printer_fma(-x1, LN2U, x0);
		x0 = printer_fma(-x1, LN2L, x0);
		/* reduce argument */
		x0 *= 0.0625;
		/* Taylor series evaluation */
		x1 = exprodata[8];
		x1 = printer_fma(x1, x0, exprodata[7]);
		x1 = printer_fma(x1, x0, exprodata[6]);
		x1 = printer_fma(x1, x0, exprodata[5]);
		x1 = printer_fma(x1, x0, exprodata[4]);
		x1 = printer_fma(x1, x0, exprodata[3]);
		x1 = printer_fma(x1, x0, exprodata[0]);
		x1 = printer_fma(x1, x0, exprodata[1]);
		x0 *= x1;
		x1 = x0 + exprodata[2];
		x0 *= x1;
		x1 = x0 + exprodata[2];
		x0 *= x1;
		x1 = x0 + exprodata[2];
		x0 *= x1;
		x1 = x0 + exprodata[2];
		x0 = printer_fma(x1, x0, exprodata[1]);
	} else {
		x0 -= x1 * LN2U;
		x0 -= x1 * LN2L;
		/* reduce argument */
		x0 *= 0.0625;
		/* Taylor series evaluation */
		x1 = exprodata[8] * x0;
		x1 += exprodata[7];
		x1 *= x0;
		x1 += exprodata[6];
		x1 *= x0;
		x1 += exprodata[5];
		x1 *= x0;
		x1 += exprodata[4];
		x1 *= x0;
		x1 += exprodata[3];
		x1 *= x0;
		x1 += exprodata[0];
		x1 *= x0;
		x1 += exprodata[1];
		x0 *= x1;
		for (int i = 0; i < 4; i++) {
			x1 = exprodata[2] + x0;
			x0 *= x1;
		}
		x0 += exprodata[1];
	}
	/* return fr * 2**exponent */
	int64_t b = (int64_t)k + 0x3FF; /* add bias */
	if (b <= 0) {
		/* denormal */
		if (b < -52)
			return 0; /* underflow */
		b += 0x3FE; /* add bias - 1 */
		x0 *= printer_float64frombits((uint64_t)b << 52);
		b = 1;
	} else if (b >= 0x7FF) {
		return printer_float64frombits(PosInf); /* overflow */
	}
	return x0 * printer_float64frombits((uint64_t)b << 52);
}

/* math.Log: port of archLog, src/math/log_amd64.s (the algorithm of src/math/log.go). */
static double printer_log(double x)
{
	const double HSqrt2 = 7.07106781186547524401e-01; /* sqrt(2)/2 */
	const double Ln2Hi = 6.93147180369123816490e-01; /* 0x3fe62e42fee00000 */
	const double Ln2Lo = 1.90821492927058770002e-10; /* 0x3dea39ef35793c76 */
	const double L1 = 6.666666666666735130e-01; /* 0x3FE5555555555593 */
	const double L2 = 3.999999999940941908e-01; /* 0x3FD999999997FA04 */
	const double L3 = 2.857142874366239149e-01; /* 0x3FD2492494229359 */
	const double L4 = 2.222219843214978396e-01; /* 0x3FCC71C51D8E78AF */
	const double L5 = 1.818357216161805012e-01; /* 0x3FC7466496CB03DE */
	const double L6 = 1.531383769920937332e-01; /* 0x3FC39A09D078C69F */
	const double L7 = 1.479819860511658591e-01; /* 0x3FC2F112DF3E5244 */
	uint64_t bx = printer_float64bits(x);
	/* test bits for special cases */
	if ((bx & ~(UINT64_C(1) << 63)) == 0)
		return printer_float64frombits(UINT64_C(0xFFF0000000000000)); /* return -Inf */
	if ((int64_t)bx < 0)
		return printer_float64frombits(UINT64_C(0x7FF8000000000001)); /* return NaN */
	if ((int64_t)UINT64_C(0x7FF0000000000000) <= (int64_t)bx)
		return x; /* +Inf or NaN, return x */
	/* f1, ki := math.Frexp(x); k := float64(ki) */
	double f1 = printer_float64frombits((bx & UINT64_C(0x000FFFFFFFFFFFFF)) | UINT64_C(0x3FE0000000000000));
	double k = (double)((int32_t)((bx >> 52) & 0x7FF) - 0x3FE);
	/* if f1 < math.Sqrt2/2 { k -= 1; f1 *= 2 } (cmpnlt: !(HSqrt2 < f1)) */
	double x3 = !(HSqrt2 < f1) ? 1.0 : 0.0;
	k -= x3;
	x3 += 1.0;
	f1 *= x3;
	/* f := f1 - 1 */
	double f = f1 - 1.0;
	/* s := f / (2 + f) */
	double s = f / (2.0 + f);
	/* s2 := s * s; s4 := s2 * s2 */
	double s2 = s * s;
	double s4 = s2 * s2;
	/* t1 := s2 * (L1 + s4*(L3+s4*(L5+s4*L7))) */
	double x6 = L7 * s4;
	x6 += L5;
	x6 *= s4;
	x6 += L3;
	x6 *= s4;
	x6 += L1;
	double t1 = s2 * x6;
	/* t2 := s4 * (L2 + s4*(L4+s4*L6)) */
	x6 = L6 * s4;
	x6 += L4;
	x6 *= s4;
	x6 += L2;
	double t2 = s4 * x6;
	/* R := t1 + t2 */
	double R = t1 + t2;
	/* hfsq := 0.5 * f * f */
	double hfsq = 0.5 * f;
	hfsq *= f;
	/* return k*Ln2Hi - ((hfsq - (s*(hfsq+R) + k*Ln2Lo)) - f) */
	double x4 = R + hfsq;
	x3 = s * x4;
	x4 = Ln2Lo * k;
	x3 += x4;
	double x0 = hfsq - x3;
	x0 -= f;
	double x1 = k * Ln2Hi;
	return x1 - x0;
}

