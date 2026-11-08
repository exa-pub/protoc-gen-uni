/* Port of src/code_generator.rs (and src/code_generator/syntax.rs) of prost-build 0.14.1
 * (https://github.com/tokio-rs/prost, Apache-2.0): the code of one input file, built as plain
 * strings (protoc-gen-prost turns the `format` feature off, so nothing is pretty-printed). No
 * service generator is configured, so services produce nothing. */

enum { SYNTAX_PROTO2, SYNTAX_PROTO3 };

/* A SourceCodeInfo.Location with its path. */
typedef struct {
	int32_t *path;
	int n;
	const PbMsg *loc;
} Loc;

typedef struct {
	int n, cap;
	int32_t *v;
} PathVec;

typedef struct {
	Context *ctx;
	const char *package;
	Vec type_path; /* const char *: enclosing message names */
	bool has_source_info;
	Loc **locs; /* sorted by path */
	int n_locs;
	int syntax;
	int depth;
	PathVec path;
	Buf *buf;
} CodeGen;

/* A message field with the index of its path. */
typedef struct {
	const PbMsg *d;
	int path_index;
} Field;

typedef struct {
	const PbMsg *d; /* OneofDescriptorProto */
	Field *fields;
	int n_fields;
	int path_index;
} OneofField;

static void path_push(PathVec *p, int32_t x)
{
	if (p->n == p->cap) {
		int cap = p->cap ? p->cap * 2 : 16;
		p->v = xrealloc(p->v, sizeof *p->v * (size_t)p->cap, sizeof *p->v * (size_t)cap);
		p->cap = cap;
	}
	p->v[p->n++] = x;
}

static void path_pop(PathVec *p) { p->n--; }

/* Vec<i32> ordering. */
static int path_cmp(const int32_t *a, int an, const int32_t *b, int bn)
{
	for (int i = 0; i < an && i < bn; i++)
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	return an < bn ? -1 : an > bn ? 1 : 0;
}

/* Stable merge sort (slice::sort_by). */
static void sort_locs(Loc **v, Loc **tmp, int n)
{
	if (n < 2)
		return;
	int h = n / 2;
	sort_locs(v, tmp, h);
	sort_locs(v + h, tmp, n - h);
	int i = 0, j = h, k = 0;
	while (i < h && j < n)
		tmp[k++] = path_cmp(v[j]->path, v[j]->n, v[i]->path, v[i]->n) < 0 ? v[j++] : v[i++];
	while (i < h)
		tmp[k++] = v[i++];
	while (j < n)
		tmp[k++] = v[j++];
	memcpy(v, tmp, sizeof *v * (size_t)n);
}

static void push_indent_n(Buf *buf, int depth)
{
	for (int i = 0; i < depth; i++)
		buf_adds(buf, "    ");
}

static void push_indent(CodeGen *g) { push_indent_n(g->buf, g->depth); }

static const char *prost_path(void) { return "::prost"; }

/* Returns the fully-qualified name, starting with a dot. */
static char *fq_name(CodeGen *g, const char *message_name)
{
	Buf b = {0};
	if (*g->package) {
		buf_addc(&b, '.');
		/* trim_matches('.') */
		const char *s = g->package, *e = s + strlen(s);
		while (s < e && *s == '.')
			s++;
		while (e > s && e[-1] == '.')
			e--;
		buf_add(&b, s, (size_t)(e - s));
	}
	if (g->type_path.n)
		buf_addc(&b, '.');
	buf_adds(&b, str_join(&g->type_path, "."));
	buf_addc(&b, '.');
	buf_adds(&b, message_name);
	return xstrdup(buf_str(&b));
}

/* location(): the location of the current path (binary_search_by of Rust 1.91, unwrapped). */
static const PbMsg *location(CodeGen *g)
{
	if (!g->has_source_info)
		return NULL;
	int size = g->n_locs;
	if (!size)
		fail("called `Result::unwrap()` on an `Err` value: 0");
	int base = 0;
	while (size > 1) {
		int half = size / 2, mid = base + half;
		if (path_cmp(g->locs[mid]->path, g->locs[mid]->n, g->path.v, g->path.n) <= 0)
			base = mid;
		size -= half;
	}
	int c = path_cmp(g->locs[base]->path, g->locs[base]->n, g->path.v, g->path.n);
	if (c)
		fail("called `Result::unwrap()` on an `Err` value: %d", base + (c < 0));
	return g->locs[base]->loc;
}

