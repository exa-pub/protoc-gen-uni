/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/message.rs: message structs (fields, Debug, TYPE_URL, with_*
 * setters, custom Default), the nested-type module trees, the natural re-exports and the
 * per-message JSON/text registry consts. The JSON field attributes and custom Deserialize are in
 * json.c. */

typedef struct {
	Vec owned_top, owned_mod, oneof_tree, view_tree, lazy_view_tree, view_oneof_tree;
	Reg reg;
} MsgOut;

/* canonical text of a type node (for the From-impl type counts and extern checks) */
static void type_key_rec(Buf *b, const PpNode *t)
{
	if (t->kind == PP_TY_PATH) {
		const PpNode *p = t->b;
		if (p->flags & PP_F_LEADING_COLON)
			buf_adds(b, "::");
		for (int i = 0; i < p->list.n; i++) {
			const PpNode *seg = p->list.v[i];
			if (i)
				buf_adds(b, "::");
			buf_adds(b, seg->text);
			if (seg->a && seg->a->kind == PP_ANGLE_ARGS) {
				buf_addc(b, '<');
				for (int k = 0; k < seg->a->list.n; k++) {
					if (k)
						buf_addc(b, ',');
					type_key_rec(b, seg->a->list.v[k]);
				}
				buf_addc(b, '>');
			}
		}
	} else
		buf_fmt(b, "#%d", t->kind);
}

static const char *type_key(PpNode *ty)
{
	Buf b = {0};
	type_key_rec(&b, ty);
	return buf_str(&b);
}

static PpNode *ty_path_str(const char *path) { return pp_ty_path(pp_path(rust_path(path))); }

static PpNode *scalar_rust_type(int t, int nesting)
{
	switch (t) {
	case PT_DOUBLE: return pp_ty("f64");
	case PT_FLOAT: return pp_ty("f32");
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return pp_ty("i64");
	case PT_UINT64: case PT_FIXED64: return pp_ty("u64");
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return pp_ty("i32");
	case PT_UINT32: case PT_FIXED32: return pp_ty("u32");
	case PT_BOOL: return pp_ty("bool");
	case PT_STRING: return pp_ty(ctx_root_path(STRING_PATH, nesting));
	case PT_BYTES: return ty_g1(ctx_root_path(VEC_PATH, nesting), pp_ty("u8"));
	default: fail("%s", CODEGEN_ERROR("scalar_rust_type called for non-scalar type"));
	}
}

static PpNode *resolve_message_type(const PbMsg *field, const char *pkg, int nesting)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	const char *p = ctx_rust_type_relative(tn, pkg, nesting);
	if (!p)
		fail("%s", CODEGEN_ERROR("message type '%s' not found in descriptor set; ensure all imports are included with "
					 "--include_imports", tn));
	return ty_path_str(p);
}

static PpNode *resolve_enum_type(const PbMsg *field, const char *pkg, int nesting, Feat features)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	const char *p = ctx_rust_type_relative(tn, pkg, nesting);
	if (!p)
		fail("%s", CODEGEN_ERROR("enum type '%s' not found in descriptor set; ensure all imports are included with "
					 "--include_imports", tn));
	if (is_closed_enum(feat_resolve_field(field, features)))
		return ty_path_str(p);
	return ty_g1(ctx_root_path(ENUM_VALUE_PATH, nesting), ty_path_str(p));
}

static PpNode *scalar_or_message_type(const PbMsg *field, const char *pkg, int nesting, Feat features)
{
	int t = effective_type(field, features);
	if (t == PT_MESSAGE || t == PT_GROUP)
		return resolve_message_type(field, pkg, nesting);
	if (t == PT_ENUM)
		return resolve_enum_type(field, pkg, nesting, features);
	return scalar_rust_type(t, nesting);
}

/* FieldInfo (the parts the plugin path uses) */
typedef struct {
	PpNode *rust_type, *struct_field_type, *inner_opt_type;
	bool is_repeated, is_map, is_optional, is_required;
	int field_type, map_key_type, map_value_type; /* 0: none */
	int map_value_enum_closed;                    /* -1 none */
} FieldInfo;

