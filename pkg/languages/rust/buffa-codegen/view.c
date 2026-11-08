/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/view.rs (zero-copy view structs, MessageView decode arms,
 * to_owned conversion, oneof view enums, required-field presence) with the view encoding of
 * src/impl_message.rs (build_view_encode_methods, map_view_*) and src/owned_view.rs (the
 * `FooOwnedView` wrapper). The JSON Serialize impl of views is in json.c. */

static PpNode *lt_a(void) { return pp_lifetime("'a"); }
static PpNode *generics_a(void)
{
	PpNode *g = pp_generics();
	pp_push(g, pp_lifetime_param("'a"));
	return g;
}
static PpNode *ty_lt(const char *path, const char *lt) { return pp_ty_gen(path, pp_lifetime(lt), NULL); }
static PpNode *ref_lt(const char *lt, PpNode *ty) { return pp_ty_ref(lt, false, ty); }
static PpNode *view_scalar_ty(int ty) { return scalar_rust_type(ty, 1); }
static PpNode *view_e(const char *f) { return pp_e_field(pp_e("view"), f); }

static PpNode *resolve_enum_ty(const Scope *s, const PbMsg *field)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	const char *p = ctx_rust_type_relative(tn, s->pkg, s->nesting);
	if (!p)
		fail("%s", CODEGEN_ERROR("enum type '%s' not found", tn));
	return ty_path_str(p);
}

/* resolve_view_family_path: the path of `<Msg>View` in the target package's __buffa::view tree */
static char *resolve_view_path(const Scope *s, const PbMsg *field)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	SplitPath sp;
	if (!ctx_rust_type_relative_split(tn, s->pkg, s->nesting, &sp))
		fail("%s", CODEGEN_ERROR("message type '%s' not found", tn));
	Buf b = {0};
	if (*sp.to_package) {
		buf_adds(&b, rust_path(sp.to_package));
		buf_adds(&b, "::");
	}
	buf_adds(&b, SENTINEL_MOD "::view::");
	const char *cut = NULL;
	for (const char *p = strstr(sp.within_package, "::"); p; p = strstr(p + 2, "::"))
		cut = p;
	const char *last = sp.within_package;
	if (cut) {
		buf_adds(&b, rust_path(xstrndup(sp.within_package, (size_t)(cut - sp.within_package))));
		buf_adds(&b, "::");
		last = cut + 2;
	}
	buf_adds(&b, make_field_ident(xcat(last, "View")));
	return (char *)buf_str(&b);
}

static PpNode *mv_call2(const char *vt, const char *m, PpNode *a, PpNode *b)
{
	return pp_e_call(e_qpath(pp_ty_path(pp_path(vt)), "::buffa::MessageView", m), a, b, NULL);
}

static PpNode *mv_call3(const char *vt, const char *m, PpNode *a, PpNode *b, PpNode *c)
{
	return pp_e_call(e_qpath(pp_ty_path(pp_path(vt)), "::buffa::MessageView", m), a, b, c, NULL);
}

static PpNode *resolve_view_ty(const Scope *s, const PbMsg *field, const char *lt) { return ty_lt(resolve_view_path(s, field), lt); }

static const char *resolve_owned_path(const Scope *s, const PbMsg *field)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: field.type_name");
	const char *p = ctx_rust_type_relative(tn, s->pkg, s->nesting);
	if (!p)
		fail("%s", CODEGEN_ERROR("message type '%s' not found", tn));
	return p;
}

static PpNode *enum_or_value(PpNode *et, bool closed) { return closed ? et : ty_g1("::buffa::EnumValue", et); }

static PpNode *view_singular_type(const Scope *s, const PbMsg *field, const char *lt)
{
	Feat f = feat_resolve_field(field, s->features);
	int ty = effective_type(field, f);
	PpNode *inner;
	if (ty == PT_STRING)
		inner = ref_lt(lt, pp_ty("str"));
	else if (ty == PT_BYTES)
		inner = ref_lt(lt, pp_ty_slice(pp_ty("u8")));
	else if (ty == PT_ENUM)
		inner = enum_or_value(resolve_enum_ty(s, field), is_closed_enum(f));
	else if (ty == PT_MESSAGE || ty == PT_GROUP)
		inner = NULL;
	else
		inner = view_scalar_ty(ty);
	if (is_explicit_presence_scalar(field, ty, f))
		return ty_g1(OPTION_PATH, inner);
	if (!inner)
		return ty_g1("::buffa::MessageFieldView", resolve_view_ty(s, field, lt));
	return inner;
}

static PpNode *view_repeated_type(const Scope *s, const PbMsg *field, const char *lt)
{
	Feat f = feat_resolve_field(field, s->features);
	int ty = effective_type(field, f);
	PpNode *e;
	if (ty == PT_STRING)
		e = ref_lt(lt, pp_ty("str"));
	else if (ty == PT_BYTES)
		e = ref_lt(lt, pp_ty_slice(pp_ty("u8")));
	else if (ty == PT_MESSAGE || ty == PT_GROUP)
		e = resolve_view_ty(s, field, lt);
	else if (ty == PT_ENUM)
		e = enum_or_value(resolve_enum_ty(s, field), is_closed_enum(f));
	else
		e = view_scalar_ty(ty);
	return pp_ty_gen("::buffa::RepeatedView", pp_lifetime(lt), e, NULL);
}

static PpNode *view_map_elem(const Scope *s, const PbMsg *fd, const char *lt, bool value)
{
	int ty = effective_type_in_map_entry(fd, s->features);
	if (ty == PT_STRING)
		return ref_lt(lt, pp_ty("str"));
	if (ty == PT_BYTES)
		return ref_lt(lt, pp_ty_slice(pp_ty("u8")));
	if (value && ty == PT_MESSAGE)
		return resolve_view_ty(s, fd, lt);
	if (value && ty == PT_ENUM)
		return enum_or_value(resolve_enum_ty(s, fd), is_closed_enum(feat_resolve_field(fd, s->features)));
	return view_scalar_ty(ty);
}

static PpNode *view_map_type(const Scope *s, const PbMsg *msg, const PbMsg *field, const char *lt)
{
	const PbMsg *k, *v;
	find_map_entry_fields(msg, field, &k, &v);
	return pp_ty_gen("::buffa::MapView", pp_lifetime(lt), view_map_elem(s, k, lt, false), view_map_elem(s, v, lt, true), NULL);
}

static bool borrows(int ty) { return ty == PT_STRING || ty == PT_BYTES || ty == PT_MESSAGE || ty == PT_GROUP; }

static Vec oneof_members(const PbMsg *msg, int idx)
{
	Vec v = {0};
	for (int i = 0; i < n_fields(msg); i++)
		if (is_real_oneof_member(field_at(msg, i)) && fd_oneof(field_at(msg, i)) == idx)
			vec_push(&v, (void *)field_at(msg, i));
	return v;
}

static bool oneof_view_needs_lifetime(const Vec *fields, Feat features)
{
	for (int i = 0; i < fields->n; i++)
		if (borrows(effective_type(fields->v[i], features)))
			return true;
	return false;
}

static bool message_view_has_borrowing_field(const PbMsg *msg, Feat features, bool preserve)
{
	if (preserve)
		return true;
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		if (fd_label(f) == LABEL_REPEATED || borrows(effective_type(f, features)))
			return true;
	}
	for (int i = 0; i < pb_count(msg, DP_ONEOF_DECL); i++) {
		Vec m = oneof_members(msg, i);
		if (oneof_view_needs_lifetime(&m, features))
			return true;
	}
	return false;
}

static PpNode *oneof_ty(const char *prefix, const char *ident, bool lifetime, const char *lt)
{
	const char *p = xcat(prefix, ident);
	return lifetime ? ty_lt(p, lt) : pp_ty(p);
}

/* ---- oneof view enums ---- */

static void generate_oneof_view_enum(Vec *out, const Scope *s, const PbMsg *msg, int idx, const char **oneof_idents)
{
	const char *base = oneof_idents[idx];
	if (!base)
		return;
	Vec fields = oneof_members(msg, idx);
	if (!fields.n)
		return;
	Scope body = *s;
	body.nesting = s->nesting + 4;
	bool lifetime = oneof_view_needs_lifetime(&fields, s->features);
	PpNode *en = pp_item_enum(PP_VIS_PUB, base);
	if (lifetime)
		pp_set_generics(en, generics_a());
	bool redacted = false;
	for (int i = 0; i < fields.n; i++) {
		const PbMsg *f = fields.v[i];
		const char *variant = oneof_variant_ident(field_name_req(f));
		int ty = effective_type(f, s->features);
		Feat ff = feat_resolve_field(f, s->features);
		PpNode *vty;
		if (ty == PT_STRING)
			vty = ref_lt("'a", pp_ty("str"));
		else if (ty == PT_BYTES)
			vty = ref_lt("'a", pp_ty_slice(pp_ty("u8")));
		else if (ty == PT_MESSAGE || ty == PT_GROUP)
			vty = ty_g1("::buffa::alloc::boxed::Box", resolve_view_ty(&body, f, "'a"));
		else if (ty == PT_ENUM)
			vty = enum_or_value(resolve_enum_ty(&body, f), is_closed_enum(ff));
		else
			vty = view_scalar_ty(ty);
		PpNode *var = pp_variant(variant, PP_F_UNNAMED, NULL);
		pp_push(var, pp_field(0, NULL, vty));
		pp_push(en, var);
		redacted |= is_debug_redacted(f);
	}
	pp_push_attr(en, attr_src("derive", redacted ? "Clone" : "Clone, Debug"));
	vec_push(out, en);
	if (redacted) {
		PpNode *m = pp_e_match(pp_e("self"));
		for (int i = 0; i < fields.n; i++) {
			const PbMsg *f = fields.v[i];
			const char *ident = oneof_variant_ident(field_name_req(f));
			PpNode *dt = e_meth1(pp_e("f"), "debug_tuple", lit_str(ident));
			if (is_debug_redacted(f)) {
				PpNode *fa = pp_macro(pp_path("::core::format_args"), PP_DELIM_PAREN);
				pp_push(fa, pp_tt_lit(pm2_string(str_c(DEBUG_REDACT_PLACEHOLDER))));
				pp_push(m, pp_arm(pp_p_tuple_struct(pp_path(xcat("Self::", ident)), pp_p_wild(), NULL), NULL,
						  e_meth0(e_meth1(dt, "field", pp_e_ref(false, pp_e_macro(fa))), "finish")));
			} else
				pp_push(m, pp_arm(pp_p_tuple_struct(pp_path(xcat("Self::", ident)), pp_p_ident("value", false, false), NULL), NULL,
						  e_meth0(e_meth1(dt, "field", pp_e("value")), "finish")));
		}
		PpNode *imp = pp_item_impl(pp_path("::core::fmt::Debug"), lifetime ? ty_lt(base, "'a") : pp_ty(base));
		if (lifetime)
			pp_set_generics(imp, generics_a());
		PpNode *sig = pp_sig("fmt", pp_ty("::core::fmt::Result"));
		pp_push(sig, pp_receiver(true, false, NULL));
		pp_push(sig, arg("f", ty_ref_mut(pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
		pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(m), NULL)));
		vec_push(out, imp);
	}
}

