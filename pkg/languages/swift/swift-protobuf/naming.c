/* Copyright (c) 2014 - 2017 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/NamingUtils.swift, SwiftLanguage.swift and
 * UnicodeScalar+Extensions.swift (swift-protobuf 1.39.0), plus the Foundation character sets
 * the plugin uses. */
#include "swift_protobuf.h"

#include <string.h>

/* ---- strings ---- */

int sw_next_scalar(const char *s, size_t n, uint32_t *c) { return utf8_decode(s, n, c); }

/* CharacterSet.whitespaces: Unicode General Category Zs and tab. */
bool sw_is_whitespace(uint32_t c)
{
	return c == ' ' || c == '\t' || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x202f ||
	       c == 0x205f || c == 0x3000;
}

/* CharacterSet.newlines: U+000A–U+000D, U+0085, U+2028 and U+2029. */
bool sw_is_newline(uint32_t c) { return (c >= 0x0a && c <= 0x0d) || c == 0x85 || c == 0x2028 || c == 0x2029; }

/* CharacterSet.whitespacesAndNewlines: General Category Z*, U+0009–U+000D and U+0085. */
bool sw_is_whitespace_or_newline(uint32_t c) { return sw_is_whitespace(c) || sw_is_newline(c); }

char *sw_ascii_lower(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p + 0x20);
	return r;
}

static char asc_upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 0x20) : c; }
static char asc_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 0x20) : c; }

/* ---- SwiftLanguage.swift ---- */

bool sw_is_swift_identifier_head(uint32_t c)
{
	switch (c) {
	case 0x5f: case 0xa8: case 0xaa: case 0xad: case 0xaf: case 0x2054: return true;
	}
	return (c >= 0x61 && c <= 0x7a) || (c >= 0x41 && c <= 0x5a) || (c >= 0xb2 && c <= 0xb5) ||
	       (c >= 0xb7 && c <= 0xba) || (c >= 0xbc && c <= 0xbe) || (c >= 0xc0 && c <= 0xd6) ||
	       (c >= 0xd8 && c <= 0xf6) || (c >= 0xf8 && c <= 0xff) || (c >= 0x100 && c <= 0x2ff) ||
	       (c >= 0x370 && c <= 0x167f) || (c >= 0x1681 && c <= 0x180d) || (c >= 0x180f && c <= 0x1dbf) ||
	       (c >= 0x1e00 && c <= 0x1fff) || (c >= 0x200b && c <= 0x200d) || (c >= 0x202a && c <= 0x202e) ||
	       (c >= 0x203f && c <= 0x2040) || (c >= 0x2060 && c <= 0x206f) || (c >= 0x2070 && c <= 0x20cf) ||
	       (c >= 0x2100 && c <= 0x218f) || (c >= 0x2460 && c <= 0x24ff) || (c >= 0x2776 && c <= 0x2793) ||
	       (c >= 0x2c00 && c <= 0x2dff) || (c >= 0x2e80 && c <= 0x2fff) || (c >= 0x3004 && c <= 0x3007) ||
	       (c >= 0x3021 && c <= 0x302f) || (c >= 0x3031 && c <= 0x303f) || (c >= 0x3040 && c <= 0xd7ff) ||
	       (c >= 0xf900 && c <= 0xfd3d) || (c >= 0xfd40 && c <= 0xfdcf) || (c >= 0xfdf0 && c <= 0xfe1f) ||
	       (c >= 0xfe30 && c <= 0xfe44) || (c >= 0xfe47 && c <= 0xfffd) ||
	       /* U+10000–U+1FFFD, ..., U+E0000–U+EFFFD */
	       (c >= 0x10000 && c <= 0xefffd && (c & 0xffff) <= 0xfffd);
}

