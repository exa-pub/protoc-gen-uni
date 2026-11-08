/* Copyright 2010 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* encoding/json of Go 1.27 with GOEXPERIMENT=jsonv2 (the default): the v1 API (v2_*.go) on the
 * v2 engine (encoding/json/v2, jsontext, internal/jsonwire), with the v1 default options
 * (jsonflags.DefaultV1Flags: AllowDuplicateNames, AllowInvalidUTF8, EscapeForHTML, EscapeForJS,
 * PreserveRawStrings, ...). Ported are the pieces a generator needs to marshal its own values
 * the way the struct and map arshalers do (it writes the members itself):
 *
 *   - jsonwire.AppendQuote and AppendFloat (internal/jsonwire/encode.go);
 *   - Encoder.WriteValue of a MarshalJSON result (jsontext/encode.go reformatValue): validation
 *     and compaction, preserving raw strings but escaping <, >, & and U+2028, U+2029;
 *   - Indent (v2_indent.go: jsontext.AppendFormat with Multiline) of valid JSON;
 *   - Unmarshal into an `any` (v2/arshal_any.go): null, bool, float64, string, []any,
 *     map[string]any.
 *
 * Syntax errors are reported without their text (callers report invalid input themselves). */

#include <stdlib.h>

/* internal/jsonwire/encode.go: escapeASCII */
static const uint8_t json_escapeASCII[128] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* escape control characters */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* escape control characters */
	0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* escape '"' and '&' */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, /* escape '<' and '>' */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, /* escape '\\' */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static void json_appendEscapedUTF16(Buf *b, uint16_t x)
{
	static const char hex[] = "0123456789abcdef";
	char s[6] = {'\\', 'u', hex[(x >> 12) & 0xf], hex[(x >> 8) & 0xf], hex[(x >> 4) & 0xf], hex[x & 0xf]};
	buf_add(b, s, 6);
}

static void json_appendEscapedASCII(Buf *b, uint8_t c)
{
	switch (c) {
	case '"':
	case '\\':
		buf_addc(b, '\\');
		buf_addc(b, (char)c);
		break;
	case '\b':
		buf_adds(b, "\\b");
		break;
	case '\f':
		buf_adds(b, "\\f");
		break;
	case '\n':
		buf_adds(b, "\\n");
		break;
	case '\r':
		buf_adds(b, "\\r");
		break;
	case '\t':
		buf_adds(b, "\\t");
		break;
	default:
		json_appendEscapedUTF16(b, c);
	}
}

/* AppendQuote with EscapeForHTML and EscapeForJS; invalid UTF-8 becomes U+FFFD (AllowInvalidUTF8). */
void go_std_json_quote(Buf *b, Str s)
{
	buf_addc(b, '"');
	size_t i = 0, n = 0;
	while (n < s.n) {
		uint8_t c = (uint8_t)s.p[n];
		if (c < 0x80) {
			n++;
			if (!json_escapeASCII[c])
				continue;
			buf_add(b, s.p + i, n - 1 - i);
			json_appendEscapedASCII(b, c);
			i = n;
		} else {
			uint32_t r;
			int rn = utf8_decode(s.p + n, s.n - n, &r);
			n += (size_t)rn;
			if (r != 0xFFFD && r != 0x2028 && r != 0x2029)
				continue;
			if (r == 0xFFFD && rn == 1) {
				buf_add(b, s.p + i, n - (size_t)rn - i);
				buf_adds(b, "\xef\xbf\xbd");
				i = n;
			} else if (r == 0x2028 || r == 0x2029) {
				buf_add(b, s.p + i, n - (size_t)rn - i);
				json_appendEscapedUTF16(b, (uint16_t)r);
				i = n;
			}
		}
	}
	buf_add(b, s.p + i, n - i);
	buf_addc(b, '"');
}

