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

/* A port of kotlinpoet/src/jvmMain/kotlin/com/squareup/kotlinpoet/CodeBlock.kt of KotlinPoet 2.4.0
 * (CodeBlock, CodeBlock.Builder.add and addNamed with their format errors, trim, withoutPrefix,
 * joinToCode) and of trimTrailingNewLine from Util.jvm.kt. */
#include "internal.h"

#include <string.h>

KpCode *kp_code(void) { return xalloc(sizeof(KpCode)); }

KpCode *kp_code_new(Vec parts, Vec args)
{
	KpCode *c = kp_code();
	c->parts = parts, c->args = args;
	return c;
}

/* isSingleCharNoArgPlaceholder of the character at s */
static int kp_placeholder_at(const char *s)
{
	if (!strncmp(s, KP_INDENT, 3) || !strncmp(s, KP_UNINDENT, 3))
		return 3;
	if (!strncmp(s, KP_OPEN, 2) || !strncmp(s, KP_CLOSE, 2))
		return 2;
	return 0;
}

bool kp_is_single_char_placeholder(const char *part)
{
	int n = kp_placeholder_at(part);
	return n && !part[n];
}

bool kp_part_is_placeholder(const char *part)
{
	return kp_is_single_char_placeholder(part) || (part[0] == '%' && part[1] && !part[1 + kp_utf8_width(part + 1)]);
}

/* nextPotentialPlaceholderPosition: the next '%', '«', '»', '⇥' or '⇤' at or after s, or NULL */
static const char *kp_next_placeholder(const char *s)
{
	for (; *s; s++)
		if (*s == '%' || kp_placeholder_at(s))
			return s;
	return NULL;
}

/* the format substring of [from, to) */
static const char *kp_sub(const char *from, const char *to) { return xstrndup(from, (size_t)(to - from)); }

/* addArgument(format, c, arg) and the part of the placeholder */
static void kp_add_argument(KpCode *b, const char *format, char k, const KpArg *a)
{
	if (k != 'N' && k != 'L' && k != 'S' && k != 'T' && k != 'M')
		kp_illegal_argument(xfmt("invalid format string: '%s'", format));
	if ((k == 'T') != (a->kind == 'T') || (k == 'M') != (a->kind == 'M') || (k == 'N' && a->kind != 'N' && !a->s) ||
	    (k == 'S' && a->code))
		fail("kotlinpoet: argument kind %c for %%%c is not supported", a->kind, k);
	KpArg *arg = xalloc(sizeof *arg);
	*arg = *a;
	arg->kind = k;
	if (k == 'N')
		arg->s = kp_escape(a->s);
	vec_push(&b->args, arg);
	vec_push(&b->parts, (void *)xfmt("%%%c", k));
}

void kp_add(KpCode *b, const char *format, int nargs, const KpArg *args)
{
	bool has_relative = false, has_indexed = false;
	int relative_count = 0;
	int *indexed_count = xalloc(sizeof(int) * (size_t)(nargs ? nargs : 1));
	const char *p = format;
	while (*p) {
		int ph = kp_placeholder_at(p);
		if (ph) {
			vec_push(&b->parts, (void *)kp_sub(p, p + ph));
			p += ph;
			continue;
		}
		if (*p != '%') {
			const char *next = kp_next_placeholder(p + 1);
			if (!next)
				next = p + strlen(p);
			vec_push(&b->parts, (void *)kp_sub(p, next));
			p = next;
			continue;
		}
		p++; /* '%' */
		const char *index_start = p, *c;
		do {
			if (!*p)
				kp_illegal_argument(xfmt("dangling format characters in '%s'", format));
			c = p;
			p += kp_utf8_width(p);
		} while (*c >= '0' && *c <= '9');
		const char *index_end = c;
		if (*c == '%') {
			if (index_start != index_end)
				kp_illegal_argument("%% may not have an index");
			vec_push(&b->parts, (void *)"%%");
			continue;
		}
		long long index;
		if (index_start < index_end) {
			/* Integer.parseInt */
			index = 0;
			for (const char *d = index_start; d < index_end; d++) {
				index = index * 10 + (*d - '0');
				if (index > 2147483647LL)
					java_exception("java.lang.NumberFormatException",
						       xfmt("For input string: \"%s\"", kp_sub(index_start, index_end)));
			}
			index--;
			has_indexed = true;
			if (nargs) {
				long long m = index % nargs;
				if (m < 0)
					java_exception("java.lang.ArrayIndexOutOfBoundsException",
						       xfmt("Index %lld out of bounds for length %d", m, nargs));
				indexed_count[m]++;
			}
		} else {
			index = relative_count++;
			has_relative = true;
		}
		if (index < 0 || index >= nargs)
			kp_illegal_argument(xfmt("index %lld for '%s' not in range (received %d arguments)", index + 1,
						 kp_sub(index_start - 1, p), nargs));
		if (has_indexed && has_relative)
			kp_illegal_argument("cannot mix indexed and positional parameters");
		kp_add_argument(b, format, *c, &args[index]);
	}
	if (has_relative && relative_count < nargs)
		kp_illegal_argument(xfmt("unused arguments: expected %d, received %d", relative_count, nargs));
	if (has_indexed) {
		Vec unused = {0};
		for (int i = 0; i < nargs; i++)
			if (!indexed_count[i])
				vec_push(&unused, xfmt("%%%d", i + 1));
		if (unused.n)
			kp_illegal_argument(xfmt("unused argument%s: %s", unused.n == 1 ? "" : "s", str_join(&unused, ", ")));
	}
}

