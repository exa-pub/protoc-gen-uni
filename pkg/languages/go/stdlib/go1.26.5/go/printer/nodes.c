/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.26.5 src/go/printer/nodes.go.
 *
 * This file implements printing of AST nodes; specifically expressions, statements,
 * declarations, and files. It uses the print functionality implemented in printer.c.
 *
 * math.Exp and math.Log (used by the exprList alignment heuristic) are ported from the amd64
 * assembly Go uses (src/math/exp_amd64.s with the AVX/FMA path, src/math/log_amd64.s), so that
 * the alignment decisions match Go on amd64 bit for bit; see printer_exp. */
#include "../../internal.h"

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

/* ---- nodeSizes map ---- */

static uint64_t printer_hashPtr(const void *p)
{
	uint64_t h = (uint64_t)(uintptr_t)p;
	h ^= h >> 33;
	h *= UINT64_C(0xff51afd7ed558ccd);
	h ^= h >> 33;
	return h;
}

static bool printer_nodeSizes_get(printer_nodeSizeMap *m, const ast_Node *n, int *size)
{
	if (m->cap == 0)
		return false;
	for (uint64_t i = printer_hashPtr(n) & (uint64_t)(m->cap - 1);; i = (i + 1) & (uint64_t)(m->cap - 1)) {
		if (m->keys[i] == NULL)
			return false;
		if (m->keys[i] == n) {
			*size = m->vals[i];
			return true;
		}
	}
}

static void printer_nodeSizes_put(printer_nodeSizeMap *m, const ast_Node *n, int size)
{
	if (2 * (m->n + 1) > m->cap) {
		int cap = m->cap ? 2 * m->cap : 256;
		const ast_Node **keys = xalloc(sizeof *keys * (size_t)cap);
		int *vals = xalloc(sizeof *vals * (size_t)cap);
		for (int j = 0; j < m->cap; j++) {
			if (m->keys[j] == NULL)
				continue;
			uint64_t i = printer_hashPtr(m->keys[j]) & (uint64_t)(cap - 1);
			while (keys[i] != NULL)
				i = (i + 1) & (uint64_t)(cap - 1);
			keys[i] = m->keys[j];
			vals[i] = m->vals[j];
		}
		m->keys = keys;
		m->vals = vals;
		m->cap = cap;
	}
	uint64_t i = printer_hashPtr(n) & (uint64_t)(m->cap - 1);
	while (m->keys[i] != NULL && m->keys[i] != n)
		i = (i + 1) & (uint64_t)(m->cap - 1);
	if (m->keys[i] == NULL) {
		m->keys[i] = n;
		m->n++;
	}
	m->vals[i] = size;
}

/* Formatting issues:
 * - better comment formatting for / *-style comments at the end of a line (e.g. a declaration)
 *   when the comment spans multiple lines; if such a comment is just two lines, formatting is
 *   not idempotent
 * - formatting of expression lists
 * - should use blank instead of tab to separate one-line function bodies from the function
 *   header unless there is a group of consecutive one-liners */

/* ---- Common AST nodes. ---- */

/* Print as many newlines as necessary (but at least min newlines) to get to the current line.
 * ws is printed before the first line break. If newSection is set, the first line break is
 * printed as formfeed. Returns 0 if no line breaks were printed, returns 1 if there was exactly
 * one newline printed, and returns a value > 1 if there was a formfeed or more than one newline
 * printed. */
static int printer_linebreak(printer_printer *p, int line, int min, printer_whiteSpace ws, bool newSection)
{
	int nbreaks = 0;
	int n = printer_nlimit(line - p->pos.Line);
	if (n < min)
		n = min;
	if (n > 0) {
		printer_print(p, P_WS(ws));
		if (newSection) {
			printer_print(p, P_formfeed);
			n--;
			nbreaks = 2;
		}
		nbreaks += n;
		for (; n > 0; n--)
			printer_print(p, P_newline);
	}
	return nbreaks;
}

/* setComment sets g as the next comment if g != nil and if node comments are enabled - this
 * mode is used when printing source code fragments such as exports only. It assumes that there
 * is no pending comment in p.comments and at most one pending comment in the p.comment cache. */
static void printer_setComment(printer_printer *p, ast_CommentGroup *g)
{
	if (g == NULL || !p->useNodeComments)
		return;
	if (p->comments.v == NULL) {
		/* initialize p.comments lazily */
		p->comments = (Vec){0};
		vec_push(&p->comments, NULL);
	} else if (p->cindex < p->comments.n) {
		/* for some reason there are pending comments; this should never happen - handle
		 * gracefully and flush all comments up to g, ignore anything after that */
		bool wroteNewline, droppedFF;
		printer_flush(p, printer_posFor(p, ast_Pos(g->List.v[0])), token_ILLEGAL, &wroteNewline, &droppedFF);
		p->comments.n = 1;
		/* in debug mode, report error */
		printer_internalError(p, "setComment found pending comments");
	}
	p->comments.v[0] = g;
	p->cindex = 0;
	/* don't overwrite any pending comment in the p.comment cache (there may be a pending
	 * comment when a line comment is immediately followed by a lead comment with no other
	 * tokens between) */
	if (p->commentOffset == printer_infinity)
		printer_nextComment(p); /* get comment ready for use */
}

typedef unsigned printer_exprListMode;

enum {
	printer_commaTerm = 1 << 0, /* list is optionally terminated by a comma */
	printer_noIndent = 1 << 1, /* no extra indentation in multi-line lists */
};

static void printer_exprList(printer_printer *p, token_Pos prev0, Vec list, int depth, printer_exprListMode mode,
			     token_Pos next0, bool isIncomplete);
static void printer_expr0(printer_printer *p, ast_Expr *x, int depth);
static int printer_nodeSize(printer_printer *p, ast_Node *n, int maxSize);

/* If indent is set, a multi-line identifier list is indented after the first linebreak
 * encountered. */
static void printer_identList(printer_printer *p, Vec list, bool indent)
{
	/* convert into an expression list so we can re-use exprList formatting (a Vec of
	 * *ast.Ident is a Vec of ast.Expr already) */
	printer_exprListMode mode = 0;
	if (!indent)
		mode = printer_noIndent;
	printer_exprList(p, token_NoPos, list, 1, mode, token_NoPos, false);
}

#define printer_filteredMsg "contains filtered or unexported fields"

/* Print a list of expressions. If the list spans multiple source lines, the original line
 * breaks are respected between expressions. */
static void printer_exprList(printer_printer *p, token_Pos prev0, Vec list, int depth, printer_exprListMode mode,
			     token_Pos next0, bool isIncomplete)
{
	if (list.n == 0) {
		if (isIncomplete) {
			token_Position prev = printer_posFor(p, prev0);
			token_Position next = printer_posFor(p, next0);
			if (token_Position_IsValid(&prev) && prev.Line == next.Line) {
				printer_print(p, P_STR(GOSTR("/* " printer_filteredMsg " */")));
			} else {
				printer_print(p, P_newline);
				printer_print(p, P_indent, P_STR(GOSTR("// " printer_filteredMsg)), P_unindent, P_newline);
			}
		}
		return;
	}

	token_Position prev = printer_posFor(p, prev0);
	token_Position next = printer_posFor(p, next0);
	int line = printer_lineFor(p, ast_Pos(list.v[0]));
	int endLine = printer_lineFor(p, ast_End(list.v[list.n - 1]));

	if (token_Position_IsValid(&prev) && prev.Line == line && line == endLine) {
		/* all list entries on a single line */
		for (int i = 0; i < list.n; i++) {
			ast_Expr *x = list.v[i];
			if (i > 0) {
				/* use position of expression following the comma as comma position for
				 * correct comment placement */
				printer_setPos(p, ast_Pos(x));
				printer_print(p, P_TOK(token_COMMA), P_blank);
			}
			printer_expr0(p, x, depth);
		}
		if (isIncomplete)
			printer_print(p, P_TOK(token_COMMA), P_blank, P_STR(GOSTR("/* " printer_filteredMsg " */")));
		return;
	}

	/* list entries span multiple lines; use source code positions to guide line breaks */

	/* Don't add extra indentation if noIndent is set; i.e., pretend that the first line is
	 * already indented. */
	printer_whiteSpace ws = printer_ignore;
	if ((mode & printer_noIndent) == 0)
		ws = printer_indent;

	/* The first linebreak is always a formfeed since this section must not depend on any
	 * previous formatting. */
	int prevBreak = -1; /* index of last expression that was followed by a linebreak */
	if (token_Position_IsValid(&prev) && prev.Line < line && printer_linebreak(p, line, 0, ws, true) > 0) {
		ws = printer_ignore;
		prevBreak = 0;
	}

	/* initialize expression/key size: a zero value indicates expr/key doesn't fit on a single
	 * line */
	int size = 0;

	/* We use the ratio between the geometric mean of the previous key sizes and the current
	 * size to determine if there should be a break in the alignment. To compute the geometric
	 * mean we accumulate the ln(size) values (lnsum) and the number of sizes included (count). */
	double lnsum = 0.0;
	int count = 0;

	/* print all list elements */
	int prevLine = prev.Line;
	for (int i = 0; i < list.n; i++) {
		ast_Expr *x = list.v[i];
		line = printer_lineFor(p, ast_Pos(x));

		/* Determine if the next linebreak, if any, needs to use formfeed: in general, use the
		 * entire node size to make the decision; for key:value expressions, use the key
		 * size. */
		bool useFF = true;

		/* Determine element size: All bets are off if we don't have position information
		 * for the previous and next token (likely generated code - simply ignore the size in
		 * this case by setting it to 0). */
		int prevSize = size;
		const int infinity = 1000000; /* larger than any source line */
		size = printer_nodeSize(p, x, infinity);
		ast_KeyValueExpr *pair = x->kind == AST_KeyValueExpr ? (ast_KeyValueExpr *)x : NULL;
		bool isPair = pair != NULL;
		if (size <= infinity && token_Position_IsValid(&prev) && token_Position_IsValid(&next)) {
			/* x fits on a single line */
			if (isPair)
				size = printer_nodeSize(p, pair->Key, infinity); /* size <= infinity */
		} else {
			/* size too large or we don't have good layout information */
			size = 0;
		}

		/* If the previous line and the current line had single- line-expressions and the key
		 * sizes are small or the ratio between the current key and the geometric mean if the
		 * previous key sizes does not exceed a threshold, align columns and do not use
		 * formfeed. */
		if (prevSize > 0 && size > 0) {
			const int smallSize = 40;
			if (count == 0 || (prevSize <= smallSize && size <= smallSize)) {
				useFF = false;
			} else {
				const double r = 2.5; /* threshold */
				double geomean = printer_exp(lnsum / (double)count); /* count > 0 */
				double ratio = (double)size / geomean;
				useFF = r * ratio <= 1 || r <= ratio;
			}
		}

		bool needsLinebreak = 0 < prevLine && prevLine < line;
		if (i > 0) {
			/* Use position of expression following the comma as comma position for correct
			 * comment placement, but only if the expression is on the same line. */
			if (!needsLinebreak)
				printer_setPos(p, ast_Pos(x));
			printer_print(p, P_TOK(token_COMMA));
			bool needsBlank = true;
			if (needsLinebreak) {
				/* Lines are broken using newlines so comments remain aligned unless useFF
				 * is set or there are multiple expressions on the same line in which case
				 * formfeed is used. */
				int nbreaks = printer_linebreak(p, line, 0, ws, useFF || prevBreak + 1 < i);
				if (nbreaks > 0) {
					ws = printer_ignore;
					prevBreak = i;
					needsBlank = false; /* we got a line break instead */
				}
				/* If there was a new section or more than one new line (which means that
				 * the tabwriter will implicitly break the section), reset the geomean
				 * variables since we are starting a new group of elements with the next
				 * element. */
				if (nbreaks > 1) {
					lnsum = 0;
					count = 0;
				}
			}
			if (needsBlank)
				printer_print(p, P_blank);
		}

		if (list.n > 1 && isPair && size > 0 && needsLinebreak) {
			/* We have a key:value expression that fits onto one line and it's not on the
			 * same line as the prior expression: Use a column for the key such that
			 * consecutive entries can align if possible. (needsLinebreak is set if we
			 * started a new line before) */
			printer_expr(p, pair->Key);
			printer_setPos(p, pair->Colon);
			printer_print(p, P_TOK(token_COLON), P_vtab);
			printer_expr(p, pair->Value);
		} else {
			printer_expr0(p, x, depth);
		}

		if (size > 0) {
			lnsum += printer_log((double)size);
			count++;
		}

		prevLine = line;
	}

	if ((mode & printer_commaTerm) != 0 && token_Position_IsValid(&next) && p->pos.Line < next.Line) {
		/* Print a terminating comma if the next token is on a new line. */
		printer_print(p, P_TOK(token_COMMA));
		if (isIncomplete) {
			printer_print(p, P_newline);
			printer_print(p, P_STR(GOSTR("// " printer_filteredMsg)));
		}
		if (ws == printer_ignore && (mode & printer_noIndent) == 0) {
			/* unindent if we indented */
			printer_print(p, P_unindent);
		}
		printer_print(p, P_formfeed); /* terminating comma needs a line break to look good */
		return;
	}

	if (isIncomplete) {
		printer_print(p, P_TOK(token_COMMA), P_newline);
		printer_print(p, P_STR(GOSTR("// " printer_filteredMsg)), P_newline);
	}

	if (ws == printer_ignore && (mode & printer_noIndent) == 0) {
		/* unindent if we indented */
		printer_print(p, P_unindent);
	}
}

