/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/impl_text.rs: `impl ::buffa::text::TextFormat` (text=true):
 * encode_text and merge_text for scalar, repeated, oneof and map fields, extensions and unknown
 * fields. Custom owned representations are not modeled (the defaults apply). */

static const char *text_kind(int ty)
{
	switch (ty) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return "i32";
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return "i64";
	case PT_UINT32: case PT_FIXED32: return "u32";
	case PT_UINT64: case PT_FIXED64: return "u64";
	case PT_FLOAT: return "f32";
	case PT_DOUBLE: return "f64";
	case PT_BOOL: return "bool";
	case PT_STRING: return "string";
	case PT_BYTES: return "bytes";
	default: return "message";
	}
}

static PpNode *enc_call(const char *m, PpNode *v) { return s_semi(call_try(e_meth1(pp_e("enc"), m, v))); }
static PpNode *write_call(int ty, PpNode *val) { return enc_call(xcat("write_", text_kind(ty)), val); }
static PpNode *read_call(int ty, const char *dec) { return call_try(e_meth0(pp_e(dec), xcat("read_", text_kind(ty)))); }
static bool is_copy_scalar(int ty) { return ty != PT_STRING && ty != PT_BYTES && ty != PT_MESSAGE && ty != PT_GROUP; }

static const char *text_field_name(const char *proto_name, const PbMsg *field, int ty, PpNode **pat)
{
	if (ty == PT_GROUP && fd_type_name(field)) {
		const char *tn = fd_type_name(field);
		const char *dot = strrchr(tn, '.');
		const char *simple = dot ? dot + 1 : tn;
		char *low = xstrdup(simple);
		for (char *p = low; *p; p++)
			*p = lower(*p);
		if (!str_eq(simple, proto_name) && str_eq(low, proto_name)) {
			if (pat)
				*pat = pp_p_or(pp_p_lit(lit_str(simple)), pp_p_lit(lit_str(proto_name)), NULL);
			return simple;
		}
	}
	if (pat)
		*pat = pp_p_lit(lit_str(proto_name));
	return proto_name;
}

static void enum_write(PpNode *b, bool closed, PpNode *val)
{
	if (closed) {
		pp_push(b, enc_call("write_enum_name", e_meth0(val, "proto_name")));
		return;
	}
	PpNode *m = pp_e_match(val);
	pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::buffa::EnumValue::Known"), pp_p_ident("__e", false, false), NULL), NULL,
			  call_try(e_meth1(pp_e("enc"), "write_enum_name", e_meth0(pp_e("__e"), "proto_name")))));
	pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::buffa::EnumValue::Unknown"), pp_p_ident("__n", false, false), NULL), NULL,
			  call_try(e_meth1(pp_e("enc"), "write_enum_number", pp_e_unary("*", pp_e("__n"))))));
	pp_push(b, s_expr(m));
}

static PpNode *enum_type_path(const PbMsg *field, const char *pkg, int nesting)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	const char *p = ctx_rust_type_relative(tn, pkg, nesting);
	if (!p)
		fail("%s", CODEGEN_ERROR("enum type '%s' not found", tn));
	return ty_path_str(p);
}

static PpNode *enum_read(bool closed, PpNode *enum_ty, const char *dec)
{
	if (closed)
		return pp_e_method_tf(pp_e(dec), "read_closed_enum_by_name", pp_angle(enum_ty, NULL), NULL);
	return e_meth1(pp_e_method_tf(pp_e(dec), "read_enum_by_name", pp_angle(enum_ty, NULL), NULL), "map", pp_e("::buffa::EnumValue::from"));
}

static PpNode *write_name(const char *name) { return enc_call("write_field_name", lit_str(name)); }