/* AppendFloat: like the ES6 number-to-string conversion (finite values only). */
void go_std_json_float(Buf *b, double f, int bits)
{
	if (bits == 32)
		f = (double)(float)f;
	double abs = f < 0 ? -f : f;
	char fmt = 'f';
	if (abs != 0) {
		if ((bits == 64 && (abs < 1e-6 || abs >= 1e21)) || (bits == 32 && ((float)abs < 1e-6f || (float)abs >= 1e21f)))
			fmt = 'e';
	}
	size_t start = b->n;
	strconv_AppendFloat(b, f, fmt, -1, bits);
	if (fmt == 'e') {
		/* Clean up e-09 to e-9. */
		size_t n = b->n - start;
		char *d = b->p + start;
		if (n >= 4 && d[n - 4] == 'e' && d[n - 3] == '-' && d[n - 2] == '0') {
			d[n - 2] = d[n - 1];
			b->n--;
			b->p[b->n] = 0;
		}
	}
}

/* ---- scanning (jsonwire Consume*) ---- */

enum { json_maxNestingDepth = 10000 };
/* The decoder recurses on the C stack: Go's limit (10000) is not supported. */
enum { json_maxDecodeDepth = 1000 };

static size_t json_ws(Str s, size_t i)
{
	while (i < s.n && (s.p[i] == ' ' || s.p[i] == '\t' || s.p[i] == '\r' || s.p[i] == '\n'))
		i++;
	return i;
}

static bool json_isdigit(char c) { return c >= '0' && c <= '9'; }

/* ConsumeNumber: the length of the number at s[i:], 0 if invalid. */
static size_t json_number(Str s, size_t i)
{
	size_t j = i;
	if (j < s.n && s.p[j] == '-')
		j++;
	if (j < s.n && s.p[j] == '0')
		j++;
	else if (j < s.n && s.p[j] >= '1' && s.p[j] <= '9')
		while (j < s.n && json_isdigit(s.p[j]))
			j++;
	else
		return 0;
	if (j < s.n && s.p[j] == '.') {
		j++;
		if (j >= s.n || !json_isdigit(s.p[j]))
			return 0;
		while (j < s.n && json_isdigit(s.p[j]))
			j++;
	}
	if (j < s.n && (s.p[j] == 'e' || s.p[j] == 'E')) {
		j++;
		if (j < s.n && (s.p[j] == '+' || s.p[j] == '-'))
			j++;
		if (j >= s.n || !json_isdigit(s.p[j]))
			return 0;
		while (j < s.n && json_isdigit(s.p[j]))
			j++;
	}
	return j - i;
}

static int json_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* ConsumeString with validateUTF8 false: the length of the string at s[i:] (quotes included),
 * 0 if invalid. */
static size_t json_string(Str s, size_t i)
{
	size_t j = i + 1;
	while (j < s.n) {
		uint8_t c = (uint8_t)s.p[j];
		if (c == '"')
			return j + 1 - i;
		if (c < 0x20)
			return 0;
		if (c != '\\') {
			j++;
			continue;
		}
		if (j + 1 >= s.n)
			return 0;
		switch (s.p[j + 1]) {
		case '"':
		case '\\':
		case '/':
		case 'b':
		case 'f':
		case 'n':
		case 'r':
		case 't':
			j += 2;
			break;
		case 'u':
			if (j + 6 > s.n)
				return 0;
			for (int k = 2; k < 6; k++)
				if (json_hex(s.p[j + k]) < 0)
					return 0;
			j += 6;
			break;
		default:
			return 0;
		}
	}
	return 0;
}

/* The string at s[i:i+n] reformatted under PreserveRawStrings with EscapeForHTML and EscapeForJS. */
static void json_reformat_string(Buf *b, Str s, size_t i, size_t n)
{
	size_t last = i, k = i, end = i + n;
	bool simple = true;
	for (size_t j = i + 1; j + 1 < end; j++)
		if ((uint8_t)s.p[j] >= 0x80 || json_escapeASCII[(uint8_t)s.p[j]]) {
			simple = false;
			break;
		}
	if (simple) {
		buf_add(b, s.p + i, n);
		return;
	}
	while (k < end) {
		uint8_t c = (uint8_t)s.p[k];
		if (c < 0x80) {
			if (c == '<' || c == '>' || c == '&') {
				buf_add(b, s.p + last, k - last);
				json_appendEscapedASCII(b, c);
				last = k + 1;
			}
			k++;
		} else {
			uint32_t r;
			int rn = utf8_decode(s.p + k, end - k, &r);
			if (r == 0x2028 || r == 0x2029) {
				buf_add(b, s.p + last, k - last);
				json_appendEscapedUTF16(b, (uint16_t)r);
				last = k + (size_t)rn;
			}
			k += (size_t)rn;
		}
	}
	buf_add(b, s.p + last, end - last);
}

