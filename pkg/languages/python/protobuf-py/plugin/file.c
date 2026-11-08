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

/* protobuf/plugin/_file.py of protobuf-py v0.7.0: the generated file (print, scopes, docstrings,
 * imports of identifiers) and write() with the ruff-style import groups and the alias resolver. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

static const char INDENT[] = "    ";
static const char TYPING[] = "typing";

/* _WKT_PROTO_PATHS: the *_pb modules of protobuf.wkt.google.protobuf and of its compiler
 * package. */
static const char *const wkt_names[] = {
	"any", "api", "c_sharp_features", "cpp_features", "cpp_file_options", "cpp_options", "descriptor",
	"duration", "empty", "field_mask", "go_features", "java_features", "json_enumvalue_options",
	"json_options", "source_context", "struct", "timestamp", "type", "wrappers", NULL,
};

/* _wkt_module: the module of a WKT file not being generated (file_mod for a DescFile, else
 * member_mod), or NULL. */
static const char *wkt_module(const PbpySchema *s, const void *desc)
{
	const PbpyDesc *d = desc;
	const PbpyDescFile *file = d->kind == PBPY_FILE ? desc
		: d->kind == PBPY_MESSAGE ? ((const PbpyDescMessage *)desc)->file
		: d->kind == PBPY_ENUM ? ((const PbpyDescEnum *)desc)->file
		: ((const PbpyDescExtension *)desc)->file;
	if (map_has(&s->file_to_generate, file->name))
		return NULL;
	const char *pkg = NULL, *base = NULL;
	if (str_eq(file->name, "google/protobuf/compiler/plugin.proto")) {
		pkg = "protobuf.wkt.google.protobuf.compiler";
		base = "plugin";
	} else if (has_prefix(file->name, "google/protobuf/") && has_suffix(file->name, ".proto")) {
		const char *rest = file->name + strlen("google/protobuf/");
		for (const char *const *w = wkt_names; *w; w++) {
			size_t n = strlen(*w);
			if (!strncmp(rest, *w, n) && str_eq(rest + n, ".proto")) {
				pkg = "protobuf.wkt.google.protobuf";
				base = *w;
			}
		}
	}
	if (!pkg)
		return NULL;
	return d->kind == PBPY_FILE ? pkg : xcat(pkg, ".", base, "_pb");
}

/* _desc_ident */
static PbpyIdent *desc_ident(const PbpySchema *s, const void *desc, bool type_only)
{
	PbpyIdent *id = pbpy_ident_for_desc(desc, type_only, s->escape_module_with_hash);
	const char *mod = wkt_module(s, desc);
	if (mod)
		return pbpy_ident(mod, id->name, type_only);
	return id;
}

static bool is_relative(const char *module) { return *module == '.'; }

/* _module_segments compare as the path without one leading dot */
static const char *seg_path(const char *module) { return *module == '.' ? module + 1 : module; }

static int segments(const char *module, Vec *out)
{
	const char *p = seg_path(module);
	*out = *p ? str_split(p, '.') : (Vec){0};
	return out->n;
}

static bool is_init_py(const char *path)
{
	const char *slash = strrchr(path, '/');
	return str_eq(slash ? slash + 1 : path, "__init__.py");
}

