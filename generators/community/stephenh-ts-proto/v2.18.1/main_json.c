/* Port of ts-proto v2.18.1 src/main.ts: generateFromJson, generateCanonicalToJson, generateToJson,
 * generateFromPartial, convertFromObjectKey and convertToObjectKey.
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

static Node *cond_nl(Node *test, Node *cons, Node *alt, unsigned hints) { return HINT(dts_cond(test, cons, alt), hints); }

/* capitalize(valueType.toCodeString([])) */
static const char *cap_type(TpCode *t) { return capitalize(type_text(t)); }

/* code`${gt}.${cstr}(${from})` */
static TpCode *gt_conv(const char *cstr, Node *from)
{
	TpCode *c = tp_code(NULL);
	c->node = call(MEM(tp_co(c, C.u->global_this), cstr), tp_clone(from), NULL);
	return c;
}

static TpCode *fn_of(TpCond *co, Node *arg)
{
	TpCode *c = tp_code(NULL);
	Node *f = tp_co(c, co);
	c->node = call(f, arg, NULL);
	return c;
}

static TpCode *imp_call(TpImport *i, Node *arg)
{
	TpCode *c = tp_code(NULL);
	Node *f = tp_ref(c, i);
	c->node = call(f, arg, NULL);
	return c;
}

/* valueTypeFromJson */
static TpCode *value_type_from_json(const TpdField *f, Node *from)
{
	TpCode *vt = value_type_name(f->type_name);
	TpCode *c = tp_code(NULL);
	if (is_long_value_type(f) && O.force_long == LONG_LONG)
		c->node = mcall(ID(cap_type(vt)), "fromValue", tp_clone(from), NULL);
	else if (is_long_value_type(f) && O.force_long == LONG_BIGINT)
		c->node = call(ID("BigInt"), tp_clone(from), NULL);
	else if (is_bytes_value_type(f))
		c->node = newx(ID(cap_type(vt)), tp_clone(from), NULL);
	else
		c->node = call(ID(cap_type(vt)), tp_clone(from), NULL);
	return c;
}

/* the readSnippet of generateFromJson; returns NULL for code`${from}` (the input itself) */
static TpCode *from_json_read(TpdMessage *m, const TpdField *f, Node *from)
{
	if (is_enum(f))
		return imp_call(get_enum_method(f->type_name, "FromJSON"), tp_clone(from));
	if (is_primitive(f)) {
		if (is_bytes(f)) {
			TpCode *b = fn_of(C.u->bytes_from_base64, tp_clone(from));
			if (O.env != ENV_NODE)
				return b;
			TpCode *c = tp_code(NULL);
			c->node = call(MEM(ID("Buffer"), "from"), tp_sub(c, b), NULL);
			return c;
		}
		if (is_long(f) && is_js_type_field_option(f)) {
			int ft = get_field_options_js_type(f);
			return gt_conv(cap_type(basic_type_name_t(f, ft ? ft : f->type, true)), from);
		}
		if (is_long(f) && O.force_long == LONG_LONG) {
			TpCode *c = tp_code(NULL);
			c->node = mcall(ID(cap_type(basic_type_name(f, true))), "fromValue", tp_clone(from), NULL);
			return c;
		}
		if (is_long(f) && O.force_long == LONG_BIGINT) {
			TpCode *c = tp_code(NULL);
			c->node = call(ID("BigInt"), tp_clone(from), NULL);
			return c;
		}
		return gt_conv(cap_type(basic_type_name(f, true)), from);
	}
	if (is_object_id(f) && O.use_mongo_object_id)
		return fn_of(C.u->from_json_object_id, tp_clone(from));
	if (is_timestamp(f) && (O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO))
		return gt_conv("String", from);
	if (is_timestamp(f) && (O.use_date == DATE_DATE || O.use_date == DATE_TEMPORAL || O.use_date == DATE_TIMESTAMP))
		return fn_of(C.u->from_json_timestamp, tp_clone(from));
	if (is_any_value_type(f) || is_struct_type(f))
		return NULL;
	if (is_field_mask_type(f)) {
		TpCode *type = basic_type_name(f, true);
		TpCode *c = tp_code(NULL);
		Node *t1 = tp_expr(tp_sub(c, type));
		Node *t2 = tp_expr(tp_sub(c, type));
		c->node = mcall(t1, "unwrap", mcall(t2, "fromJSON", tp_clone(from), NULL), NULL);
		return c;
	}
	if (is_list_value_type(f)) {
		TpCode *c = tp_code(dts_arr(dts_spread_arg(tp_clone(from)), NULL));
		return c;
	}
	if (is_value_type(f))
		return value_type_from_json(f, from);
	if (is_message(f)) {
		MapType mt;
		if (is_repeated(f) && detect_map_type(m, f, &mt)) {
			TpdField *vf = mt.value_field;
			if (is_primitive(vf)) {
				if (is_bytes(vf)) {
					TpCode *b = fn_of(C.u->bytes_from_base64, dts_as(tp_clone(from), KW("string")));
					if (O.env != ENV_NODE)
						return b;
					TpCode *c = tp_code(NULL);
					c->node = call(MEM(ID("Buffer"), "from"), tp_sub(c, b), NULL);
					return c;
				}
				if (is_long(vf) && O.force_long == LONG_LONG)
					return tp_code(mcall(ID("Long"), "fromValue", dts_as(tp_clone(from), dts_tunion(tref("Long", NULL), KW("string"), NULL)), NULL));
				if (is_long(vf) && O.force_long == LONG_BIGINT)
					return tp_code(call(ID("BigInt"), dts_as(tp_clone(from), dts_tunion(KW("string"), KW("number"), KW("bigint"), KW("boolean"), NULL)), NULL));
				if (is_enum(vf))
					return imp_call(get_enum_method(vf->type_name, "FromJSON"), tp_clone(from));
				return gt_conv(cap_type(mt.value_type), from);
			}
			if (is_object_id(vf) && O.use_mongo_object_id)
				return fn_of(C.u->from_json_object_id, tp_clone(from));
			if (is_timestamp(vf) && (O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO))
				return gt_conv("String", from);
			if (is_timestamp(vf) && (O.use_date == DATE_DATE || O.use_date == DATE_TEMPORAL || O.use_date == DATE_TIMESTAMP))
				return fn_of(C.u->from_json_timestamp, tp_clone(from));
			if (is_long_value_type(vf))
				return value_type_from_json(vf, from);
			if (is_value_type(vf)) {
				TpCode *c = tp_code(NULL);
				c->node = dts_as(tp_clone(from), tp_sub(c, mt.value_type));
				return c;
			}
			if (is_any_value_type(vf))
				return NULL;
			TpCode *c = tp_code(NULL);
			c->node = mcall(tp_expr(tp_sub(c, mt.value_type)), "fromJSON", tp_clone(from), NULL);
			return c;
		}
		TpCode *type = basic_type_name(f, false);
		TpCode *c = tp_code(NULL);
		c->node = mcall(tp_expr(tp_sub(c, type)), "fromJSON", tp_clone(from), NULL);
		return c;
	}
	fail("Unhandled field [object Object]");
}

/* `${readSnippet(from)}` in a code: the node (a copy of from for code`${from}`) */
static Node *read_node(TpCode *parent, TpCode *read, Node *from) { return read ? tp_sub(parent, read) : tp_clone(from); }