typedef enum {
	printer_funcParam,
	printer_funcTParam,
	printer_typeTParam,
} printer_paramMode;

static ast_Expr *printer_stripParensAlways(ast_Expr *x);
static bool printer_combinesWithName(ast_Expr *x);

static void printer_parameters(printer_printer *p, ast_FieldList *fields, printer_paramMode mode)
{
	token_Token openTok = token_LPAREN, closeTok = token_RPAREN;
	if (mode != printer_funcParam) {
		openTok = token_LBRACK;
		closeTok = token_RBRACK;
	}
	printer_setPos(p, fields->Opening);
	printer_print(p, P_TOK(openTok));
	if (fields->List.n > 0) {
		int prevLine = printer_lineFor(p, fields->Opening);
		printer_whiteSpace ws = printer_indent;
		for (int i = 0; i < fields->List.n; i++) {
			ast_Field *par = fields->List.v[i];
			/* determine par begin and end line (may be different if there are multiple
			 * parameter names for this par or the type is on a separate line) */
			int parLineBeg = printer_lineFor(p, ast_Pos(par));
			int parLineEnd = printer_lineFor(p, ast_End(par));
			/* separating "," if needed */
			bool needsLinebreak = 0 < prevLine && prevLine < parLineBeg;
			if (i > 0) {
				/* use position of parameter following the comma as comma position for
				 * correct comma placement, but only if the next parameter is on the same
				 * line */
				if (!needsLinebreak)
					printer_setPos(p, ast_Pos(par));
				printer_print(p, P_TOK(token_COMMA));
			}
			/* separator if needed (linebreak or blank) */
			if (needsLinebreak && printer_linebreak(p, parLineBeg, 0, ws, true) > 0) {
				/* break line if the opening "(" or previous parameter ended on a different
				 * line */
				ws = printer_ignore;
			} else if (i > 0) {
				printer_print(p, P_blank);
			}
			/* parameter names */
			if (par->Names.n > 0) {
				/* Very subtle: If we indented before (ws == ignore), identList won't
				 * indent again. If we didn't (ws == indent), identList will indent if the
				 * identList spans multiple lines, and it will outdent again at the end
				 * (and still ws == indent). Thus, a subsequent indent by a linebreak call
				 * after a type, or in the next multi-line identList will do the right
				 * thing. */
				printer_identList(p, par->Names, ws == printer_indent);
				printer_print(p, P_blank);
			}
			/* parameter type */
			printer_expr(p, printer_stripParensAlways(par->Type));
			prevLine = parLineEnd;
		}

		/* if the closing ")" is on a separate line from the last parameter, print an
		 * additional "," and line break */
		int closing = printer_lineFor(p, fields->Closing);
		if (0 < prevLine && prevLine < closing) {
			printer_print(p, P_TOK(token_COMMA));
			printer_linebreak(p, closing, 0, printer_ignore, true);
		} else if (mode == printer_typeTParam && ast_FieldList_NumFields(fields) == 1 &&
			   printer_combinesWithName(
				   printer_stripParensAlways(VEC_AT(fields->List, ast_Field, 0)->Type))) {
			/* A type parameter list [P T] where the name P and the type expression T
			 * syntactically combine to another valid (value) expression requires a trailing
			 * comma, as in [P *T,] (or an enclosing interface as in [P interface(*T)]), so
			 * that the type parameter list is not parsed as an array length [P*T]. */
			printer_print(p, P_TOK(token_COMMA));
		}

		/* unindent if we indented */
		if (ws == printer_ignore)
			printer_print(p, P_unindent);
	}

	printer_setPos(p, fields->Closing);
	printer_print(p, P_TOK(closeTok));
}

static bool printer_isTypeElem(ast_Expr *x);

/* combinesWithName reports whether a name followed by the expression x syntactically combines
 * to another valid (value) expression. For instance using *T for x, "name *T" syntactically
 * appears as the expression x*T. On the other hand, using  P|Q or *P|~Q for x, "name P|Q" or
 * "name *P|~Q" cannot be combined into a valid (value) expression. */
static bool printer_combinesWithName(ast_Expr *x)
{
	switch (x->kind) {
	case AST_StarExpr:
		/* name *x.X combines to name*x.X if x.X is not a type element */
		return !printer_isTypeElem(((ast_StarExpr *)x)->X);
	case AST_BinaryExpr:
		return printer_combinesWithName(((ast_BinaryExpr *)x)->X) &&
		       !printer_isTypeElem(((ast_BinaryExpr *)x)->Y);
	case AST_ParenExpr:
		return !printer_isTypeElem(((ast_ParenExpr *)x)->X);
	default:
		return false;
	}
}

/* isTypeElem reports whether x is a (possibly parenthesized) type element expression. The
 * result is false if x could be a type element OR an ordinary (value) expression. */
static bool printer_isTypeElem(ast_Expr *x)
{
	switch (x->kind) {
	case AST_ArrayType:
	case AST_StructType:
	case AST_FuncType:
	case AST_InterfaceType:
	case AST_MapType:
	case AST_ChanType:
		return true;
	case AST_UnaryExpr:
		return ((ast_UnaryExpr *)x)->Op == token_TILDE;
	case AST_BinaryExpr:
		return printer_isTypeElem(((ast_BinaryExpr *)x)->X) || printer_isTypeElem(((ast_BinaryExpr *)x)->Y);
	case AST_ParenExpr:
		return printer_isTypeElem(((ast_ParenExpr *)x)->X);
	default:
		return false;
	}
}

static void printer_signature(printer_printer *p, ast_FuncType *sig)
{
	if (sig->TypeParams != NULL)
		printer_parameters(p, sig->TypeParams, printer_funcTParam);
	if (sig->Params != NULL)
		printer_parameters(p, sig->Params, printer_funcParam);
	else
		printer_print(p, P_TOK(token_LPAREN), P_TOK(token_RPAREN));
	ast_FieldList *res = sig->Results;
	int n = res ? ast_FieldList_NumFields(res) : 0;
	if (n > 0) {
		/* res != nil */
		printer_print(p, P_blank);
		if (n == 1 && VEC_AT(res->List, ast_Field, 0)->Names.n == 0) {
			/* single anonymous res; no ()'s */
			printer_expr(p, printer_stripParensAlways(VEC_AT(res->List, ast_Field, 0)->Type));
			return;
		}
		printer_parameters(p, res, printer_funcParam);
	}
}

static int printer_identListSize(Vec list, int maxSize)
{
	int size = 0;
	for (int i = 0; i < list.n; i++) {
		if (i > 0)
			size += 2; /* len(", ") */
		size += utf8_RuneCountInString(VEC_AT(list, ast_Ident, i)->Name);
		if (size >= maxSize)
			break;
	}
	return size;
}

static bool printer_isOneLineFieldList(printer_printer *p, Vec list)
{
	if (list.n != 1)
		return false; /* allow only one field */
	ast_Field *f = list.v[0];
	if (f->Tag != NULL || f->Comment != NULL)
		return false; /* don't allow tags or comments */
	/* only name(s) and type */
	const int maxSize = 30; /* adjust as appropriate, this is an approximate value */
	int namesSize = printer_identListSize(f->Names, maxSize);
	if (namesSize > 0)
		namesSize = 1; /* blank between names and types */
	int typeSize = printer_nodeSize(p, f->Type, maxSize);
	return namesSize + typeSize <= maxSize;
}

