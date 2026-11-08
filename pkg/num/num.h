/* Exact shortest round-trip digits, the numeric algorithm shared by the number printing of
 * language runtimes.
 *
 * num_shortest computes the digits every modern runtime starts from (Go strconv 'e' -1, Python
 * repr, Java Double.toString, JavaScript Number#toString, Rust Display, ...); the layout of one
 * runtime belongs to its standard library port (pkg/languages/<language>/std, e.g. Java's in
 * pkg/languages/java/std). */
#ifndef NUM_H
#define NUM_H

#include "util.h"

/* A decimal 0.d[0]d[1]...d[nd-1] x 10^dp with ASCII digits and no trailing zeros (nd == 0 for
 * zero). */
typedef struct {
	char d[32];
	int nd, dp;
} NumDecimal;

/* The shortest decimal that rounds to v (as a binary64, or as a binary32 when bits == 32) under
 * round-to-nearest-even; of several, the one closest to v (ties: even last digit). Uses the
 * absolute value of v, which must be finite. */
void num_shortest(double v, int bits, NumDecimal *out);

/* Like num_shortest, but with Java's (JDK 19+) rule for one-digit results: when a decimal of
 * two digits that is closer to v also rounds to v, that one is used (4.9E-324, not 5E-324). */
void num_shortest_java(double v, int bits, NumDecimal *out);

#endif
