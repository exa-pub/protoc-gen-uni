/*
 * Copyright (C) 2015 Square, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* A port of kotlinpoet/src/commonMain/kotlin/com/squareup/kotlinpoet/Util.kt and Util.jvm.kt of
 * KotlinPoet 2.4.0 and 2.2.0: identifier escaping, import aliases and string literals. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

int kp_version = KP_V2_4_0;
void kp_select_version(int version) { kp_version = version; }

void kp_illegal_argument(const char *msg) { java_exception("java.lang.IllegalArgumentException", msg); }
void kp_illegal_state(const char *msg) { java_exception("java.lang.IllegalStateException", msg); }

int kp_utf8_width(const char *s)
{
	unsigned char c = (unsigned char)*s;
	return c < 0x80 ? 1 : c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
}

size_t kp_utf16_len(const char *s, size_t n)
{
	size_t len = 0;
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if ((c & 0xc0) != 0x80)
			len += c >= 0xf0 ? 2 : 1;
	}
	return len;
}

static uint32_t kp_decode(const char *s, int *w)
{
	uint32_t r;
	*w = utf8_decode(s, strlen(s), &r);
	return r;
}

/* Character.isJavaIdentifierStart / isJavaIdentifierPart. Beyond ASCII only the identifier-ignorable
 * C1 controls are known; other characters need the JDK's Unicode tables. */
COLD static NORETURN void kp_unsupported_char(const char *s)
{
	fail("protoc-gen-uni does not support characters beyond ASCII in Kotlin names (KotlinPoet escapes them with "
	     "Java's Unicode character classes): %s",
	     s);
}

static bool kp_ident_start(uint32_t c, const char *s)
{
	if (c >= 0x80)
		kp_unsupported_char(s);
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_';
}

static bool kp_ident_part(uint32_t c, const char *s)
{
	if (c >= 0x80 && c <= 0x9f)
		return true; /* isIdentifierIgnorable */
	if (c >= 0x80)
		kp_unsupported_char(s);
	return kp_ident_start(c, s) || (c >= '0' && c <= '9') || c <= 0x08 || (c >= 0x0e && c <= 0x1b) || c == 0x7f;
}

/* KEYWORDS, sorted for bsearch */
static const char *const kp_keywords[] = {
	"abstract", "actual", "annotation", "as", "break", "by", "catch", "class", "companion", "const",
	"constructor", "continue", "crossinline", "data", "delegate", "do", "dynamic", "else", "enum", "expect",
	"external", "false", "field", "file", "final", "finally", "for", "fun", "get", "header", "if", "impl",
	"import", "in", "infix", "init", "inline", "inner", "interface", "internal", "is", "lateinit", "noinline",
	"null", "object", "open", "operator", "out", "override", "package", "param", "private", "property",
	"protected", "public", "receiver", "reified", "return", "sealed", "set", "setparam", "super", "suspend",
	"tailrec", "this", "throw", "true", "try", "typealias", "typeof", "val", "value", "var", "vararg", "when",
	"where", "while", "yield",
};

static int kp_cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

bool kp_is_keyword(const char *s)
{
	return bsearch(&s, kp_keywords, sizeof kp_keywords / sizeof *kp_keywords, sizeof *kp_keywords, kp_cmp_str) != NULL;
}

static bool kp_already_escaped(const char *s)
{
	size_t n = strlen(s);
	return n >= 1 && s[0] == '`' && s[n - 1] == '`';
}

static bool kp_all_underscores(const char *s)
{
	for (; *s; s++)
		if (*s != '_')
			return false;
	return true;
}

/* ILLEGAL_CHARACTERS_TO_ESCAPE, in the order of the set */
static const char kp_illegal_chars[] = ".;[]/<>:\\";

/* failIfEscapeInvalid of KotlinPoet 2.2.0 (2.4.0 accepts every escaped identifier, and any
 * identifier with one of these characters is escaped by then) */
static void kp_fail_if_escape_invalid(const char *s)
{
	Buf found = {0};
	for (const char *c = kp_illegal_chars; *c; c++)
		if (strchr(s, *c))
			buf_addc(&found, *c);
	if (found.n)
		kp_illegal_argument(xcat("Can't escape identifier ", s, " because it contains illegal characters: ", buf_str(&found)));
}

/* escapeIfNecessary: escapeIfNotJavaIdentifier, escapeIfKeyword, escapeIfHasAllowedCharacters,
 * escapeIfAllCharactersAreUnderscore, each wrapping in backticks unless already escaped, then the
 * validation (failIfEscapeInvalid). */
static const char *kp_escape_impl(const char *s, bool validate)
{
	if (!*s)
		java_exception("java.util.NoSuchElementException", "Char sequence is empty.");
	int w;
	bool java_ident = kp_ident_start(kp_decode(s, &w), s);
	for (const char *p = s + w; *p && java_ident; p += w)
		java_ident = kp_ident_part(kp_decode(p, &w), s);
	if (!java_ident && !kp_already_escaped(s))
		s = xcat("`", s, "`");
	if (kp_is_keyword(s) && !kp_already_escaped(s))
		s = xcat("`", s, "`");
	if (strchr(s, '$') && !kp_already_escaped(s))
		s = xcat("`", s, "`");
	if (kp_all_underscores(s) && !kp_already_escaped(s))
		s = xcat("`", s, "`");
	if (validate && KP_V22)
		kp_fail_if_escape_invalid(s);
	return s;
}

const char *kp_escape(const char *s) { return kp_escape_impl(s, true); }
const char *kp_escape_unvalidated(const char *s) { return kp_escape_impl(s, false); }