static void printer_setLineComment(printer_printer *p, GoStr text)
{
	ast_Comment *c = AST_NEW(Comment);
	c->Slash = token_NoPos;
	c->Text = text;
	ast_CommentGroup *g = AST_NEW(CommentGroup);
	vec_push(&g->List, c);
	printer_setComment(p, g);
}

static void printer_fieldList(printer_printer *p, ast_FieldList *fields, bool isStruct, bool isIncomplete)
{
	token_Pos lbrace = fields->Opening;
	Vec list = fields->List;
	token_Pos rbrace = fields->Closing;
	bool hasComments = isIncomplete || printer_commentBefore(p, printer_posFor(p, rbrace));
	bool srcIsOneLine = token_Pos_IsValid(lbrace) && token_Pos_IsValid(rbrace) &&
			    printer_lineFor(p, lbrace) == printer_lineFor(p, rbrace);

	if (!hasComments && srcIsOneLine) {
		/* possibly a one-line struct/interface */
		if (list.n == 0) {
			/* no blank between keyword and {} in this case */
			printer_setPos(p, lbrace);
			printer_print(p, P_TOK(token_LBRACE));
			printer_setPos(p, rbrace);
			printer_print(p, P_TOK(token_RBRACE));
			return;
		} else if (printer_isOneLineFieldList(p, list)) {
			/* small enough - print on one line (don't use identList and ignore source line
			 * breaks) */
			printer_setPos(p, lbrace);
			printer_print(p, P_TOK(token_LBRACE), P_blank);
			ast_Field *f = list.v[0];
			if (isStruct) {
				for (int i = 0; i < f->Names.n; i++) {
					if (i > 0) {
						/* no comments so no need for comma position */
						printer_print(p, P_TOK(token_COMMA), P_blank);
					}
					printer_expr(p, f->Names.v[i]);
				}
				if (f->Names.n > 0)
					printer_print(p, P_blank);
				printer_expr(p, f->Type);
			} else { /* interface */
				if (f->Names.n > 0) {
					ast_Ident *name = f->Names.v[0]; /* method name */
					printer_expr(p, (ast_Expr *)name);
					if (f->Type->kind != AST_FuncType)
						fail("go/printer: interface method type is not a function type");
					printer_signature(p, (ast_FuncType *)f->Type); /* don't print "func" */
				} else {
					/* embedded interface */
					printer_expr(p, f->Type);
				}
			}
			printer_print(p, P_blank);
			printer_setPos(p, rbrace);
			printer_print(p, P_TOK(token_RBRACE));
			return;
		}
	}
	/* hasComments || !srcIsOneLine */

	printer_print(p, P_blank);
	printer_setPos(p, lbrace);
	printer_print(p, P_TOK(token_LBRACE), P_indent);
	if (hasComments || list.n > 0)
		printer_print(p, P_formfeed);

	if (isStruct) {

		printer_whiteSpace sep = printer_vtab;
		if (list.n == 1)
			sep = printer_blank;
		int line = 0;
		for (int i = 0; i < list.n; i++) {
			ast_Field *f = list.v[i];
			if (i > 0)
				printer_linebreak(p, printer_lineFor(p, ast_Pos(f)), 1, printer_ignore,
						  printer_linesFrom(p, line) > 0);
			int extraTabs = 0;
			printer_setComment(p, f->Doc);
			printer_recordLine(p, &line);
			if (f->Names.n > 0) {
				/* named fields */
				printer_identList(p, f->Names, false);
				printer_print(p, P_WS(sep));
				printer_expr(p, f->Type);
				extraTabs = 1;
			} else {
				/* anonymous field */
				printer_expr(p, f->Type);
				extraTabs = 2;
			}
			if (f->Tag != NULL) {
				if (f->Names.n > 0 && sep == printer_vtab)
					printer_print(p, P_WS(sep));
				printer_print(p, P_WS(sep));
				printer_expr(p, (ast_Expr *)f->Tag);
				extraTabs = 0;
			}
			if (f->Comment != NULL) {
				for (; extraTabs > 0; extraTabs--)
					printer_print(p, P_WS(sep));
				printer_setComment(p, f->Comment);
			}
		}
		if (isIncomplete) {
			if (list.n > 0)
				printer_print(p, P_formfeed);
			bool wroteNewline, droppedFF;
			printer_flush(p, printer_posFor(p, rbrace), token_RBRACE, &wroteNewline,
				      &droppedFF); /* make sure we don't lose the last line comment */
			printer_setLineComment(p, GOSTR("// " printer_filteredMsg));
		}

	} else { /* interface */

		int line = 0;
		ast_Ident *prev = NULL; /* previous "type" identifier */
		for (int i = 0; i < list.n; i++) {
			ast_Field *f = list.v[i];
			ast_Ident *name = NULL; /* first name, or NULL */
			if (f->Names.n > 0)
				name = f->Names.v[0];
			if (i > 0) {
				/* don't do a line break (min == 0) if we are printing a list of types */
				int min = 1;
				if (prev != NULL && name == prev)
					min = 0;
				printer_linebreak(p, printer_lineFor(p, ast_Pos(f)), min, printer_ignore,
						  printer_linesFrom(p, line) > 0);
			}
			printer_setComment(p, f->Doc);
			printer_recordLine(p, &line);
			if (name != NULL) {
				/* method */
				printer_expr(p, (ast_Expr *)name);
				if (f->Type->kind != AST_FuncType)
					fail("go/printer: interface method type is not a function type");
				printer_signature(p, (ast_FuncType *)f->Type); /* don't print "func" */
				prev = NULL;
			} else {
				/* embedded interface */
				printer_expr(p, f->Type);
				prev = NULL;
			}
			printer_setComment(p, f->Comment);
		}
		if (isIncomplete) {
			if (list.n > 0)
				printer_print(p, P_formfeed);
			bool wroteNewline, droppedFF;
			printer_flush(p, printer_posFor(p, rbrace), token_RBRACE, &wroteNewline,
				      &droppedFF); /* make sure we don't lose the last line comment */
			printer_setLineComment(p, GOSTR("// contains filtered or unexported methods"));
		}
	}
	printer_print(p, P_unindent, P_formfeed);
	printer_setPos(p, rbrace);
	printer_print(p, P_TOK(token_RBRACE));
}

/* ---- Expressions ---- */

static void printer_walkBinary(ast_BinaryExpr *e, bool *has4, bool *has5, int *maxProblem)
{
	*has4 = false;
	*has5 = false;
	*maxProblem = 0;
	switch (token_Token_Precedence(e->Op)) {
	case 4:
		*has4 = true;
		break;
	case 5:
		*has5 = true;
		break;
	}

	if (e->X->kind == AST_BinaryExpr) {
		ast_BinaryExpr *l = (ast_BinaryExpr *)e->X;
		if (token_Token_Precedence(l->Op) < token_Token_Precedence(e->Op)) {
			/* parens will be inserted. pretend this is an *ast.ParenExpr and do nothing. */
		} else {
			bool h4, h5;
			int mp;
			printer_walkBinary(l, &h4, &h5, &mp);
			*has4 = *has4 || h4;
			*has5 = *has5 || h5;
			if (mp > *maxProblem)
				*maxProblem = mp;
		}
	}

	switch (e->Y->kind) {
	case AST_BinaryExpr: {
		ast_BinaryExpr *r = (ast_BinaryExpr *)e->Y;
		if (token_Token_Precedence(r->Op) <= token_Token_Precedence(e->Op)) {
			/* parens will be inserted. pretend this is an *ast.ParenExpr and do nothing. */
			break;
		}
		bool h4, h5;
		int mp;
		printer_walkBinary(r, &h4, &h5, &mp);
		*has4 = *has4 || h4;
		*has5 = *has5 || h5;
		if (mp > *maxProblem)
			*maxProblem = mp;
		break;
	}

	case AST_StarExpr:
		if (e->Op == token_QUO) /* `*\/` */
			*maxProblem = 5;
		break;

	case AST_UnaryExpr: {
		ast_UnaryExpr *r = (ast_UnaryExpr *)e->Y;
		GoStr ops = gostr_cat(token_Token_String(e->Op), token_Token_String(r->Op));
		if (gostr_eqc(ops, "/*") || gostr_eqc(ops, "&&") || gostr_eqc(ops, "&^")) {
			*maxProblem = 5;
		} else if (gostr_eqc(ops, "++") || gostr_eqc(ops, "--")) {
			if (*maxProblem < 4)
				*maxProblem = 4;
		}
		break;
	}

	default:
		break;
	}
}

static int printer_cutoff(ast_BinaryExpr *e, int depth)
{
	bool has4, has5;
	int maxProblem;
	printer_walkBinary(e, &has4, &has5, &maxProblem);
	if (maxProblem > 0)
		return maxProblem + 1;
	if (has4 && has5) {
		if (depth == 1)
			return 5;
		return 4;
	}
	if (depth == 1)
		return 6;
	return 4;
}

static int printer_diffPrec(ast_Expr *expr, int prec)
{
	if (expr->kind != AST_BinaryExpr || prec != token_Token_Precedence(((ast_BinaryExpr *)expr)->Op))
		return 1;
	return 0;
}

static int printer_reduceDepth(int depth)
{
	depth--;
	if (depth < 1)
		depth = 1;
	return depth;
}

static void printer_expr1(printer_printer *p, ast_Expr *expr, int prec1, int depth);

/* Format the binary expression: decide the cutoff and then format. Let's call depth == 1
 * Normal mode, and depth > 1 Compact mode. (Algorithm suggestion by Russ Cox.)
 *
 * The precedences are:
 *
 *	5             *  /  %  <<  >>  &  &^
 *	4             +  -  |  ^
 *	3             ==  !=  <  <=  >  >=
 *	2             &&
 *	1             ||
 *
 * The only decision is whether there will be spaces around levels 4 and 5. There are never
 * spaces at level 6 (unary), and always spaces at levels 3 and below.
 *
 * To choose the cutoff, look at the whole expression but excluding primary expressions
 * (function calls, parenthesized exprs), and apply these rules:
 *
 *  1. If there is a binary operator with a right side unary operand that would clash without a
 *     space, the cutoff must be (in order): / *	6, &&	6, &^	6, ++	5, --	5.
 *     (Comparison operators always have spaces around them.)
 *
 *  2. If there is a mix of level 5 and level 4 operators, then the cutoff is 5 (use spaces to
 *     distinguish precedence) in Normal mode and 4 (never use spaces) in Compact mode.
 *
 *  3. If there are no level 4 operators or no level 5 operators, then the cutoff is 6 (always
 *     use spaces) in Normal mode and 4 (never use spaces) in Compact mode. */
