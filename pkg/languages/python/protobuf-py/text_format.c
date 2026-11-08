/* Copyright (c) 2025-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* _c_unescape of _wire/_text_format.py (protobuf-py v0.7.0, copied from protobuf's
 * python/google/protobuf/text_encoding.py) with the CPython 3.13 codecs it goes through:
 *
 *   result = _CUNESCAPE_HEX.sub(replace_hex, text)      # \xF -> \x0F (odd backslashes)
 *   result = _CUNESCAPE_OCTAL.sub(replace_octal, result) # \NNN -> \xHH (odd backslashes)
 *   result = result.encode("raw_unicode_escape").decode("raw_unicode_escape")
 *   result = result.encode("utf-8").decode("unicode_escape")
 *   return result.encode("latin-1")
 *
 * Strings are arrays of code points here. */
#include "protobuf_py.h"

#include <string.h>

typedef struct {
	uint32_t *v;
	size_t n, cap;
} Cps;

static void push(Cps *s, uint32_t c)
{
	if (s->n == s->cap) {
		size_t cap = s->cap ? s->cap * 2 : 64;
		s->v = xrealloc(s->v, sizeof *s->v * s->cap, sizeof *s->v * cap);
		s->cap = cap;
	}
	s->v[s->n++] = c;
}

static bool is_hex(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int hex_val(uint32_t c) { return c <= '9' ? (int)c - '0' : (int)(c | 0x20) - 'a' + 10; }
static bool is_oct(uint32_t c) { return c >= '0' && c <= '7'; }

static uint32_t at(const Cps *s, size_t i) { return i < s->n ? s->v[i] : 0x110000; }

/* re.sub of (\\+)x([0-9a-fA-F])(?![0-9a-fA-F]) (hex) or (\\+)([0-7]{1,3})(?![0-7]) (octal): a
 * match starts at a run of backslashes (backtracking into the run cannot help: a backslash would
 * follow). */
static Cps cunescape_regex(const Cps *in, bool hex)
{
	Cps out = {0};
	for (size_t i = 0; i < in->n;) {
		if (in->v[i] != '\\') {
			push(&out, in->v[i++]);
			continue;
		}
		size_t j = i;
		while (j < in->n && in->v[j] == '\\')
			j++;
		size_t run = j - i, end = 0;
		if (hex) {
			if (at(in, j) == 'x' && is_hex(at(in, j + 1)) && !is_hex(at(in, j + 2)))
				end = j + 2;
		} else {
			size_t k = j;
			while (k < in->n && is_oct(in->v[k]))
				k++;
			if (k > j && k - j <= 3)
				end = k;
		}
		if (!end) {
			for (; i < j; i++)
				push(&out, '\\');
			continue;
		}
		for (size_t k = 0; k < run; k++)
			push(&out, '\\');
		if (!(run & 1)) {
			for (size_t k = j; k < end; k++)
				push(&out, in->v[k]);
		} else if (hex) {
			push(&out, 'x');
			push(&out, '0');
			push(&out, in->v[j + 1]);
		} else {
			int v = 0;
			for (size_t k = j; k < end; k++)
				v = v * 8 + (int)(in->v[k] - '0');
			char t[8];
			int n = 0;
			do
				t[n++] = "0123456789abcdef"[v % 16];
			while (v /= 16);
			if (n < 2)
				t[n++] = '0';
			push(&out, 'x');
			while (n)
				push(&out, (uint32_t)t[--n]);
		}
		i = end;
	}
	return out;
}

/* The error of codec on object at [start, end). */
static NORETURN void decode_error(const char *codec, Str object, size_t start, size_t end, const char *msg)
{
	fail("%s", py_unicode_decode_error(codec, object, start, end, msg));
}

/* .encode("raw_unicode_escape").decode("raw_unicode_escape"): the encoder writes a code point
 * above U+00FF as \uXXXX or \UXXXXXXXX; the decoder reads \u and \U after an odd number of
 * backslashes. */
static Cps raw_unicode_escape_roundtrip(const Cps *in)
{
	Buf enc = {0};
	for (size_t i = 0; i < in->n; i++) {
		uint32_t c = in->v[i];
		if (c < 0x100) {
			buf_addc(&enc, (char)c);
			continue;
		}
		int nd = c <= 0xffff ? 4 : 8;
		buf_addc(&enc, '\\');
		buf_addc(&enc, nd == 4 ? 'u' : 'U');
		for (int k = nd - 1; k >= 0; k--)
			buf_addc(&enc, "0123456789abcdef"[(c >> (4 * k)) & 15]);
	}
	Str bytes = buf_view(&enc);
	const unsigned char *b = (const unsigned char *)bytes.p;
	Cps out = {0};
	for (size_t i = 0; i < bytes.n;) {
		if (b[i] != '\\') {
			push(&out, b[i++]);
			continue;
		}
		size_t j = i;
		while (j < bytes.n && b[j] == '\\')
			push(&out, b[j++]);
		size_t run = j - i;
		i = j;
		if (!(run & 1) || j >= bytes.n || (b[j] != 'u' && b[j] != 'U'))
			continue;
		out.n--;
		size_t start = j - 1; /* the backslash */
		bool small = b[j] == 'u';
		int count = small ? 4 : 8;
		j++;
		uint32_t ch = 0;
		for (; count; j++, count--) {
			if (j >= bytes.n || !is_hex(b[j]))
				decode_error("rawunicodeescape", bytes, start, j,
					small ? "truncated \\uXXXX escape" : "truncated \\UXXXXXXXX escape");
			ch = ch << 4 | (uint32_t)hex_val(b[j]);
		}
		if (ch > 0x10ffff)
			decode_error("rawunicodeescape", bytes, start, j, "\\Uxxxxxxxx out of range");
		push(&out, ch);
		i = j;
	}
	return out;
}

/* .encode("utf-8").decode("unicode_escape") */
static Cps unicode_escape_roundtrip(const Cps *in)
{
	Buf utf8 = {0};
	for (size_t i = 0; i < in->n; i++) {
		uint32_t c = in->v[i];
		char t[4];
		int n;
		if (c < 0x80) {
			t[0] = (char)c, n = 1;
		} else if (c < 0x800) {
			t[0] = (char)(0xc0 | c >> 6), t[1] = (char)(0x80 | (c & 0x3f)), n = 2;
		} else if (c < 0x10000) {
			if (c >= 0xd800 && c <= 0xdfff) {
				size_t end = i + 1;
				while (end < in->n && in->v[end] >= 0xd800 && in->v[end] <= 0xdfff)
					end++;
				fail("%s", py_unicode_encode_error("utf-8", in->v, in->n, i, end, "surrogates not allowed"));
			}
			t[0] = (char)(0xe0 | c >> 12), t[1] = (char)(0x80 | ((c >> 6) & 0x3f)), t[2] = (char)(0x80 | (c & 0x3f)), n = 3;
		} else {
			t[0] = (char)(0xf0 | c >> 18), t[1] = (char)(0x80 | ((c >> 12) & 0x3f)), t[2] = (char)(0x80 | ((c >> 6) & 0x3f));
			t[3] = (char)(0x80 | (c & 0x3f)), n = 4;
		}
		buf_add(&utf8, t, (size_t)n);
	}
	Str bytes = buf_view(&utf8);
	const unsigned char *s = (const unsigned char *)bytes.p;
	size_t n = bytes.n;
	Cps out = {0};
	for (size_t i = 0; i < n;) {
		if (s[i] != '\\') {
			push(&out, s[i++]);
			continue;
		}
		size_t start = i; /* the backslash */
		if (++i >= n)
			decode_error("unicodeescape", bytes, start, n, "\\ at end of string");
		unsigned char c = s[i++];
		switch (c) {
		case '\n': break;
		case '\\': case '\'': case '"': push(&out, c); break;
		case 'b': push(&out, '\b'); break;
		case 'f': push(&out, '\f'); break;
		case 't': push(&out, '\t'); break;
		case 'n': push(&out, '\n'); break;
		case 'r': push(&out, '\r'); break;
		case 'v': push(&out, '\v'); break;
		case 'a': push(&out, '\a'); break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
			uint32_t v = c - '0';
			for (int k = 0; k < 2 && i < n && s[i] >= '0' && s[i] <= '7'; k++)
				v = v * 8 + (uint32_t)(s[i++] - '0');
			push(&out, v);
			break;
		}
		case 'x': case 'u': case 'U': {
			int count = c == 'x' ? 2 : c == 'u' ? 4 : 8;
			uint32_t v = 0;
			for (int k = 0; k < count; k++, i++) {
				if (i >= n || !is_hex(s[i]))
					decode_error("unicodeescape", bytes, start, i,
						c == 'x' ? "truncated \\xXX escape" : c == 'u' ? "truncated \\uXXXX escape" : "truncated \\UXXXXXXXX escape");
				v = v << 4 | (uint32_t)hex_val(s[i]);
			}
			if (v > 0x10ffff)
				decode_error("unicodeescape", bytes, start, i, "illegal Unicode character");
			push(&out, v);
			break;
		}
		case 'N':
			fail("unsupported by protoc-gen-uni: a \\N{name} escape in a bytes default value");
		default:
			push(&out, '\\');
			push(&out, c);
		}
	}
	return out;
}

COLD Str pbpy_c_unescape(Str text)
{
	Cps cps = {0};
	for (size_t i = 0; i < text.n;) {
		uint32_t r;
		i += (size_t)utf8_decode(text.p + i, text.n - i, &r);
		push(&cps, r);
	}
	Cps a = cunescape_regex(&cps, true);
	Cps b = cunescape_regex(&a, false);
	Cps c = raw_unicode_escape_roundtrip(&b);
	Cps d = unicode_escape_roundtrip(&c);
	char *out = xalloc(d.n + 1);
	for (size_t i = 0; i < d.n; i++) {
		if (d.v[i] > 0xff) {
			size_t end = i + 1;
			while (end < d.n && d.v[end] > 0xff)
				end++;
			fail("%s", py_unicode_encode_error("latin-1", d.v, d.n, i, end, "ordinal not in range(256)"));
		}
		out[i] = (char)d.v[i];
	}
	return (Str){out, d.n};
}
