/* pkg/num: shortest digits. */
#include "num.h"
#include "test.h"

#include <float.h>

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static void test_shortest(void)
{
	static const struct {
		double v;
		int bits;
		const char *digits;
		int dp;
	} cases[] = {
		{1, 64, "1", 1},
		{0.1, 64, "1", 0},
		{123.456, 64, "123456", 3},
		{5e-324, 64, "5", -323}, /* Go/Python: 5e-324 */
		{1e23, 64, "1", 24},
		{DBL_MAX, 64, "17976931348623157", 309},
		{0.1f, 32, "1", 0},
		{FLT_MAX, 32, "34028235", 39},
		{1.4e-45f, 32, "1", -44},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		NumDecimal d;
		num_shortest(cases[i].v, cases[i].bits, &d);
		CHECK_MEM(d.d, (size_t)d.nd, cases[i].digits, strlen(cases[i].digits));
		CHECK(d.dp == cases[i].dp);
	}
}

TESTS(TEST(test_shortest))
