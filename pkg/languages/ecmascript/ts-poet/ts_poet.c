/* ts-poet 6.12.0: Import.js (Import.from, emitImports, maybeRelativePath, sameModule), Code.js
 * (deepFindAll, assignAliasesIfNeeded, generateCodeWithImports) and ConditionalOutput.js, for
 * generators that build dprint-typescript nodes. See ts_poet.h.
 *
 * ts-poet: Copyright (c) Stephen Haberman, Apache-2.0 License. */
#include "ts_poet.h"

#include <string.h>

#include "es_std.h"
#ifdef TP_DUMP
#include "sys.h"
#endif

/* ---- Import.from ---- */

/* identPattern: (?:[a-zA-Z][_a-zA-Z0-9]*)|(?:[_a-zA-Z][_a-zA-Z0-9]+), as a maximal run (the
 * patterns that follow it cannot match an identifier character, so the regex never backtracks
 * into it). Returns the length or 0. */
static size_t ident_len(const char *s)
{
	bool alpha0 = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z');
	if (!alpha0 && *s != '_')
		return 0;
	size_t n = 1;
	while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') || (s[n] >= '0' && s[n] <= '9') ||
	       s[n] == '_')
		n++;
	return alpha0 || n >= 2 ? n : 0;
}

static TpImport *new_import(int kind, const char *symbol, const char *source)
{
	TpImport *i = xalloc(sizeof *i);
	i->tp = TP_IMPORT;
	i->kind = (uint8_t)kind;
	i->symbol = symbol;
	i->source = source;
	return i;
}

/* Matches `^(prefix IDENT)(\.IDENT)?([kinds])(.+)`; with_source: the symbol may be `IDENT:IDENT`
 * (sourceImportPattern, kind `@` only). */
static bool match_spec(const char *spec, bool with_source, size_t *sym_end, const char **child, size_t *child_n,
		       char *kind, const char **module)
{
	const char *p = spec;
	if (p[0] == 't' && p[1] == ':' && ident_len(p + 2))
		p += 2;
	size_t n = ident_len(p);
	if (!n)
		return false;
	p += n;
	if (with_source && *p == ':' && ident_len(p + 1))
		p += 1 + ident_len(p + 1);
	*sym_end = (size_t)(p - spec);
	*child = NULL;
	*child_n = 0;
	if (*p == '.' && ident_len(p + 1)) {
		*child = p + 1;
		*child_n = ident_len(p + 1);
		p += 1 + *child_n;
	}
	if (with_source ? *p != '@' : !strchr("*@+=", *p) || !*p)
		return false;
	*kind = *p++;
	/* (.+): one or more characters up to a line terminator */
	size_t m = strcspn(p, "\n\r");
	if (!m)
		return false;
	*module = xstrndup(p, m);
	return true;
}

TpImport *tp_imp(const char *spec)
{
	size_t sym_end, child_n;
	const char *child, *module;
	char kind;
	if (!match_spec(spec, false, &sym_end, &child, &child_n, &kind, &module) &&
	    !match_spec(spec, true, &sym_end, &child, &child_n, &kind, &module))
		return new_import(IMP_IMPLICIT, xstrdup(spec), NULL);
	char *symbol_name = xstrndup(spec, sym_end);
	switch (kind) {
	case '*':
		return new_import(IMP_ALL, symbol_name, module);
	case '=':
		return new_import(IMP_DEFAULT, symbol_name, module);
	case '+':
		return new_import(IMP_SIDE_EFFECT, symbol_name, module);
	default: {
		bool type_import = has_prefix(symbol_name, "t:");
		Vec names = str_split(type_import ? symbol_name + 2 : symbol_name, ':');
		TpImport *i = new_import(IMP_NAME, names.v[names.n - 1], module);
		i->type_import = type_import;
		if (names.n > 1)
			i->source_symbol = names.v[0];
		if (child)
			i->child_symbol = xstrndup(child, child_n);
		return i;
	}
	}
}

/* ---- codes ---- */

Node *tp_clone(const Node *n);

TpCode *tp_code(Node *node)
{
	TpCode *c = xalloc(sizeof *c);
	c->tp = TP_CODE;
	c->node = node;
	return c;
}