static void append_doc(CodeGen *g, const char *fq_name, const char *field_name)
{
	if (should_disable_comments(g->ctx, fq_name, field_name))
		return;
	const PbMsg *loc = location(g);
	if (!loc)
		return;
	ProstComments c = prost_comments_from_location(loc);
	prost_comments_append_with_indent(&c, g->depth, g->buf);
}

static void append_attributes_of(CodeGen *g, const PathMap *pm, const FullPath *path)
{
	int i = 0;
	for (const char *attr; (attr = path_map_next(pm, path, &i));) {
		push_indent(g);
		buf_adds(g->buf, attr);
		buf_addc(g->buf, '\n');
	}
}

static void append_attributes(CodeGen *g, const PathMap *pm, const char *path)
{
	FullPath p = full_path(path, NULL, NULL);
	append_attributes_of(g, pm, &p);
}

static void append_type_attributes(CodeGen *g, const char *fq) { append_attributes(g, &g->ctx->config->type_attributes, fq); }
static void append_message_attributes(CodeGen *g, const char *fq) { append_attributes(g, &g->ctx->config->message_attributes, fq); }
static void append_enum_attributes(CodeGen *g, const char *fq) { append_attributes(g, &g->ctx->config->enum_attributes, fq); }

static void append_field_attributes(CodeGen *g, const char *fq, const char *field)
{
	FullPath p = full_path(fq, field, NULL);
	append_attributes_of(g, &g->ctx->config->field_attributes, &p);
}

static void append_skip_debug(CodeGen *g, const char *fq)
{
	if (should_skip_debug(g->ctx, fq)) {
		push_indent(g);
		buf_adds(g->buf, "#[prost(skip_debug)]\n");
	}
}

/* A cursor over the elements of a dot-separated string (str::split('.')). */
typedef struct {
	const char *p; /* the current element; NULL at the end */
	size_t n;      /* its length */
} Split;

static Split split_first(const char *s)
{
	const char *dot = strchr(s, '.');
	return (Split){s, dot ? (size_t)(dot - s) : strlen(s)};
}

static void split_next(Split *it)
{
	if (it->p[it->n] == '.')
		*it = split_first(it->p + it->n + 1);
	else
		it->p = NULL;
}

static char *resolve_ident(CodeGen *g, const char *pb_ident)
{
	assert_fully_qualified(pb_ident);
	char *ext = resolve_extern_ident(g->ctx, pb_ident);
	if (ext)
		return ext;
	/* local_path: the elements of the package, then type_path (li); an empty first element (no
	 * package) is ignored. ident_path: the elements of pb_ident[1..] but the last one. */
	Split pkg = split_first(g->package);
	if (!pkg.n)
		split_next(&pkg);
	int li = 0;
	const char *ident_type = strrchr(pb_ident, '.') + 1;
	Split ident = split_first(pb_ident + 1);
	if (ident.p == ident_type)
		ident.p = NULL;
	/* Skip path elements in common. */
	while (ident.p && (pkg.p || li < g->type_path.n)) {
		const char *l = pkg.p ? pkg.p : g->type_path.v[li];
		size_t ln = pkg.p ? pkg.n : strlen(l);
		if (ln != ident.n || memcmp(l, ident.p, ln))
			break;
		if (pkg.p)
			split_next(&pkg);
		else
			li++;
		split_next(&ident);
		if (ident.p == ident_type)
			ident.p = NULL;
	}
	Buf b = {0};
	bool first = true;
	for (; pkg.p; split_next(&pkg), first = false)
		buf_adds(&b, first ? "super" : "::super");
	for (; li < g->type_path.n; li++, first = false)
		buf_adds(&b, first ? "super" : "::super");
	for (; ident.p; first = false) {
		if (!first)
			buf_adds(&b, "::");
		buf_adds(&b, to_snake(xstrndup(ident.p, ident.n)));
		split_next(&ident);
		if (ident.p == ident_type)
			ident.p = NULL;
	}
	if (!first)
		buf_adds(&b, "::");
	buf_adds(&b, to_upper_camel(ident_type));
	return xstrdup(buf_str(&b));
}

static char *bytes_rust_type(CodeGen *g, const char *fq, const char *field)
{
	return is_bytes_bytes(g->ctx, fq, field) ? "::prost::bytes::Bytes" : "::prost::alloc::vec::Vec<u8>";
}