/* ---- required presence ---- */

typedef struct {
	const char *ident, *rust_name, *proto_name;
	uint32_t number;
	int bit; /* -1: message (is_set) */
} RequiredViewField;

static RequiredViewField *required_view_fields(const Scope *s, const PbMsg *msg, int *count, int *bits)
{
	int nf = n_fields(msg), n = 0, next = 0;
	RequiredViewField *r = xalloc(sizeof *r * (size_t)(nf + 1));
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		int ty = fd_type(f);
		if (is_real_oneof_member(f) || fd_label(f) == LABEL_REPEATED || !is_supported_field_type(ty) || !is_required_field(f, s->features))
			continue;
		if (!pb_has(f, FD_NAME) || !pb_has(f, FD_NUMBER))
			continue;
		r[n].ident = ctx_field_ident(dname(f), fd_number(f));
		r[n].rust_name = ctx_field_rust_name(dname(f), fd_number(f));
		r[n].proto_name = dname(f);
		r[n].number = (uint32_t)fd_number(f);
		r[n].bit = ty == PT_MESSAGE || ty == PT_GROUP ? -1 : next++;
		n++;
	}
	*count = n;
	*bits = next;
	return r;
}

static PpNode *mask_lit(int bit) { return lit_usuffixed((uint64_t)1 << (bit % 64), "u64"); }
static char *seen_word(int bit) { return xfmt("__buffa_required_seen_%d", bit / 64); }

static PpNode *required_bit_stmt(const RequiredViewField *r, int n, uint32_t number)
{
	for (int i = 0; i < n; i++)
		if (r[i].number == number && r[i].bit >= 0)
			return s_semi(pp_e_binary(view_e(seen_word(r[i].bit)), "|=", mask_lit(r[i].bit)));
	return NULL;
}

static void required_has_methods(Vec *out, const RequiredViewField *r, int n)
{
	for (int i = 0; i < n; i++) {
		const char *doc;
		PpNode *body;
		if (r[i].bit >= 0) {
			doc = xcat("Whether required field `", r[i].proto_name,
				   "` was present on the wire.\n\nDistinguishes a field that was absent from one explicitly encoded with its "
				   "default value (required scalar fields are stored as bare, non-`Option` types, so the value alone cannot "
				   "tell the two apart). Presence is recorded only by the wire decoder: a default or hand-built view reports "
				   "`false`. Encoding is unaffected \xe2\x80\x94 required fields are always written.");
			body = pp_e_binary(pp_e_binary(self_f(seen_word(r[i].bit)), "&", mask_lit(r[i].bit)), "!=", pp_e_lit_int("0"));
		} else {
			doc = xcat("Whether required field `", r[i].proto_name,
				   "` is set.\n\nMirrors `is_set()` on the field: `true` after decoding a message where the field was present "
				   "on the wire, and `true` on a hand-built view whose field is populated. Encoding is unaffected \xe2\x80\x94 "
				   "required fields are always written.");
			body = e_meth0(self_f(r[i].ident), "is_set");
		}
		PpNode *sig = pp_sig(xcat("has_", r[i].rust_name), pp_ty("bool"));
		sig->flags |= PP_F_CONST;
		pp_push(sig, pp_receiver(true, false, NULL));
		PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(body), NULL));
		pp_push_attr(fn, doc_attr(doc));
		pp_push_attr(fn, attr_word("must_use"));
		pp_push_attr(fn, attr_word("inline"));
		vec_push(out, fn);
	}
}

/* ---- decode arms ---- */

static void closed_enum_view_unknown_route(Vec *out, bool preserve)
{
	if (!preserve)
		return;
	vec_push(out, s_let("__span_len", false, pp_e_binary(e_meth0(pp_e("before_tag"), "len"), "-", e_meth0(pp_e("cur"), "len"))));
	vec_push(out, s_semi(call_try(pp_e_method(view_e("__buffa_unknown_fields"), "push_record", pp_e("before_tag"), pp_e("__span_len"),
						  pp_e("ctx"), NULL))));
}

static PpNode *mut_cur(void) { return pp_e_ref(true, pp_e("cur")); }

static void sub_message_merge(PpNode *b, const Scope *s, const PbMsg *field, const char *id, bool group, uint32_t number)
{
	const char *vt = resolve_view_path(s, field);
	pp_push(b, s_let("__sub_ctx", false, call_try(e_meth0(pp_e("ctx"), "descend"))));
	if (group)
		pp_push(b, s_let("sub", false, call_try(e_call3("::buffa::types::borrow_group", mut_cur(), lit_u32(number), e_meth0(pp_e("__sub_ctx"), "depth")))));
	else
		pp_push(b, s_let("sub", false, call_try(e_call1("::buffa::types::borrow_bytes", mut_cur()))));
	PpNode *m = pp_e_match(e_meth0(view_e(id), "as_mut"));
	pp_push(m, pp_arm(p_some(pp_p_ident("existing", false, false)), NULL,
			  pp_e_block(pp_block(s_expr(call_try(e_call3("::buffa::MessageView::merge_into_view", pp_e("existing"), pp_e("sub"),
								       pp_e("__sub_ctx")))),
					      NULL))));
	PpNode *dec = call_try(mv_call2(vt, "decode_view_ctx", pp_e("sub"), pp_e("__sub_ctx")));
	pp_push(m, pp_arm(pp_p_path(pp_path("None")), NULL, pp_e_assign(view_e(id), e_call1("::buffa::MessageFieldView::set", dec))));
	pp_push(b, s_expr(m));
}

static PpNode *scalar_decode_arm(const Scope *s, const PbMsg *field, PpNode *bit_stmt)
{
	bool preserve = CFG->preserve_unknown_fields;
	Feat f = feat_resolve_field(field, s->features);
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, f);
	const char *id = ctx_field_ident(name, fd_number(field));
	PpNode *b = pp_block(wire_type_check(pp_e("tag"), wire_type_token(ty)), NULL);
	bool opt = is_explicit_presence_scalar(field, ty, f);
	PpNode *(*wrap)(PpNode *) = NULL;
	(void)wrap;
	PpNode *val = NULL;
	switch (ty) {
	case PT_STRING: val = call_try(e_call1("::buffa::types::borrow_str", mut_cur())); break;
	case PT_BYTES: val = call_try(e_call1("::buffa::types::borrow_bytes", mut_cur())); break;
	case PT_ENUM:
		if (is_closed_enum(f)) {
			Vec unk = {0}, known = {0};
			closed_enum_view_unknown_route(&unk, preserve);
			vec_push(&known, s_semi(pp_e_assign(view_e(id), opt ? e_call1("Some", pp_e("__v")) : pp_e("__v"))));
			if (!opt && bit_stmt)
				vec_push(&known, bit_stmt);
			closed_enum_decode(b, mut_cur(), &known, &unk);
			return arm_block(number, b);
		}
		val = e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", mut_cur())));
		break;
	case PT_MESSAGE: case PT_GROUP:
		sub_message_merge(b, s, field, id, ty == PT_GROUP, number);
		return arm_block(number, b);
	default:
		val = call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), mut_cur()));
	}
	pp_push(b, s_semi(pp_e_assign(view_e(id), opt ? e_call1("Some", val) : val)));
	if (!opt && bit_stmt)
		pp_push(b, bit_stmt);
	return arm_block(number, b);
}

static PpNode *register_size_of(const char *vt)
{
	return s_semi(call_try(e_meth1(pp_e("ctx"), "register_element_memory",
				       pp_e_call(pp_e_path(pp_path_args(pp_path("::core::mem::size_of"), pp_angle(pp_ty(vt), NULL))), NULL))));
}

static PpNode *view_push(const char *id, PpNode *e) { return s_semi(e_meth1(view_e(id), "push", e)); }