/* extractMemberName(part) of CodeWriter: the longest prefix that isIdentifier matches. The parts
 * after a type are text of the generators, ASCII: IDENTIFIER_REGEX is letters, digits, upper case
 * letters, lower case letters (the backtick form never starts with an identifier start). */
const char *kp_extract_member_name(const char *part)
{
	if (!kp_ident_start((unsigned char)part[0], part))
		kp_illegal_argument(xcat("not an identifier: ", part));
	int phase = 0;
	size_t n = 0;
	for (; part[n]; n++) {
		char c = part[n];
		bool upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
		if (phase == 0 && (upper || lower))
			continue;
		if (phase <= 1 && digit)
			phase = 1;
		else if (phase <= 2 && upper)
			phase = 2;
		else if (phase <= 3 && lower)
			phase = 3;
		else
			break;
	}
	return xstrndup(part, n);
}

const char *kp_escape_if_necessary(const char *s) { return kp_escape(s); }

const char *kp_escape_segments_if_necessary(const char *s)
{
	Buf b = {0};
	bool first = true;
	for (const char *p = s;;) {
		const char *dot = strchr(p, '.');
		size_t n = dot ? (size_t)(dot - p) : strlen(p);
		if (n) {
			if (!first)
				buf_addc(&b, '.');
			buf_adds(&b, kp_escape(xstrndup(p, n)));
			first = false;
		}
		if (!dot)
			break;
		p = dot + 1;
	}
	return xstrdup(buf_str(&b));
}

const char *kp_escape_as_alias(const char *s)
{
	if (kp_all_underscores(s))
		return xcat(s, "0");
	if (kp_is_keyword(s))
		return xcat("__", s);
	Buf b = {0};
	int w;
	if (!kp_ident_start(kp_decode(s, &w), s))
		buf_addc(&b, '_');
	for (const char *p = s; *p; p += w) {
		uint32_t c = kp_decode(p, &w);
		if (c == '$')
			buf_adds(&b, "__");
		else if (!kp_ident_part(c, s))
			buf_fmt(&b, "_U%04x", (unsigned)c);
		else
			buf_add(&b, p, (size_t)w);
	}
	return xstrdup(buf_str(&b));
}

/* characterLiteralWithoutSingleQuotes */
static void kp_char_literal(Buf *b, const char *p, int w)
{
	unsigned char c = (unsigned char)*p;
	uint32_t cp = c;
	if (w > 1)
		utf8_decode(p, (size_t)w, &cp);
	switch (cp) {
	case '\b': buf_adds(b, "\\b"); return;
	case '\t': buf_adds(b, "\\t"); return;
	case '\n': buf_adds(b, "\\n"); return;
	case '\r': buf_adds(b, "\\r"); return;
	case '"': buf_adds(b, "\""); return;
	case '\'': buf_adds(b, "\\'"); return;
	case '\\': buf_adds(b, "\\\\"); return;
	}
	if (cp <= 0x1f || (cp >= 0x7f && cp <= 0x9f))
		buf_fmt(b, "\\u%04x", (unsigned)cp);
	else
		buf_add(b, p, (size_t)w);
}

const char *kp_escape_character_literals(const char *s)
{
	Buf b = {0};
	for (const char *p = s; *p;) {
		int w = kp_utf8_width(p);
		kp_char_literal(&b, p, w);
		p += w;
	}
	return xstrdup(buf_str(&b));
}

const char *kp_string_literal_with_quotes(const char *value, bool is_constant_context)
{
	Buf r = {0};
	if (!is_constant_context && strchr(value, '\n')) {
		buf_adds(&r, "\"\"\"\n|");
		size_t n = strlen(value);
		for (size_t i = 0; i < n; i++) {
			char c = value[i];
			if (!strncmp(value + i, "\"\"\"", 3)) {
				buf_adds(&r, "\"\"${'\"'}");
				i += 2;
			} else if (c == '\n') {
				buf_adds(&r, "\n|");
			} else if (c == '$') {
				buf_adds(&r, "${'$'}");
			} else {
				buf_addc(&r, c);
			}
		}
		if (n == 0 || value[n - 1] != '\n')
			buf_addc(&r, '\n');
		buf_adds(&r, "\"\"\".trimMargin()");
		return xstrdup(buf_str(&r));
	}
	buf_addc(&r, '"');
	for (const char *p = value; *p;) {
		int w = kp_utf8_width(p);
		if (*p == '\'')
			buf_addc(&r, '\'');
		else if (*p == '"')
			buf_adds(&r, "\\\"");
		else if (*p == '$')
			buf_adds(&r, "${'$'}");
		else
			kp_char_literal(&r, p, w);
		p += w;
	}
	buf_addc(&r, '"');
	return xstrdup(buf_str(&r));
}

/* replaceFirstChar(Char::uppercaseChar / lowercaseChar): Character.toUpperCase/toLowerCase of the
 * JDK's tables beyond ASCII are not ported. */
static const char *kp_map_first(const char *s, bool upper)
{
	if (!*s)
		return s;
	if ((unsigned char)*s >= 0x80)
		kp_unsupported_char(s);
	char *r = xstrdup(s);
	if (upper && r[0] >= 'a' && r[0] <= 'z')
		r[0] = (char)(r[0] - 'a' + 'A');
	else if (!upper && r[0] >= 'A' && r[0] <= 'Z')
		r[0] = (char)(r[0] - 'A' + 'a');
	return r;
}

const char *kp_upper_first(const char *s) { return kp_map_first(s, true); }
const char *kp_lower_first(const char *s) { return kp_map_first(s, false); }