void *tp_add(TpCode *c, void *item)
{
	if (!item)
		return NULL;
	if (c->n == c->cap) {
		int cap = c->cap ? c->cap * 2 : 4;
		c->v = xrealloc(c->v, sizeof(void *) * (size_t)c->cap, sizeof(void *) * (size_t)cap);
		c->cap = cap;
	}
	c->v[c->n++] = item;
	return item;
}

Node *tp_sub(TpCode *c, TpCode *child)
{
	tp_add(c, child);
	return child && child->node ? tp_clone(child->node) : NULL;
}

TpCode *tp_join(TpCode *const *chunks, int n)
{
	TpCode *c = tp_code(NULL);
	for (int i = 0; i < n; i++)
		tp_add(c, chunks[i]);
	return c;
}

TpCond *tp_cond(const char *name, TpCode *decl, bool is_type)
{
	TpCond *co = xalloc(sizeof *co);
	co->tp = TP_COND;
	co->name = name;
	co->decl = decl ? decl : tp_code(NULL);
	co->is_type = is_type;
	return co;
}

TpDef *tp_def(const char *symbol)
{
	TpDef *d = xalloc(sizeof *d);
	d->tp = TP_DEF;
	d->symbol = symbol;
	return d;
}

/* ---- uses of imports in the current file ---- */

typedef struct {
	Node *ident;
	TpImport *imp;
} Use;

typedef struct {
	TpCode *shared;
	int from, to;
} Cached;

static struct {
	unsigned file;
	Use *uses;
	int n, cap;
	Vec cached; /* Cached */
} F;

void tp_file_begin(void)
{
	F.file++;
	F.uses = NULL;
	F.n = F.cap = 0;
	F.cached = (Vec){0};
}

int tp_uses_mark(void) { return F.n; }

void tp_cached_text(TpCode *shared, int from)
{
	Cached *c = xalloc(sizeof *c);
	*c = (Cached){shared, from, F.n};
	vec_push(&F.cached, c);
}

/* the names of the uses in a code printed before take the text of its first printing; the
 * first printing records them in memory that outlives the file (arena_release) */
static void apply_cached_text(void)
{
	for (int k = 0; k < F.cached.n; k++) {
		Cached *c = F.cached.v[k];
		TpCode *s = c->shared;
		int n = c->to - c->from;
		if (!s->cached) {
			s->cache = xalloc_keep(sizeof(char *) * (size_t)(n ? n : 1));
			for (int i = 0; i < n; i++) {
				Str t = F.uses[c->from + i].ident->text;
				char *keep = xalloc_keep(t.n + 1);
				memcpy(keep, t.p, t.n);
				s->cache[i] = keep;
			}
			s->ncache = n;
			s->cached = true;
		} else {
			if (s->ncache != n)
				fail("ts-poet: a shared code built differently in two files");
			for (int i = 0; i < n; i++)
				F.uses[c->from + i].ident->text = str_c(s->cache[i]);
		}
	}
}

static Node *register_use(Node *id, TpImport *imp)
{
	id->user = imp;
	if (F.n == F.cap) {
		int cap = F.cap ? F.cap * 2 : 64;
		F.uses = xrealloc(F.uses, sizeof(Use) * (size_t)F.cap, sizeof(Use) * (size_t)cap);
		F.cap = cap;
	}
	F.uses[F.n++] = (Use){id, imp};
	return id;
}

static Node *use_ident(TpImport *imp) { return register_use(dts_id(imp->symbol), imp); }

static void clone_cb(Node *copy, const Node *orig, void *arg)
{
	(void)orig;
	(void)arg;
	if (copy->kind == N_IDENT && copy->user)
		register_use(copy, copy->user);
}

Node *tp_clone(const Node *n) { return dts_clone(n, clone_cb, NULL); }

/* the expression form of a type name: TsTypeRef / TsQualifiedName / Ident -> Ident / MemberExpr */
static Node *entity_expr(Node *e)
{
	if (e->kind == N_TS_QUALIFIED_NAME)
		return dts_membern(entity_expr(e->a), e->b->text);
	return e;
}

