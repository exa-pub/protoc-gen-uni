/* Port of the literal constructors of proc-macro2 1.0.106 src/fallback.rs (Copyright David Tolnay,
 * MIT OR Apache-2.0): Literal::string (escape_utf8), byte_string, character and the suffixed
 * numbers (`format!("{}<suffix>", n)`). */
#include "proc_macro2.h"

#include "rust_std.h"

/* buf_fmt has no %u nor %X */
static void add_u64(Buf *b, uint64_t u)
{
	char t[24];
	int n = 0;
	do
		t[n++] = (char)('0' + u % 10);
	while (u /= 10);
	while (n)
		buf_addc(b, t[--n]);
}

char *pm2_string(Str s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < s.n;) {
		uint32_t c;
		i += (size_t)utf8_decode(s.p + i, s.n - i, &c);
		if (c == 0)
			buf_adds(&b, i < s.n && s.p[i] >= '0' && s.p[i] <= '7' ? "\\x00" : "\\0");
		else if (c == '\'')
			buf_addc(&b, '\'');
		else
			rust_char_escape_debug(&b, c);
	}
	buf_addc(&b, '"');
	return (char *)buf_str(&b);
}

char *pm2_byte_string(Str s)
{
	Buf b = {0};
	buf_adds(&b, "b\"");
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		switch (c) {
		case 0:
			buf_adds(&b, i + 1 < s.n && s.p[i + 1] >= '0' && s.p[i + 1] <= '7' ? "\\x00" : "\\0");
			break;
		case '\t': buf_adds(&b, "\\t"); break;
		case '\n': buf_adds(&b, "\\n"); break;
		case '\r': buf_adds(&b, "\\r"); break;
		case '"': buf_adds(&b, "\\\""); break;
		case '\\': buf_adds(&b, "\\\\"); break;
		default:
			if (c >= 0x20 && c <= 0x7e)
				buf_addc(&b, (char)c);
			else {
				buf_adds(&b, "\\x");
				buf_addc(&b, "0123456789ABCDEF"[c >> 4]);
				buf_addc(&b, "0123456789ABCDEF"[c & 15]);
			}
		}
	}
	buf_addc(&b, '"');
	return (char *)buf_str(&b);
}

char *pm2_character(uint32_t c)
{
	Buf b = {0};
	buf_addc(&b, '\'');
	if (c == '"')
		buf_addc(&b, '"');
	else
		rust_char_escape_debug(&b, c);
	buf_addc(&b, '\'');
	return (char *)buf_str(&b);
}

char *pm2_int(int64_t n, const char *suffix)
{
	Buf b = {0};
	if (n < 0)
		buf_addc(&b, '-');
	add_u64(&b, n < 0 ? 0 - (uint64_t)n : (uint64_t)n);
	buf_adds(&b, suffix);
	return (char *)buf_str(&b);
}

char *pm2_uint(uint64_t n, const char *suffix)
{
	Buf b = {0};
	add_u64(&b, n);
	buf_adds(&b, suffix);
	return (char *)buf_str(&b);
}

char *pm2_f32_suffixed(float v)
{
	Buf b = {0};
	rust_float_display(&b, v, 32);
	buf_adds(&b, "f32");
	return (char *)buf_str(&b);
}

char *pm2_f64_suffixed(double v)
{
	Buf b = {0};
	rust_float_display(&b, v, 64);
	buf_adds(&b, "f64");
	return (char *)buf_str(&b);
}
