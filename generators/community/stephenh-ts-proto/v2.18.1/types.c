/* Port of ts-proto v2.18.1 src/types.ts. Functions that return a ts-poet Code return a TpCode
 * whose node is the type (or expression) the code stands for and whose items mirror the Code's
 * placeholders.
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

enum { T_DOUBLE = 1, T_FLOAT, T_INT64, T_UINT64, T_INT32, T_FIXED64, T_FIXED32, T_BOOL, T_STRING, T_GROUP,
       T_MESSAGE, T_BYTES, T_UINT32, T_ENUM, T_SFIXED32, T_SFIXED64, T_SINT32, T_SINT64 };
enum { LABEL_OPT = 1, LABEL_REQ = 2, LABEL_REP = 3 };

/* a number as ts-proto prints it into code (`-1` is a unary minus, `inf` an identifier) */
static Node *num_text(const char *text)
{
	if (text[0] == '-')
		return dts_unary(UNARY_MINUS, num_text(text + 1));
	if ((text[0] >= '0' && text[0] <= '9') || text[0] == '.')
		return dts_num(text);
	return dts_id(text);
}

static int basic_wire_type(int type)
{
	switch (type) {
	case T_DOUBLE:
		return 1;
	case T_FLOAT:
		return 5;
	case T_INT32:
	case T_ENUM:
	case T_UINT32:
	case T_SINT32:
		return 0;
	case T_FIXED32:
	case T_SFIXED32:
		return 5;
	case T_INT64:
	case T_UINT64:
	case T_SINT64:
		return 0;
	case T_FIXED64:
	case T_SFIXED64:
		return 1;
	case T_BOOL:
		return 0;
	case T_STRING:
	case T_BYTES:
	case T_MESSAGE:
		return 2;
	case T_GROUP:
		return 3;
	default:
		fail("Invalid type %d", type);
	}
}

static int basic_long_wire_type(int type)
{
	switch (type) {
	case T_INT64:
	case T_UINT64:
	case T_SINT64:
		return 0;
	case T_FIXED64:
	case T_SFIXED64:
		return 1;
	default:
		return -1;
	}
}

static bool is_repeated(const TpdField *f) { return f->label == LABEL_REP; }
static bool is_message(const TpdField *f) { return f->type == T_MESSAGE || f->type == T_GROUP; }
static bool is_enum(const TpdField *f) { return f->type == T_ENUM; }
static bool is_bytes(const TpdField *f) { return f->type == T_BYTES; }
static bool is_primitive(const TpdField *f) { return !is_message(f); }
static bool is_long(const TpdField *f) { return basic_long_wire_type(f->type) >= 0; }
static bool is_within_one_of(const TpdField *f) { return f->has_oneof_index; }
static bool is_scalar(const TpdField *f)
{
	return f->type >= T_DOUBLE && f->type <= T_SINT64 && f->type != T_GROUP && f->type != T_MESSAGE && f->type != T_ENUM;
}
static bool is_within_one_of_that_should_be_union(const TpdField *f)
{
	return is_within_one_of(f) && (O.oneof == ONEOF_UNIONS || O.oneof == ONEOF_UNIONS_VALUE) && !f->proto3_optional;
}
static bool is_whole_number(const TpdField *f)
{
	switch (f->type) {
	case T_INT32: case T_INT64: case T_UINT32: case T_UINT64: case T_SINT32: case T_SINT64: case T_FIXED32:
	case T_FIXED64: case T_SFIXED32: case T_SFIXED64:
		return true;
	default:
		return false;
	}
}
static bool is_floating_point(const TpdField *f)
{
	return f->type == T_DOUBLE || f->type == T_FLOAT || str_eq(f->type_name, ".google.protobuf.DoubleValue") ||
	       str_eq(f->type_name, ".google.protobuf.FloatValue");
}
static bool is_object_id(const TpdField *f) { return has_suffix(f->type_name, ".ObjectId"); }
static bool is_timestamp(const TpdField *f) { return str_eq(f->type_name, ".google.protobuf.Timestamp"); }
static bool is_any_value_type_name(const char *n) { return str_eq(n, "google.protobuf.Value") || str_eq(n, ".google.protobuf.Value"); }
static bool is_any_value_type(const TpdField *f) { return is_any_value_type_name(f->type_name); }
static bool is_bytes_value_type(const TpdField *f) { return str_eq(f->type_name, ".google.protobuf.BytesValue"); }
static bool is_field_mask_type_name(const char *n) { return str_eq(n, "google.protobuf.FieldMask") || str_eq(n, ".google.protobuf.FieldMask"); }
static bool is_field_mask_type(const TpdField *f) { return is_field_mask_type_name(f->type_name); }
static bool is_list_value_type_name(const char *n) { return str_eq(n, "google.protobuf.ListValue") || str_eq(n, ".google.protobuf.ListValue"); }
static bool is_list_value_type(const TpdField *f) { return is_list_value_type_name(f->type_name); }
static bool is_struct_type_name(const char *n) { return str_eq(n, "google.protobuf.Struct") || str_eq(n, ".google.protobuf.Struct"); }
static bool is_struct_type(const TpdField *f) { return is_struct_type_name(f->type_name); }
static bool is_long_value_type(const TpdField *f)
{
	return str_eq(f->type_name, ".google.protobuf.Int64Value") || str_eq(f->type_name, ".google.protobuf.UInt64Value");
}

