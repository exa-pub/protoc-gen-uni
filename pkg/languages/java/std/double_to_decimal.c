/* Java Double.toString and Float.toString as of JDK 19 (jdk.internal.math.DoubleToDecimal and
 * FloatToDecimal, Raffaello Giulietti's algorithm): the shortest decimal (num_shortest_java),
 * plain for 10^-3 <= |v| < 10^7 ("0.001", "1234567.0"), else computerized scientific
 * ("1.0E7", "1.234E-5"); at least one digit after the point. */
#include "java_std.h"

#include <math.h>

static char *java_format(double v, int bits)
{
	if (isnan(v))
		return xstrdup("NaN");
	Buf b = {0};
	if (signbit(v))
		buf_addc(&b, '-');
	if (isinf(v)) {
		buf_adds(&b, "Infinity");
		return (char *)buf_str(&b);
	}
	if (v == 0) {
		buf_adds(&b, "0.0");
		return (char *)buf_str(&b);
	}
	NumDecimal d;
	num_shortest_java(v, bits, &d);
	int e = d.dp; /* v = 0.d x 10^e */
	if (0 < e && e <= 7) {
		for (int i = 0; i < e; i++)
			buf_addc(&b, i < d.nd ? d.d[i] : '0');
		buf_addc(&b, '.');
		if (d.nd > e)
			buf_add(&b, d.d + e, (size_t)(d.nd - e));
		else
			buf_addc(&b, '0');
	} else if (-3 < e && e <= 0) {
		buf_adds(&b, "0.");
		for (int i = 0; i < -e; i++)
			buf_addc(&b, '0');
		buf_add(&b, d.d, (size_t)d.nd);
	} else {
		buf_addc(&b, d.d[0]);
		buf_addc(&b, '.');
		if (d.nd > 1)
			buf_add(&b, d.d + 1, (size_t)(d.nd - 1));
		else
			buf_addc(&b, '0');
		buf_fmt(&b, "E%d", e - 1);
	}
	return (char *)buf_str(&b);
}

char *java_double_to_string(double v) { return java_format(v, 64); }

char *java_float_to_string(float v) { return java_format((double)v, 32); }
