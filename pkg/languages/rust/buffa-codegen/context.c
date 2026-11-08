/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/context.rs (CodeGenContext: type maps, nested module names,
 * relative type paths, ancillary prefixes, extern resolution), src/features.rs with
 * buffa-descriptor 0.9.2 src/features.rs (feature resolution), src/idents.rs, the naming helpers
 * of src/oneof.rs (to_snake_case, to_pascal_case), src/field_names.rs (idiomatic field names),
 * src/imports.rs (package-root imports), lib.rs effective_extern_paths and the oneof/pointer
 * representation resolution of oneof.rs (resolve_unboxed_variants, resolve_inlined_fields). */

/* ---- features ---- */

static const Feat FEAT_PROTO2 = {PROTO_PRESENCE_EXPLICIT, PROTO_ENUM_TYPE_CLOSED, PROTO_REPEATED_EXPANDED,
				 PROTO_UTF8_NONE, PROTO_MESSAGE_LENGTH_PREFIXED, PROTO_JSON_LEGACY_BEST_EFFORT};
static const Feat FEAT_PROTO3 = {PROTO_PRESENCE_IMPLICIT, PROTO_ENUM_TYPE_OPEN, PROTO_REPEATED_PACKED,
				 PROTO_UTF8_VERIFY, PROTO_MESSAGE_LENGTH_PREFIXED, PROTO_JSON_ALLOW};
static const Feat FEAT_2023 = {PROTO_PRESENCE_EXPLICIT, PROTO_ENUM_TYPE_OPEN, PROTO_REPEATED_PACKED,
			       PROTO_UTF8_VERIFY, PROTO_MESSAGE_LENGTH_PREFIXED, PROTO_JSON_ALLOW};

/* merge(): a set feature with a known value overrides the parent's */
static Feat feat_child(Feat p, const PbMsg *fs)
{
	if (!fs)
		return p;
	int v;
	if ((v = (int)pb_int(fs, PROTO_FEATURE_FIELD_PRESENCE, 0)) >= 1 && v <= 3)
		p.presence = (uint8_t)v;
	if ((v = (int)pb_int(fs, PROTO_FEATURE_ENUM_TYPE, 0)) >= 1 && v <= 2)
		p.enum_type = (uint8_t)v;
	if ((v = (int)pb_int(fs, PROTO_FEATURE_REPEATED_FIELD_ENCODING, 0)) >= 1 && v <= 2)
		p.rep = (uint8_t)v;
	if ((v = (int)pb_int(fs, PROTO_FEATURE_UTF8_VALIDATION, 0)) == 2 || v == 3)
		p.utf8 = (uint8_t)v;
	if ((v = (int)pb_int(fs, PROTO_FEATURE_MESSAGE_ENCODING, 0)) >= 1 && v <= 2)
		p.msg_enc = (uint8_t)v;
	if ((v = (int)pb_int(fs, PROTO_FEATURE_JSON_FORMAT, 0)) >= 1 && v <= 2)
		p.json = (uint8_t)v;
	return p;
}

static Feat feat_for_file(const PbMsg *file)
{
	const char *syntax = pb_has(file, FDP_SYNTAX) ? pb_str(file, FDP_SYNTAX) : "";
	Feat base = FEAT_PROTO2;
	if (str_eq(syntax, "proto3"))
		base = FEAT_PROTO3;
	else if (str_eq(syntax, "editions")) {
		int ed = (int)pb_int(file, FDP_EDITION, 0);
		base = ed == PROTO_EDITION_PROTO2 || ed == PROTO_EDITION_LEGACY ? FEAT_PROTO2
		       : ed == PROTO_EDITION_PROTO3			 ? FEAT_PROTO3
									 : FEAT_2023;
	}
	return feat_child(base, pb_msg(pb_msg(file, FDP_OPTIONS), FOPT_FEATURES));
}

/* features::resolve_field: the referenced enum's closedness overrides the field's chain unless
 * the field itself sets enum_type OPEN. */
static Feat feat_resolve_field(const PbMsg *field, Feat parent)
{
	Feat r = feat_child(parent, field_features(field));
	bool opens = pb_int(field_features(field), PROTO_FEATURE_ENUM_TYPE, 0) == PROTO_ENUM_TYPE_OPEN;
	if (fd_type(field) == PT_ENUM && !opens && fd_type_name(field)) {
		int closed = ctx_is_enum_closed(fd_type_name(field));
		if (closed >= 0)
			r.enum_type = closed ? PROTO_ENUM_TYPE_CLOSED : PROTO_ENUM_TYPE_OPEN;
	}
	return r;
}

/* ---- idents.rs ---- */

static bool is_rust_keyword(const char *s)
{
	static const char *const kw[] = {
		"as", "break", "const", "continue", "crate", "else", "enum", "extern", "false", "fn", "for", "if",
		"impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self", "Self",
		"static", "struct", "super", "trait", "true", "type", "unsafe", "use", "where", "while", "async",
		"await", "dyn", "gen", "abstract", "become", "box", "do", "final", "macro", "override", "priv",
		"try", "typeof", "unsized", "virtual", "yield", NULL};
	for (int i = 0; kw[i]; i++)
		if (str_eq(s, kw[i]))
			return true;
	return false;
}

static bool can_be_raw_ident(const char *s)
{
	return !str_eq(s, "self") && !str_eq(s, "super") && !str_eq(s, "Self") && !str_eq(s, "crate");
}

static char *make_field_ident(const char *name)
{
	if (is_rust_keyword(name))
		return can_be_raw_ident(name) ? xcat("r#", name) : xcat(name, "_");
	return (char *)name;
}

static char *escape_mod_ident(const char *name) { return make_field_ident(name); }

static char *rust_path(const char *path)
{
	Buf b = {0};
	const char *p = path;
	if (p[0] == ':' && p[1] == ':') {
		buf_adds(&b, "::");
		p += 2;
	}
	for (bool first = true;; first = false) {
		const char *e = strstr(p, "::");
		char *seg = e ? xstrndup(p, (size_t)(e - p)) : (char *)p;
		if (!first)
			buf_adds(&b, "::");
		buf_adds(&b, is_rust_keyword(seg) && can_be_raw_ident(seg) ? xcat("r#", seg) : seg);
		if (!e)
			break;
		p = e + 2;
	}
	return (char *)buf_str(&b);
}

static bool is_up(char c) { return c >= 'A' && c <= 'Z'; }
static bool is_lo(char c) { return c >= 'a' && c <= 'z'; }
static char lower(char c) { return is_up(c) ? (char)(c + 32) : c; }
static char upper(char c) { return is_lo(c) ? (char)(c - 32) : c; }

