/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/impl_message.rs: `impl Message` (compute_size, write_to,
 * merge_field, clear) for scalar, repeated, map and oneof fields, `impl ExtensionSet`,
 * `impl MessageName`, and the field classification helpers shared with views, text and
 * reflection. Custom owned representations (bytes_fields, string_fields, map_fields,
 * repeated_fields, custom pointers) are buffa-build options and not modeled: the defaults apply. */

#define MAX_FIELD_NUMBER 536870911

static const char *const type_names[19] = {"", "double", "float", "int64", "uint64", "int32", "fixed64", "fixed32", "bool",
					   "string", "group", "message", "bytes", "uint32", "enum", "sfixed32", "sfixed64",
					   "sint32", "sint64"};

static const char *tfn(const char *prefix, int ty, const char *suffix) { return xcat(prefix, type_names[ty], suffix); }

static uint32_t validated_field_number(const PbMsg *field)
{
	if (!pb_has(field, FD_NUMBER))
		fail("missing required descriptor field: field.number");
	int n = fd_number(field);
	if (n < 1 || n > MAX_FIELD_NUMBER)
		fail("%s", CODEGEN_ERROR("invalid field number: %d", n));
	return (uint32_t)n;
}

static const char *field_name_req(const PbMsg *field)
{
	if (!pb_has(field, FD_NAME))
		fail("missing required descriptor field: field.name");
	return dname(field);
}

static bool is_explicit_presence_scalar(const PbMsg *field, int ty, Feat features)
{
	if (ty == PT_MESSAGE || ty == PT_GROUP)
		return false;
	if (fd_proto3_optional(field))
		return true;
	if (fd_label(field) == LABEL_REQUIRED)
		return false;
	return feat_child(features, field_features(field)).presence == PROTO_PRESENCE_EXPLICIT && !fd_has_oneof(field);
}

static bool is_required_field(const PbMsg *field, Feat features)
{
	if (fd_label(field) == LABEL_REQUIRED)
		return true;
	return feat_child(features, field_features(field)).presence == PROTO_PRESENCE_LEGACY_REQUIRED;
}

static int effective_type(const PbMsg *field, Feat features)
{
	int ty = fd_type(field);
	if (ty == PT_MESSAGE && feat_child(features, field_features(field)).msg_enc == PROTO_MESSAGE_DELIMITED)
		return PT_GROUP;
	if (!CFG->strict_utf8_mapping || ty != PT_STRING)
		return ty;
	return feat_child(features, field_features(field)).utf8 == PROTO_UTF8_NONE ? PT_BYTES : ty;
}

static int effective_type_in_map_entry(const PbMsg *field, Feat features)
{
	features.msg_enc = PROTO_MESSAGE_LENGTH_PREFIXED;
	return effective_type(field, features);
}

static bool is_closed_enum(Feat f) { return f.enum_type == PROTO_ENUM_TYPE_CLOSED; }

static bool is_supported_field_type(int ty) { return ty >= PT_DOUBLE && ty <= PT_SINT64; }

static int wire_type_byte(int ty)
{
	switch (ty) {
	case PT_FIXED64: case PT_SFIXED64: case PT_DOUBLE: return 1;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return 2;
	case PT_GROUP: return 3;
	case PT_FIXED32: case PT_SFIXED32: case PT_FLOAT: return 5;
	default: return 0;
	}
}

static PpNode *wire_type_token(int ty)
{
	static const char *const names[] = {"Varint", "Fixed64", "LengthDelimited", "StartGroup", "", "Fixed32"};
	return pp_e(xcat("::buffa::encoding::WireType::", names[wire_type_byte(ty)]));
}

static bool is_packed_type(int ty) { return ty != PT_STRING && ty != PT_BYTES && ty != PT_MESSAGE && ty != PT_GROUP && is_supported_field_type(ty); }
static bool is_fixed32_family(int ty) { return ty == PT_FIXED32 || ty == PT_SFIXED32 || ty == PT_FLOAT; }
static bool is_fixed64_family(int ty) { return ty == PT_FIXED64 || ty == PT_SFIXED64 || ty == PT_DOUBLE; }

static bool is_field_packed(const PbMsg *field, Feat features)
{
	if (!is_packed_type(fd_type(field)))
		return false;
	const PbMsg *opts = pb_msg(field, FD_OPTIONS);
	if (pb_has(opts, FLDOPT_PACKED))
		return pb_int(opts, FLDOPT_PACKED, 0) != 0;
	return feat_child(features, field_features(field)).rep == PROTO_REPEATED_PACKED;
}

static uint64_t tag_encoded_len(uint32_t number, int wt)
{
	uint64_t v = ((uint64_t)number << 3) | (uint64_t)wt;
	int bits = 0;
	while (v) {
		bits++;
		v >>= 1;
	}
	return (uint64_t)(bits + 6) / 7;
}

static PpNode *u64lit(uint64_t v) { return lit_usuffixed(v, "u64"); }
static PpNode *cast_u64(PpNode *e) { return pp_e_cast(e, pp_ty("u64")); }
static PpNode *plus(PpNode *a, PpNode *b) { return pp_e_binary(a, "+", b); }
static PpNode *size_add(PpNode *e) { return s_semi(pp_e_binary(pp_e("size"), "+=", e)); }
static PpNode *call_try(PpNode *call) { return pp_e_try(call); }
static PpNode *e_if(PpNode *cond, PpNode *block) { return pp_e_if(cond, block, NULL); }
static PpNode *stmt_if(PpNode *cond, PpNode *block) { return s_expr(e_if(cond, block)); }
static PpNode *not(PpNode *e) { return pp_e_unary("!", e); }
static PpNode *p_some(PpNode *pat) { return pp_p_tuple_struct(pp_path("Some"), pat, NULL); }
static PpNode *p_core_some(PpNode *pat) { return pp_p_tuple_struct(pp_path("::core::option::Option::Some"), pat, NULL); }

/* `if let Some(ref v) = self.x { ... }` */
static PpNode *if_let_some_ref(const char *ident, PpNode *block)
{
	return stmt_if(pp_e_let(p_some(pp_p_ident("v", true, false)), self_f(ident)), block);
}

/* type_encoded_size_expr */
static PpNode *type_encoded_size_expr(int ty, PpNode *val)
{
	if (is_fixed32_family(ty))
		return cast_u64(pp_e("::buffa::types::FIXED32_ENCODED_LEN"));
	if (is_fixed64_family(ty))
		return cast_u64(pp_e("::buffa::types::FIXED64_ENCODED_LEN"));
	if (ty == PT_BOOL)
		return cast_u64(pp_e("::buffa::types::BOOL_ENCODED_LEN"));
	return cast_u64(e_call1(tfn("::buffa::types::", ty, "_encoded_len"), val));
}

static PpNode *is_non_default_expr(int ty, const char *ident)
{
	switch (ty) {
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return pp_e_binary(self_f(ident), "!=", pp_e_lit_int("0i32"));
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return pp_e_binary(self_f(ident), "!=", pp_e_lit_int("0i64"));
	case PT_UINT32: case PT_FIXED32: return pp_e_binary(self_f(ident), "!=", pp_e_lit_int("0u32"));
	case PT_UINT64: case PT_FIXED64: return pp_e_binary(self_f(ident), "!=", pp_e_lit_int("0u64"));
	case PT_FLOAT: return pp_e_binary(e_meth0(self_f(ident), "to_bits"), "!=", pp_e_lit_int("0u32"));
	case PT_DOUBLE: return pp_e_binary(e_meth0(self_f(ident), "to_bits"), "!=", pp_e_lit_int("0u64"));
	default: return self_f(ident);
	}
}

static PpNode *wire_type_check(PpNode *tag, PpNode *wire_type)
{
	return s_semi(call_try(e_call2("::buffa::encoding::check_wire_type", tag, wire_type)));
}

static PpNode *wire_check_ld(void) { return wire_type_check(pp_e("tag"), pp_e("::buffa::encoding::WireType::LengthDelimited")); }

/* closed_enum_decode(_with_unknown): statements appended to block */
static void closed_enum_decode(PpNode *block, PpNode *buf_expr, Vec *on_known, Vec *on_unknown)
{
	pp_push(block, s_let("__raw", false, call_try(e_call1("::buffa::types::decode_int32", buf_expr))));
	PpNode *known = pp_block(NULL);
	push_all(known, on_known);
	PpNode *els = NULL;
	if (on_unknown && on_unknown->n) {
		PpNode *ub = pp_block(NULL);
		push_all(ub, on_unknown);
		els = pp_e_block(ub);
	}
	PpNode *cond = pp_e_let(p_core_some(pp_p_ident("__v", false, false)), e_call1("::buffa::Enumeration::from_i32", pp_e("__raw")));
	pp_push(block, s_expr(pp_e_if(cond, known, els)));
}