/* Encoder.WriteValue (reformatValue) without whitespace: appends the value to b; false if raw
 * is not one valid JSON value (b is then left as it was). */
bool go_std_json_write_raw(Buf *b, Str raw)
{
	size_t start = b->n;
	/* the stack of open containers: '{' or '[' and whether an element was written */
	char *stack = NULL;
	size_t depth = 0, cap = 0;
	size_t i = json_ws(raw, 0);
	bool expect_value = true; /* else: expecting ',' or the end of the container */
	for (;;) {
		if (expect_value) {
			if (i >= raw.n)
				goto bad;
			char c = raw.p[i];
			size_t n;
			if (c == 'n' || c == 't' || c == 'f') {
				const char *lit = c == 'n' ? "null" : c == 't' ? "true" : "false";
				n = strlen(lit);
				if (raw.n - i < n || memcmp(raw.p + i, lit, n) != 0)
					goto bad;
				buf_add(b, lit, n);
				i += n;
			} else if (c == '"') {
				if (!(n = json_string(raw, i)))
					goto bad;
				json_reformat_string(b, raw, i, n);
				i += n;
			} else if (c == '-' || json_isdigit(c)) {
				if (!(n = json_number(raw, i)))
					goto bad;
				buf_add(b, raw.p + i, n);
				i += n;
			} else if (c == '{' || c == '[') {
				if (depth == json_maxNestingDepth)
					goto bad;
				if (depth == cap) {
					stack = xrealloc(stack, cap, cap * 2 + 16);
					cap = cap * 2 + 16;
				}
				stack[depth++] = c;
				buf_addc(b, c);
				i = json_ws(raw, i + 1);
				char close = c == '{' ? '}' : ']';
				if (i < raw.n && raw.p[i] == close) {
					buf_addc(b, close);
					depth--;
					i++;
				} else if (c == '{') {
					/* the first name */
					if (i >= raw.n || raw.p[i] != '"' || !(n = json_string(raw, i)))
						goto bad;
					json_reformat_string(b, raw, i, n);
					i = json_ws(raw, i + n);
					if (i >= raw.n || raw.p[i] != ':')
						goto bad;
					buf_addc(b, ':');
					i = json_ws(raw, i + 1);
					continue;
				} else {
					continue;
				}
			} else {
				goto bad;
			}
			expect_value = false;
		}
		i = json_ws(raw, i);
		if (depth == 0)
			break;
		if (i >= raw.n)
			goto bad;
		char open = stack[depth - 1];
		if (raw.p[i] == (open == '{' ? '}' : ']')) {
			buf_addc(b, raw.p[i]);
			depth--;
			i++;
			continue;
		}
		if (raw.p[i] != ',')
			goto bad;
		buf_addc(b, ',');
		i = json_ws(raw, i + 1);
		if (open == '{') {
			size_t n;
			if (i >= raw.n || raw.p[i] != '"' || !(n = json_string(raw, i)))
				goto bad;
			json_reformat_string(b, raw, i, n);
			i = json_ws(raw, i + n);
			if (i >= raw.n || raw.p[i] != ':')
				goto bad;
			buf_addc(b, ':');
			i = json_ws(raw, i + 1);
		}
		expect_value = true;
	}
	if (i != raw.n)
		goto bad;
	return true;
bad:
	b->n = start;
	if (b->p)
		b->p[start] = 0;
	return false;
}

/* Indent (jsontext.AppendFormat with Multiline, PreserveRawStrings) of valid compact JSON, such
 * as the output of the functions above. */