static char *upper_ascii(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		*p = upper(*p);
	return r;
}

/* oneof::to_snake_case (module names) */
static char *to_snake_case(const char *s)
{
	Buf b = {0};
	for (size_t i = 0; s[i]; i++) {
		char c = s[i];
		if (is_up(c) && i > 0) {
			char prev = s[i - 1];
			if (is_lo(prev) || (is_up(prev) && is_lo(s[i + 1])))
				buf_addc(&b, '_');
		}
		buf_addc(&b, lower(c));
	}
	return (char *)buf_str(&b);
}

/* oneof::to_pascal_case */
static char *to_pascal_case(const char *s)
{
	Buf b = {0};
	bool cap = true;
	for (const char *p = s; *p; p++) {
		if (*p == '_')
			cap = true;
		else if (cap) {
			buf_addc(&b, upper(*p));
			cap = false;
		} else
			buf_addc(&b, *p);
	}
	return (char *)buf_str(&b);
}

static char *to_upper_camel_case(const char *s)
{
	Buf b = {0};
	bool start = true;
	for (size_t i = 0; s[i]; i++) {
		char ch = s[i];
		if (ch == '_') {
			start = true;
			continue;
		}
		if (!start && i > 0) {
			char prev = s[i - 1];
			if ((is_lo(prev) && is_up(ch)) || (is_up(prev) && is_up(ch) && is_lo(s[i + 1])))
				start = true;
		}
		buf_addc(&b, start ? upper(ch) : lower(ch));
		start = false;
	}
	return (char *)buf_str(&b);
}

static char *to_shouty_snake_case(const char *s)
{
	Buf b = {0};
	for (size_t i = 0; s[i]; i++) {
		char ch = s[i];
		if (ch == '_') {
			buf_addc(&b, '_');
			continue;
		}
		if (i > 0 && is_up(ch) && s[i - 1] != '_') {
			char prev = s[i - 1];
			if (is_lo(prev) || (prev >= '0' && prev <= '9') || (is_up(prev) && is_lo(s[i + 1])))
				buf_addc(&b, '_');
		}
		buf_addc(&b, upper(ch));
	}
	return (char *)buf_str(&b);
}

/* field_names::idiomatic_snake_case */
static char *idiomatic_snake_case(const char *s)
{
	Buf b = {0};
	char last_cased = 0;
	for (size_t i = 0; s[i]; i++) {
		char c = s[i];
		if (c == '_') {
			last_cased = 0;
			buf_addc(&b, '_');
			continue;
		}
		if (is_up(c) && i > 0 && last_cased && (is_lo(last_cased) || (is_up(last_cased) && is_lo(s[i + 1]))))
			buf_addc(&b, '_');
		buf_addc(&b, lower(c));
		if (is_lo(c) || is_up(c))
			last_cased = c;
	}
	return (char *)buf_str(&b);
}

/* ---- context.rs ---- */

static const char *prefixed_type_name(const char *proto_name)
{
	return *CFG->type_name_prefix ? xcat(CFG->type_name_prefix, proto_name) : proto_name;
}

static bool matches_proto_prefix(const char *prefix, const char *fqn_dotted)
{
	size_t n = strlen(prefix);
	return str_eq(prefix, ".") || str_eq(prefix, fqn_dotted) || (!strncmp(fqn_dotted, prefix, n) && fqn_dotted[n] == '.');
}

static const char *join_mod(const char *module, const char *name) { return *module ? xcat(module, "::", name) : name; }

/* resolve_extern_prefix */
static char *resolve_extern_prefix(const char *package, const Vec *paths)
{
	char *dotted = xcat(".", package);
	const BcPair *best = NULL;
	for (int i = 0; i < paths->n; i++) {
		const BcPair *p = paths->v[i];
		if (str_eq(dotted, p->key))
			return (char *)p->value;
		size_t n = strlen(p->key);
		if (!strncmp(dotted, p->key, n) && (str_eq(p->key, ".") || dotted[n] == '.'))
			if (!best || n > strlen(best->key))
				best = p;
	}
	if (!best)
		return NULL;
	const char *rest = dotted + strlen(best->key);
	if (*rest == '.')
		rest++;
	Vec segs = str_split(rest, '.');
	Buf b = {0};
	buf_adds(&b, best->value);
	buf_adds(&b, "::");
	for (int i = 0; i < segs.n; i++) {
		if (i)
			buf_adds(&b, "::");
		buf_adds(&b, to_snake_case(segs.v[i]));
	}
	return (char *)buf_str(&b);
}

/* resolve_extern_type */
static char *resolve_extern_type(const char *fqn, const Vec *paths)
{
	for (int i = 0; i < paths->n; i++)
		if (str_eq(((BcPair *)paths->v[i])->key, fqn))
			return (char *)((BcPair *)paths->v[i])->value;
	const BcPair *best = NULL;
	for (int i = 0; i < paths->n; i++) {
		const BcPair *p = paths->v[i];
		size_t n = strlen(p->key);
		bool m = str_eq(p->key, ".") || (!strncmp(fqn, p->key, n) && fqn[n] == '.');
		if (m && (!best || n > strlen(best->key)))
			best = p;
	}
	if (!best)
		return NULL;
	const char *rest;
	if (str_eq(best->key, "."))
		rest = fqn[0] == '.' ? fqn + 1 : fqn;
	else
		rest = fqn[strlen(best->key)] == '.' ? fqn + strlen(best->key) + 1 : "";
	Vec segs = str_split(rest, '.');
	Buf b = {0};
	buf_adds(&b, best->value);
	for (int i = 0; i + 1 < segs.n; i++) {
		buf_adds(&b, "::");
		buf_adds(&b, to_snake_case(segs.v[i]));
	}
	buf_adds(&b, "::");
	buf_adds(&b, segs.v[segs.n - 1]);
	return (char *)buf_str(&b);
}

static const char *resolve_type_path(const char *fqn, const char *name, const char *file_root, const char *local_module,
				     const Vec *paths, bool *is_extern)
{
	*is_extern = true;
	for (int i = 0; i < paths->n; i++)
		if (str_eq(((BcPair *)paths->v[i])->key, fqn))
			return ((BcPair *)paths->v[i])->value;
	if (file_root)
		return join_mod(file_root, name);
	char *p = resolve_extern_type(fqn, paths);
	if (p)
		return p;
	*is_extern = false;
	return join_mod(local_module, xcat(CFG->type_name_prefix, name));
}