static char *resolve_type(CodeGen *g, const PbMsg *field, const char *fq_message_name)
{
	switch (field_type(field)) {
	case PT_FLOAT: return "f32";
	case PT_DOUBLE: return "f64";
	case PT_UINT32: case PT_FIXED32: return "u32";
	case PT_UINT64: case PT_FIXED64: return "u64";
	case PT_INT32: case PT_SFIXED32: case PT_SINT32: case PT_ENUM: return "i32";
	case PT_INT64: case PT_SFIXED64: case PT_SINT64: return "i64";
	case PT_BOOL: return "bool";
	case PT_STRING: return xcat(prost_path(), "::alloc::string::String");
	case PT_BYTES: return bytes_rust_type(g, fq_message_name, field_name(field));
	default: return resolve_ident(g, field_type_name(field)); /* group, message */
	}
}

static char *field_type_tag(CodeGen *g, const PbMsg *field)
{
	static const char *const tags[] = {
		[PT_FLOAT] = "float", [PT_DOUBLE] = "double", [PT_INT32] = "int32", [PT_INT64] = "int64",
		[PT_UINT32] = "uint32", [PT_UINT64] = "uint64", [PT_SINT32] = "sint32", [PT_SINT64] = "sint64",
		[PT_FIXED32] = "fixed32", [PT_FIXED64] = "fixed64", [PT_SFIXED32] = "sfixed32",
		[PT_SFIXED64] = "sfixed64", [PT_BOOL] = "bool", [PT_STRING] = "string", [PT_BYTES] = "bytes",
		[PT_GROUP] = "group", [PT_MESSAGE] = "message",
	};
	int t = field_type(field);
	if (t == PT_ENUM) {
		Buf b = {0};
		buf_adds(&b, "enumeration=");
		/* non-ASCII only from an extern_path target, used as is */
		debug_ascii(&b, resolve_ident(g, field_type_name(field)), "extern_path target");
		return xstrdup(buf_str(&b));
	}
	return (char *)tags[t];
}

static char *map_value_type_tag(CodeGen *g, const PbMsg *field)
{
	if (field_type(field) == PT_ENUM)
		return xcat("enumeration(", resolve_ident(g, field_type_name(field)), ")");
	return field_type_tag(g, field);
}

static bool optional(CodeGen *g, const PbMsg *field)
{
	if (pb_int(field, FD_PROTO3_OPTIONAL, 0))
		return true;
	if (field_label(field) != LABEL_OPTIONAL)
		return false;
	return field_type(field) == PT_MESSAGE || g->syntax == SYNTAX_PROTO2;
}

static bool deprecated(const PbMsg *field) { return pb_int(pb_msg(field, FD_OPTIONS), FO_DEPRECATED, 0) != 0; }

/* Returns true if the repeated field type can be packed. */
static bool can_pack(const PbMsg *field)
{
	int t = field_type(field);
	return t != PT_STRING && t != PT_GROUP && t != PT_MESSAGE && t != PT_BYTES;
}

static void append_field(CodeGen *g, const char *fq_message_name, const Field *field)
{
	const PbMsg *d = field->d;
	int type = field_type(d);
	bool repeated = field_label(d) == LABEL_REPEATED;
	bool is_deprecated = deprecated(d);
	bool is_optional = optional(g, d);
	bool boxed = should_box_message_field(g->ctx, fq_message_name, d);
	char *ty = resolve_type(g, d, fq_message_name);
	Buf *b = g->buf;

	append_doc(g, fq_message_name, field_name(d));
	if (is_deprecated) {
		push_indent(g);
		buf_adds(b, "#[deprecated]\n");
	}
	push_indent(g);
	buf_adds(b, "#[prost(");
	buf_adds(b, field_type_tag(g, d));
	if (type == PT_BYTES)
		buf_adds(b, is_bytes_bytes(g->ctx, fq_message_name, field_name(d)) ? "=\"bytes\"" : "=\"vec\"");
	switch (field_label(d)) {
	case LABEL_OPTIONAL:
		if (is_optional)
			buf_adds(b, ", optional");
		break;
	case LABEL_REQUIRED:
		buf_adds(b, ", required");
		break;
	default:
		buf_adds(b, ", repeated");
		if (can_pack(d)) {
			const PbMsg *options = pb_msg(d, FD_OPTIONS);
			bool packed = options ? pb_int(options, FO_PACKED, 0) != 0 : g->syntax == SYNTAX_PROTO3;
			if (!packed)
				buf_adds(b, ", packed=\"false\"");
		}
	}
	if (boxed)
		buf_adds(b, ", boxed");
	buf_adds(b, ", tag=\"");
	buf_fmt(b, "%d", (int32_t)pb_int(d, FD_NUMBER, 0));
	if (pb_has(d, FD_DEFAULT_VALUE)) {
		Str def = pb_bytes(d, FD_DEFAULT_VALUE);
		buf_adds(b, "\", default=\"");
		if (type == PT_BYTES) {
			buf_adds(b, "b\\\"");
			Buf bytes = {0};
			unescape_c_escape_string(def, &bytes);
			for (size_t i = 0; i < bytes.n; i++) {
				Buf esc = {0};
				rust_ascii_escape_default(&esc, (uint8_t)bytes.p[i]);
				rust_str_escape_default(b, buf_view(&esc));
			}
			buf_adds(b, "\\\"");
		} else if (type == PT_ENUM) {
			char *enum_value = to_upper_camel(def.p);
			if (g->ctx->config->strip_enum_prefix) {
				/* Field types are fully qualified: strip the last segment from the left side of
				 * the default value. */
				if (!pb_has(d, FD_TYPE_NAME))
					fail("called `Option::unwrap()` on a `None` value");
				const char *ty_name = field_type_name(d), *dot = strrchr(ty_name, '.');
				const char *enum_type = dot ? dot + 1 : ty_name;
				enum_value = prost_strip_enum_prefix(to_upper_camel(enum_type), enum_value);
			}
			buf_adds(b, enum_value);
		} else {
			rust_str_escape_default(b, def);
		}
	}
	buf_adds(b, "\")]\n");
	append_field_attributes(g, fq_message_name, field_name(d));
	push_indent(g);
	buf_adds(b, "pub ");
	buf_adds(b, to_snake(field_name(d)));
	buf_adds(b, ": ");
	if (repeated)
		buf_adds(b, xcat(prost_path(), "::alloc::vec::Vec<"));
	else if (is_optional)
		buf_adds(b, "::core::option::Option<");
	if (boxed)
		buf_adds(b, xcat(prost_path(), "::alloc::boxed::Box<"));
	buf_adds(b, ty);
	if (boxed)
		buf_addc(b, '>');
	if (repeated || is_optional)
		buf_addc(b, '>');
	buf_adds(b, ",\n");
}