void go_std_json_indent(Buf *dst, Str src, const char *prefix, const char *indent)
{
	size_t depth = 0;
	for (size_t i = 0; i < src.n;) {
		char c = src.p[i];
		if (c == '"') {
			size_t n = json_string(src, i);
			if (!n)
				n = src.n - i;
			buf_add(dst, src.p + i, n);
			i += n;
			continue;
		}
		if (c == '{' || c == '[') {
			char close = c == '{' ? '}' : ']';
			if (i + 1 < src.n && src.p[i + 1] == close) {
				buf_addc(dst, c);
				buf_addc(dst, close);
				i += 2;
				continue;
			}
			buf_addc(dst, c);
			depth++;
		} else if (c == '}' || c == ']') {
			depth--;
		} else if (c == ',') {
			buf_addc(dst, ',');
		} else if (c == ':') {
			buf_adds(dst, ": ");
			i++;
			continue;
		} else {
			buf_addc(dst, c);
			i++;
			continue;
		}
		buf_addc(dst, '\n');
		buf_adds(dst, prefix);
		for (size_t k = 0; k < depth; k++)
			buf_adds(dst, indent);
		if (c == '}' || c == ']')
			buf_addc(dst, c);
		i++;
	}
}

/* ---- Unmarshal into any ---- */

static Str json_unquote(Str s, size_t i, size_t n)
{
	Buf b = {0};
	size_t end = i + n - 1;
	for (size_t k = i + 1; k < end;) {
		uint8_t c = (uint8_t)s.p[k];
		if (c == '\\') {
			char e = s.p[k + 1];
			k += 2;
			switch (e) {
			case 'b':
				buf_addc(&b, '\b');
				break;
			case 'f':
				buf_addc(&b, '\f');
				break;
			case 'n':
				buf_addc(&b, '\n');
				break;
			case 'r':
				buf_addc(&b, '\r');
				break;
			case 't':
				buf_addc(&b, '\t');
				break;
			case 'u': {
				uint32_t r = 0;
				for (int j = 0; j < 4; j++)
					r = r << 4 | (uint32_t)json_hex(s.p[k + j]);
				k += 4;
				/* a surrogate pair, else U+FFFD for a lone surrogate */
				if (r >= 0xD800 && r < 0xDC00 && k + 6 <= end && s.p[k] == '\\' && s.p[k + 1] == 'u') {
					uint32_t r2 = 0;
					for (int j = 0; j < 4; j++)
						r2 = r2 << 4 | (uint32_t)json_hex(s.p[k + 2 + j]);
					if (r2 >= 0xDC00 && r2 < 0xE000) {
						r = 0x10000 + ((r - 0xD800) << 10) + (r2 - 0xDC00);
						k += 6;
					}
				}
				if (r >= 0xD800 && r < 0xE000)
					r = 0xFFFD;
				char u[4];
				int w;
				if (r < 0x80)
					u[0] = (char)r, w = 1;
				else if (r < 0x800)
					u[0] = (char)(0xC0 | r >> 6), u[1] = (char)(0x80 | (r & 0x3F)), w = 2;
				else if (r < 0x10000)
					u[0] = (char)(0xE0 | r >> 12), u[1] = (char)(0x80 | ((r >> 6) & 0x3F)),
					u[2] = (char)(0x80 | (r & 0x3F)), w = 3;
				else
					u[0] = (char)(0xF0 | r >> 18), u[1] = (char)(0x80 | ((r >> 12) & 0x3F)),
					u[2] = (char)(0x80 | ((r >> 6) & 0x3F)), u[3] = (char)(0x80 | (r & 0x3F)), w = 4;
				buf_add(&b, u, (size_t)w);
				break;
			}
			default:
				buf_addc(&b, e);
			}
		} else if (c < 0x80) {
			buf_addc(&b, (char)c);
			k++;
		} else {
			uint32_t r;
			int w = utf8_decode(s.p + k, end - k, &r);
			if (r == 0xFFFD && w == 1)
				buf_adds(&b, "\xef\xbf\xbd");
			else
				buf_add(&b, s.p + k, (size_t)w);
			k += (size_t)w;
		}
	}
	return (Str){b.p ? b.p : "", b.n};
}

static int json_cmp_member(const void *x, const void *y)
{
	const go_std_json_member *a = x, *b = y;
	size_t n = a->key.n < b->key.n ? a->key.n : b->key.n;
	int c = n ? memcmp(a->key.p, b->key.p, n) : 0;
	if (c)
		return c;
	if (a->key.n != b->key.n)
		return a->key.n < b->key.n ? -1 : 1;
	return (a->index > b->index) - (a->index < b->index);
}

