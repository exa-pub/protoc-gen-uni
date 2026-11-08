/* Quoting: Go's strconv.Quote (error messages of protoc-gen-uni itself, like the %q verb of the
 * Go implementation) and JSON.stringify (error messages and embedded descriptors of
 * @bufbuild/protobuf). */
#include "util.h"

#include "isprint.h"

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Index of the first element >= v (Go's strconv.bsearch). */
static size_t lower_bound16(const uint16_t *a, size_t n, uint16_t v)
{
	size_t i = 0, j = n;
	while (i < j) {
		size_t h = i + (j - i) / 2;
		if (a[h] < v)
			i = h + 1;
		else
			j = h;
	}
	return i;
}

static size_t lower_bound32(const uint32_t *a, size_t n, uint32_t v)
{
	size_t i = 0, j = n;
	while (i < j) {
		size_t h = i + (j - i) / 2;
		if (a[h] < v)
			i = h + 1;
		else
			j = h;
	}
	return i;
}

/* v is inside one of the [lo, hi] pairs of a. */
static bool in_ranges16(const uint16_t *a, size_t n, uint16_t v)
{
	size_t i = lower_bound16(a, n, v);
	return i < n && a[i & ~(size_t)1] <= v && v <= a[i | 1];
}

static bool in_list16(const uint16_t *a, size_t n, uint16_t v)
{
	size_t i = lower_bound16(a, n, v);
	return i < n && a[i] == v;
}

/* Ported from Go's strconv.IsPrint (Copyright 2009 The Go Authors, BSD license). */
static bool is_print(uint32_t r)
{
	if (r <= 0xff) {
		if (r >= 0x20 && r <= 0x7e)
			return true;
		if (r >= 0xa1 && r <= 0xff)
			return r != 0xad;
		return false;
	}
	if (r < 0x10000)
		return in_ranges16(isPrint16, LEN(isPrint16), (uint16_t)r) &&
		       !in_list16(isNotPrint16, LEN(isNotPrint16), (uint16_t)r);
	if (r < 0x20000)
		return in_ranges16(isPrint32lo, LEN(isPrint32lo), (uint16_t)(r - 0x10000)) &&
		       !in_list16(isNotPrint32, LEN(isNotPrint32), (uint16_t)(r - 0x10000));
	size_t i = lower_bound32(isPrint32hi, LEN(isPrint32hi), r);
	return i < LEN(isPrint32hi) && isPrint32hi[i & ~(size_t)1] <= r && r <= isPrint32hi[i | 1];
}

static void add_hex(Buf *b, const char *prefix, uint32_t v, int digits)
{
	buf_adds(b, prefix);
	for (int s = (digits - 1) * 4; s >= 0; s -= 4)
		buf_addc(b, "0123456789abcdef"[(v >> s) & 0xf]);
}

/* Ported from Go's strconv.appendQuotedWith and appendEscapedRune (Copyright 2009 The Go
 * Authors, BSD license), for strconv.Quote: quote '"', ASCIIonly and graphicOnly false. */
char *quote_go(Str s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (w == 1 && r == 0xfffd) {
			add_hex(&b, "\\x", (unsigned char)s.p[i], 2);
			i++;
			continue;
		}
		if (r == '"' || r == '\\') {
			buf_addc(&b, '\\');
			buf_addc(&b, (char)r);
		} else if (is_print(r)) {
			buf_add(&b, s.p + i, (size_t)w);
		} else {
			switch (r) {
			case '\a': buf_adds(&b, "\\a"); break;
			case '\b': buf_adds(&b, "\\b"); break;
			case '\f': buf_adds(&b, "\\f"); break;
			case '\n': buf_adds(&b, "\\n"); break;
			case '\r': buf_adds(&b, "\\r"); break;
			case '\t': buf_adds(&b, "\\t"); break;
			case '\v': buf_adds(&b, "\\v"); break;
			default:
				if (r < ' ' || r == 0x7f)
					add_hex(&b, "\\x", r, 2);
				else if (r < 0x10000)
					add_hex(&b, "\\u", r, 4);
				else
					add_hex(&b, "\\U", r, 8);
			}
		}
		i += (size_t)w;
	}
	buf_addc(&b, '"');
	return (char *)buf_str(&b);
}

char *quote_json(Str s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		switch (c) {
		case '"': buf_adds(&b, "\\\""); break;
		case '\\': buf_adds(&b, "\\\\"); break;
		case '\b': buf_adds(&b, "\\b"); break;
		case '\f': buf_adds(&b, "\\f"); break;
		case '\n': buf_adds(&b, "\\n"); break;
		case '\r': buf_adds(&b, "\\r"); break;
		case '\t': buf_adds(&b, "\\t"); break;
		default:
			if (c < 0x20)
				add_hex(&b, "\\u", c, 4);
			else
				buf_addc(&b, (char)c);
		}
	}
	buf_addc(&b, '"');
	return (char *)buf_str(&b);
}