static PpNode *repeated_decode_arm(const Scope *s, const PbMsg *field)
{
	bool preserve = CFG->preserve_unknown_fields;
	Feat f = feat_resolve_field(field, s->features);
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, f);
	const char *id = ctx_field_ident(name, fd_number(field));
	if (ty == PT_MESSAGE || ty == PT_GROUP) {
		const char *vt = resolve_view_path(s, field);
		PpNode *b = pp_block(wire_type_check(pp_e("tag"), wire_type_token(ty)), s_let("__sub_ctx", false, call_try(e_meth0(pp_e("ctx"), "descend"))), NULL);
		if (ty == PT_GROUP)
			pp_push(b, s_let("sub", false, call_try(e_call3("::buffa::types::borrow_group", mut_cur(), lit_u32(number), e_meth0(pp_e("__sub_ctx"), "depth")))));
		else
			pp_push(b, s_let("sub", false, call_try(e_call1("::buffa::types::borrow_bytes", mut_cur()))));
		pp_push(b, register_size_of(vt));
		pp_push(b, view_push(id, call_try(mv_call2(vt, "decode_view_ctx", pp_e("sub"), pp_e("__sub_ctx")))));
		return arm_block(number, b);
	}
	if (!is_packed_type(ty)) {
		PpNode *borrow = call_try(e_call1(ty == PT_STRING ? "::buffa::types::borrow_str" : "::buffa::types::borrow_bytes", mut_cur()));
		return arm_block(number, pp_block(wire_check_ld(), s_let("__elem", false, borrow), register_elem("__elem"),
						  view_push(id, pp_e("__elem")), NULL));
	}
	bool closed = is_closed_enum(f);
	PpNode *packed_elem = pp_block(NULL), *unpacked = pp_block(NULL);
	PpNode *pcur = pp_e_ref(true, pp_e("pcur"));
	if (ty == PT_ENUM && closed) {
		Vec unk = {0}, known = one(view_push(id, pp_e("__v")));
		if (preserve)
			vec_push(&unk, s_semi(call_try(pp_e_method(view_e("__buffa_unknown_fields"), "push_varint", lit_u32(number),
								   cast_u64(pp_e("__raw")), pp_e("ctx"), NULL))));
		closed_enum_decode(packed_elem, pcur, &known, &unk);
		Vec unk2 = {0}, known2 = one(view_push(id, pp_e("__v")));
		closed_enum_view_unknown_route(&unk2, preserve);
		closed_enum_decode(unpacked, mut_cur(), &known2, &unk2);
	} else if (ty == PT_ENUM) {
		pp_push(packed_elem, view_push(id, e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32_packed", pcur)))));
		pp_push(unpacked, view_push(id, e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", mut_cur())))));
	} else {
		const char *pfn = is_fixed32_family(ty) || is_fixed64_family(ty) ? tfn("::buffa::types::decode_", ty, "") : tfn("::buffa::types::decode_", ty, "_packed");
		pp_push(packed_elem, view_push(id, call_try(e_call1(pfn, pcur))));
		pp_push(unpacked, view_push(id, call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), mut_cur()))));
	}
	PpNode *ld = pp_block(s_let("payload", false, call_try(e_call1("::buffa::types::borrow_bytes", mut_cur()))), NULL);
	if (is_fixed32_family(ty) || is_fixed64_family(ty))
		pp_push(ld, s_semi(call_try(e_call2(tfn("::buffa::types::extend_packed_", ty, ""), pp_e("payload"), e_meth0(view_e(id), "as_mut_vec")))));
	else if (ty != PT_ENUM)
		pp_push(ld, s_semi(call_try(e_call3(tfn("::buffa::types::extend_packed_", ty, ""), pp_e("payload"), e_meth0(view_e(id), "as_mut_vec"),
						    e_call1("::buffa::encoding::count_varints", pp_e("payload"))))));
	else {
		pp_push(ld, s_semi(e_meth1(view_e(id), "reserve", e_call1("::buffa::encoding::count_varints", pp_e("payload")))));
		pp_push(ld, pp_s_local(pp_p_type(pp_p_ident("pcur", false, true), ty_ref(pp_ty_slice(pp_ty("u8")))), pp_e("payload")));
		pp_push(ld, s_expr(pp_e_while(not(e_meth0(pp_e("pcur"), "is_empty")), packed_elem)));
	}
	PpNode *mismatch = pp_block(s_semi(pp_e_return(e_call1("Err", e_call2("::buffa::encoding::wire_type_mismatch", pp_e("tag"),
										   pp_e("::buffa::encoding::WireType::LengthDelimited"))))),
				    NULL);
	PpNode *elif = pp_e_if(pp_e_binary(e_meth0(pp_e("tag"), "wire_type"), "==", wire_type_token(ty)), unpacked, pp_e_block(mismatch));
	PpNode *iff = pp_e_if(pp_e_binary(e_meth0(pp_e("tag"), "wire_type"), "==", pp_e("::buffa::encoding::WireType::LengthDelimited")), ld, elif);
	return arm_block(number, pp_block(s_expr(iff), NULL));
}

static PpNode *map_entry_default(int ty)
{
	if (ty == PT_STRING)
		return lit_str("");
	if (ty == PT_BYTES)
		return pp_e_ref(false, pp_e_index(pp_e_array(NULL), pp_e_range(NULL, NULL, false)));
	return pp_e_call(pp_e("::core::default::Default::default"), NULL);
}

static void map_view_entry_decode(PpNode *b, const Scope *s, const PbMsg *fd, const char *var, bool hooks)
{
	Feat f = feat_resolve_field(fd, s->features);
	int ty = effective_type_in_map_entry(fd, f);
	pp_push(b, wire_type_check(pp_e("entry_tag"), wire_type_token(ty)));
	PpNode *ec = pp_e_ref(true, pp_e("entry_cur"));
	switch (ty) {
	case PT_STRING: pp_push(b, s_semi(pp_e_assign(pp_e(var), call_try(e_call1("::buffa::types::borrow_str", ec))))); break;
	case PT_BYTES: pp_push(b, s_semi(pp_e_assign(pp_e(var), call_try(e_call1("::buffa::types::borrow_bytes", ec))))); break;
	case PT_ENUM:
		if (is_closed_enum(f)) {
			Vec known = {0}, unk = {0};
			vec_push(&known, s_semi(pp_e_assign(pp_e(var), pp_e("__v"))));
			if (hooks) {
				vec_push(&known, s_semi(pp_e_assign(pp_e("__entry_unknown"), pp_e_lit_bool(false))));
				vec_push(&unk, s_semi(pp_e_assign(pp_e("__entry_unknown"), pp_e_lit_bool(true))));
			}
			closed_enum_decode(b, ec, &known, &unk);
		} else
			pp_push(b, s_semi(pp_e_assign(pp_e(var), e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", ec))))));
		break;
	case PT_MESSAGE: {
		const char *vt = resolve_view_path(s, fd);
		pp_push(b, s_let("__sub_ctx", false, call_try(e_meth0(pp_e("ctx"), "descend"))));
		pp_push(b, s_let("sub", false, call_try(e_call1("::buffa::types::borrow_bytes", ec))));
		pp_push(b, s_semi(call_try(mv_call3(vt, "merge_into_view", pp_e_ref(true, pp_e(var)),
						   pp_e("sub"), pp_e("__sub_ctx")))));
		break;
	}
	default: pp_push(b, s_semi(pp_e_assign(pp_e(var), call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), ec)))));
	}
}

static PpNode *map_decode_arm(const Scope *s, const PbMsg *msg, const PbMsg *field)
{
	bool preserve = CFG->preserve_unknown_fields;
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	const char *id = ctx_field_ident(name, fd_number(field));
	const PbMsg *k, *v;
	find_map_entry_fields(msg, field, &k, &v);
	int kt = effective_type_in_map_entry(k, s->features), vt = effective_type_in_map_entry(v, s->features);
	bool closed = vt == PT_ENUM && is_closed_enum(feat_resolve_field(v, s->features));
	PpNode *b = pp_block(wire_check_ld(), s_let("entry_bytes", false, call_try(e_call1("::buffa::types::borrow_bytes", mut_cur()))),
			     pp_s_local(pp_p_type(pp_p_ident("entry_cur", false, true), pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8")))), pp_e("entry_bytes")),
			     s_let("key", true, map_entry_default(kt)), s_let("val", true, map_entry_default(vt)), NULL);
	pp_push(b, s_semi(call_try(e_meth1(pp_e("ctx"), "register_element_memory",
					   plus(e_call1("::buffa::__private::element_footprint", pp_e_ref(false, pp_e("key"))),
						e_call1("::buffa::__private::element_footprint", pp_e_ref(false, pp_e("val"))))))));
	if (closed)
		pp_push(b, s_let("__entry_unknown", true, pp_e_lit_bool(false)));
	PpNode *kb = pp_block(NULL), *vb = pp_block(NULL);
	map_view_entry_decode(kb, s, k, "key", false);
	map_view_entry_decode(vb, s, v, "val", closed);
	PpNode *m = pp_e_match(e_meth0(pp_e("entry_tag"), "field_number"));
	pp_push(m, pp_arm(pp_p_lit(pp_e_lit_int("1")), NULL, pp_e_block(kb)));
	pp_push(m, pp_arm(pp_p_lit(pp_e_lit_int("2")), NULL, pp_e_block(vb)));
	pp_push(m, pp_arm(pp_p_wild(), NULL,
			  pp_e_block(pp_block(s_semi(call_try(e_call3("::buffa::encoding::skip_field_depth", pp_e("entry_tag"),
								      pp_e_ref(true, pp_e("entry_cur")), e_meth0(pp_e("ctx"), "depth")))),
					      NULL))));
	PpNode *wb = pp_block(s_let("entry_tag", false, call_try(e_call1("::buffa::encoding::Tag::decode", pp_e_ref(true, pp_e("entry_cur"))))),
			      s_expr(m), NULL);
	pp_push(b, s_expr(pp_e_while(not(e_meth0(pp_e("entry_cur"), "is_empty")), wb)));
	PpNode *push = s_semi(pp_e_method(view_e(id), "push", pp_e("key"), pp_e("val"), NULL));
	if (closed) {
		if (preserve) {
			Vec r = {0};
			closed_enum_view_unknown_route(&r, true);
			PpNode *t = pp_block(NULL);
			push_all(t, &r);
			pp_push(b, s_expr(pp_e_if(pp_e("__entry_unknown"), t, pp_e_block(pp_block(push, NULL)))));
		} else
			pp_push(b, stmt_if(not(pp_e("__entry_unknown")), pp_block(push, NULL)));
	} else
		pp_push(b, push);
	return arm_block(number, b);
}

static void oneof_view_decode_arms(Vec *out, const Scope *s, const char *base, const char *oneof_name, const Vec *fields, const char *prefix)
{
	bool preserve = CFG->preserve_unknown_fields;
	const char *fid = ctx_oneof_ident(oneof_name);
	const char *ve = xcat(prefix, base);
	for (int i = 0; i < fields->n; i++) {
		const PbMsg *field = fields->v[i];
		const char *name = field_name_req(field);
		uint32_t number = validated_field_number(field);
		int ty = effective_type(field, s->features);
		Feat ff = feat_resolve_field(field, s->features);
		const char *variant = oneof_variant_ident(name);
		const char *vpath = xcat(ve, "::", variant);
		PpNode *b = pp_block(wire_type_check(pp_e("tag"), wire_type_token(ty)), NULL);
		PpNode *val = NULL;
		switch (ty) {
		case PT_STRING: val = call_try(e_call1("::buffa::types::borrow_str", mut_cur())); break;
		case PT_BYTES: val = call_try(e_call1("::buffa::types::borrow_bytes", mut_cur())); break;
		case PT_MESSAGE: case PT_GROUP: {
			const char *vt = resolve_view_path(s, field);
			pp_push(b, s_let("__sub_ctx", false, call_try(e_meth0(pp_e("ctx"), "descend"))));
			if (ty == PT_GROUP)
				pp_push(b, s_let("sub", false, call_try(e_call3("::buffa::types::borrow_group", mut_cur(), lit_u32(number), e_meth0(pp_e("__sub_ctx"), "depth")))));
			else
				pp_push(b, s_let("sub", false, call_try(e_call1("::buffa::types::borrow_bytes", mut_cur()))));
			PpNode *pat = p_some(pp_p_tuple_struct(pp_path(vpath), pp_p_ident("existing", true, true), NULL));
			PpNode *t = pp_block(s_semi(call_try(e_call3("::buffa::MessageView::merge_into_view",
								     pp_e_ref(true, pp_e_unary("*", pp_e_unary("*", pp_e("existing")))), pp_e("sub"),
								     pp_e("__sub_ctx")))),
					     NULL);
			PpNode *dec = call_try(mv_call2(vt, "decode_view_ctx", pp_e("sub"), pp_e("__sub_ctx")));
			PpNode *e = pp_block(s_semi(pp_e_assign(view_e(fid), e_call1("Some", e_call1(vpath, e_call1("::buffa::alloc::boxed::Box::new", dec))))), NULL);
			pp_push(b, s_expr(pp_e_if(pp_e_let(pat, view_e(fid)), t, pp_e_block(e))));
			vec_push(out, arm_block(number, b));
			continue;
		}
		case PT_ENUM:
			if (is_closed_enum(ff)) {
				Vec unk = {0}, known = one(s_semi(pp_e_assign(view_e(fid), e_call1("Some", e_call1(vpath, pp_e("__v"))))));
				closed_enum_view_unknown_route(&unk, preserve);
				closed_enum_decode(b, mut_cur(), &known, &unk);
				vec_push(out, arm_block(number, b));
				continue;
			}
			val = e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", mut_cur())));
			break;
		default: val = call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), mut_cur()));
		}
		pp_push(b, s_semi(pp_e_assign(view_e(fid), e_call1("Some", e_call1(vpath, val)))));
		vec_push(out, arm_block(number, b));
	}
}