static const char *exact_extern(const char *fqn, const Vec *paths)
{
	for (int i = 0; i < paths->n; i++)
		if (str_eq(((BcPair *)paths->v[i])->key, fqn))
			return ((BcPair *)paths->v[i])->value;
	return NULL;
}

static void register_nested_types(const char *package, const Vec *paths, const char *parent_fqn, const char *parent_mod,
				  const PbMsg *msg)
{
	int n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *nested = pb_msg_at(msg, DP_NESTED_TYPE, i);
		if (!pb_has(nested, DP_NAME))
			continue;
		const char *name = dname(nested);
		char *fqn = xcat(parent_fqn, ".", name);
		const char *rust = exact_extern(fqn, paths), *child;
		if (rust) {
			const char *cut = NULL;
			for (const char *p = strstr(rust, "::"); p; p = strstr(p + 2, "::"))
				cut = p;
			child = cut ? xcat(xstrndup(rust, (size_t)(cut - rust)), "::", to_snake_case(name)) : to_snake_case(name);
		} else {
			rust = xcat(parent_mod, "::", CFG->type_name_prefix, name);
			child = xcat(parent_mod, "::", to_snake_case(name));
		}
		map_put(&cx.type_map, fqn, (void *)rust);
		map_put(&cx.package_of, fqn, (void *)package);
		register_nested_types(package, paths, fqn, child, nested);
	}
	n = pb_count(msg, DP_ENUM_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *e = pb_msg_at(msg, DP_ENUM_TYPE, i);
		if (!pb_has(e, EDP_NAME))
			continue;
		char *fqn = xcat(parent_fqn, ".", dname(e));
		const char *rust = exact_extern(fqn, paths);
		if (!rust)
			rust = xcat(parent_mod, "::", CFG->type_name_prefix, dname(e));
		map_put(&cx.type_map, fqn, (void *)rust);
		map_put(&cx.package_of, fqn, (void *)package);
	}
}

static void register_enum_closedness(const char *fqn, Feat parent, const PbMsg *e)
{
	Feat r = feat_child(parent, enum_features(e));
	map_put(&cx.enum_closed, fqn, (void *)(uintptr_t)(r.enum_type == PROTO_ENUM_TYPE_CLOSED ? 2 : 1));
}

static void register_nested_enum_closedness(const char *parent_fqn, Feat parent, const PbMsg *msg)
{
	Feat mf = feat_child(parent, msg_features(msg));
	int n = pb_count(msg, DP_ENUM_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *e = pb_msg_at(msg, DP_ENUM_TYPE, i);
		if (pb_has(e, EDP_NAME))
			register_enum_closedness(xcat(parent_fqn, ".", dname(e)), mf, e);
	}
	n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *nested = pb_msg_at(msg, DP_NESTED_TYPE, i);
		if (pb_has(nested, DP_NAME))
			register_nested_enum_closedness(xcat(parent_fqn, ".", dname(nested)), mf, nested);
	}
}

static void collect_enum_first(const char *prefix, const PbMsg *parent, int field)
{
	int n = pb_count(parent, field);
	for (int i = 0; i < n; i++) {
		const PbMsg *e = pb_msg_at(parent, field, i);
		if (pb_has(e, EDP_NAME) && pb_count(e, EDP_VALUE)) {
			int *v = xalloc(sizeof *v);
			*v = (int)pb_int(pb_msg_at(e, EDP_VALUE, 0), EVDP_NUMBER, 0);
			map_put(&cx.enum_first, xcat(prefix, dname(e)), v);
		}
	}
}

static void walk_enum_first(const char *prefix, const PbMsg *msg)
{
	if (!pb_has(msg, DP_NAME))
		return;
	char *child = xcat(prefix, dname(msg), ".");
	collect_enum_first(child, msg, DP_ENUM_TYPE);
	int n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++)
		walk_enum_first(child, pb_msg_at(msg, DP_NESTED_TYPE, i));
}

/* child_package_segments + deconflict_package_modules for one package */
static int str_cmp_qsort(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

typedef struct {
	const char *base;
	int idx;
} BaseIdx;

static int base_idx_cmp(const void *a, const void *b)
{
	int c = strcmp(((const BaseIdx *)a)->base, ((const BaseIdx *)b)->base);
	return c ? c : ((const BaseIdx *)a)->idx - ((const BaseIdx *)b)->idx;
}

static void deconflict_package(const char *package, const Vec *names, const Map *all_packages, const Vec *pkg_list)
{
	Map children = {0}, taken = {0};
	char *prefix = *package ? xcat(package, ".") : "";
	for (int i = 0; i < pkg_list->n; i++) {
		const char *p = pkg_list->v[i];
		const char *rest = *package ? (has_prefix(p, prefix) ? p + strlen(prefix) : NULL) : p;
		if (!rest || !*rest)
			continue;
		const char *dot = strchr(rest, '.');
		char *seg = dot ? xstrndup(rest, (size_t)(dot - rest)) : (char *)rest;
		map_put(&children, seg, (void *)1);
		map_put(&taken, seg, (void *)1);
	}
	(void)all_packages;
	map_put(&taken, SENTINEL_MOD, (void *)1);
	BaseIdx *order = xalloc(sizeof *order * (size_t)(names->n + 1));
	for (int i = 0; i < names->n; i++) {
		order[i].base = to_snake_case(names->v[i]);
		order[i].idx = i;
		map_put(&taken, order[i].base, (void *)1);
	}
	const char **out = xalloc(sizeof *out * (size_t)(names->n + 1));
	for (int i = 0; i < names->n; i++)
		out[i] = order[i].base;
	if (names->n > 1)
		qsort(order, (size_t)names->n, sizeof *order, base_idx_cmp);
	for (int k = 0; k < names->n; k++) {
		if (!map_has(&children, order[k].base))
			continue;
		char *cand = xcat(order[k].base, "_");
		while (map_has(&taken, cand))
			cand = xcat(cand, "_");
		map_put(&taken, cand, (void *)1);
		out[order[k].idx] = cand;
	}
	for (int i = 0; i < names->n; i++) {
		const char *name = names->v[i];
		map_put(&cx.nested_mods, *package ? xcat(".", package, ".", name) : xcat(".", name), (void *)out[i]);
	}
}

/* lib.rs effective_extern_paths */
static Vec effective_extern_paths(const PbMsg *req, const Vec *to_generate)
{
	Vec paths = {0};
	vcat(&paths, &CFG->extern_paths);
	bool has_wkt = false;
	for (int i = 0; i < paths.n; i++)
		has_wkt |= str_eq(((BcPair *)paths.v[i])->key, ".google.protobuf");
	if (!has_wkt) {
		bool generating = false;
		for (int i = 0; i < to_generate->n && !generating; i++)
			for (int j = 0; j < cx.n_files; j++)
				if (str_eq(pb_str(cx.files[j], FDP_NAME), to_generate->v[i]) && pb_has(cx.files[j], FDP_NAME) &&
				    pb_has(cx.files[j], FDP_PACKAGE) && str_eq(pb_str(cx.files[j], FDP_PACKAGE), "google.protobuf"))
					generating = true;
		if (!generating) {
			BcPair *p = xalloc(sizeof *p);
			p->key = ".google.protobuf";
			p->value = "::buffa_types::google::protobuf";
			vec_push(&paths, p);
		}
	}
	(void)req;
	return paths;
}

/* lib.rs effective_file_extern_paths */
static Vec effective_file_extern_paths(const Vec *to_generate)
{
	static const char *const files[][3] = {
		{"google/protobuf/descriptor.proto", "google.protobuf", "::buffa_descriptor::generated::descriptor"},
		{"google/protobuf/compiler/plugin.proto", "google.protobuf.compiler", "::buffa_descriptor::generated::compiler"},
	};
	Vec out = {0};
	for (int k = 0; k < 2; k++) {
		if (resolve_extern_prefix(files[k][1], &CFG->extern_paths))
			continue;
		bool gen = false;
		for (int i = 0; i < to_generate->n; i++)
			gen |= str_eq(to_generate->v[i], files[k][0]);
		if (gen)
			continue;
		BcPair *p = xalloc(sizeof *p);
		p->key = files[k][0];
		p->value = files[k][2];
		vec_push(&out, p);
	}
	return out;
}

/* ---- oneof.rs: unboxed variants and inline fields ---- */

static Map msg_index; /* "pkg.Msg" -> PbMsg * */

static void index_messages(const char *prefix, const PbMsg *msg, Vec *order)
{
	if (!pb_has(msg, DP_NAME))
		return;
	char *fqn = *prefix ? xcat(prefix, ".", dname(msg)) : (char *)dname(msg);
	int n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++)
		index_messages(fqn, pb_msg_at(msg, DP_NESTED_TYPE, i), order);
	if (!map_has(&msg_index, fqn))
		vec_push(order, fqn);
	map_put(&msg_index, fqn, (void *)msg);
}