static bool is_js_type_field_option(const TpdField *f)
{
	return O.use_js_type_override && f->options && (f->options->jstype == TPD_JS_NUMBER || f->options->jstype == TPD_JS_STRING);
}

/* getFieldOptionsJsType: the type override, or 0 */
static int get_field_options_js_type(const TpdField *f)
{
	if (!O.use_js_type_override || !f->options)
		return 0;
	switch (f->options->jstype) {
	case TPD_JS_STRING:
		return T_STRING;
	case TPD_JS_NUMBER:
		return T_INT64;
	default:
		return 0;
	}
}

static const char *to_reader_call(int type)
{
	switch (type) {
	case T_DOUBLE: return "double";
	case T_FLOAT: return "float";
	case T_INT32:
	case T_ENUM: return "int32";
	case T_UINT32: return "uint32";
	case T_SINT32: return "sint32";
	case T_FIXED32: return "fixed32";
	case T_SFIXED32: return "sfixed32";
	case T_INT64: return "int64";
	case T_UINT64: return "uint64";
	case T_SINT64: return "sint64";
	case T_FIXED64: return "fixed64";
	case T_SFIXED64: return "sfixed64";
	case T_BOOL: return "bool";
	case T_STRING: return "string";
	case T_BYTES: return "bytes";
	default:
		fail("Not a primitive field [object Object]");
	}
}

static int packed_type(int type)
{
	switch (type) {
	case T_DOUBLE:
	case T_FIXED64:
	case T_SFIXED64:
		return 1;
	case T_FLOAT:
	case T_FIXED32:
	case T_SFIXED32:
		return 5;
	case T_INT32: case T_ENUM: case T_UINT32: case T_SINT32: case T_INT64: case T_UINT64: case T_SINT64: case T_BOOL:
		return 0;
	default:
		return -1;
	}
}

/* packedField: `field.options?.packed ?? isProto3Syntax` (options.packed is false, never
 * undefined, when the options are present) */
static int packed_field(const TpdField *f)
{
	bool should_pack = f->options ? f->options->packed : C.is_proto3;
	return should_pack ? packed_type(f->type) : -1;
}

static TypeInfo *type_info(const char *proto_type)
{
	TypeInfo *t = map_get(&C.type_map, proto_type);
	if (!t)
		fail("No type found for %s", proto_type);
	return t;
}

/* a keyword or plain type as a code without placeholders */
static TpCode *tkw(const char *k) { return tp_code(KW(k)); }
static TpCode *tplain(const char *name) { return tp_code(tref(name, NULL)); }

static TpCode *long_type_name(void);
static TpCode *message_to_type_name(const char *proto_type, bool keep_value_type, bool repeated);

