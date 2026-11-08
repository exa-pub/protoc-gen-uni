/* Dart standard library behavior used by protocolbuffers/dart (dart_std.h). The layouts follow
 * the Dart SDK (sdk/lib/_internal/vm/lib/double.dart, convert/base64.dart, convert/json.dart,
 * convert/line_splitter.dart, core/string.dart; BSD-3-Clause, Copyright the Dart project authors)
 * over the shortest digits of pkg/num. */
#include "dart_std.h"

#include <math.h>
#include <string.h>

#include "num.h"

char *dart_double_to_string(double v)
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
	num_shortest(v, 64, &d);
	int k = d.nd, n = d.dp; /* v = 0.d x 10^n */
	if (k <= n && n <= 21) {
		buf_add(&b, d.d, (size_t)k);
		for (int i = k; i < n; i++)
			buf_addc(&b, '0');
		buf_adds(&b, ".0");
	} else if (0 < n && n <= 21) {
		buf_add(&b, d.d, (size_t)n);
		buf_addc(&b, '.');
		buf_add(&b, d.d + n, (size_t)(k - n));
	} else if (-6 < n && n <= 0) {
		buf_adds(&b, "0.");
		for (int i = 0; i < -n; i++)
			buf_addc(&b, '0');
		buf_add(&b, d.d, (size_t)k);
	} else {
		buf_addc(&b, d.d[0]);
		if (k > 1) {
			buf_addc(&b, '.');
			buf_add(&b, d.d + 1, (size_t)(k - 1));
		}
		buf_fmt(&b, "e%c%d", n - 1 < 0 ? '-' : '+', n - 1 < 0 ? 1 - n : n - 1);
	}
	return (char *)buf_str(&b);
}

char *dart_base64_encode(const uint8_t *p, size_t n)
{
	char *s = base64_raw_std(p, n);
	size_t len = strlen(s);
	if (len % 4 == 0)
		return s;
	Buf b = {0};
	buf_add(&b, s, len);
	while (b.n % 4)
		buf_addc(&b, '=');
	return (char *)buf_str(&b);
}

int dart_utf16_len(const char *s, size_t n)
{
	int len = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if ((c & 0xC0) != 0x80)
			len += c >= 0xF0 ? 2 : 1;
	}
	return len;
}

/* The UTF-16 code units of the code point at s[*i], advancing *i; units[1] is 0 for the BMP. */
static void utf16_units(const char *s, size_t n, size_t *i, uint32_t units[2])
{
	uint32_t r;
	int w = utf8_decode(s + *i, n - *i, &r);
	*i += (size_t)w;
	if (r >= 0x10000) {
		r -= 0x10000;
		units[0] = 0xD800 + (r >> 10);
		units[1] = 0xDC00 + (r & 0x3FF);
	} else {
		units[0] = r;
		units[1] = 0;
	}
}

int dart_compare(const char *a, const char *b)
{
	size_t na = strlen(a), nb = strlen(b), i = 0, j = 0;
	uint32_t ua[2] = {0}, ub[2] = {0};
	int ka = 0, kb = 0; /* pending second units */
	for (;;) {
		uint32_t x, y;
		if (ka) {
			x = ua[1];
			ka = 0;
		} else if (i < na) {
			utf16_units(a, na, &i, ua);
			x = ua[0];
			ka = ua[1] != 0;
		} else {
			x = UINT32_MAX;
		}
		if (kb) {
			y = ub[1];
			kb = 0;
		} else if (j < nb) {
			utf16_units(b, nb, &j, ub);
			y = ub[0];
			kb = ub[1] != 0;
		} else {
			y = UINT32_MAX;
		}
		if (x == UINT32_MAX && y == UINT32_MAX)
			return 0;
		if (x != y)
			return x == UINT32_MAX ? -1 : y == UINT32_MAX ? 1 : x < y ? -1 : 1;
	}
}

