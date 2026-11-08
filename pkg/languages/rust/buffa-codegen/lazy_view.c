/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/lazy_view.rs: the lazy view family (`FooLazyView<'a>`, option
 * `lazy_views`), generated beside the eager views into the `__buffa::lazy_view::` tree. Singular and
 * repeated message fields are recorded as undecoded byte ranges; everything else reuses the eager
 * view machinery of view.c and json.c. The lazy encode statements are in view.c
 * (build_view_encode_methods with a lazy scope). */

/* is_lazy_field: singular/repeated message fields (not groups or DELIMITED) of non-extern types */
static bool is_lazy_field(const Scope *s, const PbMsg *field)
{
	if (effective_type(field, feat_resolve_field(field, s->features)) != PT_MESSAGE)
		return false;
	const char *tn = fd_type_name(field);
	if (!tn)
		return false;
	SplitPath sp;
	return ctx_rust_type_relative_split(tn, s->pkg, s->nesting, &sp) && !sp.is_extern;
}

/* resolve_lazy_view_path: resolve_view_family_path with the "LazyView" suffix */
static char *resolve_lazy_view_path(const Scope *s, const PbMsg *field)
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
	buf_adds(&b, SENTINEL_MOD "::lazy_view::");
	const char *cut = NULL;
	for (const char *p = strstr(sp.within_package, "::"); p; p = strstr(p + 2, "::"))
		cut = p;
	const char *last = sp.within_package;
	if (cut) {
		buf_adds(&b, rust_path(xstrndup(sp.within_package, (size_t)(cut - sp.within_package))));
		buf_adds(&b, "::");
		last = cut + 2;
	}
	buf_adds(&b, make_field_ident(xcat(last, "LazyView")));
	return (char *)buf_str(&b);
}

/* lazy_struct_field: lazy types for message fields, the eager view types for everything else */
static PpNode *lazy_struct_field(const Scope *s, const PbMsg *msg, const PbMsg *field)
{
	const char *name = field_name_req(field);
	int number = fd_number(field);
	const char *id = ctx_field_ident(name, number);
	bool rep = fd_label(field) == LABEL_REPEATED;
	const char *comment = ctx_comment(xcat(s->fqn, ".", name));
	Vec doc = {0};
	PpNode *ty;
	if (rep && is_map_field(msg, field)) {
		doc_attrs_with_tag(&doc, comment, xfmt("Field %d: `%s` (map)", number, name), s->fqn);
		ty = view_map_type(s, msg, field, "'a");
	} else {
		doc_attrs_with_tag(&doc, comment, xfmt("Field %d: `%s`", number, name), s->fqn);
		bool lazy = is_lazy_field(s, field);
		const char *tn = fd_type_name(field);
		bool self_ = tn && str_eq(tn, xcat(".", s->fqn));
		if (lazy) {
			PpNode *elem = self_ ? pp_ty("Self") : ty_lt(resolve_lazy_view_path(s, field), "'a");
			ty = pp_ty_gen(rep ? "::buffa::LazyRepeatedView" : "::buffa::LazyMessageFieldView", lt_a(), elem, NULL);
		} else
			ty = rep ? view_repeated_type(s, field, "'a") : view_singular_type(s, field, "'a");
	}
	PpNode *fld = pp_field(PP_VIS_PUB, id, ty);
	attrs_all(fld, &doc);
	return fld;
}