bool sw_is_swift_identifier_char(uint32_t c)
{
	return (c >= 0x30 && c <= 0x39) || (c >= 0x300 && c <= 0x36f) || (c >= 0x1dc0 && c <= 0x1dff) ||
	       (c >= 0x20d0 && c <= 0x20ff) || (c >= 0xfe20 && c <= 0xfe2f) || sw_is_swift_identifier_head(c);
}

bool sw_is_valid_swift_identifier(const char *s, bool allow_quoted)
{
	size_t n = strlen(s);
	if (allow_quoted && n >= 2 && s[0] == '`' && s[n - 1] == '`') {
		s++;
		n -= 2;
	}
	if (n == 1 && s[0] == '_')
		return false;
	if (n == 0)
		return false;
	uint32_t c;
	size_t i = (size_t)sw_next_scalar(s, n, &c);
	if (!sw_is_swift_identifier_head(c))
		return false;
	while (i < n) {
		i += (size_t)sw_next_scalar(s + i, n - i, &c);
		if (!sw_is_swift_identifier_char(c))
			return false;
	}
	return true;
}

static const char *const keywords_declarations[] = {
	"associatedtype", "class", "deinit", "enum", "extension", "fileprivate", "func", "import", "init",
	"inout", "internal", "let", "open", "operator", "private", "protocol", "public", "static", "struct",
	"subscript", "typealias", "var", NULL,
};
static const char *const keywords_statements[] = {
	"break", "case", "continue", "default", "defer", "do", "else", "fallthrough", "for", "guard", "if",
	"in", "repeat", "return", "switch", "where", "while", NULL,
};
static const char *const keywords_expressions_and_types[] = {
	"as", "Any", "catch", "false", "is", "nil", "rethrows", "super", "self", "Self", "throw", "throws",
	"true", "try", NULL,
};
static const char *const common_types[] = {
	"Bool", "Data", "Double", "Float", "Int", "Int32", "Int64", "String", "UInt", "UInt32", "UInt64", NULL,
};
static const char *const special_variables[] = {"__COLUMN__", "__FILE__", "__FUNCTION__", "__LINE__", NULL};

static bool in_list(const char *const *list, const char *s)
{
	for (; *list; list++)
		if (str_eq(*list, s))
			return true;
	return false;
}

static bool is_keyword(const char *s)
{
	return in_list(keywords_declarations, s) || in_list(keywords_statements, s) ||
	       in_list(keywords_expressions_and_types, s);
}

/* ---- NamingUtils.swift ---- */

static bool is_reserved_type_name(const char *s)
{
	static const char *const names[] = {
		"SwiftProtobuf", "Extensions", "protoMessageName", "decodeMessage", "traverse", "isInitialized",
		"unknownFields", "debugDescription", "description", "dynamicType", "hashValue", "Type",
		"Protocol", "Swift", "Equatable", "Hashable", "Sendable", NULL,
	};
	return in_list(names, s) || is_keyword(s) || in_list(common_types, s) || in_list(special_variables, s);
}

static bool is_quotable_field_name(const char *s) { return is_keyword(s); }

static bool is_reserved_field_name(const char *s)
{
	static const char *const names[] = {
		"isInitialized", "unknownFields", "debugDescription", "description", "dynamicType", "hashValue",
		"init", "self", "Type", "Protocol", NULL,
	};
	return in_list(names, s) || in_list(common_types, s) || in_list(special_variables, s);
}

static bool is_quotable_enum_case(const char *s)
{
	static const char *const names[] = {"associativity", "dynamicType", "optional", "required", NULL};
	return in_list(names, s) || is_keyword(s);
}

static bool is_reserved_enum_case(const char *s)
{
	static const char *const names[] = {
		"allCases", "debugDescription", "description", "dynamicType", "hashValue", "init", "rawValue",
		"self", NULL,
	};
	return in_list(names, s);
}

static bool is_all_underscore(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++)
		if (*s != '_')
			return false;
	return true;
}

