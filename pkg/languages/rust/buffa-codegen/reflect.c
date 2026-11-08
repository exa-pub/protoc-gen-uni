/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/reflect.rs (the embedded FileDescriptorSet, the descriptor pool
 * module, the package-root re-exports, the vtable Reflectable impl), src/reflect_owned.rs
 * (ReflectMessage for owned messages) and src/reflect_view.rs (ReflectMessage for views). Bridge
 * mode and the shared descriptor pool are not ported (bc_generate_files rejects them). */

#define VR "::buffa_descriptor::reflect::ValueRef"
#define COW "::buffa_descriptor::reflect::ReflectCow"

static bool skip_source_info(void *ctx, const PbMsg *m, uint32_t num)
{
	(void)ctx;
	return m->def == &pbd_FileDescriptorProto && num == FDP_SOURCE_CODE_INFO;
}

/* encode_fds_once: FileDescriptorSet { file: every proto_file without source_code_info } */
static Str encode_fds_once(void)
{
	Buf out = {0};
	for (int i = 0; i < cx.n_files; i++) {
		Buf b = {0};
		pb_encode(cx.files[i], &b, skip_source_info, NULL);
		pb_put_bytes(&out, 1, buf_view(&b));
	}
	return (Str){out.p ? out.p : "", out.n};
}

static PpNode *reflect_doc(PpNode *n, const char *const *lines)
{
	for (int i = 0; lines[i]; i++)
		pp_push_attr(n, doc_attr(lines[i]));
	return n;
}

static PpNode *arc_pool_ty(void) { return ty_g1("::buffa::alloc::sync::Arc", pp_ty("::buffa_descriptor::DescriptorPool")); }

static PpNode *static_once_lock(const char *name, PpNode *ty)
{
	return pp_s_item(pp_item_static(0, false, name, ty_g1("::std::sync::OnceLock", ty), pp_e_call(pp_e("::std::sync::OnceLock::new"), NULL)));
}

static void reflect_pool_module(Vec *out, Str fds)
{
	PpNode *m = pp_item_mod(PP_VIS_PUB, "reflect", true);
	static const char *const mdoc[] = {" Reflection support: embedded descriptor pool shared by this", " package's [`Reflectable`](::buffa_descriptor::reflect::Reflectable)",
					   " and `ReflectMessage` impls (bridge and vtable mode alike).", NULL};
	reflect_doc(m, mdoc);
	PpNode *c = pp_item_const(PP_VIS_PUB, "FILE_DESCRIPTOR_SET_BYTES", ty_ref(pp_ty_slice(pp_ty("u8"))),
				  pp_e_lit(pp_lit(PP_LIT_BYTE_STR, pm2_byte_string(fds))));
	static const char *const cdoc[] = {" The serialized `FileDescriptorSet` for this codegen run,", " including transitive dependencies, with `source_code_info`",
					   " stripped. Used to build the runtime", " [`DescriptorPool`](::buffa_descriptor::DescriptorPool);",
					   " also suitable for shipping the schema over the wire.", " Re-exported at the package root — prefer that path over",
					   " going through `__buffa`.", NULL};
	pp_push(m, reflect_doc(c, cdoc));
	PpNode *len = e_meth1(e_meth1(e_meth0(pp_e("FILE_DESCRIPTOR_SET_BYTES"), "len"), "saturating_mul", pp_e_lit_int("64")), "max",
			      pp_e("::buffa::DEFAULT_ELEMENT_MEMORY_LIMIT"));
	PpNode *opts = e_meth1(pp_e_call(pp_e("::buffa::DecodeOptions::new"), NULL), "with_element_memory_limit", len);
	PpNode *dec = e_meth1(e_call2("::buffa_descriptor::DescriptorPool::decode_with_options", pp_e("FILE_DESCRIPTOR_SET_BYTES"), pp_e_ref(false, pp_e("options"))),
			      "expect", lit_str("buffa-codegen emitted a decodable FileDescriptorSet"));
	PpNode *init = pp_e_closure(pp_e_block(pp_block(s_let("options", false, opts), s_expr(e_call1("::buffa::alloc::sync::Arc::new", dec)), NULL)), false);
	PpNode *sig = pp_sig("descriptor_pool", pp_ty_ref("'static", false, arc_pool_ty()));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, sig, pp_block(static_once_lock("POOL", arc_pool_ty()), s_expr(e_meth1(pp_e("POOL"), "get_or_init", init)), NULL));
	static const char *const fdoc[] = {" The lazily-built descriptor pool for this package's", " `Reflectable` impls. Built from",
					   " [`FILE_DESCRIPTOR_SET_BYTES`] on first access.", "",
					   " The element-memory bound is derived from the embedded length",
					   " rather than the untrusted-input default, which is sized for",
					   " wire input and which a schema of a few hundred `.proto` files",
					   " exceeds — descriptor types being wide structs. Scaling with the",
					   " input keeps the bound correct at any schema size while staying",
					   " finite, so corrupt embedded bytes still fail rather than", " exhausting memory.", "",
					   " The scaled bound is floored at",
					   " [`DEFAULT_ELEMENT_MEMORY_LIMIT`](::buffa::DEFAULT_ELEMENT_MEMORY_LIMIT),",
					   " so it is never tighter than the untrusted-input default.", "", " # Panics", "",
					   " Panics on first access if the embedded bytes are malformed —",
					   " they're emitted by `buffa-codegen` from the same descriptors",
					   " it generated this code from, so a panic indicates a codegen", " bug, not consumer input.", NULL};
	pp_push(m, reflect_doc(fn, fdoc));
	vec_push(out, m);
}