Node *tp_expr(const Node *type)
{
	Node *t = tp_clone(type);
	if (t->kind == N_TS_TYPE_REF && !t->b)
		return entity_expr(t->a);
	if (t->kind == N_TS_KEYWORD_TYPE)
		return dts_idn(t->text);
	return entity_expr(t);
}

/* an Implicit symbol is printed as written: a dotted name becomes a member chain */
static Node *implicit_expr(const char *s)
{
	Node *n = NULL;
	Vec parts = str_split(s, '.');
	for (int i = 0; i < parts.n; i++)
		n = n ? dts_member(n, parts.v[i]) : dts_id(parts.v[i]);
	return n;
}

Node *tp_ref(TpCode *c, TpImport *imp)
{
	tp_add(c, imp);
	if (imp->kind == IMP_IMPLICIT)
		return implicit_expr(imp->symbol);
	Node *id = use_ident(imp);
	return imp->kind == IMP_NAME && imp->child_symbol ? dts_member(id, imp->child_symbol) : id;
}

Node *tp_entity(TpCode *c, TpImport *imp)
{
	tp_add(c, imp);
	if (imp->kind == IMP_IMPLICIT)
		return dts_entity(imp->symbol);
	Node *id = use_ident(imp);
	if (imp->kind == IMP_NAME && imp->child_symbol) {
		Node *q = dts_new(N_TS_QUALIFIED_NAME);
		q->a = id;
		q->b = dts_id(imp->child_symbol);
		return q;
	}
	return id;
}

Node *tp_tref(TpCode *c, TpImport *imp, NodeList targs) { return dts_tref_node(tp_entity(c, imp), targs); }

Node *tp_defn(TpCode *c, TpDef *def)
{
	tp_add(c, def);
	return dts_id(def->symbol);
}

Node *tp_co(TpCode *c, TpCond *co)
{
	tp_add(c, co);
	return dts_id(co->name);
}

Node *tp_co_type(TpCode *c, TpCond *co, NodeList targs)
{
	tp_add(c, co);
	return dts_tref_node(dts_id(co->name), targs);
}

/* ---- Code.deepFindAll ---- */

static void push(Vec *todo, void *item) { vec_push(todo, item); }

static void deep_find_all(TpCode *root, Vec *imports, Vec *defs)
{
	Vec todo = {0};
	push(&todo, root);
	for (int i = 0; i < todo.n; i++) {
		uint8_t tp = *(uint8_t *)todo.v[i];
		if (tp == TP_CODE) {
			TpCode *c = todo.v[i];
			for (int k = 0; k < c->n; k++)
				push(&todo, c->v[k]);
		} else if (tp == TP_COND) {
			/* childNodes (the declaration code), then the declaration's children */
			TpCond *co = todo.v[i];
			co->used = F.file;
			push(&todo, co->decl);
			for (int k = 0; k < co->decl->n; k++)
				push(&todo, co->decl->v[k]);
		} else if (tp == TP_IMPORT) {
			if (imports)
				vec_push(imports, todo.v[i]);
		} else if (tp == TP_DEF) {
			if (defs)
				vec_push(defs, todo.v[i]);
		}
	}
}

void tp_find_used(TpCode *root) { deep_find_all(root, NULL, NULL); }

bool tp_used(const TpCond *co) { return co->used == F.file; }

/* ---- paths ---- */

/* p.replace(/\.[tj]sx?/, "") */
static const char *strip_ext(const char *p)
{
	for (const char *s = p; (s = strchr(s, '.')); s++) {
		if ((s[1] == 't' || s[1] == 'j') && s[2] == 's') {
			size_t m = 3 + (s[3] == 'x');
			return xcat(xstrndup(p, (size_t)(s - p)), s + m);
		}
	}
	return p;
}

bool tp_same_module(const char *path1, const char *path2)
{
	const char *a = strip_ext(path1), *b = strip_ext(path2);
	return str_eq(a, b) || str_eq(node_path_resolve(a), node_path_resolve(b));
}