/* convertFromObjectKey / convertToObjectKey */
static TpCode *convert_from_object_key(TpdMessage *m, const TpdField *f, const char *var)
{
	MapType mt;
	detect_map_type(m, f, &mt);
	if (str_eq(type_text(mt.key_type), "string"))
		return tp_code(ID(var));
	if (is_long(mt.key_field) && should_generate_js_map_type(m, f)) {
		if (O.force_long == LONG_LONG)
			return tp_code(mcall(ID(cap_type(mt.key_type)), "fromValue", ID(var), NULL));
		if (O.force_long == LONG_BIGINT)
			return tp_code(call(ID("BigInt"), ID(var), NULL));
		return gt_conv(O.force_long == LONG_STRING ? "String" : "Number", ID(var));
	}
	if (mt.key_field->type == T_BOOL)
		return gt_conv("Boolean", ID(var));
	return gt_conv("Number", ID(var));
}

static TpCode *convert_to_object_key(TpdMessage *m, const TpdField *f, const char *var)
{
	MapType mt;
	detect_map_type(m, f, &mt);
	if (str_eq(type_text(mt.key_type), "string"))
		return tp_code(ID(var));
	if (is_long(mt.key_field) && should_generate_js_map_type(m, f)) {
		if (O.force_long == LONG_LONG || O.force_long == LONG_BIGINT)
			return tp_code(mcall(ID(var), "toString", NULL));
		return tp_code(ID(var));
	}
	if (mt.key_field->type == T_BOOL)
		return gt_conv("String", ID(var));
	return tp_code(ID(var));
}

/* (gt.Object.entries(x) as [string, T][]) */
static Node *entries_as(TpCode *c, Node *x, Node *value_type)
{
	Node *e = mcall(MEM(tp_co(c, C.u->global_this), "Object"), "entries", x, NULL);
	return dts_paren(dts_as(e, dts_tarray(dts_ttuple(KW("string"), value_type, NULL))));
}

/* [key, value]: [string, T] */
static Node *kv_pat(const char *k, const char *v, Node *type)
{
	Node *pat = dts_new(N_ARRAY_PAT);
	dts_add(&pat->list, dts_binding(k, NULL, false));
	dts_add(&pat->list, dts_binding(v, NULL, false));
	if (type)
		pat->b = dts_type_ann(type);
	return pat;
}

/* the map reduce of fromJSON (into a Map or an object) */
static Node *from_json_map_reduce(TpCode *c, TpdMessage *m, const TpdField *f, Node *json, TpCode *field_type, bool js_map)
{
	TpCode *i = convert_from_object_key(m, f, "key");
	Node *e = entries_as(c, tp_clone(json), KW("any"));
	Node *ft = tp_sub(c, field_type);
	NodeList body;
	if (js_map) {
		Node *k = tp_sub(c, i);
		TpCode *r = from_json_read(m, f, ID("value"));
		body = L(ES(mcall(ID("acc"), "set", k, read_node(c, r, ID("value")), NULL)), RET(ID("acc")), NULL);
	} else {
		Node *g = tp_co(c, C.u->global_this);
		Node *k = tp_sub(c, i);
		TpCode *r = from_json_read(m, f, ID("value"));
		Node *desc = obj(NL(kv("value", read_node(c, r, ID("value"))), LINE), NL(kv("enumerable", dts_bool(true)), LINE),
				 kv("configurable", dts_bool(true)), kv("writable", dts_bool(true)), NULL);
		desc->hints |= H_TRAILING_COMMA;
		body = L(ES(mcall(MEM(g, "Object"), "defineProperty", ID("acc"), k, desc, NULL)), RET(ID("acc")), NULL);
	}
	NodeList ps = L(dts_binding("acc", ft, false), kv_pat("key", "value", dts_ttuple(KW("string"), KW("any"), NULL)), NULL);
	return mcall(e, "reduce", arrow(ps, NULL, blockl(body)), js_map ? newx(ID("Map"), NULL) : dts_obj(), NULL);
}

static Node *tpl_of(int n, const char *const *quasis, Node *const *exprs);

/* useCanonicalAnyJson */
static bool use_canonical_any_json(void) { return opt_is(&O.opts, "anyType", "@type") && O.output_type_registry && O.output_encode_true; }

/* anyFields: the fields numbered 1 and 2 */
static TpdField *any_field(TpdMessage *m, int number)
{
	for (int i = 0; i < m->nfield; i++)
		if (m->field[i]->number == number)
			return m->field[i];
	fail("TypeError: Cannot read properties of undefined (reading '%s')", O.use_json_name ? "jsonName" : "name");
}

/* `throw new globalThis.Error(`<msg>${typeName}`);` */
static Node *throw_unknown_any_type(TpCode *c, const char *msg)
{
	const char *q[2] = {msg, ""};
	Node *e = ID("typeName");
	return dts_throw(newx(MEM(tp_co(c, C.u->global_this), "Error"), tpl_of(1, q, &e), NULL));
}

/* const typeName = typeUrl.slice(typeUrl.lastIndexOf("/") + 1); const type = messageTypeRegistry.get(typeName); if (...) throw */
static void any_type_lookup(TpCode *c, NodeList *body, const char *msg)
{
	TpImport *reg = imp_file("messageTypeRegistry@./typeRegistry");
	dts_add(body, const_("typeName", NULL, mcall(ID("typeUrl"), "slice", BIN(ADD, mcall(ID("typeUrl"), "lastIndexOf", DQ("/"), NULL), NUM("1")), NULL)));
	dts_add(body, const_("type", NULL, mcall(tp_ref(c, reg), "get", ID("typeName"), NULL)));
	dts_add(body, dts_if(BIN(EQEQEQ, ID("type"), UNDEF()), block(throw_unknown_any_type(c, msg), NULL), NULL));
}

/* generateCanonicalAnyToJson */
static TpCode *canonical_any_to_json(const char *full_name, TpdMessage *m)
{
	TpCode *c = tp_code(NULL);
	TpdField *tu = any_field(m, 1), *va = any_field(m, 2);
	NodeList body = L(const_("typeUrl", NULL, BIN(NULLISH, prop("message", get_field_name(tu)), DQ(""))),
			  dts_if(BIN(EQEQEQ, ID("typeUrl"), DQ("")), block(RET(dts_obj()), NULL), NULL), NULL);
	any_type_lookup(c, &body, "Cannot convert google.protobuf.Any to JSON: unknown type ");
	Node *dec = mcall(ID("type"), "decode", BIN(NULLISH, prop("message", get_field_name(va)), newx(ID("Uint8Array"), NUM("0"), NULL)), NULL);
	dts_add(&body, const_("json", NULL, mcall(ID("type"), "toJSON", dec, NULL)));
	Node *test = mcall(tp_co(c, C.u->any_json_value_types), "includes", ID("typeName"), NULL);
	Node *a = obj(dts_kv(DQ("@type"), ID("typeUrl")), kv("value", ID("json")), NULL);
	Node *b = obj(dts_kv(DQ("@type"), ID("typeUrl")), dts_spread(dts_paren(dts_as(ID("json"), KW("object")))), NULL);
	dts_add(&body, RET(HINT(dts_cond(test, a, b), H_OP_NL | H_OP2_NL)));
	c->node = method("toJSON", NULL, L(param("message", tref(full_name, NULL), false, NULL), NULL), KW("unknown"), blockl(body));
	return c;
}

