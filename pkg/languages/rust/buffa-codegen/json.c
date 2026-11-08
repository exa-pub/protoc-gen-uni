/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of the JSON (serde) parts of buffa-codegen 0.9.2 src/message.rs and src/oneof.rs:
 * serde field attributes, the custom Deserialize of messages with oneofs or extension ranges,
 * the extension JSON wrapper and the oneof Serialize impl. */

#define JSON_TODO "Unsupported: anthropics/buffa v0.9.2 json=true is not ported yet"

static bool value_needs_proto_json(int ty)
{
	return ty == PT_INT64 || ty == PT_SINT64 || ty == PT_SFIXED64 || ty == PT_UINT64 || ty == PT_FIXED64 || ty == PT_FLOAT ||
	       ty == PT_DOUBLE || ty == PT_BYTES;
}

static const char *map_serde_module(const FieldInfo *fi)
{
	if (fi->map_key_type == PT_BYTES)
		return fi->map_value_type == PT_BYTES ? "::buffa::json_helpers::bytes_key_bytes_val_map" : "::buffa::json_helpers::bytes_key_map";
	if (fi->map_value_enum_closed >= 0)
		return fi->map_value_enum_closed ? "::buffa::json_helpers::map_closed_enum" : "::buffa::json_helpers::map_enum";
	bool string_key = fi->map_key_type == PT_STRING;
	if (fi->map_value_type == PT_MESSAGE)
		return string_key ? NULL : "::buffa::json_helpers::string_key_map";
	int vt = fi->map_value_type ? fi->map_value_type : PT_STRING;
	if (value_needs_proto_json(vt))
		return "::buffa::json_helpers::proto_map";
	return string_key ? NULL : "::buffa::json_helpers::string_key_map";
}

static const char *int_family(int ty)
{
	switch (ty) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return "int32";
	case PT_UINT32: case PT_FIXED32: return "uint32";
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return "int64";
	case PT_UINT64: case PT_FIXED64: return "uint64";
	case PT_FLOAT: return "float";
	case PT_DOUBLE: return "double";
	case PT_BYTES: return "bytes";
	default: return NULL;
	}
}

static const char *field_with_module(int ty, const FieldInfo *fi, Feat f)
{
	if (fi->is_map)
		return map_serde_module(fi);
	if (fi->is_repeated) {
		if (ty == PT_ENUM)
			return is_closed_enum(f) ? "::buffa::json_helpers::repeated_closed_enum" : "::buffa::json_helpers::repeated_enum";
		if (ty == PT_MESSAGE || ty == PT_GROUP)
			return NULL;
		return value_needs_proto_json(ty) ? "::buffa::json_helpers::proto_seq" : NULL;
	}
	if (fi->is_optional) {
		if (ty == PT_ENUM)
			return is_closed_enum(f) ? "::buffa::json_helpers::opt_closed_enum" : "::buffa::json_helpers::opt_enum";
		const char *k = int_family(ty);
		return k ? xcat("::buffa::json_helpers::opt_", k) : NULL;
	}
	if (ty == PT_BOOL)
		return "::buffa::json_helpers::proto_bool";
	if (ty == PT_STRING)
		return "::buffa::json_helpers::proto_string";
	if (ty == PT_ENUM)
		return is_closed_enum(f) ? "::buffa::json_helpers::closed_enum" : "::buffa::json_helpers::proto_enum";
	const char *k = int_family(ty);
	return k ? xcat("::buffa::json_helpers::", k) : NULL;
}

static const char *skip_predicate(int ty, const FieldInfo *fi, Feat f)
{
	if (fi->is_required)
		return NULL;
	if (fi->is_map)
		return "::buffa::__private::HashMap::is_empty";
	if (fi->is_repeated)
		return "::buffa::json_helpers::skip_if::is_empty_vec";
	if (fi->is_optional)
		return "::core::option::Option::is_none";
	switch (ty) {
	case PT_MESSAGE: case PT_GROUP: return "::buffa::json_helpers::skip_if::is_unset_message_field";
	case PT_ENUM:
		return is_closed_enum(f) ? "::buffa::json_helpers::skip_if::is_default_closed_enum" : "::buffa::json_helpers::skip_if::is_default_enum_value";
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return "::buffa::json_helpers::skip_if::is_zero_i64";
	case PT_UINT64: case PT_FIXED64: return "::buffa::json_helpers::skip_if::is_zero_u64";
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return "::buffa::json_helpers::skip_if::is_zero_i32";
	case PT_UINT32: case PT_FIXED32: return "::buffa::json_helpers::skip_if::is_zero_u32";
	case PT_BOOL: return "::buffa::json_helpers::skip_if::is_false";
	case PT_FLOAT: return "::buffa::json_helpers::skip_if::is_zero_f32";
	case PT_DOUBLE: return "::buffa::json_helpers::skip_if::is_zero_f64";
	case PT_STRING: return "::buffa::json_helpers::skip_if::is_empty_str";
	default: return "::buffa::json_helpers::skip_if::is_empty_bytes";
	}
}