/* String._isWhitespace */
static bool dart_is_ws(uint32_t c)
{
	return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 ||
	       (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
	       c == 0x3000 || c == 0xFEFF;
}

/* Width of the whitespace code point ending at s[n-1], or 0. */
static size_t ws_before(const char *s, size_t n)
{
	size_t i = n - 1;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80 && n - i < 4)
		i--;
	uint32_t r;
	int w = utf8_decode(s + i, n - i, &r);
	return (size_t)w == n - i && dart_is_ws(r) ? (size_t)w : 0;
}

Str dart_trim_right(Str s)
{
	size_t w;
	while (s.n > 0 && (w = ws_before(s.p, s.n)) > 0)
		s.n -= w;
	return s;
}

Str dart_trim(Str s)
{
	while (s.n > 0) {
		uint32_t r;
		int w = utf8_decode(s.p, s.n, &r);
		if (!dart_is_ws(r))
			break;
		s.p += w;
		s.n -= (size_t)w;
	}
	return dart_trim_right(s);
}

int dart_split_lines(Str s, Str **out)
{
	int n = 0, cap = 8;
	Str *lines = xalloc((size_t)cap * sizeof *lines);
	size_t start = 0;
	for (size_t i = 0; i <= s.n; i++) {
		bool end = i == s.n;
		if (!end && s.p[i] != '\n' && s.p[i] != '\r')
			continue;
		if (end && start == s.n)
			break;
		if (n == cap) {
			lines = xrealloc(lines, (size_t)cap * sizeof *lines, (size_t)cap * 2 * sizeof *lines);
			cap *= 2;
		}
		lines[n++] = (Str){s.p + start, i - start};
		if (!end && s.p[i] == '\r' && i + 1 < s.n && s.p[i + 1] == '\n')
			i++;
		start = i + 1;
	}
	*out = lines;
	return n;
}

char *dart_json_string(Str s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		switch (c) {
		case '"': buf_adds(&b, "\\\""); break;
		case '\\': buf_adds(&b, "\\\\"); break;
		case '\b': buf_adds(&b, "\\b"); break;
		case '\t': buf_adds(&b, "\\t"); break;
		case '\n': buf_adds(&b, "\\n"); break;
		case '\f': buf_adds(&b, "\\f"); break;
		case '\r': buf_adds(&b, "\\r"); break;
		default:
			if (c < 0x20)
				buf_fmt(&b, "\\u%04x", c);
			else
				buf_addc(&b, (char)c);
		}
	}
	buf_addc(&b, '"');
	return (char *)buf_str(&b);
}

/* Utf8Decoder(allowMalformed: true).convert (convert/utf.dart): a leading byte order mark is
 * dropped; each maximal ill-formed subsequence (a lead byte and the continuation bytes valid after
 * it, or a single invalid byte) becomes U+FFFD. */
Str dart_utf8_decode_allow_malformed(Str s)
{
	if (str_has_prefix(s, STR_LIT("\xEF\xBB\xBF")))
		s.p += 3, s.n -= 3;
	const unsigned char *p = (const unsigned char *)s.p;
	size_t i = 0;
	Buf b = {0};
	bool copied = false;
	while (i < s.n) {
		unsigned c = p[i];
		size_t start = i++;
		int n = -1;
		unsigned lo = 0x80, hi = 0xBF;
		if (c < 0x80)
			n = 0;
		else if (c >= 0xC2 && c <= 0xDF)
			n = 1;
		else if (c >= 0xE0 && c <= 0xEF)
			n = 2, lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;
		else if (c >= 0xF0 && c <= 0xF4)
			n = 3, lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;
		bool valid = n >= 0;
		for (int k = 0; k < n && valid; k++, lo = 0x80, hi = 0xBF) {
			if (i >= s.n || p[i] < lo || p[i] > hi)
				valid = false;
			else
				i++;
		}
		if (valid) {
			if (copied)
				buf_add(&b, p + start, i - start);
			continue;
		}
		if (!copied) {
			buf_add(&b, p, start);
			copied = true;
		}
		buf_adds(&b, "\xEF\xBF\xBD");
	}
	return copied ? buf_view(&b) : s;
}