/* _map_import */
static PbpyIdent *map_import(PbpyFile *f, PbpyIdent *id)
{
	const PbpySchema *s = f->schema;
	if (!s->map_imports.n || !id->desc || !is_relative(id->module))
		return id;
	const PbpyDesc *d = id->desc;
	bool module_import = d->kind == PBPY_FILE;
	if (!module_import && str_eq(seg_path(id->module), seg_path(f->module)))
		return id; /* references to symbols in this file are not imports */
	const PbpyDescFile *file = module_import ? id->desc
		: d->kind == PBPY_MESSAGE ? ((const PbpyDescMessage *)id->desc)->file
		: d->kind == PBPY_ENUM ? ((const PbpyDescEnum *)id->desc)->file
		: ((const PbpyDescExtension *)id->desc)->file;
	const char *target = map_get(&s->map_targets, file->name);
	if (!target) {
		target = pbpy_map_import_target(&s->map_imports, file->name);
		map_put((Map *)&s->map_targets, file->name, (void *)(target ? target : "\x01"));
	}
	if (!target || str_eq(target, "\x01"))
		return id;
	const char *module_path = id->module;
	if (module_import)
		module_path = xcat(module_path, has_suffix(module_path, ".") ? "" : ".", id->name);
	const char *dotted = seg_path(module_path);
	const char *mapped = *target ? xcat(target, ".", dotted) : dotted;
	PbpyIdent *out;
	if (module_import) {
		const char *dot = strrchr(mapped, '.');
		const char *name = dot ? dot + 1 : mapped;
		const char *parent = dot ? xstrndup(mapped, (size_t)(dot - mapped)) : "";
		out = pbpy_ident(*parent ? parent : name, name, id->type_only);
	} else {
		out = pbpy_ident(mapped, id->name, id->type_only);
	}
	out->desc = id->desc;
	return out;
}

/* _relativize */
static PbpyIdent *relativize(PbpyFile *f, PbpyIdent *id)
{
	bool type_only = id->type_only || f->type_checking;
	if (!is_relative(id->module)) {
		PbpyIdent *out = pbpy_ident(id->module, id->name, type_only);
		out->desc = id->desc;
		return out;
	}
	const char *cached = map_get(&f->rel, id->module);
	if (cached)
		return pbpy_ident(cached, id->name, type_only);
	if (str_eq(seg_path(f->module), seg_path(id->module))) {
		map_put(&f->rel, id->module, "");
		return pbpy_ident("", id->name, type_only);
	}
	Vec self, imp;
	int ns = segments(f->module, &self), ni = segments(id->module, &imp);
	int npkg = is_init_py(f->path) ? ns : ns - 1;
	if (npkg < 0)
		npkg = 0;
	int shared = 0;
	while (shared < npkg && shared < ni && str_eq(self.v[shared], imp.v[shared]))
		shared++;
	Buf b = {0};
	for (int i = 0; i < npkg - shared + 1; i++)
		buf_addc(&b, '.');
	for (int i = shared; i < ni; i++) {
		if (i > shared)
			buf_addc(&b, '.');
		buf_adds(&b, imp.v[i]);
	}
	const char *path = xstrdup(buf_str(&b));
	map_put(&f->rel, id->module, (void *)path);
	return pbpy_ident(path, id->name, type_only);
}

static void imports_add(PbpyImports *im, PbpyIdent *id)
{
	PbpyModImports *m = map_get(&im->by_module, id->module);
	if (!m) {
		m = xalloc(sizeof *m);
		m->module = id->module;
		map_put(&im->by_module, id->module, m);
		vec_push(&im->modules, m);
	}
	if (!map_has(&m->names, id->name)) {
		map_put(&m->names, id->name, id);
		vec_push(&m->idents, id);
	}
}

static const char *scalar_type(int scalar)
{
	switch (scalar) {
	case PT_DOUBLE: case PT_FLOAT: return "float";
	case PT_BOOL: return "bool";
	case PT_STRING: return "str";
	case PT_BYTES: return "bytes";
	default: return "int";
	}
}

/* In a docstring: \\ -> \\\\, """ -> \\"\\"\\" */
static Str doc_escape(Str s)
{
	Buf b = {0};
	for (size_t i = 0; i < s.n; i++) {
		if (s.p[i] == '\\') {
			buf_adds(&b, "\\\\");
		} else if (s.p[i] == '"' && i + 2 < s.n && s.p[i + 1] == '"' && s.p[i + 2] == '"') {
			buf_adds(&b, "\\\"\\\"\\\"");
			i += 2;
		} else {
			buf_addc(&b, s.p[i]);
		}
	}
	return buf_view(&b);
}