static PpNode *text_scalar_encode(const PbMsg *field, Feat parent)
{
	Feat f = feat_resolve_field(field, parent);
	const char *pn = field_name_req(field);
	const char *id = ctx_field_ident(pn, fd_number(field));
	int ty = effective_type(field, f);
	const char *name = text_field_name(pn, field, ty, NULL);
	bool req = is_required_field(field, f);
	if (is_explicit_presence_scalar(field, ty, f)) {
		PpNode *b = pp_block(write_name(name), NULL);
		if (ty == PT_ENUM)
			enum_write(b, is_closed_enum(f), pp_e("__v"));
		else
			pp_push(b, write_call(ty, is_copy_scalar(ty) ? pp_e_unary("*", pp_e("__v")) : pp_e("__v")));
		return stmt_if(pp_e_let(p_core_some(pp_p_ident("__v", true, false)), self_f(id)), b);
	}
	if (ty == PT_MESSAGE || ty == PT_GROUP)
		return stmt_if(e_meth0(self_f(id), "is_set"),
			       pp_block(write_name(name), enc_call("write_message", pp_e_ref(false, pp_e_unary("*", self_f(id)))), NULL));
	PpNode *b = pp_block(write_name(name), NULL);
	PpNode *cond;
	if (ty == PT_ENUM) {
		bool closed = is_closed_enum(f);
		enum_write(b, closed, closed ? self_f(id) : pp_e_ref(false, self_f(id)));
		cond = pp_e_binary(e_meth0(self_f(id), "to_i32"), "!=", pp_e_lit_int("0"));
	} else if (ty == PT_STRING || ty == PT_BYTES) {
		pp_push(b, write_call(ty, pp_e_ref(false, self_f(id))));
		cond = not(e_meth0(self_f(id), "is_empty"));
	} else {
		pp_push(b, write_call(ty, self_f(id)));
		cond = is_non_default_expr(ty, id);
	}
	if (req)
		return s_expr(pp_e_block(b)); /* spliced: replaced by its statements below */
	return stmt_if(cond, b);
}

static PpNode *some_core(PpNode *e) { return e_some(e); }

static PpNode *text_scalar_merge(const PbMsg *field, const char *pkg, Feat parent, int nesting)
{
	Feat f = feat_resolve_field(field, parent);
	const char *pn = field_name_req(field);
	const char *id = ctx_field_ident(pn, fd_number(field));
	int ty = effective_type(field, f);
	PpNode *pat;
	text_field_name(pn, field, ty, &pat);
	bool explicit_ = is_explicit_presence_scalar(field, ty, f);
	if (ty == PT_MESSAGE || ty == PT_GROUP)
		return pp_arm(pat, NULL, call_try(e_meth1(pp_e("dec"), "merge_message", e_meth0(self_f(id), "get_or_insert_default"))));
	PpNode *read;
	if (ty == PT_ENUM)
		read = call_try(enum_read(is_closed_enum(f), enum_type_path(field, pkg, nesting), "dec"));
	else if (ty == PT_STRING)
		read = e_meth0(call_try(e_meth0(pp_e("dec"), "read_string")), "into_owned");
	else
		read = read_call(ty, "dec");
	return pp_arm(pat, NULL, pp_e_assign(self_f(id), explicit_ ? some_core(read) : read));
}

static PpNode *text_repeated_encode(const PbMsg *field, Feat parent)
{
	Feat f = feat_resolve_field(field, parent);
	const char *pn = field_name_req(field);
	const char *id = ctx_field_ident(pn, fd_number(field));
	int ty = effective_type(field, f);
	const char *name = text_field_name(pn, field, ty, NULL);
	PpNode *b = pp_block(write_name(name), NULL);
	if (ty == PT_ENUM)
		enum_write(b, is_closed_enum(f), pp_e("__v"));
	else if (ty == PT_MESSAGE || ty == PT_GROUP || !is_copy_scalar(ty))
		pp_push(b, write_call(ty, pp_e("__v")));
	else
		pp_push(b, write_call(ty, pp_e_unary("*", pp_e("__v"))));
	return s_expr(pp_e_for(pp_p_ident("__v", false, false), pp_e_ref(false, self_f(id)), b));
}

static PpNode *ok_e(PpNode *e) { return e_call1("::core::result::Result::Ok", e); }

static PpNode *text_repeated_merge(const PbMsg *field, const char *pkg, Feat parent, int nesting)
{
	Feat f = feat_resolve_field(field, parent);
	const char *pn = field_name_req(field);
	const char *id = ctx_field_ident(pn, fd_number(field));
	int ty = effective_type(field, f);
	PpNode *pat;
	text_field_name(pn, field, ty, &pat);
	PpNode *elem;
	if (ty == PT_MESSAGE || ty == PT_GROUP)
		elem = pp_e_block(pp_block(s_let("__m", true, pp_e_call(pp_e("::core::default::Default::default"), NULL)),
					   s_semi(call_try(e_meth1(pp_e("__d"), "merge_message", pp_e_ref(true, pp_e("__m"))))), s_expr(ok_e(pp_e("__m"))), NULL));
	else if (ty == PT_ENUM)
		elem = enum_read(is_closed_enum(f), enum_type_path(field, pkg, nesting), "__d");
	else if (ty == PT_STRING)
		elem = ok_e(e_meth0(call_try(e_meth0(pp_e("__d"), "read_string")), "into_owned"));
	else
		elem = e_meth0(pp_e("__d"), xcat("read_", text_kind(ty)));
	PpNode *c = pp_e_closure(elem, false);
	pp_push(c, pp_p_ident("__d", false, false));
	return pp_arm(pat, NULL, call_try(pp_e_method(pp_e("dec"), "read_repeated_into", pp_e_ref(true, self_f(id)), c, NULL)));
}