static void reflect_reexports(Vec *out, const char *gate)
{
	static const char *const d1[] = {"The lazily-built descriptor pool for this package's", "`Reflectable` impls. Re-exported from `__buffa::reflect`.", NULL};
	static const char *const d2[] = {"The serialized `FileDescriptorSet` this package's descriptor", "pool is built from (`source_code_info` stripped).",
					 "Re-exported from `__buffa::reflect`.", NULL};
	const char *names[] = {"descriptor_pool", "FILE_DESCRIPTOR_SET_BYTES"};
	const char *const *docs[] = {d1, d2};
	for (int i = 0; i < 2; i++) {
		PpNode *u = pp_item_use(PP_VIS_PUB, pp_use_tree(xcat("self::" SENTINEL_MOD "::reflect::", names[i])), false);
		reflect_doc(u, docs[i]);
		pp_push_attr(u, attr_src("doc", "inline"));
		Vec one_ = one(u);
		cfg_block(out, &one_, gate);
	}
}

static const char *scalar_variant(int ty)
{
	switch (ty) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return "I32";
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return "I64";
	case PT_UINT32: case PT_FIXED32: return "U32";
	case PT_UINT64: case PT_FIXED64: return "U64";
	case PT_FLOAT: return "F32";
	case PT_DOUBLE: return "F64";
	default: return "Bool";
	}
}

static PpNode *scalar_default(int ty)
{
	if (ty == PT_BOOL)
		return pp_e_lit_bool(false);
	if (ty == PT_FLOAT || ty == PT_DOUBLE)
		return pp_e_lit(pp_lit(PP_LIT_FLOAT, "0.0"));
	return pp_e_lit_int("0");
}

static PpNode *vr(const char *variant, PpNode *arg) { return e_call1(xcat(VR "::", variant), arg); }
static PpNode *num_pat(const PbMsg *f) { return pp_p_lit(lit_u32((uint32_t)fd_number(f))); }
static PpNode *map_or_e(const char *id)
{
	return pp_e_method(self_f(id), "map_or", pp_e_lit_int("0"), closure1("e", e_meth0(pp_e("e"), "to_i32")), NULL);
}

static PpNode *debug_assert_arm(const char *what)
{
	PpNode *m = pp_macro(pp_path("::core::debug_assert"), PP_DELIM_PAREN);
	pp_tt_push_src(m, "false,");
	pp_push(m, pp_tt_lit(pm2_string(str_c(xcat("field number {} is not a member of this ", what, " reflect get()")))));
	pp_tt_push_src(m, ", field.number(),");
	return pp_arm(pp_p_wild(), NULL, pp_e_block(pp_block(s_semi(pp_e_macro(m)), s_expr(vr("Bool", pp_e_lit_bool(false))), NULL)));
}

