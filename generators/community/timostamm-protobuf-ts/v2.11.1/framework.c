/* Copyright (c) 2016 Timo Stamm
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

/* Ported from packages/plugin/src/framework: typescript-comments.ts,
 * typescript-literal-from-value.ts, symbol-table.ts, typescript-file.ts, typescript-imports.ts,
 * typescript-enum-builder.ts; and packages/plugin/src/out-file.ts, file-table.ts. */

/* ---- short forms of the TypeScript factory calls ---- */

static TsNode *ID(const char *s) { return ts_create_identifier(s); }
static TsNode *PA(TsNode *e, const char *name) { return ts_create_property_access(e, ID(name)); }
static TsNode *THIS(void) { return ts_create_this(); }
static TsNode *STR(const char *s) { return ts_create_string_literal_c(s); }
static TsNode *NUM(const char *s) { return ts_create_numeric_literal(s); }
static TsNode *NUMI(long long v) { return ts_create_numeric_literal(xfmt("%lld", v)); }
static TsNode *TOK(TsSyntaxKind k) { return ts_create_token(k); }
static TsNode *KW(TsSyntaxKind k) { return ts_create_keyword_type_node(k); }
static TsNode *TREF(const char *name) { return ts_create_type_reference_node(ID(name), NULL); }
static TsNode *CALL(TsNode *e, TsNodeArray *args) { return ts_create_call(e, NULL, args ? args : ts_list_new()); }
static TsNode *BIN(TsNode *l, TsSyntaxKind op, TsNode *r) { return ts_create_binary_token(l, TOK(op), r); }
static TsNode *EXPR(TsNode *e) { return ts_create_expression_statement(e); }
static TsNode *PARAM(const char *name, TsNode *question, TsNode *type)
{
	return ts_create_parameter(NULL, NULL, NULL, ID(name), question, type, NULL);
}
/* let/const <decls> as a statement */
static TsNode *VARS(uint32_t flags, TsNodeArray *decls)
{
	return ts_create_variable_statement(NULL, ts_create_variable_declaration_list(decls, flags));
}
static TsNode *VAR(const char *name, TsNode *type, TsNode *init) { return ts_create_variable_declaration(ID(name), type, init); }
static TsNode *PROP(const char *name, TsNode *init) { return ts_create_property_assignment(ID(name), init); }
static TsNode *ARROW(TsNodeArray *params, TsNode *body)
{
	return ts_create_arrow_function(NULL, NULL, params, NULL, TOK(TS_EqualsGreaterThanToken), body);
}

/* ---- typescript-comments.ts ---- */

/* addCommentBlocksAsLeadingDetachedLines */
static void add_comment_blocks_as_leading_detached_lines(TsNode *node, const Str *texts, int n)
{
	for (int i = 0; i < n; i++) {
		Str *lines;
		int nl = str_split_s(texts[i], '\n', &lines);
		for (int j = 0; j < nl; j++) {
			Buf b = {0};
			if (!lines[j].n || lines[j].p[0] != ' ')
				buf_addc(&b, ' ');
			buf_add_str(&b, lines[j]);
			if (j == nl - 1) {
				buf_adds(&b, "\n\n");
				ts_add_synthetic_leading_comment(node, TS_SingleLineCommentTrivia, buf_view(&b), false);
			} else {
				ts_add_synthetic_leading_comment(node, TS_SingleLineCommentTrivia, buf_view(&b), true);
			}
		}
	}
}

/* addCommentBlockAsJsDoc */
static void add_comment_block_as_js_doc(TsNode *node, Str text)
{
	Buf b = {0};
	buf_adds(&b, "*\n");
	Str *lines;
	int n = str_split_s(text, '\n', &lines);
	for (int i = 0; i < n; i++) {
		if (i)
			buf_addc(&b, '\n');
		buf_adds(&b, lines[i].n && lines[i].p[0] == ' ' ? " *" : " * ");
		buf_add_str(&b, lines[i]);
	}
	buf_adds(&b, "\n ");
	/* text.split('*' + '/').join('*\/') */
	Buf e = {0};
	for (size_t i = 0; i < b.n; i++) {
		if (b.p[i] == '*' && i + 1 < b.n && b.p[i + 1] == '/') {
			buf_adds(&e, "*\\/");
			i++;
		} else {
			buf_addc(&e, b.p[i]);
		}
	}
	ts_add_synthetic_leading_comment(node, TS_MultiLineCommentTrivia, buf_view(&e), true);
}