static bool is_real_oneof_member(const PbMsg *f) { return fd_has_oneof(f) && !fd_proto3_optional(f); }
static bool is_boxed_variant(int ty) { return ty == PT_MESSAGE || ty == PT_GROUP; }

static bool rule_matches(const Vec *rules, const char *fqn)
{
	for (int i = 0; i < rules->n; i++)
		if (matches_proto_prefix(rules->v[i], fqn))
			return true;
	return false;
}

/* calls f(ctx, variant_fqn, target) for message variants (oneof) or singular message fields */
typedef void (*EdgeFn)(void *ctx, const char *path, const char *target);

static void for_each_message_variant(const PbMsg *msg, const char *fqn, EdgeFn f, void *ctx)
{
	int n = n_fields(msg);
	for (int i = 0; i < n; i++) {
		const PbMsg *fd = field_at(msg, i);
		if (!is_real_oneof_member(fd) || !is_boxed_variant(fd_type(fd)) || !pb_has(fd, FD_NAME) || !fd_type_name(fd))
			continue;
		int idx = fd_oneof(fd);
		if (idx < 0 || idx >= pb_count(msg, DP_ONEOF_DECL) || !pb_has(pb_msg_at(msg, DP_ONEOF_DECL, idx), ODP_NAME))
			continue;
		const char *t = fd_type_name(fd);
		while (*t == '.')
			t++;
		f(ctx, xcat(".", fqn, ".", dname(pb_msg_at(msg, DP_ONEOF_DECL, idx)), ".", dname(fd)), t);
	}
}

static void for_each_singular_message_field(const PbMsg *msg, const char *fqn, EdgeFn f, void *ctx)
{
	int n = n_fields(msg);
	for (int i = 0; i < n; i++) {
		const PbMsg *fd = field_at(msg, i);
		if (is_real_oneof_member(fd) || !is_boxed_variant(fd_type(fd)) || fd_label(fd) == LABEL_REPEATED ||
		    !pb_has(fd, FD_NAME) || !fd_type_name(fd))
			continue;
		const char *t = fd_type_name(fd);
		while (*t == '.')
			t++;
		f(ctx, xcat(".", fqn, ".", dname(fd)), t);
	}
}

typedef struct {
	Vec stack;
	const Vec *rules;
} Walk;

/* inline_is_recursive(enclosing, target): whether `target` reaches `enclosing` through inline edges
 * (rule-matched message variants and singular message fields, whose raw pointer repr is always the
 * default Inline). Every caller asks it for an edge enclosing -> target of that same graph, so the
 * answer is "same strongly connected component"; the components are computed once (Tarjan,
 * iterative) instead of one walk per field, which keeps time and memory linear. */
typedef struct {
	Vec targets; /* const char * */
} Edges;

static Map scc_of; /* message fqn -> (void *)(component + 1) */

static void collect_edge(void *ctx, const char *path, const char *target)
{
	Walk *w = ctx;
	if (!w->rules || rule_matches(w->rules, path))
		vec_push(&w->stack, (void *)target);
}

