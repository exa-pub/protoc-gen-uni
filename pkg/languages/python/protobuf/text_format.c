/* Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* google/protobuf/text_format.py of protobuf 7.36.2: text_format.Merge (MergeLines,
 * _Parser._ParseOrMerge, _MergeField, _MergeScalarField, the Tokenizer, ParseInteger, ParseBool,
 * ParseEnum and _ConsumeSingleByteString) for fields that are not messages. Columns count code
 * points like Python's str. */
#include "python_protobuf.h"

#include <string.h>

/* \001 at the start of a frame stands for the file name of text_format.py, \002 for the internal
 * type_checkers.py (expanded when the error is raised). */
#define TFL(line, func) "\001" #line ", in " func "\n"

static const char P_MERGE[] =
	TFL(788, "Merge")
	"    return MergeLines(\n"
	"        text.split(b'\\n' if isinstance(text, bytes) else '\\n'),\n"
	"    ...<5 lines>...\n"
	"        max_recursion_depth=max_recursion_depth,\n"
	"    )\n"
	TFL(894, "MergeLines")
	"    return parser.MergeLines(lines, message)\n"
	"           ~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^\n"
	TFL(924, "MergeLines")
	"    self._ParseOrMerge(lines, message)\n"
	"    ~~~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^\n"
	TFL(959, "_ParseOrMerge")
	"    self._MergeField(tokenizer, message)\n"
	"    ~~~~~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^\n";

#define RAISE_PREV(line) TFL(line, "_MergeField") "    raise tokenizer.ParseErrorPreviousToken(\n    ...<2 lines>...\n    )\n"
static const char T_UNKNOWN[] = RAISE_PREV(1082);
static const char T_NOEXT[] = RAISE_PREV(1035);
static const char T_IDENT[] = TFL(1618, "ConsumeIdentifier") "    raise self.ParseError('Expected identifier.')\n";
static const char T_EXT_NAME1[] = TFL(1029, "_MergeField") "    name = [tokenizer.ConsumeIdentifier()]\n"
	"            ~~~~~~~~~~~~~~~~~~~~~~~~~~~^^\n";
static const char T_EXT_NAME2[] = TFL(1031, "_MergeField") "    name.append(tokenizer.ConsumeIdentifier())\n"
	"                ~~~~~~~~~~~~~~~~~~~~~~~~~~~^^\n";
static const char T_IDNUM[] = TFL(1062, "_MergeField") "    name = tokenizer.ConsumeIdentifierOrNumber()\n"
	TFL(1640, "ConsumeIdentifierOrNumber") "    raise self.ParseError('Expected identifier or number, got %s.' % result)\n";
static const char T_COLON[] = TFL(1112, "_MergeField") "    tokenizer.Consume(':')\n    ~~~~~~~~~~~~~~~~~^^^^^\n";
static const char T_CONSUME[] = TFL(1576, "Consume") "    raise self.ParseError('Expected \"%s\".' % token)\n";
static const char T_MERGER[] = TFL(1128, "_MergeField") "    merger(tokenizer, message, field)\n"
	"    ~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char T_MERGER_REP[] = TFL(1122, "_MergeField") "    merger(tokenizer, message, field)\n"
	"    ~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char T_COMMA_REP[] = TFL(1125, "_MergeField") "    tokenizer.Consume(',')\n    ~~~~~~~~~~~~~~~~~^^^^^\n";
static const char T_INT32[] = TFL(1276, "_MergeScalarField") "    value = _ConsumeInt32(tokenizer)\n"
	TFL(1865, "_ConsumeInt32") "    return _ConsumeInteger(tokenizer, is_signed=True, is_long=False)\n";
static const char T_UINT32[] = TFL(1287, "_MergeScalarField") "    value = _ConsumeUint32(tokenizer)\n"
	TFL(1880, "_ConsumeUint32") "    return _ConsumeInteger(tokenizer, is_signed=False, is_long=False)\n";
static const char T_CONSUME_INT[] = TFL(1946, "_ConsumeInteger") "    raise tokenizer.ParseError(str(e))\n";
static const char T_BOOL[] = TFL(1299, "_MergeScalarField") "    value = tokenizer.ConsumeBool()\n"
	TFL(1702, "ConsumeBool") "    raise self.ParseError(str(e))\n";