static void closed_enum_unknown_route(Vec *out, uint32_t number, bool preserve)
{
	if (!preserve)
		return;
	vec_push(out, s_semi(call_try(e_meth0(pp_e("ctx"), "register_unknown_field"))));
	PpNode *st = pp_e_struct(pp_path("::buffa::UnknownField"), NULL);
	pp_push(st, pp_field_value("number", lit_u32(number)));
	pp_push(st, pp_field_value("data", e_call1("::buffa::UnknownFieldData::Varint", cast_u64(pp_e("__raw")))));
	vec_push(out, s_semi(e_meth1(self_f("__buffa_unknown_fields"), "push", st)));
}

static Vec one(PpNode *n)
{
	Vec v = {0};
	vec_push(&v, n);
	return v;
}

static PpNode *arm_block(uint32_t number, PpNode *block) { return pp_arm(pp_p_lit(lit_u32(number)), NULL, pp_e_block(block)); }

/* ---- field classification ---- */

enum { FK_SCALAR, FK_REPEATED, FK_MAP, FK_ONEOF };

typedef struct {
	int kind;
	const PbMsg *field;   /* scalar, repeated, map */
	const char *name;     /* oneof */
	const char *enum_ident;
	Vec fields;           /* oneof members */
	uint32_t key;
	int seq;              /* declaration order (stable sort) */
} FieldKind;

static const PbMsg *find_map_entry(const PbMsg *msg, const PbMsg *field);

static bool is_map_field(const PbMsg *msg, const PbMsg *field)
{
	return fd_type(field) == PT_MESSAGE && find_map_entry(msg, field);
}

static int fk_cmp(const void *a, const void *b)
{
	const FieldKind *x = *(FieldKind *const *)a, *y = *(FieldKind *const *)b;
	if (x->key != y->key)
		return x->key < y->key ? -1 : 1;
	return x->seq - y->seq;
}

/* oneof_idents: index -> enum ident, for oneofs with real members */
static const char **resolve_oneof_idents(const PbMsg *msg);

static Vec classify_fields_ordered(const PbMsg *msg, const char **oneof_idents)
{
	Vec entries = {0};
	int nf = n_fields(msg), no = pb_count(msg, DP_ONEOF_DECL);
	bool *seen = xalloc((size_t)no + 1);
	for (int i = 0; i < nf; i++) {
		const PbMsg *f = field_at(msg, i);
		uint32_t number = validated_field_number(f);
		FieldKind *k = xalloc(sizeof *k);
		k->key = number;
		if (is_real_oneof_member(f)) {
			int idx = fd_oneof(f);
			if (idx < 0 || idx >= no || seen[idx])
				continue;
			seen[idx] = true;
			if (!oneof_idents[idx] || !pb_has(pb_msg_at(msg, DP_ONEOF_DECL, idx), ODP_NAME))
				continue;
			k->kind = FK_ONEOF;
			k->name = dname(pb_msg_at(msg, DP_ONEOF_DECL, idx));
			k->enum_ident = oneof_idents[idx];
			for (int j = 0; j < nf; j++) {
				const PbMsg *m = field_at(msg, j);
				if (is_real_oneof_member(m) && fd_oneof(m) == idx) {
					vec_push(&k->fields, (void *)m);
					uint32_t n = validated_field_number(m);
					if (n < k->key)
						k->key = n;
				}
			}
		} else if (fd_label(f) == LABEL_REPEATED) {
			if (is_map_field(msg, f))
				k->kind = FK_MAP;
			else if (is_supported_field_type(fd_type(f)))
				k->kind = FK_REPEATED;
			else
				continue;
			k->field = f;
		} else if (is_supported_field_type(fd_type(f))) {
			k->kind = FK_SCALAR;
			k->field = f;
		} else
			continue;
		k->seq = entries.n;
		vec_push(&entries, k);
	}
	/* sort_by_key is stable: ties keep declaration order (seq) */
	if (entries.n > 1)
		qsort(entries.v, (size_t)entries.n, sizeof *entries.v, fk_cmp);
	return entries;
}

static void find_map_entry_fields(const PbMsg *msg, const PbMsg *field, const PbMsg **key, const PbMsg **val);

/* lazy: the scope of the lazy view family (is_lazy_field, lazy_view.c), whose lazy message fields
 * re-emit raw fragments without a cache slot; NULL for the owned and eager families. */
static bool is_lazy_field(const Scope *s, const PbMsg *field);
static bool message_uses_size_cache(const PbMsg *msg, const Vec *fields, Feat features, const Scope *lazy)
{
	for (int i = 0; i < fields->n; i++) {
		const FieldKind *k = fields->v[i];
		int ty;
		switch (k->kind) {
		case FK_SCALAR: case FK_REPEATED:
			ty = effective_type(k->field, features);
			if ((ty == PT_MESSAGE && !(lazy && is_lazy_field(lazy, k->field))) || ty == PT_GROUP)
				return true;
			break;
		case FK_ONEOF:
			for (int j = 0; j < k->fields.n; j++) {
				ty = effective_type(k->fields.v[j], features);
				if (ty == PT_MESSAGE || ty == PT_GROUP)
					return true;
			}
			break;
		case FK_MAP: {
			const PbMsg *entry = find_map_entry(msg, k->field);
			if (!entry)
				break;
			const PbMsg *val = NULL;
			for (int j = 0; j < n_fields(entry); j++)
				if (fd_number(field_at(entry, j)) == 2) {
					val = field_at(entry, j);
					break;
				}
			if (val && effective_type_in_map_entry(val, features) == PT_MESSAGE)
				return true;
			break;
		}
		}
	}
	return false;
}

/* message_name_impl */
static PpNode *message_name_impl(const char *pkg, const char *fqn, PpNode *generics, PpNode *ty)
{
	const char *name = fqn;
	if (*pkg) {
		char *pre = xcat(pkg, ".");
		if (has_prefix(fqn, pre))
			name = fqn + strlen(pre);
	}
	PpNode *imp = pp_item_impl(pp_path("::buffa::MessageName"), ty);
	if (generics)
		pp_set_generics(imp, generics);
	pp_push(imp, const_static_str("PACKAGE", pkg));
	pp_push(imp, const_static_str("NAME", name));
	pp_push(imp, const_static_str("FULL_NAME", fqn));
	pp_push(imp, const_static_str("TYPE_URL", xcat("type.googleapis.com/", fqn)));
	return imp;
}

/* ---- scalar fields ---- */

static PpNode *scalar_clear_stmt(const PbMsg *field, const char *pkg, Feat parent, int nesting)
{
	Feat f = feat_resolve_field(field, parent);
	const char *name = field_name_req(field);
	int ty = effective_type(field, f);
	const char *id = ctx_field_ident(name, fd_number(field));
	if (is_explicit_presence_scalar(field, ty, f))
		return s_semi(pp_e_assign(self_f(id), e_none()));
	PpNode *d = parse_default_value(field, pkg, f, nesting);
	if (!d)
		d = open_enum_bare_default_value(field, pkg, f, nesting);
	if (d)
		return s_semi(pp_e_assign(self_f(id), d));
	switch (ty) {
	case PT_STRING: case PT_BYTES: return s_semi(e_meth0(self_f(id), "clear"));
	case PT_MESSAGE: case PT_GROUP: return s_semi(pp_e_assign(self_f(id), pp_e_call(pp_e("::buffa::MessageField::none"), NULL)));
	case PT_ENUM:
		if (is_closed_enum(f))
			return s_semi(pp_e_assign(self_f(id), pp_e_call(pp_e("::core::default::Default::default"), NULL)));
		return s_semi(pp_e_assign(self_f(id), e_call1("::buffa::EnumValue::from", pp_e_lit_int("0"))));
	case PT_INT32: case PT_SINT32: case PT_SFIXED32: return s_semi(pp_e_assign(self_f(id), pp_e_lit_int("0i32")));
	case PT_INT64: case PT_SINT64: case PT_SFIXED64: return s_semi(pp_e_assign(self_f(id), pp_e_lit_int("0i64")));
	case PT_UINT32: case PT_FIXED32: return s_semi(pp_e_assign(self_f(id), pp_e_lit_int("0u32")));
	case PT_UINT64: case PT_FIXED64: return s_semi(pp_e_assign(self_f(id), pp_e_lit_int("0u64")));
	case PT_FLOAT: return s_semi(pp_e_assign(self_f(id), pp_e_lit(pp_lit(PP_LIT_FLOAT, "0f32"))));
	case PT_DOUBLE: return s_semi(pp_e_assign(self_f(id), pp_e_lit(pp_lit(PP_LIT_FLOAT, "0f64"))));
	default: return s_semi(pp_e_assign(self_f(id), pp_e_lit_bool(false)));
	}
}