static void printer_binaryExpr(printer_printer *p, ast_BinaryExpr *x, int prec1, int cutoff, int depth)
{
	int prec = token_Token_Precedence(x->Op);
	if (prec < prec1) {
		/* parenthesis needed. Note: The parser inserts an ast.ParenExpr node; thus this case
		 * can only occur if the AST is created in a different way. */
		printer_print(p, P_TOK(token_LPAREN));
		printer_expr0(p, (ast_Expr *)x, printer_reduceDepth(depth)); /* parentheses undo one level of depth */
		printer_print(p, P_TOK(token_RPAREN));
		return;
	}

	bool printBlank = prec < cutoff;

	printer_whiteSpace ws = printer_indent;
	printer_expr1(p, x->X, prec, depth + printer_diffPrec(x->X, prec));
	if (printBlank)
		printer_print(p, P_blank);
	int xline = p->pos.Line; /* before the operator (it may be on the next line!) */
	int yline = printer_lineFor(p, ast_Pos(x->Y));
	printer_setPos(p, x->OpPos);
	printer_print(p, P_TOK(x->Op));
	if (xline != yline && xline > 0 && yline > 0) {
		/* at least one line break, but respect an extra empty line in the source */
		if (printer_linebreak(p, yline, 1, ws, true) > 0) {
			ws = printer_ignore;
			printBlank = false; /* no blank after line break */
		}
	}
	if (printBlank)
		printer_print(p, P_blank);
	printer_expr1(p, x->Y, prec + 1, depth + 1);
	if (ws == printer_ignore)
		printer_print(p, P_unindent);
}

static bool printer_isBinary(ast_Expr *expr) { return expr->kind == AST_BinaryExpr; }

static ast_BasicLit *printer_normalizedNumber(ast_BasicLit *lit);
static bool printer_possibleSelectorExpr(printer_printer *p, ast_Expr *expr, int prec1, int depth);
static bool printer_selectorExpr(printer_printer *p, ast_SelectorExpr *x, int depth, bool isMethod);
static void printer_funcBody(printer_printer *p, int headerSize, printer_whiteSpace sep, ast_BlockStmt *b);
static int printer_distanceFrom(printer_printer *p, token_Pos startPos, int startOutCol);
static void printer_fieldList(printer_printer *p, ast_FieldList *fields, bool isStruct, bool isIncomplete);

static void printer_expr1(printer_printer *p, ast_Expr *expr, int prec1, int depth)
{
	printer_setPos(p, ast_Pos(expr));

	switch (expr->kind) {
	case AST_BadExpr:
		printer_print(p, P_STR(GOSTR("BadExpr")));
		break;

	case AST_Ident:
		printer_print(p, P_IDENT(expr));
		break;

	case AST_BinaryExpr: {
		ast_BinaryExpr *x = (ast_BinaryExpr *)expr;
		if (depth < 1) {
			printer_internalError(p, "depth < 1");
			depth = 1;
		}
		printer_binaryExpr(p, x, prec1, printer_cutoff(x, depth), depth);
		break;
	}

	case AST_KeyValueExpr: {
		ast_KeyValueExpr *x = (ast_KeyValueExpr *)expr;
		printer_expr(p, x->Key);
		printer_setPos(p, x->Colon);
		printer_print(p, P_TOK(token_COLON), P_blank);
		printer_expr(p, x->Value);
		break;
	}

	case AST_StarExpr: {
		ast_StarExpr *x = (ast_StarExpr *)expr;
		const int prec = token_UnaryPrec;
		if (prec < prec1) {
			/* parenthesis needed */
			printer_print(p, P_TOK(token_LPAREN));
			printer_print(p, P_TOK(token_MUL));
			printer_expr(p, x->X);
			printer_print(p, P_TOK(token_RPAREN));
		} else {
			/* no parenthesis needed */
			printer_print(p, P_TOK(token_MUL));
			printer_expr(p, x->X);
		}
		break;
	}

	case AST_UnaryExpr: {
		ast_UnaryExpr *x = (ast_UnaryExpr *)expr;
		const int prec = token_UnaryPrec;
		if (prec < prec1) {
			/* parenthesis needed */
			printer_print(p, P_TOK(token_LPAREN));
			printer_expr(p, expr);
			printer_print(p, P_TOK(token_RPAREN));
		} else {
			/* no parenthesis needed */
			printer_print(p, P_TOK(x->Op));
			if (x->Op == token_RANGE) {
				/* TODO(gri) Remove this code if it cannot be reached. */
				printer_print(p, P_blank);
			}
			printer_expr1(p, x->X, prec, depth);
		}
		break;
	}

	case AST_BasicLit: {
		ast_BasicLit *x = (ast_BasicLit *)expr;
		if (p->Config.Mode & printer_normalizeNumbers)
			x = printer_normalizedNumber(x);
		printer_print(p, P_LIT(x));
		break;
	}

	case AST_FuncLit: {
		ast_FuncLit *x = (ast_FuncLit *)expr;
		printer_setPos(p, ast_Pos(x->Type));
		printer_print(p, P_TOK(token_FUNC));
		/* See the comment in funcDecl about how the header size is computed. */
		int startCol = p->out.Column - 4; /* len("func") */
		printer_signature(p, x->Type);
		printer_funcBody(p, printer_distanceFrom(p, ast_Pos(x->Type), startCol), printer_blank, x->Body);
		break;
	}

	case AST_ParenExpr: {
		ast_ParenExpr *x = (ast_ParenExpr *)expr;
		if (x->X->kind == AST_ParenExpr) {
			/* don't print parentheses around an already parenthesized expression */
			printer_expr0(p, x->X, depth);
		} else {
			printer_print(p, P_TOK(token_LPAREN));
			printer_expr0(p, x->X, printer_reduceDepth(depth)); /* parentheses undo one level of depth */
			printer_setPos(p, x->Rparen);
			printer_print(p, P_TOK(token_RPAREN));
		}
		break;
	}

	case AST_SelectorExpr:
		printer_selectorExpr(p, (ast_SelectorExpr *)expr, depth, false);
		break;

	case AST_TypeAssertExpr: {
		ast_TypeAssertExpr *x = (ast_TypeAssertExpr *)expr;
		printer_expr1(p, x->X, token_HighestPrec, depth);
		printer_print(p, P_TOK(token_PERIOD));
		printer_setPos(p, x->Lparen);
		printer_print(p, P_TOK(token_LPAREN));
		if (x->Type != NULL)
			printer_expr(p, x->Type);
		else
			printer_print(p, P_TOK(token_TYPE));
		printer_setPos(p, x->Rparen);
		printer_print(p, P_TOK(token_RPAREN));
		break;
	}

	case AST_IndexExpr: {
		ast_IndexExpr *x = (ast_IndexExpr *)expr;
		/* TODO(gri): should treat[] like parentheses and undo one level of depth */
		printer_expr1(p, x->X, token_HighestPrec, 1);
		printer_setPos(p, x->Lbrack);
		printer_print(p, P_TOK(token_LBRACK));
		printer_expr0(p, x->Index, depth + 1);
		printer_setPos(p, x->Rbrack);
		printer_print(p, P_TOK(token_RBRACK));
		break;
	}

	case AST_IndexListExpr: {
		ast_IndexListExpr *x = (ast_IndexListExpr *)expr;
		/* TODO(gri): as for IndexExpr, should treat [] like parentheses and undo one level of
		 * depth */
		printer_expr1(p, x->X, token_HighestPrec, 1);
		printer_setPos(p, x->Lbrack);
		printer_print(p, P_TOK(token_LBRACK));
		printer_exprList(p, x->Lbrack, x->Indices, depth + 1, printer_commaTerm, x->Rbrack, false);
		printer_setPos(p, x->Rbrack);
		printer_print(p, P_TOK(token_RBRACK));
		break;
	}

	case AST_SliceExpr: {
		ast_SliceExpr *x = (ast_SliceExpr *)expr;
		/* TODO(gri): should treat[] like parentheses and undo one level of depth */
		printer_expr1(p, x->X, token_HighestPrec, 1);
		printer_setPos(p, x->Lbrack);
		printer_print(p, P_TOK(token_LBRACK));
		ast_Expr *indices[3] = {x->Low, x->High, x->Max};
		int nindices = 2;
		if (x->Max != NULL)
			nindices = 3;
		/* determine if we need extra blanks around ':' */
		bool needsBlanks = false;
		if (depth <= 1) {
			int indexCount = 0;
			bool hasBinaries = false;
			for (int i = 0; i < nindices; i++) {
				if (indices[i] != NULL) {
					indexCount++;
					if (printer_isBinary(indices[i]))
						hasBinaries = true;
				}
			}
			if (indexCount > 1 && hasBinaries)
				needsBlanks = true;
		}
		for (int i = 0; i < nindices; i++) {
			ast_Expr *ix = indices[i];
			if (i > 0) {
				if (indices[i - 1] != NULL && needsBlanks)
					printer_print(p, P_blank);
				printer_print(p, P_TOK(token_COLON));
				if (ix != NULL && needsBlanks)
					printer_print(p, P_blank);
			}
			if (ix != NULL)
				printer_expr0(p, ix, depth + 1);
		}
		printer_setPos(p, x->Rbrack);
		printer_print(p, P_TOK(token_RBRACK));
		break;
	}

	case AST_CallExpr: {
		ast_CallExpr *x = (ast_CallExpr *)expr;
		if (x->Args.n > 1)
			depth++;

		/* Conversions to literal function types or <-chan types require parentheses around
		 * the type. */
		bool paren = false;
		switch (x->Fun->kind) {
		case AST_FuncType:
			paren = true;
			break;
		case AST_ChanType:
			paren = ((ast_ChanType *)x->Fun)->Dir == ast_RECV;
			break;
		default:
			break;
		}
		if (paren)
			printer_print(p, P_TOK(token_LPAREN));
		bool wasIndented = printer_possibleSelectorExpr(p, x->Fun, token_HighestPrec, depth);
		if (paren)
			printer_print(p, P_TOK(token_RPAREN));

		printer_setPos(p, x->Lparen);
		printer_print(p, P_TOK(token_LPAREN));
		if (token_Pos_IsValid(x->Ellipsis)) {
			printer_exprList(p, x->Lparen, x->Args, depth, 0, x->Ellipsis, false);
			printer_setPos(p, x->Ellipsis);
			printer_print(p, P_TOK(token_ELLIPSIS));
			if (token_Pos_IsValid(x->Rparen) && printer_lineFor(p, x->Ellipsis) < printer_lineFor(p, x->Rparen))
				printer_print(p, P_TOK(token_COMMA), P_formfeed);
		} else {
			printer_exprList(p, x->Lparen, x->Args, depth, printer_commaTerm, x->Rparen, false);
		}
		printer_setPos(p, x->Rparen);
		printer_print(p, P_TOK(token_RPAREN));
		if (wasIndented)
			printer_print(p, P_unindent);
		break;
	}

	case AST_CompositeLit: {
		ast_CompositeLit *x = (ast_CompositeLit *)expr;
		/* composite literal elements that are composite literals themselves may have the type
		 * omitted */
		if (x->Type != NULL)
			printer_expr1(p, x->Type, token_HighestPrec, depth);
		p->level++;
		printer_setPos(p, x->Lbrace);
		printer_print(p, P_TOK(token_LBRACE));
		printer_exprList(p, x->Lbrace, x->Elts, 1, printer_commaTerm, x->Rbrace, x->Incomplete);
		/* do not insert extra line break following a / *-style comment before the closing '}'
		 * as it might break the code if there is no trailing ',' */
		printer_pmode mode = printer_noExtraLinebreak;
		/* do not insert extra blank following a / *-style comment before the closing '}'
		 * unless the literal is empty */
		if (x->Elts.n > 0)
			mode |= printer_noExtraBlank;
		/* need the initial indent to print lone comments with the proper level of
		 * indentation */
		printer_print(p, P_indent, P_unindent, P_MODE(mode));
		printer_setPos(p, x->Rbrace);
		printer_print(p, P_TOK(token_RBRACE), P_MODE(mode));
		p->level--;
		break;
	}

	case AST_Ellipsis: {
		ast_Ellipsis *x = (ast_Ellipsis *)expr;
		printer_print(p, P_TOK(token_ELLIPSIS));
		if (x->Elt != NULL)
			printer_expr(p, x->Elt);
		break;
	}

	case AST_ArrayType: {
		ast_ArrayType *x = (ast_ArrayType *)expr;
		printer_print(p, P_TOK(token_LBRACK));
		if (x->Len != NULL)
			printer_expr(p, x->Len);
		printer_print(p, P_TOK(token_RBRACK));
		printer_expr(p, x->Elt);
		break;
	}

	case AST_StructType: {
		ast_StructType *x = (ast_StructType *)expr;
		printer_print(p, P_TOK(token_STRUCT));
		printer_fieldList(p, x->Fields, true, x->Incomplete);
		break;
	}

	case AST_FuncType:
		printer_print(p, P_TOK(token_FUNC));
		printer_signature(p, (ast_FuncType *)expr);
		break;

	case AST_InterfaceType: {
		ast_InterfaceType *x = (ast_InterfaceType *)expr;
		printer_print(p, P_TOK(token_INTERFACE));
		printer_fieldList(p, x->Methods, false, x->Incomplete);
		break;
	}

	case AST_MapType: {
		ast_MapType *x = (ast_MapType *)expr;
		printer_print(p, P_TOK(token_MAP), P_TOK(token_LBRACK));
		printer_expr(p, x->Key);
		printer_print(p, P_TOK(token_RBRACK));
		printer_expr(p, x->Value);
		break;
	}

	case AST_ChanType: {
		ast_ChanType *x = (ast_ChanType *)expr;
		switch (x->Dir) {
		case ast_SEND | ast_RECV:
			printer_print(p, P_TOK(token_CHAN));
			break;
		case ast_RECV:
			printer_print(p, P_TOK(token_ARROW), P_TOK(token_CHAN)); /* x.Arrow and x.Pos() are the same */
			break;
		case ast_SEND:
			printer_print(p, P_TOK(token_CHAN));
			printer_setPos(p, x->Arrow);
			printer_print(p, P_TOK(token_ARROW));
			break;
		}
		printer_print(p, P_blank);
		printer_expr(p, x->Value);
		break;
	}

	default:
		fail("go/printer: unreachable");
	}
}