static bool is_value_field(const PbMsg *field, const FieldInfo *fi)
{
	return fd_type(field) == PT_MESSAGE && !fi->is_repeated && !fi->is_map && fd_type_name(field) &&
	       str_eq(fd_type_name(field), ".google.protobuf.Value");
}

static const char *json_name_of(const PbMsg *field, const char *name) { return pb_has(field, FD_JSON_NAME) ? pb_str(field, FD_JSON_NAME) : name; }
static char *q(const char *s) { return pm2_string(str_c(s)); }

static void serde_field_attr(Vec *attrs, const PbMsg *field, const char *name, const FieldInfo *fi, Feat features)
{
	int ty = effective_type(field, features);
	Feat ff = feat_resolve_field(field, features);
	const char *json = json_name_of(field, name);
	const char *with = field_with_module(ty, fi, ff);
	const char *null_deser = !with && (fi->is_repeated || fi->is_map) ? "::buffa::json_helpers::null_as_default" : NULL;
	const char *skip = skip_predicate(ty, fi, ff);
	Buf b = {0};
	buf_adds(&b, "serde(rename = ");
	buf_adds(&b, q(json));
	if (!str_eq(json, name))
		buf_adds(&b, xcat(", alias = ", q(name)));
	if (with)
		buf_adds(&b, xcat(", with = ", q(with)));
	if (skip)
		buf_adds(&b, xcat(", skip_serializing_if = ", q(skip)));
	if (is_value_field(field, fi))
		buf_adds(&b, ", deserialize_with = \"::buffa::json_helpers::message_field_always_present\"");
	else if (null_deser)
		buf_adds(&b, xcat(", deserialize_with = ", q(null_deser)));
	buf_addc(&b, ')');
	cfg_attr(attrs, buf_str(&b), gates().json);
}

/* fn deserialize<D: ::serde::Deserializer<'de>>(self?, d: D) -> Result<ret, D::Error> */
static PpNode *deser_sig(PpNode *ret, bool self_)
{
	PpNode *sig = sig_generic("deserialize", ty_result(ret, pp_ty("D::Error")), pp_generics());
	pp_push(pp_generics_of(sig), pp_type_param("D", pp_bounds(pp_trait_bound(pp_path_args(pp_path("::serde::Deserializer"), pp_angle(pp_lifetime("'de"), NULL)), false), NULL), NULL));
	if (self_)
		pp_push(sig, pp_receiver(false, false, NULL));
	pp_push(sig, arg("d", pp_ty("D")));
	return sig;
}

static PpNode *impl_de(const char *trait, PpNode *self_ty)
{
	PpNode *imp = pp_item_impl(pp_path_args(pp_path(trait), pp_angle(pp_lifetime("'de"), NULL)), self_ty);
	pp_set_generics(imp, generics_tp("'de", NULL, NULL));
	return imp;
}

/* deser_seed_expr: { struct _S; impl DeserializeSeed for _S {...} map.next_value_seed(_S)? } */
static PpNode *deser_seed_expr(PpNode *rust_type, PpNode *inner)
{
	PpNode *imp = impl_de("::serde::de::DeserializeSeed", pp_ty("_S"));
	pp_push(imp, pp_impl_type(0, "Value", rust_type));
	pp_push(imp, pp_item_fn(0, deser_sig(rust_type, true), pp_block(s_expr(inner), NULL)));
	return pp_e_block(pp_block(pp_s_item(pp_item_struct(0, "_S", 0)), pp_s_item(imp),
				   s_expr(call_try(e_meth1(pp_e("map"), "next_value_seed", pp_e("_S")))), NULL));
}

static PpNode *arm_pat_names(const char *json, const char *name)
{
	if (str_eq(json, name))
		return pp_p_lit(lit_str(json));
	return pp_p_or(pp_p_lit(lit_str(json)), pp_p_lit(lit_str(name)), NULL);
}