/* ---- typescript-literal-from-value.ts ---- */

/* /^(?![0-9])[a-zA-Z0-9$_]+$/ */
static bool valid_property_key(Str k)
{
	if (!k.n || (k.p[0] >= '0' && k.p[0] <= '9'))
		return false;
	for (size_t i = 0; i < k.n; i++) {
		char c = k.p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_'))
			return false;
	}
	return true;
}

/* typescriptLiteralFromValue (a NULL value is undefined) */
static TsNode *literal_from_value(const JsVal *v)
{
	if (!v)
		return ID("undefined");
	switch (v->t) {
	case JS_UNDEFINED: return ID("undefined");
	case JS_BOOL: return v->b ? ts_create_true() : ts_create_false();
	case JS_STRING: return ts_create_string_literal(v->s);
	case JS_BIGINT: return NUM(xcat(xstrndup(v->s.p, v->s.n), "n"));
	case JS_NUMBER:
		if (v->num != v->num)
			return PA(ID("Number"), "Nan");
		if (v->num > 1.7976931348623157e308)
			return PA(ID("Number"), "POSITIVE_INFINITY");
		if (v->num < -1.7976931348623157e308)
			return PA(ID("Number"), "NEGATIVE_INFINITY");
		return NUM(js_number_to_string(v->num));
	case JS_NULL: return ts_create_null();
	case JS_BYTES:
		if (!v->s.n)
			return ts_create_new(ID("Uint8Array"), NULL, ts_list(NUM("0")));
		fail("unsupported by protoc-gen-uni: a non-empty Uint8Array literal");
	case JS_ARRAY: {
		TsNodeArray *el = ts_list_new();
		for (int i = 0; i < v->items.n; i++)
			ts_list_push(el, literal_from_value(v->items.v[i]));
		return ts_create_array_literal(el, false);
	}
	case JS_OBJECT: {
		TsNodeArray *props = ts_list_new();
		Str *keys;
		int n = js_obj_keys(v, &keys);
		for (int i = 0; i < n; i++) {
			TsNode *name = valid_property_key(keys[i]) ? ID(xstrndup(keys[i].p, keys[i].n))
								    : ts_create_string_literal(keys[i]);
			ts_list_push(props, ts_create_property_assignment(name, literal_from_value(js_obj_get(v, keys[i]))));
		}
		return ts_create_object_literal(props, false);
	}
	}
	return NULL;
}

/* ---- typescript-file.ts, out-file.ts ---- */

/* A protobuf-ts output file. Named imports are indexed so that looking them up does not scan
 * the statements (findNamedImports). */
typedef struct {
	const char *name;
	DescFile *desc_file;
	Vec top;  /* statements added at the top, in the order added (printed last first) */
	Vec body; /* statements added at the end */
	Map named;     /* "name\1from\1typeOnly" -> the imported name (as ?? name) */
	Map taken;     /* names of named imports (as ?? name) */
	Map namespace; /* "as\1from\1typeOnly" */
	Map symbols;   /* names of the symbol table entries in this file */
	Map alias_next; /* name -> where the search for its alternative name resumes */
	const Options *options;
} OutFile;

static void add_statement(OutFile *f, TsNode *s, bool at_top) { vec_push(at_top ? &f->top : &f->body, s); }
static bool out_file_empty(const OutFile *f) { return !f->top.n && !f->body.n; }