/* lazy_singular_message_arm / lazy_repeated_message_arm */
static PpNode *lazy_message_arm(const Scope *s, const PbMsg *field, const char *push)
{
	uint32_t number = validated_field_number(field);
	(void)s;
	const char *id = ctx_field_ident(field_name_req(field), fd_number(field));
	PpNode *size_of = pp_e_call(pp_e_path(pp_path_args(pp_path("::core::mem::size_of"), pp_angle(pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8"))), NULL))), NULL);
	return arm_block(number, pp_block(wire_check_ld(), s_let("__sub_ctx", false, call_try(e_meth0(pp_e("ctx"), "descend"))),
					  s_let("sub", false, call_try(e_call1("::buffa::types::borrow_bytes", mut_cur()))),
					  s_semi(call_try(e_meth1(pp_e("ctx"), "register_element_memory", size_of))),
					  s_semi(pp_e_method(view_e(id), push, pp_e("sub"), pp_e("__sub_ctx"), NULL)), NULL));
}

/* the `to_owned_message` initialiser of a lazy field: decode, then convert */
static PpNode *lazy_to_owned(const Scope *s, const PbMsg *field, const char *id, bool rep)
{
	if (rep) {
		PpNode *loop = pp_e_for(pp_p_ident("__r", false, false), e_meth0(self_f(id), "iter"),
					pp_block(s_semi(e_meth1(pp_e("__out"), "push", call_try(e_meth0(call_try(pp_e("__r")), "to_owned_message")))), NULL));
		return pp_e_block(pp_block(s_let("__out", true, e_call1("::buffa::alloc::vec::Vec::with_capacity", e_meth0(self_f(id), "len"))), s_expr(loop),
					   s_expr(pp_e("__out")), NULL));
	}
	PpNode *owned = ty_path_str(resolve_owned_path(s, field));
	bool inl = ctx_field_inline(xcat(".", s->fqn, ".", dname(field)));
	PpNode *args = inl ? pp_angle(owned, ty_g1("::buffa::Inline", owned), NULL) : pp_angle(owned, NULL);
	PpNode *some = pp_e_path(pp_path_push(pp_path_args(pp_path("::buffa::MessageField"), args), "some", NULL));
	PpNode *m = pp_e_match(call_try(e_meth0(self_f(id), "get")));
	pp_push(m, pp_arm(p_core_some(pp_p_ident("v", false, false)), NULL,
			  pp_e_block(pp_block(s_expr(pp_e_call(some, call_try(e_meth0(pp_e("v"), "to_owned_message")), NULL)), NULL))));
	pp_push(m, pp_arm(pp_p_path(pp_path("::core::option::Option::None")), NULL, pp_e_call(pp_e("::buffa::MessageField::none"), NULL)));
	return m;
}

static PpNode *serde_custom_err(void)
{
	return s_semi(pp_e_return(e_call1("::core::result::Result::Err", e_call1_q(pp_ty("__S::Error"), "::serde::ser::Error", "custom", pp_e("__e")))));
}

/* lazy_field_serialize_stmt: decode on serialize; malformed bytes become a serde error */
static PpNode *lazy_field_serialize_stmt(const Scope *s, const PbMsg *field)
{
	const char *name = field_name_req(field);
	const char *json = json_name_of(field, name);
	const char *id = ctx_field_ident(name, fd_number(field));
	PpNode *err_arm = pp_arm(pp_p_tuple_struct(pp_path("::core::result::Result::Err"), pp_p_ident("__e", false, false), NULL), NULL,
				 pp_e_block(pp_block(serde_custom_err(), NULL)));
	if (fd_label(field) == LABEL_REPEATED) {
		PpNode *elem = ty_lt(resolve_lazy_view_path(s, field), "'__a");
		PpNode *ws = pp_item_struct(0, "_WSeq", PP_F_UNNAMED);
		pp_set_generics(ws, generics_lts("'__x", "'__a"));
		pp_push(ws, pp_field(0, NULL, pp_ty_ref("'__x", false, pp_ty_gen("::buffa::LazyRepeatedView", pp_lifetime("'__a"), elem, NULL))));
		PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), pp_ty_gen("_WSeq", pp_lifetime("'_"), pp_lifetime("'__a"), NULL));
		pp_set_generics(imp, generics_lts("'__a", NULL));
		PpNode *sig = sig_generic("serialize", ty_result(pp_ty("__S::Ok"), pp_ty("__S::Error")), generics_tp(NULL, "__S", "::serde::Serializer"));
		pp_push(sig, pp_receiver(true, false, NULL));
		pp_push(sig, arg("__s", pp_ty("__S")));
		PpNode *m = pp_e_match(pp_e("__r"));
		pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::core::result::Result::Ok"), pp_p_ident("__v", false, false), NULL), NULL,
				  call_try(e_meth1(pp_e("__seq"), "serialize_element", pp_e_ref(false, pp_e("__v"))))));
		pp_push(m, err_arm);
		PpNode *self0 = pp_e_field(pp_e("self"), "0");
		PpNode *body = pp_block(pp_s_item(pp_item_use(0, pp_use_tree("serde::ser::SerializeSeq as _"), true)),
					s_let("__seq", true, call_try(e_meth1(pp_e("__s"), "serialize_seq", e_some(e_meth0(self0, "len"))))),
					s_expr(pp_e_for(pp_p_ident("__r", false, false), e_meth0(pp_e_field(pp_e("self"), "0"), "iter"), pp_block(s_expr(m), NULL))),
					s_expr(e_meth0(pp_e("__seq"), "end")), NULL);
		pp_push(imp, pp_item_fn(0, sig, body));
		return stmt_if(not(e_meth0(self_f(id), "is_empty")),
			       pp_block(pp_s_item(ws), pp_s_item(imp), ser_entry("__map", json, pp_e_ref(false, e_call1("_WSeq", pp_e_ref(false, self_f(id))))), NULL));
	}
	PpNode *m = pp_e_match(e_meth0(self_f(id), "get"));
	pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::core::result::Result::Ok"), p_core_some(pp_p_ident("__v", false, false)), NULL), NULL,
			  pp_e_block(pp_block(ser_entry("__map", json, pp_e_ref(false, pp_e("__v"))), NULL))));
	pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::core::result::Result::Ok"), pp_p_path(pp_path("::core::option::Option::None")), NULL), NULL,
			  pp_e_block(pp_block(NULL))));
	pp_push(m, err_arm);
	return s_expr(pp_e_block(pp_block(s_expr(m), NULL)));
}