static PpNode *oneof_variant_deser_arm(const char *variant, PpNode *vtype, const char *json, const char *proto, int ty, bool null_forward,
				       bool boxed, const char *qenum, const char *result_var, const char *oneof_name)
{
	const char *dup = xcat("multiple oneof fields set for '", oneof_name, "'");
	PpNode *wrapped = boxed ? e_call1("::buffa::alloc::boxed::Box::new", pp_e("v")) : pp_e("v");
	PpNode *b = pp_block(NULL);
	PpNode *dseed = pp_e_call(pp_e_path(pp_path_push(pp_path_args(pp_path("::buffa::json_helpers::DefaultDeserializeSeed"), pp_angle(vtype, NULL)), "new", NULL)), NULL);
	PpNode *dup_ret = pp_block(s_semi(pp_e_return(e_call1("Err", e_call1("::serde::de::Error::custom", lit_str(dup))))), NULL);
	PpNode *set = s_semi(pp_e_assign(pp_e(result_var), e_call1("Some", e_call1(xcat(qenum, "::", variant), wrapped))));
	if (null_forward) {
		pp_push(b, pp_s_local(pp_p_type(pp_p_ident("v", false, false), vtype), call_try(e_meth1(pp_e("map"), "next_value_seed", dseed))));
		pp_push(b, stmt_if(e_meth0(pp_e(result_var), "is_some"), dup_ret));
		pp_push(b, set);
	} else {
		const char *helper = int_family(ty);
		PpNode *seed;
		if (helper) {
			PpNode *imp = impl_de("::serde::de::DeserializeSeed", pp_ty("_DeserSeed"));
			pp_push(imp, pp_impl_type(0, "Value", vtype));
			pp_push(imp, pp_item_fn(0, deser_sig(vtype, true), pp_block(s_expr(e_call1(xcat("::buffa::json_helpers::", helper, "::deserialize"), pp_e("d"))), NULL)));
			pp_push(b, pp_s_item(pp_item_struct(0, "_DeserSeed", 0)));
			pp_push(b, pp_s_item(imp));
			seed = e_call1("::buffa::json_helpers::NullableDeserializeSeed", pp_e("_DeserSeed"));
		} else
			seed = e_call1("::buffa::json_helpers::NullableDeserializeSeed", dseed);
		pp_push(b, pp_s_local(pp_p_type(pp_p_ident("v", false, false), ty_g1(OPTION_PATH, vtype)), call_try(e_meth1(pp_e("map"), "next_value_seed", seed))));
		pp_push(b, stmt_if(pp_e_let(p_some(pp_p_ident("v", false, false)), pp_e("v")),
				   pp_block(stmt_if(e_meth0(pp_e(result_var), "is_some"), dup_ret), set, NULL)));
	}
	return pp_arm(arm_pat_names(json, proto), NULL, pp_e_block(b));
}