static void compute_sccs(const Vec *order)
{
	int n = order->n;
	Map index_of = {0};
	for (int i = 0; i < n; i++)
		map_put(&index_of, order->v[i], (void *)(uintptr_t)(i + 1));
	int **adj = xalloc(sizeof *adj * (size_t)(n + 1));
	int *deg = xalloc(sizeof *deg * (size_t)(n + 1));
	for (int i = 0; i < n; i++) {
		const PbMsg *m = map_get(&msg_index, order->v[i]);
		Walk w = {{0}, &CFG->unboxed_oneof_fields};
		for_each_message_variant(m, order->v[i], collect_edge, &w);
		Walk w2 = {{0}, NULL};
		for_each_singular_message_field(m, order->v[i], collect_edge, &w2);
		vcat(&w.stack, &w2.stack);
		adj[i] = xalloc(sizeof *adj[i] * (size_t)(w.stack.n + 1));
		for (int k = 0; k < w.stack.n; k++) {
			uintptr_t t = (uintptr_t)map_get(&index_of, w.stack.v[k]);
			if (t)
				adj[i][deg[i]++] = (int)t - 1;
		}
	}
	int *idx = xalloc(sizeof *idx * (size_t)(n + 1)), *low = xalloc(sizeof *low * (size_t)(n + 1));
	int *stack = xalloc(sizeof *stack * (size_t)(n + 1)), *call = xalloc(sizeof *call * (size_t)(n + 1));
	int *edge = xalloc(sizeof *edge * (size_t)(n + 1));
	bool *on = xalloc((size_t)n + 1);
	int counter = 0, sp = 0, comp = 0;
	for (int i = 0; i < n; i++)
		idx[i] = -1;
	for (int root = 0; root < n; root++) {
		if (idx[root] >= 0)
			continue;
		int cp = 0;
		call[cp++] = root;
		idx[root] = low[root] = counter++;
		stack[sp++] = root;
		on[root] = true;
		edge[root] = 0;
		while (cp) {
			int v = call[cp - 1];
			if (edge[v] < deg[v]) {
				int w = adj[v][edge[v]++];
				if (idx[w] < 0) {
					idx[w] = low[w] = counter++;
					stack[sp++] = w;
					on[w] = true;
					edge[w] = 0;
					call[cp++] = w;
				} else if (on[w] && idx[w] < low[v])
					low[v] = idx[w];
				continue;
			}
			if (low[v] == idx[v]) {
				int w;
				do {
					w = stack[--sp];
					on[w] = false;
					map_put(&scc_of, order->v[w], (void *)(uintptr_t)(comp + 1));
				} while (w != v);
				comp++;
			}
			cp--;
			if (cp && low[v] < low[call[cp - 1]])
				low[call[cp - 1]] = low[v];
		}
	}
}

static bool inline_is_recursive(const Vec *rules, const char *enclosing, const char *target)
{
	(void)rules;
	uintptr_t a = (uintptr_t)map_get(&scc_of, enclosing), b = (uintptr_t)map_get(&scc_of, target);
	return a && a == b;
}

typedef struct {
	const char *msg_fqn;
	Map *out;
	bool variants;
} Resolve;

static void resolve_edge(void *ctx, const char *path, const char *target)
{
	Resolve *r = ctx;
	if (r->variants && !rule_matches(&CFG->unboxed_oneof_fields, path))
		return;
	if (!inline_is_recursive(&CFG->unboxed_oneof_fields, r->msg_fqn, target))
		map_put(r->out, path, (void *)1);
}

static Map inlined_fields;

static void resolve_reprs(void)
{
	Vec order = {0};
	for (int i = 0; i < cx.n_files; i++) {
		const PbMsg *file = cx.files[i];
		int n = pb_count(file, FDP_MESSAGE_TYPE);
		for (int j = 0; j < n; j++)
			index_messages(pb_str(file, FDP_PACKAGE), pb_msg_at(file, FDP_MESSAGE_TYPE, j), &order);
	}
	memset(&scc_of, 0, sizeof scc_of);
	compute_sccs(&order);
	for (int i = 0; i < order.n; i++) {
		const PbMsg *m = map_get(&msg_index, order.v[i]);
		Resolve r = {order.v[i], &cx.unboxed, true};
		if (CFG->unboxed_oneof_fields.n)
			for_each_message_variant(m, order.v[i], resolve_edge, &r);
		r.out = &inlined_fields;
		r.variants = false;
		for_each_singular_message_field(m, order.v[i], resolve_edge, &r);
	}
}

static bool ctx_oneof_unboxed(const char *variant_fqn) { return map_has(&cx.unboxed, variant_fqn); }
/* context::pointer_repr: Inline unless recursive (then Box) */
static bool ctx_field_inline(const char *field_fqn) { return map_has(&inlined_fields, field_fqn); }

/* ---- field_names.rs: plan_field_names ---- */

static char *rename_key(const char *name, int number) { return xfmt("%s\x01%d", name, number); }

typedef struct {
	const char *proto_name, *converted;
	bool is_field;
	int number;
} Member;

static void plan_message(const PbMsg *msg)
{
	int nf = n_fields(msg), no = pb_count(msg, DP_ONEOF_DECL);
	Member *mem = xalloc(sizeof *mem * (size_t)(nf + no + 1));
	int n = 0;
	bool *real = xalloc((size_t)no + 1);
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (!pb_has(f, FD_NAME))
			continue;
		if (is_real_oneof_member(f)) {
			if (fd_oneof(f) >= 0 && fd_oneof(f) < no)
				real[fd_oneof(f)] = true;
			continue;
		}
		mem[n++] = (Member){dname(f), idiomatic_snake_case(dname(f)), true, fd_number(f)};
	}
	for (int i = 0; i < no; i++) {
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, i);
		if (pb_has(o, ODP_NAME) && real[i])
			mem[n++] = (Member){dname(o), idiomatic_snake_case(dname(o)), false, 0};
	}
	Map count = {0};
	for (int i = 0; i < n; i++)
		map_put(&count, mem[i].converted, (void *)((uintptr_t)map_get(&count, mem[i].converted) + 1));
	bool any = false;
	for (int i = 0; i < n; i++)
		any |= (uintptr_t)map_get(&count, mem[i].converted) > 1;
	if (!any)
		return;
	const char **finals = xalloc(sizeof *finals * (size_t)(n + 1));
	bool *adjusted = xalloc((size_t)n + 1);
	for (int i = 0; i < n; i++) {
		finals[i] = mem[i].converted;
		if ((uintptr_t)map_get(&count, mem[i].converted) > 1 && !str_eq(mem[i].converted, mem[i].proto_name)) {
			finals[i] = mem[i].is_field ? xfmt("%s_f%d", mem[i].converted, mem[i].number) : mem[i].proto_name;
			adjusted[i] = true;
		}
	}
	Map uniq = {0};
	bool dup = false;
	for (int i = 0; i < n; i++) {
		dup |= map_has(&uniq, finals[i]);
		map_put(&uniq, finals[i], (void *)1);
	}
	if (dup)
		for (int i = 0; i < n; i++)
			if (!str_eq(mem[i].converted, mem[i].proto_name)) {
				finals[i] = mem[i].proto_name;
				adjusted[i] = true;
			}
	for (int i = 0; i < n; i++) {
		if (!adjusted[i])
			continue;
		if (mem[i].is_field) {
			char *key = rename_key(mem[i].proto_name, mem[i].number);
			const char *existing = map_get(&cx.field_renames, key);
			int rnew = str_eq(finals[i], mem[i].proto_name) ? 2 : 1;
			if (!existing || rnew > (str_eq(existing, mem[i].proto_name) ? 2 : 1))
				map_put(&cx.field_renames, key, (void *)finals[i]);
		} else
			map_put(&cx.oneof_keep, mem[i].proto_name, (void *)1);
	}
}