/* String.toLowerCase of a string of one UTF-16 code unit in the Dart VM (simple case mappings):
 * runs of code units (first, count - 1 with bit 15 set when every second one maps, delta modulo
 * 2^16), read from Dart 3.13 for every code unit above U+007F. */
static const uint16_t dart_lower_runs[][3] = {
	{0x00c0, 0x0016, 0x0020}, {0x00d8, 0x0006, 0x0020}, {0x0100, 0x802e, 0x0001},
	{0x0130, 0x0000, 0xff39}, {0x0132, 0x8004, 0x0001}, {0x0139, 0x800e, 0x0001},
	{0x014a, 0x802c, 0x0001}, {0x0178, 0x0000, 0xff87}, {0x0179, 0x8004, 0x0001},
	{0x0181, 0x0000, 0x00d2}, {0x0182, 0x8002, 0x0001}, {0x0186, 0x0000, 0x00ce},
	{0x0187, 0x0000, 0x0001}, {0x0189, 0x0001, 0x00cd}, {0x018b, 0x0000, 0x0001},
	{0x018e, 0x0000, 0x004f}, {0x018f, 0x0000, 0x00ca}, {0x0190, 0x0000, 0x00cb},
	{0x0191, 0x0000, 0x0001}, {0x0193, 0x0000, 0x00cd}, {0x0194, 0x0000, 0x00cf},
	{0x0196, 0x0000, 0x00d3}, {0x0197, 0x0000, 0x00d1}, {0x0198, 0x0000, 0x0001},
	{0x019c, 0x0000, 0x00d3}, {0x019d, 0x0000, 0x00d5}, {0x019f, 0x0000, 0x00d6},
	{0x01a0, 0x8004, 0x0001}, {0x01a6, 0x0000, 0x00da}, {0x01a7, 0x0000, 0x0001},
	{0x01a9, 0x0000, 0x00da}, {0x01ac, 0x0000, 0x0001}, {0x01ae, 0x0000, 0x00da},
	{0x01af, 0x0000, 0x0001}, {0x01b1, 0x0001, 0x00d9}, {0x01b3, 0x8002, 0x0001},
	{0x01b7, 0x0000, 0x00db}, {0x01b8, 0x0000, 0x0001}, {0x01bc, 0x0000, 0x0001},
	{0x01c4, 0x0000, 0x0002}, {0x01c5, 0x0000, 0x0001}, {0x01c7, 0x0000, 0x0002},
	{0x01c8, 0x0000, 0x0001}, {0x01ca, 0x0000, 0x0002}, {0x01cb, 0x8010, 0x0001},
	{0x01de, 0x8010, 0x0001}, {0x01f1, 0x0000, 0x0002}, {0x01f2, 0x8002, 0x0001},
	{0x01f6, 0x0000, 0xff9f}, {0x01f7, 0x0000, 0xffc8}, {0x01f8, 0x8026, 0x0001},
	{0x0220, 0x0000, 0xff7e}, {0x0222, 0x8010, 0x0001}, {0x023a, 0x0000, 0x2a2b},
	{0x023b, 0x0000, 0x0001}, {0x023d, 0x0000, 0xff5d}, {0x023e, 0x0000, 0x2a28},
	{0x0241, 0x0000, 0x0001}, {0x0243, 0x0000, 0xff3d}, {0x0244, 0x0000, 0x0045},
	{0x0245, 0x0000, 0x0047}, {0x0246, 0x8008, 0x0001}, {0x0370, 0x8002, 0x0001},
	{0x0376, 0x0000, 0x0001}, {0x0386, 0x0000, 0x0026}, {0x0388, 0x0002, 0x0025},
	{0x038c, 0x0000, 0x0040}, {0x038e, 0x0001, 0x003f}, {0x0391, 0x0010, 0x0020},
	{0x03a3, 0x0008, 0x0020}, {0x03cf, 0x0000, 0x0008}, {0x03d8, 0x8016, 0x0001},
	{0x03f4, 0x0000, 0xffc4}, {0x03f7, 0x0000, 0x0001}, {0x03f9, 0x0000, 0xfff9},
	{0x03fa, 0x0000, 0x0001}, {0x03fd, 0x0002, 0xff7e}, {0x0400, 0x000f, 0x0050},
	{0x0410, 0x001f, 0x0020}, {0x0460, 0x8020, 0x0001}, {0x048a, 0x8034, 0x0001},
	{0x04c0, 0x0000, 0x000f}, {0x04c1, 0x800c, 0x0001}, {0x04d0, 0x8052, 0x0001},
	{0x0531, 0x0025, 0x0030}, {0x10a0, 0x0025, 0x1c60}, {0x1e00, 0x8094, 0x0001},
	{0x1e9e, 0x0000, 0xe241}, {0x1ea0, 0x805e, 0x0001}, {0x1f08, 0x0007, 0xfff8},
	{0x1f18, 0x0005, 0xfff8}, {0x1f28, 0x0007, 0xfff8}, {0x1f38, 0x0007, 0xfff8},
	{0x1f48, 0x0005, 0xfff8}, {0x1f59, 0x8006, 0xfff8}, {0x1f68, 0x0007, 0xfff8},
	{0x1f88, 0x0007, 0xfff8}, {0x1f98, 0x0007, 0xfff8}, {0x1fa8, 0x0007, 0xfff8},
	{0x1fb8, 0x0001, 0xfff8}, {0x1fba, 0x0001, 0xffb6}, {0x1fbc, 0x0000, 0xfff7},
	{0x1fc8, 0x0003, 0xffaa}, {0x1fcc, 0x0000, 0xfff7}, {0x1fd8, 0x0001, 0xfff8},
	{0x1fda, 0x0001, 0xff9c}, {0x1fe8, 0x0001, 0xfff8}, {0x1fea, 0x0001, 0xff90},
	{0x1fec, 0x0000, 0xfff9}, {0x1ff8, 0x0001, 0xff80}, {0x1ffa, 0x0001, 0xff82},
	{0x1ffc, 0x0000, 0xfff7}, {0x2126, 0x0000, 0xe2a3}, {0x212a, 0x0000, 0xdf41},
	{0x212b, 0x0000, 0xdfba}, {0x2132, 0x0000, 0x001c}, {0x2160, 0x000f, 0x0010},
	{0x2183, 0x0000, 0x0001}, {0x24b6, 0x0019, 0x001a}, {0x2c00, 0x002e, 0x0030},
	{0x2c60, 0x0000, 0x0001}, {0x2c62, 0x0000, 0xd609}, {0x2c63, 0x0000, 0xf11a},
	{0x2c64, 0x0000, 0xd619}, {0x2c67, 0x8004, 0x0001}, {0x2c6d, 0x0000, 0xd5e4},
	{0x2c6e, 0x0000, 0xd603}, {0x2c6f, 0x0000, 0xd5e1}, {0x2c72, 0x0000, 0x0001},
	{0x2c75, 0x0000, 0x0001}, {0x2c80, 0x8062, 0x0001}, {0xa640, 0x801e, 0x0001},
	{0xa662, 0x800a, 0x0001}, {0xa680, 0x8016, 0x0001}, {0xa722, 0x800c, 0x0001},
	{0xa732, 0x803c, 0x0001}, {0xa779, 0x8002, 0x0001}, {0xa77d, 0x0000, 0x75fc},
	{0xa77e, 0x8008, 0x0001}, {0xa78b, 0x0000, 0x0001}, {0xff21, 0x0019, 0x0020},
};

uint32_t dart_to_lower_code_unit(uint32_t c)
{
	if (c < 0x80)
		return c >= 'A' && c <= 'Z' ? c + 32 : c;
	for (size_t i = 0; i < sizeof dart_lower_runs / sizeof *dart_lower_runs; i++) {
		uint32_t lo = dart_lower_runs[i][0], n = dart_lower_runs[i][1] & 0x7fff;
		if (c >= lo && c <= lo + n && (!(dart_lower_runs[i][1] & 0x8000) || !((c - lo) & 1)))
			return (uint16_t)(c + dart_lower_runs[i][2]);
	}
	return c;
}
