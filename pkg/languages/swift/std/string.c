/* Swift String and Character behavior (swift/stdlib/public/core: StringCharacterView.swift,
 * Collection.swift split, CharacterProperties.swift isNewline), with a conservative grapheme
 * cluster break (UAX #29 rules GB3 to GB9 for the characters described in swift_std.h). */
#include "swift_std.h"

static uint32_t scalar_at(Str s, size_t i, int *w)
{
	uint32_t c;
	*w = utf8_decode(s.p + i, s.n - i, &c);
	return c;
}

/* Grapheme_Cluster_Break=Control (or CR/LF): a cluster of its own (GB4, GB5). */
static bool is_control(uint32_t c)
{
	return c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0xad || c == 0x2028 || c == 0x2029;
}

/* Grapheme_Cluster_Break=Extend: joins the preceding cluster (GB9). Only blocks that are fully
 * assigned combining marks. */
static bool is_known_extend(uint32_t c)
{
	return (c >= 0x300 && c <= 0x36f) || (c >= 0x20d0 && c <= 0x20f0) || (c >= 0xfe00 && c <= 0xfe0f) ||
	       (c >= 0xfe20 && c <= 0xfe2f);
}

/* Characters that are neither Control, Extend, SpacingMark, ZWJ, Prepend, Hangul jamo nor
 * Regional_Indicator: a cluster starts at them and grows only by Extend (GB9). */
static bool is_plain(uint32_t c)
{
	static const uint32_t ranges[][2] = {
		{0x20, 0x7e},       {0xa0, 0xac},       {0xae, 0x2ff},      {0x370, 0x482},     {0x48a, 0x52f},
		{0x2000, 0x200a},   {0x2010, 0x2027},   {0x2030, 0x205f},   {0x2190, 0x23ff},   {0x2500, 0x27bf},
		{0x3000, 0x3029},   {0x3041, 0x3096},   {0x30a1, 0x30fa},   {0x3400, 0x4dbf},   {0x4e00, 0x9fff},
		{0xac00, 0xd7a3},   {0xff01, 0xff9d},   {0x1f300, 0x1f3fa}, {0x1f400, 0x1f64f}, {0x1f680, 0x1f6ff},
		{0x1f900, 0x1f9ff},
	};
	for (size_t i = 0; i < sizeof ranges / sizeof *ranges; i++)
		if (c >= ranges[i][0] && c <= ranges[i][1])
			return true;
	return false;
}

static NORETURN COLD void unsupported(Str s, const char *what)
{
	fail("unsupported by protoc-gen-uni: grapheme cluster breaking of the characters in %s %s", what,
	     quote_go(s));
}

size_t swift_character_end(Str s, size_t i, const char *what)
{
	int w;
	uint32_t c = scalar_at(s, i, &w);
	if (c == '\r')
		return i + 1 < s.n && s.p[i + 1] == '\n' ? i + 2 : i + 1;
	if (is_control(c))
		return i + (size_t)w;
	if (!is_plain(c))
		unsupported(s, what);
	size_t j = i + (size_t)w;
	while (j < s.n) {
		uint32_t d = scalar_at(s, j, &w);
		if (is_known_extend(d)) {
			j += (size_t)w;
			continue;
		}
		if (!is_plain(d) && !is_control(d))
			unsupported(s, what);
		break;
	}
	return j;
}

size_t swift_drop_first(Str s, int n, const char *what)
{
	size_t i = 0;
	for (int k = 0; k < n && i < s.n; k++)
		i = swift_character_end(s, i, what);
	return i;
}

int swift_split(Str s, char c, int max_splits, Str **out, const char *what)
{
	Vec parts = {0};
	size_t start = 0, i = 0;
	if (max_splits != 0) {
		while (i < s.n) {
			size_t e = swift_character_end(s, i, what);
			if (e == i + 1 && s.p[i] == c) {
				bool appended = i > start;
				if (appended) {
					Str *p = xalloc(sizeof *p);
					*p = (Str){s.p + start, i - start};
					vec_push(&parts, p);
				}
				start = e;
				if (appended && parts.n == max_splits)
					break;
			}
			i = e;
		}
	}
	if (start < s.n) {
		Str *p = xalloc(sizeof *p);
		*p = (Str){s.p + start, s.n - start};
		vec_push(&parts, p);
	}
	*out = xalloc(sizeof(Str) * (size_t)(parts.n ? parts.n : 1));
	for (int k = 0; k < parts.n; k++)
		(*out)[k] = *(Str *)parts.v[k];
	return parts.n;
}

/* The length of the newline Character at byte i, or 0. */
static size_t newline_at(Str s, size_t i)
{
	unsigned char c = (unsigned char)s.p[i];
	if (c == '\r')
		return i + 1 < s.n && s.p[i + 1] == '\n' ? 2 : 1;
	if (c >= '\n' && c <= '\f')
		return 1;
	if (c == 0xc2 && i + 1 < s.n && (unsigned char)s.p[i + 1] == 0x85)
		return 2;
	if (c == 0xe2 && i + 2 < s.n && (unsigned char)s.p[i + 1] == 0x80 &&
	    ((unsigned char)s.p[i + 2] == 0xa8 || (unsigned char)s.p[i + 2] == 0xa9))
		return 3;
	return 0;
}

int swift_lines(Str s, Str **out)
{
	int n = 1;
	for (size_t i = 0; i < s.n;) {
		size_t w = newline_at(s, i);
		n += w > 0;
		i += w ? w : 1;
	}
	*out = xalloc(sizeof(Str) * (size_t)n);
	int k = 0;
	size_t start = 0;
	for (size_t i = 0; i < s.n;) {
		size_t w = newline_at(s, i);
		if (w) {
			(*out)[k++] = (Str){s.p + start, i - start};
			start = i + w;
			i += w;
		} else {
			i++;
		}
	}
	(*out)[k] = (Str){s.p + start, s.n - start};
	return n;
}