static void append_map_field(CodeGen *g, const char *fq_message_name, const Field *field, const PbMsg *key,
			     const PbMsg *value)
{
	char *key_ty = resolve_type(g, key, fq_message_name);
	char *value_ty = resolve_type(g, value, fq_message_name);
	const PbMsg *d = field->d;
	append_doc(g, fq_message_name, field_name(d));
	push_indent(g);
	bool btree = is_btree_map(g->ctx, fq_message_name, field_name(d));
	char *key_tag = field_type_tag(g, key);
	char *value_tag = map_value_type_tag(g, value);
	buf_adds(g->buf, xcat("#[prost(", btree ? "btree_map" : "map", "=\"", key_tag, ", ", value_tag, "\", tag=\""));
	buf_fmt(g->buf, "%d\")]\n", (int32_t)pb_int(d, FD_NUMBER, 0));
	append_field_attributes(g, fq_message_name, field_name(d));
	push_indent(g);
	buf_adds(g->buf, xcat("pub ", to_snake(field_name(d)), ": ",
			      btree ? "::prost::alloc::collections::BTreeMap" : "::std::collections::HashMap", "<", key_ty,
			      ", ", value_ty, ">,\n"));
}

static void append_oneof_field(CodeGen *g, const char *message_name, const char *fq_message_name, const OneofField *oneof)
{
	const char *oneof_name = pb_str(oneof->d, ODP_NAME);
	char *type_name = xcat(to_snake(message_name), "::", to_upper_camel(oneof_name));
	append_doc(g, fq_message_name, NULL);
	push_indent(g);
	buf_adds(g->buf, xcat("#[prost(oneof=\"", type_name, "\", tags=\""));
	for (int i = 0; i < oneof->n_fields; i++)
		buf_fmt(g->buf, i ? ", %d" : "%d", (int32_t)pb_int(oneof->fields[i].d, FD_NUMBER, 0));
	buf_adds(g->buf, "\")]\n");
	append_field_attributes(g, fq_message_name, oneof_name);
	push_indent(g);
	buf_adds(g->buf, xcat("pub ", to_snake(oneof_name), ": ::core::option::Option<", type_name, ">,\n"));
}

