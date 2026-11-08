/* Number::toString (ECMA-262, 6.1.6.1.20) with radix 10. */
#include "es_std.h"
#include "num.h"

const char *js_number_to_string(double v)
{
	if (v != v)
		return "NaN";
	if (v == 0)
		return "0";
	Buf b = {0};
	if (v < 0) {
		buf_addc(&b, '-');
		v = -v;
	}
	if (v > 1.7976931348623157e308) {
		buf_adds(&b, "Infinity");
		return buf_str(&b);
	}
	NumDecimal d;
	num_shortest(v, 64, &d);
	int k = d.nd, n = d.dp;
	if (k <= n && n <= 21) {
		buf_add(&b, d.d, (size_t)k);
		for (int i = k; i < n; i++)
			buf_addc(&b, '0');
	} else if (0 < n && n <= 21) {
		buf_add(&b, d.d, (size_t)n);
		buf_addc(&b, '.');
		buf_add(&b, d.d + n, (size_t)(k - n));
	} else if (-6 < n && n <= 0) {
		buf_adds(&b, "0.");
		for (int i = n; i < 0; i++)
			buf_addc(&b, '0');
		buf_add(&b, d.d, (size_t)k);
	} else {
		buf_addc(&b, d.d[0]);
		if (k > 1) {
			buf_addc(&b, '.');
			buf_add(&b, d.d + 1, (size_t)(k - 1));
		}
		buf_fmt(&b, "e%c%d", n - 1 >= 0 ? '+' : '-', n - 1 >= 0 ? n - 1 : 1 - n);
	}
	return buf_str(&b);
}