/* generate_lazy_view_serialize */
static void generate_lazy_view_serialize(Vec *out, const Scope *vs, const PbMsg *msg, const char *lazy_ident, const char *view_oneof_prefix,
					 const char **oneof_idents)
{
	PpNode *body = pp_block(pp_s_item(pp_item_use(0, pp_use_tree("serde::ser::SerializeMap as _"), true)),
				s_let("__map", true, call_try(e_meth1(pp_e("__s"), "serialize_map", e_none()))), NULL);
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f) || !is_supported_field_type(fd_type(f)))
			continue;
		bool map = fd_label(f) == LABEL_REPEATED && is_map_field(msg, f);
		pp_push(body, !map && is_lazy_field(vs, f) ? lazy_field_serialize_stmt(vs, f) : view_field_serialize_stmt(vs, msg, f));
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
	PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), ty_lt(lazy_ident, "'__a"));
	pp_set_generics(imp, generics_lts("'__a", NULL));
	PpNode *sig = sig_generic("serialize", ty_result(pp_ty("__S::Ok"), pp_ty("__S::Error")), generics_tp(NULL, "__S", "::serde::Serializer"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("__s", pp_ty("__S")));
	pp_push(imp, pp_item_fn(0, sig, body));
	static const char *const doc[] = {" Serializes this lazy view as protobuf JSON, decoding deferred", " message fields on the fly. Malformed or over-budget deferred",
					  " bytes surface as a serializer error.", NULL};
	for (int i = 0; doc[i]; i++)
		pp_push_attr(imp, doc_attr(doc[i]));
	vec_push(out, imp);
}

/* ---- the fixed encode entry points of the lazy family ---- */

static PpNode *self_try_encode(PpNode *call)
{
	PpNode *c = pp_e_closure(pp_e_call(pp_e("::buffa::encode_size_overflow"), NULL), false);
	pp_push(c, pp_p_wild());
	return e_meth1(call, "unwrap_or_else", c);
}

static PpNode *new_cache(void) { return s_let("__cache", true, pp_e_call(pp_e("::buffa::SizeCache::new"), NULL)); }
static PpNode *checked_size(void)
{
	return e_call1("::buffa::checked_encode_size", e_meth1(pp_e("self"), "compute_size", pp_e_ref(true, pp_e("__cache"))));
}
static PpNode *write_into(const char *buf)
{
	return s_semi(pp_e_method(pp_e("self"), "write_to", pp_e_ref(true, pp_e("__cache")), buf ? pp_e_ref(true, pp_e(buf)) : pp_e("buf"), NULL));
}
static PpNode *size_match(void)
{
	PpNode *m = pp_e_match(checked_size());
	pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::core::result::Result::Ok"), pp_p_ident("__size", false, false), NULL), NULL,
			  pp_e_cast(pp_e("__size"), pp_ty("usize"))));
	pp_push(m, pp_arm(pp_p_tuple_struct(pp_path("::core::result::Result::Err"), pp_p_wild(), NULL), NULL, pp_e_call(pp_e("::buffa::encode_size_overflow"), NULL)));
	return s_let("__size", false, m);
}

