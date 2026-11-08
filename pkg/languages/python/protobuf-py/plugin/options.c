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

/* protobuf/plugin/_options.py of protobuf-py v0.7.0: the dataclass option parser, for fields of
 * type bool, str, dict[str, str] and StrEnum (optionally `| None`). */
#include "internal.h"

#include <stdarg.h>
#include <string.h>

NORETURN COLD void pbpy_raise(const char *frames, const char *fmt, ...)
{
	Buf b = {0};
	buf_adds(&b, "Traceback (most recent call last):\n");
	buf_adds(&b, frames);
	buf_adds(&b, "ValueError: ");
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	fail_str(buf_view(&b));
}

#define RAISE_LINE "raise ValueError(msg)"
static const char PARSE_OPTIONS[] = PBPY_FRAME("_options.py", 78, "parse_options",
	"kwargs[field.name] = _parse_field(key, hint, entries, field)")
	"                         ~~~~~~~~~~~~^^^^^^^^^^^^^^^^^^^^^^^^^^^\n";
static const char FIELD_MISSING[] = PBPY_FRAME("_options.py", 101, "_parse_field", RAISE_LINE);
static const char FIELD_DUP[] = PBPY_FRAME("_options.py", 107, "_parse_field", RAISE_LINE);
static const char FIELD_SCALAR[] = PBPY_FRAME("_options.py", 109, "_parse_field", "return _parse_scalar(name, hint, entries[0])");
static const char FIELD_DICT[] = PBPY_FRAME("_options.py", 92, "_parse_field", "return _parse_dict(name, hint, entries)");
static const char DICT_NONE[] = PBPY_FRAME("_options.py", 137, "_parse_dict", RAISE_LINE);
static const char DICT_COLON[] = PBPY_FRAME("_options.py", 140, "_parse_dict", RAISE_LINE);
static const char DICT_DUP[] = PBPY_FRAME("_options.py", 144, "_parse_dict", RAISE_LINE);
static const char SCALAR_BOOL[] = PBPY_FRAME("_options.py", 171, "_parse_scalar", RAISE_LINE);
static const char SCALAR_BARE[] = PBPY_FRAME("_options.py", 176, "_parse_scalar", RAISE_LINE);
static const char SCALAR_UNION[] = PBPY_FRAME("_options.py", 157, "_parse_scalar", "return _parse_scalar(name, inner[0], raw_value)");
static const char SCALAR_ENUM[] = PBPY_FRAME("_options.py", 209, "_parse_scalar", RAISE_LINE);

typedef struct {
	const char *key;
	const char *value; /* NULL: a bare key */
} Pair;

static bool lower_eq(const char *s, const char *lower)
{
	for (; *s && *lower; s++, lower++)
		if ((*s >= 'A' && *s <= 'Z' ? *s + 32 : *s) != *lower)
			return false;
	return !*s && !*lower;
}

static char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* raw.lower() == ascii.lower() for an ASCII string: str.lower() of a non-ASCII character is
 * non-ASCII, except for KELVIN SIGN U+212A, which becomes "k". */
static bool unicode_lower_eq(const char *raw, const char *ascii)
{
	for (; *raw; ascii++) {
		if (!*ascii)
			return false;
		if (!strncmp(raw, "\xe2\x84\xaa", 3)) {
			if (ascii_lower(*ascii) != 'k')
				return false;
			raw += 3;
			continue;
		}
		if (ascii_lower(*raw) != ascii_lower(*ascii))
			return false;
		raw++;
	}
	return !*ascii;
}