static void generate_custom_deserialize(Vec *out, const Scope *s, const PbMsg *msg, const char *name, const char *oneof_prefix,
					bool has_ext_ranges, const char **oneof_idents)
{
	Vec vars = {0}, arms = {0}, inits = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		const char *fname = field_name_req(f);
		const char *json = json_name_of(f, fname);
		const char *var = xcat("__f_", ctx_field_rust_name(fname, fd_number(f)));
		const char *fid = ctx_field_ident(fname, fd_number(f));
		FieldInfo fi = classify_field(s, msg, f);
		Feat ff = feat_resolve_field(f, s->features);
		int ty = effective_type(f, s->features);
		const char *with = field_with_module(ty, &fi, ff);
		bool null_deser = !with && (fi.is_repeated || fi.is_map);
		PpNode *de;
		if (is_value_field(f, &fi))
			de = deser_seed_expr(fi.rust_type, e_call1("::buffa::json_helpers::message_field_always_present", pp_e("d")));
		else if (with)
			de = deser_seed_expr(fi.rust_type, e_call1(xcat(with, "::deserialize"), pp_e("d")));
		else if (null_deser)
			de = deser_seed_expr(fi.rust_type, e_call1("::buffa::json_helpers::null_as_default", pp_e("d")));
		else
			de = call_try(pp_e_method_tf(pp_e("map"), "next_value", pp_angle(fi.rust_type, NULL), NULL));
		vec_push(&arms, pp_arm(arm_pat_names(json, fname), NULL, pp_e_block(pp_block(s_semi(pp_e_assign(pp_e(var), e_call1("Some", de))), NULL))));
		vec_push(&vars, pp_s_local(pp_p_type(pp_p_ident(var, false, true), ty_g1(OPTION_PATH, fi.rust_type)), pp_e("None")));
		vec_push(&inits, stmt_if(pp_e_let(p_core_some(pp_p_ident("v", false, false)), pp_e(var)),
					 pp_block(s_semi(pp_e_assign(pp_e_field(pp_e("__r"), fid), pp_e("v"))), NULL)));
	}
	int no = pb_count(msg, DP_ONEOF_DECL);
	for (int idx = 0; idx < no; idx++) {
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		const char *oneof_name = dname(o);
		if (!oneof_idents[idx])
			continue;
		const char *var = xcat("__oneof_", ctx_oneof_rust_name(oneof_name));
		const char *qenum = xcat(oneof_prefix, oneof_idents[idx]);
		vec_push(&vars, pp_s_local(pp_p_type(pp_p_ident(var, false, true), ty_g1(OPTION_PATH, pp_ty(qenum))), pp_e("None")));
		for (int i = 0; i < n_fields(msg); i++) {
			const PbMsg *f = field_at(msg, i);
			if (!is_real_oneof_member(f) || fd_oneof(f) != idx)
				continue;
			const char *pname = field_name_req(f);
			int ty = effective_type(f, s->features);
			PpNode *vt = scalar_or_message_type(f, s->pkg, s->nesting, s->features);
			bool null_fwd = fd_type_name(f) && (str_eq(fd_type_name(f), ".google.protobuf.NullValue") || str_eq(fd_type_name(f), ".google.protobuf.Value"));
			vec_push(&arms, oneof_variant_deser_arm(oneof_variant_ident(pname), vt, json_name_of(f, pname), pname, ty, null_fwd,
								variant_boxed(ty, xcat(".", s->fqn, ".", oneof_name, ".", pname)), qenum, var, oneof_name));
		}
		vec_push(&inits, s_semi(pp_e_assign(pp_e_field(pp_e("__r"), ctx_oneof_ident(oneof_name)), pp_e(var))));
	}
	bool ext = has_ext_ranges && CFG->preserve_unknown_fields;
	if (ext) {
		vec_push(&vars, pp_s_local(pp_p_type(pp_p_ident("__ext_records", false, true), ty_g1("::buffa::alloc::vec::Vec", pp_ty("::buffa::UnknownField"))),
					   pp_e_call(pp_e("::buffa::alloc::vec::Vec::new"), NULL)));
		PpNode *m = pp_e_match(pp_e_call(pp_e("::buffa::extension_registry::deserialize_extension_key"), lit_str(s->fqn), pp_e("__k"), pp_e("__v"), NULL));
		pp_push(m, pp_arm(p_core_some(pp_p_tuple_struct(pp_path("::core::result::Result::Ok"), pp_p_ident("__recs", false, false), NULL)), NULL,
				  pp_e_block(pp_block(s_expr(pp_e_for(pp_p_ident("__rec", false, false), pp_e("__recs"),
								      pp_block(s_semi(e_meth1(pp_e("__ext_records"), "push", pp_e("__rec"))), NULL))),
						      NULL))));
		pp_push(m, pp_arm(p_core_some(pp_p_tuple_struct(pp_path("::core::result::Result::Err"), pp_p_ident("__e", false, false), NULL)), NULL,
				  pp_e_block(pp_block(s_semi(pp_e_return(e_call1("::core::result::Result::Err",
										e_call1_q(pp_ty("A::Error"), "::serde::de::Error", "custom", pp_e("__e"))))),
						      NULL))));
		pp_push(m, pp_arm(pp_p_path(pp_path("::core::option::Option::None")), NULL, pp_e_block(pp_block(NULL))));
		PpNode *ab = pp_block(pp_s_local(pp_p_type(pp_p_ident("__v", false, false), pp_ty("::buffa::serde_json::Value")), call_try(e_meth0(pp_e("map"), "next_value"))),
				      s_expr(m), NULL);
		vec_push(&arms, pp_arm(pp_p_ident("__k", false, false), e_meth1(pp_e("__k"), "starts_with", pp_e_lit(pp_lit(PP_LIT_CHAR, "'['"))), pp_e_block(ab)));
	}
	PpNode *m = pp_e_match(e_meth0(pp_e("key"), "as_str"));
	push_all(m, &arms);
	pp_push(m, pp_arm(pp_p_wild(), NULL, pp_e_block(pp_block(s_semi(call_try(pp_e_method_tf(pp_e("map"), "next_value", pp_angle(pp_ty("::serde::de::IgnoredAny"), NULL), NULL))), NULL))));
	PpNode *vb = pp_block(NULL);
	push_all(vb, &vars);
	PpNode *cond = pp_e_let(p_some(pp_p_ident("key", false, false)),
				call_try(pp_e_method_tf(pp_e("map"), "next_key", pp_angle(pp_ty("::buffa::alloc::string::String"), NULL), NULL)));
	pp_push(vb, s_expr(pp_e_while(cond, pp_block(s_expr(m), NULL))));
	pp_push(vb, s_let("__r", true, pp_e_call(e_qpath(pp_ty(name), "::core::default::Default", "default"), NULL)));
	push_all(vb, &inits);
	if (ext)
		pp_push(vb, s_expr(pp_e_for(pp_p_ident("__rec", false, false), pp_e("__ext_records"),
					    pp_block(s_semi(e_meth1(pp_e_field(pp_e("__r"), "__buffa_unknown_fields"), "push", pp_e("__rec"))), NULL))));
	pp_push(vb, s_expr(e_call1("Ok", pp_e("__r"))));

	PpNode *vis = impl_de("::serde::de::Visitor", pp_ty("_V"));
	pp_push(vis, pp_impl_type(0, "Value", pp_ty(name)));
	PpNode *esig = pp_sig("expecting", pp_ty("::core::fmt::Result"));
	pp_push(esig, pp_receiver(true, false, NULL));
	pp_push(esig, arg("f", ty_ref_mut(pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
	pp_push(vis, pp_item_fn(0, esig, pp_block(s_expr(e_meth1(pp_e("f"), "write_str", lit_str(xcat("struct ", name)))), NULL)));
	PpNode *msig = sig_generic("visit_map", ty_result(pp_ty(name), pp_ty("A::Error")), pp_generics());
	pp_push(pp_generics_of(msig), pp_type_param("A", pp_bounds(pp_trait_bound(pp_path_args(pp_path("::serde::de::MapAccess"), pp_angle(pp_lifetime("'de"), NULL)), false), NULL), NULL));
	pp_push(msig, pp_receiver(false, false, NULL));
	pp_push(msig, pp_p_type(pp_p_ident("map", false, true), pp_ty("A")));
	PpNode *mfn = pp_item_fn(0, msig, vb);
	pp_push_attr(mfn, attr_src("allow", "clippy::field_reassign_with_default"));
	pp_push(vis, mfn);
	PpNode *imp = impl_de("::serde::Deserialize", pp_ty(name));
	Vec ns = {0};
	ctx_message_non_snake_attr(msg, &ns);
	attrs_all(imp, &ns);
	pp_push(imp, pp_item_fn(0, deser_sig(pp_ty("Self"), false),
				pp_block(pp_s_item(pp_item_struct(0, "_V", 0)), pp_s_item(vis), s_expr(e_meth1(pp_e("d"), "deserialize_map", pp_e("_V"))), NULL)));
	vec_push(out, imp);
}

static void ext_json_wrapper(Vec *out, const char *ident, const char *fqn)
{
	PpNode *st = pp_item_struct(PP_VIS_PUB, ident, PP_F_UNNAMED);
	pp_push(st, pp_field(PP_VIS_PUB, NULL, pp_ty("::buffa::UnknownFields")));
	pp_push_attr(st, attr_src("doc", "hidden"));
	pp_push_attr(st, attr_src("derive", "Clone, Debug, Default, PartialEq"));
	pp_push_attr(st, attr_src("repr", "transparent"));
	if (CFG->generate_arbitrary)
		pp_push_attr(st, attr_src("cfg_attr", "feature = \"arbitrary\", derive(::arbitrary::Arbitrary)"));
	vec_push(out, st);
	PpNode *uf = pp_ty("::buffa::UnknownFields");
	PpNode *imp = pp_item_impl(pp_path("::core::ops::Deref"), pp_ty(ident));
	pp_push(imp, pp_impl_type(0, "Target", uf));
	pp_push(imp, fn_self("deref", false, ty_ref(uf), pp_block(s_expr(pp_e_ref(false, pp_e_field(pp_e("self"), "0"))), NULL)));
	vec_push(out, imp);
	imp = pp_item_impl(pp_path("::core::ops::DerefMut"), pp_ty(ident));
	pp_push(imp, fn_self("deref_mut", true, ty_ref_mut(uf), pp_block(s_expr(pp_e_ref(true, pp_e_field(pp_e("self"), "0"))), NULL)));
	vec_push(out, imp);
	imp = pp_item_impl(pp_path_args(pp_path("::core::convert::From"), pp_angle(uf, NULL)), pp_ty(ident));
	PpNode *sig = pp_sig("from", pp_ty("Self"));
	pp_push(sig, arg("u", uf));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call1("Self", pp_e("u"))), NULL)));
	vec_push(out, imp);
	FeatureGates g = gates();
	imp = pp_item_impl(pp_path("::serde::Serialize"), pp_ty(ident));
	sig = sig_generic("serialize", ty_result(pp_ty("S::Ok"), pp_ty("S::Error")), generics_tp(NULL, "S", "::serde::Serializer"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("s", pp_ty("S")));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call3("::buffa::extension_registry::serialize_extensions", lit_str(fqn), pp_e_ref(false, pp_e_field(pp_e("self"), "0")), pp_e("s"))), NULL)));
	Vec one_ = one(imp);
	cfg_block(out, &one_, g.json);
	imp = impl_de("::serde::Deserialize", pp_ty(ident));
	pp_push(imp, pp_item_fn(0, deser_sig(pp_ty("Self"), false),
				pp_block(s_expr(e_meth1(e_call2("::buffa::extension_registry::deserialize_extensions", lit_str(fqn), pp_e("d")), "map", pp_e("Self"))), NULL)));
	one_ = one(imp);
	cfg_block(out, &one_, g.json);
}