/* generateCanonicalAnyFromJson */
static TpCode *canonical_any_from_json(const char *full_name, TpdMessage *m)
{
	TpCode *c = tp_code(NULL);
	TpdField *tu = any_field(m, 1), *va = any_field(m, 2);
	Node *tu_key = safe_key(get_field_name(tu)), *va_key = safe_key(get_field_name(va));
	Node *at = dts_opt_index(ID("object"), DQ("@type"), true);
	Node *s1 = const_("typeUrl", NULL, dts_cond(call(tp_co(c, C.u->is_set), at, NULL),
						     call(MEM(tp_co(c, C.u->global_this), "String"), dts_index(ID("object"), DQ("@type")), NULL), DQ("")));
	Node *empty = dts_obj();
	if (O.add_type_to_messages)
		dts_obj_add(empty, kv("$type", MEM(ID(full_name), "$type")));
	dts_obj_add(empty, dts_kv(tu_key, tp_sub(c, default_value(tu).c)));
	dts_obj_add(empty, dts_kv(va_key, tp_sub(c, default_value(va).c)));
	NodeList body = L(s1, dts_if(BIN(EQEQEQ, ID("typeUrl"), DQ("")), block(RET(empty), NULL), NULL), NULL);
	any_type_lookup(c, &body, "Cannot convert JSON to google.protobuf.Any: unknown type ");
	dts_add(&body, let_("json", NULL, MEM(ID("object"), "value")));
	Node *inc = dts_unary(UNARY_BANG, mcall(tp_co(c, C.u->any_json_value_types), "includes", ID("typeName"), NULL));
	dts_add(&body, dts_if(inc, block(ES(SET(ID("json"), obj(dts_spread(ID("object")), NULL))),
					 ES(dts_unary(UNARY_DELETE, dts_index(ID("json"), DQ("@type")))), NULL), NULL));
	dts_add(&body, const_("bytes", NULL, mcall(mcall(ID("type"), "encode", mcall(ID("type"), "fromJSON", ID("json"), NULL), NULL), "finish", NULL)));
	Node *ro = dts_obj();
	if (O.add_type_to_messages)
		dts_obj_add(ro, kv("$type", MEM(ID(full_name), "$type")));
	dts_obj_add(ro, dts_kv(tp_clone(tu_key), ID("typeUrl")));
	dts_obj_add(ro, dts_kv(tp_clone(va_key), O.env == ENV_NODE ? call(MEM(ID("Buffer"), "from"), ID("bytes"), NULL) : ID("bytes")));
	dts_add(&body, RET(ro));
	c->node = method("fromJSON", NULL, L(param("object", KW("any"), false, NULL), NULL), tref(full_name, NULL), blockl(body));
	return c;
}

/* `if (test) throw new Error("Invalid duration string");` */
static Node *throw_invalid_duration(Node *test)
{
	return dts_if(test, dts_throw(newx(ID("Error"), DQ("Invalid duration string"), NULL)), NULL);
}

static Node *tpl_of(int n, const char *const *quasis, Node *const *exprs)
{
	Str q[4];
	for (int i = 0; i <= n; i++)
		q[i] = str_c(quasis[i]);
	return dts_tpl(n, q, exprs);
}

/* generateFromJson for google.protobuf.Duration with useDuration=string */
static TpCode *duration_from_json(const char *full_name)
{
	TpCode *c = tp_code(NULL);
	TpCode *ps = tp_add(c, tp_code(NULL));
	Node *neg_nz = BIN(LOGICAL_AND, ID("negative"), BIN(NOTEQEQ, ID("secondsStr"), DQ("0")));
	Node *parse;
	switch (O.force_long) {
	case LONG_BIGINT:
		parse = dts_cond(neg_nz, dts_unary(UNARY_MINUS, call(ID("BigInt"), ID("secondsStr"), NULL)), call(ID("BigInt"), ID("secondsStr"), NULL));
		break;
	case LONG_LONG:
		parse = mcall(tp_ref(ps, C.u->long_), "fromString",
			      dts_cond(ID("negative"), BIN(ADD, DQ("-"), ID("secondsStr")), ID("secondsStr")), NULL);
		break;
	case LONG_STRING:
		parse = dts_cond(neg_nz, BIN(ADD, DQ("-"), ID("secondsStr")), ID("secondsStr"));
		break;
	default:
		parse = dts_cond(neg_nz, dts_unary(UNARY_MINUS, call(ID("Number"), ID("secondsStr"), NULL)), call(ID("Number"), ID("secondsStr"), NULL));
		break;
	}
	Node *dot_neg = BIN(LT, ID("dot"), NUM("0"));
	NodeList body = L(throw_invalid_duration(dts_unary(UNARY_BANG, mcall(ID("object"), "endsWith", DQ("s"), NULL))),
			  const_("body", NULL, mcall(ID("object"), "slice", NUM("0"), dts_unary(UNARY_MINUS, NUM("1")), NULL)),
			  const_("negative", NULL, mcall(ID("body"), "startsWith", DQ("-"), NULL)),
			  const_("unsigned", NULL, dts_cond(ID("negative"), mcall(ID("body"), "slice", NUM("1"), NULL), ID("body"))),
			  const_("dot", NULL, mcall(ID("unsigned"), "indexOf", DQ("."), NULL)),
			  const_("secondsStr", NULL, dts_cond(dot_neg, ID("unsigned"), mcall(ID("unsigned"), "slice", NUM("0"), ID("dot"), NULL))),
			  const_("fracStr", NULL,
				 dts_cond(BIN(LT, ID("dot"), NUM("0")), DQ(""), mcall(ID("unsigned"), "slice", BIN(ADD, ID("dot"), NUM("1")), NULL))),
			  throw_invalid_duration(BIN(EQEQEQ, MEM(ID("secondsStr"), "length"), NUM("0"))), const_("seconds", NULL, parse),
			  const_("nanosAbs", NULL,
				 dts_cond(BIN(EQEQEQ, ID("fracStr"), DQ("")), NUM("0"),
					  call(ID("Number"), mcall(mcall(ID("fracStr"), "padEnd", NUM("9"), DQ("0"), NULL), "slice", NUM("0"), NUM("9"), NULL), NULL))),
			  const_("nanos", NULL,
				 dts_cond(BIN(LOGICAL_AND, ID("negative"), BIN(NOTEQEQ, ID("nanosAbs"), NUM("0"))), dts_unary(UNARY_MINUS, ID("nanosAbs")),
					  ID("nanosAbs"))),
			  RET(obj(shorthand("seconds"), shorthand("nanos"), NULL)), NULL);
	c->node = method("fromJSON", NULL, L(param("object", KW("string"), false, NULL), NULL), tref(full_name, NULL), blockl(body));
	return c;
}