static PpNode *text_oneof_encode(const char *enum_ident, const char *oneof_name, const Vec *fields, const char *prefix, const char *fqn, Feat parent)
{
	const char *fid = ctx_oneof_ident(oneof_name);
	const char *q = xcat(prefix, enum_ident);
	PpNode *m = pp_e_match(pp_e("__v"));
	for (int i = 0; i < fields->n; i++) {
		const PbMsg *field = fields->v[i];
		Feat f = feat_resolve_field(field, parent);
		const char *pn = field_name_req(field);
		const char *variant = oneof_variant_ident(pn);
		int ty = effective_type(field, f);
		const char *name = text_field_name(pn, field, ty, NULL);
		bool boxed = variant_boxed(ty, xcat(".", fqn, ".", oneof_name, ".", pn));
		PpNode *b = pp_block(write_name(name), NULL);
		if (ty == PT_ENUM)
			enum_write(b, is_closed_enum(f), pp_e("__v"));
		else if (ty == PT_MESSAGE || ty == PT_GROUP)
			pp_push(b, write_call(ty, boxed ? pp_e_ref(false, pp_e_unary("*", pp_e_unary("*", pp_e("__v")))) : pp_e("__v")));
		else
			pp_push(b, write_call(ty, is_copy_scalar(ty) ? pp_e_unary("*", pp_e("__v")) : pp_e("__v")));
		pp_push(m, pp_arm(pp_p_tuple_struct(pp_path(xcat(q, "::", variant)), pp_p_ident("__v", false, false), NULL), NULL, pp_e_block(b)));
	}
	return stmt_if(pp_e_let(p_core_some(pp_p_ident("__v", true, false)), self_f(fid)), pp_block(s_expr(m), NULL));
}

static void text_oneof_merge(Vec *out, const char *enum_ident, const char *oneof_name, const Vec *fields, const char *prefix, const char *pkg,
			     const char *fqn, Feat parent, int nesting)
{
	const char *fid = ctx_oneof_ident(oneof_name);
	const char *q = xcat(prefix, enum_ident);
	for (int i = 0; i < fields->n; i++) {
		const PbMsg *field = fields->v[i];
		Feat f = feat_resolve_field(field, parent);
		const char *pn = field_name_req(field);
		const char *variant = oneof_variant_ident(pn);
		const char *vp = xcat(q, "::", variant);
		int ty = effective_type(field, f);
		PpNode *pat;
		text_field_name(pn, field, ty, &pat);
		bool boxed = variant_boxed(ty, xcat(".", fqn, ".", oneof_name, ".", pn));
		PpNode *b;
		if (ty == PT_MESSAGE || ty == PT_GROUP) {
			PpNode *existing = boxed ? pp_e_ref(true, pp_e_unary("*", pp_e_unary("*", pp_e("__existing")))) : pp_e("__existing");
			PpNode *wrapped = boxed ? e_call1("::buffa::alloc::boxed::Box::new", pp_e("__m")) : pp_e("__m");
			PpNode *els = pp_block(s_let("__m", true, pp_e_call(pp_e("::core::default::Default::default"), NULL)),
					       s_semi(call_try(e_meth1(pp_e("dec"), "merge_message", pp_e_ref(true, pp_e("__m"))))),
					       s_semi(pp_e_assign(self_f(fid), some_core(e_call1(vp, wrapped)))), NULL);
			PpNode *cond = pp_e_let(p_core_some(pp_p_tuple_struct(pp_path(vp), pp_p_ident("__existing", true, true), NULL)), self_f(fid));
			b = pp_block(s_expr(pp_e_if(cond, pp_block(s_semi(call_try(e_meth1(pp_e("dec"), "merge_message", existing))), NULL), pp_e_block(els))), NULL);
		} else {
			PpNode *read;
			if (ty == PT_ENUM)
				read = call_try(enum_read(is_closed_enum(f), enum_type_path(field, pkg, nesting), "dec"));
			else if (ty == PT_STRING)
				read = e_meth0(call_try(e_meth0(pp_e("dec"), "read_string")), "into_owned");
			else
				read = read_call(ty, "dec");
			b = pp_block(s_semi(pp_e_assign(self_f(fid), some_core(e_call1(vp, read)))), NULL);
		}
		vec_push(out, pp_arm(pat, NULL, pp_e_block(b)));
	}
}