static const char T_ENUM[] = TFL(1305, "_MergeScalarField") "    value = tokenizer.ConsumeEnum(field)\n"
	TFL(1772, "ConsumeEnum") "    raise self.ParseError(str(e))\n";
static const char T_STRING[] = TFL(1301, "_MergeScalarField") "    value = tokenizer.ConsumeString()\n"
	TFL(1722, "ConsumeString") "    the_bytes = self.ConsumeByteString()\n";
static const char T_BYTES1[] = TFL(1737, "ConsumeByteString") "    the_list = [self._ConsumeSingleByteString()]\n"
	"                ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^\n";
static const char T_BYTES2[] = TFL(1739, "ConsumeByteString") "    the_list.append(self._ConsumeSingleByteString())\n"
	"                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^^\n";
static const char T_NOT_STRING[] = TFL(1756, "_ConsumeSingleByteString")
	"    raise self.ParseError('Expected string but found: %r' % (text,))\n";
static const char T_NO_END_QUOTE[] = TFL(1759, "_ConsumeSingleByteString")
	"    raise self.ParseError('String missing ending quote: %r' % (text,))\n";

#define DURING "\nDuring handling of the above exception, another exception occurred:\n\n"
static const char C_INT_RANGE[] = "Traceback (most recent call last):\n"
	TFL(1944, "_ConsumeInteger") "    result = ParseInteger(tokenizer.token, is_signed=is_signed, is_long=is_long)\n"
	TFL(1970, "ParseInteger") "    checker.CheckValue(result)\n    ~~~~~~~~~~~~~~~~~~^^^^^^^^\n"
	"\002180, in CheckValue\n"
	"    raise ValueError('Value out of range: %d' % proposed_value)\n";
static const char C_INT_LITERAL[] = "Traceback (most recent call last):\n"
	TFL(1994, "_ParseAbstractInteger") "    return int(text, 0)\n";
static const char C_INT_PARSE[] = "Traceback (most recent call last):\n"
	TFL(1944, "_ConsumeInteger") "    result = ParseInteger(tokenizer.token, is_signed=is_signed, is_long=is_long)\n"
	TFL(1966, "ParseInteger") "    result = _ParseAbstractInteger(text)\n"
	TFL(1996, "_ParseAbstractInteger") "    raise ValueError(\"Couldn't parse integer: %s\" % orig_text)\n";
static const char C_BOOL[] = "Traceback (most recent call last):\n"
	TFL(1700, "ConsumeBool") "    result = ParseBool(self.token)\n"
	TFL(2050, "ParseBool") "    raise ValueError('Expected \"true\" or \"false\".')\n";
static const char C_ENUM_LITERAL[] = "Traceback (most recent call last):\n"
	TFL(2071, "ParseEnum") "    number = int(value, 0)\n";
static const char C_ENUM_NAME[] = "Traceback (most recent call last):\n"
	TFL(1770, "ConsumeEnum") "    result = ParseEnum(field, self.token)\n"
	TFL(2076, "ParseEnum") "    raise ValueError(\n    ...<2 lines>...\n    )\n";
static const char C_ENUM_NUMBER[] = "Traceback (most recent call last):\n"
	TFL(1770, "ConsumeEnum") "    result = ParseEnum(field, self.token)\n"
	TFL(2085, "ParseEnum") "    raise ValueError(\n    ...<2 lines>...\n    )\n";

#define PB_DIR "  File \"/app/lib/python3.13/site-packages/google/protobuf/"

static COLD const char *expand(const char *s)
{
	Buf b = {0};
	for (; s && *s; s++) {
		if (*s == 1)
			buf_adds(&b, PB_DIR "text_format.py\", line ");
		else if (*s == 2)
			buf_adds(&b, PB_DIR "internal/type_checkers.py\", line ");
		else
			buf_addc(&b, *s);
	}
	return buf_str(&b);
}

/* ---- the tokenizer ---- */

typedef struct {
	Str *lines;
	int nlines, next;
	int line;
	size_t col; /* bytes */
	Str cur;
	bool more;
	Str token;
	int prev_line;
	size_t prev_col;
	PypbError *err;
	Buf frames; /* the frames of the current _MergeField below P_MERGE */
} Tok;

static size_t cps(Str s, size_t n)
{
	size_t c = 0;
	for (size_t i = 0; i < n && i < s.n; i++)
		c += ((unsigned char)s.p[i] & 0xc0) != 0x80;
	return c;
}

