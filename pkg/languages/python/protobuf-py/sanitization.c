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

/* _sanitization.py of protobuf-py v0.7.0. */
#include "protobuf_py.h"

#include <string.h>

const char *const pbpy_python_keywords[] = {
	"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue",
	"def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in",
	"is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
	NULL,
};

/* _DISALLOWED_CLASS_NAMES */
static const char *const disallowed_class_names[] = {"int", "str", "bytes", "float", "bool", "list", "dict", NULL};
/* _RESERVED_MESSAGE_ATTRS without the keywords */
static const char *const message_attrs[] = {
	"has_field", "clear_field", "to_binary", "to_json", "from_binary", "from_json", "desc", "self", NULL,
};
/* _RESERVED_ENUM_ATTRS without the keywords */
static const char *const enum_attrs[] = {"name", "value", "mro", "desc", NULL};

static bool in_list(const char *const *list, const char *s, size_t n)
{
	for (; *list; list++)
		if (strlen(*list) == n && !memcmp(*list, s, n))
			return true;
	return false;
}

/* len(s.rstrip("_")) */
static size_t rstrip_underscores(const char *s)
{
	size_t n = strlen(s);
	while (n && s[n - 1] == '_')
		n--;
	return n;
}

/* ident.rstrip("_") in the reserved sets (each a NULL-terminated list; up to three) */
static bool reserved(const char *ident, const char *const *a, const char *const *b, const char *const *c)
{
	size_t n = rstrip_underscores(ident);
	return in_list(a, ident, n) || (b && in_list(b, ident, n)) || (c && in_list(c, ident, n));
}

static char *escape_public(const char *ident, const char *const *a, const char *const *b, const char *const *c)
{
	const char *s = reserved(ident, a, b, c) ? xcat(ident, "_") : ident;
	if (*s == '_' || has_prefix(s, "pb_"))
		return xcat("pb_", s);
	return xstrdup(s);
}

COLD char *pbpy_escape_public_identifier(const char *ident, const char *const *list)
{
	return escape_public(ident, list, NULL, NULL);
}

COLD char *pbpy_escape_proto_module_component(const char *ident, bool escape_with_hash)
{
	/* _INVALID_MODULE_COMPONENT_RE.sub("_", ident), per code point */
	Buf b = {0};
	bool changed = false;
	size_t n = strlen(ident);
	for (size_t i = 0; i < n;) {
		unsigned char c = (unsigned char)ident[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			buf_addc(&b, (char)c);
			i++;
			continue;
		}
		uint32_t r;
		i += (size_t)utf8_decode(ident + i, n - i, &r);
		buf_addc(&b, '_');
		changed = true;
	}
	const char *component = buf_str(&b);
	if (changed && escape_with_hash) {
		char *hash = py_sha256_hex(str_c(ident));
		hash[8] = 0;
		component = xcat(component, "_", hash);
	}
	if (!*component)
		fail("IndexError: string index out of range");
	if (*component >= '0' && *component <= '9')
		component = xcat("_", component);
	char *out = escape_public(component, pbpy_python_keywords, NULL, NULL);
	/* the _pb suffix is reserved for generated modules */
	size_t m = rstrip_underscores(out);
	if (m >= 3 && !memcmp(out + m - 3, "_pb", 3))
		return xcat(out, "_");
	return out;
}

COLD char *pbpy_escape_message_attr(const char *attr)
{
	char *name = escape_public(attr, message_attrs, pbpy_python_keywords, NULL);
	size_t n = strlen(name);
	/* only escape a suffixed value that collides with a reserved class name */
	if (n && name[n - 1] == '_' && in_list(disallowed_class_names, name, rstrip_underscores(name)))
		return xcat(name, "_");
	if (has_prefix(name, "ext_"))
		return xcat("pb_", name);
	return name;
}

COLD char *pbpy_escape_enum_attr(const char *attr) { return escape_public(attr, enum_attrs, pbpy_python_keywords, NULL); }

COLD char *pbpy_escape_class_name(const char *name)
{
	/* _RESERVED_CLASS_NAMES = disallowed class names | keywords | reserved message attributes */
	char *s = escape_public(name, disallowed_class_names, pbpy_python_keywords, message_attrs);
	if (has_prefix(s, "ext_"))
		return xcat("pb_", s);
	return s;
}

COLD char *pbpy_escape_extension_name(const char *name) { return xcat("ext_", name); }

COLD char *pbpy_escape_identifier(const char *ident)
{
	if (reserved(ident, pbpy_python_keywords, NULL, NULL))
		return xcat(ident, "_");
	return xstrdup(ident);
}
