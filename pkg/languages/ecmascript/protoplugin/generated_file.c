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

/* Ported from packages/protoplugin/src/generated-file.ts and printable.ts: a generated file is a
 * list of elements (text, import symbols, exported declarations); imports are resolved when the
 * content is created. */
#include "internal.h"

#include <string.h>

enum { EL_STR, EL_SYM, EL_EXPORT };
typedef struct {
	int kind;
	Str s;            /* EL_STR: text; EL_EXPORT: name */
	const char *decl; /* EL_EXPORT */
	ImportSymbol *sym;
} El;

GeneratedFile *gf_new(Schema *s, const char *name)
{
	if (!s->target)
		fail("prepareGenerate() must be called before generateFile()");
	GeneratedFile *f = xalloc(sizeof *f);
	f->schema = s;
	f->name = name;
	f->import_path = derive_import_path(name);
	f->legacy_commonjs = s->target == TARGET_JS && s->opts.legacy_commonjs;
	f->rt = s->runtime;
	vec_push(&s->generated, f);
	return f;
}

void gf_preamble(GeneratedFile *f, DescFile *file) { f->preamble = make_file_preamble(f->schema, file); }

static void push_el(GeneratedFile *f, int kind, Str s, const char *decl, ImportSymbol *sym)
{
	El *e = xalloc(sizeof *e);
	e->kind = kind;
	e->s = s;
	e->decl = decl;
	e->sym = sym;
	vec_push(&f->el, e);
}

void gf_str(GeneratedFile *f, Str s)
{
	if (s.n)
		push_el(f, EL_STR, s, NULL, NULL);
}

void gf_s(GeneratedFile *f, const char *s) { gf_str(f, str_c(s)); }

void gf_sf(GeneratedFile *f, const char *fmt, ...)
{
	Buf b = {0};
	va_list ap;
	va_start(ap, fmt);
	buf_vfmt(&b, fmt, ap);
	va_end(ap);
	gf_str(f, buf_view(&b));
}

void gf_nl(GeneratedFile *f) { gf_s(f, "\n"); }
void gf_sym(GeneratedFile *f, ImportSymbol *sym) { push_el(f, EL_SYM, (Str){"", 0}, NULL, sym); }
void gf_export(GeneratedFile *f, const char *decl, const char *name) { push_el(f, EL_EXPORT, str_c(name), decl, NULL); }

Str escape_string(Str v)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < v.n; i++) {
		switch (v.p[i]) {
		case '\\': buf_adds(&b, "\\\\"); break;
		case '"': buf_adds(&b, "\\\""); break;
		case '\r': buf_adds(&b, "\\r"); break;
		case '\n': buf_adds(&b, "\\n"); break;
		default: buf_addc(&b, v.p[i]);
		}
	}
	buf_addc(&b, '"');
	return buf_view(&b);
}

void gf_lit(GeneratedFile *f, Str s) { gf_str(f, escape_string(s)); }
void gf_jsdoc_text(GeneratedFile *f, Str text, const char *indent) { gf_str(f, format_jsdoc_block(text, indent)); }
void gf_jsdoc(GeneratedFile *f, const Desc *d, const char *indent) { gf_jsdoc_text(f, jsdoc_text_from_desc(d), indent); }

static ImportSymbol *import_desc(GeneratedFile *f, const Desc *d, int which, bool type_only)
{
	static const char *const what[] = {"descriptor", "shape", "json type", "valid type"};
	DescFile *file = desc_file_of(d);
	all_names(file);
	const char *names[] = {d->n_desc, d->n_shape, d->n_json, d->n_valid};
	if (!names[which])
		fail("unable to determine unique %s name for %s", what[which], desc_string(d));
	return new_import_symbol(names[which], generate_file_path(f->schema, file), type_only);
}

ImportSymbol *gf_import_schema(GeneratedFile *f, const Desc *d, bool type_only) { return import_desc(f, d, 0, type_only); }
ImportSymbol *gf_import_shape(GeneratedFile *f, const Desc *d) { return import_desc(f, d, 1, true); }
ImportSymbol *gf_import_json(GeneratedFile *f, const Desc *d) { return import_desc(f, d, 2, true); }
ImportSymbol *gf_import_valid(GeneratedFile *f, const Desc *d) { return import_desc(f, d, 3, true); }

/* ---- imports ---- */

typedef struct {
	const char *name, *alias; /* alias NULL if none */
} ImportName;

/* Insertion-ordered name -> alias map. */
typedef struct {
	Vec names; /* ImportName* */
	Map idx;
} OrderedNames;

static void on_set(OrderedNames *m, const char *name, const char *alias)
{
	ImportName *n = map_get(&m->idx, name);
	if (!n) {
		n = xalloc(sizeof *n);
		n->name = name;
		map_put(&m->idx, name, n);
		vec_push(&m->names, n);
	}
	n->alias = alias;
}

/* Stable sort by name with localeCompare (Array.prototype.sort is stable): a merge sort of
 * v[lo:hi] using tmp. */
static void merge_sort(void **v, void **tmp, int lo, int hi)
{
	if (hi - lo < 2)
		return;
	int mid = lo + (hi - lo) / 2;
	merge_sort(v, tmp, lo, mid);
	merge_sort(v, tmp, mid, hi);
	int i = lo, j = mid, k = lo;
	while (i < mid && j < hi)
		tmp[k++] = locale_compare(((ImportName *)v[j])->name, ((ImportName *)v[i])->name) < 0 ? v[j++] : v[i++];
	while (i < mid)
		tmp[k++] = v[i++];
	while (j < hi)
		tmp[k++] = v[j++];
	for (k = lo; k < hi; k++)
		v[k] = tmp[k];
}