static const char *maybe_relative_path(const char *output_path, const char *import_path)
{
	if (!has_prefix(import_path, "./"))
		return import_path;
	import_path = node_path_normalize(import_path);
	output_path = node_path_normalize(output_path);
	const char *dir = node_path_dirname(output_path);
	if (str_eq(dir, import_path))
		return xcat("../", node_path_basename(import_path, NULL));
	const char *rel = node_path_relative(dir, import_path);
	if (rel[0] != '.')
		rel = xcat("./", rel);
	return rel;
}

/* ---- Code.assignAliasesIfNeeded ---- */

static void assign_aliases(const Vec *defs, const Vec *imports, const char *our_module_path)
{
	Map used = {0};
	for (int i = 0; i < defs->n; i++)
		map_put(&used, VEC_AT(*defs, TpDef, i)->symbol, (void *)1);
	Map assigned = {0};
	int j = 1;
	for (int k = 0; k < imports->n; k++) {
		TpImport *i = imports->v[k];
		if (i->kind != IMP_NAME || tp_same_module(i->source, our_module_path) ||
		    (i->defined_in && tp_same_module(i->defined_in, our_module_path)))
			continue;
		const char *key = xcat(i->symbol, "@", i->source);
		if (map_has(&used, i->symbol)) {
			const char *alias = map_get(&assigned, key);
			if (!alias) {
				/* kept: a shared import keeps its alias in the next files (generators may
				 * free a file's memory with arena_release) */
				const char *a = xfmt("%s%d", i->symbol, j++);
				char *k = xalloc_keep(strlen(a) + 1);
				memcpy(k, a, strlen(a));
				alias = k;
				map_put(&assigned, key, (void *)alias);
			}
			/* an equal alias keeps its string (it may belong to a per-file import) */
			if (!str_eq(alias, i->symbol)) {
				i->source_symbol = i->symbol;
				i->symbol = alias;
			}
		} else {
			map_put(&used, i->symbol, (void *)1);
			map_put(&assigned, key, (void *)i->symbol);
		}
	}
}

/* ---- Import.emitImports ---- */

static Node *quoted(const char *s) { return dts_str(str_c(xcat("'", s, "'"))); }

/* unique(list) of Import.js: adds s to the set; false if it was there */
static bool add_unique(Map *set, const char *s)
{
	if (map_has(set, s))
		return false;
	map_put(set, s, (void *)1);
	return true;
}

/* groupBy(list, source): the groups in Object.entries order */
typedef struct {
	const char *key;
	Vec items;
} Group;

static int group_by_source(const Vec *list, Group **out)
{
	Map idx = {0};
	Group *g = NULL;
	int n = 0;
	for (int k = 0; k < list->n; k++) {
		TpImport *i = list->v[k];
		intptr_t at = (intptr_t)map_get(&idx, i->source);
		if (!at) {
			g = xrealloc(g, sizeof(Group) * (size_t)n, sizeof(Group) * (size_t)(n + 1));
			g[n] = (Group){i->source, {0}};
			at = ++n;
			map_put(&idx, i->source, (void *)at);
		}
		vec_push(&g[at - 1].items, i);
	}
	/* integer-like keys first, ascending (stable insertion sort: keys are few) */
	for (int a = 1; a < n; a++) {
		uint32_t ka;
		if (!js_is_array_index(str_c(g[a].key), &ka))
			continue;
		Group t = g[a];
		int b = a;
		for (; b > 0; b--) {
			uint32_t kb;
			if (js_is_array_index(str_c(g[b - 1].key), &kb) && kb <= ka)
				break;
			g[b] = g[b - 1];
		}
		g[b] = t;
	}
	*out = g;
	return n;
}