static TpCode *value_type_name(const char *type_name)
{
	if (str_eq(type_name, ".google.protobuf.StringValue"))
		return tkw("string");
	if (str_eq(type_name, ".google.protobuf.Int32Value") || str_eq(type_name, ".google.protobuf.UInt32Value") ||
	    str_eq(type_name, ".google.protobuf.DoubleValue") || str_eq(type_name, ".google.protobuf.FloatValue"))
		return tkw("number");
	if (str_eq(type_name, ".google.protobuf.Int64Value") || str_eq(type_name, ".google.protobuf.UInt64Value"))
		return long_type_name();
	if (str_eq(type_name, ".google.protobuf.BoolValue"))
		return tkw("boolean");
	if (str_eq(type_name, ".google.protobuf.BytesValue"))
		return O.env == ENV_NODE ? tplain("Buffer") : O.use_json_wire_format ? tkw("string") : tplain("Uint8Array");
	if (str_eq(type_name, ".google.protobuf.ListValue")) {
		Node *item = KW(O.use_unknown_for_value ? "unknown" : "any");
		return tp_code(tref(O.use_readonly_types ? "ReadonlyArray" : "Array", item, NULL));
	}
	if (str_eq(type_name, ".google.protobuf.Value"))
		return tkw(O.use_unknown_for_value ? "unknown" : "any");
	if (str_eq(type_name, ".google.protobuf.Struct")) {
		Node *lit = dts_ttype_lit();
		dts_ttype_lit_add(lit, HINT(dts_index_sig("key", KW("string"), KW(O.use_unknown_for_value ? "unknown" : "any"),
							  O.use_readonly_types),
					    H_NO_SEMI));
		return tp_code(lit);
	}
	if (str_eq(type_name, ".google.protobuf.FieldMask")) {
		if (O.use_json_wire_format)
			return tkw("string");
		Node *a = dts_tarray(KW("string"));
		return tp_code(O.use_readonly_types ? dts_treadonly(a) : a);
	}
	if (str_eq(type_name, ".google.protobuf.Duration") || str_eq(type_name, ".google.protobuf.Timestamp"))
		return O.use_json_wire_format ? tkw("string") : NULL;
	return NULL;
}

static bool is_value_type(const TpdField *f) { return value_type_name(f->type_name) != NULL; }

/* wrapperTypeName */
UNUSED static const char *wrapper_type_name(const char *type_name)
{
	static const char *const names[] = {".google.protobuf.StringValue", ".google.protobuf.Int32Value",
					    ".google.protobuf.UInt32Value", ".google.protobuf.DoubleValue",
					    ".google.protobuf.FloatValue", ".google.protobuf.Int64Value",
					    ".google.protobuf.UInt64Value", ".google.protobuf.BoolValue",
					    ".google.protobuf.BytesValue", ".google.protobuf.ListValue",
					    ".google.protobuf.Timestamp", ".google.protobuf.Struct", ".google.protobuf.Value"};
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (str_eq(type_name, names[i]))
			return type_name + strlen(".google.protobuf.");
	return NULL;
}

/* mapped to the module and type of `typeMap.get(protoType)` */
static TpCode *imp_type(const char *proto_type)
{
	TypeInfo *t = type_info(proto_type);
	TpCode *c = tp_code(NULL);
	TpImport *i = imp_proto(t->module, t->ts_name);
	c->node = tp_tref(c, i, (NodeList){0});
	return c;
}

static TpCode *message_to_type_name(const char *proto_type, bool keep_value_type, bool repeated)
{
	TpCode *value_type = value_type_name(proto_type);
	if (!keep_value_type && value_type) {
		if (repeated)
			return value_type;
		TpCode *c = tp_code(NULL);
		c->node = or_null_or_undefined(tp_sub(c, value_type), false);
		return c;
	}
	if (!keep_value_type && str_eq(proto_type, ".google.protobuf.Timestamp")) {
		if (O.use_date == DATE_DATE)
			return tplain("Date");
		if (O.use_date == DATE_TEMPORAL)
			return tplain("Temporal.Instant");
		if (O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO)
			return tkw("string");
	}
	if (!keep_value_type && O.use_mongo_object_id && has_suffix(proto_type, ".ObjectId"))
		return tplain("mongodb.ObjectId");
	return imp_type(proto_type);
}