static FieldInfo classify_field(const Scope *s, const PbMsg *msg, const PbMsg *field)
{
	FieldInfo fi = {0};
	fi.map_value_enum_closed = -1;
	int label = fd_label(field);
	int ty = effective_type(field, s->features);
	fi.field_type = ty;
	fi.is_repeated = label == LABEL_REPEATED;
	const PbMsg *entry = fi.is_repeated ? find_map_entry(msg, field) : NULL;
	fi.is_map = entry != NULL;
	fi.is_optional = is_explicit_presence_scalar(field, ty, s->features);
	fi.is_required = is_required_field(field, s->features);
	const char *field_fqn = xcat(".", s->fqn, ".", dname(field));
	bool inline_ptr = ctx_field_inline(field_fqn);
	if (entry) {
		const PbMsg *k = NULL, *v = NULL;
		for (int i = 0; i < n_fields(entry); i++) {
			const PbMsg *f = field_at(entry, i);
			if (!k && pb_has(f, FD_NUMBER) && fd_number(f) == 1)
				k = f;
			if (!v && pb_has(f, FD_NUMBER) && fd_number(f) == 2)
				v = f;
		}
		if (!k)
			fail("missing required descriptor field: map_entry.key");
		if (!v)
			fail("missing required descriptor field: map_entry.value");
		fi.rust_type = ty_g2(ctx_root_path(HASHMAP_PATH, s->nesting), scalar_or_message_type(k, s->pkg, s->nesting, s->features),
				     scalar_or_message_type(v, s->pkg, s->nesting, s->features));
		fi.map_key_type = effective_type_in_map_entry(k, s->features);
		fi.map_value_type = effective_type_in_map_entry(v, s->features);
		if (fi.map_value_type == PT_ENUM)
			fi.map_value_enum_closed = is_closed_enum(feat_resolve_field(v, s->features));
	} else if (fi.is_repeated) {
		fi.rust_type = ty_g1(ctx_root_path(VEC_PATH, s->nesting), scalar_or_message_type(field, s->pkg, s->nesting, s->features));
	} else if (ty == PT_MESSAGE || ty == PT_GROUP) {
		PpNode *inner = resolve_message_type(field, s->pkg, s->nesting);
		const char *mf = ctx_root_path(MESSAGE_FIELD_PATH, s->nesting);
		fi.rust_type = inline_ptr ? ty_g2(mf, inner, ty_g1("::buffa::Inline", inner)) : ty_g1(mf, inner);
	} else if (fi.is_optional) {
		PpNode *inner = ty == PT_ENUM ? resolve_enum_type(field, s->pkg, s->nesting, s->features) : scalar_rust_type(ty, s->nesting);
		fi.inner_opt_type = inner;
		fi.rust_type = ty_g1(ctx_root_path(OPTION_PATH, s->nesting), inner);
	} else if (ty == PT_ENUM)
		fi.rust_type = resolve_enum_type(field, s->pkg, s->nesting, s->features);
	else
		fi.rust_type = scalar_rust_type(ty, s->nesting);
	const char *tn = fd_type_name(field);
	bool self_ref = tn && str_eq(tn, xcat(".", s->fqn)) && !fi.is_map;
	if (self_ref) {
		if (fi.is_repeated)
			fi.struct_field_type = ty_g1(ctx_root_path(VEC_PATH, s->nesting), pp_ty("Self"));
		else {
			const char *mf = ctx_root_path(MESSAGE_FIELD_PATH, s->nesting);
			fi.struct_field_type = inline_ptr ? ty_g2(mf, pp_ty("Self"), ty_g1("::buffa::Inline", pp_ty("Self"))) : ty_g1(mf, pp_ty("Self"));
		}
	} else
		fi.struct_field_type = fi.rust_type;
	return fi;
}

typedef struct {
	PpNode *decl;
	const char *ident;
	const char *setter;  /* NULL: none */
	PpNode *param_type;
	bool use_into, debug_redact;
} GenField;

static void serde_field_attr(Vec *attrs, const PbMsg *field, const char *name, const FieldInfo *fi, Feat features);