static void append_oneof(CodeGen *g, const char *fq_message_name, const OneofField *oneof)
{
	path_push(&g->path, 8);
	path_push(&g->path, oneof->path_index);
	append_doc(g, fq_message_name, NULL);
	path_pop(&g->path);
	path_pop(&g->path);

	const char *name = pb_str(oneof->d, ODP_NAME);
	char *oneof_name = xcat(fq_message_name, ".", name);
	append_type_attributes(g, oneof_name);
	append_enum_attributes(g, oneof_name);
	push_indent(g);

	bool copy = true, eq = true;
	for (int i = 0; copy && i < oneof->n_fields; i++)
		copy = can_field_derive_copy(g->ctx, fq_message_name, oneof->fields[i].d);
	for (int i = 0; eq && i < oneof->n_fields; i++)
		eq = can_field_derive_eq(g->ctx, fq_message_name, oneof->fields[i].d);
	buf_adds(g->buf, xcat("#[derive(Clone, ", copy ? "Copy, " : "", "PartialEq, ", eq ? "Eq, Hash, " : "",
			      prost_path(), "::Oneof)]\n"));
	append_skip_debug(g, fq_message_name);
	push_indent(g);
	buf_adds(g->buf, xcat("pub enum ", to_upper_camel(name), " {\n"));

	path_push(&g->path, 2);
	g->depth++;
	for (int i = 0; i < oneof->n_fields; i++) {
		const Field *field = &oneof->fields[i];
		const PbMsg *d = field->d;
		path_push(&g->path, field->path_index);
		append_doc(g, fq_message_name, field_name(d));
		path_pop(&g->path);

		push_indent(g);
		buf_adds(g->buf, xcat("#[prost(", field_type_tag(g, d), ", tag=\""));
		buf_fmt(g->buf, "%d\")]\n", (int32_t)pb_int(d, FD_NUMBER, 0));
		append_field_attributes(g, oneof_name, field_name(d));

		push_indent(g);
		char *ty = resolve_type(g, d, fq_message_name);
		bool boxed = should_box_oneof_field(g->ctx, fq_message_name, name, d);
		if (boxed)
			buf_adds(g->buf, xcat(to_upper_camel(field_name(d)), "(::prost::alloc::boxed::Box<", ty, ">),\n"));
		else
			buf_adds(g->buf, xcat(to_upper_camel(field_name(d)), "(", ty, "),\n"));
	}
	g->depth--;
	path_pop(&g->path);

	push_indent(g);
	buf_adds(g->buf, "}\n");
}

static void push_mod(CodeGen *g, const char *module)
{
	push_indent(g);
	buf_adds(g->buf, xcat("/// Nested message and enum types in `", module, "`.\n"));
	push_indent(g);
	buf_adds(g->buf, xcat("pub mod ", to_snake(module), " {\n"));
	vec_push(&g->type_path, (void *)module);
	g->depth++;
}

static void pop_mod(CodeGen *g)
{
	g->depth--;
	g->type_path.n--;
	push_indent(g);
	buf_adds(g->buf, "}\n");
}

typedef struct {
	int path_idx;
	const char *proto_name;
	int32_t proto_number;
	char *generated_variant_name;
} EnumVariantMapping;

static int build_enum_value_mappings(const char *generated_enum_name, bool do_strip_enum_prefix,
				     const PbMsg *desc, EnumVariantMapping **out)
{
	int n = pb_count(desc, EDP_VALUE);
	EnumVariantMapping *m = xalloc(sizeof *m * (size_t)(n ? n : 1));
	Map generated_names = {0}, numbers = {0};
	int k = 0;
	for (int i = 0; i < n; i++) {
		const PbMsg *value = pb_msg_at(desc, EDP_VALUE, i);
		int32_t number = (int32_t)pb_int(value, EVDP_NUMBER, 0);
		/* Skip duplicate enum values (allow_alias). */
		Buf key = {0};
		buf_fmt(&key, "%d", number);
		if (map_has(&numbers, buf_str(&key)))
			continue;
		map_put(&numbers, buf_str(&key), (void *)1);
		const char *name = pb_str(value, EVDP_NAME);
		char *variant = to_upper_camel(name);
		if (do_strip_enum_prefix)
			variant = prost_strip_enum_prefix(generated_enum_name, variant);
		const char *old = map_get(&generated_names, variant);
		if (old)
			fail("Generated enum variant names overlap: `%s` variant name to be used both by `%s` and `%s` "
			     "ProtoBuf enum values",
			     variant, old, name);
		map_put(&generated_names, variant, (void *)name);
		m[k++] = (EnumVariantMapping){i, name, number, variant};
	}
	*out = m;
	return k;
}