/* OutFile.makeHeader */
static void make_header(const OutFile *f, Buf *b)
{
	const Options *o = f->options;
	const PbMsg *proto = f->desc_file->h.proto;
	Vec header = {0};
	const char *pkg = pb_str(proto, FDP_PACKAGE);
	const char *syntax = pb_str(proto, FDP_SYNTAX);
	const char *props = xcat(*pkg ? xcat("package \"", pkg, "\", ") : "", "syntax ", *syntax ? syntax : "proto2");
	if (o->es_lint_disable)
		vec_push(&header, "/* eslint-disable */");
	vec_push(&header, xcat("// @generated ", o->plugin_credit));
	vec_push(&header, xcat("// @generated from protobuf file \"", pb_str(proto, FDP_NAME), "\" (", props, ")"));
	vec_push(&header, "// tslint:disable");
	if (o->ts_nocheck)
		vec_push(&header, "// @ts-nocheck");
	if (f->desc_file->h.deprecated)
		vec_push(&header, "// @deprecated");
	Buf h = {0};
	for (int i = 0; i < header.n; i++) {
		if (i)
			buf_addc(&h, '\n');
		buf_adds(&h, header.v[i]);
	}
	Comments cs[2] = {get_syntax_comments(f->desc_file), get_package_comments(f->desc_file)};
	for (int c = 0; c < 2; c++) {
		for (int i = 0; i < cs[c].n_leading_detached; i++) {
			Str block = cs[c].leading_detached[i];
			if (block.n && block.p[block.n - 1] == '\n')
				block.n--;
			buf_adds(&h, h.n ? "\n//" : "//");
			Str *lines;
			int n = str_split_s(block, '\n', &lines);
			for (int j = 0; j < n; j++) {
				buf_adds(&h, "\n//");
				buf_add_str(&h, lines[j]);
			}
			buf_adds(&h, "\n//");
		}
	}
	if (h.n && h.p[h.n - 1] != '\n')
		buf_addc(&h, '\n');
	buf_add_str(b, buf_view(&h));
}

/* OutFile.getContent: "" for a file without statements. */
static Str out_file_content(const OutFile *f)
{
	if (out_file_empty(f))
		return (Str){"", 0};
	Buf b = {0};
	make_header(f, &b);
	TsNodeArray *st = ts_list_new();
	for (int i = f->top.n - 1; i >= 0; i--)
		ts_list_push(st, f->top.v[i]);
	for (int i = 0; i < f->body.n; i++)
		ts_list_push(st, f->body.v[i]);
	ts_print_file(ts_create_source_file(st), &b);
	return buf_view(&b);
}

/* ---- file-table.ts ---- */

typedef struct {
	Vec out_files; /* OutFile* */
	Map names;     /* registered names */
	Map entries;   /* "protoFilename\1kind" -> name */
	const Options *options;
} FileTable;

COLD static void file_table_register(FileTable *t, const char *requested, DescFile *file, const char *kind)
{
	const char *key = xcat(pb_str(file->h.proto, FDP_NAME), "\1", kind);
	if (map_has(&t->entries, key))
		fail("Error: Cannot register name \"%s\" of kind \"%s\" for file %s. The descriptor is already registered "
		     "with name \"%s\". Use a different 'kind' to register multiple symbols for a descriptor.",
		     requested, kind, pb_str(file->h.proto, FDP_NAME), (const char *)map_get(&t->entries, key));
	const char *name = requested;
	for (int count = 0; map_has(&t->names, name) && count < 100;) {
		/* defaultClashResolver */
		const char *dot = strrchr(requested, '.');
		bool has_dot = dot && dot > requested;
		char *base = has_dot ? xstrndup(requested, (size_t)(dot - requested)) : xstrdup(requested);
		const char *suffix = has_dot ? dot : "";
		if (*base && base[strlen(base) - 1] == '$')
			base++;
		name = xfmt("%s$%d%s", base, ++count, suffix);
	}
	if (map_has(&t->names, name))
		fail("Error: Failed to register name \"%s\" for file %s. Gave up finding alternative name after 100 tries. "
		     "There is something wrong with the clash resolver.",
		     requested, pb_str(file->h.proto, FDP_NAME));
	map_put(&t->names, name, (void *)1);
	map_put(&t->entries, key, (void *)name);
}