static bool generate_field(const Scope *s, const PbMsg *msg, const PbMsg *field, GenField *out)
{
	const char *name = field_name_req(field);
	int number = fd_number(field);
	if (is_real_oneof_member(field))
		return false;
	FieldInfo fi = classify_field(s, msg, field);
	const char *rust_name = ctx_field_ident(name, number);
	const char *field_fqn = xcat(s->fqn, ".", name);
	Vec attrs = {0};
	doc_attrs_with_tag(&attrs, ctx_comment(field_fqn), xfmt("Field %d: `%s`", number, name), s->fqn);
	const char *note = ctx_field_rename_note(name, number);
	if (note) {
		vec_push(&attrs, doc_attr(""));
		vec_push(&attrs, doc_attr(note));
	}
	if (CFG->generate_json)
		serde_field_attr(&attrs, field, name, &fi, s->features);
	PpNode *f = pp_field(PP_VIS_PUB, rust_name, fi.struct_field_type);
	attrs_all(f, &attrs);
	out->decl = f;
	out->ident = rust_name;
	out->setter = NULL;
	out->debug_redact = is_debug_redacted(field);
	if (fi.inner_opt_type) {
		int ft = effective_type(field, s->features);
		out->setter = xcat("with_", ctx_field_rust_name(name, number));
		out->use_into = ft == PT_STRING || ft == PT_BYTES || ft == PT_ENUM;
		out->param_type = out->use_into ? pp_ty_impl(pp_bounds(pp_trait_bound(pp_path_args(pp_path("Into"), pp_angle(fi.inner_opt_type, NULL)), false), NULL))
						: fi.inner_opt_type;
	}
	return true;
}

/* generate_custom_default */
static PpNode *generate_custom_default(const PbMsg *msg, const char *name, const Scope *s)
{
	bool explicit_ = s->features.presence == PROTO_PRESENCE_EXPLICIT;
	bool overrides = CFG->feature_overrides.n > 0;
	if (!explicit_ && !overrides)
		return NULL;
	int nf = n_fields(msg);
	bool custom = false;
	for (int i = 0; i < nf && !custom; i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		int ft = effective_type(f, s->features);
		if (is_explicit_presence_scalar(f, ft, s->features) || fd_label(f) == LABEL_REPEATED || ft == PT_MESSAGE || ft == PT_GROUP)
			continue;
		if ((explicit_ && pb_has(f, FD_DEFAULT_VALUE) && *pb_str(f, FD_DEFAULT_VALUE)) ||
		    (overrides && open_enum_bare_default_value(f, s->pkg, feat_resolve_field(f, s->features), s->nesting)))
			custom = true;
	}
	if (!custom)
		return NULL;
	PpNode *st = pp_e_struct(pp_path("Self"), NULL);
	PpNode *dflt = pp_e_call(pp_e("::core::default::Default::default"), NULL);
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		const char *id = ctx_field_ident(field_name_req(f), fd_number(f));
		int ft = effective_type(f, s->features);
		if (is_explicit_presence_scalar(f, ft, s->features) || fd_label(f) == LABEL_REPEATED || ft == PT_MESSAGE || ft == PT_GROUP) {
			pp_push(st, pp_field_value(id, dflt));
			continue;
		}
		Feat ff = feat_resolve_field(f, s->features);
		PpNode *e = parse_default_value(f, s->pkg, ff, s->nesting);
		if (!e)
			e = open_enum_bare_default_value(f, s->pkg, ff, s->nesting);
		pp_push(st, pp_field_value(id, e ? e : dflt));
	}
	int no = pb_count(msg, DP_ONEOF_DECL);
	for (int i = 0; i < no; i++) {
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, i);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		bool real = false;
		for (int k = 0; k < nf; k++)
			real |= is_real_oneof_member(field_at(msg, k)) && fd_oneof(field_at(msg, k)) == i;
		if (real)
			pp_push(st, pp_field_value(ctx_oneof_ident(dname(o)), dflt));
	}
	if (CFG->preserve_unknown_fields)
		pp_push(st, pp_field_value("__buffa_unknown_fields", dflt));
	PpNode *imp = pp_item_impl(pp_path("::core::default::Default"), pp_ty(name));
	pp_push(imp, pp_item_fn(0, pp_sig("default", pp_ty("Self")), pp_block(s_expr(st), NULL)));
	return imp;
}

/* emit_surviving_reexports */
static void emit_surviving_reexports(Vec *out, const Vec *candidates, const Map *occupied)
{
	Map counts = {0};
	for (int i = 0; i < candidates->n; i++) {
		const Reexport *c = candidates->v[i];
		map_put(&counts, c->name, (void *)((uintptr_t)map_get(&counts, c->name) + 1));
	}
	for (int i = 0; i < candidates->n; i++) {
		const Reexport *c = candidates->v[i];
		if (map_has(occupied, c->name) || (uintptr_t)map_get(&counts, c->name) > 1)
			continue;
		vcat(out, &c->items);
	}
}