static void plan_recurse(const PbMsg *msg)
{
	if (msg_is_map_entry(msg))
		return;
	int n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++)
		plan_recurse(pb_msg_at(msg, DP_NESTED_TYPE, i));
	plan_message(msg);
}

static const char *ctx_field_rust_name(const char *name, int number)
{
	if (!CFG->idiomatic_field_names)
		return name;
	if (cx.field_renames.n) {
		const char *r = map_get(&cx.field_renames, rename_key(name, number));
		if (r)
			return r;
	}
	return idiomatic_snake_case(name);
}

static char *ctx_field_ident(const char *name, int number) { return make_field_ident(ctx_field_rust_name(name, number)); }

static const char *ctx_oneof_rust_name(const char *name)
{
	if (!CFG->idiomatic_field_names || map_has(&cx.oneof_keep, name))
		return name;
	return idiomatic_snake_case(name);
}

static char *ctx_oneof_ident(const char *name) { return make_field_ident(ctx_oneof_rust_name(name)); }

static const char *ctx_field_rename_note(const char *name, int number)
{
	if (!CFG->idiomatic_field_names || !cx.field_renames.n)
		return NULL;
	const char *r = map_get(&cx.field_renames, rename_key(name, number));
	if (!r)
		return NULL;
	return xcat(" Note: the snake_case conversion of `", name,
		    "` collides with another member of this message; the Rust name was adjusted to `", r,
		    "` (`_f<n>` suffixes carry the field number).");
}

static bool has_upper(const char *s)
{
	for (; *s; s++)
		if (is_up(*s))
			return true;
	return false;
}

static void ctx_message_non_snake_attr(const PbMsg *msg, Vec *attrs)
{
	int nf = n_fields(msg), no = pb_count(msg, DP_ONEOF_DECL);
	bool *real = xalloc((size_t)no + 1), found = false;
	for (int i = 0; i < nf && !found; i++) {
		const PbMsg *f = field_at(msg, i);
		if (!pb_has(f, FD_NAME))
			continue;
		if (fd_has_oneof(f) && !fd_proto3_optional(f)) {
			if (fd_oneof(f) >= 0 && fd_oneof(f) < no)
				real[fd_oneof(f)] = true;
			continue;
		}
		found = has_upper(ctx_field_rust_name(dname(f), fd_number(f)));
	}
	for (int i = 0; i < no && !found; i++) {
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, i);
		if (pb_has(o, ODP_NAME) && real[i])
			found = has_upper(ctx_oneof_rust_name(dname(o)));
	}
	if (found)
		vec_push(attrs, attr_src("allow", "non_snake_case"));
}

/* ---- imports.rs: package-root imports ---- */

static bool shortenable(const char *p) { return has_prefix(p, "::") || has_prefix(p, "crate::") || has_prefix(p, "super::"); }

static const char *ctx_root_path(const char *path, int nesting)
{
	if (nesting)
		return path;
	if (cx.imports_phase == IMPORTS_COLLECTING) {
		if (shortenable(path) && !map_has(&cx.imports_collected, path)) {
			map_put(&cx.imports_collected, path, (void *)1);
			vec_push(&cx.imports_collected_list, (void *)path);
		}
	} else if (cx.imports_phase == IMPORTS_RESOLVING) {
		const char *s = map_get(&cx.imports_resolved, path);
		if (s)
			return s;
	}
	return path;
}

static const char *const runtime_imports[][2] = {
	{ENUM_VALUE_PATH, "EnumValue"}, {HASHMAP_PATH, "HashMap"}, {MESSAGE_FIELD_PATH, "MessageField"},
	{OPTION_PATH, "Option"},	{STRING_PATH, "String"},   {VEC_PATH, "Vec"},
};

static bool root_bare_referenced(const char *name)
{
	static const char *const names[] = {"Box", "Clone", "Copy", "Debug", "Default", "Eq", "From", "Hash", "Into",
					    "None", "PartialEq", "Result", "Self", "Send", "Some", "Sync", "bool",
					    "f32", "f64", "i32", "i64", "serde", "str", "u8", "u32", "u64", NULL};
	for (int i = 0; names[i]; i++)
		if (str_eq(name, names[i]))
			return true;
	return false;
}

static Map import_bindings; /* short -> path */

static int pair_cmp(const void *a, const void *b) { return strcmp((*(char **const *)a)[0], (*(char **const *)b)[0]); }

static void bind(const char *s, const char *path)
{
	map_put(&import_bindings, s, (void *)path);
	char **pair = xalloc(2 * sizeof *pair);
	pair[0] = (char *)s;
	pair[1] = (char *)path;
	vec_push(&cx.imports_bindings, pair);
}

static bool proto_claimable(const char *name, const Map *occupied)
{
	for (int i = 0; i < 6; i++)
		if (str_eq(name, runtime_imports[i][1]))
			return false;
	return !map_has(occupied, name) && !root_bare_referenced(name) && can_be_raw_ident(name) && *name;
}

/* RootImports::assign over the collected paths (sorted) */
static void imports_assign(const Map *occupied)
{
	memset(&import_bindings, 0, sizeof import_bindings);
	memset(&cx.imports_resolved, 0, sizeof cx.imports_resolved);
	memset(&cx.imports_bindings, 0, sizeof cx.imports_bindings);
	for (int i = 0; i < 6; i++)
		if (map_has(&cx.imports_collected, runtime_imports[i][0]) && !map_has(occupied, runtime_imports[i][1])) {
			bind(runtime_imports[i][1], runtime_imports[i][0]);
			map_put(&cx.imports_resolved, runtime_imports[i][0], (void *)runtime_imports[i][1]);
		}
	Vec sorted = cx.imports_collected_list;
	if (sorted.n > 1)
		qsort(sorted.v, (size_t)sorted.n, sizeof *sorted.v, str_cmp_qsort);
	for (int k = 0; k < sorted.n; k++) {
		const char *path = sorted.v[k];
		bool runtime = false;
		for (int i = 0; i < 6; i++)
			runtime |= str_eq(path, runtime_imports[i][0]);
		if (map_has(&cx.imports_resolved, path) || runtime || !shortenable(path))
			continue;
		const char *cut = NULL;
		for (const char *p = strstr(path, "::"); p; p = strstr(p + 2, "::"))
			cut = p;
		if (!cut)
			continue;
		char *parent = xstrndup(path, (size_t)(cut - path));
		const char *leaf = cut + 2;
		if (proto_claimable(leaf, occupied)) {
			const char *ex = map_get(&import_bindings, leaf);
			if (!ex) {
				bind(leaf, path);
				map_put(&cx.imports_resolved, path, (void *)leaf);
				continue;
			}
			if (str_eq(ex, path)) {
				map_put(&cx.imports_resolved, path, (void *)leaf);
				continue;
			}
		}
		const char *pcut = NULL;
		for (const char *p = strstr(parent, "::"); p; p = strstr(p + 2, "::"))
			pcut = p;
		const char *parent_leaf = pcut ? pcut + 2 : parent;
		if (proto_claimable(parent_leaf, occupied)) {
			const char *ex = map_get(&import_bindings, parent_leaf);
			bool claimed = !ex || str_eq(ex, parent);
			if (!ex)
				bind(parent_leaf, parent);
			if (claimed)
				map_put(&cx.imports_resolved, path, xcat(parent_leaf, "::", leaf));
		}
	}
	/* bindings: BTreeMap by short name (unique) */
	if (cx.imports_bindings.n > 1)
		qsort(cx.imports_bindings.v, (size_t)cx.imports_bindings.n, sizeof *cx.imports_bindings.v, pair_cmp);
}