static PpNode *field_descriptor_arg(void) { return arg("field", ty_ref(pp_ty("::buffa_descriptor::FieldDescriptor"))); }

static PpNode *oneof_get_arm(const PbMsg *f, const char *fid, const char *qenum, const char *variant, PpNode *active, PpNode *dflt)
{
	PpNode *m = pp_e_match(pp_e_ref(false, self_f(fid)));
	pp_push(m, pp_arm(p_core_some(pp_p_tuple_struct(pp_path(xcat(qenum, "::", variant)), pp_p_ident("v", false, false), NULL)), NULL, active));
	pp_push(m, pp_arm(pp_p_wild(), NULL, dflt));
	return pp_arm(num_pat(f), NULL, m);
}

static PpNode *oneof_has_arm(const PbMsg *f, const char *fid, const char *qenum, const char *variant)
{
	PpNode *m = pp_macro(pp_path("::core::matches"), PP_DELIM_PAREN);
	pp_tt_push_src(m, "&self.");
	pp_push(m, pp_tt_ident(fid));
	pp_tt_push_src(m, ", ::core::option::Option::Some(");
	PpNode *g = (PpNode *)m->list.v[m->list.n - 1]; /* the ( group */
	pp_tt_push_path(g, qenum);
	pp_tt_push_src(g, "::");
	pp_push(g, pp_tt_ident(variant));
	pp_tt_push_src(g, "(_)");
	return pp_arm(num_pat(f), NULL, pp_e_macro(m));
}