/* normalizedNumber rewrites base prefixes and exponents of numbers to use lower-case letters
 * (0X123 to 0x123 and 1.2E3 to 1.2e3), and removes leading 0's from integer imaginary literals
 * (0765i to 765i). It leaves hexadecimal digits alone.
 *
 * normalizedNumber doesn't modify the ast.BasicLit value lit points to. If lit is not a number
 * or a number in canonical format already, lit is returned as is. Otherwise a new ast.BasicLit
 * is created. */
static ast_BasicLit *printer_normalizedNumber(ast_BasicLit *lit)
{
	if (lit->Kind != token_INT && lit->Kind != token_FLOAT && lit->Kind != token_IMAG)
		return lit; /* not a number - nothing to do */
	if (lit->Value.n < 2)
		return lit; /* only one digit (common case) - nothing to do */
	/* len(lit.Value) >= 2 */

	/* We ignore lit.Kind because for lit.Kind == token.IMAG the literal may be an integer or
	 * floating-point value, decimal or not. Instead, just consider the literal pattern. */
	GoStr x = lit->Value;
	GoStr x2 = gostr_sub(x, 0, 2);
	if (gostr_eqc(x2, "0X")) {
		x = gostr_cat(GOSTR("0x"), gostr_from(x, 2));
		/* possibly a hexadecimal float */
		int i = strings_LastIndexByte(x, 'P');
		if (i >= 0)
			x = gostr_cat(gostr_cat(gostr_sub(x, 0, i), GOSTR("p")), gostr_from(x, i + 1));
	} else if (gostr_eqc(x2, "0x")) {
		/* possibly a hexadecimal float */
		int i = strings_LastIndexByte(x, 'P');
		if (i == -1)
			return lit; /* nothing to do */
		x = gostr_cat(gostr_cat(gostr_sub(x, 0, i), GOSTR("p")), gostr_from(x, i + 1));
	} else if (gostr_eqc(x2, "0O")) {
		x = gostr_cat(GOSTR("0o"), gostr_from(x, 2));
	} else if (gostr_eqc(x2, "0o")) {
		return lit; /* nothing to do */
	} else if (gostr_eqc(x2, "0B")) {
		x = gostr_cat(GOSTR("0b"), gostr_from(x, 2));
	} else if (gostr_eqc(x2, "0b")) {
		return lit; /* nothing to do */
	} else {
		/* 0-prefix octal, decimal int, or float (possibly with 'i' suffix) */
		int i = strings_LastIndexByte(x, 'E');
		if (i >= 0) {
			x = gostr_cat(gostr_cat(gostr_sub(x, 0, i), GOSTR("e")), gostr_from(x, i + 1));
		} else if (x.p[x.n - 1] == 'i' && !strings_ContainsAny(x, GOSTR(".e"))) {
			/* remove leading 0's from integer (but not floating-point) imaginary literals */
			x = strings_TrimLeft(x, GOSTR("0_"));
			if (gostr_eqc(x, "i"))
				x = GOSTR("0i");
		}
	}

	ast_BasicLit *n = AST_NEW(BasicLit);
	n->ValuePos = lit->ValuePos;
	n->Kind = lit->Kind;
	n->Value = x;
	return n;
}

static bool printer_possibleSelectorExpr(printer_printer *p, ast_Expr *expr, int prec1, int depth)
{
	if (expr->kind == AST_SelectorExpr)
		return printer_selectorExpr(p, (ast_SelectorExpr *)expr, depth, true);
	printer_expr1(p, expr, prec1, depth);
	return false;
}

/* selectorExpr handles an *ast.SelectorExpr node and reports whether x spans multiple lines. */
static bool printer_selectorExpr(printer_printer *p, ast_SelectorExpr *x, int depth, bool isMethod)
{
	printer_expr1(p, x->X, token_HighestPrec, depth);
	printer_print(p, P_TOK(token_PERIOD));
	int line = printer_lineFor(p, ast_Pos(x->Sel));
	if (token_Position_IsValid(&p->pos) && p->pos.Line < line) {
		printer_print(p, P_indent, P_newline);
		printer_setPos(p, ast_Pos(x->Sel));
		printer_print(p, P_IDENT(x->Sel));
		if (!isMethod)
			printer_print(p, P_unindent);
		return true;
	}
	printer_setPos(p, ast_Pos(x->Sel));
	printer_print(p, P_IDENT(x->Sel));
	return false;
}

static void printer_expr0(printer_printer *p, ast_Expr *x, int depth) { printer_expr1(p, x, token_LowestPrec, depth); }

static void printer_expr(printer_printer *p, ast_Expr *x)
{
	const int depth = 1;
	printer_expr1(p, x, token_LowestPrec, depth);
}

/* ---- Statements ---- */

/* Print the statement list indented, but without a newline after the last statement. Extra
 * line breaks between statements in the source are respected but at most one empty line is
 * printed between statements. */
static void printer_stmtList(printer_printer *p, Vec list, int nindent, bool nextIsRBrace)
{
	if (nindent > 0)
		printer_print(p, P_indent);
	int line = 0;
	int i = 0;
	for (int k = 0; k < list.n; k++) {
		ast_Stmt *s = list.v[k];
		/* ignore empty statements (was issue 3466) */
		if (s->kind != AST_EmptyStmt) {
			/* nindent == 0 only for lists of switch/select case clauses; in those cases
			 * each clause is a new section */
			if (p->output.n > 0) {
				/* only print line break if we are not at the beginning of the output (i.e.,
				 * we are not printing only a partial program) */
				printer_linebreak(p, printer_lineFor(p, ast_Pos(s)), 1, printer_ignore,
						  i == 0 || nindent == 0 || printer_linesFrom(p, line) > 0);
			}
			printer_recordLine(p, &line);
			printer_stmt(p, s, nextIsRBrace && i == list.n - 1);
			/* labeled statements put labels on a separate line, but here we only care about
			 * the start line of the actual statement without label - correct line for each
			 * label */
			for (ast_Stmt *t = s; t->kind == AST_LabeledStmt; t = ((ast_LabeledStmt *)t)->Stmt)
				line++;
			i++;
		}
	}
	if (nindent > 0)
		printer_print(p, P_unindent);
}