static TpImport *get_enum_method(const char *enum_proto_type, const char *suffix)
{
	TypeInfo *t = type_info(enum_proto_type);
	return imp_proto(t->module, xcat(uncapitalize(t->ts_name), suffix));
}

/* basicTypeName(ctx, field, { keepValueType }); type: the field's type (or a jstype override) */
static TpCode *basic_type_name_t(const TpdField *f, int type, bool keep_value_type)
{
	int field_type = get_field_options_js_type(f);
	if (!field_type)
		field_type = type;
	switch (field_type) {
	case T_DOUBLE: case T_FLOAT: case T_INT32: case T_UINT32: case T_SINT32: case T_FIXED32: case T_SFIXED32:
		return tkw("number");
	case T_INT64: case T_UINT64: case T_SINT64: case T_FIXED64: case T_SFIXED64:
		if (is_js_type_field_option(f)) {
			if (f->options->jstype == TPD_JS_STRING)
				return tkw("string");
			if (f->options->jstype == TPD_JS_NUMBER)
				return tkw("number");
		}
		return long_type_name();
	case T_BOOL:
		return tkw("boolean");
	case T_STRING:
		return tkw("string");
	case T_BYTES:
		return tplain(O.env == ENV_NODE ? "Buffer" : "Uint8Array");
	case T_MESSAGE: case T_GROUP: case T_ENUM:
		return message_to_type_name(f->type_name, keep_value_type, is_repeated(f));
	default:
		return tp_code(dts_trefn(str_c(f->type_name), (NodeList){0}));
	}
}

static TpCode *basic_type_name(const TpdField *f, bool keep_value_type) { return basic_type_name_t(f, f->type, keep_value_type); }

/* the text of a simple type code (`toCodeString([])`) */
static const char *type_text(const TpCode *c)
{
	Node *n = c->node;
	if (n->kind == N_TS_KEYWORD_TYPE)
		return xstrndup(n->text.p, n->text.n);
	if (n->kind == N_TS_TYPE_REF && !n->b && n->a->kind == N_IDENT)
		return xstrndup(n->a->text.p, n->a->text.n);
	return "";
}

static TpCode *long_type_name(void)
{
	if (O.force_long == LONG_LONG) {
		TpCode *c = tp_code(NULL);
		c->node = tp_tref(c, utils_long(), (NodeList){0});
		return c;
	}
	return tkw(O.force_long == LONG_STRING ? "string" : O.force_long == LONG_BIGINT ? "bigint" : "number");
}

/* isOptionalProperty */
static bool is_optional_property(const TpdField *f, const TpdMessageOptions *mo, bool is_proto3)
{
	bool deprecated_only = O.uo_deprecated_only && f->options && f->options->deprecated;
	bool map_entry = mo && mo->map_entry;
	return (O.uo_true_messages_all && is_message(f) && !is_repeated(f)) || ((O.uo_all || deprecated_only) && !map_entry) ||
	       (O.no_defaults_for_optionals && !is_repeated(f) && (is_scalar(f) || is_enum(f))) ||
	       (!is_proto3 && f->label == LABEL_OPT && !map_entry && !O.disable_proto2_optionals) || is_within_one_of(f) ||
	       f->proto3_optional;
}

/* the fields of each oneof of a message (isWithinOneOf && oneofIndex === i), in field order;
 * computed once per message (upstream filters all fields for each oneof) */
static struct {
	TpdMessage *m;
	int *start, *idx;
} oneof_cache;

static const int *oneof_fields(TpdMessage *m, int oi, int *n)
{
	if (oneof_cache.m != m) {
		int k = m->noneof_decl;
		int *start = xalloc(sizeof(int) * (size_t)(k + 2));
		int *idx = xalloc(sizeof(int) * (size_t)(m->nfield + 1));
		for (int i = 0; i < m->nfield; i++) {
			TpdField *f = m->field[i];
			if (is_within_one_of(f) && f->oneof_index >= 0 && f->oneof_index < k)
				start[f->oneof_index + 1]++;
		}
		for (int i = 0; i < k; i++)
			start[i + 1] += start[i];
		int *fill = xalloc(sizeof(int) * (size_t)(k + 1));
		for (int i = 0; i < m->nfield; i++) {
			TpdField *f = m->field[i];
			if (is_within_one_of(f) && f->oneof_index >= 0 && f->oneof_index < k)
				idx[start[f->oneof_index] + fill[f->oneof_index]++] = i;
		}
		oneof_cache.m = m;
		oneof_cache.start = start;
		oneof_cache.idx = idx;
	}
	if (oi < 0 || oi >= m->noneof_decl) {
		*n = 0;
		return NULL;
	}
	*n = oneof_cache.start[oi + 1] - oneof_cache.start[oi];
	return oneof_cache.idx + oneof_cache.start[oi];
}

