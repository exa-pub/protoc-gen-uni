/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/oneof.rs: oneof enums (variants, Debug, From impls, the JSON
 * Serialize impl) and the oneof naming helpers. The unboxed-variant and inline-field resolution
 * is in context.c. */

static char *oneof_variant_ident(const char *proto_name) { return make_field_ident(to_pascal_case(proto_name)); }

static const char **resolve_oneof_idents(const PbMsg *msg)
{
	int no = pb_count(msg, DP_ONEOF_DECL), nf = n_fields(msg);
	const char **r = xalloc(sizeof *r * (size_t)(no + 1));
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		int idx = fd_oneof(f);
		if (is_real_oneof_member(f) && idx >= 0 && idx < no && pb_has(pb_msg_at(msg, DP_ONEOF_DECL, idx), ODP_NAME))
			r[idx] = to_pascal_case(dname(pb_msg_at(msg, DP_ONEOF_DECL, idx)));
	}
	return r;
}

static bool any_oneof(const char **idents, int n)
{
	for (int i = 0; i < n; i++)
		if (idents[i])
			return true;
	return false;
}

static bool is_debug_redacted(const PbMsg *field) { return pb_int(pb_msg(field, FD_OPTIONS), FLDOPT_DEBUG_REDACT, 0) != 0; }
#define DEBUG_REDACT_PLACEHOLDER "[REDACTED]"

static PpNode *scalar_or_message_type(const PbMsg *field, const char *pkg, int nesting, Feat features);

typedef struct {
	const char *variant, *type_path, *json_name;
	PpNode *rust_type;
	int field_type;
	bool is_null_value, is_boxed, debug_redact;
	const PbMsg *field;
} VariantInfo;

static VariantInfo *collect_variant_info(const PbMsg *msg, const char *oneof_name, const char *pkg, const char *fqn, Feat features,
					 int nesting, int *count)
{
	int no = pb_count(msg, DP_ONEOF_DECL), idx = -1;
	for (int i = 0; i < no && idx < 0; i++)
		if (pb_has(pb_msg_at(msg, DP_ONEOF_DECL, i), ODP_NAME) && str_eq(dname(pb_msg_at(msg, DP_ONEOF_DECL, i)), oneof_name))
			idx = i;
	if (idx < 0)
		fail("%s", CODEGEN_ERROR("oneof '%s' not found in message", oneof_name));
	int nf = n_fields(msg);
	VariantInfo *v = xalloc(sizeof *v * (size_t)(nf + 1));
	int n = 0;
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (!fd_has_oneof(f) || fd_oneof(f) != idx || fd_proto3_optional(f))
			continue;
		const char *name = field_name_req(f);
		VariantInfo *vi = &v[n++];
		vi->field = f;
		vi->json_name = pb_has(f, FD_JSON_NAME) ? pb_str(f, FD_JSON_NAME) : name;
		vi->variant = oneof_variant_ident(name);
		vi->field_type = effective_type(f, features);
		vi->rust_type = scalar_or_message_type(f, pkg, nesting + 3, features);
		char *dotted = xcat(".", fqn, ".", oneof_name, ".", name);
		vi->is_boxed = variant_boxed(vi->field_type, dotted);
		if (vi->is_boxed)
			for (int k = 0; k < CFG->unboxed_oneof_fields.n; k++)
				if (str_eq(CFG->unboxed_oneof_fields.v[k], dotted))
					fail("%s", CODEGEN_ERROR("oneof variant `%s.%s.%s` is recursive and cannot be stored inline: it would "
								 "make the generated enum unsized. Remove `\"%s\"` from unbox_oneof_in, or "
								 "use a broader prefix (or the blanket `.` rule) to keep this variant boxed "
								 "while inlining the rest.", fqn, oneof_name, name, dotted));
		vi->is_null_value = fd_type_name(f) && str_eq(fd_type_name(f), ".google.protobuf.NullValue");
		vi->debug_redact = is_debug_redacted(f);
	}
	*count = n;
	return v;
}

static const char *type_key(PpNode *ty);
static void generate_oneof_serialize(Vec *out, const char *enum_ident, const VariantInfo *v, int n);