/* generateCanonicalToJson for google.protobuf.Duration with useDuration=string */
static TpCode *duration_to_json(const char *full_name)
{
	TpCode *c = tp_code(NULL);
	tp_add(c, tp_code(NULL));
	tp_add(c, tp_code(NULL));
	Node *secs = MEM(ID("message"), "seconds");
	Node *is_neg, *abs;
	switch (O.force_long) {
	case LONG_BIGINT:
		is_neg = BIN(LT, secs, dts_leafc(N_BIGINT, "0n"));
		abs = dts_cond(BIN(LT, tp_clone(secs), dts_leafc(N_BIGINT, "0n")), dts_unary(UNARY_MINUS, tp_clone(secs)), tp_clone(secs));
		break;
	case LONG_LONG:
		is_neg = mcall(secs, "isNegative", NULL);
		abs = dts_cond(mcall(tp_clone(secs), "isNegative", NULL), mcall(mcall(tp_clone(secs), "toString", NULL), "slice", NUM("1"), NULL),
			       mcall(tp_clone(secs), "toString", NULL));
		break;
	case LONG_STRING:
		is_neg = mcall(secs, "startsWith", DQ("-"), NULL);
		abs = dts_cond(mcall(tp_clone(secs), "startsWith", DQ("-"), NULL), mcall(tp_clone(secs), "slice", NUM("1"), NULL), tp_clone(secs));
		break;
	default:
		is_neg = BIN(LT, secs, NUM("0"));
		abs = dts_cond(BIN(LT, tp_clone(secs), NUM("0")), dts_unary(UNARY_MINUS, tp_clone(secs)), tp_clone(secs));
		break;
	}
	Node *nanos = MEM(ID("message"), "nanos");
	static const char *const q1[] = {"", "", "s"};
	static const char *const q2[] = {"", "", ".", "s"};
	Node *e1[] = {ID("sign"), ID("absSeconds")};
	Node *e2[] = {ID("sign"), ID("absSeconds"), mcall(ID("padded"), "slice", NUM("0"), ID("len"), NULL)};
	Node *len = dts_cond(BIN(EQEQEQ, BIN(MOD, ID("absNanos"), NUM("1_000_000")), NUM("0")), NUM("3"),
			     dts_cond(BIN(EQEQEQ, BIN(MOD, ID("absNanos"), NUM("1_000")), NUM("0")), NUM("6"), NUM("9")));
	NodeList body = L(const_("negative", NULL, BIN(LOGICAL_OR, is_neg, BIN(LT, nanos, NUM("0")))),
			  const_("sign", NULL, dts_cond(ID("negative"), DQ("-"), DQ(""))), const_("absSeconds", NULL, abs),
			  const_("absNanos", NULL, dts_cond(BIN(LT, tp_clone(nanos), NUM("0")), dts_unary(UNARY_MINUS, tp_clone(nanos)), tp_clone(nanos))),
			  dts_if(BIN(EQEQEQ, ID("absNanos"), NUM("0")), RET(tpl_of(2, q1, e1)), NULL),
			  const_("padded", NULL, mcall(mcall(ID("absNanos"), "toString", NULL), "padStart", NUM("9"), DQ("0"), NULL)),
			  const_("len", NULL, len), RET(tpl_of(3, q2, e2)), NULL);
	c->node = method("toJSON", NULL, L(param("message", tref(full_name, NULL), false, NULL), NULL), KW("string"), blockl(body));
	return c;
}