static PpNode *text_map_encode(const PbMsg *msg, const PbMsg *field, Feat features)
{
	const char *pn = field_name_req(field);
	const char *id = ctx_field_ident(pn, fd_number(field));
	const PbMsg *kf, *vf;
	find_map_entry_fields(msg, field, &kf, &vf);
	int kt = effective_type_in_map_entry(kf, features), vt = effective_type_in_map_entry(vf, features);
	Feat vff = feat_resolve_field(vf, features);
	PpNode *cb = pp_block(write_name("key"), write_call(kt, kt == PT_STRING ? pp_e("__k") : pp_e_unary("*", pp_e("__k"))), write_name("value"), NULL);
	if (vt == PT_ENUM)
		enum_write(cb, is_closed_enum(vff), pp_e("__v"));
	else
		pp_push(cb, write_call(vt, vt == PT_MESSAGE || vt == PT_STRING || vt == PT_BYTES ? pp_e("__v") : pp_e_unary("*", pp_e("__v"))));
	pp_push(cb, s_expr(e_ok_unit()));
	PpNode *c = pp_e_closure(pp_e_block(cb), false);
	pp_push(c, pp_p_ident("enc", false, false));
	return s_expr(pp_e_for(pp_p_tuple(pp_p_ident("__k", false, false), pp_p_ident("__v", false, false), NULL), pp_e_ref(false, self_f(id)),
			       pp_block(write_name(pn), s_semi(call_try(e_meth1(pp_e("enc"), "write_map_entry", c))), NULL)));
}

static PpNode *text_map_merge(const PbMsg *msg, const PbMsg *field, const char *pkg, Feat features, int nesting)
{
	const char *pn = field_name_req(field);
	const char *id = ctx_field_ident(pn, fd_number(field));
	const PbMsg *kf, *vf;
	find_map_entry_fields(msg, field, &kf, &vf);
	int kt = effective_type_in_map_entry(kf, features), vt = effective_type_in_map_entry(vf, features);
	Feat vff = feat_resolve_field(vf, features);
	PpNode *kr;
	switch (kt) {
	case PT_STRING: kr = e_meth0(call_try(e_meth0(pp_e("__d"), "read_string")), "into_owned"); break;
	case PT_FLOAT: case PT_DOUBLE: case PT_BYTES: case PT_ENUM: case PT_MESSAGE: case PT_GROUP:
		fail("%s", CODEGEN_ERROR("unsupported map key type %s", "TYPE"));
	default: kr = read_call(kt, "__d");
	}
	PpNode *vr;
	if (vt == PT_MESSAGE)
		vr = pp_e_block(pp_block(s_let("__m", true, pp_e_call(pp_e("::core::default::Default::default"), NULL)),
					 s_semi(call_try(e_meth1(pp_e("__d"), "merge_message", pp_e_ref(true, pp_e("__m"))))), s_expr(pp_e("__m")), NULL));
	else if (vt == PT_ENUM)
		vr = call_try(enum_read(is_closed_enum(vff), enum_type_path(vf, pkg, nesting), "__d"));
	else if (vt == PT_STRING)
		vr = e_meth0(call_try(e_meth0(pp_e("__d"), "read_string")), "into_owned");
	else if (vt == PT_GROUP)
		fail("%s", CODEGEN_ERROR("map value is a group"));
	else
		vr = read_call(vt, "__d");
	PpNode *m = pp_e_match(pp_e("__n"));
	pp_push(m, pp_arm(pp_p_lit(lit_str("key")), NULL, pp_e_assign(pp_e("__k"), some_core(kr))));
	pp_push(m, pp_arm(pp_p_lit(lit_str("value")), NULL, pp_e_assign(pp_e("__v"), some_core(vr))));
	pp_push(m, pp_arm(pp_p_wild(), NULL, call_try(e_meth0(pp_e("__d"), "skip_value"))));
	PpNode *wh = pp_e_while(pp_e_let(p_core_some(pp_p_ident("__n", false, false)), call_try(e_meth0(pp_e("__d"), "read_field_name"))), pp_block(s_expr(m), NULL));
	PpNode *inner = pp_e_closure(pp_e_block(pp_block(s_expr(wh), s_expr(e_ok_unit()), NULL)), false);
	pp_push(inner, pp_p_ident("__d", false, false));
	PpNode *outer_b = pp_block(s_let("__k", true, e_none()), s_let("__v", true, e_none()), s_semi(call_try(e_meth1(pp_e("__d"), "merge_map_entry", inner))),
				   s_expr(ok_e(pp_e_tuple(e_meth0(pp_e("__k"), "unwrap_or_default"), e_meth0(pp_e("__v"), "unwrap_or_default"), NULL))), NULL);
	PpNode *outer = pp_e_closure(pp_e_block(outer_b), false);
	pp_push(outer, pp_p_ident("__d", false, false));
	PpNode *b = pp_block(pp_s_local(pp_p_type(pp_p_ident("__pairs", false, true), ty_g1("::buffa::alloc::vec::Vec", pp_ty_infer())),
					pp_e_call(pp_e("::buffa::alloc::vec::Vec::new"), NULL)),
			     s_semi(call_try(pp_e_method(pp_e("dec"), "read_repeated_into", pp_e_ref(true, pp_e("__pairs")), outer, NULL))),
			     s_expr(pp_e_for(pp_p_tuple(pp_p_ident("__k", false, false), pp_p_ident("__v", false, false), NULL), pp_e("__pairs"),
					     pp_block(s_semi(pp_e_method(self_f(id), "insert", pp_e("__k"), pp_e("__v"), NULL)), NULL))),
			     NULL);
	return pp_arm(pp_p_lit(lit_str(pn)), NULL, pp_e_block(b));
}

