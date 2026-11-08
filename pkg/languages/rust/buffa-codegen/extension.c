/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/extension.rs: `pub const` extension descriptors, their
 * default fns and the JSON/text registry entries. */

static char *extension_const_ident(const char *name) { return make_field_ident(upper_ascii(to_snake_case(name))); }

static PpNode *ext_type_path(const PbMsg *field, const char *pkg, int nesting, const char *kind)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		fail("missing required descriptor field: extension.type_name");
	const char *p = ctx_rust_type_relative(tn, pkg, nesting);
	if (!p)
		fail("%s", CODEGEN_ERROR("extension %s type '%s' not found in descriptor set", kind, tn));
	return ty_path_str(p);
}

static PpNode *codec_for_type(const PbMsg *field, int ty, const char *pkg, int nesting)
{
	static const char *const names[19] = {"", "Double", "Float", "Int64", "Uint64", "Int32", "Fixed64", "Fixed32", "Bool", "StringCodec",
					      "", "", "BytesCodec", "Uint32", "EnumI32", "Sfixed32", "Sfixed64", "Sint32", "Sint64"};
	if (ty == PT_MESSAGE || ty == PT_GROUP)
		return ty_g1(ty == PT_GROUP ? "::buffa::extension::codecs::GroupCodec" : "::buffa::extension::codecs::MessageCodec",
			     ext_type_path(field, pkg, nesting, "message"));
	return pp_ty(xcat("::buffa::extension::codecs::", names[ty]));
}

static PpNode *codec_value_type(int ty)
{
	switch (ty) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: case PT_ENUM: return pp_ty("i32");
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return pp_ty("i64");
	case PT_UINT32: case PT_FIXED32: return pp_ty("u32");
	case PT_UINT64: case PT_FIXED64: return pp_ty("u64");
	case PT_BOOL: return pp_ty("bool");
	case PT_FLOAT: return pp_ty("f32");
	case PT_DOUBLE: return pp_ty("f64");
	case PT_STRING: return pp_ty("::buffa::alloc::string::String");
	default: return ty_g1("::buffa::alloc::vec::Vec", pp_ty("u8"));
	}
}

static PpNode *turbofish_path(const char *path, PpNode *ty) { return pp_e_path(pp_path_args(pp_path(path), pp_angle(ty, NULL))); }

static bool json_helper_tokens(const PbMsg *field, int ty, bool repeated, const char *pkg, int nesting, PpNode **to, PpNode **from)
{
	const char *h = "::buffa::extension_registry::helpers::";
	const char *rep = repeated ? "repeated_" : "";
	if (ty == PT_GROUP)
		return false;
	if (ty == PT_ENUM && fd_type_name(field) && str_eq(fd_type_name(field), ".google.protobuf.NullValue")) {
		*to = pp_e(xcat(h, rep, "null_value_to_json"));
		*from = pp_e(xcat(h, rep, "null_value_from_json"));
		return true;
	}
	if (ty == PT_ENUM || ty == PT_MESSAGE) {
		const char *k = ty == PT_ENUM ? "enum" : "message";
		PpNode *t = ext_type_path(field, pkg, nesting, k);
		*to = turbofish_path(xcat(h, rep, k, "_to_json"), t);
		*from = turbofish_path(xcat(h, rep, k, "_from_json"), t);
		return true;
	}
	*to = pp_e(xcat(h, rep, type_names[ty], "_to_json"));
	*from = pp_e(xcat(h, rep, type_names[ty], "_from_json"));
	return true;
}