/* the get/has arms shared by owned and view reflection */
static void reflect_arms(PpNode *get_m, PpNode *has_m, const PbMsg *msg, const Scope *s, const char **oneof_idents, const char *oneof_prefix, bool view)
{
	Feat features = s->features;
	for (int i = 0; i < n_fields(msg); i++) {
		const PbMsg *f = field_at(msg, i);
		if (is_real_oneof_member(f))
			continue;
		int ty = effective_type(f, features);
		if (!is_supported_field_type(ty))
			continue;
		const char *name = field_name_req(f);
		const char *id = ctx_field_ident(name, fd_number(f));
		bool rep = fd_label(f) == LABEL_REPEATED;
		if (rep && is_map_field(msg, f)) {
			pp_push(get_m, pp_arm(num_pat(f), NULL, vr("Map", pp_e_ref(false, self_f(id)))));
			pp_push(has_m, pp_arm(num_pat(f), NULL, view ? not(e_call1("::buffa::MapView::is_empty", pp_e_ref(false, self_f(id)))) : not(e_meth0(self_f(id), "is_empty"))));
			continue;
		}
		if (rep) {
			pp_push(get_m, pp_arm(num_pat(f), NULL, vr("List", pp_e_ref(false, self_f(id)))));
			pp_push(has_m, pp_arm(num_pat(f), NULL, view ? not(e_call1("::buffa::RepeatedView::is_empty", pp_e_ref(false, self_f(id)))) : not(e_meth0(self_f(id), "is_empty"))));
			continue;
		}
		Feat ff = feat_resolve_field(f, features);
		PpNode *get, *has;
		if (is_explicit_presence_scalar(f, ty, ff)) {
			has = e_meth0(self_f(id), "is_some");
			if (ty == PT_STRING)
				get = vr("String", view ? e_meth1(self_f(id), "unwrap_or", lit_str("")) : e_meth1(e_meth0(self_f(id), "as_deref"), "unwrap_or", lit_str("")));
			else if (ty == PT_BYTES)
				get = vr("Bytes", view ? e_meth1(self_f(id), "unwrap_or", pp_e_ref(false, pp_e_array(NULL)))
						       : e_meth1(e_meth0(self_f(id), "as_deref"), "unwrap_or", pp_e_ref(false, pp_e_array(NULL))));
			else if (ty == PT_ENUM)
				get = vr("EnumNumber", map_or_e(id));
			else
				get = vr(scalar_variant(ty), e_meth1(self_f(id), "unwrap_or", scalar_default(ty)));
		} else {
			switch (ty) {
			case PT_STRING:
				get = vr("String", view ? self_f(id) : pp_e_ref(false, self_f(id)));
				has = not(e_meth0(self_f(id), "is_empty"));
				break;
			case PT_BYTES:
				get = vr("Bytes", view ? self_f(id) : pp_e_ref(false, pp_e_index(self_f(id), pp_e_range(NULL, NULL, false))));
				has = not(e_meth0(self_f(id), "is_empty"));
				break;
			case PT_MESSAGE: case PT_GROUP:
				get = view ? vr("Message", e_call1(COW "::Borrowed", pp_e_ref(false, pp_e_unary("*", self_f(id)))))
					   : vr("Message", e_call1("::buffa_descriptor::reflect::Reflectable::reflect", pp_e_ref(false, pp_e_unary("*", self_f(id)))));
				has = e_meth0(self_f(id), "is_set");
				break;
			case PT_ENUM: {
				get = vr("EnumNumber", e_meth0(self_f(id), "to_i32"));
				PpNode *d = NULL;
				if (is_closed_enum(ff))
					has = pp_e_binary(self_f(id), "!=", pp_e_call(pp_e("::core::default::Default::default"), NULL));
				else if (!view && (d = open_enum_bare_default_value(f, s->pkg, ff, s->nesting)))
					has = pp_e_binary(self_f(id), "!=", d);
				else
					has = pp_e_binary(e_meth0(self_f(id), "to_i32"), "!=", pp_e_lit_int("0"));
				break;
			}
			default:
				get = vr(scalar_variant(ty), self_f(id));
				has = ty == PT_BOOL ? self_f(id)
				      : ty == PT_FLOAT || ty == PT_DOUBLE ? pp_e_binary(self_f(id), "!=", pp_e_lit(pp_lit(PP_LIT_FLOAT, "0.0")))
									  : pp_e_binary(self_f(id), "!=", pp_e_lit_int("0"));
			}
		}
		if (view && is_required_field(f, features) && pb_has(f, FD_NUMBER))
			has = e_meth0(pp_e("self"), xcat("has_", ctx_field_rust_name(name, fd_number(f))));
		pp_push(get_m, pp_arm(num_pat(f), NULL, get));
		pp_push(has_m, pp_arm(num_pat(f), NULL, has));
	}
	for (int idx = 0; idx < pb_count(msg, DP_ONEOF_DECL); idx++) {
		if (!oneof_idents[idx])
			continue;
		const PbMsg *o = pb_msg_at(msg, DP_ONEOF_DECL, idx);
		if (!pb_has(o, ODP_NAME))
			fail("missing required descriptor field: oneof.name");
		const char *oneof_name = dname(o);
		const char *fid = ctx_oneof_ident(oneof_name);
		const char *qenum = xcat(oneof_prefix, oneof_idents[idx]);
		for (int i = 0; i < n_fields(msg); i++) {
			const PbMsg *f = field_at(msg, i);
			if (!is_real_oneof_member(f) || fd_oneof(f) != idx)
				continue;
			const char *name = field_name_req(f);
			const char *variant = oneof_variant_ident(name);
			int ty = effective_type(f, features);
			PpNode *active, *dflt;
			switch (ty) {
			case PT_STRING:
				active = vr("String", pp_e("v"));
				dflt = vr("String", lit_str(""));
				break;
			case PT_BYTES:
				active = vr("Bytes", view ? pp_e("v") : pp_e_ref(false, pp_e_index(pp_e("v"), pp_e_range(NULL, NULL, false))));
				dflt = vr("Bytes", pp_e_ref(false, pp_e_array(NULL)));
				break;
			case PT_MESSAGE: case PT_GROUP:
				if (view) {
					active = vr("Message", e_call1(COW "::Borrowed", pp_e_ref(false, pp_e_unary("*", pp_e_unary("*", pp_e("v"))))));
					PpNode *vt = resolve_view_ty(s, f, "'a");
					dflt = vr("Message", e_call1(COW "::Borrowed", pp_e_call(e_qpath(vt, "::buffa::view::DefaultViewInstance", "default_view_instance"), NULL)));
				} else {
					const char *tn = fd_type_name(f);
					if (!tn)
						fail("missing required descriptor field: field.type_name");
					const char *op = ctx_rust_type_relative(tn, s->pkg, s->nesting);
					if (!op)
						fail("%s", CODEGEN_ERROR("owned type for oneof message '%s' not resolvable", tn));
					bool boxed = variant_boxed(ty, xcat(".", s->fqn, ".", oneof_name, ".", name));
					active = vr("Message", e_call1("::buffa_descriptor::reflect::Reflectable::reflect",
								       boxed ? pp_e_ref(false, pp_e_unary("*", pp_e_unary("*", pp_e("v")))) : pp_e("v")));
					dflt = vr("Message", e_call1("::buffa_descriptor::reflect::Reflectable::reflect",
								     pp_e_call(e_qpath(ty_path_str(op), "::buffa::DefaultInstance", "default_instance"), NULL)));
				}
				break;
			case PT_ENUM:
				active = vr("EnumNumber", e_meth0(pp_e("v"), "to_i32"));
				dflt = vr("EnumNumber", pp_e_lit_int("0"));
				break;
			default:
				active = vr(scalar_variant(ty), pp_e_unary("*", pp_e("v")));
				dflt = vr(scalar_variant(ty), scalar_default(ty));
			}
			pp_push(get_m, oneof_get_arm(f, fid, qenum, variant, active, dflt));
			pp_push(has_m, oneof_has_arm(f, fid, qenum, variant));
		}
	}
}