typedef struct {
	TpdMessage *msg;
	TpdField *key_field, *value_field;
	TpCode *key_type, *value_type;
} MapType;

static TpCode *to_type_name(TpdMessage *m, const TpdField *f, bool ensure_optional);

static int map_type_depth;

static bool detect_map_type(TpdMessage *m, const TpdField *f, MapType *out)
{
	if (f->label != LABEL_REP || f->type != T_MESSAGE)
		return false;
	TypeInfo *ti = map_get(&C.type_map, f->type_name);
	if (!ti)
		fail("TypeError: Cannot read properties of undefined (reading '2')");
	TpdMessage *mt = ti->msg;
	if (!mt) /* an enum: `.options?.mapEntry` of an EnumDescriptorProto is undefined */
		return false;
	if (!mt->options || !mt->options->map_entry)
		return false;
	if (mt->nfield < 2)
		fail("TypeError: Cannot read properties of undefined (reading 'options')");
	if (out) {
		/* a map entry whose key is a map of itself: upstream recurses until the stack overflows */
		if (++map_type_depth > 100)
			fail("RangeError: Maximum call stack size exceeded");
		out->msg = mt;
		out->key_field = mt->field[0];
		out->value_field = mt->field[1];
		out->key_type = to_type_name(m, mt->field[0], false);
		out->value_type = basic_type_name(mt->field[1], false);
		map_type_depth--;
	}
	return true;
}

static bool is_map_type(TpdMessage *m, const TpdField *f) { return detect_map_type(m, f, NULL); }

static bool should_generate_js_map_type(TpdMessage *m, const TpdField *f)
{
	if (O.use_map_type)
		return true;
	MapType mt;
	if (!detect_map_type(m, f, &mt))
		return false;
	return mt.key_field->type == T_BOOL || (is_long(mt.key_field) && (O.force_long == LONG_LONG || O.force_long == LONG_BIGINT));
}

static TpCode *finalize(const TpdField *f, TpCode *type, bool optional)
{
	if (!optional)
		return type;
	TpCode *c = tp_code(NULL);
	c->node = or_null_or_undefined(tp_sub(c, type), f->proto3_optional);
	return c;
}

static TpCode *to_type_name(TpdMessage *m, const TpdField *f, bool ensure_optional)
{
	int field_type = get_field_options_js_type(f);
	TpCode *type = basic_type_name_t(f, field_type ? field_type : f->type, false);
	if (is_repeated(f)) {
		MapType mt;
		if (m && detect_map_type(m, f, &mt)) {
			TpCode *c = tp_code(NULL);
			if (should_generate_js_map_type(m, f)) {
				Node *k = tp_sub(c, mt.key_type);
				c->node = tref("Map", k, tp_sub(c, mt.value_type), NULL);
			} else {
				Node *lit = dts_ttype_lit();
				Node *k = tp_sub(c, mt.key_type);
				Node *sig = dts_new(N_TS_INDEX_SIGNATURE);
				dts_add(&sig->list, dts_binding("key", k, false));
				sig->c = dts_type_ann(tp_sub(c, mt.value_type));
				dts_ttype_lit_add(lit, HINT(sig, H_NO_SEMI));
				c->node = lit;
			}
			return finalize(f, c, ensure_optional);
		}
		TpCode *c = tp_code(NULL);
		Node *a = dts_tarray(tp_sub(c, type));
		c->node = O.use_readonly_types ? dts_treadonly(a) : a;
		return finalize(f, c, ensure_optional);
	}
	if (is_value_type(f))
		return type;
	bool w = is_within_one_of(f);
	return finalize(f, type,
			(!w && is_message(f) && O.uo_none) || (w && O.oneof == ONEOF_PROPERTIES) || (w && f->proto3_optional) ||
				ensure_optional);
}