static void sort_names(Vec *v)
{
	if (v->n > 1)
		merge_sort(v->v, xalloc(sizeof(void *) * (size_t)v->n), 0, v->n);
}

typedef struct {
	const char *from;
	OrderedNames types, values;
} ImportGroup;

static void write_import(Buf *c, bool legacy, bool type_only, const char *from, Vec *names)
{
	sort_names(names);
	Vec p = {0};
	for (int i = 0; i < names->n; i++) {
		const ImportName *n = names->v[i];
		vec_push(&p, n->alias ? xcat(n->name, legacy ? ": " : " as ", n->alias) : (void *)n->name);
	}
	if (legacy)
		buf_adds(c, xcat("const { ", str_join(&p, ", "), " } = require("));
	else
		buf_adds(c, xcat(type_only ? "import type { " : "import { ", str_join(&p, ", "), " } from "));
	buf_add_str(c, escape_string(str_c(from)));
	buf_adds(c, legacy ? ");\n" : ";\n");
}

/* processImports: assigns an identifier to every import symbol (with an alias if the name is
 * taken) and writes the import statements, grouped by module in order of first use. */
static Map process_imports(GeneratedFile *f, Buf *c, bool *wrote_imports)
{
	Map ident = {0}, is_value = {0}, taken = {0};
	Vec foreign = {0};
	for (int i = 0; i < f->el.n; i++) {
		El *e = f->el.v[i];
		if (e->kind == EL_SYM) {
			ImportSymbol *s = e->sym;
			map_put(&ident, s->id, (void *)s->name);
			if (!s->type_only)
				map_put(&is_value, s->id, (void *)1);
			if (str_eq(s->from, f->import_path))
				map_put(&taken, s->name, (void *)1);
			else
				vec_push(&foreign, s);
		} else if (e->kind == EL_EXPORT) {
			map_put(&taken, e->s.p, (void *)1);
		}
	}
	Map handled = {0};
	for (int i = 0; i < foreign.n; i++) {
		ImportSymbol *s = foreign.v[i];
		if (map_has(&handled, s->id))
			continue;
		map_put(&handled, s->id, (void *)1);
		if (!map_has(&taken, s->name)) {
			map_put(&taken, s->name, (void *)1);
			continue;
		}
		const char *alias;
		for (int k = 1;; k++) {
			/* We choose '$' because it is invalid in proto identifiers. */
			alias = xfmt("%s$%d", s->name, k);
			if (!map_has(&taken, alias))
				break;
		}
		map_put(&taken, alias, (void *)1);
		map_put(&ident, s->id, (void *)alias);
	}
	Map groups = {0};
	Vec group_order = {0};
	for (int i = 0; i < foreign.n; i++) {
		ImportSymbol *s = foreign.v[i];
		ImportGroup *g = map_get(&groups, s->from);
		if (!g) {
			g = xalloc(sizeof *g);
			g->from = s->from;
			map_put(&groups, s->from, g);
			vec_push(&group_order, g);
		}
		const char *alias = map_get(&ident, s->id);
		if (str_eq(alias, s->name))
			alias = NULL;
		on_set(map_has(&is_value, s->id) ? &g->values : &g->types, s->name, alias);
	}
	for (int i = 0; i < group_order.n; i++) {
		ImportGroup *g = group_order.v[i];
		const char *from = make_import_path_relative(f->import_path, rewrite_import_path(f->schema, g->from));
		if (g->types.names.n) {
			*wrote_imports = true;
			write_import(c, f->legacy_commonjs, true, from, &g->types.names);
		}
		if (g->values.names.n) {
			*wrote_imports = true;
			write_import(c, f->legacy_commonjs, false, from, &g->values.names);
		}
	}
	return ident;
}

/* elToContent: the content of the file without the preamble. */
Str generated_file_content(GeneratedFile *f)
{
	if (!f->el.n)
		return (Str){"", 0};
	Buf c = {0};
	bool legacy = f->legacy_commonjs;
	if (legacy)
		buf_adds(&c, "\"use strict\";\nObject.defineProperty(exports, \"__esModule\", { value: true });\n\n");
	bool wrote_imports = legacy;
	Map ident = process_imports(f, &c, &wrote_imports);
	if (wrote_imports)
		buf_adds(&c, "\n");
	Vec exports = {0};
	for (int i = 0; i < f->el.n; i++) {
		El *e = f->el.v[i];
		if (e->kind == EL_STR) {
			buf_add_str(&c, e->s);
		} else if (e->kind == EL_SYM) {
			buf_adds(&c, map_get(&ident, e->sym->id));
		} else {
			if (legacy)
				vec_push(&exports, (void *)e->s.p);
			else
				buf_adds(&c, "export ");
			if (*e->decl)
				buf_adds(&c, xcat(e->decl, " "));
			buf_add_str(&c, e->s);
		}
	}
	if (legacy) {
		if (exports.n)
			buf_adds(&c, "\n");
		for (int i = 0; i < exports.n; i++)
			buf_adds(&c, xcat("exports.", exports.v[i], " = ", exports.v[i], ";\n"));
	}
	return buf_view(&c);
}