static TpCode *generate_from_json(const char *full_name, const char *full_type_name, TpdMessage *m)
{
	if (str_eq(full_type_name, "google.protobuf.Any") && use_canonical_any_json())
		return canonical_any_from_json(full_name, m);
	if (str_eq(full_type_name, "google.protobuf.Duration") && opt_is(&O.opts, "useDuration", "string"))
		return duration_from_json(full_name);
	TpCode *c = tp_code(NULL);
	NodeList body = {0};
	Node *ret = dts_obj();
	TpCode *head = tp_add(c, tp_code(NULL));
	const char *pname = m->nfield > 0 ? "object" : "_";
	if (str_eq(full_type_name, "google.protobuf.Timestamp")) {
		pname = "object";
		Node *fn = tp_co(head, C.u->timestamp_from_json);
		Node *r = RET(mcall(ID(full_name), "fromJSON", call(fn, ID("object"), NULL), NULL));
		dts_add(&body, dts_if(BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, ID("object")), DQ("string")), block(r, NULL), NULL));
	}
	if (O.add_type_to_messages)
		dts_obj_add(ret, kv("$type", MEM(ID(full_name), "$type")));
	Node *union_cond = NULL, **union_slot = NULL; /* the alternate the next union case fills */
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *field_name = get_field_name(f);
		Node *field_key = safe_key(field_name);
		const char *json_name = get_field_json_name(f->name, f->json_name);
		Node *jp = prop("object", json_name);
		Node *jpo = prop_access(ID("object"), json_name, true);
		bool no_default = !O.initialize_fields_as_undefined && is_optional_property(f, m->options, C.is_proto3);
		bool pj = O.proto_json_format && !str_eq(f->name, f->json_name);
		Node *pjp = prop("object", f->name);
		Node *pjpo = prop_access(ID("object"), f->name, true);
		TpCode *fc = tp_add(c, tp_code(NULL));
		if (str_eq(full_type_name, "google.protobuf.FieldMask") && str_eq(field_name, "paths")) {
			TpCode *pc = tp_add(fc, tp_code(NULL));
			Node *o = ID("object");
			Node *a = mcall(mcall(tp_clone(o), "split", DQ(","), NULL), "filter", MEM(tp_co(pc, C.u->global_this), "Boolean"), NULL);
			Node *t2 = call(MEM(MEM(tp_co(pc, C.u->global_this), "Array"), "isArray"), dts_opt_member(tp_clone(o), "paths", true), NULL);
			Node *b = mcall(MEM(tp_clone(o), "paths"), "map", MEM(tp_co(pc, C.u->global_this), "String"), NULL);
			Node *inner = cond_nl(t2, b, dts_arr(NULL), H_OP_NL | H_OP2_NL);
			Node *t1 = BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, dts_paren(tp_clone(o))), SQ("string"));
			dts_obj_add(ret, kv(field_name, cond_nl(t1, a, inner, H_OP_NL | H_OP2_NL)));
			continue;
		}
		if (is_repeated(f)) {
			MapType mt;
			if (detect_map_type(m, f, &mt)) {
				TpCode *ft = to_type_name(m, f, false);
				bool js_map = should_generate_js_map_type(m, f);
				Node *fallback = no_default ? null_or_undefined() : js_map ? newx(ID("Map"), NULL) : dts_obj();
				Node *pjc = NULL;
				TpCode *pjcode = NULL;
				if (pj) {
					pjcode = tp_code(NULL);
					Node *t = call(tp_co(pjcode, C.u->is_object), tp_clone(pjp), NULL);
					pjc = cond_nl(t, from_json_map_reduce(pjcode, m, f, pjp, ft, js_map), NULL, H_OP_NL | H_OP2_NL);
				}
				Node *t = call(tp_co(fc, C.u->is_object), tp_clone(jp), NULL);
				Node *v = from_json_map_reduce(fc, m, f, jp, ft, js_map);
				if (pjcode)
					tp_add(fc, pjcode);
				if (pjc) {
					pjc->c = fallback;
					fallback = pjc;
				}
				dts_obj_add(ret, dts_kv(field_key, cond_nl(t, v, fallback, H_OP_NL | H_OP2_NL)));
			} else {
				Node *fallback = no_default ? null_or_undefined() : dts_arr(NULL);
				TpCode *probe = from_json_read(m, f, ID("e"));
				if (!probe) {
					Node *t = call(MEM(MEM(tp_co(fc, C.u->global_this), "Array"), "isArray"), jpo, NULL);
					Node *alt = dts_arr(NULL);
					if (pj) {
						TpCode *pc = tp_add(fc, tp_code(NULL));
						Node *t2 = call(MEM(MEM(tp_co(pc, C.u->global_this), "Array"), "isArray"), pjpo, NULL);
						alt = dts_cond(t2, dts_arr(dts_spread_arg(tp_clone(pjp)), NULL), alt);
					}
					dts_obj_add(ret, dts_kv(field_key, dts_cond(t, dts_arr(dts_spread_arg(tp_clone(jp)), NULL), alt)));
				} else {
					Node *g = tp_co(fc, C.u->global_this);
					TpCode *r1 = from_json_read(m, f, ID("e"));
					Node *map1 = mcall(tp_clone(jp), "map", arrow(L(dts_binding("e", KW("any"), false), NULL), NULL, read_node(fc, r1, ID("e"))), NULL);
					Node *alt = fallback;
					if (pj) {
						TpCode *pc = tp_add(fc, tp_code(NULL));
						Node *t2 = call(MEM(MEM(tp_co(pc, C.u->global_this), "Array"), "isArray"), pjpo, NULL);
						TpCode *r2 = from_json_read(m, f, ID("e"));
						Node *map2 = mcall(tp_clone(pjp), "map", arrow(L(dts_binding("e", KW("any"), false), NULL), NULL, read_node(pc, r2, ID("e"))), NULL);
						alt = dts_cond(t2, map2, alt);
					}
					Node *t = call(MEM(MEM(g, "Array"), "isArray"), jpo, NULL);
					dts_obj_add(ret, dts_kv(field_key, dts_cond(t, map1, alt)));
				}
			}
		} else if (is_within_one_of_that_should_be_union(f)) {
			int oi = f->oneof_index;
			if (oi < 0 || oi >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading '0')");
			int nf;
			const int *fi = oneof_fields(m, oi, &nf);
			int first = nf ? fi[0] : -1, last = nf ? fi[nf - 1] : -1;
			/* a proto3 optional field first or last in the oneof: upstream prints invalid code */
			if (k != first && !union_slot)
				fail("unsupported by protoc-gen-uni: a proto3 optional field in a oneof with other fields (%s)", f->name);
			if (k == first) {
				if (union_slot) /* oneofs interleaved: upstream prints invalid code */
					fail("unsupported by protoc-gen-uni: fields of different oneofs interleaved (%s)", f->name);
				union_cond = NULL;
				dts_obj_add(ret, kv(maybe_snake_to_camel(m->oneof_decl[oi]->name), NULL));
				union_slot = &ret->list.v[ret->list.n - 1]->b;
			}
			const char *vn = oneof_value_name(node_text(field_key));
			TpCode *tc = tp_add(fc, tp_code(NULL));
			TpCode *ic = tp_add(tc, tp_code(NULL));
			Node *test = call(tp_co(ic, C.u->is_set), tp_clone(jp), NULL);
			TpCode *thc = tp_add(tc, tp_code(NULL));
			Node *then = obj(kv("$case", sq(field_name)), kv(vn, read_node(thc, from_json_read(m, f, jp), jp)), NULL);
			Node *cond = dts_cond(test, then, NULL);
			*union_slot = cond;
			union_slot = &cond->c;
			if (pj) {
				TpCode *tc2 = tp_add(fc, tp_code(NULL));
				TpCode *ic2 = tp_add(tc2, tp_code(NULL));
				Node *test2 = call(tp_co(ic2, C.u->is_set), tp_clone(pjp), NULL);
				TpCode *thc2 = tp_add(tc2, tp_code(NULL));
				Node *then2 = obj(kv("$case", sq(field_name)), kv(vn, read_node(thc2, from_json_read(m, f, pjp), pjp)), NULL);
				Node *cond2 = dts_cond(test2, then2, NULL);
				*union_slot = cond2;
				union_slot = &cond2->c;
			}
			if (k == last) {
				*union_slot = null_or_undefined();
				union_slot = NULL;
			}
			(void)union_cond;
		} else if (is_any_value_type(f) || is_struct_type(f) || is_list_value_type(f)) {
			bool any = is_any_value_type(f), st = is_struct_type(f);
			Node *alt = null_or_undefined();
			TpCode *pc = NULL;
			Node *pjc = NULL;
			if (pj) {
				pc = tp_code(NULL);
				Node *t2 = any ? call(tp_co(pc, C.u->is_set), pjpo, NULL)
					   : st ? call(tp_co(pc, C.u->is_object), tp_clone(pjp), NULL)
						: call(MEM(MEM(tp_co(pc, C.u->global_this), "Array"), "isArray"), tp_clone(pjp), NULL);
				pjc = cond_nl(t2, read_node(pc, from_json_read(m, f, pjp), pjp), alt, H_OP2_NL);
			}
			Node *t = any ? call(tp_co(fc, C.u->is_set), jpo, NULL)
				  : st ? call(tp_co(fc, C.u->is_object), tp_clone(jp), NULL)
				       : call(MEM(MEM(tp_co(fc, C.u->global_this), "Array"), "isArray"), tp_clone(jp), NULL);
			Node *v = read_node(fc, from_json_read(m, f, jp), jp);
			if (pc)
				tp_add(fc, pc);
			dts_obj_add(ret, dts_kv(field_key, cond_nl(t, v, pjc ? pjc : alt, H_OP_NL | H_OP2_NL)));
		} else {
			Node *t = call(tp_co(fc, C.u->is_set), tp_clone(jp), NULL);
			Node *v = read_node(fc, from_json_read(m, f, jp), jp);
			Node *alt;
			TpCode *pc = NULL;
			Node *pjc = NULL;
			if (pj) {
				pc = tp_add(fc, tp_code(NULL));
				Node *t2 = call(tp_co(pc, C.u->is_set), tp_clone(pjp), NULL);
				pjc = cond_nl(t2, read_node(pc, from_json_read(m, f, pjp), pjp), NULL, H_OP2_NL);
			}
			if (is_within_one_of(f) || no_default)
				alt = null_or_undefined();
			else
				alt = tp_sub(fc, default_value(f).c);
			if (pjc) {
				pjc->c = alt;
				alt = pjc;
			}
			dts_obj_add(ret, dts_kv(field_key, cond_nl(t, v, alt, H_OP2_NL)));
		}
	}
	if (union_slot)
		fail("unsupported by protoc-gen-uni: a proto3 optional field in a oneof with other fields");
	/* each property chunk is on its own line (dprint keeps it in mind for the object) */
	for (int i = 0; i < ret->list.n; i++)
		ret->list.v[i]->nl = NL_LINE;
	if (ret->list.n)
		ret->hints |= H_TRAILING_COMMA;
	else
		ret->hints |= H_CLOSE_NL;
	dts_add(&body, RET(ret));
	c->node = method("fromJSON", NULL, L(param(pname, KW("any"), false, NULL), NULL), tref(full_name, NULL), blockl(body));
	return c;
}

/* ---- toJSON ---- */