static void generate_oneof_serialize(Vec *out, const char *enum_ident, const VariantInfo *v, int n)
{
	PpNode *m = pp_e_match(pp_e("self"));
	for (int i = 0; i < n; i++) {
		PpNode *val, *pat;
		const char *p = xcat("Self::", v[i].variant);
		if (v[i].is_null_value) {
			pat = pp_p_tuple_struct(pp_path(p), pp_p_wild(), NULL);
			val = pp_e_ref(false, pp_e_tuple(NULL));
		} else {
			pat = pp_p_tuple_struct(pp_path(p), pp_p_ident("v", false, false), NULL);
			if (v[i].is_boxed)
				val = pp_e_ref(false, pp_e_unary("*", pp_e_unary("*", pp_e("v"))));
			else if (int_family(v[i].field_type))
				val = pp_e_ref(false, e_call1("::buffa::json_helpers::ProtoJson", pp_e("v")));
			else
				val = pp_e("v");
		}
		pp_push(m, pp_arm(pat, NULL, pp_e_block(pp_block(s_semi(call_try(pp_e_method(pp_e("map"), "serialize_entry", lit_str(v[i].json_name), val, NULL))), NULL))));
	}
	PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), pp_ty(enum_ident));
	PpNode *sig = sig_generic("serialize", ty_result(pp_ty("S::Ok"), pp_ty("S::Error")), generics_tp(NULL, "S", "::serde::Serializer"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("s", pp_ty("S")));
	pp_push(imp, pp_item_fn(0, sig,
				pp_block(pp_s_item(pp_item_use(0, pp_use_tree("serde::ser::SerializeMap"), true)),
					 s_let("map", true, call_try(e_meth1(pp_e("s"), "serialize_map", e_call1("Some", pp_e_lit_int("1"))))), s_expr(m),
					 s_expr(e_meth0(pp_e("map"), "end")), NULL)));
	vec_push(out, imp);
}