static PpNode *message_index_fn(const char *pool, const char *expect, const char *const *doc)
{
	PpNode *mi = e_meth1(pp_e_call(pp_e(pool), NULL), "message_index", e_qpath(pp_ty("Self"), "::buffa::MessageName", "FULL_NAME"));
	PpNode *closure = pp_e_closure(e_meth1(mi, "expect", lit_str(expect)), false);
	PpNode *body = pp_block(static_once_lock("IDX", pp_ty("::buffa_descriptor::MessageIndex")),
				s_expr(pp_e_unary("*", e_meth1(pp_e("IDX"), "get_or_init", closure))), NULL);
	PpNode *fn = pp_item_fn(0, pp_sig("__buffa_reflect_message_index", pp_ty("::buffa_descriptor::MessageIndex")), body);
	reflect_doc(fn, doc);
	pp_push_attr(fn, attr_src("doc", "hidden"));
	return fn;
}

static void reflect_message_impl(Vec *out, const PbMsg *msg, PpNode *self_ty, bool generics, const char *pool, const Scope *s,
				 const char **oneof_idents, const char *oneof_prefix, bool view)
{
	PpNode *imp = pp_item_impl(pp_path("::buffa_descriptor::reflect::ReflectMessage"), self_ty);
	if (generics)
		pp_set_generics(imp, generics_a());
	PpNode *pool_call = pp_e_call(pp_e(pool), NULL);
	pp_push(imp, fn_self("message_descriptor", false, ty_ref(pp_ty("::buffa_descriptor::MessageDescriptor")),
			     pp_block(s_expr(e_meth1(pool_call, "message", pp_e_call(pp_e("Self::__buffa_reflect_message_index"), NULL))), NULL)));
	pp_push(imp, fn_self("pool", false, ty_ref(arc_pool_ty()), pp_block(s_expr(pp_e_call(pp_e(pool), NULL)), NULL)));
	if (!view && CFG->preserve_unknown_fields)
		pp_push(imp, fn_self("unknown_fields", false, ty_ref(pp_ty("::buffa::UnknownFields")), pp_block(s_expr(pp_e_ref(false, self_f("__buffa_unknown_fields"))), NULL)));
	PpNode *get_m = pp_e_match(e_meth0(pp_e("field"), "number")), *has_m = pp_e_match(e_meth0(pp_e("field"), "number"));
	reflect_arms(get_m, has_m, msg, s, oneof_idents, oneof_prefix, view);
	pp_push(get_m, debug_assert_arm(view ? "view's" : "message's"));
	pp_push(has_m, pp_arm(pp_p_wild(), NULL, pp_e_lit_bool(false)));
	PpNode *sig = pp_sig("get", ty_lt(VR, "'_"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, field_descriptor_arg());
	pp_push(imp, pp_item_fn(0, sig, pp_block(allow_unused_use("::buffa::Enumeration as _"), s_expr(get_m), NULL)));
	sig = pp_sig("has", pp_ty("bool"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, field_descriptor_arg());
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(has_m), NULL)));
	sig = pp_sig("for_each_set", NULL);
	pp_push(sig, pp_receiver(true, false, NULL));
	PpNode *fnmut = pp_path("::core::ops::FnMut");
	PpNode *pa = pp_node(PP_PAREN_ARGS);
	pp_push(pa, ty_ref(pp_ty("::buffa_descriptor::FieldDescriptor")));
	pp_push(pa, ty_lt(VR, "'_"));
	pp_path_args(fnmut, pa);
	pp_push(sig, arg("f", ty_ref_mut(pp_ty_dyn(pp_bounds(pp_trait_bound(fnmut, false), NULL)))));
	PpNode *loop = pp_e_for(pp_p_ident("fd", false, false), e_meth0(pp_e("md"), "fields"),
				pp_block(stmt_if(e_call2("::buffa_descriptor::reflect::ReflectMessage::has", pp_e("self"), pp_e("fd")),
						 pp_block(s_semi(e_call2("f", pp_e("fd"), e_call2("::buffa_descriptor::reflect::ReflectMessage::get", pp_e("self"), pp_e("fd")))), NULL)),
					 NULL));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_let("md", false, e_call1("::buffa_descriptor::reflect::ReflectMessage::message_descriptor", pp_e("self"))),
						 s_expr(loop), NULL)));
	PpNode *td;
	if (view) {
		PpNode *len = e_meth1(e_meth1(e_meth0(pp_e("bytes"), "len"), "saturating_mul", pp_e_lit_int("128")), "max", pp_e("::buffa::DEFAULT_ELEMENT_MEMORY_LIMIT"));
		PpNode *opts = e_meth1(e_meth1(pp_e_call(pp_e("::buffa::DecodeOptions::new"), NULL), "with_element_memory_limit", len), "with_unknown_field_limit",
				       e_meth1(e_meth0(pp_e("bytes"), "len"), "max", pp_e("::buffa::DEFAULT_UNKNOWN_FIELD_LIMIT")));
		PpNode *dec = pp_e_call(pp_e("::buffa_descriptor::reflect::DynamicMessage::decode_with_options"),
					e_call1("::buffa::alloc::sync::Arc::clone", pp_e_call(pp_e(pool), NULL)),
					pp_e_call(pp_e("Self::__buffa_reflect_message_index"), NULL), pp_e_ref(false, pp_e("bytes")), pp_e_ref(false, pp_e("options")), NULL);
		td = pp_block(s_let("bytes", false, e_call1("::buffa::ViewEncode::encode_to_vec", pp_e("self"))), s_let("options", false, opts),
			      s_expr(e_meth1(dec, "expect", lit_str("view re-encodes to bytes decodable against its own descriptor"))), NULL);
	} else
		td = pp_block(s_expr(e_call3("::buffa_descriptor::reflect::DynamicMessage::from_message", pp_e("self"),
					     e_call1("::buffa::alloc::sync::Arc::clone", pp_e_call(pp_e(pool), NULL)),
					     pp_e_call(pp_e("Self::__buffa_reflect_message_index"), NULL))),
			      NULL);
	pp_push(imp, fn_self("to_dynamic", false, pp_ty("::buffa_descriptor::reflect::DynamicMessage"), td));
	vec_push(out, imp);

	imp = pp_item_impl(pp_path("::buffa_descriptor::reflect::ReflectElement"), self_ty);
	if (generics)
		pp_set_generics(imp, generics_a());
	PpNode *fn = fn_self("as_value_ref", false, ty_lt(VR, "'_"), pp_block(s_expr(vr("Message", e_call1(COW "::Borrowed", pp_e("self")))), NULL));
	if (!view)
		pp_push_attr(fn, attr_word("inline"));
	pp_push(imp, fn);
	vec_push(out, imp);

	imp = pp_item_impl(NULL, self_ty);
	if (generics)
		pp_set_generics(imp, generics_a());
	static const char *const odoc[] = {" Memoized `MessageIndex` for this message type, resolved once", " against the package's embedded descriptor pool.", NULL};
	static const char *const vdoc[] = {" Memoized `MessageIndex` for this view's message type, resolved",
					   " once against the package's embedded descriptor pool. An inherent",
					   " associated fn (not a free fn) so sibling views in the same module", " do not collide.", NULL};
	pp_push(imp, message_index_fn(pool, view ? "generated view type is registered in the embedded descriptor pool"
						 : "generated message is registered in the embedded descriptor pool",
				      view ? vdoc : odoc));
	vec_push(out, imp);
}