static void pop_line(Tok *t)
{
	while (t->cur.n <= t->col) {
		if (t->next >= t->nlines) {
			t->cur = (Str){"", 0};
			t->more = false;
			return;
		}
		t->cur = t->lines[t->next++];
		t->line++;
		t->col = 0;
	}
}

/* _SkipWhitespace: (\s|(#.*$))+ */
static void skip_ws(Tok *t)
{
	for (;;) {
		pop_line(t);
		size_t start = t->col;
		while (t->col < t->cur.n) {
			uint32_t c;
			int w = utf8_decode(t->cur.p + t->col, t->cur.n - t->col, &c);
			if (c == '#')
				t->col = t->cur.n;
			else if (py_isspace(c))
				t->col += (size_t)w;
			else
				break;
		}
		if (t->col == start)
			break;
	}
}

static bool is_alnum_(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* The _TOKEN match at the current column, or one character. */
static size_t token_len(Str s, size_t i)
{
	char c = s.p[i];
	size_t j = i + 1;
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
		while (j < s.n && (is_alnum_(s.p[j]) || s.p[j] == '+' || s.p[j] == '-'))
			j++;
		return j - i;
	}
	if ((c >= '0' && c <= '9') || c == '+' || c == '-' || (c == '.' && j < s.n && s.p[j] >= '0' && s.p[j] <= '9')) {
		if (c == '.')
			j++;
		while (j < s.n && (is_alnum_(s.p[j]) || s.p[j] == '.' || s.p[j] == '+' || s.p[j] == '-'))
			j++;
		return j - i;
	}
	if (c == '"' || c == '\'') {
		while (j < s.n) {
			if (s.p[j] == c)
				return j + 1 - i;
			if (s.p[j] == '\\') {
				if (j + 1 >= s.n)
					return s.n - i; /* \\?$ */
				uint32_t r;
				j += 1 + (size_t)utf8_decode(s.p + j + 1, s.n - j - 1, &r);
				continue;
			}
			j++;
		}
		return s.n - i;
	}
	uint32_t r;
	return (size_t)utf8_decode(s.p + i, s.n - i, &r);
}

static void next_token(Tok *t)
{
	t->prev_line = t->line;
	t->prev_col = t->col;
	t->col += t->token.n;
	skip_ws(t);
	if (!t->more) {
		t->token = (Str){"", 0};
		return;
	}
	t->token = (Str){t->cur.p + t->col, token_len(t->cur, t->col)};
}

static bool try_consume(Tok *t, const char *s)
{
	if (!str_eqc(t->token, s))
		return false;
	next_token(t);
	return true;
}

/* Raises a ParseError: chained is the traceback of the exceptions it occurred during. */
static COLD bool raise_at(Tok *t, const char *chained, const char *tail, int line, size_t col, const char *msg)
{
	Buf f = {0};
	buf_adds(&f, P_MERGE);
	buf_add(&f, t->frames.p ? t->frames.p : "", t->frames.n);
	buf_adds(&f, tail);
	t->err->chained = chained;
	t->err->frames = expand(buf_str(&f));
	t->err->exc = xfmt("google.protobuf.text_format.ParseError: %d:%d : %s\n", line, (int)col, msg);
	return false;
}

/* tokenizer.ParseError(message): for the current token */
static bool parse_error(Tok *t, const char *chained, const char *tail, const char *msg)
{
	Buf m = {0};
	buf_addc(&m, '\'');
	buf_add_str(&m, t->cur);
	buf_fmt(&m, "': %s", msg);
	return raise_at(t, chained, tail, t->line + 1, cps(t->cur, t->col) + 1, buf_str(&m));
}

/* tokenizer.ParseErrorPreviousToken(message) */
static bool prev_error(Tok *t, const char *tail, const char *msg)
{
	Str line = t->prev_line == t->line ? t->cur : t->lines[t->prev_line];
	return raise_at(t, NULL, tail, t->prev_line + 1, cps(line, t->prev_col) + 1, msg);
}

static void frames_add(Tok *t, const char *f) { buf_adds(&t->frames, f); }

/* ---- values ---- */

static void fail_non_ascii(Str s)
{
	for (size_t i = 0; i < s.n; i++)
		if ((unsigned char)s.p[i] >= 0x80)
			fail("unsupported by protoc-gen-uni: non-ASCII characters in a text format value");
}

static int digit_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	return 99;
}

