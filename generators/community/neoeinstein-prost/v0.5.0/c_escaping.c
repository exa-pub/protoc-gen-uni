/* Port of src/code_generator/c_escaping.rs of prost-build 0.14.1
 * (https://github.com/tokio-rs/prost, Apache-2.0): unescape_c_escape_string, based on
 * google::protobuf::UnescapeCEscapeString. Errors are panics upstream. */

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* u8::from_str_radix(s[0:2], 16): an optional '+' sign, then hex digits. */
static int parse_hex_u8(const char *s)
{
	if (s[0] == '+')
		return hex_digit(s[1]);
	int hi = hex_digit(s[0]), lo = hex_digit(s[1]);
	return hi < 0 || lo < 0 ? -1 : hi * 16 + lo;
}

static void unescape_c_escape_string(Str s, Buf *dst)
{
	const char *src = s.p;
	size_t len = s.n, p = 0;
	while (p < len) {
		if (src[p] != '\\') {
			buf_addc(dst, src[p++]);
			continue;
		}
		p++;
		if (p == len)
			fail("invalid c-escaped default binary value (%s): ends with '\\'", s.p);
		char c = src[p];
		static const char simple[] = "abfnrtv\\?'\"";
		static const char value[] = "\a\b\f\n\r\t\v\\?'\"";
		const char *k = c ? strchr(simple, c) : NULL;
		if (k) {
			buf_addc(dst, value[k - simple]);
			p++;
		} else if (c >= '0' && c <= '7') {
			unsigned octal = 0; /* u8 arithmetic: wraps in release builds */
			for (int i = 0; i < 3 && p < len && src[p] >= '0' && src[p] <= '7'; i++)
				octal = (octal * 8 + (unsigned)(src[p++] - '0')) & 0xff;
			buf_addc(dst, (char)octal);
		} else if (c == 'x' || c == 'X') {
			if (p + 3 > len)
				fail("invalid c-escaped default binary value (%s): incomplete hex value", s.p);
			/* &s[p + 1..p + 3] panics unless both ends are char boundaries */
			if (((unsigned char)src[p + 1] & 0xc0) == 0x80 ||
			    (p + 3 < len && ((unsigned char)src[p + 3] & 0xc0) == 0x80))
				fail("byte index is not a char boundary");
			int b = parse_hex_u8(src + p + 1);
			if (b < 0)
				fail("invalid c-escaped default binary value (%s): invalid hex value", xstrndup(src + p, 2));
			buf_addc(dst, (char)b);
			p += 3;
		} else {
			fail("invalid c-escaped default binary value (%s): invalid escape", s.p);
		}
	}
}