static PpNode *text_format_impl(const char *name, PpNode *encode_fn, PpNode *merge_fn)
{
	PpNode *imp = pp_item_impl(pp_path("::buffa::text::TextFormat"), pp_ty(name));
	pp_push(imp, encode_fn);
	pp_push(imp, merge_fn);
	return imp;
}

static PpNode *encode_sig(const char *param)
{
	PpNode *sig = pp_sig("encode_text", pp_ty("::core::fmt::Result"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg(param, ty_ref_mut(pp_ty_gen("::buffa::text::TextEncoder", pp_lifetime("'_"), NULL))));
	return sig;
}

static PpNode *merge_sig(void)
{
	PpNode *sig = pp_sig("merge_text", ty_g2("::core::result::Result", pp_ty_tuple(NULL), pp_ty("::buffa::text::ParseError")));
	pp_push(sig, pp_receiver(true, true, NULL));
	pp_push(sig, arg("dec", ty_ref_mut(pp_ty_gen("::buffa::text::TextDecoder", pp_lifetime("'_"), NULL))));
	return sig;
}

static void generate_text_impl(Vec *out, const PbMsg *msg, const char *rust_name, const Scope *s, bool has_ext_ranges,
			       const char **oneof_idents, const char *oneof_prefix)
{
	if (!CFG->generate_text || str_eq(s->fqn, "google.protobuf.Any"))
		return;
	Feat features = s->features;
	Vec one_ = {0};
	if (pb_int(pb_msg(msg, DP_OPTIONS), MOPT_MESSAGE_SET, 0)) {
		PpNode *e = pp_item_fn(0, encode_sig("_enc"), pp_block(s_expr(e_ok_unit()), NULL));
		PpNode *wh = pp_e_while(e_meth0(call_try(e_meth0(pp_e("dec"), "read_field_name")), "is_some"),
					pp_block(s_semi(call_try(e_meth0(pp_e("dec"), "skip_value"))), NULL));
		PpNode *m = pp_item_fn(0, merge_sig(), pp_block(s_expr(wh), s_expr(e_ok_unit()), NULL));
		vec_push(&one_, text_format_impl(rust_name, e, m));
		cfg_block(out, &one_, gates().text);
		return;
	}
	Vec enc = {0}, arms = {0};
	int nf = n_fields(msg), no = pb_count(msg, DP_ONEOF_DECL);
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (!is_real_oneof_member(f) && fd_label(f) != LABEL_REPEATED && is_supported_field_type(fd_type(f))) {
			PpNode *st = text_scalar_encode(f, features);
			if (st->kind == PP_S_EXPR && st->a->kind == PP_E_BLOCK) /* required: the statements themselves */
				for (int k = 0; k < st->a->a->list.n; k++)
					vec_push(&enc, st->a->a->list.v[k]);
			else
				vec_push(&enc, st);
		}
	}
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && !is_map_field(msg, f) && is_supported_field_type(fd_type(f)))
			vec_push(&enc, text_repeated_encode(f, features));
	}
	for (int idx = 0; idx < no; idx++) {
		Vec fields = oneof_members(msg, idx);
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!oneof_idents[idx] || !fields.n || !pb_has(o, ODP_NAME))
			continue;
		vec_push(&enc, text_oneof_encode(oneof_idents[idx], dname(o), &fields, oneof_prefix, s->fqn, features));
	}
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && is_map_field(msg, f))
			vec_push(&enc, text_map_encode(msg, f, features));
	}
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (!is_real_oneof_member(f) && fd_label(f) != LABEL_REPEATED && is_supported_field_type(fd_type(f)))
			vec_push(&arms, text_scalar_merge(f, s->pkg, features, s->nesting));
	}
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && !is_map_field(msg, f) && is_supported_field_type(fd_type(f)))
			vec_push(&arms, text_repeated_merge(f, s->pkg, features, s->nesting));
	}
	for (int idx = 0; idx < no; idx++) {
		Vec fields = oneof_members(msg, idx);
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!oneof_idents[idx] || !fields.n || !pb_has(o, ODP_NAME))
			continue;
		text_oneof_merge(&arms, oneof_idents[idx], dname(o), &fields, oneof_prefix, s->pkg, s->fqn, features, s->nesting);
	}
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && is_map_field(msg, f))
			vec_push(&arms, text_map_merge(msg, f, s->pkg, features, s->nesting));
	}
	bool has_encode = enc.n || CFG->preserve_unknown_fields;
	if (CFG->preserve_unknown_fields && has_ext_ranges) {
		vec_push(&enc, s_semi(call_try(pp_e_method(pp_e("enc"), "write_extension_fields", lit_str(s->fqn), pp_e_ref(false, self_f("__buffa_unknown_fields")), NULL))));
		PpNode *loop = pp_e_for(pp_p_ident("__r", false, false), call_try(pp_e_method(pp_e("dec"), "read_extension", pp_e("__name"), lit_str(s->fqn), NULL)),
					pp_block(s_semi(e_meth1(self_f("__buffa_unknown_fields"), "push", pp_e("__r"))), NULL));
		vec_push(&arms, pp_arm(pp_p_ident("__name", false, false), e_meth1(pp_e("__name"), "starts_with", pp_e_lit(pp_lit(PP_LIT_CHAR, "'['"))),
				       pp_e_block(pp_block(s_expr(loop), NULL))));
	}
	if (CFG->preserve_unknown_fields)
		vec_push(&enc, s_semi(call_try(e_meth1(pp_e("enc"), "write_unknown_fields", pp_e_ref(false, self_f("__buffa_unknown_fields"))))));
	PpNode *eb = pp_block(allow_unused_use("::buffa::Enumeration as _"), NULL);
	push_all(eb, &enc);
	pp_push(eb, s_expr(e_ok_unit()));
	PpNode *m = pp_e_match(pp_e("__name"));
	push_all(m, &arms);
	pp_push(m, pp_arm(pp_p_wild(), NULL, call_try(e_meth0(pp_e("dec"), "skip_value"))));
	PpNode *wh = pp_e_while(pp_e_let(p_core_some(pp_p_ident("__name", false, false)), call_try(e_meth0(pp_e("dec"), "read_field_name"))),
				pp_block(s_expr(m), NULL));
	PpNode *mb = pp_block(allow_unused_use("::buffa::Enumeration as _"), s_expr(wh), s_expr(e_ok_unit()), NULL);
	vec_push(&one_, text_format_impl(rust_name, pp_item_fn(0, encode_sig(has_encode ? "enc" : "_enc"), eb), pp_item_fn(0, merge_sig(), mb)));
	cfg_block(out, &one_, gates().text);
}
