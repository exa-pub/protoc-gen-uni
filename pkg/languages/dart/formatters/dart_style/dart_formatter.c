/* Port of dart_style 3.1.13 lib/src/dart_formatter.dart (DartFormatter.formatSource, short style
 * path only) and lib/src/string_compare.dart.
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* string_compare.dart: _isWhitespace over code points (the BMP-only list upstream). */
static bool sc_isWhitespace(uint32_t c)
{
	return c == 0x002c || c == 0x005b || c == 0x005d || c == 0x007b || c == 0x007d || c == 0x003b ||
	       (c >= 0x0009 && c <= 0x000d) || c == 0x0020 || c == 0x0085 || c == 0x00a0 || c == 0x1680 ||
	       (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
	       c == 0x3000 || c == 0xfeff;
}

static size_t sc_moveNextNonWhitespace(const char *s, size_t len, size_t i)
{
	while (i < len) {
		uint32_t r;
		int w = utf8_decode(s + i, len - i, &r);
		if (!sc_isWhitespace(r))
			break;
		i += (size_t)w;
	}
	return i;
}

/* equalIgnoringWhitespace (comparing code points instead of UTF-16 units is equivalent). */
static bool sc_equalIgnoringWhitespace(const char *s1, size_t len1, const char *s2, size_t len2)
{
	size_t i1 = 0, i2 = 0;
	for (;;) {
		i1 = sc_moveNextNonWhitespace(s1, len1, i1);
		i2 = sc_moveNextNonWhitespace(s2, len2, i2);
		if (i1 >= len1 || i2 >= len2)
			return (i1 >= len1) == (i2 >= len2);
		if (s1[i1] != s2[i2])
			return false;
		i1++;
		i2++;
	}
}

/* inferLineEnding */
static const char *df_inferLineEnding(const LineInfo *li, const char *source)
{
	if (li->n > 1 && li->starts[1] >= 2 && source[li->starts[1] - 2] == '\r')
		return "\r\n";
	return "\n";
}

/* tokenizeLanguageVersionOrSingleLineComment: `//` + spaces + `@dart` + spaces + `=` + spaces +
 * digits `.` digits + spaces + line end. Returns major * 100 + minor, or -1. */
static int df_languageVersion(const char *p)
{
	if (strncmp(p, "//", 2) != 0 || p[2] == '/')
		return -1;
	p += 2;
	while (*p == ' ')
		p++;
	if (strncmp(p, "@dart", 5) != 0)
		return -1;
	p += 5;
	while (*p == ' ')
		p++;
	if (*p++ != '=')
		return -1;
	while (*p == ' ')
		p++;
	int major = 0, minor = 0;
	const char *d = p;
	while (*p >= '0' && *p <= '9' && major < 10000)
		major = major * 10 + (*p++ - '0');
	if (p == d || *p++ != '.')
		return -1;
	d = p;
	while (*p >= '0' && *p <= '9' && minor < 100)
		minor = minor * 10 + (*p++ - '0');
	if (p == d)
		return -1;
	while (*p == ' ')
		p++;
	return *p ? -1 : major * 100 + minor;
}

/* DartFormatter(languageVersion: 3.7.0).formatSource(SourceCode(text)) of a linearized
 * compilation unit (ds_linearize). */
static Str df_formatSource(const DartFormatterOpts *opts, Node *unit, Str text, int languageVersion)
{
	if (text.n > (size_t)INT32_MAX / 2)
		fail("dart_style: source too large");
	rule_dummy = rule_hard();
	LineInfo lines = {0};
	int cap = 64;
	lines.starts = xalloc((size_t)cap * sizeof(int));
	lines.starts[lines.n++] = 0;
	for (size_t i = 0; i < text.n; i++)
		if (text.p[i] == '\n') {
			if (lines.n == cap) {
				lines.starts = xrealloc(lines.starts, (size_t)cap * sizeof(int), (size_t)cap * 2 * sizeof(int));
				cap *= 2;
			}
			lines.starts[lines.n++] = (int)i + 1;
		}
	const char *lineEnding = df_inferLineEnding(&lines, text.p);

	/* CompilationUnit.languageVersionToken: a `// @dart = x.y` comment before the first token.
	 * If the code has one, use it to determine the style. */
	for (Token *c = unit->begin->precedingComments; c; c = c->next) {
		int v = df_languageVersion(c->lexeme);
		if (v >= 0) {
			languageVersion = v;
			break;
		}
	}
	if (languageVersion >= 307)
		fail("dart_style: the tall style (language version %d.%d) is not ported", languageVersion / 100,
		     languageVersion % 100);

	SourceVisitor v = {0};
	v.lineInfo = &lines;
	v.builder = cb_new(opts, true);
	FormatResult *r = sv_run(&v, unit, lineEnding);
	if (!sc_equalIgnoringWhitespace(text.p, text.n, r->text, r->n))
		fail("dart_style: the formatter produced unexpected output (only whitespace may change)");
	return (Str){r->text, r->n};
}