static void append_enum(CodeGen *g, const PbMsg *desc)
{
	const char *proto_enum_name = pb_str(desc, EDP_NAME);
	char *enum_name = to_upper_camel(proto_enum_name);
	char *fq = fq_name(g, proto_enum_name);
	if (resolve_extern_ident(g->ctx, fq))
		return;
	Buf *b = g->buf;

	append_doc(g, fq, NULL);
	append_type_attributes(g, fq);
	append_enum_attributes(g, fq);
	push_indent(g);
	const char *dbg = should_skip_debug(g->ctx, fq) ? "" : "Debug, ";
	buf_adds(b, xcat("#[derive(Clone, Copy, ", dbg, "PartialEq, Eq, Hash, PartialOrd, Ord, ", prost_path(),
			 "::Enumeration)]\n"));
	push_indent(g);
	buf_adds(b, "#[repr(i32)]\n");
	push_indent(g);
	buf_adds(b, xcat("pub enum ", enum_name, " {\n"));

	EnumVariantMapping *variants;
	int n = build_enum_value_mappings(enum_name, g->ctx->config->strip_enum_prefix, desc, &variants);

	g->depth++;
	path_push(&g->path, 2);
	for (int i = 0; i < n; i++) {
		path_push(&g->path, variants[i].path_idx);
		append_doc(g, fq, variants[i].proto_name);
		append_field_attributes(g, fq, variants[i].proto_name);
		push_indent(g);
		buf_adds(b, variants[i].generated_variant_name);
		buf_fmt(b, " = %d,\n", variants[i].proto_number);
		path_pop(&g->path);
	}
	path_pop(&g->path);
	g->depth--;

	push_indent(g);
	buf_adds(b, "}\n");

	push_indent(g);
	buf_adds(b, xcat("impl ", enum_name, " {\n"));
	g->depth++;
	path_push(&g->path, 2);

	push_indent(g);
	buf_adds(b, "/// String value of the enum field names used in the ProtoBuf definition.\n");
	push_indent(g);
	buf_adds(b, "///\n");
	push_indent(g);
	buf_adds(b, "/// The values are not transformed in any way and thus are considered stable\n");
	push_indent(g);
	buf_adds(b, "/// (if the ProtoBuf definition does not change) and safe for programmatic use.\n");
	push_indent(g);
	buf_adds(b, "pub fn as_str_name(&self) -> &'static str {\n");
	g->depth++;
	push_indent(g);
	buf_adds(b, "match self {\n");
	g->depth++;
	for (int i = 0; i < n; i++) {
		push_indent(g);
		buf_adds(b, xcat("Self::", variants[i].generated_variant_name, " => \"", variants[i].proto_name, "\",\n"));
	}
	g->depth--;
	push_indent(g);
	buf_adds(b, "}\n"); /* End of match */
	g->depth--;
	push_indent(g);
	buf_adds(b, "}\n"); /* End of as_str_name() */

	push_indent(g);
	buf_adds(b, "/// Creates an enum from field names used in the ProtoBuf definition.\n");
	push_indent(g);
	buf_adds(b, "pub fn from_str_name(value: &str) -> ::core::option::Option<Self> {\n");
	g->depth++;
	push_indent(g);
	buf_adds(b, "match value {\n");
	g->depth++;
	for (int i = 0; i < n; i++) {
		push_indent(g);
		buf_adds(b, xcat("\"", variants[i].proto_name, "\" => Some(Self::", variants[i].generated_variant_name, "),\n"));
	}
	push_indent(g);
	buf_adds(b, "_ => None,\n");
	g->depth--;
	push_indent(g);
	buf_adds(b, "}\n"); /* End of match */
	g->depth--;
	push_indent(g);
	buf_adds(b, "}\n"); /* End of from_str_name() */

	path_pop(&g->path);
	g->depth--;
	push_indent(g);
	buf_adds(b, "}\n"); /* End of impl */
}

static void append_type_name(CodeGen *g, const char *message_name, const char *fq_message_name)
{
	(void)fq_message_name; /* type_name_domain: no domains are configured */
	Buf *b = g->buf;
	buf_adds(b, xcat("impl ", prost_path(), "::Name for ", to_upper_camel(message_name), " {\n"));
	buf_adds(b, xcat("const NAME: &'static str = \"", message_name, "\";\n"));
	buf_adds(b, xcat("const PACKAGE: &'static str = \"", g->package, "\";\n"));
	const char *string_path = xcat(prost_path(), "::alloc::string::String");
	Buf full = {0};
	const char *s = g->package, *e = s + strlen(s);
	while (s < e && *s == '.')
		s++;
	while (e > s && e[-1] == '.')
		e--;
	buf_add(&full, s, (size_t)(e - s));
	if (*g->package)
		buf_addc(&full, '.');
	buf_adds(&full, str_join(&g->type_path, "."));
	if (g->type_path.n)
		buf_addc(&full, '.');
	buf_adds(&full, message_name);
	const char *full_name = buf_str(&full);
	buf_adds(b, xcat("fn full_name() -> ", string_path, " { \"", full_name, "\".into() }"));
	buf_adds(b, xcat("fn type_url() -> ", string_path, " { \"/", full_name, "\".into() }"));
	buf_adds(b, "}\n");
}