static PpNode *encode_fn(const char *name, PpNode *ret, bool sink, PpNode *body, const char *const *doc, bool inl, bool must_use)
{
	PpNode *sig = pp_sig(name, ret);
	pp_push(sig, pp_receiver(true, false, NULL));
	if (sink)
		pp_push(sig, arg("buf", ty_ref_mut(ty_impl("::buffa::EncodeSink"))));
	PpNode *fn = doc_fn(pp_item_fn(PP_VIS_PUB, sig, body), doc);
	if (inl)
		pp_push_attr(fn, attr_word("inline"));
	if (must_use)
		pp_push_attr(fn, attr_word("must_use"));
	return fn;
}

static void lazy_encode_entry_points(PpNode *imp)
{
	static const char *const d_encode[] = {" Compute size, then write. Primary encode entry point.", "", " # Panics", "",
					       " Panics if the encoded size exceeds the 2 GiB protobuf limit",
					       " ([`::buffa::MAX_MESSAGE_BYTES`]) \xe2\x80\x94 see", " [`try_encode`](Self::try_encode) for the error-returning",
					       " variant.", NULL};
	static const char *const d_try_encode[] = {" Encode, returning an error instead of panicking if the", " encoded size exceeds the 2 GiB protobuf limit",
						   " ([`::buffa::MAX_MESSAGE_BYTES`]).", "", " On `Err`, nothing is written to `buf`.", "", " # Errors", "",
						   " Returns [`::buffa::EncodeError::MessageTooLarge`] if the", " encoded size exceeds the limit.", NULL};
	static const char *const d_len[] = {" Encoded byte size of this view.", "", " # Panics", "", " Panics if the encoded size exceeds the 2 GiB protobuf limit",
					    " ([`::buffa::MAX_MESSAGE_BYTES`]) \xe2\x80\x94 see", " [`try_encoded_len`](Self::try_encoded_len) for the",
					    " error-returning variant.", NULL};
	static const char *const d_try_len[] = {" Encoded byte size, returning an error instead of panicking", " if it exceeds the 2 GiB protobuf limit",
						" ([`::buffa::MAX_MESSAGE_BYTES`]).", "", " # Errors", "", " Returns [`::buffa::EncodeError::MessageTooLarge`] if the",
						" encoded size exceeds the limit.", NULL};
	static const char *const d_vec[] = {" Encode this view to a new `Vec<u8>`.", "", " # Panics", "", " Panics if the encoded size exceeds the 2 GiB protobuf limit",
					    " ([`::buffa::MAX_MESSAGE_BYTES`]) \xe2\x80\x94 see", " [`try_encode_to_vec`](Self::try_encode_to_vec) for the",
					    " error-returning variant. In debug builds, also panics if the", " two encode passes disagree on the byte count.", NULL};
	static const char *const d_try_vec[] = {" Encode to a new `Vec<u8>`, returning an error instead of", " panicking if the encoded size exceeds the 2 GiB protobuf",
						" limit ([`::buffa::MAX_MESSAGE_BYTES`]).", "", " # Errors", "", " Returns [`::buffa::EncodeError::MessageTooLarge`] if the",
						" encoded size exceeds the limit.", "", " # Panics", "", " In debug builds, panics if the two encode passes disagree",
						" on the byte count.", NULL};
	static const char *const d_bytes[] = {" Encode this view to a new [`::buffa::bytes::Bytes`].", "", " # Panics", "",
					      " Panics if the encoded size exceeds the 2 GiB protobuf limit", " ([`::buffa::MAX_MESSAGE_BYTES`]) \xe2\x80\x94 see",
					      " [`try_encode_to_bytes`](Self::try_encode_to_bytes) for the", " error-returning variant. In debug builds, also panics if the",
					      " two encode passes disagree on the byte count.", NULL};
	static const char *const d_try_bytes[] = {" Encode to a new [`::buffa::bytes::Bytes`], returning an", " error instead of panicking if the encoded size exceeds the",
						  " 2 GiB protobuf limit ([`::buffa::MAX_MESSAGE_BYTES`]).", "", " # Errors", "",
						  " Returns [`::buffa::EncodeError::MessageTooLarge`] if the", " encoded size exceeds the limit.", "", " # Panics", "",
						  " In debug builds, panics if the two encode passes disagree", " on the byte count.", NULL};
	PpNode *res_enc = ty_result(pp_ty_tuple(NULL), pp_ty("::buffa::EncodeError"));
	pp_push(imp, encode_fn("encode", NULL, true, pp_block(s_expr(self_try_encode(e_meth1(pp_e("self"), "try_encode", pp_e("buf")))), NULL), d_encode, true, false));
	pp_push(imp, encode_fn("try_encode", res_enc, true,
			       pp_block(new_cache(), s_semi(call_try(checked_size())), write_into(NULL), s_expr(e_ok_unit()), NULL), d_try_encode, false, false));
	pp_push(imp, encode_fn("encoded_len", pp_ty("u32"), false, pp_block(s_expr(self_try_encode(e_meth0(pp_e("self"), "try_encoded_len"))), NULL), d_len,
			       true, true));
	pp_push(imp, encode_fn("try_encoded_len", ty_result(pp_ty("u32"), pp_ty("::buffa::EncodeError")), false,
			       pp_block(s_expr(e_call1("::buffa::checked_encode_size",
						       e_meth1(pp_e("self"), "compute_size", pp_e_ref(true, pp_e_call(pp_e("::buffa::SizeCache::new"), NULL))))),
					NULL),
			       d_try_len, false, false));
	static const char *const bufs[2] = {"::buffa::alloc::vec::Vec::with_capacity", "::buffa::bytes::BytesMut::with_capacity"};
	for (int k = 0; k < 2; k++) {
		bool bytes = k == 1;
		PpNode *ret = bytes ? pp_ty("::buffa::bytes::Bytes") : pp_ty_gen("::buffa::alloc::vec::Vec", pp_ty("u8"), NULL);
		PpNode *tail = bytes ? e_meth0(pp_e("__buf"), "freeze") : pp_e("__buf");
		PpNode *two_pass = s_semi(e_call2("::buffa::debug_assert_two_pass", e_meth0(pp_e("__buf"), "len"), pp_e("__size")));
		pp_push(imp, encode_fn(bytes ? "encode_to_bytes" : "encode_to_vec", ret, false,
				       pp_block(new_cache(), size_match(), s_let("__buf", true, e_call1(bufs[k], pp_e("__size"))), write_into("__buf"), two_pass,
						s_expr(tail), NULL),
				       bytes ? d_bytes : d_vec, true, true));
		ret = bytes ? pp_ty("::buffa::bytes::Bytes") : pp_ty_gen("::buffa::alloc::vec::Vec", pp_ty("u8"), NULL);
		tail = bytes ? e_meth0(pp_e("__buf"), "freeze") : pp_e("__buf");
		two_pass = s_semi(e_call2("::buffa::debug_assert_two_pass", e_meth0(pp_e("__buf"), "len"), pp_e("__size")));
		pp_push(imp, encode_fn(bytes ? "try_encode_to_bytes" : "try_encode_to_vec", ty_result(ret, pp_ty("::buffa::EncodeError")), false,
				       pp_block(new_cache(), s_let("__size", false, pp_e_cast(call_try(checked_size()), pp_ty("usize"))),
						s_let("__buf", true, e_call1(bufs[k], pp_e("__size"))), write_into("__buf"), two_pass,
						s_expr(e_call1("::core::result::Result::Ok", tail)), NULL),
				       bytes ? d_try_bytes : d_try_vec, false, false));
	}
}