/* _parse_scalar; stack: the frames down to its caller */
static void parse_scalar(const PbpyOptDef *d, const char *raw, PbpyOptValue *v, const char *stack)
{
	if (d->optional)
		stack = xcat(stack, SCALAR_UNION);
	if (d->type == PBPY_OPT_BOOL) {
		if (!raw)
			v->b = true;
		else if (lower_eq(raw, "true"))
			v->b = true;
		else if (lower_eq(raw, "false"))
			v->b = false;
		else
			pbpy_raise(xcat(stack, SCALAR_BOOL), "option '%s': cannot parse '%s' as bool; use 'true' or 'false'", d->name, raw);
		return;
	}
	if (!raw)
		pbpy_raise(xcat(stack, SCALAR_BARE), "option '%s' requires a value (bare key only valid for bool fields)", d->name);
	if (d->type != PBPY_OPT_STR_ENUM) {
		v->s = raw;
		return;
	}
	/* a StrEnum: a member by name or value, case-insensitive */
	Buf allowed = {0};
	for (const PbpyOptMember *m = d->members; m->name; m++) {
		if (unicode_lower_eq(raw, m->name) || unicode_lower_eq(raw, m->value)) {
			v->s = m->value;
			return;
		}
		if (allowed.n)
			buf_adds(&allowed, ", ");
		buf_adds(&allowed, m->name);
	}
	pbpy_raise(xcat(stack, SCALAR_ENUM), "option '%s': '%s' is not a valid %s; allowed: %s", d->name, raw, d->enum_name, buf_str(&allowed));
}

COLD const char *pbpy_parse_options(const PbpyOptDef *defs, int n, Str parameter, PbpyOptValue *values, const char *stack)
{
	stack = xcat(stack, PARSE_OPTIONS);
	Vec pairs = {0};
	if (parameter.n) {
		Str *tokens;
		int nt = str_split_s(parameter, ',', &tokens);
		for (int i = 0; i < nt; i++) {
			Str t = tokens[i];
			const char *eq = memchr(t.p, '=', t.n);
			Pair *p = xalloc(sizeof *p);
			Str key = eq ? (Str){t.p, (size_t)(eq - t.p)} : t;
			key = py_strip(key);
			p->key = xstrndup(key.p, key.n);
			p->value = eq ? xstrndup(eq + 1, t.n - (size_t)(eq + 1 - t.p)) : NULL;
			vec_push(&pairs, p);
		}
	}
	Vec unknown = {0};
	Vec *grouped = xalloc(sizeof *grouped * (size_t)(n ? n : 1));
	for (int i = 0; i < pairs.n; i++) {
		Pair *p = pairs.v[i];
		int k = 0;
		while (k < n && !str_eq(defs[k].name, p->key))
			k++;
		if (k == n)
			vec_push(&unknown, p->value ? xcat(p->key, "=", p->value) : (char *)p->key);
		else
			vec_push(&grouped[k], p);
	}
	for (int k = 0; k < n; k++) {
		const PbpyOptDef *d = &defs[k];
		PbpyOptValue *v = &values[k];
		Vec *entries = &grouped[k];
		if (d->type == PBPY_OPT_DICT_STR) {
			Map seen = {0};
			for (int i = 0; i < entries->n; i++) {
				const char *raw = ((Pair *)entries->v[i])->value;
				if (!raw)
					pbpy_raise(xcat(stack, FIELD_DICT, DICT_NONE), "option '%s' requires a value in the form '%s=key:value'", d->name, d->name);
				const char *colon = strchr(raw, ':');
				if (!colon)
					pbpy_raise(xcat(stack, FIELD_DICT, DICT_COLON), "option '%s' value '%s' must contain ':' to separate key and value", d->name, raw);
				char *key = xstrndup(raw, (size_t)(colon - raw));
				if (map_has(&seen, key))
					pbpy_raise(xcat(stack, FIELD_DICT, DICT_DUP), "option '%s' has duplicate key '%s'", d->name, key);
				map_put(&seen, key, (void *)1);
				vec_push(&v->keys, key);
				vec_push(&v->values, (void *)(colon + 1));
			}
			continue;
		}
		if (!entries->n) {
			if (d->type == PBPY_OPT_BOOL)
				v->b = d->default_bool;
			else if (d->default_str || d->optional)
				v->s = d->default_str;
			else
				pbpy_raise(xcat(stack, FIELD_MISSING), "missing required option '%s'", d->name);
			continue;
		}
		if (entries->n > 1)
			pbpy_raise(xcat(stack, FIELD_DUP), "option '%s' specified %d times but expects a single value", d->name, entries->n);
		parse_scalar(d, ((Pair *)entries->v[0])->value, v, xcat(stack, FIELD_SCALAR));
	}
	return str_join(&unknown, ",");
}