static void generate_oneof_enum(Vec *out, const PbMsg *msg, int idx, const char *pkg, const char *fqn, Feat features,
				const char **oneof_idents, int nesting)
{
	const char *enum_ident = oneof_idents[idx];
	if (!enum_ident)
		return;
	const PbMsg *oneof = pb_msg_at(msg, DP_ONEOF_DECL, idx);
	if (!pb_has(oneof, ODP_NAME))
		fail("missing required descriptor field: oneof.name");
	const char *oneof_name = dname(oneof);
	int n;
	VariantInfo *v = collect_variant_info(msg, oneof_name, pkg, fqn, features, nesting, &n);
	if (!n)
		return;
	PpNode *en = pp_item_enum(PP_VIS_PUB, enum_ident);
	for (int i = 0; i < n; i++) {
		PpNode *var = pp_variant(v[i].variant, PP_F_UNNAMED, NULL);
		PpNode *ty = v[i].is_boxed ? ty_g1("::buffa::alloc::boxed::Box", v[i].rust_type) : v[i].rust_type;
		pp_push(var, pp_field(0, NULL, ty));
		pp_push(en, var);
	}
	Map counts = {0};
	for (int i = 0; i < n; i++)
		if (is_boxed_variant(v[i].field_type)) {
			const char *k = type_key(v[i].rust_type);
			map_put(&counts, k, (void *)((uintptr_t)map_get(&counts, k) + 1));
		}
	Vec from_impls = {0};
	for (int i = 0; i < n; i++) {
		if (!is_boxed_variant(v[i].field_type) || (uintptr_t)map_get(&counts, type_key(v[i].rust_type)) != 1)
			continue;
		const char *ty = type_key(v[i].rust_type);
		bool ext = has_prefix(ty, "::");
		PpNode *wrapped = v[i].is_boxed ? e_call1("::buffa::alloc::boxed::Box::new", pp_e("v")) : pp_e("v");
		PpNode *imp = pp_item_impl(pp_path_args(pp_path("From"), pp_angle(v[i].rust_type, NULL)), pp_ty(enum_ident));
		PpNode *sig = pp_sig("from", pp_ty("Self"));
		pp_push(sig, arg("v", v[i].rust_type));
		pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call1(xcat("Self::", v[i].variant), wrapped)), NULL)));
		vec_push(&from_impls, imp);
		if (!ext) {
			imp = pp_item_impl(pp_path_args(pp_path("From"), pp_angle(v[i].rust_type, NULL)), ty_g1(OPTION_PATH, pp_ty(enum_ident)));
			sig = pp_sig("from", pp_ty("Self"));
			pp_push(sig, arg("v", v[i].rust_type));
			pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call1("Self::Some", e_call1(xcat(enum_ident, "::from"), pp_e("v")))), NULL)));
			vec_push(&from_impls, imp);
		}
	}
	const char *oneof_fqn = xcat(fqn, ".", oneof_name);
	Vec doc = {0};
	doc_attrs(&doc, ctx_comment(oneof_fqn), fqn);
	attrs_all(en, &doc);
	bool any_redacted = false, large = false;
	for (int i = 0; i < n; i++) {
		any_redacted |= v[i].debug_redact;
		large |= is_boxed_variant(v[i].field_type) && !v[i].is_boxed;
	}
	pp_push_attr(en, attr_src("derive", any_redacted ? "Clone, PartialEq" : "Clone, PartialEq, Debug"));
	if (CFG->generate_arbitrary)
		pp_push_attr(en, attr_src("cfg_attr", "feature = \"arbitrary\", derive(::arbitrary::Arbitrary)"));
	if (large)
		pp_push_attr(en, attr_src("allow", "clippy::large_enum_variant"));
	vec_push(out, en);
	if (any_redacted) {
		PpNode *m = pp_e_match(pp_e("self"));
		for (int i = 0; i < n; i++) {
			const char *name = has_prefix(v[i].variant, "r#") ? v[i].variant + 2 : v[i].variant;
			PpNode *dt = e_meth1(pp_e("f"), "debug_tuple", lit_str(name)), *body;
			if (v[i].debug_redact) {
				PpNode *fa = pp_macro(pp_path("::core::format_args"), PP_DELIM_PAREN);
				pp_push(fa, pp_tt_lit(pm2_string(str_c(DEBUG_REDACT_PLACEHOLDER))));
				body = e_meth0(e_meth1(dt, "field", pp_e_ref(false, pp_e_macro(fa))), "finish");
				pp_push(m, pp_arm(pp_p_tuple_struct(pp_path(xcat("Self::", v[i].variant)), pp_p_wild(), NULL), NULL, body));
			} else {
				body = e_meth0(e_meth1(dt, "field", pp_e("value")), "finish");
				pp_push(m, pp_arm(pp_p_tuple_struct(pp_path(xcat("Self::", v[i].variant)), pp_p_ident("value", false, false), NULL), NULL, body));
			}
		}
		PpNode *imp = pp_item_impl(pp_path("::core::fmt::Debug"), pp_ty(enum_ident));
		PpNode *sig = pp_sig("fmt", pp_ty("::core::fmt::Result"));
		pp_push(sig, pp_receiver(true, false, NULL));
		pp_push(sig, arg("f", ty_ref_mut(pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
		pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(m), NULL)));
		vec_push(out, imp);
	}
	vec_push(out, pp_item_impl(pp_path("::buffa::Oneof"), pp_ty(enum_ident)));
	vcat(out, &from_impls);
	if (CFG->generate_json) {
		Vec ser = {0};
		generate_oneof_serialize(&ser, enum_ident, v, n);
		cfg_block(out, &ser, gates().json);
	}
}