/* int(text, 0): false for a ValueError. */
static bool int_base0(Str s, int64_t *out, bool *huge)
{
	fail_non_ascii(s);
	size_t i = 0, n = s.n;
	*huge = false;
	while (i < n && py_isspace((unsigned char)s.p[i]))
		i++;
	while (n > i && py_isspace((unsigned char)s.p[n - 1]))
		n--;
	bool neg = false;
	if (i < n && (s.p[i] == '+' || s.p[i] == '-'))
		neg = s.p[i++] == '-';
	int base = 10;
	bool prefixed = false;
	if (i + 1 < n && s.p[i] == '0' && strchr("xXoObB", s.p[i + 1])) {
		base = s.p[i + 1] == 'x' || s.p[i + 1] == 'X' ? 16 : s.p[i + 1] == 'o' || s.p[i + 1] == 'O' ? 8 : 2;
		i += 2;
		prefixed = true;
	}
	if (i >= n)
		return false;
	bool zero_lead = !prefixed && s.p[i] == '0';
	uint64_t v = 0;
	int prev = 0; /* 0: start, 1: after a digit, 2: after an underscore */
	for (; i < n; i++) {
		char c = s.p[i];
		if (c == '_') {
			if (!(prev == 1 || (prev == 0 && prefixed)))
				return false;
			prev = 2;
			continue;
		}
		int d = digit_value(c);
		if (d >= base)
			return false;
		if (zero_lead && d)
			return false;
		if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base)
			*huge = true;
		v = v * (uint64_t)base + (uint64_t)d;
		prev = 1;
	}
	if (prev != 1)
		return false;
	if (v > (uint64_t)INT64_MAX + neg)
		*huge = true;
	*out = neg ? (int64_t)(0 - v) : (int64_t)v;
	return true;
}

/* repr(s) shortened like %.200R */
static char *repr200(Str s)
{
	char *r = py_repr_str(s);
	Str rs = str_c(r);
	if (cps(rs, rs.n) <= 200)
		return r;
	size_t i = 0, c = 0;
	while (i < rs.n && c < 200) {
		i++;
		while (i < rs.n && ((unsigned char)rs.p[i] & 0xc0) == 0x80)
			i++;
		c++;
	}
	return xstrndup(r, i);
}

static bool consume_integer(Tok *t, bool is_signed, int64_t *out)
{
	Str text = t->token;
	Str orig = text;
	fail_non_ascii(text);
	/* (-?)0(\d+)$ -> C octal */
	size_t k = text.n && text.p[0] == '-' ? 1 : 0;
	bool octal = text.n >= k + 2 && text.p[k] == '0';
	for (size_t i = k + 1; octal && i < text.n; i++)
		octal = text.p[i] >= '0' && text.p[i] <= '9';
	if (octal)
		text = str_c(xfmt("%s0o%s", k ? "-" : "", xstrndup(text.p + k + 1, text.n - k - 1)));
	int64_t v;
	bool huge;
	if (!int_base0(text, &v, &huge)) {
		const char *orig_s = xstrndup(orig.p, orig.n);
		const char *chained = xcat(expand(C_INT_LITERAL), "ValueError: invalid literal for int() with base 0: ", repr200(text),
			"\n" DURING, expand(C_INT_PARSE), "ValueError: Couldn't parse integer: ", orig_s, "\n" DURING);
		return parse_error(t, chained, T_CONSUME_INT, xcat("Couldn't parse integer: ", orig_s));
	}
	if (huge)
		fail("unsupported by protoc-gen-uni: an integer beyond 64 bits in a text format value");
	if (is_signed ? v < INT32_MIN || v > INT32_MAX : v < 0 || v > (int64_t)UINT32_MAX) {
		const char *msg = xfmt("Value out of range: %lld", (long long)v);
		return parse_error(t, xcat(expand(C_INT_RANGE), "ValueError: ", msg, "\n" DURING), T_CONSUME_INT, msg);
	}
	*out = v;
	next_token(t);
	return true;
}