/* bridge mode: reflectable_impl + reflect_element_impl_bridge (cfg_block gates the first item) */
static void reflectable_impl_bridge(Vec *out, const char *name)
{
	PpNode *imp = pp_item_impl(pp_path("::buffa_descriptor::reflect::Reflectable"), pp_ty(name));
	PpNode *pool = pp_e_call(pp_e(SENTINEL_MOD "::reflect::descriptor_pool"), NULL);
	PpNode *full = e_qpath(pp_ty("Self"), "::buffa::MessageName", "FULL_NAME");
	PpNode *pm = pp_macro(pp_path("panic"), PP_DELIM_PAREN);
	pp_push(pm, pp_tt_lit(pm2_string(str_c("type {:?} not registered in this package's descriptor pool (cross-crate reflect()?)"))));
	pp_tt_push_src(pm, ", <Self as ::buffa::MessageName>::FULL_NAME,");
	PpNode *idx = e_meth1(e_meth1(pp_e("pool"), "message_index", full), "unwrap_or_else", pp_e_closure(pp_e_macro(pm), false));
	PpNode *dm = e_call3("::buffa_descriptor::reflect::DynamicMessage::from_message", pp_e("self"), e_call1("::buffa::alloc::sync::Arc::clone", pp_e("pool")), pp_e("idx"));
	PpNode *fn = fn_self("reflect", false, ty_lt(COW, "'_"),
			     pp_block(s_let("pool", false, pool), s_let("idx", false, idx),
				      s_expr(e_call1(COW "::Owned", e_call1("::buffa::alloc::boxed::Box::new", dm))), NULL));
	static const char *const d[] = {" Bridge-mode reflective handle: encodes `self` and decodes", " it into a [`DynamicMessage`](::buffa_descriptor::reflect::DynamicMessage)",
					" against the package's embedded descriptor pool.", "", " # Performance", "",
					" One full encode/decode round-trip plus a heap allocation per", " call. Hold onto the returned handle for repeated field reads",
					" rather than calling `reflect()` per field.", "", " # Panics", "", " Panics if the embedded `FileDescriptorSet` is malformed or",
					" `Self::FULL_NAME` is not registered. Both indicate codegen", " emitted inconsistent output, not consumer misuse — except",
					" when this type was re-exported from a different", " `buffa-build` invocation, whose pool is a different",
					" instance. Each `generate_reflection(true)` codegen run", " embeds its own pool; do not mix `reflect()` calls across",
					" independently-generated crates.", NULL};
	reflect_doc(fn, d);
	pp_push(imp, fn);
	Vec items = one(imp);
	imp = pp_item_impl(pp_path("::buffa_descriptor::reflect::ReflectElement"), pp_ty(name));
	fn = fn_self("as_value_ref", false, ty_lt(VR, "'_"), pp_block(s_expr(vr("Message", e_call1("::buffa_descriptor::reflect::Reflectable::reflect", pp_e("self")))), NULL));
	static const char *const d2[] = {" Bridge-mode element reflection: each call snapshots this", " element through [`Reflectable::reflect`]",
					 " (one encode/decode round-trip plus an allocation).", "",
					 " [`Reflectable::reflect`]: ::buffa_descriptor::reflect::Reflectable::reflect", NULL};
	reflect_doc(fn, d2);
	pp_push(imp, fn);
	vec_push(&items, imp);
	cfg_block(out, &items, gates().reflect);
}