static PpNode *scalar_compute_size_stmt(const PbMsg *field, Feat features)
{
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, features);
	const char *id = ctx_field_ident(name, fd_number(field));
	PpNode *tag = u64lit(tag_encoded_len(number, wire_type_byte(ty)));
	bool req = is_required_field(field, features);
	if (is_explicit_presence_scalar(field, ty, features)) {
		if (ty == PT_STRING || ty == PT_BYTES)
			return if_let_some_ref(id, pp_block(size_add(plus(tag, cast_u64(e_call1(tfn("::buffa::types::", ty, "_encoded_len"), pp_e("v"))))), NULL));
		if (ty == PT_ENUM)
			return if_let_some_ref(id, pp_block(size_add(plus(tag, cast_u64(e_call1("::buffa::types::int32_encoded_len", e_meth0(pp_e("v"), "to_i32"))))), NULL));
		PpNode *sz = type_encoded_size_expr(ty, pp_e("v"));
		if (is_fixed32_family(ty) || is_fixed64_family(ty) || ty == PT_BOOL)
			return stmt_if(e_meth0(self_f(id), "is_some"), pp_block(size_add(plus(tag, sz)), NULL));
		return stmt_if(pp_e_let(p_some(pp_p_ident("v", false, false)), self_f(id)), pp_block(size_add(plus(tag, sz)), NULL));
	}
	if (ty == PT_STRING || ty == PT_BYTES) {
		PpNode *s = size_add(plus(tag, cast_u64(e_call1(tfn("::buffa::types::", ty, "_encoded_len"), pp_e_ref(false, self_f(id))))));
		return req ? s : stmt_if(not(e_meth0(self_f(id), "is_empty")), pp_block(s, NULL));
	}
	if (ty == PT_ENUM) {
		PpNode *s = size_add(plus(tag, cast_u64(e_call1("::buffa::types::int32_encoded_len", pp_e("val")))));
		PpNode *b = pp_block(s_let("val", false, e_meth0(self_f(id), "to_i32")), NULL);
		pp_push(b, req ? s : stmt_if(pp_e_binary(pp_e("val"), "!=", pp_e_lit_int("0")), pp_block(s, NULL)));
		return s_expr(pp_e_block(b));
	}
	if (ty == PT_MESSAGE) {
		PpNode *b = pp_block(s_let("__slot", false, e_meth0(pp_e("__cache"), "reserve")),
				     s_let("inner_size", false, e_meth1(self_f(id), "compute_size", pp_e("__cache"))),
				     s_semi(pp_e_method(pp_e("__cache"), "set", pp_e("__slot"), pp_e("inner_size"), NULL)), NULL);
		pp_push(b, size_add(plus(plus(tag, cast_u64(e_call1("::buffa::encoding::varint_len", cast_u64(pp_e("inner_size"))))),
					 cast_u64(pp_e("inner_size")))));
		return stmt_if(e_meth0(self_f(id), "is_set"), b);
	}
	if (ty == PT_GROUP) {
		PpNode *b = pp_block(s_let("inner_size", false, e_meth1(self_f(id), "compute_size", pp_e("__cache"))), NULL);
		pp_push(b, size_add(plus(plus(tag, cast_u64(pp_e("inner_size"))), u64lit(tag_encoded_len(number, wire_type_byte(ty))))));
		return stmt_if(e_meth0(self_f(id), "is_set"), b);
	}
	PpNode *s = size_add(plus(tag, type_encoded_size_expr(ty, self_f(id))));
	return req ? s : stmt_if(is_non_default_expr(ty, id), pp_block(s, NULL));
}

static PpNode *put3(const char *fn, uint32_t number, PpNode *v) { return s_semi(e_call3(fn, lit_u32(number), v, pp_e("buf"))); }

static PpNode *len_delimited_header(uint32_t number)
{
	return s_semi(e_call3("::buffa::types::put_len_delimited_header", lit_u32(number),
			      e_call1("u64::from", e_meth0(pp_e("__cache"), "consume_next")), pp_e("buf")));
}

static PpNode *scalar_write_to_stmt(const PbMsg *field, Feat features)
{
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, features);
	const char *id = ctx_field_ident(name, fd_number(field));
	bool req = is_required_field(field, features);
	if (is_explicit_presence_scalar(field, ty, features)) {
		if (ty == PT_STRING)
			return if_let_some_ref(id, pp_block(put3("::buffa::types::put_string_field", number, pp_e("v")), NULL));
		if (ty == PT_BYTES)
			return if_let_some_ref(id, pp_block(put3("::buffa::types::put_shared_bytes_field", number, pp_e("v")), NULL));
		if (ty == PT_ENUM)
			return if_let_some_ref(id, pp_block(put3("::buffa::types::put_int32_field", number, e_meth0(pp_e("v"), "to_i32")), NULL));
		return stmt_if(pp_e_let(p_some(pp_p_ident("v", false, false)), self_f(id)),
			       pp_block(put3(tfn("::buffa::types::put_", ty, "_field"), number, pp_e("v")), NULL));
	}
	if (ty == PT_STRING || ty == PT_BYTES) {
		PpNode *s = put3(ty == PT_STRING ? "::buffa::types::put_string_field" : "::buffa::types::put_shared_bytes_field", number,
				 pp_e_ref(false, self_f(id)));
		return req ? s : stmt_if(not(e_meth0(self_f(id), "is_empty")), pp_block(s, NULL));
	}
	if (ty == PT_ENUM) {
		if (req)
			return put3("::buffa::types::put_int32_field", number, e_meth0(self_f(id), "to_i32"));
		PpNode *b = pp_block(s_let("val", false, e_meth0(self_f(id), "to_i32")),
				     stmt_if(pp_e_binary(pp_e("val"), "!=", pp_e_lit_int("0")),
					     pp_block(put3("::buffa::types::put_int32_field", number, pp_e("val")), NULL)),
				     NULL);
		return s_expr(pp_e_block(b));
	}
	if (ty == PT_MESSAGE)
		return stmt_if(e_meth0(self_f(id), "is_set"),
			       pp_block(len_delimited_header(number),
					s_semi(pp_e_method(self_f(id), "write_to", pp_e("__cache"), pp_e("buf"), NULL)), NULL));
	if (ty == PT_GROUP)
		return stmt_if(e_meth0(self_f(id), "is_set"),
			       pp_block(s_semi(e_call2("::buffa::types::put_group_start", lit_u32(number), pp_e("buf"))),
					s_semi(pp_e_method(self_f(id), "write_to", pp_e("__cache"), pp_e("buf"), NULL)),
					s_semi(e_call2("::buffa::types::put_group_end", lit_u32(number), pp_e("buf"))), NULL));
	PpNode *s = put3(tfn("::buffa::types::put_", ty, "_field"), number, self_f(id));
	return req ? s : stmt_if(is_non_default_expr(ty, id), pp_block(s, NULL));
}