/* \w of java.util.regex: [a-zA-Z_0-9] */
static bool kp_is_word(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

void kp_add_named(KpCode *b, const char *format, int nargs, const KpNamedArg *args)
{
	for (int i = 0; i < nargs; i++) {
		/* LOWERCASE: [a-z]+[\w_]* */
		const char *n = args[i].name;
		bool ok = *n >= 'a' && *n <= 'z';
		for (const char *c = n; ok && *c; c++)
			ok = kp_is_word(*c);
		if (!ok)
			kp_illegal_argument(xcat("argument '", n, "' must start with a lowercase character"));
	}
	size_t len = strlen(format);
	const char *p = format, *end = format + len;
	while (p < end) {
		const char *next = kp_next_placeholder(p);
		if (!next) {
			vec_push(&b->parts, (void *)kp_sub(p, end));
			break;
		}
		if (p != next) {
			vec_push(&b->parts, (void *)kp_sub(p, next));
			p = next;
		}
		/* NAMED_ARGUMENT %([\w_]+):([\w]).* matched on format[p, colon + 2) */
		const char *colon = strchr(p, ':');
		const char *name_end = p + 1;
		while (name_end < end && kp_is_word(*name_end))
			name_end++;
		if (colon && *p == '%' && name_end > p + 1 && name_end == colon && colon + 1 < end && kp_is_word(colon[1])) {
			const char *name = kp_sub(p + 1, colon);
			const KpArg *a = NULL;
			for (int i = 0; i < nargs && !a; i++)
				if (str_eq(args[i].name, name))
					a = &args[i].arg;
			if (!a)
				kp_illegal_argument(xcat("Missing named argument for %", name));
			kp_add_argument(b, format, colon[1], a);
			p = colon + 2;
		} else if (kp_placeholder_at(p)) {
			int n = kp_placeholder_at(p);
			vec_push(&b->parts, (void *)kp_sub(p, p + n));
			p += n;
		} else {
			if (p + 1 >= end)
				kp_illegal_argument("dangling % at end");
			if (p[1] != '%')
				kp_illegal_argument(xfmt("unknown format %%%s at %lld in '%s'", kp_sub(p + 1, p + 1 + kp_utf8_width(p + 1)),
							 (long long)kp_utf16_len(format, (size_t)(p + 1 - format)), format));
			vec_push(&b->parts, (void *)kp_sub(p, p + 2));
			p += 2;
		}
	}
}

void kp_add_code(KpCode *b, const KpCode *c)
{
	for (int i = 0; i < c->parts.n; i++)
		vec_push(&b->parts, c->parts.v[i]);
	for (int i = 0; i < c->args.n; i++)
		vec_push(&b->args, c->args.v[i]);
}

void kp_add_statement(KpCode *b, const char *format, int nargs, const KpArg *args)
{
	kp_add0(b, KP_OPEN);
	kp_add(b, format, nargs, args);
	kp_add0(b, "\n" KP_CLOSE);
}

void kp_indent(KpCode *b) { vec_push(&b->parts, (void *)KP_INDENT); }
void kp_unindent(KpCode *b) { vec_push(&b->parts, (void *)KP_UNINDENT); }

KpCode *kp_code_of(const char *format, int nargs, const KpArg *args)
{
	KpCode *c = kp_code();
	kp_add(c, format, nargs, args);
	return c;
}

static KpCode *kp_code_slice(const KpCode *c, int from, int to)
{
	Vec parts = {0};
	for (int i = from; i < to; i++)
		vec_push(&parts, c->parts.v[i]);
	return kp_code_new(parts, c->args);
}

KpCode *kp_code_trim(const KpCode *c, bool keep_balanced)
{
	int n = c->parts.n, keep_from = 0, keep_until = n;
	while (keep_from < keep_until && kp_is_single_char_placeholder(c->parts.v[keep_from]))
		keep_from++;
	while (keep_from < keep_until && kp_is_single_char_placeholder(c->parts.v[keep_until - 1]))
		keep_until--;
	if (keep_balanced && (keep_from > 0 || keep_until < n)) {
		char *ph = xalloc((size_t)n + 1);
		int count = 0;
		for (int i = 0; i < n; i++) {
			const char *part = c->parts.v[i];
			if (kp_is_single_char_placeholder(part))
				ph[count++] = !strcmp(part, KP_INDENT) ? 'i' : !strcmp(part, KP_UNINDENT) ? 'u' : !strcmp(part, KP_OPEN) ? 'o' : 'c';
		}
		int ph_from = keep_from, ph_until = count - (n - keep_until);
		int indent = 0, statement = 0;
		bool balanced = true;
		for (int i = 0; i < count && balanced; i++) {
			indent += ph[i] == 'i' ? 1 : ph[i] == 'u' ? -1 : 0;
			statement += ph[i] == 'o' ? 1 : ph[i] == 'c' ? -1 : 0;
			if (indent < 0 || statement < 0)
				balanced = false;
		}
		if (balanced && indent == 0 && statement == 0) {
			int indent_low = 0, indent_total = 0, statement_low = 0, statement_total = 0;
			for (int i = ph_from; i < ph_until; i++) {
				indent_total += ph[i] == 'i' ? 1 : ph[i] == 'u' ? -1 : 0;
				statement_total += ph[i] == 'o' ? 1 : ph[i] == 'c' ? -1 : 0;
				if (indent_total < indent_low)
					indent_low = indent_total;
				if (statement_total < statement_low)
					statement_low = statement_total;
			}
			int pointer = ph_from;
			while (pointer > 0 && (indent_low < 0 || statement_low < 0)) {
				switch (ph[pointer - 1]) {
				case 'i': indent_low++, indent_total++; break;
				case 'u': indent_low--, indent_total--; break;
				case 'o': statement_low++, statement_total++; break;
				case 'c': statement_low--, statement_total--; break;
				}
				pointer--;
				if (indent_low >= 0 && statement_low >= 0) {
					keep_from -= ph_from - pointer;
					break;
				}
			}
			pointer = ph_until;
			while (pointer < count && (indent_total > 0 || statement_total > 0)) {
				indent_total += ph[pointer] == 'i' ? 1 : ph[pointer] == 'u' ? -1 : 0;
				statement_total += ph[pointer] == 'o' ? 1 : ph[pointer] == 'c' ? -1 : 0;
				if (indent_total < 0 || statement_total < 0)
					break;
				pointer++;
				if (indent_total == 0 && statement_total == 0) {
					keep_until += pointer - ph_until;
					break;
				}
			}
		}
	}
	if (keep_from > 0 || keep_until < n)
		return kp_code_slice(c, keep_from, keep_until);
	return (KpCode *)c;
}

/* Argument equality of withoutPrefix (the arguments of the prefixes used are none). */
static bool kp_arg_equals(const KpArg *a, const KpArg *b)
{
	return a->kind == b->kind && str_eq(kp_arg_to_string(a), kp_arg_to_string(b));
}

KpCode *kp_code_without_prefix(const KpCode *c, const KpCode *prefix)
{
	if (c->parts.n < prefix->parts.n || c->args.n < prefix->args.n)
		return NULL;
	int prefix_args = 0;
	const char *first = NULL;
	for (int i = 0; i < prefix->parts.n; i++) {
		const char *fp = prefix->parts.v[i], *p = c->parts.v[i];
		if (!str_eq(p, fp)) {
			if (i == prefix->parts.n - 1 && has_prefix(p, fp))
				first = p + strlen(fp);
			else
				return NULL;
		}
		if (fp[0] == '%' && fp[1] != '%') {
			if (!kp_arg_equals(c->args.v[prefix_args], prefix->args.v[prefix_args]))
				return NULL;
			prefix_args++;
		}
	}
	Vec parts = {0}, args = {0};
	if (first)
		vec_push(&parts, (void *)first);
	for (int i = prefix->parts.n; i < c->parts.n; i++)
		vec_push(&parts, c->parts.v[i]);
	for (int i = prefix->args.n; i < c->args.n; i++)
		vec_push(&args, c->args.v[i]);
	return kp_code_new(parts, args);
}

bool kp_code_has_statements(const KpCode *c)
{
	for (int i = 0; i < c->parts.n; i++)
		if (strstr(c->parts.v[i], KP_OPEN))
			return true;
	return false;
}

bool kp_code_has_unmatched_closing_statement(const KpCode *c)
{
	int open = 0;
	for (int i = 0; i < c->parts.n; i++) {
		if (!strcmp(c->parts.v[i], KP_OPEN)) {
			open++;
		} else if (!strcmp(c->parts.v[i], KP_CLOSE)) {
			if (!open)
				return true;
			open--;
		}
	}
	return false;
}

static const char *kp_trim_end_newlines(const char *s)
{
	size_t n = strlen(s);
	while (n && s[n - 1] == '\n')
		n--;
	return xstrndup(s, n);
}

KpCode *kp_code_trim_trailing_newline(const KpCode *c, char replace_with)
{
	if (kp_code_is_empty(c))
		return (KpCode *)c;
	KpCode *b = kp_code();
	kp_add_code(b, c);
	KpCode *t = kp_code_trim(c, false);
	if (!t->parts.n)
		java_exception("java.util.NoSuchElementException", "List is empty.");
	const char *last = t->parts.v[t->parts.n - 1];
	if (kp_part_is_placeholder(last) && b->args.n) {
		KpArg *a = b->args.v[b->args.n - 1];
		if (a->s && !a->code && a->kind != 'T') {
			KpArg *na = xalloc(sizeof *na);
			*na = *a;
			const char *trimmed = kp_trim_end_newlines(a->s);
			na->s = replace_with ? xfmt("%s%c", trimmed, replace_with) : trimmed;
			b->args.v[b->args.n - 1] = na;
		}
	} else {
		for (int i = b->parts.n - 1; i >= 0; i--)
			if (str_eq(b->parts.v[i], last)) {
				b->parts.v[i] = (void *)kp_trim_end_newlines(last);
				break;
			}
		if (replace_with)
			vec_push(&b->parts, (void *)xfmt("%c", replace_with));
	}
	return b;
}

KpCode *kp_code_ensure_ends_with_newline(const KpCode *c) { return kp_code_trim_trailing_newline(c, '\n'); }

KpCode *kp_join_to_code(const Vec *blocks, const char *separator, const char *prefix, const char *suffix)
{
	Buf f = {0};
	buf_adds(&f, prefix);
	KpArg *args = xalloc(sizeof(KpArg) * (size_t)(blocks->n ? blocks->n : 1));
	for (int i = 0; i < blocks->n; i++) {
		if (i)
			buf_adds(&f, separator);
		buf_adds(&f, "%L");
		args[i] = kp_arg_code(blocks->v[i]);
	}
	buf_adds(&f, suffix);
	return kp_code_of(buf_str(&f), blocks->n, args);
}

const char *kp_arg_to_string(const KpArg *a)
{
	if (a->kind == 'M')
		return a->member->canonical;
	if (a->kind == 'T')
		return kp_type_to_string(a->type);
	if (a->code)
		return kp_code_to_string(a->code);
	return a->s ? a->s : "null";
}