static void generate_one_extension(Vec *out, Vec *json_ids, Vec *text_ids, const PbMsg *field, const char *pkg, int nesting, Feat features,
				   const char *scope_fqn)
{
	if (!pb_has(field, FD_NAME))
		fail("missing required descriptor field: extension.name");
	const char *proto_name = dname(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, features);
	const char *extendee = pb_has(field, FD_EXTENDEE) ? pb_str(field, FD_EXTENDEE) : "<unknown>";
	const char *extendee_no_dot = extendee[0] == '.' ? extendee + 1 : extendee;
	bool repeated = fd_label(field) == LABEL_REPEATED;
	char *const_ident = extension_const_ident(proto_name);
	PpNode *codec = codec_for_type(field, ty, pkg, nesting);
	if (repeated)
		codec = ty_g1(is_field_packed(field, features) ? "::buffa::extension::codecs::PackedRepeated" : "::buffa::extension::codecs::Repeated", codec);
	PpNode *default_fn = NULL;
	const char *fn_ident = NULL;
	if (pb_has(field, FD_DEFAULT_VALUE) && *pb_str(field, FD_DEFAULT_VALUE)) {
		if (repeated)
			fail("%s", CODEGEN_ERROR("extension `%s`: repeated fields cannot have a default value", proto_name));
		if (ty == PT_MESSAGE || ty == PT_GROUP)
			fail("%s", CODEGEN_ERROR("extension `%s`: message/group fields cannot have a default value", proto_name));
		char *lowered = xstrdup(const_ident);
		for (char *p = lowered; *p; p++)
			*p = lower(*p);
		fn_ident = xcat("__", lowered, "_default");
		PpNode *body, *ret;
		bool is_const = true;
		if (ty == PT_ENUM) {
			PpNode *path = ext_type_path(field, pkg, nesting, "enum");
			(void)path;
			const char *tn = fd_type_name(field);
			const char *rp = ctx_rust_type_relative(tn, pkg, nesting);
			body = pp_e_cast(pp_e(xcat(rust_path(rp), "::", make_field_ident(pb_str(field, FD_DEFAULT_VALUE)))), pp_ty("i32"));
			ret = pp_ty("i32");
		} else {
			ret = codec_value_type(ty);
			body = parse_default_value(field, pkg, features, nesting);
			if (!body)
				fail("%s", CODEGEN_ERROR("extension `%s`: could not parse default_value `%s` for type %s", proto_name,
							 pb_str(field, FD_DEFAULT_VALUE), "TYPE"));
			is_const = ty != PT_STRING && ty != PT_BYTES;
		}
		PpNode *sig = pp_sig(fn_ident, ret);
		if (is_const)
			sig->flags |= PP_F_CONST;
		default_fn = pp_item_fn(0, sig, pp_block(s_expr(body), NULL));
		pp_push_attr(default_fn, attr_src("doc", "hidden"));
	}
	const char *doc = xfmt("Extension `%s` on `%s` (field %d%s).", proto_name, extendee, (int)number, ty == PT_GROUP ? ", group-encoded" : "");
	if (ty == PT_ENUM && fd_type_name(field))
		doc = xcat(doc, "\n\nProto enum type: `", fd_type_name(field), "`. Cast via `EnumValue::from_i32`.");
	const char *full_name = *scope_fqn ? xcat(scope_fqn, ".", proto_name) : proto_name;
	FeatureGates g = gates();

	if (default_fn)
		vec_push(out, default_fn);
	PpNode *init = fn_ident ? pp_e_call(pp_e("::buffa::Extension::with_default"), lit_u32(number), lit_str(extendee_no_dot), pp_e(fn_ident), NULL)
				: e_call2("::buffa::Extension::new", lit_u32(number), lit_str(extendee_no_dot));
	PpNode *c = pp_item_const(PP_VIS_PUB, const_ident, ty_g1("::buffa::Extension", codec), init);
	pp_push_attr(c, doc_attr(doc));
	vec_push(out, c);
	PpNode *to, *from;
	if (CFG->generate_json && json_helper_tokens(field, ty, repeated, pkg, nesting, &to, &from)) {
		char *ident = xcat("__", const_ident, "_JSON_EXT");
		PpNode *from_expr = from;
		bool null_ok = fd_type_name(field) && (str_eq(fd_type_name(field), ".google.protobuf.NullValue") ||
						       str_eq(fd_type_name(field), ".google.protobuf.Value"));
		if (repeated || !null_ok) {
			PpNode *sig = pp_sig("__from_json", ty_g2("::core::result::Result", ty_g1("::buffa::alloc::vec::Vec", pp_ty("::buffa::UnknownField")),
								  pp_ty("::buffa::alloc::string::String")));
			pp_push(sig, arg("value", pp_ty("::buffa::serde_json::Value")));
			pp_push(sig, arg("number", pp_ty("u32")));
			PpNode *b = pp_block(stmt_if(e_meth0(pp_e("value"), "is_null"),
						     pp_block(s_semi(pp_e_return(e_call1("::core::result::Result::Ok", pp_e_call(pp_e("::buffa::alloc::vec::Vec::new"), NULL)))), NULL)),
					     s_expr(pp_e_call(from, pp_e("value"), pp_e("number"), NULL)), NULL);
			from_expr = pp_e_block(pp_block(pp_s_item(pp_item_fn(0, sig, b)), s_expr(pp_e("__from_json")), NULL));
		}
		PpNode *st = pp_e_struct(pp_path("::buffa::type_registry::JsonExtEntry"), NULL);
		pp_push(st, pp_field_value("number", lit_u32(number)));
		pp_push(st, pp_field_value("full_name", lit_str(full_name)));
		pp_push(st, pp_field_value("extendee", lit_str(extendee_no_dot)));
		pp_push(st, pp_field_value("to_json", to));
		pp_push(st, pp_field_value("from_json", from_expr));
		PpNode *jc = pp_item_const(PP_VIS_PUB, ident, pp_ty("::buffa::type_registry::JsonExtEntry"), st);
		pp_push_attr(jc, attr_src("doc", "hidden"));
		Vec one_ = one(jc);
		cfg_block(out, &one_, g.json);
		vec_push(json_ids, ident);
	}
	if (CFG->generate_text && (ty == PT_MESSAGE || ty == PT_GROUP)) {
		char *ident = xcat("__", const_ident, "_TEXT_EXT");
		PpNode *t = ext_type_path(field, pkg, nesting, ty == PT_MESSAGE ? "message" : "group");
		const char *k = ty == PT_MESSAGE ? "message" : "group";
		PpNode *st = pp_e_struct(pp_path("::buffa::type_registry::TextExtEntry"), NULL);
		pp_push(st, pp_field_value("number", lit_u32(number)));
		pp_push(st, pp_field_value("full_name", lit_str(full_name)));
		pp_push(st, pp_field_value("extendee", lit_str(extendee_no_dot)));
		pp_push(st, pp_field_value("text_encode", turbofish_path(xcat("::buffa::type_registry::", k, "_encode_text"), t)));
		pp_push(st, pp_field_value("text_merge", turbofish_path(xcat("::buffa::type_registry::", k, "_merge_text"), t)));
		PpNode *tc = pp_item_const(PP_VIS_PUB, ident, pp_ty("::buffa::type_registry::TextExtEntry"), st);
		pp_push_attr(tc, attr_src("doc", "hidden"));
		Vec one_ = one(tc);
		cfg_block(out, &one_, g.text);
		vec_push(text_ids, ident);
	}
}

static void generate_extensions(Vec *out, Vec *json_ids, Vec *text_ids, const Vec *exts, const char *pkg, int nesting, Feat features,
				const char *scope_fqn)
{
	for (int i = 0; i < exts->n; i++)
		generate_one_extension(out, json_ids, text_ids, exts->v[i], pkg, nesting, features, scope_fqn);
}