/* reflect_pool_module_shared: delegate to the root `__buffa_fds` module */
static void reflect_pool_module_shared(Vec *out, const char *pkg)
{
	int n = (*pkg ? str_split(pkg, '.').n : 0) + 2;
	Buf b = {0};
	for (int i = 0; i < n; i++)
		buf_adds(&b, "super::");
	buf_adds(&b, "__buffa_fds");
	const char *root = buf_str(&b);
	PpNode *m = pp_item_mod(PP_VIS_PUB, "reflect", true);
	static const char *const md[] = {" Reflection support: this package's view onto the crate-wide",
					 " descriptor pool. In shared-pool mode the bytes and pool live once",
					 " at the module-tree root (`__buffa_fds`); this module re-exports",
					 " them so the per-package [`Reflectable`](::buffa_descriptor::reflect::Reflectable)", " paths keep resolving.", NULL};
	reflect_doc(m, md);
	PpNode *u = pp_item_use(PP_VIS_PUB, pp_use_tree(xcat(root, "::FILE_DESCRIPTOR_SET_BYTES")), false);
	static const char *const ud[] = {" The serialized `FileDescriptorSet` for this codegen run.", " Re-exported from the shared root module so",
					 " `pkg::FILE_DESCRIPTOR_SET_BYTES` keeps working; the bytes are", " embedded once for the whole generated tree.", NULL};
	pp_push(m, reflect_doc(u, ud));
	PpNode *fn = pp_item_fn(PP_VIS_PUB, pp_sig("descriptor_pool", pp_ty_ref("'static", false, arc_pool_ty())),
				pp_block(s_expr(pp_e_call(pp_e(xcat(root, "::descriptor_pool")), NULL)), NULL));
	static const char *const fd[] = {" The crate-wide descriptor pool, shared by every package's",
					 " `Reflectable` impls. Delegates to the single lazily-built pool",
					 " at the module-tree root, so all packages observe the same",
					 " [`DescriptorPool`](::buffa_descriptor::DescriptorPool)", " instance.", NULL};
	pp_push(m, reflect_doc(fn, fd));
	vec_push(out, m);
}