static bool consume_enum(Tok *t, const PypbEnum *e, int64_t *out)
{
	int64_t num;
	bool huge;
	Str tok = t->token;
	if (int_base0(tok, &num, &huge)) {
		if (huge)
			fail("unsupported by protoc-gen-uni: an integer beyond 64 bits in a text format value");
		for (int i = 0; i < e->n; i++)
			if (e->v[i].number == num) {
				*out = num;
				next_token(t);
				return true;
			}
		const char *msg = xfmt("Enum type \"%s\" has no value with number %lld.", e->full_name, (long long)num);
		return parse_error(t, xcat(expand(C_ENUM_NUMBER), "ValueError: ", msg, "\n" DURING), "", msg);
	}
	for (int i = 0; i < e->n; i++)
		if (str_eqc(tok, e->v[i].name)) {
			*out = e->v[i].number;
			next_token(t);
			return true;
		}
	const char *v = xstrndup(tok.p, tok.n);
	const char *msg = xfmt("Enum type \"%s\" has no value named %s.", e->full_name, v);
	const char *chained = xcat(expand(C_ENUM_LITERAL), "ValueError: invalid literal for int() with base 0: ", repr200(tok),
		"\n" DURING, expand(C_ENUM_NAME), "ValueError: ", msg, "\n" DURING);
	return parse_error(t, chained, "", msg);
}

static int hexd(char c)
{
	int d = digit_value(c);
	return d < 16 ? d : -1;
}

/* text_encoding.CUnescape for text without \u, \U and \N escapes (the latter, and the escapes
 * the codecs reject, are not ported). */
static Str c_unescape(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n;) {
		if (s.p[i] != '\\') {
			buf_addc(&b, s.p[i++]);
			continue;
		}
		if (++i >= s.n)
			fail("unsupported by protoc-gen-uni: a trailing backslash in a text format string");
		char c = s.p[i++];
		const char *simple = strchr("\\'\"abfnrtv", c);
		if (simple && c) {
			static const char out[] = "\\'\"\a\b\f\n\r\t\v";
			buf_addc(&b, out[simple - "\\'\"abfnrtv"]);
		} else if (c == '\n') {
		} else if (c >= '0' && c <= '7') {
			int v = c - '0';
			for (int k = 0; k < 2 && i < s.n && s.p[i] >= '0' && s.p[i] <= '7'; k++)
				v = v * 8 + (s.p[i++] - '0');
			if (v > 0377)
				fail("unsupported by protoc-gen-uni: an octal escape above \\377 in a text format string");
			buf_addc(&b, (char)v);
		} else if (c == 'x' && i < s.n && hexd(s.p[i]) >= 0) {
			int v = hexd(s.p[i++]);
			if (i < s.n && hexd(s.p[i]) >= 0)
				v = v * 16 + hexd(s.p[i++]);
			buf_addc(&b, (char)v);
		} else if (c == 'u' || c == 'U' || c == 'N' || c == 'x') {
			fail("unsupported by protoc-gen-uni: the escape \\%c in a text format string", c);
		} else {
			buf_addc(&b, '\\');
			buf_addc(&b, c);
		}
	}
	return buf_view(&b);
}

static bool consume_single_string(Tok *t, const char *bytes_frame, Buf *out)
{
	Str text = t->token;
	if (text.n < 1 || (text.p[0] != '"' && text.p[0] != '\'')) {
		Buf f = {0};
		buf_adds(&f, bytes_frame);
		buf_adds(&f, T_NOT_STRING);
		return parse_error(t, NULL, buf_str(&f), xcat("Expected string but found: ", py_repr_str(text)));
	}
	if (text.n < 2 || text.p[text.n - 1] != text.p[0]) {
		Buf f = {0};
		buf_adds(&f, bytes_frame);
		buf_adds(&f, T_NO_END_QUOTE);
		return parse_error(t, NULL, buf_str(&f), xcat("String missing ending quote: ", py_repr_str(text)));
	}
	Str v = c_unescape((Str){text.p + 1, text.n - 2});
	buf_add_str(out, v);
	next_token(t);
	return true;
}

static bool consume_string(Tok *t, Str *out)
{
	Buf b = {0};
	if (!consume_single_string(t, T_BYTES1, &b))
		return false;
	while (t->token.n && (t->token.p[0] == '"' || t->token.p[0] == '\''))
		if (!consume_single_string(t, T_BYTES2, &b))
			return false;
	Str s = buf_view(&b);
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xfffd && w == 1)
			fail("unsupported by protoc-gen-uni: a text format string that is not valid UTF-8");
		i += (size_t)w;
	}
	*out = s;
	return true;
}