static PpNode *scalar_merge_arm(const PbMsg *field, Feat parent, bool preserve)
{
	Feat f = feat_resolve_field(field, parent);
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, f);
	const char *id = ctx_field_ident(name, fd_number(field));
	PpNode *b = pp_block(wire_type_check(pp_e("tag"), wire_type_token(ty)), NULL);
	if (is_explicit_presence_scalar(field, ty, f)) {
		if (ty == PT_STRING || ty == PT_BYTES) {
			PpNode *ins = e_meth1(self_f(id), "get_or_insert_with",
					      pp_e(ty == PT_STRING ? "::buffa::alloc::string::String::new" : "::buffa::alloc::vec::Vec::new"));
			pp_push(b, s_semi(call_try(e_call2(ty == PT_STRING ? "::buffa::types::merge_string" : "::buffa::types::merge_bytes", ins, pp_e("buf")))));
		} else if (ty == PT_ENUM && is_closed_enum(f)) {
			Vec unk = {0};
			closed_enum_unknown_route(&unk, number, preserve);
			Vec known = one(s_semi(pp_e_assign(self_f(id), e_some(pp_e("__v")))));
			closed_enum_decode(b, pp_e("buf"), &known, &unk);
		} else if (ty == PT_ENUM) {
			pp_push(b, s_semi(pp_e_assign(self_f(id), e_some(e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", pp_e("buf"))))))));
		} else
			pp_push(b, s_semi(pp_e_assign(self_f(id), e_some(call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), pp_e("buf")))))));
		return arm_block(number, b);
	}
	switch (ty) {
	case PT_STRING: case PT_BYTES:
		pp_push(b, s_semi(call_try(e_call2(ty == PT_STRING ? "::buffa::types::merge_string" : "::buffa::types::merge_bytes",
						   pp_e_ref(true, self_f(id)), pp_e("buf")))));
		break;
	case PT_ENUM:
		if (is_closed_enum(f)) {
			Vec unk = {0};
			closed_enum_unknown_route(&unk, number, preserve);
			Vec known = one(s_semi(pp_e_assign(self_f(id), pp_e("__v"))));
			closed_enum_decode(b, pp_e("buf"), &known, &unk);
		} else
			pp_push(b, s_semi(pp_e_assign(self_f(id), e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", pp_e("buf")))))));
		break;
	case PT_MESSAGE:
		pp_push(b, s_semi(call_try(e_call3("::buffa::Message::merge_length_delimited", e_meth0(self_f(id), "get_or_insert_default"),
						   pp_e("buf"), pp_e("ctx")))));
		break;
	case PT_GROUP:
		pp_push(b, s_semi(call_try(pp_e_call(pp_e("::buffa::Message::merge_group"), e_meth0(self_f(id), "get_or_insert_default"),
						     pp_e("buf"), pp_e("ctx"), lit_u32(number), NULL))));
		break;
	default:
		pp_push(b, s_semi(pp_e_assign(self_f(id), call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), pp_e("buf"))))));
	}
	return arm_block(number, b);
}

/* ---- repeated fields ---- */

static PpNode *for_loop(PpNode *pat, PpNode *e, PpNode *body) { return s_expr(pp_e_for(pat, e, body)); }
static PpNode *pv(void) { return pp_p_ident("v", false, false); }

static PpNode *repeated_payload_size_expr(int ty, const char *id)
{
	if (is_fixed32_family(ty) || is_fixed64_family(ty) || ty == PT_BOOL)
		return pp_e_binary(cast_u64(e_meth0(self_f(id), "len")), "*", type_encoded_size_expr(ty, NULL));
	PpNode *closure;
	if (ty == PT_ENUM) {
		closure = pp_e_closure(cast_u64(e_call1("::buffa::types::int32_encoded_len", e_meth0(pp_e("v"), "to_i32"))), false);
		pp_push(closure, pv());
	} else {
		closure = pp_e_closure(type_encoded_size_expr(ty, pp_e("v")), false);
		pp_push(closure, pp_p_ref(false, pv()));
	}
	return pp_e_method_tf(e_meth1(e_meth0(self_f(id), "iter"), "map", closure), "sum", pp_angle(pp_ty("u64"), NULL), NULL);
}

static PpNode *repeated_compute_size_stmt(const PbMsg *field, Feat features)
{
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, features);
	const char *id = ctx_field_ident(name, fd_number(field));
	PpNode *elems = pp_e_ref(false, self_f(id));
	uint64_t ld = tag_encoded_len(number, 2), et = tag_encoded_len(number, wire_type_byte(ty));
	if (ty == PT_MESSAGE) {
		PpNode *b = pp_block(s_let("__slot", false, e_meth0(pp_e("__cache"), "reserve")),
				     s_let("inner_size", false, e_meth1(pp_e("v"), "compute_size", pp_e("__cache"))),
				     s_semi(pp_e_method(pp_e("__cache"), "set", pp_e("__slot"), pp_e("inner_size"), NULL)), NULL);
		pp_push(b, size_add(plus(plus(u64lit(ld), cast_u64(e_call1("::buffa::encoding::varint_len", cast_u64(pp_e("inner_size"))))),
					 cast_u64(pp_e("inner_size")))));
		return for_loop(pv(), elems, b);
	}
	if (ty == PT_GROUP) {
		PpNode *b = pp_block(s_let("inner_size", false, e_meth1(pp_e("v"), "compute_size", pp_e("__cache"))), NULL);
		pp_push(b, size_add(plus(plus(u64lit(et), cast_u64(pp_e("inner_size"))), u64lit(et))));
		return for_loop(pv(), elems, b);
	}
	if (!is_field_packed(field, features)) {
		if (is_fixed32_family(ty) || is_fixed64_family(ty) || ty == PT_BOOL)
			return size_add(pp_e_binary(cast_u64(e_meth0(self_f(id), "len")), "*",
						    pp_e_paren(plus(u64lit(et), type_encoded_size_expr(ty, NULL)))));
		PpNode *per;
		if (ty == PT_STRING || ty == PT_BYTES)
			per = size_add(plus(u64lit(ld), cast_u64(e_call1(tfn("::buffa::types::", ty, "_encoded_len"), pp_e("v")))));
		else if (ty == PT_ENUM)
			per = size_add(plus(u64lit(et), cast_u64(e_call1("::buffa::types::int32_encoded_len", e_meth0(pp_e("v"), "to_i32")))));
		else
			per = size_add(plus(u64lit(et), type_encoded_size_expr(ty, pp_e_unary("*", pp_e("v")))));
		return for_loop(pv(), elems, pp_block(per, NULL));
	}
	PpNode *b = pp_block(pp_s_local(pp_p_type(pp_p_ident("payload", false, false), pp_ty("u64")), repeated_payload_size_expr(ty, id)), NULL);
	pp_push(b, size_add(plus(plus(u64lit(ld), cast_u64(e_call1("::buffa::encoding::varint_len", pp_e("payload")))), pp_e("payload"))));
	return stmt_if(not(e_meth0(self_f(id), "is_empty")), b);
}

static PpNode *repeated_write_to_stmt(const PbMsg *field, Feat features)
{
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, features);
	const char *id = ctx_field_ident(name, fd_number(field));
	PpNode *elems = pp_e_ref(false, self_f(id));
	if (ty == PT_MESSAGE)
		return for_loop(pv(), elems,
				pp_block(len_delimited_header(number),
					 s_semi(pp_e_method(pp_e("v"), "write_to", pp_e("__cache"), pp_e("buf"), NULL)), NULL));
	if (ty == PT_GROUP)
		return for_loop(pv(), elems,
				pp_block(s_semi(e_call2("::buffa::types::put_group_start", lit_u32(number), pp_e("buf"))),
					 s_semi(pp_e_method(pp_e("v"), "write_to", pp_e("__cache"), pp_e("buf"), NULL)),
					 s_semi(e_call2("::buffa::types::put_group_end", lit_u32(number), pp_e("buf"))), NULL));
	if (!is_field_packed(field, features)) {
		PpNode *per;
		if (ty == PT_STRING)
			per = put3("::buffa::types::put_string_field", number, pp_e("v"));
		else if (ty == PT_BYTES)
			per = put3("::buffa::types::put_shared_bytes_field", number, pp_e("v"));
		else if (ty == PT_ENUM)
			per = put3("::buffa::types::put_int32_field", number, e_meth0(pp_e("v"), "to_i32"));
		else
			per = put3(tfn("::buffa::types::put_", ty, "_field"), number, pp_e_unary("*", pp_e("v")));
		return for_loop(pv(), elems, pp_block(per, NULL));
	}
	PpNode *b = pp_block(pp_s_local(pp_p_type(pp_p_ident("payload", false, false), pp_ty("u64")), repeated_payload_size_expr(ty, id)),
			     s_semi(e_call3("::buffa::types::put_len_delimited_header", lit_u32(number), pp_e("payload"), pp_e("buf"))), NULL);
	if (ty == PT_ENUM)
		pp_push(b, for_loop(pv(), elems, pp_block(s_semi(e_call2("::buffa::types::encode_int32", e_meth0(pp_e("v"), "to_i32"), pp_e("buf"))), NULL)));
	else
		pp_push(b, for_loop(pp_p_ref(false, pv()), elems, pp_block(s_semi(e_call2(tfn("::buffa::types::encode_", ty, ""), pp_e("v"), pp_e("buf"))), NULL)));
	return stmt_if(not(e_meth0(self_f(id), "is_empty")), b);
}

static PpNode *register_elem(const char *var)
{
	return s_semi(call_try(e_meth1(pp_e("ctx"), "register_element_memory",
				       e_call1("::buffa::__private::element_footprint", pp_e_ref(false, pp_e(var))))));
}

static PpNode *push_self(const char *id, PpNode *e) { return s_semi(e_meth1(self_f(id), "push", e)); }