static void view_serialize_owned_wrapper(Vec *out, const char *wrapper)
{
	PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), pp_ty(wrapper));
	PpNode *sig = sig_generic("serialize", ty_result(pp_ty("__S::Ok"), pp_ty("__S::Error")), generics_tp(NULL, "__S", "::serde::Serializer"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("__s", pp_ty("__S")));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call2("::serde::Serialize::serialize", pp_e_ref(false, pp_e_field(pp_e("self"), "0")), pp_e("__s"))), NULL)));
	Vec one_ = one(imp);
	cfg_block(out, &one_, gates().json);
}

static PpNode *ser_entry(const char *map, const char *json, PpNode *val)
{
	return s_semi(call_try(pp_e_method(pp_e(map), "serialize_entry", lit_str(json), val, NULL)));
}

static PpNode *json_wrap(const char *helper, PpNode *v) { return pp_e_ref(false, e_call1(xcat("::buffa::json_helpers::", helper), v)); }

static PpNode *generics_lts(const char *a, const char *b)
{
	PpNode *g = pp_generics();
	if (a)
		pp_push(g, pp_lifetime_param(a));
	if (b)
		pp_push(g, pp_lifetime_param(b));
	return g;
}

static PpNode *view_elem_ty(const Scope *s, const PbMsg *fd, int raw, bool value)
{
	if (raw == PT_STRING)
		return ref_lt("'__a", pp_ty("str"));
	if (raw == PT_BYTES)
		return ref_lt("'__a", pp_ty_slice(pp_ty("u8")));
	if (value && raw == PT_MESSAGE)
		return ty_lt(resolve_view_path(s, fd), "'__a");
	if (value && raw == PT_ENUM)
		return enum_or_value(resolve_enum_ty(s, fd), is_closed_enum(feat_resolve_field(fd, s->features)));
	return view_scalar_ty(raw);
}

static PpNode *if_some_v(const char *id, bool ref, PpNode *stmt)
{
	return stmt_if(pp_e_let(p_core_some(pp_p_ident("__v", ref, false)), self_f(id)), pp_block(stmt, NULL));
}