/* ---- to_owned ---- */

static PpNode *closure1(const char *param, PpNode *body)
{
	PpNode *c = pp_e_closure(body, false);
	pp_push(c, pp_p_ident(param, false, false));
	return c;
}

static PpNode *singular_to_owned(const Scope *s, const PbMsg *field, int ty, const char *id)
{
	if (is_explicit_presence_scalar(field, ty, s->features)) {
		if (ty == PT_STRING)
			return e_meth1(self_f(id), "map", closure1("s", e_meth0(pp_e("s"), "to_string")));
		if (ty == PT_BYTES)
			return e_meth1(self_f(id), "map", closure1("b", e_meth0(pp_e_paren(pp_e("b")), "to_vec")));
		return self_f(id);
	}
	if (ty == PT_STRING)
		return e_meth0(self_f(id), "to_string");
	if (ty == PT_BYTES)
		return e_meth0(pp_e_paren(self_f(id)), "to_vec");
	if (ty == PT_MESSAGE || ty == PT_GROUP) {
		PpNode *owned = ty_path_str(resolve_owned_path(s, field));
		bool inl = ctx_field_inline(xcat(".", s->fqn, ".", dname(field)));
		PpNode *args = inl ? pp_angle(owned, ty_g1("::buffa::Inline", owned), NULL) : pp_angle(owned, NULL);
		PpNode *some = pp_e_path(pp_path_push(pp_path_args(pp_path("::buffa::MessageField"), args), "some", NULL));
		PpNode *m = pp_e_match(e_meth0(self_f(id), "as_option"));
		pp_push(m, pp_arm(p_some(pp_p_ident("v", false, false)), NULL,
				  pp_e_call(some, call_try(e_meth1(pp_e("v"), "to_owned_from_source", pp_e("__buffa_src"))), NULL)));
		pp_push(m, pp_arm(pp_p_path(pp_path("None")), NULL, pp_e_call(pp_e("::buffa::MessageField::none"), NULL)));
		return m;
	}
	return self_f(id);
}

static PpNode *collect_result(PpNode *recv)
{
	return call_try(pp_e_method_tf(recv, "collect", pp_angle(ty_g2("::core::result::Result", pp_ty_infer(), pp_ty("::buffa::DecodeError")), NULL), NULL));
}

static PpNode *repeated_to_owned(int ty, const char *id)
{
	PpNode *it = e_meth0(self_f(id), "iter");
	if (ty == PT_STRING)
		return e_meth0(e_meth1(it, "map", closure1("s", e_meth0(pp_e("s"), "to_string"))), "collect");
	if (ty == PT_BYTES)
		return e_meth0(e_meth1(it, "map", closure1("b", e_meth0(pp_e_paren(pp_e("b")), "to_vec"))), "collect");
	if (ty == PT_MESSAGE || ty == PT_GROUP)
		return collect_result(e_meth1(it, "map", closure1("v", e_meth1(pp_e("v"), "to_owned_from_source", pp_e("__buffa_src")))));
	return e_meth0(self_f(id), "to_vec");
}

static PpNode *map_to_owned_expr(const Scope *s, const PbMsg *msg, const PbMsg *field, const char *id)
{
	const PbMsg *kf, *vf;
	find_map_entry_fields(msg, field, &kf, &vf);
	int kt = effective_type_in_map_entry(kf, s->features), vt = effective_type_in_map_entry(vf, s->features);
	PpNode *kc = kt == PT_STRING ? e_meth0(pp_e("k"), "to_string") : kt == PT_BYTES ? e_meth0(pp_e("k"), "to_vec") : pp_e_unary("*", pp_e("k"));
	PpNode *vc = vt == PT_STRING ? e_meth0(pp_e("v"), "to_string")
		     : vt == PT_BYTES ? e_meth0(pp_e("v"), "to_vec")
		     : vt == PT_MESSAGE ? call_try(e_meth1(pp_e("v"), "to_owned_from_source", pp_e("__buffa_src")))
					: pp_e_unary("*", pp_e("v"));
	if (vt == PT_MESSAGE)
		(void)resolve_owned_path(s, vf);
	PpNode *c;
	if (vt == PT_MESSAGE)
		c = pp_e_closure(pp_e_block(pp_block(s_expr(pp_e_call(pp_e_path(pp_path_push(pp_path_args(pp_path("::core::result::Result"), pp_angle(pp_ty_infer(), pp_ty("::buffa::DecodeError"), NULL)), "Ok", NULL)), pp_e_tuple(kc, vc, NULL), NULL)), NULL)), false);
	else
		c = pp_e_closure(pp_e_tuple(kc, vc, NULL), false);
	pp_push(c, pp_p_tuple(pp_p_ident("k", false, false), pp_p_ident("v", false, false), NULL));
	PpNode *mapped = e_meth1(e_meth0(self_f(id), "iter"), "map", c);
	return vt == PT_MESSAGE ? collect_result(mapped) : e_meth0(mapped, "collect");
}

static PpNode *oneof_variant_to_owned(const Scope *s, int ty, const char *oneof_name, const char *field_name)
{
	if (ty == PT_STRING)
		return e_meth0(pp_e("v"), "to_string");
	if (ty == PT_BYTES)
		return e_meth0(pp_e_paren(pp_e("v")), "to_vec");
	if (ty == PT_MESSAGE || ty == PT_GROUP) {
		PpNode *owned = call_try(e_meth1(pp_e("v"), "to_owned_from_source", pp_e("__buffa_src")));
		if (variant_boxed(ty, xcat(".", s->fqn, ".", oneof_name, ".", field_name)))
			return e_call1("::buffa::alloc::boxed::Box::new", owned);
		return owned;
	}
	return pp_e_unary("*", pp_e("v"));
}

static void build_oneof_to_owned_fields(PpNode *st, const Scope *s, const PbMsg *msg, const char *view_prefix, const char *owned_prefix,
					const char **oneof_idents);
static void build_to_owned_fields(PpNode *st, const Scope *s, const PbMsg *msg, const char *view_prefix, const char *owned_prefix, const char **oneof_idents)
{
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		const char *name = field_name_req(f);
		const char *id = ctx_field_ident(name, fd_number(f));
		bool rep = fd_label(f) == LABEL_REPEATED;
		if (rep && is_map_field(msg, f)) {
			pp_push(st, pp_field_value(id, map_to_owned_expr(s, msg, f, id)));
			continue;
		}
		int ty = effective_type(f, s->features);
		pp_push(st, pp_field_value(id, rep ? repeated_to_owned(ty, id) : singular_to_owned(s, f, ty, id)));
	}
	build_oneof_to_owned_fields(st, s, msg, view_prefix, owned_prefix, oneof_idents);
}

/* the oneof and unknown-field initialisers of to_owned (shared with the lazy family) */
static void build_oneof_to_owned_fields(PpNode *st, const Scope *s, const PbMsg *msg, const char *view_prefix, const char *owned_prefix,
					const char **oneof_idents)
{
	int no = pb_count(msg, DP_ONEOF_DECL);
	for (int idx = 0; idx < no; idx++) {
		const char *base = oneof_idents[idx];
		if (!base)
			continue;
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		const char *oneof_name = dname(o);
		Vec group = oneof_members(msg, idx);
		if (!group.n)
			continue;
		const char *fid = ctx_oneof_ident(oneof_name);
		PpNode *m = pp_e_match(pp_e("v"));
		bool fallible = false;
		for (int k = 0; k < group.n; k++) {
			const PbMsg *f = group.v[k];
			const char *fname = field_name_req(f);
			const char *variant = oneof_variant_ident(fname);
			int ty = effective_type(f, s->features);
			fallible |= ty == PT_MESSAGE || ty == PT_GROUP;
			pp_push(m, pp_arm(pp_p_tuple_struct(pp_path(xcat(view_prefix, base, "::", variant)), pp_p_ident("v", false, false), NULL), NULL,
					  e_call1(xcat(owned_prefix, base, "::", variant), oneof_variant_to_owned(s, ty, oneof_name, fname))));
		}
		PpNode *val;
		if (fallible) {
			val = pp_e_match(e_meth0(self_f(fid), "as_ref"));
			pp_push(val, pp_arm(p_core_some(pp_p_ident("v", false, false)), NULL, pp_e_block(pp_block(s_expr(e_some(m)), NULL))));
			pp_push(val, pp_arm(pp_p_path(pp_path("::core::option::Option::None")), NULL, e_none()));
		} else
			val = e_meth1(e_meth0(self_f(fid), "as_ref"), "map", closure1("v", m));
		pp_push(st, pp_field_value(fid, val));
	}
	if (CFG->preserve_unknown_fields)
		pp_push(st, pp_field_value("__buffa_unknown_fields", e_meth0(call_try(e_meth0(self_f("__buffa_unknown_fields"), "to_owned")), "into")));
}

