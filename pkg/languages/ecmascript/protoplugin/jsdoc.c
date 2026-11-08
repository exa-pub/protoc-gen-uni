/* Copyright 2021-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/protoplugin/src/jsdoc.ts. */
#include "internal.h"

/* The text of a JSDoc comment for a descriptor: the comments from the protobuf source, and
 * annotations such as `@generated from message MyMessage`. */
Str jsdoc_text_from_desc(const Desc *d)
{
	Comments c = get_comments(d);
	Buf t = {0};
	if (c.leading.p)
		buf_add_str(&t, str_trim_suffix_s(c.leading, STR_LIT("\n")));
	if (c.trailing.p) {
		if (t.n)
			buf_adds(&t, "\n\n");
		buf_add_str(&t, str_trim_suffix_s(c.trailing, STR_LIT("\n")));
	}
	if (t.n)
		buf_adds(&t, "\n\n");
	/* remove one leading space of every line */
	Buf b = {0};
	Str *lines;
	int n = str_split_s(buf_view(&t), '\n', &lines);
	for (int i = 0; i < n; i++) {
		if (i)
			buf_addc(&b, '\n');
		Str l = lines[i];
		if (l.n && l.p[0] == ' ')
			l.p++, l.n--;
		buf_add_str(&b, l);
	}
	const char *kind = desc_kind(d);
	if (d->kind == DK_ENUM_VALUE || d->kind == DK_FIELD) {
		buf_adds(&b, xcat("@generated from ", d->kind == DK_ENUM_VALUE ? "enum value" : kind, ": "));
		buf_add_str(&b, declaration_string(d));
		buf_addc(&b, ';');
	} else if (d->kind == DK_MESSAGE || d->kind == DK_ENUM) {
		buf_adds(&b, xcat("@generated from ", desc_string(d)));
		Vec fo = feature_option_strings(d);
		if (fo.n)
			buf_adds(&b, xcat("\n@generated with ", fo.n > 1 ? "options " : "option ", str_join(&fo, ", ")));
	} else {
		buf_adds(&b, xcat("@generated from ", desc_string(d)));
	}
	bool deprecated = d->kind == DK_FIELD || d->kind == DK_ENUM_VALUE || d->kind == DK_METHOD
				  ? d->deprecated
				  : desc_deprecated_with_parents(d);
	if (deprecated)
		buf_adds(&b, "\n@deprecated");
	return buf_view(&b);
}

/* text.trim().length == 0; only ASCII whitespace is considered, because the text of every JSDoc
 * comment contains an annotation. */
static bool is_blank(Str s)
{
	for (size_t i = 0; i < s.n; i++) {
		char c = s.p[i];
		if (!(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'))
			return false;
	}
	return true;
}

/* Formats text as a JSDoc comment block. */
Str format_jsdoc_block(Str text, const char *indent)
{
	if (is_blank(text))
		return (Str){"", 0};
	Buf b = {0};
	buf_adds(&b, indent);
	buf_adds(&b, "/**\n");
	Str *lines;
	int n = str_split_s(text, '\n', &lines);
	for (int i = 0; i < n; i++) {
		buf_adds(&b, indent);
		buf_adds(&b, " *");
		if (lines[i].n)
			buf_addc(&b, ' ');
		/* l.replace(/\*\//g, "*\\/") */
		Str l = lines[i];
		for (size_t j = 0; j < l.n; j++) {
			if (l.p[j] == '*' && j + 1 < l.n && l.p[j + 1] == '/') {
				buf_adds(&b, "*\\/");
				j++;
			} else {
				buf_addc(&b, l.p[j]);
			}
		}
		buf_addc(&b, '\n');
	}
	buf_adds(&b, indent);
	buf_adds(&b, " */");
	return buf_view(&b);
}