static TpCode *to_json_read(TpdMessage *m, const TpdField *f, Node *from)
{
	if (is_enum(f))
		return imp_call(get_enum_method(f->type_name, "ToJSON"), tp_clone(from));
	if (is_object_id(f) && O.use_mongo_object_id)
		return tp_code(mcall(tp_clone(from), "toString", NULL));
	if (is_timestamp(f) && O.use_date == DATE_DATE)
		return tp_code(mcall(tp_clone(from), "toISOString", NULL));
	if (is_timestamp(f) && O.use_date == DATE_TEMPORAL)
		return tp_code(mcall(tp_clone(from), "toString", NULL));
	if (is_timestamp(f) && (O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO))
		return NULL;
	if (is_timestamp(f) && O.use_date == DATE_TIMESTAMP) {
		if (opt_is(&O.opts, "useJsonTimestamp", "raw"))
			return NULL;
		TpCode *w = tp_code(NULL);
		w->node = mcall(call(tp_co(w, C.u->from_timestamp), tp_clone(from), NULL), "toISOString", NULL);
		return w;
	}
	MapType mt;
	if (detect_map_type(m, f, &mt)) {
		TpdField *vt = mt.value_field;
		if (is_enum(vt))
			return imp_call(get_enum_method(vt->type_name, "ToJSON"), tp_clone(from));
		if (is_bytes(vt))
			return fn_of(C.u->base64_from_bytes, tp_clone(from));
		if (is_object_id(vt) && O.use_mongo_object_id)
			return tp_code(mcall(tp_clone(from), "toString", NULL));
		if (is_timestamp(vt) && O.use_date == DATE_DATE)
			return tp_code(mcall(tp_clone(from), "toISOString", NULL));
		if (is_timestamp(vt) && O.use_date == DATE_TEMPORAL)
			return tp_code(mcall(tp_clone(from), "toString", NULL));
		if (is_timestamp(vt) && (O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO))
			return NULL;
		if (is_timestamp(vt) && O.use_date == DATE_TIMESTAMP) {
			TpCode *w = tp_code(NULL);
			w->node = mcall(call(tp_co(w, C.u->from_timestamp), tp_clone(from), NULL), "toISOString", NULL);
			return w;
		}
		if (is_long(vt) && (O.force_long == LONG_LONG || O.force_long == LONG_BIGINT))
			return tp_code(mcall(tp_clone(from), "toString", NULL));
		if (is_whole_number(vt) && !(is_long(vt) && O.force_long == LONG_STRING))
			return tp_code(call(MEM(ID("Math"), "round"), tp_clone(from), NULL));
		if (is_floating_point(vt))
			return fn_of(C.u->float_to_json, tp_clone(from));
		if (is_long_value_type(vt) && (O.force_long == LONG_LONG || O.force_long == LONG_BIGINT))
			return tp_code(call(dts_opt_member(tp_clone(from), "toString", true), NULL));
		if (is_scalar(vt) || is_value_type(vt))
			return NULL;
		if (is_any_value_type(vt))
			return NULL;
		TpCode *c = tp_code(NULL);
		c->node = mcall(tp_expr(tp_sub(c, basic_type_name(vt, false))), "toJSON", tp_clone(from), NULL);
		return c;
	}
	if (is_any_value_type(f))
		return NULL;
	if (is_field_mask_type(f)) {
		TpCode *type = basic_type_name(f, true);
		TpCode *c = tp_code(NULL);
		Node *t1 = tp_expr(tp_sub(c, type));
		Node *t2 = tp_expr(tp_sub(c, type));
		c->node = mcall(t1, "toJSON", mcall(t2, "wrap", tp_clone(from), NULL), NULL);
		return c;
	}
	if (is_message(f) && !is_value_type(f) && !is_map_type(m, f)) {
		TpCode *c = tp_code(NULL);
		c->node = mcall(tp_expr(tp_sub(c, basic_type_name(f, true))), "toJSON", tp_clone(from), NULL);
		return c;
	}
	if (is_bytes(f))
		return fn_of(C.u->base64_from_bytes, tp_clone(from));
	if (is_long_value_type(f) && (O.force_long == LONG_LONG || O.force_long == LONG_BIGINT))
		return tp_code(mcall(tp_clone(from), "toString", NULL));
	if (is_long(f) && is_js_type_field_option(f)) {
		int ft = get_field_options_js_type(f);
		return gt_conv(cap_type(basic_type_name_t(f, ft ? ft : f->type, true)), from);
	}
	if (is_long(f) && O.force_long == LONG_LONG) {
		TpCode *c = tp_code(NULL);
		c->node = mcall(dts_paren(BIN(LOGICAL_OR, tp_clone(from), tp_sub(c, default_value(f).c))), "toString", NULL);
		return c;
	}
	if (is_long(f) && O.force_long == LONG_BIGINT)
		return tp_code(mcall(tp_clone(from), "toString", NULL));
	if (is_whole_number(f) && !(is_long(f) && O.force_long == LONG_STRING))
		return tp_code(call(MEM(ID("Math"), "round"), tp_clone(from), NULL));
	if (is_floating_point(f))
		return fn_of(C.u->float_to_json, tp_clone(from));
	return NULL;
}

/* setJsonProperty(value) */
static Node *set_json_property(TpCode *c, const char *json_name, Node *value)
{
	if (str_eq(json_name, "__proto__"))
		return ES(mcall(MEM(tp_co(c, C.u->global_this), "Object"), "defineProperty", ID("obj"), DQ("__proto__"),
				obj(kv("value", value), kv("enumerable", dts_bool(true)), NULL), NULL));
	return ES(SET(prop("obj", json_name), value));
}

static TpCode *generate_canonical_to_json(const char *full_name, const char *full_type_name)
{
	TpCode *c = tp_code(NULL);
	if (is_field_mask_type_name(full_type_name)) {
		Node *rt = O.uo_all ? dts_tunion(KW("string"), null_or_undefined_type(), NULL) : KW("string");
		Node *r = RET(call(dts_opt_member(MEM(ID("message"), "paths"), "join", O.uo_all), SQ(","), NULL));
		c->node = method("toJSON", NULL, L(param("message", tref(full_name, NULL), false, NULL), NULL), rt, block(r, NULL));
		return c;
	}
	if (str_eq(full_type_name, "google.protobuf.Duration") && opt_is(&O.opts, "useDuration", "string"))
		return duration_to_json(full_name);
	if (str_eq(full_type_name, "google.protobuf.Timestamp") && opt_is(&O.opts, "useJsonTimestamp", "rfc3339")) {
		Node *secs;
		if (O.force_long == LONG_LONG)
			secs = BIN(NULLISH, call(dts_opt_member(MEM(ID("message"), "seconds"), "toNumber", true), NULL), NUM("0"));
		else
			secs = call(MEM(tp_co(c, C.u->global_this), "Number"), BIN(NULLISH, MEM(ID("message"), "seconds"), NUM("0")), NULL);
		Node *r = RET(call(tp_co(c, C.u->timestamp_to_json), secs, BIN(NULLISH, MEM(ID("message"), "nanos"), NUM("0")), NULL));
		c->node = method("toJSON", NULL, L(param("message", tref(full_name, NULL), false, NULL), NULL), KW("string"), block(r, NULL));
		return c;
	}
	return NULL;
}