/* ---- view encoding (impl_message.rs build_view_encode_methods, map_view_*) ---- */

static PpNode *map_element_size_expr(int ty, const char *var)
{
	if (ty == PT_STRING || ty == PT_BYTES)
		return cast_u64(e_call1(tfn("::buffa::types::", ty, "_encoded_len"), pp_e(var)));
	if (ty == PT_ENUM)
		return cast_u64(e_call1("::buffa::types::int32_encoded_len", e_meth0(pp_e(var), "to_i32")));
	return type_encoded_size_expr(ty, pp_e_unary("*", pp_e(var)));
}

static bool map_element_size_is_constant(int ty) { return is_fixed32_family(ty) || is_fixed64_family(ty) || ty == PT_BOOL; }

static void map_element_encode(PpNode *b, int ty, uint32_t tag, const char *var)
{
	pp_push(b, s_semi(e_meth1(e_call2("::buffa::encoding::Tag::new", lit_u32(tag), wire_type_token(ty)), "encode", pp_e("buf"))));
	if (ty == PT_STRING || ty == PT_BYTES)
		pp_push(b, s_semi(e_call2(tfn("::buffa::types::encode_", ty, ""), pp_e(var), pp_e("buf"))));
	else if (ty == PT_ENUM)
		pp_push(b, s_semi(e_call2("::buffa::types::encode_int32", e_meth0(pp_e(var), "to_i32"), pp_e("buf"))));
	else if (ty == PT_MESSAGE) {
		pp_push(b, s_semi(e_call2("::buffa::encoding::encode_varint", cast_u64(pp_e("__v_len")), pp_e("buf"))));
		pp_push(b, s_semi(pp_e_method(pp_e(var), "write_to", pp_e("__cache"), pp_e("buf"), NULL)));
	} else
		pp_push(b, s_semi(e_call2(tfn("::buffa::types::encode_", ty, ""), pp_e_unary("*", pp_e(var)), pp_e("buf"))));
}

static PpNode *map_view_compute_size_stmt(const PbMsg *msg, const PbMsg *field, Feat features)
{
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	const char *id = ctx_field_ident(name, fd_number(field));
	uint64_t outer = tag_encoded_len(number, 2);
	const PbMsg *kf, *vf;
	find_map_entry_fields(msg, field, &kf, &vf);
	int kt = effective_type_in_map_entry(kf, features), vt = effective_type_in_map_entry(vf, features);
	PpNode *ks = map_element_size_expr(kt, "k"), *vs;
	if (vt == PT_MESSAGE)
		vs = pp_e_block(pp_block(s_let("__slot", false, e_meth0(pp_e("__cache"), "reserve")),
					 s_let("inner", false, e_meth1(pp_e("v"), "compute_size", pp_e("__cache"))),
					 s_semi(pp_e_method(pp_e("__cache"), "set", pp_e("__slot"), pp_e("inner"), NULL)),
					 s_expr(plus(cast_u64(e_call1("::buffa::encoding::varint_len", cast_u64(pp_e("inner")))), cast_u64(pp_e("inner")))),
					 NULL));
	else
		vs = map_element_size_expr(vt, "v");
	PpNode *entry = plus(plus(plus(u64lit(tag_encoded_len(1, wire_type_byte(kt))), ks), u64lit(tag_encoded_len(2, wire_type_byte(vt)))), vs);
	PpNode *decl = pp_s_local(pp_p_type(pp_p_ident("entry_size", false, false), pp_ty("u64")), entry);
	if (map_element_size_is_constant(kt) && map_element_size_is_constant(vt))
		return s_expr(pp_e_block(pp_block(decl,
						  size_add(pp_e_binary(cast_u64(e_meth0(self_f(id), "len")), "*",
								       pp_e_paren(plus(plus(u64lit(outer), cast_u64(e_call1("::buffa::encoding::varint_len", pp_e("entry_size")))),
										       pp_e("entry_size"))))),
						  NULL)));
	PpNode *body = pp_block(decl, size_add(plus(plus(u64lit(outer), cast_u64(e_call1("::buffa::encoding::varint_len", pp_e("entry_size")))), pp_e("entry_size"))), NULL);
	PpNode *pat = pp_p_tuple(pp_p_ident(map_element_size_is_constant(kt) ? "_k" : "k", false, false),
				 pp_p_ident(map_element_size_is_constant(vt) ? "_v" : "v", false, false), NULL);
	PpNode *loop = s_expr(pp_e_for(pat, pp_e_ref(false, self_f(id)), body));
	prepend_attr(loop->a, attr_src("allow", "clippy::for_kv_map"));
	return loop;
}

static PpNode *map_view_write_to_stmt(const PbMsg *msg, const PbMsg *field, Feat features)
{
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	const char *id = ctx_field_ident(name, fd_number(field));
	const PbMsg *kf, *vf;
	find_map_entry_fields(msg, field, &kf, &vf);
	int kt = effective_type_in_map_entry(kf, features), vt = effective_type_in_map_entry(vf, features);
	PpNode *b = pp_block(NULL), *vs;
	if (vt == PT_MESSAGE) {
		pp_push(b, s_let("__v_len", false, e_meth0(pp_e("__cache"), "consume_next")));
		vs = pp_e_paren(plus(cast_u64(e_call1("::buffa::encoding::varint_len", cast_u64(pp_e("__v_len")))), cast_u64(pp_e("__v_len"))));
	} else
		vs = map_element_size_expr(vt, "v");
	PpNode *entry = plus(plus(plus(u64lit(tag_encoded_len(1, wire_type_byte(kt))), map_element_size_expr(kt, "k")), u64lit(tag_encoded_len(2, wire_type_byte(vt)))), vs);
	pp_push(b, pp_s_local(pp_p_type(pp_p_ident("entry_size", false, false), pp_ty("u64")), entry));
	pp_push(b, s_semi(e_meth1(e_call2("::buffa::encoding::Tag::new", lit_u32(number), pp_e("::buffa::encoding::WireType::LengthDelimited")), "encode", pp_e("buf"))));
	pp_push(b, s_semi(e_call2("::buffa::encoding::encode_varint", pp_e("entry_size"), pp_e("buf"))));
	map_element_encode(b, kt, 1, "k");
	map_element_encode(b, vt, 2, "v");
	return s_expr(pp_e_for(pp_p_tuple(pp_p_ident("k", false, false), pp_p_ident("v", false, false), NULL), pp_e_ref(false, self_f(id)), b));
}

static PpNode *doc_fn(PpNode *fn, const char *const *lines);

/* lazy_fragment_encode_stmts: each recorded fragment re-emitted as one LengthDelimited occurrence */
static void lazy_fragment_encode_stmts(Vec *compute, Vec *write, const PbMsg *field, const char *accessor)
{
	const char *id = ctx_field_ident(field_name_req(field), fd_number(field));
	uint32_t number = validated_field_number(field);
	PpNode *len = cast_u64(e_meth0(pp_e("__frag"), "len"));
	PpNode *frags = e_meth0(self_f(id), accessor);
	vec_push(compute, s_expr(pp_e_for(pp_p_ident("__frag", false, false), frags,
					  pp_block(size_add(plus(plus(u64lit(tag_encoded_len(number, 2)), cast_u64(e_call1("::buffa::encoding::varint_len", len))),
								 cast_u64(e_meth0(pp_e("__frag"), "len")))),
						   NULL))));
	PpNode *b = pp_block(s_semi(e_meth1(e_call2("::buffa::encoding::Tag::new", lit_u32(number), pp_e("::buffa::encoding::WireType::LengthDelimited")),
					    "encode", pp_e("buf"))),
			     s_semi(e_call2("::buffa::encoding::encode_varint", cast_u64(e_meth0(pp_e("__frag"), "len")), pp_e("buf"))),
			     s_semi(e_meth1(pp_e("buf"), "put_slice", pp_e("__frag"))), NULL);
	vec_push(write, s_expr(pp_e_for(pp_p_ident("__frag", false, false), e_meth0(self_f(id), accessor), b)));
}

/* lazy: the scope of the lazy view family (inherent `pub fn`s with docs, lazy fields re-emit their
 * fragments), NULL for the eager ViewEncode impl. */