/* defaultValue: the value as a code (expression node); num/is_num: the number it is when ts-proto
 * returns a number */
typedef struct {
	TpCode *c;
	bool is_num;
	double num;
} DefaultValue;

static TpdEnumValue *enum_default(TpdEnum *e, const TpdField *f, bool use_default)
{
	for (int i = 0; i < e->nvalue; i++)
		if (use_default ? str_eq(e->value[i]->name, f->default_value) : e->value[i]->number == 0)
			return e->value[i];
	if (!e->nvalue)
		fail("TypeError: Cannot read properties of undefined (reading 'number')");
	return e->value[0];
}

static const char *get_member_name(TpdEnum *e, TpdEnumValue *v);

static DefaultValue default_value(const TpdField *f)
{
	DefaultValue r = {0};
	if (O.no_defaults_for_optionals) {
		r.c = tp_code(null_or_undefined());
		return r;
	}
	bool use_default = !C.is_proto3 && !O.disable_proto2_defaults && *f->default_value;
	const char *num = use_default ? f->default_value : "0";
	switch (f->type) {
	case T_DOUBLE: case T_FLOAT: case T_INT32: case T_UINT32: case T_SINT32: case T_FIXED32: case T_SFIXED32:
		r.c = tp_code(num_text(num));
		r.is_num = !use_default;
		return r;
	case T_ENUM: {
		TpdEnum *e = type_info(f->type_name)->en;
		if (!e)
			fail("TypeError: Cannot read properties of undefined (reading 'find')");
		TpdEnumValue *v = enum_default(e, f, use_default);
		if (O.string_enums) {
			TpCode *c = tp_code(NULL);
			TpCode *t = message_to_type_name(f->type_name, false, false);
			c->node = dts_member(tp_expr(tp_sub(c, t)), get_member_name(e, v));
			r.c = c;
		} else {
			r.c = tp_code(numi(v->number));
			r.is_num = true;
			r.num = v->number;
		}
		return r;
	}
	case T_INT64: case T_UINT64: case T_FIXED64: case T_SINT64: case T_SFIXED64:
		if (is_js_type_field_option(f)) {
			if (f->options->jstype == TPD_JS_STRING) {
				r.c = tp_code(dq(num));
				return r;
			}
			r.c = tp_code(num_text(num));
			r.is_num = !use_default;
			return r;
		}
		if (O.force_long == LONG_LONG) {
			TpCode *c = tp_code(NULL);
			Node *l = tp_ref(c, utils_long());
			const char *value = f->type == T_UINT64 || f->type == T_FIXED64 ? "UZERO" : "ZERO";
			c->node = use_default ? call(dts_member(l, "fromNumber"), num_text(num), NULL) : dts_member(l, value);
			r.c = c;
		} else if (O.force_long == LONG_STRING) {
			r.c = tp_code(dq(num));
		} else if (O.force_long == LONG_BIGINT) {
			Node *b = dts_leaf(N_BIGINT, str_c(xcat(num, "n")));
			r.c = tp_code(O.big_int_literal ? b : call(ID("BigInt"), dq(num), NULL));
		} else {
			r.c = tp_code(num_text(num));
			r.is_num = !use_default;
		}
		return r;
	case T_BOOL:
		r.c = tp_code(use_default ? num_text(f->default_value) : dts_bool(false));
		if (use_default && (str_eq(f->default_value, "true") || str_eq(f->default_value, "false")))
			r.c->node = dts_bool(str_eq(f->default_value, "true"));
		return r;
	case T_STRING:
		r.c = tp_code(use_default ? str_raw(quote_json(str_c(f->default_value))) : DQ(""));
		return r;
	case T_BYTES:
		if (O.env == ENV_NODE)
			r.c = tp_code(call(MEM(ID("Buffer"), "alloc"), NUM("0"), NULL));
		else
			r.c = tp_code(newx(ID("Uint8Array"), NUM("0"), NULL));
		return r;
	default:
		r.c = tp_code(null_or_undefined());
		return r;
	}
}