static Reexport *reexport(const char *name, const char *path, const char *rename, const char *gate)
{
	Reexport *r = xalloc(sizeof *r);
	r->name = name;
	PpNode *tree;
	if (rename) {
		/* path::Leaf as rename */
		tree = pp_use_tree(xcat(path, " as ", rename));
	} else
		tree = pp_use_tree(path);
	PpNode *u = pp_item_use(PP_VIS_PUB, tree, false);
	pp_push_attr(u, attr_src("doc", "inline"));
	Vec one_ = {0};
	vec_push(&one_, u);
	cfg_block(&r->items, &one_, gate);
	return r;
}

static char *extension_const_ident(const char *name);

static void collect_natural_reexports(Vec *out, const Scope *s, const PbMsg *msg, const char **oneof_idents, const Vec *non_map_nested)
{
	int no = pb_count(msg, DP_ONEOF_DECL);
	if (!CFG->generate_views && !any_oneof(oneof_idents, no))
		return;
	Map occupied = {0};
	for (int i = 0; i < non_map_nested->n; i++) {
		const char *name = dname(non_map_nested->v[i]);
		map_put(&occupied, prefixed_type_name(name), (void *)1);
		map_put(&occupied, to_snake_case(name), (void *)1);
	}
	int ne = pb_count(msg, DP_ENUM_TYPE);
	for (int i = 0; i < ne; i++)
		map_put(&occupied, prefixed_type_name(dname(pb_msg_at(msg, DP_ENUM_TYPE, i))), (void *)1);
	int nx = pb_count(msg, DP_EXTENSION);
	for (int i = 0; i < nx; i++)
		map_put(&occupied, extension_const_ident(dname(pb_msg_at(msg, DP_EXTENSION, i))), (void *)1);
	int from = s->nesting + 1;
	char *view_prefix = ancillary_prefix(ANC_VIEW, s->pkg, s->fqn, from);
	char *oneof_prefix = ancillary_prefix(ANC_ONEOF, s->pkg, s->fqn, from);
	char *view_oneof_prefix = ancillary_prefix(ANC_VIEW_ONEOF, s->pkg, s->fqn, from);
	char *lazy_view_prefix = ancillary_prefix(ANC_LAZY_VIEW, s->pkg, s->fqn, from);
	Vec cands = {0};
	for (int i = 0; i < no; i++)
		if (oneof_idents[i])
			vec_push(&cands, reexport(oneof_idents[i], xcat(oneof_prefix, oneof_idents[i]), NULL, NULL));
	if (CFG->generate_views) {
		const char *g = gates().views;
		for (int i = 0; i < non_map_nested->n; i++) {
			const char *rn = prefixed_type_name(dname(non_map_nested->v[i]));
			char *v = xcat(rn, "View"), *ov = xcat(rn, "OwnedView");
			vec_push(&cands, reexport(v, xcat(view_prefix, v), NULL, g));
			vec_push(&cands, reexport(ov, xcat(view_prefix, ov), NULL, g));
			if (CFG->lazy_views) {
				char *lv = xcat(rn, "LazyView");
				vec_push(&cands, reexport(lv, xcat(lazy_view_prefix, lv), NULL, g));
			}
		}
		for (int i = 0; i < no; i++)
			if (oneof_idents[i]) {
				char *v = xcat(oneof_idents[i], "View");
				vec_push(&cands, reexport(v, xcat(view_oneof_prefix, oneof_idents[i]), v, g));
			}
	}
	emit_surviving_reexports(out, &cands, &occupied);
}

static void generate_extensions(Vec *out, Vec *json_ids, Vec *text_ids, const Vec *exts, const char *pkg, int nesting, Feat features,
				const char *scope_fqn);
static void generate_view_with_nesting(const Scope *s, const PbMsg *msg, const char *rust_name, Vec *view_top, Vec *view_oneofs);
static void generate_lazy_view_with_nesting(const Scope *s, const PbMsg *msg, const char *rust_name, Vec *out);
static void generate_text_impl(Vec *out, const PbMsg *msg, const char *rust_name, const Scope *s, bool has_ext_ranges,
			       const char **oneof_idents, const char *oneof_prefix);
static void generate_custom_deserialize(Vec *out, const Scope *s, const PbMsg *msg, const char *name, const char *oneof_prefix,
					bool has_ext_ranges, const char **oneof_idents);
static void ext_json_wrapper(Vec *out, const char *ident, const char *fqn);

static PpNode *wrap_mod(const char *mod, Vec *body)
{
	return body->n ? item_mod_super(mod, body) : NULL;
}