static OutFile *file_table_create(FileTable *t, DescFile *file, const char *kind)
{
	const char *name = map_get(&t->entries, xcat(pb_str(file->h.proto, FDP_NAME), "\1", kind));
	if (!name)
		fail("Error: Failed to find name for file %s of kind \"%s\".", pb_str(file->h.proto, FDP_NAME), kind);
	OutFile *f = xalloc(sizeof *f);
	f->name = name;
	f->desc_file = file;
	f->options = t->options;
	vec_push(&t->out_files, f);
	return f;
}

/* ---- symbol-table.ts ---- */

typedef struct {
	OutFile *file;
	const char *name;
} SymbolEntry;

typedef struct {
	Map entries; /* "typeName\1kind" -> SymbolEntry* */
} SymbolTable;

static const char *desc_type_name(const Desc *d)
{
	switch (d->kind) {
	case DK_MESSAGE: return ((const DescMessage *)d)->type_name;
	case DK_ENUM: return ((const DescEnum *)d)->type_name;
	case DK_SERVICE: return ((const DescService *)d)->type_name;
	}
	return "";
}

COLD static const char *symbols_register(SymbolTable *t, const char *requested, const Desc *d, OutFile *file,
					 const char *kind)
{
	const char *key = xcat(desc_type_name(d), "\1", kind);
	SymbolEntry *e = map_get(&t->entries, key);
	if (e)
		fail("Error: Cannot register name \"%s\" of kind \"%s\" for %s. The descriptor is already registered in "
		     "file \"%s\" with name \"%s\". Use a different 'kind' to register multiple symbols for a descriptor.",
		     requested, kind, desc_string(d), e->file->name, e->name);
	const char *name = requested;
	for (int count = 0; map_has(&file->symbols, name) && count < 100;) {
		/* defaultClashResolver */
		const char *n = requested;
		if (*n && n[strlen(n) - 1] == '$')
			n++;
		name = xfmt("%s$%d", n, ++count);
	}
	if (map_has(&file->symbols, name))
		fail("Error: Failed to register name \"%s\" for %s. Gave up finding alternative name after 100 tries. There "
		     "is something wrong with the clash resolver.",
		     requested, desc_string(d));
	e = xalloc(sizeof *e);
	e->file = file;
	e->name = name;
	map_put(&t->entries, key, e);
	map_put(&file->symbols, name, (void *)1);
	return name;
}

static SymbolEntry *symbols_get(SymbolTable *t, const Desc *d, const char *kind)
{
	SymbolEntry *e = map_get(&t->entries, xcat(desc_type_name(d), "\1", kind));
	if (!e)
		fail("Error: Failed to find name for %s of kind \"%s\".", desc_string(d), kind);
	return e;
}

/* ---- typescript-imports.ts ---- */

typedef struct {
	SymbolTable *symbols;
	Registry *registry;
} Imports;

/* createRelativeImportPath */
static const char *relative_import_path(const char *current, const char *target)
{
	/* path.relative(path.dirname(current), target) for the relative POSIX paths of proto files */
	Vec from = str_split(current, '/'), to = str_split(target, '/');
	from.n--; /* dirname */
	Vec f = {0}, t = {0};
	for (int i = 0; i < from.n; i++)
		if (*(const char *)from.v[i] && !str_eq(from.v[i], "."))
			vec_push(&f, from.v[i]);
	for (int i = 0; i < to.n; i++)
		if (*(const char *)to.v[i] && !str_eq(to.v[i], "."))
			vec_push(&t, to.v[i]);
	int common = 0;
	while (common < f.n && common < t.n && str_eq(f.v[common], t.v[common]))
		common++;
	Vec parts = {0};
	for (int i = common; i < f.n; i++)
		vec_push(&parts, "..");
	for (int i = common; i < t.n; i++)
		vec_push(&parts, t.v[i]);
	char *rel = str_join(&parts, "/");
	/* drop file extension: .replace(/\.[a-z]+$/, '') */
	size_t n = strlen(rel), k = n;
	while (k > 0 && rel[k - 1] >= 'a' && rel[k - 1] <= 'z')
		k--;
	if (k < n && k > 0 && rel[k - 1] == '.')
		rel[k - 1] = 0;
	if (!has_prefix(rel, "../") && !has_prefix(rel, "./"))
		return xcat("./", rel);
	return rel;
}