/* block prints an *ast.BlockStmt; it always spans at least two lines. */
static void printer_block(printer_printer *p, ast_BlockStmt *b, int nindent)
{
	printer_setPos(p, b->Lbrace);
	printer_print(p, P_TOK(token_LBRACE));
	printer_stmtList(p, b->List, nindent, true);
	printer_linebreak(p, printer_lineFor(p, b->Rbrace), 1, printer_ignore, true);
	printer_setPos(p, b->Rbrace);
	printer_print(p, P_TOK(token_RBRACE));
}

static bool printer_isTypeName(ast_Expr *x)
{
	switch (x->kind) {
	case AST_Ident:
		return true;
	case AST_SelectorExpr:
		return printer_isTypeName(((ast_SelectorExpr *)x)->X);
	default:
		return false;
	}
}

static bool printer_stripParensInspect(void *ctx, ast_Node *node)
{
	bool *strip = ctx;
	if (node == NULL)
		return true;
	switch (node->kind) {
	case AST_ParenExpr:
		/* parentheses protect enclosed composite literals */
		return false;
	case AST_CompositeLit: {
		ast_Expr *t = ((ast_CompositeLit *)node)->Type;
		if (t != NULL && printer_isTypeName(t))
			*strip = false; /* do not strip parentheses */
		return false;
	}
	default:
		/* in all other cases, keep inspecting */
		return true;
	}
}

static ast_Expr *printer_stripParens(ast_Expr *x)
{
	if (x->kind == AST_ParenExpr) {
		ast_ParenExpr *px = (ast_ParenExpr *)x;
		bool strip = true;
		/* parentheses must not be stripped if there are any unparenthesized composite
		 * literals starting with a type name */
		ast_Inspect(px->X, printer_stripParensInspect, &strip);
		if (strip)
			return printer_stripParens(px->X);
	}
	return x;
}

static ast_Expr *printer_stripParensAlways(ast_Expr *x)
{
	if (x->kind == AST_ParenExpr)
		return printer_stripParensAlways(((ast_ParenExpr *)x)->X);
	return x;
}

static void printer_controlClause(printer_printer *p, bool isForStmt, ast_Stmt *init, ast_Expr *expr, ast_Stmt *post)
{
	printer_print(p, P_blank);
	bool needsBlank = false;
	if (init == NULL && post == NULL) {
		/* no semicolons required */
		if (expr != NULL) {
			printer_expr(p, printer_stripParens(expr));
			needsBlank = true;
		}
	} else {
		/* all semicolons required (they are not separators, print them explicitly) */
		if (init != NULL)
			printer_stmt(p, init, false);
		printer_print(p, P_TOK(token_SEMICOLON), P_blank);
		if (expr != NULL) {
			printer_expr(p, printer_stripParens(expr));
			needsBlank = true;
		}
		if (isForStmt) {
			printer_print(p, P_TOK(token_SEMICOLON), P_blank);
			needsBlank = false;
			if (post != NULL) {
				printer_stmt(p, post, false);
				needsBlank = true;
			}
		}
	}
	if (needsBlank)
		printer_print(p, P_blank);
}

/* indentList reports whether an expression list would look better if it were indented
 * wholesale (starting with the very first element, rather than starting at the first line
 * break). */
static bool printer_indentList(printer_printer *p, Vec list)
{
	/* Heuristic: indentList reports whether there are more than one multi- line element in
	 * the list, or if there is any element that is not starting on the same line as the
	 * previous one ends. */
	if (list.n >= 2) {
		int b = printer_lineFor(p, ast_Pos(list.v[0]));
		int e = printer_lineFor(p, ast_End(list.v[list.n - 1]));
		if (0 < b && b < e) {
			/* list spans multiple lines */
			int n = 0; /* multi-line element count */
			int line = b;
			for (int i = 0; i < list.n; i++) {
				ast_Expr *x = list.v[i];
				int xb = printer_lineFor(p, ast_Pos(x));
				int xe = printer_lineFor(p, ast_End(x));
				if (line < xb) {
					/* x is not starting on the same line as the previous one ended */
					return true;
				}
				if (xb < xe) {
					/* x is a multi-line element */
					n++;
				}
				line = xe;
			}
			return n > 1;
		}
	}
	return false;
}

static void printer_stmt(printer_printer *p, ast_Stmt *stmt, bool nextIsRBrace)
{
	printer_setPos(p, ast_Pos(stmt));

	switch (stmt->kind) {
	case AST_BadStmt:
		printer_print(p, P_STR(GOSTR("BadStmt")));
		break;

	case AST_DeclStmt:
		printer_decl(p, ((ast_DeclStmt *)stmt)->Decl);
		break;

	case AST_EmptyStmt:
		/* nothing to do */
		break;

	case AST_LabeledStmt: {
		ast_LabeledStmt *s = (ast_LabeledStmt *)stmt;
		/* a "correcting" unindent immediately following a line break is applied before the
		 * line break if there is no comment between (see writeWhitespace) */
		printer_print(p, P_unindent);
		printer_expr(p, (ast_Expr *)s->Label);
		printer_setPos(p, s->Colon);
		printer_print(p, P_TOK(token_COLON), P_indent);
		if (s->Stmt->kind == AST_EmptyStmt) {
			if (!nextIsRBrace) {
				printer_print(p, P_newline);
				printer_setPos(p, ast_Pos(s->Stmt));
				printer_print(p, P_TOK(token_SEMICOLON));
				break;
			}
		} else {
			printer_linebreak(p, printer_lineFor(p, ast_Pos(s->Stmt)), 1, printer_ignore, true);
		}
		printer_stmt(p, s->Stmt, nextIsRBrace);
		break;
	}

	case AST_ExprStmt: {
		const int depth = 1;
		printer_expr0(p, ((ast_ExprStmt *)stmt)->X, depth);
		break;
	}

	case AST_SendStmt: {
		ast_SendStmt *s = (ast_SendStmt *)stmt;
		const int depth = 1;
		printer_expr0(p, s->Chan, depth);
		printer_print(p, P_blank);
		printer_setPos(p, s->Arrow);
		printer_print(p, P_TOK(token_ARROW), P_blank);
		printer_expr0(p, s->Value, depth);
		break;
	}

	case AST_IncDecStmt: {
		ast_IncDecStmt *s = (ast_IncDecStmt *)stmt;
		const int depth = 1;
		printer_expr0(p, s->X, depth + 1);
		printer_setPos(p, s->TokPos);
		printer_print(p, P_TOK(s->Tok));
		break;
	}

	case AST_AssignStmt: {
		ast_AssignStmt *s = (ast_AssignStmt *)stmt;
		int depth = 1;
		if (s->Lhs.n > 1 && s->Rhs.n > 1)
			depth++;
		printer_exprList(p, ast_Pos(s), s->Lhs, depth, 0, s->TokPos, false);
		printer_print(p, P_blank);
		printer_setPos(p, s->TokPos);
		printer_print(p, P_TOK(s->Tok), P_blank);
		printer_exprList(p, s->TokPos, s->Rhs, depth, 0, token_NoPos, false);
		break;
	}

	case AST_GoStmt:
		printer_print(p, P_TOK(token_GO), P_blank);
		printer_expr(p, (ast_Expr *)((ast_GoStmt *)stmt)->Call);
		break;

	case AST_DeferStmt:
		printer_print(p, P_TOK(token_DEFER), P_blank);
		printer_expr(p, (ast_Expr *)((ast_DeferStmt *)stmt)->Call);
		break;

	case AST_ReturnStmt: {
		ast_ReturnStmt *s = (ast_ReturnStmt *)stmt;
		printer_print(p, P_TOK(token_RETURN));
		if (s->Results.n > 0) { /* Go: s.Results != nil */
			printer_print(p, P_blank);
			/* Use indentList heuristic to make corner cases look better (issue 1207). A more
			 * systematic approach would always indent, but this would cause significant
			 * reformatting of the code base and not necessarily lead to more nicely
			 * formatted code in general. */
			if (printer_indentList(p, s->Results)) {
				printer_print(p, P_indent);
				/* Use NoPos so that a newline never goes before the results (see issue
				 * #32854). */
				printer_exprList(p, token_NoPos, s->Results, 1, printer_noIndent, token_NoPos, false);
				printer_print(p, P_unindent);
			} else {
				printer_exprList(p, token_NoPos, s->Results, 1, 0, token_NoPos, false);
			}
		}
		break;
	}

	case AST_BranchStmt: {
		ast_BranchStmt *s = (ast_BranchStmt *)stmt;
		printer_print(p, P_TOK(s->Tok));
		if (s->Label != NULL) {
			printer_print(p, P_blank);
			printer_expr(p, (ast_Expr *)s->Label);
		}
		break;
	}

	case AST_BlockStmt:
		printer_block(p, (ast_BlockStmt *)stmt, 1);
		break;

	case AST_IfStmt: {
		ast_IfStmt *s = (ast_IfStmt *)stmt;
		printer_print(p, P_TOK(token_IF));
		printer_controlClause(p, false, s->Init, s->Cond, NULL);
		printer_block(p, s->Body, 1);
		if (s->Else != NULL) {
			printer_print(p, P_blank, P_TOK(token_ELSE), P_blank);
			switch (s->Else->kind) {
			case AST_BlockStmt:
			case AST_IfStmt:
				printer_stmt(p, s->Else, nextIsRBrace);
				break;
			default:
				/* This can only happen with an incorrectly constructed AST. Permit it but
				 * print so that it can be parsed without errors. */
				printer_print(p, P_TOK(token_LBRACE), P_indent, P_formfeed);
				printer_stmt(p, s->Else, true);
				printer_print(p, P_unindent, P_formfeed, P_TOK(token_RBRACE));
				break;
			}
		}
		break;
	}

	case AST_CaseClause: {
		ast_CaseClause *s = (ast_CaseClause *)stmt;
		if (s->List.n > 0) { /* Go: s.List != nil */
			printer_print(p, P_TOK(token_CASE), P_blank);
			printer_exprList(p, ast_Pos(s), s->List, 1, 0, s->Colon, false);
		} else {
			printer_print(p, P_TOK(token_DEFAULT));
		}
		printer_setPos(p, s->Colon);
		printer_print(p, P_TOK(token_COLON));
		printer_stmtList(p, s->Body, 1, nextIsRBrace);
		break;
	}

	case AST_SwitchStmt: {
		ast_SwitchStmt *s = (ast_SwitchStmt *)stmt;
		printer_print(p, P_TOK(token_SWITCH));
		printer_controlClause(p, false, s->Init, s->Tag, NULL);
		printer_block(p, s->Body, 0);
		break;
	}

	case AST_TypeSwitchStmt: {
		ast_TypeSwitchStmt *s = (ast_TypeSwitchStmt *)stmt;
		printer_print(p, P_TOK(token_SWITCH));
		if (s->Init != NULL) {
			printer_print(p, P_blank);
			printer_stmt(p, s->Init, false);
			printer_print(p, P_TOK(token_SEMICOLON));
		}
		printer_print(p, P_blank);
		printer_stmt(p, s->Assign, false);
		printer_print(p, P_blank);
		printer_block(p, s->Body, 0);
		break;
	}

	case AST_CommClause: {
		ast_CommClause *s = (ast_CommClause *)stmt;
		if (s->Comm != NULL) {
			printer_print(p, P_TOK(token_CASE), P_blank);
			printer_stmt(p, s->Comm, false);
		} else {
			printer_print(p, P_TOK(token_DEFAULT));
		}
		printer_setPos(p, s->Colon);
		printer_print(p, P_TOK(token_COLON));
		printer_stmtList(p, s->Body, 1, nextIsRBrace);
		break;
	}

	case AST_SelectStmt: {
		ast_SelectStmt *s = (ast_SelectStmt *)stmt;
		printer_print(p, P_TOK(token_SELECT), P_blank);
		ast_BlockStmt *body = s->Body;
		if (body->List.n == 0 && !printer_commentBefore(p, printer_posFor(p, body->Rbrace))) {
			/* print empty select statement w/o comments on one line */
			printer_setPos(p, body->Lbrace);
			printer_print(p, P_TOK(token_LBRACE));
			printer_setPos(p, body->Rbrace);
			printer_print(p, P_TOK(token_RBRACE));
		} else {
			printer_block(p, body, 0);
		}
		break;
	}

	case AST_ForStmt: {
		ast_ForStmt *s = (ast_ForStmt *)stmt;
		printer_print(p, P_TOK(token_FOR));
		printer_controlClause(p, true, s->Init, s->Cond, s->Post);
		printer_block(p, s->Body, 1);
		break;
	}

	case AST_RangeStmt: {
		ast_RangeStmt *s = (ast_RangeStmt *)stmt;
		printer_print(p, P_TOK(token_FOR), P_blank);
		if (s->Key != NULL) {
			printer_expr(p, s->Key);
			if (s->Value != NULL) {
				/* use position of value following the comma as comma position for correct
				 * comment placement */
				printer_setPos(p, ast_Pos(s->Value));
				printer_print(p, P_TOK(token_COMMA), P_blank);
				printer_expr(p, s->Value);
			}
			printer_print(p, P_blank);
			printer_setPos(p, s->TokPos);
			printer_print(p, P_TOK(s->Tok), P_blank);
		}
		printer_print(p, P_TOK(token_RANGE), P_blank);
		printer_expr(p, printer_stripParens(s->X));
		printer_print(p, P_blank);
		printer_block(p, s->Body, 1);
		break;
	}

	default:
		fail("go/printer: unreachable");
	}
}