/* _to_el */
static PbpyOut *to_el(PbpyFile *f, const PbpyEl *e)
{
	PbpyOut *o = xalloc(sizeof *o);
	PbpyIdent *id;
	switch (e->kind) {
	case PE_SCALAR:
		o->s = str_c(scalar_type(e->u.scalar));
		return o;
	case PE_STR:
		o->s = str_c(e->u.s);
		if (f->in_doc)
			o->s = doc_escape(o->s);
		return o;
	case PE_STRN:
		o->s = f->in_doc ? doc_escape(e->u.str) : e->u.str;
		return o;
	case PE_INT:
		o->s = str_c(xfmt("%lld", (long long)e->u.i));
		return o;
	case PE_BOOL:
		o->s = str_c(e->u.b ? "True" : "False");
		return o;
	case PE_BYTES:
		o->s = str_c(py_repr_bytes(e->u.bytes));
		return o;
	case PE_DESC:
		id = desc_ident(f->schema, e->u.desc, false);
		break;
	case PE_IDENT:
		id = e->u.ident->desc ? desc_ident(f->schema, e->u.ident->desc, e->u.ident->type_only) : (PbpyIdent *)e->u.ident;
		break;
	default:
		o->s = str_c("");
		return o;
	}
	id = relativize(f, map_import(f, id));
	imports_add(id->type_only ? &f->types : &f->runtime, id);
	o->id = id;
	return o;
}

void pbpy_els_push(PbpyEls *l, PbpyEl e)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 8;
		l->v = xrealloc(l->v, sizeof *l->v * (size_t)l->cap, sizeof *l->v * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = e;
}

/* _flatten + _to_el of every element */
static void convert(PbpyFile *f, const PbpyEl *els, Vec *out)
{
	for (; els && els->kind != PE_END; els++) {
		if (els->kind == PE_LIST) {
			const PbpyEls *l = els->u.list;
			for (int i = 0; i < l->n; i++)
				convert(f, (const PbpyEl[]){l->v[i], {PE_END, {0}}}, out);
			continue;
		}
		vec_push(out, to_el(f, els));
	}
}

static void emit(PbpyFile *f, const char *s)
{
	PbpyOut *o = xalloc(sizeof *o);
	o->s = str_c(s);
	vec_push(&f->out, o);
}

static const char *indent_str(int n)
{
	if (n <= 0)
		return "";
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_adds(&b, INDENT);
	return xstrdup(buf_str(&b));
}

void pbpy_print_v(PbpyFile *f, const PbpyEl *els)
{
	Vec v = {0};
	convert(f, els, &v);
	bool empty = true;
	for (int i = 0; i < v.n && empty; i++) {
		PbpyOut *o = v.v[i];
		empty = !o->id && !o->s.n;
	}
	if (empty) {
		emit(f, "\n");
		return;
	}
	emit(f, indent_str(f->indent));
	for (int i = 0; i < v.n; i++)
		vec_push(&f->out, v.v[i]);
	emit(f, "\n");
}

void pbpy_println(PbpyFile *f) { emit(f, "\n"); }

PbpyIdent *pbpy_file_ident(PbpyFile *f, const char *name, bool type_only) { return pbpy_ident(f->module, name, type_only); }

const char *pbpy_file_path(const PbpyFile *f) { return f->path; }

void pbpy_scope_v(PbpyFile *f, const PbpyEl *els)
{
	pbpy_print_v(f, els);
	f->indent++;
}

void pbpy_end(PbpyFile *f) { f->indent--; }

void pbpy_type_checking(PbpyFile *f)
{
	if (f->type_checking)
		fail("RuntimeError: already in a typechecking context");
	pbpy_scope(f, PS("if "), PI(pbpy_ident(TYPING, "TYPE_CHECKING", false)), PS(":"));
	f->type_checking = true;
}