static void build_view_encode_methods(Vec *out, const PbMsg *msg, bool preserve, Feat features, const char **oneof_idents, const char *view_oneof_prefix,
				      const Scope *lazy)
{
	Vec fields = classify_fields_ordered(msg, oneof_idents);
	const char *cache = message_uses_size_cache(msg, &fields, features, lazy) ? "__cache" : "_cache";
	Vec compute = {0}, write = {0};
	for (int i = 0; i < fields.n; i++) {
		const FieldKind *k = fields.v[i];
		if ((k->kind == FK_SCALAR || k->kind == FK_REPEATED) && lazy && is_lazy_field(lazy, k->field)) {
			lazy_fragment_encode_stmts(&compute, &write, k->field, k->kind == FK_SCALAR ? "fragments" : "raw_elements");
			continue;
		}
		switch (k->kind) {
		case FK_SCALAR:
			vec_push(&compute, scalar_compute_size_stmt(k->field, features));
			vec_push(&write, scalar_write_to_stmt(k->field, features));
			break;
		case FK_REPEATED:
			vec_push(&compute, repeated_compute_size_stmt(k->field, features));
			vec_push(&write, repeated_write_to_stmt(k->field, features));
			break;
		case FK_MAP:
			vec_push(&compute, map_view_compute_size_stmt(msg, k->field, features));
			vec_push(&write, map_view_write_to_stmt(msg, k->field, features));
			break;
		case FK_ONEOF: {
			const char *fid = ctx_oneof_ident(k->name);
			const char *q = xcat(view_oneof_prefix, k->enum_ident);
			PpNode *sm = pp_e_match(pp_e("v")), *wm = pp_e_match(pp_e("v"));
			for (int j = 0; j < k->fields.n; j++) {
				const PbMsg *f = k->fields.v[j];
				uint32_t number = validated_field_number(f);
				int ty = effective_type(f, features);
				const char *variant = oneof_variant_ident(field_name_req(f));
				pp_push(sm, oneof_size_arm(q, variant, tag_encoded_len(number, wire_type_byte(ty)), ty));
				pp_push(wm, oneof_write_arm(q, variant, number, ty));
			}
			vec_push(&compute, stmt_if(pp_e_let(p_core_some(pp_p_ident("v", true, false)), self_f(fid)), pp_block(s_expr(sm), NULL)));
			vec_push(&write, stmt_if(pp_e_let(p_core_some(pp_p_ident("v", true, false)), self_f(fid)), pp_block(s_expr(wm), NULL)));
			break;
		}
		}
	}
	bool has_body = fields.n || preserve;
	int vis = lazy ? PP_VIS_PUB : 0;
	PpNode *sig = pp_sig("compute_size", pp_ty("u32"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg(cache, ty_ref_mut(pp_ty("::buffa::SizeCache"))));
	PpNode *body = pp_block(allow_unused_use("::buffa::Enumeration as _"), NULL);
	if (lazy)
		pp_push(body, allow_unused_use("::buffa::ViewEncode as _"));
	pp_push(body, s_let("size", has_body, pp_e_lit_int("0u64")));
	push_all(body, &compute);
	if (preserve)
		pp_push(body, size_add(cast_u64(e_meth0(self_f("__buffa_unknown_fields"), "encoded_len"))));
	pp_push(body, s_expr(e_call1("::buffa::saturate_size", pp_e("size"))));
	PpNode *fn = pp_item_fn(vis, sig, body);
	if (lazy) {
		static const char *const doc[] = {" Compute the encoded byte size, filling `cache` with", " per-message sizes consumed by a following `write_to` call.",
						  " Called for that side effect by `encode`; prefer `encoded_len`", " when only the size is needed.", NULL};
		doc_fn(fn, doc);
	}
	pp_push_attr(fn, attr_src("allow", "clippy::needless_borrow, clippy::let_and_return"));
	vec_push(out, fn);
	sig = pp_sig("write_to", NULL);
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg(cache, ty_ref_mut(pp_ty("::buffa::SizeCache"))));
	pp_push(sig, arg(has_body ? "buf" : "_buf", ty_ref_mut(ty_impl("::buffa::EncodeSink"))));
	body = pp_block(allow_unused_use("::buffa::Enumeration as _"), NULL);
	if (lazy)
		pp_push(body, allow_unused_use("::buffa::ViewEncode as _"));
	push_all(body, &write);
	if (preserve)
		pp_push(body, s_semi(e_meth1(self_f("__buffa_unknown_fields"), "write_to", pp_e("buf"))));
	fn = pp_item_fn(vis, sig, body);
	if (lazy) {
		static const char *const doc[] = {" Write the encoded bytes to `buf`, reading per-message sizes", " from the `cache` filled by a preceding `compute_size` call.",
						  " Prefer `encode` unless threading a shared cache.", NULL};
		doc_fn(fn, doc);
	}
	pp_push_attr(fn, attr_src("allow", "clippy::needless_borrow"));
	vec_push(out, fn);
}

/* ---- owned_view.rs ---- */

static bool reserved_wrapper_method(const char *name)
{
	static const char *const r[] = {"decode", "decode_with_options", "from_owned", "view", "to_owned_message", "bytes", "into_bytes", "clone", "into", NULL};
	for (int i = 0; r[i]; i++)
		if (str_eq(name, r[i]))
			return true;
	return false;
}

static PpNode *doc_fn(PpNode *fn, const char *const *lines)
{
	for (int i = 0; lines[i]; i++)
		pp_push_attr(fn, doc_attr(lines[i]));
	return fn;
}

static PpNode *ok_wrap(const char *wrapper, PpNode *inner) { return e_call1("::core::result::Result::Ok", e_call1(wrapper, inner)); }

static void view_serialize_owned_wrapper(Vec *out, const char *wrapper);

static void generate_owned_view_wrapper(Vec *out, const Scope *s, const PbMsg *msg, const char *rust_name, const char *view_ident,
					const char *owned_path, const char *view_oneof_prefix, const char **oneof_idents)
{
	const char *wrapper = xcat(rust_name, "OwnedView");
	PpNode *ov_static = pp_ty_gen("::buffa::OwnedView", ty_lt(view_ident, "'static"), NULL);
	Vec accessors = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (!is_supported_field_type(fd_type(f)) || is_real_oneof_member(f))
			continue;
		const char *name = field_name_req(f);
		int number = fd_number(f);
		if (reserved_wrapper_method(ctx_field_rust_name(name, number)))
			continue;
		const char *id = ctx_field_ident(name, number);
		bool rep = fd_label(f) == LABEL_REPEATED;
		const char *tag;
		PpNode *ret, *body;
		PpNode *field_ref = pp_e_field(e_meth0(pp_e_field(pp_e("self"), "0"), "reborrow"), id);
		if (rep && is_map_field(msg, f)) {
			tag = xfmt("Field %d: `%s` (map)", number, name);
			ret = ty_ref(view_map_type(s, msg, f, "'_"));
			body = pp_e_ref(false, field_ref);
		} else if (rep) {
			tag = xfmt("Field %d: `%s`", number, name);
			ret = ty_ref(view_repeated_type(s, f, "'_"));
			body = pp_e_ref(false, field_ref);
		} else {
			tag = xfmt("Field %d: `%s`", number, name);
			PpNode *t = view_singular_type(s, f, "'_");
			int et = effective_type(f, feat_resolve_field(f, s->features));
			if (et == PT_MESSAGE || et == PT_GROUP) {
				ret = ty_ref(t);
				body = pp_e_ref(false, field_ref);
			} else {
				ret = t;
				body = field_ref;
			}
		}
		Vec doc = {0};
		doc_attrs_with_tag(&doc, ctx_comment(xcat(s->fqn, ".", name)), tag, s->fqn);
		PpNode *sig = pp_sig(id, ret);
		pp_push(sig, pp_receiver(true, false, NULL));
		PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(body), NULL));
		attrs_all(fn, &doc);
		pp_push_attr(fn, attr_word("must_use"));
		vec_push(&accessors, fn);
	}
	for (int idx = 0; idx < pb_count(msg, DP_ONEOF_DECL); idx++) {
		if (!oneof_idents[idx])
			continue;
		Vec fields = oneof_members(msg, idx);
		if (!fields.n)
			continue;
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		const char *oneof_name = dname(o);
		if (reserved_wrapper_method(ctx_oneof_rust_name(oneof_name)))
			continue;
		const char *id = ctx_oneof_ident(oneof_name);
		bool lt = oneof_view_needs_lifetime(&fields, s->features);
		PpNode *sig = pp_sig(id, ty_g1(OPTION_PATH, ty_ref(oneof_ty(view_oneof_prefix, oneof_idents[idx], lt, "'_"))));
		pp_push(sig, pp_receiver(true, false, NULL));
		PpNode *fn = pp_item_fn(PP_VIS_PUB, sig,
					pp_block(s_expr(e_meth0(pp_e_field(e_meth0(pp_e_field(pp_e("self"), "0"), "reborrow"), id), "as_ref")), NULL));
		pp_push_attr(fn, doc_attr(xcat(" Oneof `", oneof_name, "`.")));
		pp_push_attr(fn, attr_word("must_use"));
		vec_push(&accessors, fn);
	}
	const char *wdoc = xcat(" Self-contained, `'static` owned view of a `", rust_name, "` message.\n\n Wraps [`::buffa::OwnedView`]`<`[`",
				view_ident,
				"`]`<'static>>`: the decoded view and the [`::buffa::bytes::Bytes`] buffer it borrows from travel together, so the "
				"handle is `'static` and `Send + Sync` \xe2\x80\x94 suitable for async handlers, spawned tasks, and anywhere a "
				"`'static` bound is required.\n\n Field accessors return borrows tied to `&self`. Use [`Self::view`] to get the "
				"full [`",
				view_ident, "`] when you need struct patterns, iteration helpers, or to pass the view to lifetime-parameterised code.");
	PpNode *st = pp_item_struct(PP_VIS_PUB, wrapper, PP_F_UNNAMED);
	pp_push(st, pp_field(0, NULL, ov_static));
	pp_push_attr(st, doc_attr(wdoc));
	pp_push_attr(st, attr_src("derive", "Clone, Debug"));
	vec_push(out, st);

	PpNode *imp = pp_item_impl(NULL, pp_ty(wrapper));
	Vec non_snake = {0};
	ctx_message_non_snake_attr(msg, &non_snake);
	attrs_all(imp, &non_snake);
	PpNode *ret_self = ty_g2("::core::result::Result", pp_ty("Self"), pp_ty("::buffa::DecodeError"));
	PpNode *sig = pp_sig("decode", ret_self);
	pp_push(sig, arg("bytes", pp_ty("::buffa::bytes::Bytes")));
	static const char *const d1[] = {" Decode an owned view from a [`::buffa::bytes::Bytes`] buffer.", "",
					 " The view borrows directly from the buffer's data; the buffer is", " retained inside the returned handle.",
					 "", " # Errors", "", " Returns [`::buffa::DecodeError`] if the buffer contains invalid", " protobuf data.", NULL};
	pp_push(imp, doc_fn(pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(ok_wrap(wrapper, call_try(e_call1("::buffa::OwnedView::decode", pp_e("bytes"))))), NULL)), d1));
	sig = pp_sig("decode_with_options", ret_self);
	pp_push(sig, arg("bytes", pp_ty("::buffa::bytes::Bytes")));
	pp_push(sig, arg("opts", ty_ref(pp_ty("::buffa::DecodeOptions"))));
	static const char *const d2[] = {" Decode with custom [`::buffa::DecodeOptions`] (recursion limit,", " max message size).", "", " # Errors", "",
					 " Returns [`::buffa::DecodeError`] if the buffer is invalid or", " exceeds the configured limits.", NULL};
	pp_push(imp, doc_fn(pp_item_fn(PP_VIS_PUB, sig,
				       pp_block(s_expr(ok_wrap(wrapper, call_try(e_call2("::buffa::OwnedView::decode_with_options", pp_e("bytes"), pp_e("opts"))))), NULL)),
			    d2));
	sig = pp_sig("from_owned", ret_self);
	pp_push(sig, arg("msg", ty_ref(ty_path_str(owned_path))));
	static const char *const d3[] = {" Build from an owned message via an encode \xe2\x86\x92 decode round-trip.", "", " # Errors", "",
					 " Returns [`::buffa::DecodeError::MessageTooLarge`] if the", " message's encoded size exceeds the 2 GiB protobuf limit, or",
					 " another [`::buffa::DecodeError`] if the re-encoded bytes are", " somehow invalid (should not happen for well-formed messages).", NULL};
	pp_push(imp, doc_fn(pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(ok_wrap(wrapper, call_try(e_call1("::buffa::OwnedView::from_owned", pp_e("msg"))))), NULL)), d3));
	sig = pp_sig("view", ty_ref(ty_lt(view_ident, "'_")));
	pp_push(sig, pp_receiver(true, false, NULL));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(e_meth0(pp_e_field(pp_e("self"), "0"), "reborrow")), NULL));
	pp_push_attr(fn, doc_attr(xcat(" Borrow the full [`", view_ident, "`] with its lifetime tied to `&self`.")));
	pp_push_attr(fn, attr_word("must_use"));
	pp_push(imp, fn);
	sig = pp_sig("to_owned_message", ty_path_str(owned_path));
	pp_push(sig, pp_receiver(true, false, NULL));
	static const char *const d4[] = {" Convert to the owned message type.", "", " Infallible: this type's constructors wire-decode their",
					 " buffer, and a view produced by wire decoding always", " converts. Delegates to [`::buffa::OwnedView::to_owned_message`],",
					 " whose contract also governs handles converted from a raw", " [`::buffa::OwnedView`].", NULL};
	fn = doc_fn(pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(e_meth0(pp_e_field(pp_e("self"), "0"), "to_owned_message")), NULL)), d4);
	pp_push_attr(fn, attr_word("must_use"));
	pp_push(imp, fn);
	sig = pp_sig("bytes", ty_ref(pp_ty("::buffa::bytes::Bytes")));
	pp_push(sig, pp_receiver(true, false, NULL));
	fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(e_meth0(pp_e_field(pp_e("self"), "0"), "bytes")), NULL));
	pp_push_attr(fn, doc_attr(" The underlying bytes buffer."));
	pp_push_attr(fn, attr_word("must_use"));
	pp_push(imp, fn);
	sig = pp_sig("into_bytes", pp_ty("::buffa::bytes::Bytes"));
	pp_push(sig, pp_receiver(false, false, NULL));
	fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(s_expr(e_meth0(pp_e_field(pp_e("self"), "0"), "into_bytes")), NULL));
	pp_push_attr(fn, doc_attr(" Consume the handle, returning the underlying bytes buffer."));
	pp_push_attr(fn, attr_word("must_use"));
	pp_push(imp, fn);
	push_all(imp, &accessors);
	vec_push(out, imp);

	imp = pp_item_impl(pp_path_args(pp_path("::core::convert::From"), pp_angle(ov_static, NULL)), pp_ty(wrapper));
	sig = pp_sig("from", pp_ty("Self"));
	pp_push(sig, arg("inner", ov_static));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call1(wrapper, pp_e("inner"))), NULL)));
	vec_push(out, imp);
	imp = pp_item_impl(pp_path_args(pp_path("::core::convert::From"), pp_angle(pp_ty(wrapper), NULL)), ov_static);
	sig = pp_sig("from", pp_ty("Self"));
	pp_push(sig, arg("wrapper", pp_ty(wrapper)));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(pp_e_field(pp_e("wrapper"), "0")), NULL)));
	vec_push(out, imp);
	imp = pp_item_impl(pp_path_args(pp_path("::core::convert::AsRef"), pp_angle(ov_static, NULL)), pp_ty(wrapper));
	sig = pp_sig("as_ref", ty_ref(ov_static));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(pp_e_ref(false, pp_e_field(pp_e("self"), "0"))), NULL)));
	vec_push(out, imp);
	imp = pp_item_impl(pp_path("::buffa::HasMessageView"), ty_path_str(owned_path));
	PpNode *t = pp_impl_type(0, "View", ty_lt(view_ident, "'a"));
	pp_set_generics(t, generics_a());
	pp_push(imp, t);
	pp_push(imp, pp_impl_type(0, "ViewHandle", pp_ty(wrapper)));
	vec_push(out, imp);
	if (CFG->generate_json)
		view_serialize_owned_wrapper(out, wrapper);
}