/* Parses a value at raw[*i]; NULL if invalid. Containers nest at most json_maxNestingDepth. */
static go_std_json_value *json_parse(Str raw, size_t *i, int depth, const char **err)
{
	size_t k = json_ws(raw, *i), n;
	if (k >= raw.n)
		return NULL;
	go_std_json_value *v = xalloc(sizeof *v);
	char c = raw.p[k];
	if (c == 'n' || c == 't' || c == 'f') {
		const char *lit = c == 'n' ? "null" : c == 't' ? "true" : "false";
		n = strlen(lit);
		if (raw.n - k < n || memcmp(raw.p + k, lit, n) != 0)
			return NULL;
		v->kind = c == 'n' ? GO_JSON_NULL : GO_JSON_BOOL;
		v->b = c == 't';
		*i = k + n;
		return v;
	}
	if (c == '"') {
		if (!(n = json_string(raw, k)))
			return NULL;
		v->kind = GO_JSON_STRING;
		v->s = json_unquote(raw, k, n);
		*i = k + n;
		return v;
	}
	if (c == '-' || json_isdigit(c)) {
		if (!(n = json_number(raw, k)))
			return NULL;
		v->kind = GO_JSON_NUMBER;
		v->raw = (Str){raw.p + k, n};
		char *num = xstrndup(raw.p + k, n);
		/* strconv.ParseFloat (strtod rounds correctly too); out of range: ±Inf */
		v->f = strtod(num, NULL);
		if ((v->f > 1.7976931348623157e308 || v->f < -1.7976931348623157e308) && !*err) {
			/* UnmarshalTypeError of a number out of the range of float64 */
			*err = xcat("json: cannot unmarshal number ", num, " into Go value of type float64");
		}
		*i = k + n;
		return v;
	}
	if (c != '[' && c != '{')
		return NULL;
	if (depth >= json_maxDecodeDepth) {
		if (!*err)
			*err = "unsupported by protoc-gen-uni: JSON nested more than 1000 levels deep";
		return NULL;
	}
	char close = c == '[' ? ']' : '}';
	v->kind = c == '[' ? GO_JSON_ARRAY : GO_JSON_OBJECT;
	Vec items = {0};
	k = json_ws(raw, k + 1);
	if (k < raw.n && raw.p[k] == close) {
		*i = k + 1;
		v->items = xalloc(sizeof *v->items);
		return v;
	}
	for (;;) {
		go_std_json_member *m = xalloc(sizeof *m);
		m->index = items.n;
		if (c == '{') {
			k = json_ws(raw, k);
			if (k >= raw.n || raw.p[k] != '"' || !(n = json_string(raw, k)))
				return NULL;
			m->key = json_unquote(raw, k, n);
			k = json_ws(raw, k + n);
			if (k >= raw.n || raw.p[k] != ':')
				return NULL;
			k++;
		}
		if (!(m->value = json_parse(raw, &k, depth + 1, err)))
			return NULL;
		vec_push(&items, m);
		k = json_ws(raw, k);
		if (k < raw.n && raw.p[k] == ',') {
			k++;
			continue;
		}
		if (k < raw.n && raw.p[k] == close)
			break;
		return NULL;
	}
	*i = k + 1;
	v->n = items.n;
	v->items = xalloc(sizeof *v->items * (size_t)(items.n + 1));
	for (int j = 0; j < items.n; j++)
		v->items[j] = *(go_std_json_member *)items.v[j];
	if (c == '{') {
		/* a map[string]any: the last member of a name wins; sorted by name */
		qsort(v->items, (size_t)v->n, sizeof *v->items, json_cmp_member);
		int w = 0;
		for (int j = 0; j < v->n; j++) {
			if (j + 1 < v->n && str_equal(v->items[j].key, v->items[j + 1].key))
				continue;
			v->items[w++] = v->items[j];
		}
		v->n = w;
	}
	return v;
}

go_std_json_value *go_std_json_unmarshal_any(Str raw, const char **err)
{
	*err = NULL;
	size_t i = 0;
	go_std_json_value *v = json_parse(raw, &i, 0, err);
	if (!v || json_ws(raw, i) != raw.n) {
		if (!*err || strncmp(*err, "unsupported", 11) != 0)
			*err = "invalid JSON";
		return NULL;
	}
	return *err ? NULL : v;
}