/* createNamedImport */
static TsNode *create_named_import(const char *name, const char *from, const char *as, bool type_only)
{
	TsNode *spec = as ? ts_create_import_specifier(ID(name), ID(as)) : ts_create_import_specifier(NULL, ID(name));
	return ts_create_import_declaration(NULL, NULL,
					    ts_create_import_clause(NULL, ts_create_named_imports(ts_list(spec)), type_only),
					    STR(from));
}

/* ensureNamedImportPresent */
static const char *ensure_named_import(OutFile *f, const char *name, const char *from, bool type_only)
{
	const char *key = xcat(name, "\1", from, "\1", type_only ? "t" : "v");
	const char *match = map_get(&f->named, key);
	if (match)
		return match;
	const char *as = NULL;
#define TAKEN(n) (map_has(&f->taken, n) || map_has(&f->symbols, n))
	if (TAKEN(name)) {
		/* the candidates name$, name$2, name$3, ...; names stay taken, so the search for the same
		 * name resumes at the last candidate found instead of starting over */
		int i = (int)(intptr_t)map_get(&f->alias_next, name);
		as = name;
		while (TAKEN(as)) {
			as = xcat(name, "$");
			if (i++ > 0)
				as = xfmt("%s%d", as, i);
		}
		map_put(&f->alias_next, name, (void *)(intptr_t)(i - 1));
	}
#undef TAKEN
	add_statement(f, create_named_import(name, from, as, type_only), true);
	const char *r = as ? as : name;
	map_put(&f->named, key, (void *)r);
	map_put(&f->taken, r, (void *)1);
	return r;
}

/* TypeScriptImports.name */
static const char *import_name(OutFile *f, const char *name, const char *from, bool type_only)
{
	return ensure_named_import(f, name, from, type_only);
}

/* TypeScriptImports.namespace */
static const char *import_namespace(OutFile *f, const char *as, const char *from, bool type_only)
{
	const char *key = xcat(as, "\1", from, "\1", type_only ? "t" : "v");
	if (!map_has(&f->namespace, key)) {
		TsNode *imp = ts_create_import_declaration(
			NULL, NULL, ts_create_import_clause(NULL, ts_create_namespace_import(ID(as)), type_only), STR(from));
		add_statement(f, imp, true);
		map_put(&f->namespace, key, (void *)1);
	}
	return as;
}

/* TypeScriptImports.type / typeByName */
static const char *import_type(Imports *im, OutFile *f, const Desc *d, const char *kind, bool type_only)
{
	SymbolEntry *e = symbols_get(im->symbols, d, kind);
	if (e->file == f)
		return e->name;
	return ensure_named_import(f, e->name, relative_import_path(f->name, e->file->name), type_only);
}

/* ---- typescript-enum-builder.ts ---- */

typedef struct {
	Vec names, numbers, comments; /* const char *, (intptr_t), Str* (NULL: none) */
} EnumBuilder;

static void enum_builder_add(EnumBuilder *b, const char *name, int32_t number, Str *comment)
{
	vec_push(&b->names, (void *)name);
	vec_push(&b->numbers, (void *)(intptr_t)number);
	vec_push(&b->comments, comment);
}

static TsNode *enum_builder_build(EnumBuilder *b, const char *name, TsNodeArray *modifiers)
{
	int32_t *nums = xalloc(sizeof *nums * (size_t)(b->numbers.n + 1));
	for (int i = 0; i < b->numbers.n; i++)
		nums[i] = (int32_t)(intptr_t)b->numbers.v[i];
	rt_enum_object((const char **)b->names.v, nums, b->names.n); /* validate() */
	TsNodeArray *members = ts_list_new();
	for (int i = 0; i < b->names.n; i++) {
		TsNode *m = ts_create_enum_member(ID(b->names.v[i]), NUMI(nums[i]));
		Str *c = b->comments.v[i];
		if (c && c->n)
			add_comment_block_as_js_doc(m, *c);
		ts_list_push(members, m);
	}
	return ts_create_enum_declaration(NULL, modifiers, ID(name), members);
}