/* custom_view_default_impl (only with enum-type overrides) */
static PpNode *custom_view_default_impl(const Scope *vs, const PbMsg *msg, const char *view_ident, bool phantom)
{
	if (!CFG->feature_overrides.n)
		return NULL;
	PpNode *st = pp_e_struct(pp_path("Self"), NULL);
	PpNode *dflt = pp_e_call(pp_e("::core::default::Default::default"), NULL);
	bool custom = false;
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f) || !is_supported_field_type(fd_type(f)))
			continue;
		const char *id = ctx_field_ident(field_name_req(f), fd_number(f));
		PpNode *e = NULL;
		if (fd_label(f) != LABEL_REPEATED && !is_map_field(msg, f)) {
			Feat ff = feat_resolve_field(f, vs->features);
			int ty = effective_type(f, ff);
			if (ty == PT_ENUM && !is_explicit_presence_scalar(f, ty, ff))
				e = open_enum_bare_default_value(f, vs->pkg, ff, vs->nesting);
		}
		custom |= e != NULL;
		pp_push(st, pp_field_value(id, e ? e : dflt));
	}
	if (!custom)
		return NULL;
	for (int idx = 0; idx < pb_count(msg, DP_ONEOF_DECL); idx++) {
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME) || !oneof_members(msg, idx).n)
			continue;
		pp_push(st, pp_field_value(ctx_oneof_ident(dname(o)), dflt));
	}
	if (CFG->preserve_unknown_fields)
		pp_push(st, pp_field_value("__buffa_unknown_fields", dflt));
	int nreq, nbits;
	required_view_fields(vs, msg, &nreq, &nbits);
	for (int w = 0; w < (nbits + 63) / 64; w++)
		pp_push(st, pp_field_value(xfmt("__buffa_required_seen_%d", w), pp_e_lit_int("0")));
	if (phantom)
		pp_push(st, pp_field_value("__buffa_phantom", pp_e("::core::marker::PhantomData")));
	PpNode *imp = pp_item_impl(pp_path("::core::default::Default"), ty_lt(view_ident, "'a"));
	pp_set_generics(imp, generics_a());
	pp_push(imp, pp_item_fn(0, pp_sig("default", pp_ty("Self")), pp_block(s_expr(st), NULL)));
	return imp;
}

/* ---- generate_view_with_nesting ---- */

/* the Debug impl of a view with redacted fields (eager and lazy family) */
static PpNode *view_debug_impl(const char *ident, const Vec *names, const Vec *vals)
{
	PpNode *chain = e_meth1(pp_e("f"), "debug_struct", lit_str(ident));
	for (int i = 0; i < names->n; i++)
		chain = pp_e_method(chain, "field", lit_str(names->v[i]), vals->v[i], NULL);
	PpNode *imp = pp_item_impl(pp_path("::core::fmt::Debug"), ty_lt(ident, "'a"));
	pp_set_generics(imp, generics_a());
	PpNode *sig = pp_sig("fmt", pp_ty("::core::fmt::Result"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("f", ty_ref_mut(pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_meth0(chain, "finish")), NULL)));
	return imp;
}

/* the redacted placeholder value `&::core::format_args!("[REDACTED]")` */
static PpNode *redacted_value(void)
{
	PpNode *fa = pp_macro(pp_path("::core::format_args"), PP_DELIM_PAREN);
	pp_push(fa, pp_tt_lit(pm2_string(str_c(DEBUG_REDACT_PLACEHOLDER))));
	return pp_e_ref(false, pp_e_macro(fa));
}

static void generate_view_serialize(Vec *out, const Scope *vs, const PbMsg *msg, const char *view_ident, const char *view_oneof_prefix,
				    const char **oneof_idents);
static void reflect_view_impls(Vec *out, const Scope *vs, const PbMsg *msg, const char *view_ident, const char *view_oneof_prefix,
			       const char **oneof_idents);