void pbpy_type_checking_end(PbpyFile *f)
{
	f->indent--;
	f->type_checking = false;
}

void pbpy_preamble(PbpyFile *f, const PbpyDescFile *desc)
{
	f->has_preamble = true;
	f->preamble_proto_name = desc->name;
}

/* The element count when the docstrings opened (nesting is not used). */
static int doc_start;

void pbpy_doc_v(PbpyFile *f, const PbpyEl *els)
{
	f->in_doc = true;
	emit(f, indent_str(f->indent));
	emit(f, "\"\"\"");
	convert(f, els, &f->out);
	emit(f, "\n");
	doc_start = f->out.n;
}

void pbpy_doc_end(PbpyFile *f)
{
	f->in_doc = false;
	if (f->out.n == doc_start) {
		((PbpyOut *)f->out.v[f->out.n - 1])->s = STR_LIT("\"\"\"\n");
	} else {
		emit(f, indent_str(f->indent));
		emit(f, "\"\"\"");
		emit(f, "\n");
	}
}

PbpyFile *pbpy_file_new(PbpySchema *s, const char *path, const char *module)
{
	PbpyFile *f = xalloc(sizeof *f);
	f->path = path;
	f->module = module;
	f->schema = s;
	return f;
}

/* ---- write ---- */

/* _AliasResolver: for each name the modules of the idents seen with it, in order; the first one
 * keeps the name, the n-th gets n-1 underscores appended (recursively resolved). */
typedef struct {
	Vec modules;
	Map index; /* module -> position + 1 */
} Seen;
typedef struct {
	Map seen; /* name -> Seen */
} Aliases;

/* index of (name, module) in seen[name], adding it */
static int seen_index(Aliases *a, const char *name, const char *module)
{
	Seen *s = map_get(&a->seen, name);
	if (!s) {
		s = xalloc(sizeof *s);
		map_put(&a->seen, name, s);
	}
	intptr_t i = (intptr_t)map_get(&s->index, module);
	if (i)
		return (int)i - 1;
	vec_push(&s->modules, (void *)module);
	map_put(&s->index, module, (void *)(intptr_t)s->modules.n);
	return s->modules.n - 1;
}

/* _resolve: the alias, or "" */
static const char *alias_resolve(Aliases *a, const char *name, const char *module)
{
	const char *result = "";
	for (;;) {
		int i = seen_index(a, name, module);
		if (!i)
			return result;
		Buf b = {0};
		buf_adds(&b, name);
		for (int k = 0; k < i; k++)
			buf_addc(&b, '_');
		name = result = xstrdup(buf_str(&b));
	}
}

/* resolve: the alias or name, keeping a dotted suffix */
static const char *resolve(Aliases *a, const PbpyIdent *id)
{
	const char *dot = strchr(id->name, '.');
	if (dot) {
		char *root = xstrndup(id->name, (size_t)(dot - id->name));
		const char *al = alias_resolve(a, root, id->module);
		return xcat(*al ? al : root, dot);
	}
	const char *al = alias_resolve(a, id->name, id->module);
	return *al ? al : id->name;
}

/* resolve_import: "name" or "name as alias" */
static const char *resolve_import(Aliases *a, const PbpyIdent *id)
{
	const char *dot = strchr(id->name, '.');
	const char *name = dot ? xstrndup(id->name, (size_t)(dot - id->name)) : id->name;
	const char *al = alias_resolve(a, name, id->module);
	return *al ? xcat(name, " as ", al) : name;
}

static int cmp_cstr_py(const void *x, const void *y) { return py_str_cmp(str_c(*(const char *const *)x), str_c(*(const char *const *)y)); }

static int cmp_ident(const void *x, const void *y)
{
	const PbpyIdent *a = *(PbpyIdent *const *)x, *b = *(PbpyIdent *const *)y;
	int c = py_str_cmp(str_c(a->name), str_c(b->name));
	return c ? c : py_str_cmp(str_c(a->module), str_c(b->module));
}