static void append_message(CodeGen *g, const PbMsg *message)
{
	const char *message_name = pb_str(message, DP_NAME);
	char *fq_message_name = fq_name(g, message_name);

	/* Skip external types. */
	if (resolve_extern_ident(g->ctx, fq_message_name))
		return;

	/* Split the nested message types into normal nested types and map entries; the path index is
	 * kept for comments. */
	int n_nested = pb_count(message, DP_NESTED_TYPE);
	int *nested_types = xalloc(sizeof *nested_types * (size_t)(n_nested ? n_nested : 1));
	int n_nested_types = 0;
	Map map_types = {0}; /* entry name -> PbMsg *[2] (key, value) */
	for (int i = 0; i < n_nested; i++) {
		const PbMsg *nested = pb_msg_at(message, DP_NESTED_TYPE, i);
		if (pb_int(pb_msg(nested, DP_OPTIONS), 7 /* map_entry */, 0)) {
			int nf = pb_count(nested, DP_FIELD);
			if (nf < 2)
				fail("index out of bounds: the len is %d but the index is %d", nf, nf);
			const PbMsg **kv = xalloc(sizeof *kv * 2);
			kv[0] = pb_msg_at(nested, DP_FIELD, 0);
			kv[1] = pb_msg_at(nested, DP_FIELD, 1);
			if (!str_eq(field_name(kv[0]), "key"))
				fail("assertion `left == right` failed\n  left: \"key\"\n right: \"%s\"", field_name(kv[0]));
			if (!str_eq(field_name(kv[1]), "value"))
				fail("assertion `left == right` failed\n  left: \"value\"\n right: \"%s\"", field_name(kv[1]));
			map_put(&map_types, xcat(fq_message_name, ".", pb_str(nested, DP_NAME)), kv);
		} else {
			nested_types[n_nested_types++] = i;
		}
	}

	/* Split the fields into normal fields and oneof fields (MultiMap by oneof index). */
	int n_all = pb_count(message, DP_FIELD);
	Field *fields = xalloc(sizeof *fields * (size_t)(n_all ? n_all : 1));
	int n_fields = 0;
	int n_decl = pb_count(message, DP_ONEOF_DECL);
	OneofField *oneofs = xalloc(sizeof *oneofs * (size_t)(n_decl ? n_decl : 1));
	int n_oneofs = 0;
	for (int i = 0; i < n_all; i++) {
		const PbMsg *f = pb_msg_at(message, DP_FIELD, i);
		if (!pb_int(f, FD_PROTO3_OPTIONAL, 0) && pb_has(f, FD_ONEOF_INDEX))
			continue;
		fields[n_fields++] = (Field){f, i};
	}
	/* The fields of each oneof in field order (fields of an index without a declaration are
	 * dropped). Synthetic oneofs (of proto3 optional fields) have no fields here and are
	 * skipped. */
	int *count = xalloc(sizeof *count * (size_t)(n_decl ? n_decl : 1));
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < n_all; i++) {
			const PbMsg *f = pb_msg_at(message, DP_FIELD, i);
			if (pb_int(f, FD_PROTO3_OPTIONAL, 0) || !pb_has(f, FD_ONEOF_INDEX))
				continue;
			int32_t idx = (int32_t)pb_int(f, FD_ONEOF_INDEX, 0);
			if (idx < 0 || idx >= n_decl)
				continue;
			if (pass == 0)
				count[idx]++;
			else
				oneofs[idx].fields[oneofs[idx].n_fields++] = (Field){f, i};
		}
		for (int idx = 0; pass == 0 && idx < n_decl; idx++) {
			oneofs[idx] = (OneofField){pb_msg_at(message, DP_ONEOF_DECL, idx), NULL, 0, idx};
			if (count[idx])
				oneofs[idx].fields = xalloc(sizeof(Field) * (size_t)count[idx]);
		}
	}
	for (int idx = 0; idx < n_decl; idx++)
		if (oneofs[idx].n_fields)
			oneofs[n_oneofs++] = oneofs[idx];

	append_doc(g, fq_message_name, NULL);
	append_type_attributes(g, fq_message_name);
	append_message_attributes(g, fq_message_name);
	push_indent(g);
	bool copy = can_message_derive_copy(g->ctx, fq_message_name);
	bool eq = can_message_derive_eq(g->ctx, fq_message_name);
	buf_adds(g->buf, xcat("#[derive(Clone, ", copy ? "Copy, " : "", "PartialEq, ", eq ? "Eq, Hash, " : "",
			      prost_path(), "::Message)]\n"));
	append_skip_debug(g, fq_message_name);
	push_indent(g);
	buf_adds(g->buf, xcat("pub struct ", to_upper_camel(message_name), " {\n"));

	g->depth++;
	path_push(&g->path, 2);
	for (int i = 0; i < n_fields; i++) {
		path_push(&g->path, fields[i].path_index);
		const PbMsg *const *kv = pb_has(fields[i].d, FD_TYPE_NAME)
						 ? map_get(&map_types, field_type_name(fields[i].d))
						 : NULL;
		if (kv)
			append_map_field(g, fq_message_name, &fields[i], kv[0], kv[1]);
		else
			append_field(g, fq_message_name, &fields[i]);
		path_pop(&g->path);
	}
	path_pop(&g->path);

	path_push(&g->path, 8);
	for (int i = 0; i < n_oneofs; i++) {
		path_push(&g->path, oneofs[i].path_index);
		append_oneof_field(g, message_name, fq_message_name, &oneofs[i]);
		path_pop(&g->path);
	}
	path_pop(&g->path);

	g->depth--;
	push_indent(g);
	buf_adds(g->buf, "}\n");

	int n_enums = pb_count(message, DP_ENUM_TYPE);
	if (n_enums || n_nested_types || n_oneofs) {
		push_mod(g, message_name);
		path_push(&g->path, 3);
		for (int i = 0; i < n_nested_types; i++) {
			path_push(&g->path, nested_types[i]);
			append_message(g, pb_msg_at(message, DP_NESTED_TYPE, nested_types[i]));
			path_pop(&g->path);
		}
		path_pop(&g->path);

		path_push(&g->path, 4);
		for (int i = 0; i < n_enums; i++) {
			path_push(&g->path, i);
			append_enum(g, pb_msg_at(message, DP_ENUM_TYPE, i));
			path_pop(&g->path);
		}
		path_pop(&g->path);

		for (int i = 0; i < n_oneofs; i++)
			append_oneof(g, fq_message_name, &oneofs[i]);

		pop_mod(g);
	}

	if (g->ctx->config->enable_type_names)
		append_type_name(g, message_name, fq_message_name);
}