static void reflectable_impl_owned(Vec *out, const PbMsg *msg, const char *name, const Scope *s, const char **oneof_idents,
				   const char *oneof_prefix)
{
	if (!CFG->generate_reflection_vtable) {
		reflectable_impl_bridge(out, name);
		return;
	}
	Vec items = {0};
	reflect_message_impl(&items, msg, pp_ty(name), false, SENTINEL_MOD "::reflect::descriptor_pool", s, oneof_idents, oneof_prefix, false);
	PpNode *imp = pp_item_impl(pp_path("::buffa_descriptor::reflect::Reflectable"), pp_ty(name));
	PpNode *fn = fn_self("reflect", false, ty_lt(COW, "'_"), pp_block(s_expr(e_call1(COW "::Borrowed", pp_e("self"))), NULL));
	static const char *const d[] = {" Vtable-mode reflective handle: borrows `self` directly. No",
					" encode/decode round-trip and no allocation — the reflective",
					" accessors read this message's fields in place.", NULL};
	reflect_doc(fn, d);
	pp_push_attr(fn, attr_word("inline"));
	pp_push(imp, fn);
	vec_push(&items, imp);
	cfg_const_block(out, &items, gates().reflect);
}

static void reflect_view_impls(Vec *out, const Scope *vs, const PbMsg *msg, const char *view_ident, const char *view_oneof_prefix,
			       const char **oneof_idents)
{
	Buf b = {0};
	for (int i = 0; i < vs->nesting; i++)
		buf_adds(&b, "super::");
	buf_adds(&b, SENTINEL_MOD "::reflect::descriptor_pool");
	reflect_message_impl(out, msg, ty_lt(view_ident, "'a"), true, buf_str(&b), vs, oneof_idents, view_oneof_prefix, true);
}