/* notDefaultCheck(ctx, field, messageOptions, place) */
static TpCode *not_default_check(const TpdField *f, const TpdMessageOptions *mo, Node *place)
{
	bool optional = is_optional_property(f, mo, C.is_proto3);
	TpCode *c = tp_code(NULL);
	if (O.no_defaults_for_optionals) {
		Node *e = BIN(NOTEQEQ, tp_clone(place), UNDEF());
		c->node = optional ? and_not_null(e, tp_clone(place)) : e;
		return c;
	}
	Node *check;
	switch (f->type) {
	case T_DOUBLE: case T_FLOAT: case T_INT32: case T_UINT32: case T_SINT32: case T_FIXED32: case T_SFIXED32:
	case T_BOOL: case T_STRING:
		check = BIN(NOTEQEQ, tp_clone(place), tp_sub(c, default_value(f).c));
		break;
	case T_ENUM: {
		TpdEnum *e = type_info(f->type_name)->en;
		if (!e)
			fail("TypeError: Cannot read properties of undefined (reading 'value')");
		DefaultValue d = default_value(f);
		TpdEnumValue *dv = NULL;
		for (int i = 0; i < e->nvalue && !dv; i++)
			if (d.is_num && e->value[i]->number == d.num)
				dv = e->value[i];
		if (!dv) {
			if (!e->nvalue)
				fail("TypeError: Cannot read properties of undefined (reading 'number')");
			dv = e->value[0];
		}
		if (O.string_enums) {
			TpCode *t = message_to_type_name(f->type_name, false, false);
			check = BIN(NOTEQEQ, tp_clone(place), dts_member(tp_expr(tp_sub(c, t)), get_member_name(e, dv)));
		} else {
			check = BIN(NOTEQEQ, tp_clone(place), numi(dv->number));
		}
		break;
	}
	case T_UINT64: case T_FIXED64: case T_INT64: case T_SINT64: case T_SFIXED64:
		if (O.force_long == LONG_LONG && !is_js_type_field_option(f))
			check = dts_unary(UNARY_BANG, call(dts_member(tp_clone(place), "equals"), tp_sub(c, default_value(f).c), NULL));
		else
			check = BIN(NOTEQEQ, tp_clone(place), tp_sub(c, default_value(f).c));
		break;
	case T_BYTES:
		check = BIN(NOTEQEQ, dts_member(tp_clone(place), "length"), NUM("0"));
		break;
	default:
		fail("Not implemented for the given type.");
	}
	if (optional)
		check = BIN(LOGICAL_AND, and_not_null(BIN(NOTEQEQ, tp_clone(place), UNDEF()), tp_clone(place)), check);
	c->node = check;
	return c;
}

/* createTypeMap */
static void save_mapping(void *arg, const char *ts_full_name, TpdMessage *m, SourceInfo *si, const char *proto_full_name)
{
	(void)si;
	TpdFile *f = arg;
	TypeInfo *t = xalloc(sizeof *t);
	t->module = js_replace_first(f->name, ".proto", "");
	t->ts_name = ts_full_name;
	t->msg = m;
	const char *prefix = *f->package ? xcat(".", f->package) : "";
	map_put(&C.type_map, xcat(prefix, ".", proto_full_name), t);
}

static void save_enum_mapping(void *arg, const char *ts_full_name, TpdEnum *e, SourceInfo *si, const char *proto_full_name)
{
	(void)si;
	TpdFile *f = arg;
	TypeInfo *t = xalloc(sizeof *t);
	t->module = js_replace_first(f->name, ".proto", "");
	t->ts_name = ts_full_name;
	t->en = e;
	const char *prefix = *f->package ? xcat(".", f->package) : "";
	map_put(&C.type_map, xcat(prefix, ".", proto_full_name), t);
}

COLD static void create_type_map(void)
{
	for (int i = 0; i < C.req->nproto_file; i++) {
		TpdFile *f = C.req->proto_file[i];
		SourceInfo si = {xalloc(sizeof(Map)), "", &empty_location};
		visit(f, &si, save_mapping, save_enum_mapping, f);
	}
}