static char *sanitize_type_name(const char *s, const char *disambiguator, const char *forbidden)
{
	if (is_reserved_type_name(s) || is_all_underscore(s))
		return xcat(s, disambiguator);
	if (has_suffix(s, disambiguator)) {
		char *truncated = xstrndup(s, strlen(s) - strlen(disambiguator));
		return xcat(sanitize_type_name(truncated, disambiguator, forbidden), disambiguator);
	}
	if (str_eq(s, forbidden))
		return xcat(s, disambiguator);
	return xstrdup(s);
}

/* isCharacterUppercase: false past the end. */
static bool is_character_uppercase(const char *s, size_t index) { return strlen(s) > index && s[index] >= 'A' && s[index] <= 'Z'; }

enum { CC_DIGIT, CC_LOWER, CC_UPPER, CC_UNDERSCORE, CC_OTHER };

static int char_class(uint32_t c)
{
	if (c >= '0' && c <= '9')
		return CC_DIGIT;
	if (c >= 'a' && c <= 'z')
		return CC_LOWER;
	if (c >= 'A' && c <= 'Z')
		return CC_UPPER;
	if (c == '_')
		return CC_UNDERSCORE;
	return CC_OTHER;
}

static void add_current(Buf *result, Buf *current, bool initial_upper_case)
{
	if (!current->n)
		return;
	const char *cur = buf_str(current);
	if (!result->n && !initial_upper_case) {
		buf_adds(result, cur);
	} else if (str_eq(cur, "url") || str_eq(cur, "http") || str_eq(cur, "https") || str_eq(cur, "id")) {
		for (const char *p = cur; *p; p++)
			buf_addc(result, asc_upper(*p));
	} else {
		buf_addc(result, asc_upper(cur[0]));
		buf_adds(result, cur + 1);
	}
	current->n = 0;
	current->p[0] = 0;
}

/* CamelCaser.transform */
static char *camel_case(const char *s, bool initial_upper_case)
{
	Buf result = {0}, current = {0};
	int last = CC_OTHER;
	size_t n = strlen(s);
	for (size_t i = 0; i < n;) {
		uint32_t c;
		int w = sw_next_scalar(s + i, n - i, &c);
		int cls = char_class(c);
		switch (cls) {
		case CC_DIGIT:
			if (last != CC_DIGIT)
				add_current(&result, &current, initial_upper_case);
			if (!result.n)
				buf_addc(&result, '_');
			buf_addc(&current, (char)c);
			break;
		case CC_UPPER:
			if (last != CC_UPPER)
				add_current(&result, &current, initial_upper_case);
			buf_addc(&current, asc_lower((char)c));
			break;
		case CC_LOWER:
			if (last != CC_LOWER && last != CC_UPPER)
				add_current(&result, &current, initial_upper_case);
			buf_addc(&current, (char)c);
			break;
		case CC_UNDERSCORE:
			add_current(&result, &current, initial_upper_case);
			if (last == CC_UNDERSCORE)
				buf_addc(&result, '_');
			break;
		default: {
			add_current(&result, &current, initial_upper_case);
			bool escape = !result.n ? !sw_is_swift_identifier_head(c) : !sw_is_swift_identifier_char(c);
			if (escape)
				buf_fmt(&result, "_u%lld", (long long)c);
			else
				buf_add(&current, s + i, (size_t)w);
		}
		}
		last = cls;
		i += (size_t)w;
	}
	add_current(&result, &current, initial_upper_case);
	if (last == CC_UNDERSCORE)
		buf_addc(&result, '_');
	return xstrdup(buf_str(&result));
}

char *sw_type_prefix(const char *package, const PbMsg *file_options)
{
	if (pb_has(file_options, 39 /* swift_prefix */))
		return xstrdup(sw_ascii_name(pb_bytes(file_options, 39), "option swift_prefix"));
	if (!*package)
		return xstrdup("");
	Buf prefix = {0};
	bool make_upper = true;
	for (const char *p = package; *p; p++) {
		char c = *p;
		if (c == '_') {
			make_upper = true;
		} else if (c == '.') {
			make_upper = true;
			buf_addc(&prefix, '_');
		} else {
			if (!prefix.n && c >= '0' && c <= '9')
				buf_addc(&prefix, '_');
			if (make_upper) {
				buf_addc(&prefix, asc_upper(c));
				make_upper = false;
			} else {
				buf_addc(&prefix, c);
			}
		}
	}
	buf_addc(&prefix, '_');
	return xstrdup(buf_str(&prefix));
}