/* ---- Declarations ---- */

/* The keepTypeColumn function determines if the type column of a series of consecutive const
 * or var declarations must be kept, or if initialization values (V) can be placed in the type
 * column (T) instead. The i'th entry in the result slice is true if the type column in spec[i]
 * must be kept.
 *
 * For example, the declaration:
 *
 *		const (
 *			foobar int = 42 // comment
 *			x          = 7  // comment
 *			foo
 *	             bar = 991
 *		)
 *
 * leads to the type/values matrix below. A run of value columns (V) can be moved into the type
 * column if there is no type for any of the values in that column (we only move entire columns
 * so that they align properly).
 *
 *		matrix        formatted     result
 *	                   matrix
 *		T  V    ->    T  V     ->   true      there is a T and so the type
 *		-  V          -  V          true      column must be kept
 *		-  -          -  -          false
 *		-  V          V  -          false     V is moved into T column */
static bool *printer_keepTypeColumn(Vec specs)
{
	bool *m = xalloc(sizeof *m * (size_t)(specs.n + 1));

#define populate(i, j, keepType)                                                                   \
	do {                                                                                       \
		if (keepType)                                                                      \
			for (int k_ = (i); k_ < (j); k_++)                                         \
				m[k_] = true;                                                      \
	} while (0)

	int i0 = -1; /* if i0 >= 0 we are in a run and i0 is the start of the run */
	bool keepType = false;
	for (int i = 0; i < specs.n; i++) {
		ast_ValueSpec *t = specs.v[i];
		if (t->kind != AST_ValueSpec)
			fail("go/printer: interface conversion: ast.Spec is not *ast.ValueSpec");
		if (t->Values.n > 0) { /* Go: t.Values != nil */
			if (i0 < 0) {
				/* start of a run of ValueSpecs with non-nil Values */
				i0 = i;
				keepType = false;
			}
		} else {
			if (i0 >= 0) {
				/* end of a run */
				populate(i0, i, keepType);
				i0 = -1;
			}
		}
		if (t->Type != NULL)
			keepType = true;
	}
	if (i0 >= 0) {
		/* end of a run */
		populate(i0, specs.n, keepType);
	}
#undef populate

	return m;
}

static void printer_valueSpec(printer_printer *p, ast_ValueSpec *s, bool keepType)
{
	printer_setComment(p, s->Doc);
	printer_identList(p, s->Names, false); /* always present */
	int extraTabs = 3;
	if (s->Type != NULL || keepType) {
		printer_print(p, P_vtab);
		extraTabs--;
	}
	if (s->Type != NULL)
		printer_expr(p, s->Type);
	if (s->Values.n > 0) { /* Go: s.Values != nil */
		printer_print(p, P_vtab, P_TOK(token_ASSIGN), P_blank);
		printer_exprList(p, token_NoPos, s->Values, 1, 0, token_NoPos, false);
		extraTabs--;
	}
	if (s->Comment != NULL) {
		for (; extraTabs > 0; extraTabs--)
			printer_print(p, P_vtab);
		printer_setComment(p, s->Comment);
	}
}

static ast_BasicLit *printer_sanitizeImportPath(ast_BasicLit *lit)
{
	/* Note: An unmodified AST generated by go/parser will already contain a backward- or
	 * double-quoted path string that does not contain any invalid characters, and most of the
	 * work here is not needed. However, a modified or generated AST may possibly contain
	 * non-canonical paths. Do the work in all cases since it's not too hard and not
	 * speed-critical. */

	/* if we don't have a proper string, be conservative and return whatever we have */
	if (lit->Kind != token_STRING)
		return lit;
	GoStr s;
	if (!strconv_Unquote(lit->Value, &s))
		return lit;

	/* if the string is an invalid path, return whatever we have
	 *
	 * spec: "Implementation restriction: A compiler may restrict ImportPaths to non-empty
	 * strings using only characters belonging to Unicode's L, M, N, P, and S general
	 * categories (the Graphic characters without spaces) and may also exclude the characters
	 * !"#$%&'()*,:;<=>?[\]^`{|} and the Unicode replacement character U+FFFD." */
	if (s.n == 0)
		return lit;
	const GoStr illegalChars = GOSTR("!\"#$%&'()*,:;<=>?[\\]^{|}`\xEF\xBF\xBD");
	for (int i = 0; i < s.n;) {
		int size;
		rune r = utf8_DecodeRuneInString(gostr_from(s, i), &size);
		if (!unicode_IsGraphic(r) || unicode_IsSpace(r) || strings_ContainsRune(illegalChars, r))
			return lit;
		i += size;
	}

	/* otherwise, return the double-quoted path */
	s = strconv_Quote(s);
	if (gostr_eq(s, lit->Value))
		return lit; /* nothing wrong with lit */
	ast_BasicLit *n = AST_NEW(BasicLit);
	n->ValuePos = lit->ValuePos;
	n->Kind = token_STRING;
	n->Value = s;
	return n;
}

static void printer_parameters(printer_printer *p, ast_FieldList *fields, printer_paramMode mode);

/* The parameter n is the number of specs in the group. If doIndent is set, multi-line
 * identifier lists in the spec are indented when the first linebreak is encountered. */
static void printer_spec(printer_printer *p, ast_Spec *spec, int n, bool doIndent)
{
	switch (spec->kind) {
	case AST_ImportSpec: {
		ast_ImportSpec *s = (ast_ImportSpec *)spec;
		printer_setComment(p, s->Doc);
		if (s->Name != NULL) {
			printer_expr(p, (ast_Expr *)s->Name);
			printer_print(p, P_blank);
		}
		printer_expr(p, (ast_Expr *)printer_sanitizeImportPath(s->Path));
		printer_setComment(p, s->Comment);
		printer_setPos(p, s->EndPos);
		break;
	}

	case AST_ValueSpec: {
		ast_ValueSpec *s = (ast_ValueSpec *)spec;
		if (n != 1)
			printer_internalError(p, "expected n = 1");
		printer_setComment(p, s->Doc);
		printer_identList(p, s->Names, doIndent); /* always present */
		if (s->Type != NULL) {
			printer_print(p, P_blank);
			printer_expr(p, s->Type);
		}
		if (s->Values.n > 0) { /* Go: s.Values != nil */
			printer_print(p, P_blank, P_TOK(token_ASSIGN), P_blank);
			printer_exprList(p, token_NoPos, s->Values, 1, 0, token_NoPos, false);
		}
		printer_setComment(p, s->Comment);
		break;
	}

	case AST_TypeSpec: {
		ast_TypeSpec *s = (ast_TypeSpec *)spec;
		printer_setComment(p, s->Doc);
		printer_expr(p, (ast_Expr *)s->Name);
		if (s->TypeParams != NULL)
			printer_parameters(p, s->TypeParams, printer_typeTParam);
		if (n == 1)
			printer_print(p, P_blank);
		else
			printer_print(p, P_vtab);
		if (token_Pos_IsValid(s->Assign))
			printer_print(p, P_TOK(token_ASSIGN), P_blank);
		printer_expr(p, s->Type);
		printer_setComment(p, s->Comment);
		break;
	}

	default:
		fail("go/printer: unreachable");
	}
}