static void generate_message_with_nesting(const Scope *s, const PbMsg *msg, const char *rust_name, MsgOut *out, int depth)
{
	if (depth > 100)
		fail("message nesting too deep");
	const char *pkg = s->pkg, *fqn = s->fqn;
	int nesting = s->nesting;
	memset(out, 0, sizeof *out);
	bool message_set = pb_int(pb_msg(msg, DP_OPTIONS), MOPT_MESSAGE_SET, 0) != 0;
	if (message_set && !CFG->allow_message_set)
		fail("message '%s' uses `option message_set_wire_format = true` but CodeGenConfig::allow_message_set is false; "
		     "MessageSet is a legacy wire format \xe2\x80\x94 set allow_message_set(true) if this is intentional", fqn);

	Vec nested_enums = {0};
	int ne = pb_count(msg, DP_ENUM_TYPE);
	for (int i = 0; i < ne; i++) {
		const PbMsg *e = pb_msg_at(msg, DP_ENUM_TYPE, i);
		generate_enum(&nested_enums, e, prefixed_type_name(dname(e)), xcat(fqn, ".", dname(e)), s->features);
	}
	Vec non_map_nested = {0};
	int nn = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < nn; i++)
		if (!msg_is_map_entry(pb_msg_at(msg, DP_NESTED_TYPE, i)))
			vec_push(&non_map_nested, (void *)pb_msg_at(msg, DP_NESTED_TYPE, i));
	MsgOut *nested_out = xalloc(sizeof *nested_out * (size_t)(non_map_nested.n + 1));
	for (int i = 0; i < non_map_nested.n; i++) {
		const PbMsg *nested = non_map_nested.v[i];
		Scope ns = {pkg, xcat(fqn, ".", dname(nested)), feat_child(s->features, msg_features(nested)), nesting + 1};
		generate_message_with_nesting(&ns, nested, prefixed_type_name(dname(nested)), &nested_out[i], depth + 1);
	}

	int nf = n_fields(msg);
	GenField *gf = xalloc(sizeof *gf * (size_t)(nf + 1));
	int ngf = 0;
	for (int i = 0; i < nf; i++)
		if (generate_field(s, msg, field_at(msg, i), &gf[ngf]))
			ngf++;

	PpNode *setters = pp_item_impl(NULL, pp_ty(rust_name));
	int nsetters = 0;
	for (int i = 0; i < ngf; i++) {
		if (!gf[i].setter)
			continue;
		const char *doc = has_prefix(gf[i].ident, "r#")
					  ? xcat("Sets `", gf[i].ident + 2, "` to `Some(value)`, consuming and returning `self`.")
					  : xcat("Sets [`Self::", gf[i].ident, "`] to `Some(value)`, consuming and returning `self`.");
		PpNode *sig = pp_sig(gf[i].setter, pp_ty("Self"));
		pp_push(sig, pp_receiver(false, true, NULL));
		pp_push(sig, arg("value", gf[i].param_type));
		PpNode *val = gf[i].use_into ? e_meth0(pp_e("value"), "into") : pp_e("value");
		PpNode *fn = pp_item_fn(PP_VIS_PUB, sig,
					pp_block(s_semi(pp_e_assign(self_f(gf[i].ident), e_call1("Some", val))), s_expr(pp_e("self")), NULL));
		PpNode *mu = pp_attr_name_value(pp_path("must_use"), lit_str("with_* setters return `self` by value; assign or chain the result"), false);
		pp_push_attr(fn, mu);
		pp_push_attr(fn, attr_word("inline"));
		pp_push_attr(fn, doc_attr(doc));
		pp_push(setters, fn);
		nsetters++;
	}

	const char *proto_name = pb_has(msg, DP_NAME) ? dname(msg) : rust_name;
	char *mod_name = to_snake_case(proto_name);
	char *mod_ident = make_field_ident(mod_name);
	char *owned_wrap = nesting == 0 ? make_field_ident(ctx_nested_module_name(pkg, proto_name)) : mod_ident;
	const char **oneof_idents = resolve_oneof_idents(msg);
	int no = pb_count(msg, DP_ONEOF_DECL);
	char *oneof_prefix = ancillary_prefix(ANC_ONEOF, pkg, fqn, nesting);
	FeatureGates g = gates();

	Vec oneof_fields = {0}, debug_ids = {0};
	for (int i = 0; i < ngf; i++)
		vec_push(&debug_ids, (void *)(gf[i].debug_redact ? xcat("\x01", gf[i].ident) : gf[i].ident));
	for (int i = 0; i < no; i++) {
		if (!oneof_idents[i])
			continue;
		const char *fid = ctx_oneof_ident(dname(pb_msg_at(msg, DP_ONEOF_DECL, i)));
		PpNode *f = pp_field(PP_VIS_PUB, fid, ty_g1(ctx_root_path(OPTION_PATH, nesting), ty_path_str(xcat(oneof_prefix, oneof_idents[i]))));
		if (CFG->generate_json) {
			Vec a = {0};
			cfg_attr(&a, "serde(flatten)", g.json);
			attrs_all(f, &a);
		}
		vec_push(&oneof_fields, f);
		vec_push(&debug_ids, (void *)fid);
	}

	bool has_ext_ranges = pb_count(msg, DP_EXTENSION_RANGE) > 0;
	bool ext_wrapper = CFG->generate_json && CFG->preserve_unknown_fields && has_ext_ranges;
	char *wrapper_ident = xcat("__", rust_name, "ExtJson");
	PpNode *uf_field = NULL;
	Vec wrapper_def = {0};
	if (ext_wrapper) {
		ext_json_wrapper(&wrapper_def, wrapper_ident, fqn);
		uf_field = pp_field(PP_VIS_PUB, "__buffa_unknown_fields", pp_ty(wrapper_ident));
		Vec a = {0};
		cfg_attr(&a, "serde(flatten)", g.json);
		vec_push(&a, attr_src("doc", "hidden"));
		attrs_all(uf_field, &a);
	} else if (CFG->preserve_unknown_fields) {
		uf_field = pp_field(PP_VIS_PUB, "__buffa_unknown_fields", pp_ty("::buffa::UnknownFields"));
		Vec a = {0};
		if (CFG->generate_json)
			cfg_attr(&a, "serde(skip)", g.json);
		vec_push(&a, attr_src("doc", "hidden"));
		attrs_all(uf_field, &a);
	}
	bool has_real_oneofs = oneof_fields.n > 0;
	bool custom_deser = CFG->generate_json && (has_real_oneofs || (has_ext_ranges && CFG->preserve_unknown_fields));

	Vec oneof_enums = {0};
	for (int i = 0; i < no; i++)
		generate_oneof_enum(&oneof_enums, msg, i, pkg, fqn, s->features, oneof_idents, nesting);

	Vec message_impl = {0}, text_impl = {0};
	generate_message_impl(&message_impl, msg, CFG->preserve_unknown_fields, rust_name, s, oneof_idents, oneof_prefix);
	if (CFG->generate_text)
		generate_text_impl(&text_impl, msg, rust_name, s, has_ext_ranges, oneof_idents, oneof_prefix);

	char *type_url = xcat("type.googleapis.com/", fqn);
	char *upper = upper_ascii(to_snake_case(rust_name));
	Vec json_any = {0}, text_any = {0};
	const char *json_any_ident = NULL, *text_any_ident = NULL;
	if (CFG->generate_json) {
		json_any_ident = xcat("__", upper, "_JSON_ANY");
		PpNode *st = pp_e_struct(pp_path("::buffa::type_registry::JsonAnyEntry"), NULL);
		pp_push(st, pp_field_value("type_url", lit_str(type_url)));
		pp_push(st, pp_field_value("to_json", pp_e_path(pp_path_args(pp_path("::buffa::type_registry::any_to_json"), pp_angle(pp_ty(rust_name), NULL)))));
		pp_push(st, pp_field_value("from_json", pp_e_path(pp_path_args(pp_path("::buffa::type_registry::any_from_json"), pp_angle(pp_ty(rust_name), NULL)))));
		pp_push(st, pp_field_value("is_wkt", pp_e_lit_bool(false)));
		PpNode *c = pp_item_const(PP_VIS_PUB, json_any_ident, pp_ty("::buffa::type_registry::JsonAnyEntry"), st);
		pp_push_attr(c, attr_src("doc", "hidden"));
		Vec one_ = one(c);
		cfg_block(&json_any, &one_, g.json);
	}
	if (CFG->generate_text) {
		text_any_ident = xcat("__", upper, "_TEXT_ANY");
		PpNode *st = pp_e_struct(pp_path("::buffa::type_registry::TextAnyEntry"), NULL);
		pp_push(st, pp_field_value("type_url", lit_str(type_url)));
		pp_push(st, pp_field_value("text_encode", pp_e_path(pp_path_args(pp_path("::buffa::type_registry::any_encode_text"), pp_angle(pp_ty(rust_name), NULL)))));
		pp_push(st, pp_field_value("text_merge", pp_e_path(pp_path_args(pp_path("::buffa::type_registry::any_merge_text"), pp_angle(pp_ty(rust_name), NULL)))));
		PpNode *c = pp_item_const(PP_VIS_PUB, text_any_ident, pp_ty("::buffa::type_registry::TextAnyEntry"), st);
		pp_push_attr(c, attr_src("doc", "hidden"));
		Vec one_ = one(c);
		cfg_block(&text_any, &one_, g.text);
	}

	Vec custom_deserialize = {0}, elem_json = {0};
	if (custom_deser) {
		Vec cd = {0};
		generate_custom_deserialize(&cd, s, msg, rust_name, oneof_prefix, has_ext_ranges, oneof_idents);
		cfg_block(&custom_deserialize, &cd, g.json);
	}
	if (CFG->generate_json) {
		Vec one_ = one(proto_elem_json_impl(rust_name));
		cfg_block(&elem_json, &one_, g.json);
	}
	PpNode *custom_default = generate_custom_default(msg, rust_name, s);

	/* nested items, trees and registry paths */
	Vec nested_items = {0}, nested_oneof_tree = {0}, nested_view_tree = {0}, nested_lazy_view_tree = {0}, nested_view_oneof_tree = {0};
	if (json_any_ident)
		vec_push(&out->reg.json_any, (void *)json_any_ident);
	if (text_any_ident)
		vec_push(&out->reg.text_any, (void *)text_any_ident);
	for (int i = 0; i < non_map_nested.n; i++) {
		MsgOut *no_ = &nested_out[i];
		vcat(&nested_items, &no_->owned_top);
		char *nmod = make_field_ident(to_snake_case(dname(non_map_nested.v[i])));
		for (int k = 0; k < no_->reg.json_ext.n; k++)
			vec_push(&out->reg.json_ext, xcat(nmod, "::", no_->reg.json_ext.v[k]));
		for (int k = 0; k < no_->reg.text_ext.n; k++)
			vec_push(&out->reg.text_ext, xcat(nmod, "::", no_->reg.text_ext.v[k]));
		for (int k = 0; k < no_->reg.json_any.n; k++)
			vec_push(&out->reg.json_any, xcat(owned_wrap, "::", no_->reg.json_any.v[k]));
		for (int k = 0; k < no_->reg.text_any.n; k++)
			vec_push(&out->reg.text_any, xcat(owned_wrap, "::", no_->reg.text_any.v[k]));
		if (no_->owned_mod.n)
			vec_push(&nested_items, item_mod_super(nmod, &no_->owned_mod));
		vcat(&nested_oneof_tree, &no_->oneof_tree);
		vcat(&nested_view_tree, &no_->view_tree);
		vcat(&nested_lazy_view_tree, &no_->lazy_view_tree);
		vcat(&nested_view_oneof_tree, &no_->view_oneof_tree);
	}
	Vec nested_exts = {0}, ej = {0}, et = {0}, exts = {0};
	int nx = pb_count(msg, DP_EXTENSION);
	for (int i = 0; i < nx; i++)
		vec_push(&exts, (void *)pb_msg_at(msg, DP_EXTENSION, i));
	generate_extensions(&nested_exts, &ej, &et, &exts, pkg, nesting + 1, s->features, fqn);
	vcat(&out->reg.json_ext, &ej);
	vcat(&out->reg.text_ext, &et);
	Vec natural = {0};
	collect_natural_reexports(&natural, s, msg, oneof_idents, &non_map_nested);
	vcat(&out->owned_mod, &nested_enums);
	vcat(&out->owned_mod, &nested_items);
	vcat(&out->owned_mod, &nested_exts);
	vcat(&out->owned_mod, &natural);

	Vec own_view_top = {0}, own_view_oneofs = {0}, own_lazy_top = {0};
	if (CFG->generate_views)
		generate_view_with_nesting(s, msg, rust_name, &own_view_top, &own_view_oneofs);
	if (CFG->generate_views && CFG->lazy_views)
		generate_lazy_view_with_nesting(s, msg, rust_name, &own_lazy_top);
	Vec body = {0};
	vcat(&body, &oneof_enums);
	vcat(&body, &nested_oneof_tree);
	PpNode *w = wrap_mod(mod_ident, &body);
	if (w)
		vec_push(&out->oneof_tree, w);
	body = (Vec){0};
	vcat(&body, &own_view_oneofs);
	vcat(&body, &nested_view_oneof_tree);
	if ((w = wrap_mod(mod_ident, &body)))
		vec_push(&out->view_oneof_tree, w);
	vcat(&out->view_tree, &own_view_top);
	if ((w = wrap_mod(mod_ident, &nested_view_tree)))
		vec_push(&out->view_tree, w);
	vcat(&out->lazy_view_tree, &own_lazy_top);
	if ((w = wrap_mod(mod_ident, &nested_lazy_view_tree)))
		vec_push(&out->lazy_view_tree, w);

	/* Debug */
	PpNode *chain = e_meth1(pp_e("f"), "debug_struct", lit_str(rust_name));
	for (int i = 0; i < debug_ids.n; i++) {
		const char *id = debug_ids.v[i];
		bool redacted = id[0] == '\x01';
		if (redacted)
			id++;
		const char *shown = has_prefix(id, "r#") ? id + 2 : id;
		PpNode *val;
		if (redacted) {
			PpNode *fa = pp_macro(pp_path("::core::format_args"), PP_DELIM_PAREN);
			pp_push(fa, pp_tt_lit(pm2_string(str_c(DEBUG_REDACT_PLACEHOLDER))));
			val = pp_e_ref(false, pp_e_macro(fa));
		} else
			val = pp_e_ref(false, self_f(id));
		chain = pp_e_method(chain, "field", lit_str(shown), val, NULL);
	}
	chain = e_meth0(chain, "finish");
	PpNode *dbg = pp_item_impl(pp_path("::core::fmt::Debug"), pp_ty(rust_name));
	PpNode *sig = pp_sig("fmt", pp_ty("::core::fmt::Result"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("f", ty_ref_mut(pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
	pp_push(dbg, pp_item_fn(0, sig, pp_block(s_expr(chain), NULL)));

	Vec non_snake = {0};
	ctx_message_non_snake_attr(msg, &non_snake);

	PpNode *st = pp_item_struct(PP_VIS_PUB, rust_name, PP_F_NAMED);
	Vec attrs = {0};
	doc_attrs(&attrs, ctx_comment(fqn), fqn);
	vec_push(&attrs, attr_src("derive", custom_default ? "Clone, PartialEq," : "Clone, PartialEq, Default,"));
	if (CFG->generate_json) {
		cfg_attr(&attrs, custom_deser ? "derive(::serde::Serialize)" : "derive(::serde::Serialize, ::serde::Deserialize)", g.json);
		cfg_attr(&attrs, "serde(default)", g.json);
	}
	if (CFG->generate_arbitrary)
		vec_push(&attrs, attr_src("cfg_attr", "feature = \"arbitrary\", derive(::arbitrary::Arbitrary)"));
	vcat(&attrs, &non_snake);
	attrs_all(st, &attrs);
	for (int i = 0; i < ngf; i++)
		pp_push(st, gf[i].decl);
	push_all(st, &oneof_fields);
	if (uf_field)
		pp_push(st, uf_field);
	vec_push(&out->owned_top, st);
	vec_push(&out->owned_top, dbg);
	if (custom_default)
		vec_push(&out->owned_top, custom_default);
	PpNode *imp = pp_item_impl(NULL, pp_ty(rust_name));
	PpNode *c = pp_item_const(PP_VIS_PUB, "TYPE_URL", ty_static_str(), lit_str(type_url));
	pp_push_attr(c, doc_attr(" Protobuf type URL for this message, for use with `Any::pack` and"));
	pp_push_attr(c, doc_attr(" `Any::unpack_if`."));
	pp_push_attr(c, doc_attr(""));
	pp_push_attr(c, doc_attr(" Format: `type.googleapis.com/<fully.qualified.TypeName>`"));
	pp_push(imp, c);
	vec_push(&out->owned_top, imp);
	if (CFG->generate_with_setters && nsetters) {
		attrs_all(setters, &non_snake);
		vec_push(&out->owned_top, setters);
	}
	vcat(&out->owned_top, &message_impl);
	vcat(&out->owned_top, &text_impl);
	vcat(&out->owned_top, &custom_deserialize);
	vcat(&out->owned_top, &elem_json);
	vcat(&out->owned_top, &wrapper_def);
	vcat(&out->owned_top, &json_any);
	vcat(&out->owned_top, &text_any);
}

static void generate_message(const PbMsg *msg, const char *pkg, const char *rust_name, const char *fqn, Feat features, MsgOut *out)
{
	Scope s = {pkg, fqn, features, 0};
	generate_message_with_nesting(&s, msg, rust_name, out, 0);
}