/* emitImports: the declarations as nodes in out and as ts-poet's text in raw */
static void emit_imports(const Vec *imports, const char *our_module_path, const TpOpts *o, NodeList *out, Buf *raw)
{
	Vec filtered = {0};
	for (int k = 0; k < imports->n; k++) {
		TpImport *i = imports->v[k];
		if (!i->source)
			continue;
		if (i->kind == IMP_NAME && i->defined_in && tp_same_module(i->defined_in, our_module_path))
			continue;
		vec_push(&filtered, i);
	}
	Group *groups;
	int ng = group_by_source(&filtered, &groups);
	for (int gi = 0; gi < ng; gi++) {
		const char *module_path = groups[gi].key;
		Vec *items = &groups[gi].items;
		if (tp_same_module(our_module_path, module_path))
			continue;
		const char *import_path = maybe_relative_path(our_module_path, module_path);
		Map stars = {0};
		for (int k = 0; k < items->n; k++) {
			TpImport *i = items->v[k];
			if (i->kind == IMP_ALL && add_unique(&stars, i->symbol)) {
				Node *d = dts_import(quoted(import_path), false);
				dts_import_star(d, dts_id(i->symbol));
				dts_add(out, d);
				buf_fmt(raw, "import * as %s from '%s';\n", i->symbol, import_path);
			}
		}
		Vec symbol_imports = {0}, type_imports = {0};
		Map symbols = {0};
		for (int k = 0; k < items->n; k++) {
			TpImport *i = items->v[k];
			if (i->kind == IMP_NAME && !i->type_import) {
				vec_push(&symbol_imports, i);
				map_put(&symbols, i->symbol, (void *)1);
			}
		}
		for (int k = 0; k < items->n; k++) {
			TpImport *i = items->v[k];
			if (i->kind == IMP_NAME && i->type_import && !map_has(&symbols, i->symbol))
				vec_push(&type_imports, i);
		}
		bool single_type = type_imports.n > 0 && symbol_imports.n == 0;
		/* unique(...map(toImportPiece)): pieces compared as text */
		Map pieces = {0};
		Vec named = {0};
		for (int pass = 0; pass < 2; pass++) {
			Vec *src = pass == 0 ? &type_imports : &symbol_imports;
			if (single_type && pass == 1)
				break;
			for (int k = 0; k < src->n; k++) {
				TpImport *i = src->v[k];
				bool type_prefix = i->type_import && !single_type;
				const char *piece = xcat(type_prefix ? "type " : "",
							 i->source_symbol ? xcat(i->source_symbol, " as ", i->symbol) : i->symbol);
				if (add_unique(&pieces, piece))
					vec_push(&named, i);
			}
		}
		Vec defaults = {0};
		Map dset = {0};
		for (int k = 0; k < items->n; k++) {
			TpImport *i = items->v[k];
			if (i->kind == IMP_DEFAULT && add_unique(&dset, i->symbol))
				vec_push(&defaults, (void *)i->symbol);
		}
		bool force_require = false;
		for (int k = 0; k < o->nforce_require; k++)
			force_require |= str_eq(o->force_require[k], module_path);
		if (force_require && defaults.n > 0) {
			dts_add(out, dts_import_equals(dts_id(defaults.v[0]), quoted(import_path), false));
			buf_fmt(raw, "import %s = require('%s');\n", (const char *)defaults.v[0], import_path);
		} else if (named.n > 0 || defaults.n > 0) {
			Node *d = dts_import(quoted(import_path), single_type);
			buf_adds(raw, single_type ? "import type " : "import ");
			if (defaults.n > 0) {
				dts_import_default(d, dts_id(defaults.v[0]));
				buf_adds(raw, defaults.v[0]);
				buf_adds(raw, named.n ? ", " : "");
			}
			for (int k = 0; k < named.n; k++) {
				TpImport *i = named.v[k];
				bool type_prefix = i->type_import && !single_type;
				dts_import_named(d, i->source_symbol ? dts_id(i->source_symbol) : NULL, dts_id(i->symbol),
						 type_prefix);
				buf_adds(raw, k ? ", " : "{ ");
				buf_adds(raw, type_prefix ? "type " : "");
				if (i->source_symbol)
					buf_fmt(raw, "%s as ", i->source_symbol);
				buf_adds(raw, i->symbol);
			}
			buf_fmt(raw, "%s from '%s';\n", named.n ? " }" : "", import_path);
			dts_add(out, d);
		}
	}
	Vec side = {0};
	Map sset = {0};
	for (int k = 0; k < imports->n; k++) {
		TpImport *i = imports->v[k];
		if (i->kind == IMP_SIDE_EFFECT && add_unique(&sset, i->source))
			vec_push(&side, (void *)i->source);
	}
	for (int k = 0; k < side.n; k++) {
		dts_add(out, dts_import(quoted(side.v[k]), false));
		buf_fmt(raw, "import '%s';\n", (const char *)side.v[k]);
	}
}