/* One module's sorted idents. */
typedef struct {
	const char *module;
	Vec idents;
} ModList;

static void sort_vec(Vec *v, int (*cmp)(const void *, const void *))
{
	if (v->n > 1)
		qsort(v->v, (size_t)v->n, sizeof *v->v, cmp);
}

/* sorted() of a set of strings, deduplicated */
static Vec sorted_set(Vec *v)
{
	sort_vec(v, cmp_cstr_py);
	Vec out = {0};
	for (int i = 0; i < v->n; i++)
		if (!out.n || !str_eq(out.v[out.n - 1], v->v[i]))
			vec_push(&out, v->v[i]);
	return out;
}

static bool is_module_import(const char *module, const PbpyIdent *id)
{
	return id->desc && ((const PbpyDesc *)id->desc)->kind == PBPY_FILE && str_eq(module, id->name);
}

static int cmp_modlist(const void *x, const void *y)
{
	const ModList *a = *(ModList *const *)x, *b = *(ModList *const *)y;
	return py_str_cmp(str_c(a->module), str_c(b->module));
}

/* _write_imports over ModList * entries */
static void write_imports(Vec *lines, Aliases *a, Vec *mods, const char *indent)
{
	Vec groups[3] = {{0}, {0}, {0}}; /* std, global, relative */
	for (int i = 0; i < mods->n; i++) {
		ModList *m = mods->v[i];
		int g;
		if (is_relative(m->module)) {
			g = 2;
		} else {
			const char *dot = strchr(m->module, '.');
			Str first = {m->module, dot ? (size_t)(dot - m->module) : strlen(m->module)};
			g = py_is_stdlib_module(first) ? 0 : 1;
		}
		vec_push(&groups[g], m);
	}
	for (int g = 0; g < 3; g++) {
		if (!groups[g].n)
			continue;
		sort_vec(&groups[g], cmp_modlist);
		for (int i = 0; i < groups[g].n; i++) {
			ModList *m = groups[g].v[i];
			Vec mod_names = {0}, from_names = {0};
			for (int k = 0; k < m->idents.n; k++) {
				PbpyIdent *id = m->idents.v[k];
				if (is_module_import(m->module, id))
					vec_push(&mod_names, (void *)resolve_import(a, id));
			}
			for (int k = 0; k < m->idents.n; k++) {
				PbpyIdent *id = m->idents.v[k];
				if (!is_module_import(m->module, id))
					vec_push(&from_names, (void *)resolve_import(a, id));
			}
			Vec ms = sorted_set(&mod_names);
			for (int k = 0; k < ms.n; k++)
				vec_push(lines, xcat(indent, "import ", ms.v[k]));
			if (from_names.n) {
				Vec fs = sorted_set(&from_names);
				vec_push(lines, xcat(indent, "from ", m->module, " import ", str_join(&fs, ", ")));
			}
		}
		vec_push(lines, "");
	}
}

static const char *preamble(const PbpyFile *f, bool no_fmt_off)
{
	const PbpySchema *s = f->schema;
	Buf b = {0};
	buf_fmt(&b, "# Generated from %s. DO NOT EDIT.\n", f->preamble_proto_name);
	buf_fmt(&b, "# Generated by %s v%s with parameter \"%s\".\n", s->plugin->name, s->plugin->version, s->parameter);
	buf_adds(&b, "# ruff: noqa: PGH004\n# ruff: noqa\n# pyright: reportDeprecated=false\n"
		     "# ty: ignore[unused-ignore-comment]\n# ty: ignore[deprecated, unused-ignore-comment]");
	if (!no_fmt_off)
		buf_adds(&b, "\n# fmt: off");
	return xstrdup(buf_str(&b));
}