/* ---- CodeGenContext ---- */

static void ctx_init(const BcConfig *c, const PbMsg *req, const Vec *to_generate)
{
	memset(&cx, 0, sizeof cx);
	memset(&msg_index, 0, sizeof msg_index);
	memset(&inlined_fields, 0, sizeof inlined_fields);
	cx.config = c;
	cx.n_files = pb_count(req, CGR_PROTO_FILE);
	cx.files = xalloc(sizeof *cx.files * (size_t)(cx.n_files + 1));
	for (int i = 0; i < cx.n_files; i++)
		cx.files[i] = pb_msg_at(req, CGR_PROTO_FILE, i);
	Vec paths = effective_extern_paths(req, to_generate);
	Vec file_paths = effective_file_extern_paths(to_generate);

	resolve_reprs();
	for (int i = 0; i < cx.n_files; i++)
		comments_collect(cx.files[i]);

	/* all packages and top-level message names per package (not exact-extern ones) */
	Map all_packages = {0}, pkg_names = {0};
	Vec pkg_list = {0}, pkg_order = {0};
	for (int i = 0; i < cx.n_files; i++) {
		const PbMsg *file = cx.files[i];
		const char *package = pb_str(file, FDP_PACKAGE);
		if (!map_has(&all_packages, package)) {
			map_put(&all_packages, package, (void *)1);
			vec_push(&pkg_list, (void *)package);
		}
		int n = pb_count(file, FDP_MESSAGE_TYPE);
		for (int j = 0; j < n; j++) {
			const PbMsg *m = pb_msg_at(file, FDP_MESSAGE_TYPE, j);
			if (!pb_has(m, DP_NAME))
				continue;
			char *fqn = *package ? xcat(".", package, ".", dname(m)) : xcat(".", dname(m));
			if (exact_extern(fqn, &paths))
				continue;
			Vec *names = map_get(&pkg_names, package);
			if (!names) {
				names = xalloc(sizeof *names);
				map_put(&pkg_names, package, names);
				vec_push(&pkg_order, (void *)package);
			}
			vec_push(names, (void *)dname(m));
		}
	}
	for (int i = 0; i < pkg_order.n; i++)
		deconflict_package(pkg_order.v[i], map_get(&pkg_names, pkg_order.v[i]), &all_packages, &pkg_list);

	for (int i = 0; i < cx.n_files; i++) {
		const PbMsg *file = cx.files[i];
		const char *package = pb_str(file, FDP_PACKAGE);
		Feat ff = feat_for_file(file);
		char *proto_prefix = *package ? xcat(".", package, ".") : ".";
		const char *file_root = NULL;
		if (pb_has(file, FDP_NAME))
			for (int k = 0; k < file_paths.n; k++)
				if (str_eq(((BcPair *)file_paths.v[k])->key, pb_str(file, FDP_NAME)))
					file_root = ((BcPair *)file_paths.v[k])->value;
		char *local_module = str_replace_all(package, ".", "::");
		int n = pb_count(file, FDP_MESSAGE_TYPE);
		for (int j = 0; j < n; j++) {
			const PbMsg *m = pb_msg_at(file, FDP_MESSAGE_TYPE, j);
			if (!pb_has(m, DP_NAME))
				continue;
			const char *name = dname(m);
			char *fqn = xcat(proto_prefix, name);
			bool is_extern;
			const char *rust = resolve_type_path(fqn, name, file_root, local_module, &paths, &is_extern);
			const char *snake = map_get(&cx.nested_mods, fqn);
			if (!snake)
				snake = to_snake_case(name);
			const char *parent_mod;
			if (is_extern) {
				const char *cut = NULL;
				for (const char *p = strstr(rust, "::"); p; p = strstr(p + 2, "::"))
					cut = p;
				parent_mod = cut ? xcat(xstrndup(rust, (size_t)(cut - rust)), "::", snake) : snake;
			} else
				parent_mod = join_mod(local_module, snake);
			map_put(&cx.type_map, fqn, (void *)rust);
			map_put(&cx.package_of, fqn, (void *)package);
			register_nested_types(package, &paths, fqn, parent_mod, m);
			register_nested_enum_closedness(fqn, ff, m);
		}
		n = pb_count(file, FDP_ENUM_TYPE);
		for (int j = 0; j < n; j++) {
			const PbMsg *e = pb_msg_at(file, FDP_ENUM_TYPE, j);
			if (!pb_has(e, EDP_NAME))
				continue;
			char *fqn = xcat(proto_prefix, dname(e));
			bool is_extern;
			map_put(&cx.type_map, fqn, (void *)resolve_type_path(fqn, dname(e), file_root, local_module, &paths, &is_extern));
			map_put(&cx.package_of, fqn, (void *)package);
			register_enum_closedness(fqn, ff, e);
		}
	}
	if (CFG->idiomatic_field_names)
		for (int i = 0; i < cx.n_files; i++) {
			int n = pb_count(cx.files[i], FDP_MESSAGE_TYPE);
			for (int j = 0; j < n; j++)
				plan_recurse(pb_msg_at(cx.files[i], FDP_MESSAGE_TYPE, j));
		}
	if (CFG->feature_overrides.n)
		for (int i = 0; i < cx.n_files; i++) {
			const PbMsg *file = cx.files[i];
			const char *package = pb_str(file, FDP_PACKAGE);
			char *prefix = *package ? xcat(".", package, ".") : ".";
			collect_enum_first(prefix, file, FDP_ENUM_TYPE);
			int n = pb_count(file, FDP_MESSAGE_TYPE);
			for (int j = 0; j < n; j++)
				walk_enum_first(prefix, pb_msg_at(file, FDP_MESSAGE_TYPE, j));
		}
}