/* ---- Code.toString ---- */

/* Inserts comments before n's own comments; nl: line break between them and n's first comment
 * (or n itself). */
static void prepend_comments(Node *n, const NodeComment *cms, int ncms, int nl)
{
	if (!ncms)
		return;
	NodeComment *v = xalloc(sizeof(NodeComment) * (size_t)(ncms + n->ncm));
	memcpy(v, cms, sizeof(NodeComment) * (size_t)ncms);
	for (int i = 0; i < n->ncm; i++) {
		v[ncms + i] = n->cm[i];
		if (i == 0)
			v[ncms].nl = (uint8_t)nl;
	}
	if (!n->ncm)
		n->nl_after_cm = (uint8_t)nl;
	n->cm = v;
	n->ncm += ncms;
}

void tp_to_string(TpCode *root, NodeList body, const NodeComment *trailing, int ntrailing, const TpOpts *o,
		  Buf *out)
{
	const char *path = o->path;
	const char *our_module_path = strip_ext(o->path ? o->path : "");
	Vec imports = {0}, defs = {0};
	deep_find_all(root, &imports, &defs);
	assign_aliases(&defs, &imports, our_module_path);
	for (int i = 0; i < F.n; i++)
		F.uses[i].ident->text = str_c(F.uses[i].imp->symbol);
	apply_cached_text();
	NodeList imps = {0};
	Buf import_part = {0};
	emit_imports(&imports, our_module_path, o, &imps, &import_part);

	Node *m = dts_module();
	for (int i = 0; i < imps.n; i++)
		dts_block_add(m, imps.v[i]);
	for (int i = 0; i < body.n; i++) {
		Node *s = body.v[i];
		if (i == 0 && imps.n)
			s->nl = NL_BLANK; /* importPart + "\n" + bodyPart */
		dts_block_add(m, s);
	}
	/* prefix + "\n" + importPart + "\n" + bodyPart: a blank line after the prefix without imports */
	int nl = imps.n ? NL_LINE : NL_BLANK;
	if (m->list.n) {
		prepend_comments(m->list.v[0], o->prefix, o->nprefix, nl);
		for (int i = 0; i < ntrailing; i++)
			dts_trailing_comment(m, trailing[i].text, trailing[i].block, trailing[i].nl);
	} else {
		for (int i = 0; i < o->nprefix; i++)
			dts_trailing_comment(m, o->prefix[i].text, o->prefix[i].block, o->prefix[i].nl);
		for (int i = 0; i < ntrailing; i++)
			dts_trailing_comment(m, trailing[i].text, trailing[i].block, i == 0 ? NL_BLANK : trailing[i].nl);
	}
	/* maybePretty: when dprint cannot parse the code, ts-poet returns it unformatted:
	 * prefix + "\n" + importPart + "\n" + bodyPart, the body as the generator's raw text */
	const char *bad = dprint_ts_reserved_name(m);
	if (bad) {
		for (int k = 0; o->raw_body && k < imports.n; k++)
			if (((TpImport *)imports.v[k])->source_symbol) {
				o = NULL; /* an alias the raw text does not know */
				break;
			}
		if (!o || !o->raw_body)
			fail("unsupported by protoc-gen-uni: %s: the reserved word `%s` as a name makes ts-poet emit the code "
			     "unformatted", path ? path : "", bad);
		if (o->raw_prefix)
			buf_fmt(out, "%s\n", o->raw_prefix);
		buf_add(out, import_part.p, import_part.n);
		buf_addc(out, '\n');
		o->raw_body(o->raw_arg, out);
		return;
	}
#ifdef TP_DUMP /* debugging: the module tree on stderr */
	{
		Buf d = {0};
		buf_fmt(&d, "==== %s\n", o->path ? o->path : "");
		dprint_ts_dump(m, &d);
		sys_write_all(SYS_STDERR, d.p, d.n);
	}
#endif
	dprint_ts_format_module(m, out);
}