static PpNode *repeated_merge_arm(const PbMsg *field, Feat parent, bool preserve)
{
	Feat f = feat_resolve_field(field, parent);
	const char *name = field_name_req(field);
	uint32_t number = validated_field_number(field);
	int ty = effective_type(field, f);
	const char *id = ctx_field_ident(name, fd_number(field));
	if (ty == PT_MESSAGE || ty == PT_GROUP) {
		PpNode *b = pp_block(wire_type_check(pp_e("tag"), wire_type_token(ty)),
				     s_let("elem", true, pp_e_call(pp_e("::core::default::Default::default"), NULL)), register_elem("elem"), NULL);
		if (ty == PT_MESSAGE)
			pp_push(b, s_semi(call_try(e_call3("::buffa::Message::merge_length_delimited", pp_e_ref(true, pp_e("elem")), pp_e("buf"), pp_e("ctx")))));
		else
			pp_push(b, s_semi(call_try(pp_e_call(pp_e("::buffa::Message::merge_group"), pp_e_ref(true, pp_e("elem")), pp_e("buf"),
							     pp_e("ctx"), lit_u32(number), NULL))));
		pp_push(b, push_self(id, pp_e("elem")));
		return arm_block(number, b);
	}
	if (!is_packed_type(ty)) {
		PpNode *dec = ty == PT_STRING ? call_try(e_call1("::buffa::types::decode_string", pp_e("buf")))
					      : call_try(e_call1("::buffa::types::decode_bytes", pp_e("buf")));
		return arm_block(number, pp_block(wire_check_ld(), s_let("__elem", false, dec), register_elem("__elem"),
						  push_self(id, pp_e("__elem")), NULL));
	}
	bool closed = is_closed_enum(f);
	PpNode *packed_elem = pp_block(NULL), *unpacked_elem = pp_block(NULL);
	if (ty == PT_ENUM && closed) {
		Vec unk = {0}, unk2 = {0};
		closed_enum_unknown_route(&unk, number, preserve);
		closed_enum_unknown_route(&unk2, number, preserve);
		Vec known = one(push_self(id, pp_e("__v"))), known2 = one(push_self(id, pp_e("__v")));
		closed_enum_decode(packed_elem, pp_e_ref(true, pp_e("limited")), &known, &unk);
		closed_enum_decode(unpacked_elem, pp_e("buf"), &known2, &unk2);
	} else if (ty == PT_ENUM) {
		pp_push(packed_elem, push_self(id, e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32_packed", pp_e_ref(true, pp_e("limited")))))));
		pp_push(unpacked_elem, push_self(id, e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", pp_e("buf"))))));
	} else {
		const char *pfn = is_fixed32_family(ty) || is_fixed64_family(ty) ? tfn("::buffa::types::decode_", ty, "") : tfn("::buffa::types::decode_", ty, "_packed");
		pp_push(packed_elem, push_self(id, call_try(e_call1(pfn, pp_e_ref(true, pp_e("limited"))))));
		pp_push(unpacked_elem, push_self(id, call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), pp_e("buf")))));
	}
	PpNode *amount = is_fixed32_family(ty) ? pp_e_binary(pp_e("len"), "/", pp_e_lit_int("4usize"))
			 : is_fixed64_family(ty) ? pp_e_binary(pp_e("len"), "/", pp_e_lit_int("8usize"))
						 : pp_e("len");
	Vec loop = {0};
	vec_push(&loop, s_semi(e_meth1(self_f(id), "reserve", amount)));
	vec_push(&loop, s_let("limited", true, e_meth1(pp_e("buf"), "take", pp_e("len"))));
	PpNode *wh = pp_block(NULL);
	for (int i = 0; i < packed_elem->list.n; i++)
		pp_push(wh, packed_elem->list.v[i]);
	vec_push(&loop, s_expr(pp_e_while(e_meth0(pp_e("limited"), "has_remaining"), wh)));
	vec_push(&loop, s_let("leftover", false, e_meth0(pp_e("limited"), "remaining")));
	vec_push(&loop, stmt_if(pp_e_binary(pp_e("leftover"), ">", pp_e_lit_int("0")),
				pp_block(s_semi(e_meth1(pp_e("limited"), "advance", pp_e("leftover"))), NULL)));
	PpNode *chunk_slice = pp_e_ref(false, pp_e_index(e_meth0(pp_e("buf"), "chunk"), pp_e_range(NULL, pp_e("len"), false)));
	PpNode *bulk = NULL;
	if (is_fixed32_family(ty) || is_fixed64_family(ty))
		bulk = s_semi(call_try(e_call2(tfn("::buffa::types::extend_packed_", ty, ""), chunk_slice, pp_e_ref(true, self_f(id)))));
	else if (ty != PT_ENUM)
		bulk = s_semi(call_try(e_call3(tfn("::buffa::types::extend_packed_", ty, ""), chunk_slice, pp_e_ref(true, self_f(id)), pp_e("len"))));
	PpNode *too_large = pp_e_closure(pp_e("::buffa::DecodeError::MessageTooLarge"), false);
	pp_push(too_large, pp_p_wild());
	PpNode *ld = pp_block(s_let("len", false, call_try(e_call1("::buffa::encoding::decode_varint", pp_e("buf")))),
			      s_let("len", false, call_try(e_meth1(e_call1("usize::try_from", pp_e("len")), "map_err", too_large))),
			      stmt_if(pp_e_binary(e_meth0(pp_e("buf"), "remaining"), "<", pp_e("len")),
				      pp_block(s_semi(pp_e_return(e_call1("::core::result::Result::Err", pp_e("::buffa::DecodeError::UnexpectedEof")))), NULL)),
			      NULL);
	if (bulk) {
		PpNode *inner = pp_block(bulk, s_semi(e_meth1(pp_e("buf"), "advance", pp_e("len"))), NULL);
		PpNode *els = pp_block(NULL);
		push_all(els, &loop);
		pp_push(ld, s_expr(pp_e_if(pp_e_binary(e_meth0(e_meth0(pp_e("buf"), "chunk"), "len"), ">=", pp_e("len")), inner, pp_e_block(els))));
	} else
		push_all(ld, &loop);
	PpNode *mismatch = pp_block(s_semi(pp_e_return(e_call1("::core::result::Result::Err",
								 e_call2("::buffa::encoding::wire_type_mismatch", pp_e("tag"),
									 pp_e("::buffa::encoding::WireType::LengthDelimited"))))),
				    NULL);
	PpNode *elif = pp_e_if(pp_e_binary(e_meth0(pp_e("tag"), "wire_type"), "==", wire_type_token(ty)), unpacked_elem, pp_e_block(mismatch));
	PpNode *iff = pp_e_if(pp_e_binary(e_meth0(pp_e("tag"), "wire_type"), "==", pp_e("::buffa::encoding::WireType::LengthDelimited")), ld, elif);
	return arm_block(number, pp_block(s_expr(iff), NULL));
}

/* ---- map fields ---- */

static const PbMsg *find_map_entry(const PbMsg *msg, const PbMsg *field)
{
	const char *tn = fd_type_name(field);
	if (!tn)
		return NULL;
	int n = pb_count(msg, DP_NESTED_TYPE);
	for (int i = 0; i < n; i++) {
		const PbMsg *nested = pb_msg_at(msg, DP_NESTED_TYPE, i);
		if (msg_is_map_entry(nested) && pb_has(nested, DP_NAME) && has_suffix(tn, xcat(".", dname(nested))))
			return nested;
	}
	return NULL;
}

static void find_map_entry_fields(const PbMsg *msg, const PbMsg *field, const PbMsg **key, const PbMsg **val)
{
	const PbMsg *entry = find_map_entry(msg, field);
	if (!entry)
		fail("%s", CODEGEN_ERROR("map entry not found for %s", fd_type_name(field) ? fd_type_name(field) : "<unknown>"));
	*key = *val = NULL;
	for (int i = 0; i < n_fields(entry); i++) {
		const PbMsg *f = field_at(entry, i);
		if (!*key && pb_has(f, FD_NUMBER) && fd_number(f) == 1)
			*key = f;
		if (!*val && pb_has(f, FD_NUMBER) && fd_number(f) == 2)
			*val = f;
	}
	if (!*key)
		fail("missing required descriptor field: map_entry.key");
	if (!*val)
		fail("missing required descriptor field: map_entry.value");
}

static PpNode *map_codec_token(int ty, Feat f)
{
	static const char *const names[19] = {"", "Double", "Float", "Int64", "Uint64", "Int32", "Fixed64", "Fixed32", "Bool", "Str",
					      "", "", "BytesVec", "Uint32", "", "Sfixed32", "Sfixed64", "Sint32", "Sint64"};
	if (ty == PT_ENUM)
		return pp_ty_gen(is_closed_enum(f) ? "::buffa::map_codec::ClosedEnum" : "::buffa::map_codec::OpenEnum", pp_ty_infer(), NULL);
	if (ty == PT_MESSAGE)
		return pp_ty_gen("::buffa::map_codec::Msg", pp_ty_infer(), NULL);
	if (ty == PT_GROUP)
		fail("map values cannot be groups");
	return pp_ty(xcat("::buffa::map_codec::", names[ty]));
}

typedef struct {
	uint32_t number;
	const char *id;
	uint64_t outer_tag_len;
	int val_ty;
	bool val_closed;
	PpNode *key_codec, *val_codec;
} MapEntryCtx;

static MapEntryCtx map_entry_ctx(const PbMsg *msg, const PbMsg *field, Feat features)
{
	MapEntryCtx m;
	const char *name = field_name_req(field);
	m.number = validated_field_number(field);
	const PbMsg *k, *v;
	find_map_entry_fields(msg, field, &k, &v);
	int kt = effective_type_in_map_entry(k, features);
	m.val_ty = effective_type_in_map_entry(v, features);
	Feat kf = feat_resolve_field(k, features), vf = feat_resolve_field(v, features);
	m.id = ctx_field_ident(name, fd_number(field));
	m.outer_tag_len = tag_encoded_len(m.number, 2);
	m.val_closed = m.val_ty == PT_ENUM && is_closed_enum(vf);
	m.key_codec = map_codec_token(kt, kf);
	m.val_codec = map_codec_token(m.val_ty, vf);
	return m;
}

static PpNode *codec_fn(const char *name, PpNode *a, PpNode *b)
{
	return pp_e_path(pp_path_args(pp_path(name), pp_angle(a, b ? b : pp_ty_infer(), pp_ty_infer(), NULL)));
}

static PpNode *map_compute_size_stmt(const PbMsg *msg, const PbMsg *field, Feat features)
{
	MapEntryCtx m = map_entry_ctx(msg, field, features);
	if (m.val_ty == PT_MESSAGE)
		return size_add(pp_e_call(codec_fn("::buffa::map_codec::message_field_len", m.key_codec, NULL),
					  pp_e_ref(false, self_f(m.id)), u64lit(m.outer_tag_len), pp_e("__cache"), NULL));
	return size_add(pp_e_call(codec_fn("::buffa::map_codec::field_len", m.key_codec, m.val_codec), pp_e_ref(false, self_f(m.id)),
				  u64lit(m.outer_tag_len), NULL));
}

static PpNode *map_write_to_stmt(const PbMsg *msg, const PbMsg *field, Feat features)
{
	MapEntryCtx m = map_entry_ctx(msg, field, features);
	if (m.val_ty == PT_MESSAGE)
		return s_semi(pp_e_call(codec_fn("::buffa::map_codec::write_message_field", m.key_codec, NULL), pp_e_ref(false, self_f(m.id)),
					lit_u32(m.number), pp_e("__cache"), pp_e("buf"), NULL));
	return s_semi(pp_e_call(codec_fn("::buffa::map_codec::write_field", m.key_codec, m.val_codec), pp_e_ref(false, self_f(m.id)),
				lit_u32(m.number), pp_e("buf"), NULL));
}

static PpNode *map_merge_arm(const PbMsg *msg, const PbMsg *field, Feat features)
{
	MapEntryCtx m = map_entry_ctx(msg, field, features);
	PpNode *call;
	if (m.val_closed && CFG->preserve_unknown_fields)
		call = pp_e_call(codec_fn("::buffa::map_codec::merge_entry_with_unknowns", m.key_codec, m.val_codec),
				 pp_e_ref(true, self_f(m.id)), pp_e("buf"), pp_e("ctx"),
				 e_some(pp_e_tuple(lit_u32(m.number), pp_e_ref(true, self_f("__buffa_unknown_fields")), NULL)), NULL);
	else
		call = pp_e_call(codec_fn("::buffa::map_codec::merge_entry", m.key_codec, m.val_codec), pp_e_ref(true, self_f(m.id)),
				 pp_e("buf"), pp_e("ctx"), NULL);
	return arm_block(m.number, pp_block(wire_check_ld(), s_semi(call_try(call)), NULL));
}

/* ---- oneofs ---- */

static char *oneof_variant_ident(const char *proto_name);
static bool variant_boxed(int ty, const char *variant_fqn) { return is_boxed_variant(ty) && !ctx_oneof_unboxed(variant_fqn); }

static PpNode *variant_pat(const char *qenum, const char *variant, const char *bind)
{
	return pp_p_tuple_struct(pp_path(xcat(qenum, "::", variant)), pp_p_ident(bind, false, false), NULL);
}

static PpNode *oneof_size_arm(const char *qenum, const char *variant, uint64_t tag_len, int ty)
{
	PpNode *tag = u64lit(tag_len), *b;
	const char *bind = "x";
	switch (ty) {
	case PT_STRING: case PT_BYTES:
		b = pp_block(size_add(plus(tag, cast_u64(e_call1(tfn("::buffa::types::", ty, "_encoded_len"), pp_e("x"))))), NULL);
		break;
	case PT_ENUM:
		b = pp_block(size_add(plus(tag, cast_u64(e_call1("::buffa::types::int32_encoded_len", e_meth0(pp_e("x"), "to_i32"))))), NULL);
		break;
	case PT_MESSAGE:
		b = pp_block(s_let("__slot", false, e_meth0(pp_e("__cache"), "reserve")),
			     s_let("inner", false, e_meth1(pp_e("x"), "compute_size", pp_e("__cache"))),
			     s_semi(pp_e_method(pp_e("__cache"), "set", pp_e("__slot"), pp_e("inner"), NULL)),
			     size_add(plus(plus(tag, cast_u64(e_call1("::buffa::encoding::varint_len", cast_u64(pp_e("inner"))))), cast_u64(pp_e("inner")))),
			     NULL);
		break;
	case PT_GROUP:
		b = pp_block(s_let("inner", false, e_meth1(pp_e("x"), "compute_size", pp_e("__cache"))),
			     size_add(plus(plus(tag, cast_u64(pp_e("inner"))), u64lit(tag_len))), NULL);
		break;
	default:
		if (is_fixed32_family(ty) || is_fixed64_family(ty) || ty == PT_BOOL) {
			bind = "_x";
			b = pp_block(size_add(plus(tag, type_encoded_size_expr(ty, NULL))), NULL);
		} else {
			bind = "v";
			b = pp_block(size_add(plus(tag, type_encoded_size_expr(ty, pp_e_unary("*", pp_e("v"))))), NULL);
		}
	}
	return pp_arm(variant_pat(qenum, variant, bind), NULL, pp_e_block(b));
}

static PpNode *oneof_write_arm(const char *qenum, const char *variant, uint32_t number, int ty)
{
	PpNode *b;
	switch (ty) {
	case PT_STRING: b = pp_block(put3("::buffa::types::put_string_field", number, pp_e("x")), NULL); break;
	case PT_BYTES: b = pp_block(put3("::buffa::types::put_shared_bytes_field", number, pp_e("x")), NULL); break;
	case PT_ENUM: b = pp_block(put3("::buffa::types::put_int32_field", number, e_meth0(pp_e("x"), "to_i32")), NULL); break;
	case PT_MESSAGE:
		b = pp_block(len_delimited_header(number), s_semi(pp_e_method(pp_e("x"), "write_to", pp_e("__cache"), pp_e("buf"), NULL)), NULL);
		break;
	case PT_GROUP:
		b = pp_block(s_semi(e_call2("::buffa::types::put_group_start", lit_u32(number), pp_e("buf"))),
			     s_semi(pp_e_method(pp_e("x"), "write_to", pp_e("__cache"), pp_e("buf"), NULL)),
			     s_semi(e_call2("::buffa::types::put_group_end", lit_u32(number), pp_e("buf"))), NULL);
		break;
	default: b = pp_block(put3(tfn("::buffa::types::put_", ty, "_field"), number, pp_e_unary("*", pp_e("x"))), NULL);
	}
	return pp_arm(variant_pat(qenum, variant, "x"), NULL, pp_e_block(b));
}

static PpNode *set_oneof(const char *field_ident, const char *qenum, const char *variant, PpNode *val)
{
	return s_semi(pp_e_assign(self_f(field_ident), e_some(e_call1(xcat(qenum, "::", variant), val))));
}

static PpNode *oneof_merge_arm(const char *field_ident, const char *qenum, const char *variant, uint32_t number, int ty, Feat f,
			       bool preserve, bool boxed)
{
	PpNode *b = pp_block(wire_type_check(pp_e("tag"), wire_type_token(ty)), NULL);
	switch (ty) {
	case PT_STRING: case PT_BYTES:
		pp_push(b, set_oneof(field_ident, qenum, variant, call_try(e_call1(ty == PT_STRING ? "::buffa::types::decode_string" : "::buffa::types::decode_bytes", pp_e("buf")))));
		break;
	case PT_ENUM:
		if (is_closed_enum(f)) {
			Vec unk = {0};
			closed_enum_unknown_route(&unk, number, preserve);
			Vec known = one(set_oneof(field_ident, qenum, variant, pp_e("__v")));
			closed_enum_decode(b, pp_e("buf"), &known, &unk);
		} else
			pp_push(b, set_oneof(field_ident, qenum, variant, e_call1("::buffa::EnumValue::from", call_try(e_call1("::buffa::types::decode_int32", pp_e("buf"))))));
		break;
	case PT_MESSAGE: case PT_GROUP: {
		PpNode *existing = boxed ? pp_e_ref(true, pp_e_unary("*", pp_e_unary("*", pp_e("existing")))) : pp_e("existing");
		PpNode *pat = p_core_some(pp_p_tuple_struct(pp_path(xcat(qenum, "::", variant)), pp_p_ident("existing", true, true), NULL));
		PpNode *m1, *m2;
		if (ty == PT_MESSAGE) {
			m1 = e_call3("::buffa::Message::merge_length_delimited", existing, pp_e("buf"), pp_e("ctx"));
			m2 = e_call3("::buffa::Message::merge_length_delimited", pp_e_ref(true, pp_e("val")), pp_e("buf"), pp_e("ctx"));
		} else {
			m1 = pp_e_call(pp_e("::buffa::Message::merge_group"), existing, pp_e("buf"), pp_e("ctx"), lit_u32(number), NULL);
			m2 = pp_e_call(pp_e("::buffa::Message::merge_group"), pp_e_ref(true, pp_e("val")), pp_e("buf"), pp_e("ctx"), lit_u32(number), NULL);
		}
		PpNode *wrapped = boxed ? e_call1("::buffa::alloc::boxed::Box::new", pp_e("val")) : pp_e("val");
		PpNode *els = pp_block(s_let("val", true, pp_e_call(pp_e("::core::default::Default::default"), NULL)), s_semi(call_try(m2)),
				       set_oneof(field_ident, qenum, variant, wrapped), NULL);
		pp_push(b, s_expr(pp_e_if(pp_e_let(pat, self_f(field_ident)), pp_block(s_semi(call_try(m1)), NULL), pp_e_block(els))));
		break;
	}
	default:
		pp_push(b, set_oneof(field_ident, qenum, variant, call_try(e_call1(tfn("::buffa::types::decode_", ty, ""), pp_e("buf")))));
	}
	return arm_block(number, b);
}

static void generate_oneof_impls(const FieldKind *k, const char *oneof_prefix, const char *proto_fqn, Feat features, bool preserve,
				 PpNode **compute, PpNode **write, Vec *merge_arms)
{
	const char *field_ident = ctx_oneof_ident(k->name);
	const char *qenum = xcat(oneof_prefix, k->enum_ident);
	PpNode *sm = pp_e_match(pp_e("v")), *wm = pp_e_match(pp_e("v"));
	for (int i = 0; i < k->fields.n; i++) {
		const PbMsg *field = k->fields.v[i];
		const char *name = field_name_req(field);
		uint32_t number = validated_field_number(field);
		int ty = effective_type(field, features);
		const char *variant = oneof_variant_ident(name);
		pp_push(sm, oneof_size_arm(qenum, variant, tag_encoded_len(number, wire_type_byte(ty)), ty));
		pp_push(wm, oneof_write_arm(qenum, variant, number, ty));
		Feat ff = feat_resolve_field(field, features);
		bool boxed = variant_boxed(ty, xcat(".", proto_fqn, ".", k->name, ".", name));
		vec_push(merge_arms, oneof_merge_arm(field_ident, qenum, variant, number, ty, ff, preserve, boxed));
	}
	PpNode *cond = pp_e_let(p_core_some(pp_p_ident("v", true, false)), self_f(field_ident));
	*compute = stmt_if(cond, pp_block(s_expr(sm), NULL));
	cond = pp_e_let(p_core_some(pp_p_ident("v", true, false)), self_f(field_ident));
	*write = stmt_if(cond, pp_block(s_expr(wm), NULL));
}

/* ---- impl Message ---- */

static void reflectable_impl_owned(Vec *out, const PbMsg *msg, const char *name, const Scope *s, const char **oneof_idents,
				   const char *oneof_prefix);

static PpNode *unknown_fields_ty(void) { return pp_ty("::buffa::UnknownFields"); }

static void generate_message_impl(Vec *out, const PbMsg *msg, bool preserve, const char *rust_name, const Scope *s,
				  const char **oneof_idents, const char *oneof_prefix)
{
	const char *pkg = s->pkg, *fqn = s->fqn;
	Feat features = s->features;
	Vec fields = classify_fields_ordered(msg, oneof_idents);
	const char *cache = message_uses_size_cache(msg, &fields, features, NULL) ? "__cache" : "_cache";
	Vec compute = {0}, write = {0}, arms = {0}, clear = {0};
	for (int i = 0; i < fields.n; i++) {
		const FieldKind *k = fields.v[i];
		switch (k->kind) {
		case FK_SCALAR:
			vec_push(&compute, scalar_compute_size_stmt(k->field, features));
			vec_push(&write, scalar_write_to_stmt(k->field, features));
			vec_push(&arms, scalar_merge_arm(k->field, features, preserve));
			vec_push(&clear, scalar_clear_stmt(k->field, pkg, features, s->nesting));
			break;
		case FK_REPEATED:
			vec_push(&compute, repeated_compute_size_stmt(k->field, features));
			vec_push(&write, repeated_write_to_stmt(k->field, features));
			vec_push(&arms, repeated_merge_arm(k->field, features, preserve));
			vec_push(&clear, s_semi(e_meth0(self_f(ctx_field_ident(field_name_req(k->field), fd_number(k->field))), "clear")));
			break;
		case FK_MAP:
			vec_push(&compute, map_compute_size_stmt(msg, k->field, features));
			vec_push(&write, map_write_to_stmt(msg, k->field, features));
			vec_push(&arms, map_merge_arm(msg, k->field, features));
			vec_push(&clear, s_semi(e_meth0(self_f(ctx_field_ident(field_name_req(k->field), fd_number(k->field))), "clear")));
			break;
		case FK_ONEOF: {
			PpNode *cs, *ws;
			generate_oneof_impls(k, oneof_prefix, fqn, features, preserve, &cs, &ws, &arms);
			vec_push(&compute, cs);
			vec_push(&write, ws);
			vec_push(&clear, s_semi(pp_e_assign(self_f(ctx_oneof_ident(k->name)), e_none())));
			break;
		}
		}
	}
	bool message_set = pb_int(pb_msg(msg, DP_OPTIONS), MOPT_MESSAGE_SET, 0) != 0;
	PpNode *uf = self_f("__buffa_unknown_fields");
	PpNode *uf_size = NULL, *uf_write = NULL, *uf_arm, *uf_clear = NULL;
	if (message_set) {
		PpNode *pat = pp_p_tuple_struct(pp_path("::buffa::UnknownFieldData::LengthDelimited"), pp_p_ident("bytes", true, false), NULL);
		PpNode *ifs = pp_e_if(pp_e_let(pat, pp_e_field(pp_e("f"), "data")),
				      pp_block(size_add(cast_u64(e_call2("::buffa::message_set::item_encoded_len", pp_e_field(pp_e("f"), "number"),
									 e_meth0(pp_e("bytes"), "len")))), NULL),
				      pp_e_block(pp_block(size_add(cast_u64(e_meth0(pp_e("f"), "encoded_len"))), NULL)));
		uf_size = for_loop(pp_p_ident("f", false, false), e_meth0(uf, "iter"), pp_block(s_expr(ifs), NULL));
		PpNode *wb = pp_block(NULL);
		const char *tags[] = {"::buffa::message_set::ITEM_START_TAG", "::buffa::message_set::TYPE_ID_TAG"};
		for (int i = 0; i < 2; i++)
			pp_push(wb, s_semi(e_call2("::buffa::encoding::encode_varint", pp_e(tags[i]), pp_e("buf"))));
		pp_push(wb, s_semi(e_call2("::buffa::encoding::encode_varint", cast_u64(pp_e_field(pp_e("f"), "number")), pp_e("buf"))));
		pp_push(wb, s_semi(e_call2("::buffa::encoding::encode_varint", pp_e("::buffa::message_set::MESSAGE_TAG"), pp_e("buf"))));
		pp_push(wb, s_semi(e_call2("::buffa::encoding::encode_varint", cast_u64(e_meth0(pp_e("bytes"), "len")), pp_e("buf"))));
		pp_push(wb, s_semi(e_meth1(pp_e("buf"), "put_slice", pp_e("bytes"))));
		pp_push(wb, s_semi(e_call2("::buffa::encoding::encode_varint", pp_e("::buffa::message_set::ITEM_END_TAG"), pp_e("buf"))));
		pat = pp_p_tuple_struct(pp_path("::buffa::UnknownFieldData::LengthDelimited"), pp_p_ident("bytes", true, false), NULL);
		ifs = pp_e_if(pp_e_let(pat, pp_e_field(pp_e("f"), "data")), wb,
			      pp_e_block(pp_block(s_semi(e_meth1(pp_e("f"), "write_to", pp_e("buf"))), NULL)));
		uf_write = for_loop(pp_p_ident("f", false, false), e_meth0(self_f("__buffa_unknown_fields"), "iter"), pp_block(s_expr(ifs), NULL));
		PpNode *ib = pp_block(pp_s_local(pp_p_tuple(pp_p_ident("type_id", false, false), pp_p_ident("bytes", false, false), NULL),
						 call_try(e_call2("::buffa::message_set::merge_item", pp_e("buf"), call_try(e_meth0(pp_e("ctx"), "descend"))))),
				      NULL);
		PpNode *st = pp_e_struct(pp_path("::buffa::UnknownField"), NULL);
		pp_push(st, pp_field_value("number", pp_e("type_id")));
		pp_push(st, pp_field_value("data", e_call1("::buffa::UnknownFieldData::LengthDelimited", pp_e("bytes"))));
		pp_push(ib, s_semi(e_meth1(self_f("__buffa_unknown_fields"), "push", st)));
		PpNode *eb = pp_block(s_semi(e_meth1(self_f("__buffa_unknown_fields"), "push",
						     call_try(e_call3("::buffa::encoding::decode_unknown_field", pp_e("tag"), pp_e("buf"), pp_e("ctx"))))),
				      NULL);
		PpNode *cond = pp_e_binary(pp_e_binary(e_meth0(pp_e("tag"), "field_number"), "==", pp_e_lit_int("1")), "&&",
					   pp_e_binary(e_meth0(pp_e("tag"), "wire_type"), "==", pp_e("::buffa::encoding::WireType::StartGroup")));
		uf_arm = pp_arm(pp_p_wild(), NULL, pp_e_block(pp_block(s_expr(pp_e_if(cond, ib, pp_e_block(eb))), NULL)));
	} else if (preserve) {
		uf_size = size_add(cast_u64(e_meth0(uf, "encoded_len")));
		uf_write = s_semi(e_meth1(self_f("__buffa_unknown_fields"), "write_to", pp_e("buf")));
		uf_arm = pp_arm(pp_p_wild(), NULL,
				pp_e_block(pp_block(s_semi(e_meth1(self_f("__buffa_unknown_fields"), "push",
								   call_try(e_call3("::buffa::encoding::decode_unknown_field", pp_e("tag"), pp_e("buf"), pp_e("ctx"))))),
						    NULL)));
	} else
		uf_arm = pp_arm(pp_p_wild(), NULL,
				pp_e_block(pp_block(s_semi(call_try(e_call3("::buffa::encoding::skip_field_depth", pp_e("tag"), pp_e("buf"),
									    e_meth0(pp_e("ctx"), "depth")))),
						    NULL)));
	if (preserve)
		uf_clear = s_semi(e_meth0(self_f("__buffa_unknown_fields"), "clear"));
	bool has_body = fields.n || preserve;

	vec_push(out, pp_item_macro(mac_src("::buffa::impl_default_instance", PP_DELIM_PAREN, rust_name), NULL));
	if (CFG->generate_reflection && !msg_is_map_entry(msg))
		reflectable_impl_owned(out, msg, rust_name, s, oneof_idents, oneof_prefix);
	vec_push(out, message_name_impl(pkg, fqn, NULL, pp_ty(rust_name)));

	PpNode *imp = pp_item_impl(pp_path("::buffa::Message"), pp_ty(rust_name));
	/* compute_size */
	PpNode *sig = pp_sig("compute_size", pp_ty("u32"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg(cache, ty_ref_mut(pp_ty("::buffa::SizeCache"))));
	PpNode *body = pp_block(allow_unused_use("::buffa::Enumeration as _"), s_let("size", has_body, pp_e_lit_int("0u64")), NULL);
	push_all(body, &compute);
	if (uf_size)
		pp_push(body, uf_size);
	pp_push(body, s_expr(e_call1("::buffa::saturate_size", pp_e("size"))));
	PpNode *fn = pp_item_fn(0, sig, body);
	pp_push_attr(fn, doc_attr(" Returns the total encoded size in bytes."));
	pp_push_attr(fn, doc_attr(""));
	pp_push_attr(fn, doc_attr(" Accumulates in `u64` (which cannot overflow for in-memory"));
	pp_push_attr(fn, doc_attr(" data) and saturates to `u32` at return, so a message whose"));
	pp_push_attr(fn, doc_attr(" encoded size exceeds the 2 GiB protobuf limit yields a value"));
	pp_push_attr(fn, doc_attr(" above [`::buffa::MAX_MESSAGE_BYTES`] that the encode entry"));
	pp_push_attr(fn, doc_attr(" points reject, never a silently wrapped size."));
	pp_push_attr(fn, attr_src("allow", "clippy::let_and_return"));
	pp_push(imp, fn);
	/* write_to */
	sig = pp_sig("write_to", NULL);
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg(cache, ty_ref_mut(pp_ty("::buffa::SizeCache"))));
	pp_push(sig, arg(has_body ? "buf" : "_buf", ty_ref_mut(ty_impl("::buffa::EncodeSink"))));
	body = pp_block(allow_unused_use("::buffa::Enumeration as _"), NULL);
	push_all(body, &write);
	if (uf_write)
		pp_push(body, uf_write);
	pp_push(imp, pp_item_fn(0, sig, body));
	/* merge_field */
	sig = pp_sig("merge_field", ty_g2("::core::result::Result", pp_ty_tuple(NULL), pp_ty("::buffa::DecodeError")));
	pp_push(sig, pp_receiver(true, true, NULL));
	pp_push(sig, arg("tag", pp_ty("::buffa::encoding::Tag")));
	pp_push(sig, arg("buf", ty_ref_mut(ty_impl("::buffa::bytes::Buf"))));
	pp_push(sig, arg("ctx", pp_ty_gen("::buffa::DecodeContext", pp_lifetime("'_"), NULL)));
	PpNode *m = pp_e_match(e_meth0(pp_e("tag"), "field_number"));
	push_all(m, &arms);
	pp_push(m, uf_arm);
	body = pp_block(allow_unused_use("::buffa::bytes::Buf as _"), allow_unused_use("::buffa::Enumeration as _"), s_expr(m),
			s_expr(e_ok_unit()), NULL);
	pp_push(imp, pp_item_fn(0, sig, body));
	/* clear */
	body = pp_block(NULL);
	push_all(body, &clear);
	if (uf_clear)
		pp_push(body, uf_clear);
	pp_push(imp, fn_self("clear", true, NULL, body));
	vec_push(out, imp);

	if (preserve) {
		PpNode *es = pp_item_impl(pp_path("::buffa::ExtensionSet"), pp_ty(rust_name));
		pp_push(es, const_static_str("PROTO_FQN", fqn));
		pp_push(es, fn_self("unknown_fields", false, ty_ref(unknown_fields_ty()),
				    pp_block(s_expr(pp_e_ref(false, self_f("__buffa_unknown_fields"))), NULL)));
		pp_push(es, fn_self("unknown_fields_mut", true, ty_ref_mut(unknown_fields_ty()),
				    pp_block(s_expr(pp_e_ref(true, self_f("__buffa_unknown_fields"))), NULL)));
		vec_push(out, es);
	}
}