static void generate_view_with_nesting(const Scope *s, const PbMsg *msg, const char *rust_name, Vec *view_top, Vec *view_oneofs)
{
	bool preserve = CFG->preserve_unknown_fields;
	const char **oneof_idents = resolve_oneof_idents(msg);
	const char *view_ident = xcat(rust_name, "View");
	Scope vs = *s;
	vs.nesting = s->nesting + 2;
	char *view_oneof_prefix = ancillary_prefix(ANC_VIEW_ONEOF, s->pkg, s->fqn, vs.nesting);
	char *owned_oneof_prefix = ancillary_prefix(ANC_ONEOF, s->pkg, s->fqn, vs.nesting);
	FeatureGates g = gates();
	int no = pb_count(msg, DP_ONEOF_DECL);

	/* struct fields */
	PpNode *st = pp_item_struct(PP_VIS_PUB, view_ident, PP_F_NAMED);
	pp_set_generics(st, generics_a());
	bool any_redacted = false;
	Vec dbg_names = {0}, dbg_vals = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (!is_supported_field_type(fd_type(f)) || is_real_oneof_member(f))
			continue;
		const char *name = field_name_req(f);
		int number = fd_number(f);
		bool rep = fd_label(f) == LABEL_REPEATED;
		const char *id = ctx_field_ident(name, number);
		Vec doc = {0};
		PpNode *ty;
		if (rep && is_map_field(msg, f)) {
			doc_attrs_with_tag(&doc, ctx_comment(xcat(s->fqn, ".", name)), xfmt("Field %d: `%s` (map)", number, name), s->fqn);
			ty = view_map_type(&vs, msg, f, "'a");
		} else {
			doc_attrs_with_tag(&doc, ctx_comment(xcat(s->fqn, ".", name)), xfmt("Field %d: `%s`", number, name), s->fqn);
			const char *tn = fd_type_name(f);
			if (tn && str_eq(tn, xcat(".", s->fqn)))
				ty = rep ? pp_ty_gen("::buffa::RepeatedView", lt_a(), pp_ty("Self"), NULL) : ty_g1("::buffa::MessageFieldView", pp_ty("Self"));
			else
				ty = rep ? view_repeated_type(&vs, f, "'a") : view_singular_type(&vs, f, "'a");
		}
		PpNode *fld = pp_field(PP_VIS_PUB, id, ty);
		attrs_all(fld, &doc);
		pp_push(st, fld);
		bool red = is_debug_redacted(f);
		any_redacted |= red;
		vec_push(&dbg_names, (void *)(has_prefix(id, "r#") ? id + 2 : id));
		vec_push(&dbg_vals, red ? redacted_value() : pp_e_ref(false, self_f(id)));
	}
	for (int idx = 0; idx < no; idx++) {
		if (!oneof_idents[idx])
			continue;
		Vec fields = oneof_members(msg, idx);
		if (!fields.n)
			continue;
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		const char *fid = ctx_oneof_ident(dname(o));
		pp_push(st, pp_field(PP_VIS_PUB, fid, ty_g1(OPTION_PATH, oneof_ty(view_oneof_prefix, oneof_idents[idx], oneof_view_needs_lifetime(&fields, s->features), "'a"))));
		vec_push(&dbg_names, (void *)(has_prefix(fid, "r#") ? fid + 2 : fid));
		vec_push(&dbg_vals, pp_e_ref(false, self_f(fid)));
	}
	for (int idx = 0; idx < no; idx++)
		generate_oneof_view_enum(view_oneofs, s, msg, idx, oneof_idents);
	int nreq, nbits;
	RequiredViewField *req = required_view_fields(&vs, msg, &nreq, &nbits);
	if (preserve)
		pp_push(st, pp_field(PP_VIS_PUB, "__buffa_unknown_fields", ty_lt("::buffa::UnknownFieldsView", "'a")));
	for (int w = 0; w < (nbits + 63) / 64; w++) {
		PpNode *fld = pp_field(PP_VIS_PUB, xfmt("__buffa_required_seen_%d", w), pp_ty("u64"));
		pp_push_attr(fld, attr_src("doc", "hidden"));
		pp_push(st, fld);
	}
	bool phantom = !message_view_has_borrowing_field(msg, s->features, preserve);
	if (phantom) {
		PpNode *fld = pp_field(PP_VIS_PUB, "__buffa_phantom", pp_ty_gen("::core::marker::PhantomData", ref_lt("'a", pp_ty_tuple(NULL)), NULL));
		pp_push_attr(fld, attr_src("doc", "hidden"));
		pp_push(st, fld);
	}
	Vec has_methods = {0};
	required_has_methods(&has_methods, req, nreq);

	/* decode arms: scalar, repeated (+ map), oneof */
	Vec arms = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f) || fd_label(f) == LABEL_REPEATED || !is_supported_field_type(fd_type(f)))
			continue;
		vec_push(&arms, scalar_decode_arm(&vs, f, pb_has(f, FD_NUMBER) ? required_bit_stmt(req, nreq, (uint32_t)fd_number(f)) : NULL));
	}
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && !is_map_field(msg, f) && is_supported_field_type(fd_type(f)))
			vec_push(&arms, repeated_decode_arm(&vs, f));
	}
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && is_map_field(msg, f))
			vec_push(&arms, map_decode_arm(&vs, msg, f));
	}
	for (int idx = 0; idx < no; idx++) {
		if (!oneof_idents[idx])
			continue;
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		Vec fields = oneof_members(msg, idx);
		oneof_view_decode_arms(&arms, &vs, oneof_idents[idx], dname(o), &fields, view_oneof_prefix);
	}

	const char *owned_path = ctx_rust_type_relative(xcat(".", s->fqn), s->pkg, vs.nesting);
	if (!owned_path)
		fail("%s", CODEGEN_ERROR("owned type for '%s' not resolvable from view tree", s->fqn));
	PpNode *owned_ty = ty_path_str(owned_path);
	bool map_entry = msg_is_map_entry(msg);

	/* Default / Debug */
	Vec doc = {0};
	doc_attrs(&doc, ctx_comment(s->fqn), s->fqn);
	attrs_all(st, &doc);
	PpNode *custom_default = custom_view_default_impl(&vs, msg, view_ident, phantom);
	pp_push_attr(st, attr_src("derive", any_redacted ? (custom_default ? "Clone" : "Clone, Default")
							 : (custom_default ? "Clone, Debug" : "Clone, Debug, Default")));
	Vec non_snake = {0};
	ctx_message_non_snake_attr(msg, &non_snake);
	attrs_all(st, &non_snake);
	vec_push(view_top, st);
	PpNode *va = ty_lt(view_ident, "'a");
	if (any_redacted)
		vec_push(view_top, view_debug_impl(view_ident, &dbg_names, &dbg_vals));
	if (custom_default)
		vec_push(view_top, custom_default);
	if (has_methods.n) {
		PpNode *imp = pp_item_impl(NULL, va);
		pp_set_generics(imp, generics_a());
		attrs_all(imp, &non_snake);
		push_all(imp, &has_methods);
		vec_push(view_top, imp);
	}

	/* impl MessageView */
	PpNode *imp = pp_item_impl(pp_path_args(pp_path("::buffa::MessageView"), pp_angle(lt_a(), NULL)), va);
	pp_set_generics(imp, generics_a());
	pp_push(imp, pp_impl_type(0, "Owned", owned_ty));
	PpNode *res_self = ty_g2("::core::result::Result", pp_ty("Self"), pp_ty("::buffa::DecodeError"));
	PpNode *sig = pp_sig("decode_view", res_self);
	pp_push(sig, arg("buf", pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8")))));
	PpNode *ctx_new = e_meth1(e_call2("::buffa::DecodeContext::new", pp_e("::buffa::RECURSION_LIMIT"), pp_e_ref(false, pp_e("__limit"))),
				  "with_element_memory", pp_e_ref(false, pp_e("__elem")));
	pp_push(imp, pp_item_fn(0, sig,
				pp_block(s_let("__limit", false, e_call1("::core::cell::Cell::new", pp_e("::buffa::DEFAULT_UNKNOWN_FIELD_LIMIT"))),
					 s_let("__elem", false, e_call1("::core::cell::Cell::new", pp_e("::buffa::DEFAULT_ELEMENT_MEMORY_LIMIT"))),
					 s_expr(mv_call2("Self", "decode_view_ctx", pp_e("buf"), ctx_new)), NULL)));
	sig = pp_sig("decode_view_with_ctx", res_self);
	pp_push(sig, arg("buf", pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8")))));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(mv_call2("Self", "decode_view_ctx", pp_e("buf"), pp_e("ctx"))), NULL)));
	sig = pp_sig("merge_view_field", ty_g2("::core::result::Result", pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8"))), pp_ty("::buffa::DecodeError")));
	pp_push(sig, pp_receiver(true, true, NULL));
	pp_push(sig, arg("tag", pp_ty("::buffa::encoding::Tag")));
	pp_push(sig, arg("cur", pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8")))));
	pp_push(sig, arg(preserve ? "before_tag" : "_before_tag", pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8")))));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	PpNode *m = pp_e_match(e_meth0(pp_e("tag"), "field_number"));
	push_all(m, &arms);
	PpNode *db = pp_block(s_semi(call_try(e_call3("::buffa::encoding::skip_field_depth", pp_e("tag"), mut_cur(), e_meth0(pp_e("ctx"), "depth")))), NULL);
	if (preserve) {
		pp_push(db, s_let("span_len", false, pp_e_binary(e_meth0(pp_e("before_tag"), "len"), "-", e_meth0(pp_e("cur"), "len"))));
		pp_push(db, s_semi(call_try(pp_e_method(view_e("__buffa_unknown_fields"), "push_record", pp_e("before_tag"), pp_e("span_len"), pp_e("ctx"), NULL))));
	}
	pp_push(m, pp_arm(pp_p_wild(), NULL, pp_e_block(db)));
	PpNode *let_view = s_let("view", false, pp_e("self"));
	pp_push_attr(let_view, attr_src("allow", "unused_variables"));
	PpNode *fn = pp_item_fn(0, sig,
				pp_block(pp_s_local(pp_p_wild(), pp_e("ctx")), let_view, s_let("cur", true, pp_e("cur")), s_expr(m),
					 s_expr(e_call1("::core::result::Result::Ok", pp_e("cur"))), NULL));
	pp_push_attr(fn, attr_word("inline"));
	pp_push(imp, fn);
	PpNode *res_owned = ty_g2("::core::result::Result", owned_ty, pp_ty("::buffa::DecodeError"));
	sig = pp_sig("to_owned_message", res_owned);
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_meth1(pp_e("self"), "to_owned_from_source", pp_e("None"))), NULL)));
	sig = pp_sig("to_owned_from_source", res_owned);
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("__buffa_src", ty_g1(OPTION_PATH, ty_ref(pp_ty("::buffa::bytes::Bytes")))));
	PpNode *ostruct = pp_e_struct(pp_path(rust_path(owned_path)), pp_e_call(pp_e("::core::default::Default::default"), NULL));
	build_to_owned_fields(ostruct, &vs, msg, view_oneof_prefix, owned_oneof_prefix, oneof_idents);
	fn = pp_item_fn(0, sig,
			pp_block(allow_unused_use("::buffa::alloc::string::ToString as _"), pp_s_local(pp_p_wild(), pp_e("__buffa_src")),
				 s_expr(e_call1("::core::result::Result::Ok", ostruct)), NULL));
	pp_push_attr(fn, attr_src("allow", "clippy::useless_conversion, clippy::needless_update"));
	pp_push(imp, fn);
	vec_push(view_top, imp);

	imp = pp_item_impl(pp_path_args(pp_path("::buffa::ViewEncode"), pp_angle(lt_a(), NULL)), va);
	pp_set_generics(imp, generics_a());
	Vec enc = {0};
	build_view_encode_methods(&enc, msg, preserve, s->features, oneof_idents, view_oneof_prefix, NULL);
	push_all(imp, &enc);
	vec_push(view_top, imp);
	if (CFG->generate_json) {
		Vec ser = {0};
		generate_view_serialize(&ser, &vs, msg, view_ident, view_oneof_prefix, oneof_idents);
		cfg_block(view_top, &ser, g.json);
	}
	vec_push(view_top, message_name_impl(s->pkg, s->fqn, generics_a(), va));
	vec_push(view_top, pp_item_macro(mac_src("::buffa::impl_default_view_instance", PP_DELIM_PAREN, view_ident), NULL));
	vec_push(view_top, pp_item_macro(mac_src("::buffa::impl_view_reborrow", PP_DELIM_PAREN, view_ident), NULL));
	if (!map_entry)
		generate_owned_view_wrapper(view_top, &vs, msg, rust_name, view_ident, owned_path, view_oneof_prefix, oneof_idents);
	if (CFG->generate_reflection && CFG->generate_reflection_vtable && !map_entry) {
		Vec rv = {0};
		reflect_view_impls(&rv, &vs, msg, view_ident, view_oneof_prefix, oneof_idents);
		cfg_const_block(view_top, &rv, g.reflect);
	}
}