static void printer_genDecl(printer_printer *p, ast_GenDecl *d)
{
	printer_setComment(p, d->Doc);
	printer_setPos(p, ast_Pos(d));
	printer_print(p, P_TOK(d->Tok), P_blank);

	if (token_Pos_IsValid(d->Lparen) || d->Specs.n != 1) {
		/* group of parenthesized declarations */
		printer_setPos(p, d->Lparen);
		printer_print(p, P_TOK(token_LPAREN));
		int n = d->Specs.n;
		if (n > 0) {
			printer_print(p, P_indent, P_formfeed);
			if (n > 1 && (d->Tok == token_CONST || d->Tok == token_VAR)) {
				/* two or more grouped const/var declarations: determine if the type
				 * column must be kept */
				bool *keepType = printer_keepTypeColumn(d->Specs);
				int line = 0;
				for (int i = 0; i < d->Specs.n; i++) {
					ast_Spec *s = d->Specs.v[i];
					if (i > 0)
						printer_linebreak(p, printer_lineFor(p, ast_Pos(s)), 1, printer_ignore,
								  printer_linesFrom(p, line) > 0);
					printer_recordLine(p, &line);
					printer_valueSpec(p, (ast_ValueSpec *)s, keepType[i]);
				}
			} else {
				int line = 0;
				for (int i = 0; i < d->Specs.n; i++) {
					ast_Spec *s = d->Specs.v[i];
					if (i > 0)
						printer_linebreak(p, printer_lineFor(p, ast_Pos(s)), 1, printer_ignore,
								  printer_linesFrom(p, line) > 0);
					printer_recordLine(p, &line);
					printer_spec(p, s, n, false);
				}
			}
			printer_print(p, P_unindent, P_formfeed);
		}
		printer_setPos(p, d->Rparen);
		printer_print(p, P_TOK(token_RPAREN));

	} else if (d->Specs.n > 0) {
		/* single declaration */
		printer_spec(p, d->Specs.v[0], 1, true);
	}
}

/* sizeCounter is an io.Writer which counts the number of bytes written, as well as whether a
 * newline character was seen. */
typedef struct {
	bool hasNewline;
	int size;
} printer_sizeCounter;

static void printer_sizeCounter_Write(void *self, const char *p, int n)
{
	printer_sizeCounter *c = self;
	if (!c->hasNewline) {
		for (int i = 0; i < n; i++) {
			if (p[i] == '\n' || p[i] == '\f') {
				c->hasNewline = true;
				break;
			}
		}
	}
	c->size += n;
}

static const char *printer_Config_fprint(const printer_Config *cfg, io_Writer output, token_FileSet *fset,
					 ast_Node *node, printer_nodeSizeMap *nodeSizes);

/* nodeSize determines the size of n in chars after formatting. The result is <= maxSize if the
 * node fits on one line with at most maxSize chars and the formatted output doesn't contain any
 * control chars. Otherwise, the result is > maxSize. */
static int printer_nodeSize(printer_printer *p, ast_Node *n, int maxSize)
{
	/* nodeSize invokes the printer, which may invoke nodeSize recursively. For deep composite
	 * literal nests, this can lead to an exponential algorithm. Remember previous results to
	 * prune the recursion (was issue 1628). */
	int size;
	if (printer_nodeSizes_get(p->nodeSizes, n, &size))
		return size;

	size = maxSize + 1; /* assume n doesn't fit */
	printer_nodeSizes_put(p->nodeSizes, n, size);

	/* nodeSize computation must be independent of particular style so that we always get the
	 * same decision; print in RawFormat */
	printer_Config cfg = {.Mode = printer_RawFormat};
	printer_sizeCounter counter = {0};
	if (printer_Config_fprint(&cfg, (io_Writer){printer_sizeCounter_Write, &counter}, p->fset, n, p->nodeSizes) !=
	    NULL)
		return size;
	if (counter.size <= maxSize && !counter.hasNewline) {
		/* n fits in a single line */
		size = counter.size;
		printer_nodeSizes_put(p->nodeSizes, n, size);
	}
	return size;
}

/* numLines returns the number of lines spanned by node n in the original source. */
static int printer_numLines(printer_printer *p, ast_Node *n)
{
	token_Pos from = ast_Pos(n);
	if (token_Pos_IsValid(from)) {
		token_Pos to = ast_End(n);
		if (token_Pos_IsValid(to))
			return printer_lineFor(p, to) - printer_lineFor(p, from) + 1;
	}
	return printer_infinity;
}

/* bodySize is like nodeSize but it is specialized for *ast.BlockStmt's. */
static int printer_bodySize(printer_printer *p, ast_BlockStmt *b, int maxSize)
{
	token_Pos pos1 = ast_Pos(b);
	token_Pos pos2 = b->Rbrace;
	if (token_Pos_IsValid(pos1) && token_Pos_IsValid(pos2) && printer_lineFor(p, pos1) != printer_lineFor(p, pos2)) {
		/* opening and closing brace are on different lines - don't make it a one-liner */
		return maxSize + 1;
	}
	if (b->List.n > 5) {
		/* too many statements - don't make it a one-liner */
		return maxSize + 1;
	}
	/* otherwise, estimate body size */
	int bodySize = printer_commentSizeBefore(p, printer_posFor(p, pos2));
	for (int i = 0; i < b->List.n; i++) {
		if (bodySize > maxSize)
			break; /* no need to continue */
		if (i > 0)
			bodySize += 2; /* space for a semicolon and blank */
		bodySize += printer_nodeSize(p, b->List.v[i], maxSize);
	}
	return bodySize;
}

/* funcBody prints a function body following a function header of given headerSize. If the
 * header's and block's size are "small enough" and the block is "simple enough", the block is
 * printed on the current line, without line breaks, spaced from the header by sep. Otherwise
 * the block's opening "{" is printed on the current line, followed by lines for the block's
 * statements and its closing "}". */
static void printer_funcBody(printer_printer *p, int headerSize, printer_whiteSpace sep, ast_BlockStmt *b)
{
	if (b == NULL)
		return;

	/* save/restore composite literal nesting level */
	int level = p->level;
	p->level = 0;

	const int maxSize = 100;
	if (headerSize + printer_bodySize(p, b, maxSize) <= maxSize) {
		printer_print(p, P_WS(sep));
		printer_setPos(p, b->Lbrace);
		printer_print(p, P_TOK(token_LBRACE));
		if (b->List.n > 0) {
			printer_print(p, P_blank);
			for (int i = 0; i < b->List.n; i++) {
				if (i > 0)
					printer_print(p, P_TOK(token_SEMICOLON), P_blank);
				printer_stmt(p, b->List.v[i], i == b->List.n - 1);
			}
			printer_print(p, P_blank);
		}
		printer_print(p, P_MODE(printer_noExtraLinebreak));
		printer_setPos(p, b->Rbrace);
		printer_print(p, P_TOK(token_RBRACE), P_MODE(printer_noExtraLinebreak));
		p->level = level;
		return;
	}

	if (sep != printer_ignore)
		printer_print(p, P_blank); /* always use blank */
	printer_block(p, b, 1);
	p->level = level;
}

/* distanceFrom returns the column difference between p.out (the current output position) and
 * startOutCol. If the start position is on a different line from the current position (or
 * either is unknown), the result is infinity. */
static int printer_distanceFrom(printer_printer *p, token_Pos startPos, int startOutCol)
{
	if (token_Pos_IsValid(startPos) && token_Position_IsValid(&p->pos) &&
	    printer_posFor(p, startPos).Line == p->pos.Line)
		return p->out.Column - startOutCol;
	return printer_infinity;
}

static void printer_funcDecl(printer_printer *p, ast_FuncDecl *d)
{
	printer_setComment(p, d->Doc);
	printer_setPos(p, ast_Pos(d));
	printer_print(p, P_TOK(token_FUNC), P_blank);
	/* We have to save startCol only after emitting FUNC; otherwise it can be on a different
	 * line (all whitespace preceding the FUNC is emitted only when the FUNC is emitted). */
	int startCol = p->out.Column - 5; /* len("func ") */
	if (d->Recv != NULL) {
		printer_parameters(p, d->Recv, printer_funcParam); /* method: print receiver */
		printer_print(p, P_blank);
	}
	printer_expr(p, (ast_Expr *)d->Name);
	printer_signature(p, d->Type);
	printer_funcBody(p, printer_distanceFrom(p, ast_Pos(d), startCol), printer_vtab, d->Body);
}

static void printer_decl(printer_printer *p, ast_Decl *decl)
{
	switch (decl->kind) {
	case AST_BadDecl:
		printer_setPos(p, ast_Pos(decl));
		printer_print(p, P_STR(GOSTR("BadDecl")));
		break;
	case AST_GenDecl:
		printer_genDecl(p, (ast_GenDecl *)decl);
		break;
	case AST_FuncDecl:
		printer_funcDecl(p, (ast_FuncDecl *)decl);
		break;
	default:
		fail("go/printer: unreachable");
	}
}

/* ---- Files ---- */

static token_Token printer_declToken(ast_Decl *decl)
{
	token_Token tok = token_ILLEGAL;
	switch (decl->kind) {
	case AST_GenDecl:
		tok = ((ast_GenDecl *)decl)->Tok;
		break;
	case AST_FuncDecl:
		tok = token_FUNC;
		break;
	default:
		break;
	}
	return tok;
}

static void printer_declList(printer_printer *p, Vec list)
{
	token_Token tok = token_ILLEGAL;
	for (int i = 0; i < list.n; i++) {
		ast_Decl *d = list.v[i];
		token_Token prev = tok;
		tok = printer_declToken(d);
		/* If the declaration token changed (e.g., from CONST to TYPE) or the next declaration
		 * has documentation associated with it, print an empty line between top-level
		 * declarations. (because p.linebreak is called with the position of d, which is past
		 * any documentation, the minimum requirement is satisfied even w/o the extra
		 * getDoc(d) nil-check - leave it in case the linebreak logic improves - there's
		 * already a TODO). */
		if (p->output.n > 0) {
			/* only print line break if we are not at the beginning of the output (i.e., we
			 * are not printing only a partial program) */
			int min = 1;
			if (prev != tok || printer_getDoc(d) != NULL)
				min = 2;
			/* start a new section if the next declaration is a function that spans
			 * multiple lines (see also issue #19544) */
			printer_linebreak(p, printer_lineFor(p, ast_Pos(d)), min, printer_ignore,
					  tok == token_FUNC && printer_numLines(p, d) > 1);
		}
		printer_decl(p, d);
	}
}

static void printer_file(printer_printer *p, ast_File *src)
{
	printer_setComment(p, src->Doc);
	printer_setPos(p, ast_Pos(src));
	printer_print(p, P_TOK(token_PACKAGE), P_blank);
	printer_expr(p, (ast_Expr *)src->Name);
	printer_declList(p, src->Decls);
	printer_print(p, P_newline);
}
