/* base64 (RFC 4648 standard alphabet) without padding, like Go's base64.RawStdEncoding and
 * protobuf-es base64Encode(bytes, "std_raw"). */
#include "util.h"

char *base64_raw_std(const uint8_t *p, size_t n)
{
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *out = xalloc(n / 3 * 4 + 5);
	char *o = out;
	size_t i = 0;
	for (; i + 3 <= n; i += 3) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
		*o++ = tbl[v >> 18];
		*o++ = tbl[(v >> 12) & 63];
		*o++ = tbl[(v >> 6) & 63];
		*o++ = tbl[v & 63];
	}
	if (n - i == 1) {
		uint32_t v = (uint32_t)p[i] << 16;
		*o++ = tbl[v >> 18];
		*o++ = tbl[(v >> 12) & 63];
	} else if (n - i == 2) {
		uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8;
		*o++ = tbl[v >> 18];
		*o++ = tbl[(v >> 12) & 63];
		*o++ = tbl[(v >> 6) & 63];
	}
	*o = 0;
	return out;
}