/* _MergeScalarField */
static bool merge_scalar(Tok *t, const PypbMessage *m, int fi, PypbSet set, void *ctx)
{
	const PypbField *f = &m->f[fi];
	int64_t v = 0;
	Str s = {0};
	switch (f->type) {
	case PT_INT32:
	case PT_UINT32:
		frames_add(t, f->type == PT_INT32 ? T_INT32 : T_UINT32);
		if (!consume_integer(t, f->type == PT_INT32, &v))
			return false;
		break;
	case PT_BOOL: {
		frames_add(t, T_BOOL);
		const char *tv[] = {"true", "t", "1", "True"}, *fv[] = {"false", "f", "0", "False"};
		bool ok = false;
		for (int i = 0; i < 4 && !ok; i++) {
			if (str_eqc(t->token, tv[i]))
				ok = true, v = 1;
			else if (str_eqc(t->token, fv[i]))
				ok = true, v = 0;
		}
		if (!ok)
			return parse_error(t, xcat(expand(C_BOOL), "ValueError: Expected \"true\" or \"false\".\n" DURING), "",
				"Expected \"true\" or \"false\".");
		next_token(t);
		break;
	}
	case PT_ENUM:
		frames_add(t, T_ENUM);
		if (!consume_enum(t, f->enm, &v))
			return false;
		break;
	default:
		frames_add(t, T_STRING);
		if (!consume_string(t, &s))
			return false;
	}
	set(ctx, fi, v, s);
	return true;
}

/* _MergeField */
static bool merge_field(Tok *t, const PypbMessage *m, PypbSet set, void *ctx)
{
	t->frames.n = 0;
	if (try_consume(t, "[")) {
		const char *frame = T_EXT_NAME1;
		for (;;) {
			bool ident = t->token.n && !(t->token.p[0] >= '0' && t->token.p[0] <= '9');
			if (ident && (unsigned char)t->token.p[0] >= 0x80)
				fail("unsupported by protoc-gen-uni: non-ASCII characters in a text format name");
			if (!ident || !is_alnum_(t->token.p[0])) {
				frames_add(t, frame);
				return parse_error(t, NULL, T_IDENT, "Expected identifier.");
			}
			next_token(t);
			if (!try_consume(t, "."))
				break;
			frame = T_EXT_NAME2;
		}
		return prev_error(t, T_NOEXT, xfmt("Message type \"%s\" does not have extensions.", m->full_name));
	}
	Str name = t->token;
	if (name.n && (unsigned char)name.p[0] >= 0x80)
		fail("unsupported by protoc-gen-uni: non-ASCII characters in a text format name");
	if (!name.n || !is_alnum_(name.p[0]))
		return parse_error(t, NULL, T_IDNUM,
			xfmt("Expected identifier or number, got %s.", xstrndup(name.p, name.n)));
	next_token(t);
	int fi = -1;
	for (int i = 0; i < m->n && fi < 0; i++)
		if (str_eqc(name, m->f[i].name))
			fi = i;
	if (fi < 0)
		return prev_error(t, T_UNKNOWN,
			xfmt("Message type \"%s\" has no field named \"%s\".", m->full_name, xstrndup(name.p, name.n)));
	if (!try_consume(t, ":")) {
		frames_add(t, T_COLON);
		return parse_error(t, NULL, T_CONSUME, "Expected \":\".");
	}
	if (m->f[fi].repeated && try_consume(t, "[")) {
		if (!try_consume(t, "]"))
			for (;;) {
				t->frames.n = 0;
				frames_add(t, T_MERGER_REP);
				if (!merge_scalar(t, m, fi, set, ctx))
					return false;
				if (try_consume(t, "]"))
					break;
				if (!try_consume(t, ",")) {
					t->frames.n = 0;
					frames_add(t, T_COMMA_REP);
					return parse_error(t, NULL, T_CONSUME, "Expected \",\".");
				}
			}
	} else {
		frames_add(t, T_MERGER);
		if (!merge_scalar(t, m, fi, set, ctx))
			return false;
	}
	if (!try_consume(t, ","))
		try_consume(t, ";");
	return true;
}

bool pypb_text_merge(const PypbMessage *m, Str text, PypbSet set, void *ctx, PypbError *err)
{
	Tok t = {0};
	t.nlines = str_split_s(text, '\n', &t.lines);
	t.line = -1;
	t.more = true;
	t.cur = (Str){"", 0};
	t.err = err;
	t.token = (Str){"", 0};
	skip_ws(&t);
	next_token(&t);
	while (t.token.n)
		if (!merge_field(&t, m, set, ctx))
			return false;
	return true;
}