/* CodeGenerator::generate: appends the code of file to buf. */
static void code_generator_generate(Context *ctx, const PbMsg *file, Buf *buf)
{
	CodeGen g = {0};
	g.ctx = ctx;
	g.package = pb_str(file, FDP_PACKAGE);
	g.buf = buf;
	const PbMsg *sci = pb_msg(file, FDP_SOURCE_CODE_INFO);
	if (sci) {
		g.has_source_info = true;
		int n = pb_count(sci, SCI_LOCATION);
		g.locs = xalloc(sizeof *g.locs * (size_t)(n ? n : 1));
		for (int i = 0; i < n; i++) {
			const PbMsg *loc = pb_msg_at(sci, SCI_LOCATION, i);
			int len = pb_count(loc, LOC_PATH);
			if (!len || len % 2)
				continue;
			Loc *l = xalloc(sizeof *l);
			l->path = xalloc(sizeof *l->path * (size_t)len);
			for (int j = 0; j < len; j++)
				l->path[j] = (int32_t)pb_int_at(loc, LOC_PATH, j);
			l->n = len;
			l->loc = loc;
			g.locs[g.n_locs++] = l;
		}
		Loc **tmp = xalloc(sizeof *tmp * (size_t)(g.n_locs ? g.n_locs : 1));
		sort_locs(g.locs, tmp, g.n_locs);
	}
	if (!pb_has(file, FDP_SYNTAX) || str_eq(pb_str(file, FDP_SYNTAX), "proto2"))
		g.syntax = SYNTAX_PROTO2;
	else if (str_eq(pb_str(file, FDP_SYNTAX), "proto3"))
		g.syntax = SYNTAX_PROTO3;
	else
		fail("unknown syntax: %s", pb_str(file, FDP_SYNTAX));

	path_push(&g.path, 4);
	for (int i = 0; i < pb_count(file, FDP_MESSAGE_TYPE); i++) {
		path_push(&g.path, i);
		append_message(&g, pb_msg_at(file, FDP_MESSAGE_TYPE, i));
		path_pop(&g.path);
	}
	path_pop(&g.path);

	path_push(&g.path, 5);
	for (int i = 0; i < pb_count(file, FDP_ENUM_TYPE); i++) {
		path_push(&g.path, i);
		append_enum(&g, pb_msg_at(file, FDP_ENUM_TYPE, i));
		path_pop(&g.path);
	}
	path_pop(&g.path);
}