char *sw_strip_prefix(const char *prefix, const char *from)
{
	Buf pb = {0};
	for (const char *p = prefix; *p; p++)
		if (*p != '_')
			buf_addc(&pb, asc_lower(*p));
	const char *pc = buf_str(&pb);
	char *fc = sw_ascii_lower(from);
	size_t pi = 0, fi = 0, pn = strlen(pc), fn = strlen(fc);
	while (pi != pn) {
		if (fi == fn)
			return NULL;
		if (fc[fi] == '_') {
			fi++;
			continue;
		}
		if (pc[pi] != fc[fi])
			return NULL;
		pi++;
		fi++;
	}
	while (fi != fn && fc[fi] == '_')
		fi++;
	if (fi == fn)
		return NULL;
	if (!(fc[fi] >= 'a' && fc[fi] <= 'z'))
		return NULL;
	return xstrdup(from + fi);
}

char *sw_sanitize_message_name(const char *s, const char *forbidden) { return sanitize_type_name(s, "Message", forbidden); }
char *sw_sanitize_enum_name(const char *s, const char *forbidden) { return sanitize_type_name(s, "Enum", forbidden); }
char *sw_sanitize_oneof_name(const char *s, const char *forbidden) { return sanitize_type_name(s, "Oneof", forbidden); }

char *sw_sanitize_field_name(const char *s, const char *based_on)
{
	if (has_prefix(based_on, "clear") && is_character_uppercase(based_on, 5))
		return xcat(s, "_p");
	if (has_prefix(based_on, "has") && is_character_uppercase(based_on, 3))
		return xcat(s, "_p");
	if (is_reserved_field_name(based_on))
		return xcat(s, "_p");
	if (str_eq(based_on, s) && is_quotable_field_name(based_on))
		return xcat("`", s, "`");
	if (is_all_underscore(based_on))
		return xcat(s, "__");
	return xstrdup(s);
}

char *sw_sanitize_enum_case_name(const char *s)
{
	if (is_reserved_enum_case(s))
		return xcat(s, "_");
	if (is_quotable_enum_case(s))
		return xcat("`", s, "`");
	if (is_all_underscore(s))
		return xcat(s, "__");
	return xstrdup(s);
}

/* Message scoped extensions reuse the enum case sets. */
char *sw_sanitize_message_scoped_extension_name(const char *s) { return sw_sanitize_enum_case_name(s); }

char *sw_uppercase_first(const char *s)
{
	char *r = xstrdup(s);
	r[0] = asc_upper(r[0]);
	return r;
}

char *sw_to_upper_camel(const char *s) { return camel_case(s, true); }
char *sw_to_lower_camel(const char *s) { return camel_case(s, false); }

char *sw_trim_backticks(const char *s)
{
	size_t n = strlen(s);
	if (s[0] != '`')
		return xstrdup(s);
	return n < 2 ? xstrdup("") : xstrndup(s + 1, n - 2);
}

char *sw_periods_to_underscores(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p == '.')
			*p = '_';
	return r;
}

char *sw_to_json_field_name(const char *s)
{
	Buf r = {0};
	bool capitalize_next = false;
	for (const char *p = s; *p; p++) {
		if (*p == '_') {
			capitalize_next = true;
		} else if (capitalize_next) {
			buf_addc(&r, asc_upper(*p));
			capitalize_next = false;
		} else {
			buf_addc(&r, *p);
		}
	}
	return xstrdup(buf_str(&r));
}