static PpNode *view_field_serialize_stmt(const Scope *s, const PbMsg *msg, const PbMsg *field)
{
	Feat pf = s->features, ff = feat_resolve_field(field, pf);
	const char *name = field_name_req(field);
	const char *json = json_name_of(field, name);
	const char *id = ctx_field_ident(name, fd_number(field));
	bool rep = fd_label(field) == LABEL_REPEATED;
	bool req = is_required_field(field, pf);
	int ty = effective_type(field, pf);
	if (rep && is_map_field(msg, field)) {
		const PbMsg *kf, *vf;
		find_map_entry_fields(msg, field, &kf, &vf);
		int kr = effective_type_in_map_entry(kf, pf), vr = effective_type_in_map_entry(vf, pf);
		Feat vff = feat_resolve_field(vf, pf);
		PpNode *kt = view_elem_ty(s, kf, kr, false), *vt = view_elem_ty(s, vf, vr, true);
		PpNode *kexpr = kr == PT_STRING ? pp_e("k") : kr == PT_BYTES ? json_wrap("BytesJson", pp_e("k")) : json_wrap("MapKeyJson", pp_e("k"));
		PpNode *vexpr = vr == PT_BYTES ? json_wrap("BytesJson", pp_e("v"))
				: vr == PT_ENUM && is_closed_enum(vff) ? json_wrap("ClosedEnumJson", pp_e("v"))
				: int_family(vr) ? json_wrap("ProtoJson", pp_e("v"))
								    : pp_e("v");
		bool uses_a = kr == PT_STRING || kr == PT_BYTES || vr == PT_STRING || vr == PT_BYTES || vr == PT_MESSAGE;
		PpNode *wm = pp_item_struct(0, "_WM", PP_F_UNNAMED);
		pp_set_generics(wm, uses_a ? generics_lts("'__a", "'__x") : generics_lts("'__x", NULL));
		pp_push(wm, pp_field(0, NULL, pp_ty_ref("'__x", false, pp_ty_gen("::buffa::MapView", pp_lifetime("'__x"), kt, vt, NULL))));
		PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), uses_a ? pp_ty_gen("_WM", pp_lifetime("'__a"), pp_lifetime("'_"), NULL) : ty_lt("_WM", "'_"));
		if (uses_a)
			pp_set_generics(imp, generics_lts("'__a", NULL));
		PpNode *sig = sig_generic("serialize", ty_result(pp_ty("__S::Ok"), pp_ty("__S::Error")), generics_tp(NULL, "__S", "::serde::Serializer"));
		pp_push(sig, pp_receiver(true, false, NULL));
		pp_push(sig, arg("__s", pp_ty("__S")));
		PpNode *body = pp_block(pp_s_item(pp_item_use(0, pp_use_tree("serde::ser::SerializeMap as _"), true)),
					s_let("__m", true, call_try(e_meth1(pp_e("__s"), "serialize_map", e_some(e_meth0(pp_e_field(pp_e("self"), "0"), "len"))))),
					s_expr(pp_e_for(pp_p_tuple(pp_p_ident("k", false, false), pp_p_ident("v", false, false), NULL),
							e_meth0(pp_e_field(pp_e("self"), "0"), "iter_unique"),
							pp_block(s_semi(call_try(pp_e_method(pp_e("__m"), "serialize_entry", kexpr, vexpr, NULL))), NULL))),
					s_expr(e_meth0(pp_e("__m"), "end")), NULL);
		pp_push(imp, pp_item_fn(0, sig, body));
		return stmt_if(not(e_meth0(self_f(id), "is_empty")),
			       pp_block(pp_s_item(wm), pp_s_item(imp), ser_entry("__map", json, pp_e_ref(false, e_call1("_WM", pp_e_ref(false, self_f(id))))), NULL));
	}
	if (rep) {
		const char *adapter = ty == PT_BYTES ? "BytesSeqJson"
				      : ty == PT_ENUM ? (is_closed_enum(ff) ? "ClosedEnumSeqJson" : "EnumSeqJson")
				      : int_family(ty) ? "RepeatedJson"
							: NULL;
		PpNode *val = adapter ? json_wrap(adapter, pp_e_ref(false, self_f(id))) : pp_e_ref(false, pp_e_unary("*", self_f(id)));
		return stmt_if(not(e_meth0(self_f(id), "is_empty")), pp_block(ser_entry("__map", json, val), NULL));
	}
	if (is_explicit_presence_scalar(field, ty, ff)) {
		if (ty == PT_STRING)
			return if_some_v(id, false, ser_entry("__map", json, pp_e("__v")));
		if (ty == PT_BYTES)
			return if_some_v(id, false, ser_entry("__map", json, json_wrap("BytesJson", pp_e("__v"))));
		if (ty == PT_ENUM && is_closed_enum(ff))
			return if_some_v(id, false, ser_entry("__map", json, json_wrap("ClosedEnumJson", pp_e_ref(false, pp_e("__v")))));
		if (ty == PT_ENUM)
			return if_some_v(id, true, ser_entry("__map", json, pp_e("__v")));
		if (int_family(ty))
			return if_some_v(id, false, ser_entry("__map", json, json_wrap("ProtoJson", pp_e_ref(false, pp_e("__v")))));
		return if_some_v(id, false, ser_entry("__map", json, pp_e_ref(false, pp_e("__v"))));
	}
	PpNode *cond, *stmt;
	PpNode *ref_self = pp_e_ref(false, self_f(id));
	switch (ty) {
	case PT_STRING:
		cond = not(e_call1("::buffa::json_helpers::skip_if::is_empty_str", self_f(id)));
		stmt = ser_entry("__map", json, self_f(id));
		break;
	case PT_BYTES:
		cond = not(e_call1("::buffa::json_helpers::skip_if::is_empty_bytes", self_f(id)));
		stmt = ser_entry("__map", json, json_wrap("BytesJson", self_f(id)));
		break;
	case PT_MESSAGE: case PT_GROUP:
		cond = NULL;
		stmt = stmt_if(pp_e_let(p_core_some(pp_p_ident("__v", false, false)), e_meth0(self_f(id), "as_option")),
			       pp_block(ser_entry("__map", json, pp_e("__v")), NULL));
		break;
	case PT_ENUM:
		if (is_closed_enum(ff)) {
			cond = not(e_call1("::buffa::json_helpers::skip_if::is_default_closed_enum", ref_self));
			stmt = ser_entry("__map", json, json_wrap("ClosedEnumJson", pp_e_ref(false, self_f(id))));
		} else {
			cond = not(e_call1("::buffa::json_helpers::skip_if::is_default_enum_value", ref_self));
			stmt = ser_entry("__map", json, pp_e_ref(false, self_f(id)));
		}
		break;
	case PT_BOOL:
		cond = self_f(id);
		stmt = ser_entry("__map", json, ref_self);
		break;
	default: {
		static const char *const skip[19] = {"", "is_zero_f64", "is_zero_f32", "is_zero_i64", "is_zero_u64", "is_zero_i32", "is_zero_u64",
						     "is_zero_u32", "", "", "", "", "", "is_zero_u32", "", "is_zero_i32", "is_zero_i64", "is_zero_i32", "is_zero_i64"};
		cond = not(e_call1(xcat("::buffa::json_helpers::skip_if::", skip[ty]), ref_self));
		stmt = ser_entry("__map", json, json_wrap("ProtoJson", pp_e_ref(false, self_f(id))));
	}
	}
	if (ty == PT_MESSAGE || ty == PT_GROUP || req)
		return s_expr(pp_e_block(pp_block(stmt, NULL)));
	return stmt_if(cond, pp_block(stmt, NULL));
}