Str pbpy_file_write(PbpyFile *f, bool no_fmt_off)
{
	/* runtime imports with sorted identifiers, without the file's own module */
	Vec runtime = {0};
	for (int i = 0; i < f->runtime.modules.n; i++) {
		PbpyModImports *m = f->runtime.modules.v[i];
		if (!*m->module)
			continue;
		ModList *l = xalloc(sizeof *l);
		l->module = m->module;
		for (int k = 0; k < m->idents.n; k++)
			vec_push(&l->idents, m->idents.v[k]);
		sort_vec(&l->idents, cmp_ident);
		vec_push(&runtime, l);
	}
	/* type-only imports that are not runtime imports */
	Vec types = {0};
	for (int i = 0; i < f->types.modules.n; i++) {
		PbpyModImports *m = f->types.modules.v[i];
		if (!*m->module)
			continue;
		PbpyModImports *rt = map_get(&f->runtime.by_module, m->module);
		ModList *l = xalloc(sizeof *l);
		l->module = m->module;
		for (int k = 0; k < m->idents.n; k++)
			if (!rt || !map_has(&rt->names, ((PbpyIdent *)m->idents.v[k])->name))
				vec_push(&l->idents, m->idents.v[k]);
		if (l->idents.n) {
			sort_vec(&l->idents, cmp_ident);
			vec_push(&types, l);
		}
	}
	PbpyIdent *tc = pbpy_ident(TYPING, "TYPE_CHECKING", false);
	if (types.n) {
		ModList *t = NULL;
		for (int i = 0; i < runtime.n && !t; i++)
			if (str_eq(((ModList *)runtime.v[i])->module, TYPING))
				t = runtime.v[i];
		if (!t) {
			t = xalloc(sizeof *t);
			t->module = TYPING;
			vec_push(&runtime, t);
		}
		PbpyModImports *rt = map_get(&f->runtime.by_module, TYPING);
		if (!rt || !map_has(&rt->names, tc->name)) {
			Vec v = {0};
			vec_push(&v, tc);
			for (int k = 0; k < t->idents.n; k++)
				vec_push(&v, t->idents.v[k]);
			t->idents = v;
		}
	}

	Aliases a = {0};
	PbpyModImports *own = map_get(&f->runtime.by_module, "");
	for (int i = 0; own && i < own->idents.n; i++) {
		PbpyIdent *id = own->idents.v[i];
		seen_index(&a, id->name, "");
	}
	own = map_get(&f->types.by_module, "");
	for (int i = 0; own && i < own->idents.n; i++) {
		PbpyIdent *id = own->idents.v[i];
		alias_resolve(&a, id->name, "");
	}

	Vec hdr = {0};
	if (f->has_preamble && f->preamble_proto_name) {
		vec_push(&hdr, (void *)preamble(f, no_fmt_off));
		vec_push(&hdr, "");
	}
	if (has_suffix(f->path, ".py")) {
		vec_push(&hdr, "from __future__ import annotations");
		vec_push(&hdr, "");
	}
	write_imports(&hdr, &a, &runtime, "");
	if (types.n) {
		vec_push(&hdr, xcat("if ", resolve(&a, tc), ":"));
		write_imports(&hdr, &a, &types, INDENT);
	}
	Buf b = {0};
	if (hdr.n) {
		buf_adds(&b, str_join(&hdr, "\n"));
		buf_adds(&b, "\n\n");
	}
	for (int i = 0; i < f->out.n; i++) {
		PbpyOut *o = f->out.v[i];
		if (o->id)
			buf_adds(&b, resolve(&a, o->id));
		else
			buf_add_str(&b, o->s);
	}
	if (!b.n)
		return (Str){"", 0};
	Str r = py_rstrip(buf_view(&b));
	char *out = xalloc(r.n + 2);
	memcpy(out, r.p, r.n);
	out[r.n] = '\n';
	return (Str){out, r.n + 1};
}