static TpCode *generate_to_json(const char *full_name, const char *full_type_name, TpdMessage *m)
{
	if (str_eq(full_type_name, "google.protobuf.Any") && use_canonical_any_json()) {
		TpCode *c = tp_code(NULL);
		c->node = tp_sub(c, canonical_any_to_json(full_name, m));
		return c;
	}
	TpCode *canonical = generate_canonical_to_json(full_name, full_type_name);
	if (canonical) {
		TpCode *c = tp_code(NULL);
		c->node = tp_sub(c, canonical);
		return c;
	}
	TpCode *c = tp_code(NULL);
	NodeList body = L(const_("obj", KW("any"), dts_obj()), NULL);
	tp_add(c, tp_code(NULL));
	const char *if_target = "";
	Node *last_if = NULL;
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *field_name = get_field_name(f);
		const char *json_name = get_field_json_name(f->name, f->json_name);
		Node *mp = prop("message", field_name);
		TpCode *fc = tp_add(c, tp_code(NULL));
		MapType mt;
		if (detect_map_type(m, f, &mt)) {
			TpCode *i = convert_to_object_key(m, f, "k");
			if (should_generate_js_map_type(m, f)) {
				Node *set = set_json_property(fc, json_name, dts_obj());
				Node *k = tp_sub(fc, i);
				TpCode *r = to_json_read(m, f, ID("v"));
				Node *assign = ES(SET(dts_index(prop("obj", json_name), k), read_node(fc, r, ID("v"))));
				Node *fe = ES(mcall(tp_clone(mp), "forEach", arrow(L(dts_binding("v", NULL, false), dts_binding("k", NULL, false), NULL), NULL,
									  block(assign, NULL)), NULL));
				dts_add(&body, dts_if(dts_opt_member(tp_clone(mp), "size", true), block(set, fe, NULL), NULL));
			} else {
				/* const entries = gt.Object.entries(mp) as [string, T][]; (no parentheses) */
				Node *e = entries_as(fc, tp_clone(mp), tp_sub(fc, mt.value_type))->a;
				Node *set = set_json_property(fc, json_name, dts_obj());
				Node *k = tp_sub(fc, i);
				TpCode *r = to_json_read(m, f, ID("v"));
				Node *assign = ES(SET(dts_index(prop("obj", json_name), k), read_node(fc, r, ID("v"))));
				Node *fe = ES(mcall(ID("entries"), "forEach", arrow(L(kv_pat("k", "v", NULL), NULL), NULL, block(assign, NULL)), NULL));
				Node *inner = dts_if(BIN(GT, MEM(ID("entries"), "length"), NUM("0")), block(set, fe, NULL), NULL);
				dts_add(&body, dts_if(tp_clone(mp), block(const_("entries", NULL, e), inner, NULL), NULL));
			}
			last_if = NULL;
			if_target = "";
		} else if (is_repeated(f)) {
			TpCode *r = to_json_read(m, f, ID("e"));
			TpCode *vc = tp_add(fc, tp_code(NULL));
			Node *v = tp_clone(mp);
			if (r) {
				TpCode *mm = tp_add(vc, tp_code(NULL));
				Node *a = HINT(arrow1("e", tp_sub(mm, r)), H_NO_PARENS);
				v = mcall(v, "map", a, NULL);
			}
			dts_add(&body, dts_if(dts_opt_member(tp_clone(mp), "length", true), block(set_json_property(vc, json_name, v), NULL), NULL));
			last_if = NULL;
			if_target = "";
		} else if (is_within_one_of_that_should_be_union(f)) {
			if (f->oneof_index < 0 || f->oneof_index >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading 'name')");
			const char *oname = maybe_snake_to_camel(m->oneof_decl[f->oneof_index]->name);
			Node *on = prop("message", oname);
			const char *target = node_text(on);
			const char *vn = oneof_value_name(field_name);
			Node *test = BIN(EQEQEQ, dts_opt_member(tp_clone(on), "$case", true), sq(field_name));
			TpCode *r = to_json_read(m, f, MEM(tp_clone(on), vn));
			Node *set = set_json_property(fc, json_name, read_node(fc, r, MEM(tp_clone(on), vn)));
			Node *ifs = dts_if(test, block(set, NULL), NULL);
			if (last_if && str_eq(if_target, target))
				last_if->c = ifs;
			else
				dts_add(&body, ifs);
			last_if = ifs;
			if_target = target;
		} else {
			Node *check;
			if ((is_scalar(f) || is_enum(f)) && !(is_within_one_of(f) || O.emit_default_values_json))
				check = tp_sub(fc, not_default_check(f, m->options, mp));
			else
				check = and_not_null(BIN(NOTEQEQ, tp_clone(mp), UNDEF()), tp_clone(mp));
			TpCode *r = to_json_read(m, f, mp);
			Node *set = set_json_property(fc, json_name, read_node(fc, r, mp));
			dts_add(&body, dts_if(check, block(set, NULL), NULL));
			last_if = NULL;
			if_target = "";
		}
	}
	dts_add(&body, RET(ID("obj")));
	c->node = method("toJSON", NULL, L(param(m->nfield > 0 ? "message" : "_", tref(full_name, NULL), false, NULL), NULL), KW("unknown"),
			 blockl(body));
	return c;
}

/* ---- fromPartial ---- */

static TpCode *from_partial_read(TpdMessage *m, const TpdField *f, Node *from)
{
	if ((is_long(f) || is_long_value_type(f)) && O.force_long == LONG_LONG && !is_js_type_field_option(f))
		return tp_code(mcall(ID("Long"), "fromValue", tp_clone(from), NULL));
	if ((is_long(f) || is_long_value_type(f)) && O.force_long == LONG_BIGINT && !is_js_type_field_option(f))
		return tp_code(call(ID("BigInt"), tp_clone(from), NULL));
	if (is_object_id(f) && O.use_mongo_object_id)
		return tp_code(dts_as(tp_clone(from), tref("mongodb.ObjectId", NULL)));
	if (is_primitive(f) ||
	    (is_timestamp(f) && (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL)) ||
	    is_value_type(f))
		return NULL;
	if (is_message(f)) {
		MapType mt;
		if (is_repeated(f) && detect_map_type(m, f, &mt)) {
			TpdField *vf = mt.value_field;
			if (is_primitive(vf)) {
				if (is_bytes(vf))
					return NULL;
				if (is_enum(vf)) {
					TpCode *c = tp_code(NULL);
					c->node = dts_as(tp_clone(from), tp_sub(c, mt.value_type));
					return c;
				}
				if (is_long(vf) && O.force_long == LONG_LONG)
					return tp_code(mcall(ID("Long"), "fromValue", tp_clone(from), NULL));
				if (is_long(vf) && O.force_long == LONG_BIGINT)
					return tp_code(call(ID("BigInt"), dts_as(tp_clone(from), dts_tunion(KW("string"), KW("number"), KW("bigint"), KW("boolean"), NULL)), NULL));
				return gt_conv(cap_type(mt.value_type), from);
			}
			if (is_any_value_type(vf))
				return NULL;
			if (is_object_id(vf) && O.use_mongo_object_id)
				return tp_code(dts_as(tp_clone(from), tref("mongodb.ObjectId", NULL)));
			if (is_timestamp(vf) && (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL))
				return NULL;
			if (is_value_type(vf))
				return NULL;
			TpCode *c = tp_code(NULL);
			c->node = mcall(tp_expr(tp_sub(c, basic_type_name(vf, false))), "fromPartial", tp_clone(from), NULL);
			return c;
		}
		if (is_any_value_type(f))
			return NULL;
		TpCode *c = tp_code(NULL);
		c->node = mcall(tp_expr(tp_sub(c, basic_type_name(f, false))), "fromPartial", tp_clone(from), NULL);
		return c;
	}
	fail("Unhandled field [object Object]");
}