static PpNode *view_oneof_serialize_arm(const Scope *s, const PbMsg *field, const char *view_enum)
{
	Feat ff = feat_resolve_field(field, s->features);
	const char *name = field_name_req(field);
	const char *json = json_name_of(field, name);
	const char *variant = oneof_variant_ident(name);
	int ty = effective_type(field, s->features);
	const char *p = xcat(view_enum, "::", variant);
	if (fd_type_name(field) && str_eq(fd_type_name(field), ".google.protobuf.NullValue"))
		return pp_arm(pp_p_tuple_struct(pp_path(p), pp_p_wild(), NULL), NULL, pp_e_block(pp_block(ser_entry("__map", json, pp_e_ref(false, pp_e_tuple(NULL))), NULL)));
	PpNode *val;
	if (ty == PT_BYTES)
		val = json_wrap("BytesJson", pp_e("v"));
	else if (ty == PT_ENUM && is_closed_enum(ff))
		val = json_wrap("ClosedEnumJson", pp_e("v"));
	else if (ty != PT_ENUM && int_family(ty))
		val = json_wrap("ProtoJson", pp_e("v"));
	else
		val = pp_e("v");
	return pp_arm(pp_p_tuple_struct(pp_path(p), pp_p_ident("v", false, false), NULL), NULL, pp_e_block(pp_block(ser_entry("__map", json, val), NULL)));
}

static void generate_view_serialize(Vec *out, const Scope *vs, const PbMsg *msg, const char *view_ident, const char *view_oneof_prefix,
				    const char **oneof_idents)
{
	PpNode *body = pp_block(pp_s_item(pp_item_use(0, pp_use_tree("serde::ser::SerializeMap as _"), true)),
				s_let("__map", true, call_try(e_meth1(pp_e("__s"), "serialize_map", e_none()))), NULL);
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f) || !is_supported_field_type(fd_type(f)))
			continue;
		pp_push(body, view_field_serialize_stmt(vs, msg, f));
	}
	for (int idx = 0; idx < pb_count(msg, DP_ONEOF_DECL); idx++) {
		if (!oneof_idents[idx])
			continue;
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		Vec fields = oneof_members(msg, idx);
		if (!fields.n)
			continue;
		PpNode *m = pp_e_match(pp_e("__ov"));
		for (int k = 0; k < fields.n; k++)
			pp_push(m, view_oneof_serialize_arm(vs, fields.v[k], xcat(view_oneof_prefix, oneof_idents[idx])));
		pp_push(body, stmt_if(pp_e_let(p_core_some(pp_p_ident("__ov", true, false)), self_f(ctx_oneof_ident(dname(o)))), pp_block(s_expr(m), NULL)));
	}
	pp_push(body, s_expr(e_meth0(pp_e("__map"), "end")));
	PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), ty_lt(view_ident, "'__a"));
	pp_set_generics(imp, generics_lts("'__a", NULL));
	PpNode *sig = sig_generic("serialize", ty_result(pp_ty("__S::Ok"), pp_ty("__S::Error")), generics_tp(NULL, "__S", "::serde::Serializer"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("__s", pp_ty("__S")));
	pp_push(imp, pp_item_fn(0, sig, body));
	static const char *const doc[] = {" Serializes this view as protobuf JSON.", "",
					  " Implicit-presence fields with default values are omitted, `required`",
					  " fields are always emitted, explicit-presence (`optional`) fields are",
					  " emitted only when set, bytes fields are base64-encoded, and enum", " values are their proto name strings.", "",
					  " This impl uses `serialize_map(None)` because the number of emitted",
					  " fields depends on default-omission rules; serializers that require",
					  " known map lengths (e.g. `bincode`) will return a runtime error.", " Use the owned message type for those formats.", NULL};
	for (int i = 0; doc[i]; i++)
		pp_push_attr(imp, doc_attr(doc[i]));
	vec_push(out, imp);
}