static const char *ctx_comment(const char *fqn) { return map_get(&cx.comments, fqn); }
static const char *ctx_rust_type(const char *fqn) { return map_get(&cx.type_map, fqn); }

static int ctx_is_enum_closed(const char *fqn)
{
	uintptr_t v = (uintptr_t)map_get(&cx.enum_closed, fqn);
	return v ? (int)(v == 2) : -1;
}

static char *ctx_nested_module_name(const char *pkg, const char *name)
{
	const char *m = map_get(&cx.nested_mods, *pkg ? xcat(".", pkg, ".", name) : xcat(".", name));
	return m ? (char *)m : to_snake_case(name);
}

static bool is_abs_path(const char *p) { return has_prefix(p, "::") || has_prefix(p, "crate::"); }

static char *supers(int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_adds(&b, i ? "::super" : "super");
	return (char *)buf_str(&b);
}

/* the type's path within its package, and the up/down path between packages */
static const char *strip_module(const char *full, const char *module)
{
	if (!*module)
		return full;
	char *pre = xcat(module, "::");
	return has_prefix(full, pre) ? full + strlen(pre) : full;
}

static char *cross_package(const char *cur, const char *target, int nesting)
{
	Vec cp = *cur ? str_split(cur, '.') : (Vec){0};
	Vec tp = *target ? str_split(target, '.') : (Vec){0};
	int common = 0;
	while (common < cp.n && common < tp.n && str_eq(cp.v[common], tp.v[common]))
		common++;
	int up = cp.n - common + nesting;
	Buf b = {0};
	for (int i = 0; i < up; i++)
		buf_adds(&b, i ? "::super" : "super");
	for (int i = common; i < tp.n; i++) {
		if (b.n)
			buf_adds(&b, "::");
		buf_adds(&b, tp.v[i]);
	}
	return (char *)buf_str(&b);
}

static const char *ctx_rust_type_relative(const char *fqn, const char *pkg, int nesting)
{
	const char *full = map_get(&cx.type_map, fqn);
	if (!full)
		return NULL;
	if (is_abs_path(full))
		return ctx_root_path(full, nesting);
	const char *target = map_get(&cx.package_of, fqn);
	if (!target)
		target = "";
	const char *suffix = strip_module(full, str_replace_all(target, ".", "::"));
	if (str_eq(pkg, target))
		return nesting == 0 ? suffix : xcat(supers(nesting), "::", suffix);
	char *r = cross_package(pkg, target, nesting);
	return ctx_root_path(*r ? xcat(r, "::", suffix) : suffix, nesting);
}

static bool ctx_rust_type_relative_split(const char *fqn, const char *pkg, int nesting, SplitPath *out)
{
	const char *full = map_get(&cx.type_map, fqn);
	if (!full)
		return false;
	const char *target = map_get(&cx.package_of, fqn);
	if (!target)
		target = "";
	const char *module;
	if (is_abs_path(full)) {
		const char *nodot = fqn[0] == '.' ? fqn + 1 : fqn;
		const char *within = nodot;
		if (*target) {
			size_t n = strlen(target);
			within = !strncmp(nodot, target, n) && nodot[n] == '.' ? nodot + n + 1 : nodot;
		}
		int within_segs = str_split(within, '.').n;
		Vec segs = {0};
		const char *p = full;
		for (;;) {
			const char *e = strstr(p, "::");
			vec_push(&segs, e ? xstrndup(p, (size_t)(e - p)) : (char *)p);
			if (!e)
				break;
			p = e + 2;
		}
		int cut = segs.n > within_segs ? segs.n - within_segs : 0;
		segs.n = cut;
		module = str_join(&segs, "::");
	} else
		module = str_replace_all(target, ".", "::");
	out->within_package = strip_module(full, module);
	out->is_extern = false;
	if (is_abs_path(full)) {
		out->to_package = module;
		out->is_extern = true;
	} else if (str_eq(pkg, target))
		out->to_package = nesting == 0 ? "" : supers(nesting);
	else
		out->to_package = cross_package(pkg, target, nesting);
	return true;
}

/* ancillary_prefix: `super::...::__buffa::<kind>::<msg path>::` as a path prefix string */
static char *ancillary_prefix(int kind, const char *pkg, const char *fqn, int from_nesting)
{
	static const char *const segs[] = {"oneof", "view", "view::oneof", "lazy_view"};
	Buf b = {0};
	for (int i = 0; i < from_nesting; i++)
		buf_adds(&b, "super::");
	buf_adds(&b, SENTINEL_MOD "::");
	buf_adds(&b, segs[kind]);
	buf_adds(&b, "::");
	const char *within = fqn;
	if (*pkg) {
		size_t n = strlen(pkg);
		within = !strncmp(fqn, pkg, n) && fqn[n] == '.' ? fqn + n + 1 : fqn;
	}
	Vec parts = str_split(within, '.');
	for (int i = 0; i < parts.n; i++)
		if (*(char *)parts.v[i]) {
			buf_adds(&b, make_field_ident(to_snake_case(parts.v[i])));
			buf_adds(&b, "::");
		}
	return (char *)buf_str(&b);
}

/* ---- exported for connectrpc-codegen (buffa_codegen.h) ---- */

void bc_context_for_generate(const BcConfig *c, const PbMsg *req)
{
	Vec to_generate = {0};
	int n = pb_count(req, CGR_FILE_TO_GENERATE);
	for (int i = 0; i < n; i++)
		vec_push(&to_generate, (void *)pb_str_at(req, CGR_FILE_TO_GENERATE, i));
	ctx_init(c, req, &to_generate);
}

const char *bc_rust_type_relative(const char *fqn, const char *current_package, int nesting)
{
	return ctx_rust_type_relative(fqn, current_package, nesting);
}

bool bc_rust_type_relative_split(const char *fqn, const char *current_package, int nesting, const char **to_package,
				 const char **within_package)
{
	SplitPath s;
	if (!ctx_rust_type_relative_split(fqn, current_package, nesting, &s))
		return false;
	*to_package = s.to_package;
	*within_package = s.within_package;
	return true;
}

char *bc_make_field_ident(const char *name) { return make_field_ident(name); }
char *bc_rust_path(const char *path) { return rust_path(path); }