/* ---- generate_lazy_view_with_nesting ---- */

static PpNode *res_self(void) { return ty_result(pp_ty("Self"), pp_ty("::buffa::DecodeError")); }
static PpNode *res_unit(void) { return ty_result(pp_ty_tuple(NULL), pp_ty("::buffa::DecodeError")); }
static PpNode *buf_ty(void) { return pp_ty_ref("'a", false, pp_ty_slice(pp_ty("u8"))); }

static void generate_lazy_view_with_nesting(const Scope *s, const PbMsg *msg, const char *rust_name, Vec *out)
{
	bool preserve = CFG->preserve_unknown_fields;
	const char **oneof_idents = resolve_oneof_idents(msg);
	const char *lazy_ident = xcat(rust_name, "LazyView");
	Scope vs = *s;
	vs.nesting = s->nesting + 2;
	char *view_oneof_prefix = ancillary_prefix(ANC_VIEW_ONEOF, s->pkg, s->fqn, vs.nesting);
	char *owned_oneof_prefix = ancillary_prefix(ANC_ONEOF, s->pkg, s->fqn, vs.nesting);
	int no = pb_count(msg, DP_ONEOF_DECL);

	/* struct fields */
	PpNode *st = pp_item_struct(PP_VIS_PUB, lazy_ident, PP_F_NAMED);
	pp_set_generics(st, generics_a());
	bool any_redacted = false;
	Vec dbg_names = {0}, dbg_vals = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (!is_supported_field_type(fd_type(f)) || is_real_oneof_member(f))
			continue;
		pp_push(st, lazy_struct_field(&vs, msg, f));
		const char *id = ctx_field_ident(dname(f), fd_number(f));
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
	int nreq, nbits;
	RequiredViewField *req = required_view_fields(&vs, msg, &nreq, &nbits);

	/* decode arms: scalar, repeated (+ map), oneof */
	Vec arms = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f) || fd_label(f) == LABEL_REPEATED || !is_supported_field_type(fd_type(f)))
			continue;
		if (is_lazy_field(&vs, f))
			vec_push(&arms, lazy_message_arm(&vs, f, "push_fragment"));
		else
			vec_push(&arms, scalar_decode_arm(&vs, f, pb_has(f, FD_NUMBER) ? required_bit_stmt(req, nreq, (uint32_t)fd_number(f)) : NULL));
	}
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (fd_label(f) == LABEL_REPEATED && !is_map_field(msg, f) && is_supported_field_type(fd_type(f)))
			vec_push(&arms, is_lazy_field(&vs, f) ? lazy_message_arm(&vs, f, "push_bytes") : repeated_decode_arm(&vs, f));
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

	/* to_owned_message field initialisers */
	PpNode *ostruct = NULL;
	Vec owned_inits = {0};
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		const char *name = field_name_req(f);
		const char *id = ctx_field_ident(name, fd_number(f));
		bool rep = fd_label(f) == LABEL_REPEATED;
		if (rep && is_map_field(msg, f)) {
			vec_push(&owned_inits, pp_field_value(id, map_to_owned_expr(&vs, msg, f, id)));
			continue;
		}
		int ty = effective_type(f, s->features);
		PpNode *init = is_lazy_field(&vs, f) ? lazy_to_owned(&vs, f, id, rep) : rep ? repeated_to_owned(ty, id) : singular_to_owned(&vs, f, ty, id);
		vec_push(&owned_inits, pp_field_value(id, init));
	}

	Vec enc = {0};
	build_view_encode_methods(&enc, msg, preserve, s->features, oneof_idents, view_oneof_prefix, &vs);
	PpNode *va = ty_lt(lazy_ident, "'a");
	PpNode *name_impl = message_name_impl(s->pkg, s->fqn, generics_a(), va);
	Vec ser = {0};
	if (CFG->generate_json) {
		Vec si = {0};
		generate_lazy_view_serialize(&si, &vs, msg, lazy_ident, view_oneof_prefix, oneof_idents);
		cfg_block(&ser, &si, gates().json);
	}
	bool phantom = !message_view_has_borrowing_field(msg, s->features, preserve);
	const char *owned_path = ctx_rust_type_relative(xcat(".", s->fqn), s->pkg, vs.nesting);
	if (!owned_path)
		fail("%s", CODEGEN_ERROR("owned type for '%s' not resolvable from lazy view tree", s->fqn));
	PpNode *owned_ty = ty_path_str(owned_path);

	if (preserve)
		pp_push(st, pp_field(PP_VIS_PUB, "__buffa_unknown_fields", ty_lt("::buffa::UnknownFieldsView", "'a")));
	for (int w = 0; w < (nbits + 63) / 64; w++) {
		PpNode *fld = pp_field(PP_VIS_PUB, xfmt("__buffa_required_seen_%d", w), pp_ty("u64"));
		pp_push_attr(fld, attr_src("doc", "hidden"));
		pp_push(st, fld);
	}
	if (phantom) {
		PpNode *fld = pp_field(PP_VIS_PUB, "__buffa_phantom", pp_ty_gen("::core::marker::PhantomData", ref_lt("'a", pp_ty_tuple(NULL)), NULL));
		pp_push_attr(fld, attr_src("doc", "hidden"));
		pp_push(st, fld);
	}
	pp_push_attr(st, doc_attr(xfmt(" Lazy view of `%s`: nested and repeated message fields are\n recorded as undecoded byte ranges and decoded on access. See\n "
				       "[`::buffa::LazyMessageView`] for the deferred-validation contract;\n the eager, whole-tree-validated counterpart is "
				       "named `%sView`.\n\n Oneof variants, map values, groups, and extern-typed fields (e.g.\n well-known types) hold "
				       "eagerly-decoded `%sView`-family\n types; only singular/repeated message fields defer.\n\n # Examples\n\n "
				       "```rust,ignore\n use buffa::LazyMessageView;\n\n let view = %sLazyView::decode_lazy(&bytes)?;\n ```",
				       s->fqn, rust_name, rust_name, rust_name)));
	PpNode *custom_default = custom_view_default_impl(&vs, msg, lazy_ident, phantom);
	pp_push_attr(st, attr_src("derive", any_redacted ? (custom_default ? "Clone" : "Clone, Default")
							 : (custom_default ? "Clone, Debug" : "Clone, Debug, Default")));
	Vec non_snake = {0};
	ctx_message_non_snake_attr(msg, &non_snake);
	attrs_all(st, &non_snake);
	vec_push(out, st);
	if (any_redacted)
		vec_push(out, view_debug_impl(lazy_ident, &dbg_names, &dbg_vals));
	if (custom_default)
		vec_push(out, custom_default);

	/* inherent impl: _decode_lazy_ctx, _merge_lazy, has_* */
	PpNode *imp = pp_item_impl(NULL, va);
	pp_set_generics(imp, generics_a());
	attrs_all(imp, &non_snake);
	PpNode *sig = pp_sig("_decode_lazy_ctx", res_self());
	pp_push(sig, arg("buf", buf_ty()));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig,
				pp_block(s_let("view", true, pp_e_call(pp_e("Self::default"), NULL)),
					 s_semi(call_try(pp_e_method(pp_e("view"), "_merge_lazy", pp_e("buf"), pp_e("ctx"), NULL))),
					 s_expr(e_call1("::core::result::Result::Ok", pp_e("view"))), NULL));
	static const char *const d_decode[] = {" Decode from `buf` under the limits carried by `ctx`, recording", " nested/repeated message fields as byte ranges.", "",
					       " **Not part of the public API.**", NULL};
	doc_fn(fn, d_decode);
	pp_push_attr(fn, attr_src("doc", "hidden"));
	pp_push(imp, fn);
	sig = pp_sig("_merge_lazy", res_unit());
	pp_push(sig, pp_receiver(true, true, NULL));
	pp_push(sig, arg("buf", buf_ty()));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	PpNode *m = pp_e_match(e_meth0(pp_e("tag"), "field_number"));
	push_all(m, &arms);
	PpNode *db = pp_block(s_semi(call_try(e_call3("::buffa::encoding::skip_field_depth", pp_e("tag"), mut_cur(), e_meth0(pp_e("ctx"), "depth")))), NULL);
	if (preserve) {
		pp_push(db, s_let("span_len", false, pp_e_binary(e_meth0(pp_e("before_tag"), "len"), "-", e_meth0(pp_e("cur"), "len"))));
		pp_push(db, s_semi(call_try(pp_e_method(view_e("__buffa_unknown_fields"), "push_record", pp_e("before_tag"), pp_e("span_len"), pp_e("ctx"), NULL))));
	}
	pp_push(m, pp_arm(pp_p_wild(), NULL, pp_e_block(db)));
	PpNode *wb = pp_block(NULL);
	if (preserve)
		pp_push(wb, s_let("before_tag", false, pp_e("cur")));
	pp_push(wb, s_let("tag", false, call_try(e_call1("::buffa::encoding::Tag::decode", mut_cur()))));
	pp_push(wb, s_expr(m));
	PpNode *let_view = s_let("view", false, pp_e("self"));
	pp_push_attr(let_view, attr_src("allow", "unused_variables"));
	fn = pp_item_fn(PP_VIS_PUB, sig,
			pp_block(pp_s_local(pp_p_wild(), pp_e("ctx")), let_view, pp_s_local(pp_p_type(pp_p_ident("cur", false, true), buf_ty()), pp_e("buf")),
				 s_expr(pp_e_while(not(e_meth0(pp_e("cur"), "is_empty")), wb)), s_expr(e_ok_unit()), NULL));
	static const char *const d_merge[] = {" Merge fields from `buf` into this view (proto merge semantics;", " deferred message fragments accumulate).", "",
					      " **Not part of the public API.**", NULL};
	doc_fn(fn, d_merge);
	pp_push_attr(fn, attr_src("doc", "hidden"));
	pp_push(imp, fn);
	Vec has_methods = {0};
	required_has_methods(&has_methods, req, nreq);
	push_all(imp, &has_methods);
	vec_push(out, imp);

	/* impl LazyMessageView */
	imp = pp_item_impl(pp_path_args(pp_path("::buffa::LazyMessageView"), pp_angle(lt_a(), NULL)), va);
	pp_set_generics(imp, generics_a());
	pp_push(imp, pp_impl_type(0, "Owned", owned_ty));
	sig = pp_sig("decode_lazy", res_self());
	pp_push(sig, arg("buf", buf_ty()));
	PpNode *ctx_new = e_meth1(e_call2("::buffa::DecodeContext::new", pp_e("::buffa::RECURSION_LIMIT"), pp_e_ref(false, pp_e("__limit"))),
				  "with_element_memory", pp_e_ref(false, pp_e("__elem")));
	pp_push(imp, pp_item_fn(0, sig,
				pp_block(s_let("__limit", false, e_call1("::core::cell::Cell::new", pp_e("::buffa::DEFAULT_UNKNOWN_FIELD_LIMIT"))),
					 s_let("__elem", false, e_call1("::core::cell::Cell::new", pp_e("::buffa::DEFAULT_ELEMENT_MEMORY_LIMIT"))),
					 s_expr(e_call2("Self::_decode_lazy_ctx", pp_e("buf"), ctx_new)), NULL)));
	sig = pp_sig("decode_lazy_with_ctx", res_self());
	pp_push(sig, arg("buf", buf_ty()));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call2("Self::_decode_lazy_ctx", pp_e("buf"), pp_e("ctx"))), NULL)));
	sig = pp_sig("merge_lazy", res_unit());
	pp_push(sig, pp_receiver(true, true, NULL));
	pp_push(sig, arg("buf", buf_ty()));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(pp_e_method(pp_e("self"), "_merge_lazy", pp_e("buf"), pp_e("ctx"), NULL)), NULL)));
	sig = pp_sig("to_owned_message", ty_result(owned_ty, pp_ty("::buffa::DecodeError")));
	pp_push(sig, pp_receiver(true, false, NULL));
	ostruct = pp_e_struct(pp_path(rust_path(owned_path)), pp_e_call(pp_e("::core::default::Default::default"), NULL));
	push_all(ostruct, &owned_inits);
	build_oneof_to_owned_fields(ostruct, &vs, msg, view_oneof_prefix, owned_oneof_prefix, oneof_idents);
	PpNode *src_ty = ty_g1(OPTION_PATH, ty_ref(pp_ty("::buffa::bytes::Bytes")));
	fn = pp_item_fn(0, sig,
			pp_block(allow_unused_use("::buffa::alloc::string::ToString as _"), allow_unused_use("::buffa::MessageView as _"),
				 pp_s_local(pp_p_type(pp_p_ident("__buffa_src", false, false), src_ty), e_none()),
				 pp_s_local(pp_p_wild(), pp_e("__buffa_src")), s_expr(e_call1("::core::result::Result::Ok", ostruct)), NULL));
	pp_push_attr(fn, attr_src("allow", "clippy::useless_conversion, clippy::needless_update"));
	pp_push(imp, fn);
	vec_push(out, imp);

	/* inherent encode impl */
	imp = pp_item_impl(NULL, va);
	pp_set_generics(imp, generics_a());
	static const char *const d_enc[] = {" Re-encoding: recorded fragments are replayed byte-for-byte",
					    " **without validation** \xe2\x80\x94 wire-equivalent to the merged value, and",
					    " a never-accessed malformed deferred field round-trips silently.",
					    " Inherent rather than [`::buffa::ViewEncode`] (whose `MessageView`",
					    " supertrait carries the eager whole-tree-validated contract); the",
					    " fuller `ViewEncode` set (`encode_length_delimited`,", " `encode_with_cache`) lives on the eager view.", NULL};
	for (int i = 0; d_enc[i]; i++)
		pp_push_attr(imp, doc_attr(d_enc[i]));
	push_all(imp, &enc);
	lazy_encode_entry_points(imp);
	vec_push(out, imp);
	vcat(out, &ser);
	vec_push(out, name_impl);
}