/* create and fromPartial: two method properties in c->list */
static TpCode *generate_from_partial(const char *full_name, TpdMessage *m)
{
	TpCode *c = tp_code(NULL);
	TpCode *cc = tp_add(c, tp_code(NULL));
	Node *create;
	if (O.use_exact_types) {
		Node *tp = exact_tparams(cc, full_name);
		Node *r = RET(mcall(ID(full_name), "fromPartial", BIN(NULLISH, ID("base"), dts_paren(dts_as(dts_obj(), KW("any")))), NULL));
		create = method("create", tp, L(param("base", tref("I", NULL), true, NULL), NULL), tref(full_name, NULL), block(r, NULL));
	} else {
		Node *dp = tp_co_type(cc, C.u->deep_partial, L(tref(full_name, NULL), NULL));
		Node *r = RET(mcall(ID(full_name), "fromPartial", BIN(NULLISH, ID("base"), dts_obj()), NULL));
		create = method("create", NULL, L(param("base", dp, true, NULL), NULL), tref(full_name, NULL), block(r, NULL));
	}
	const char *pname = m->nfield > 0 ? "object" : "_";
	TpCode *hc = tp_add(c, tp_code(NULL));
	Node *tparams = NULL, *ptype;
	if (O.use_exact_types) {
		tparams = exact_tparams(hc, full_name);
		ptype = tref("I", NULL);
	} else {
		ptype = tp_co_type(hc, C.u->deep_partial, L(tref(full_name, NULL), NULL));
	}
	NodeList body = L(const_("message", NULL, create_base_call(full_name)), NULL);
	Node *sw = NULL;
	const char *sw_target = NULL;
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *field_name = get_field_name(f);
		Node *mp = prop("message", field_name);
		Node *op = prop("object", field_name);
		if (sw_target && !is_within_one_of_that_should_be_union(f)) {
			sw = NULL;
			sw_target = NULL;
		}
		bool no_default = !O.initialize_fields_as_undefined && is_optional_property(f, m->options, true);
		TpCode *fc = tp_add(c, tp_code(NULL));
		if (is_repeated(f)) {
			MapType mt;
			if (detect_map_type(m, f, &mt)) {
				TpCode *ft = to_type_name(m, f, false);
				TpCode *i = convert_from_object_key(m, f, "key");
				Node *rhs;
				if (should_generate_js_map_type(m, f)) {
					Node *t = tp_sub(fc, ft);
					TpCode *r = from_partial_read(m, f, ID("value"));
					Node *set = ES(mcall(ID("m"), "set", ID("key"), read_node(fc, r, ID("value")), NULL));
					Node *fe = ES(mcall(dts_paren(BIN(NULLISH, dts_as(tp_clone(op), t), newx(ID("Map"), NULL))), "forEach",
							    arrow(L(dts_binding("value", NULL, false), dts_binding("key", NULL, false), NULL), NULL,
								  block(dts_if(BIN(NOTEQEQ, ID("value"), UNDEF()), block(set, NULL), NULL), NULL)),
							    NULL));
					Node *fn = arrow((NodeList){0}, NULL, block(const_("m", NULL, newx(ID("Map"), NULL)), fe, RET(ID("m")), NULL));
					rhs = call(dts_paren(fn), NULL);
				} else {
					Node *e = entries_as(fc, BIN(NULLISH, tp_clone(op), dts_obj()), tp_sub(fc, mt.value_type));
					Node *ftn = tp_sub(fc, ft);
					Node *vt2 = tp_sub(fc, mt.value_type);
					Node *k2 = tp_sub(fc, i);
					TpCode *r = from_partial_read(m, f, ID("value"));
					Node *asg = ES(SET(dts_index(ID("acc"), k2), read_node(fc, r, ID("value"))));
					NodeList ps = L(dts_binding("acc", ftn, false), kv_pat("key", "value", dts_ttuple(KW("string"), vt2, NULL)), NULL);
					Node *fn = arrow(ps, NULL, block(dts_if(BIN(NOTEQEQ, ID("value"), UNDEF()), block(asg, NULL), NULL), RET(ID("acc")), NULL));
					rhs = mcall(e, "reduce", fn, dts_obj(), NULL);
					rhs->hints |= H_TRAILING_COMMA;
				}
				if (no_default)
					rhs = dts_cond(dts_paren(BIN(LOGICAL_OR, BIN(EQEQEQ, tp_clone(op), UNDEF()), BIN(EQEQEQ, tp_clone(op), dts_null()))), UNDEF(), rhs);
				dts_add(&body, ES(SET(tp_clone(mp), rhs)));
			} else {
				TpCode *r = from_partial_read(m, f, ID("e"));
				Node *a = arrow(L(dts_binding("e", NULL, false), NULL), NULL, read_node(fc, r, ID("e")));
				Node *rhs = BIN(LOGICAL_OR, call(dts_opt_member(tp_clone(op), "map", true), a, NULL), no_default ? UNDEF() : dts_arr(NULL));
				dts_add(&body, ES(SET(tp_clone(mp), rhs)));
			}
		} else if (is_within_one_of_that_should_be_union(f)) {
			if (f->oneof_index < 0 || f->oneof_index >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading 'name')");
			const char *oname = maybe_snake_to_camel(m->oneof_decl[f->oneof_index]->name);
			Node *on = prop("message", oname), *oo = prop("object", oname);
			const char *target = node_text(oo);
			const char *vn = oneof_value_name(field_name);
			TpCode *r = from_partial_read(m, f, MEM(tp_clone(oo), vn));
			if (sw_target && !str_eq(sw_target, target)) {
				sw = NULL;
				sw_target = NULL;
			}
			if (!sw_target) {
				sw = dts_switch(dts_opt_member(tp_clone(oo), "$case", true));
				dts_add(&body, sw);
			}
			Node *v = read_node(fc, r, MEM(tp_clone(oo), vn));
			Node *test = BIN(LOGICAL_AND, BIN(NOTEQEQ, dts_opt_member(tp_clone(oo), vn, true), UNDEF()),
					 BIN(NOTEQEQ, dts_opt_member(tp_clone(oo), vn, true), dts_null()));
			Node *asg = ES(SET(on, obj(kv("$case", sq(field_name)), kv(vn, v), NULL)));
			Node *cs = dts_case(sq(field_name));
			dts_add(&cs->list, block(dts_if(test, block(asg, NULL), NULL), dts_break(), NULL));
			dts_add(&sw->list, cs);
			sw_target = target;
		} else if (is_any_value_type(f)) {
			Node *fb = is_within_one_of(f) || no_default ? UNDEF() : tp_sub(fc, default_value(f).c);
			dts_add(&body, ES(SET(tp_clone(mp), dts_cond(BIN(EQEQEQ, tp_clone(op), UNDEF()), fb, tp_clone(op)))));
		} else {
			TpCode *r = from_partial_read(m, f, op);
			if (!r) {
				Node *fb = is_within_one_of(f) || no_default ? UNDEF() : tp_sub(fc, default_value(f).c);
				dts_add(&body, ES(SET(tp_clone(mp), BIN(NULLISH, tp_clone(op), fb))));
			} else {
				Node *test = dts_paren(BIN(LOGICAL_AND, BIN(NOTEQEQ, tp_clone(op), UNDEF()), BIN(NOTEQEQ, tp_clone(op), dts_null())));
				Node *v = tp_sub(fc, r);
				Node *fb = is_within_one_of(f) || no_default ? UNDEF() : tp_sub(fc, default_value(f).c);
				dts_add(&body, ES(SET(tp_clone(mp), cond_nl(test, v, fb, H_OP_NL | H_OP2_NL))));
			}
		}
	}
	dts_add(&body, RET(ID("message")));
	Node *fp = method("fromPartial", tparams, L(param(pname, ptype, false, NULL), NULL), tref(full_name, NULL), blockl(body));
	dts_add(&c->list, create);
	dts_add(&c->list, fp);
	return c;
}
