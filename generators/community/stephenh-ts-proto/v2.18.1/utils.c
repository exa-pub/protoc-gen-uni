/* Port of ts-proto v2.18.1 src/main.ts makeUtils: the runtime helpers that generated files declare
 * when they use them (ts-poet conditional outputs). Each helper has a builder that adds its
 * declarations to a code: once when the utils are made (the declaration-site code ts-poet visits)
 * and again for every file that uses it (fresh nodes).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

typedef void (*Builder)(TpCode *c);

typedef struct {
	TpCond *co;
	Builder build;
} Helper;

struct Utils {
	TpCond *global_this, *bytes_from_base64, *base64_from_bytes, *float_to_json;
	TpCond *builtin, *deep_partial, *exact;
	TpCond *to_timestamp, *from_timestamp, *timestamp_fraction_regex, *from_json_timestamp, *timestamp_json_regex,
		*timestamp_from_json, *timestamp_to_json;
	TpCond *number_to_long, *long_to_number;
	TpImport *long_;
	TpCond *is_object, *is_set;
	TpCond *fail;
	TpCond *message_fns, *extension_fns, *extension_holder, *struct_wrapper_fns, *any_value_wrapper_fns,
		*list_value_wrapper_fns, *field_mask_wrapper_fns;
	TpCond *any_json_value_types;
	TpCond *server_streaming_method_result, *grpc_web_error, *extension;
	TpCond *from_json_object_id, *from_proto_object_id, *to_proto_object_id;
	TpImport *mongodb;
	/* imports shared by the helpers (one object each, like upstream) */
	TpImport *timestamp, *nano_date, *binary_writer_t, *binary_reader_t;
	/* Object.values(utils) order */
	Helper helpers[40];
	int nhelpers;
};

static TpImport *utils_long(void) { return C.u->long_; }

static Node *gt(TpCode *c) { return tp_co(c, C.u->global_this); }
/* (globalThis as any) */
static Node *gt_any(TpCode *c) { return dts_paren(dts_as(gt(c), KW("any"))); }

static Node *ret_stmt(Node *e) { return dts_return(e); }

/* ---- makeByteUtils ---- */

static NodeList bytes_from_base64_browser(TpCode *c)
{
	/* const bin = gt.atob(b64); const arr = new Uint8Array(bin.length);
	 * for (let i = 0; i < bin.length; ++i) { arr[i] = bin.charCodeAt(i); } return arr; */
	Node *loop = dts_for(let_("i", NULL, NUM("0")), BIN(LT, ID("i"), MEM(ID("bin"), "length")),
			     dts_update(TK_PLUSPLUS, ID("i"), true),
			     block(ES(SET(dts_index(ID("arr"), ID("i")), mcall(ID("bin"), "charCodeAt", ID("i"), NULL))), NULL));
	return L(const_("bin", NULL, call(MEM(gt(c), "atob"), ID("b64"), NULL)),
		 const_("arr", NULL, newx(ID("Uint8Array"), MEM(ID("bin"), "length"), NULL)), loop, ret_stmt(ID("arr")), NULL);
}

static void build_bytes_from_base64(TpCode *c)
{
	NodeList body;
	if (O.env == ENV_NODE) {
		body = L(ret_stmt(mcall(ID("Uint8Array"), "from", mcall(MEM(gt(c), "Buffer"), "from", ID("b64"), SQ("base64"), NULL), NULL)),
			 NULL);
	} else if (O.env == ENV_BROWSER) {
		body = bytes_from_base64_browser(c);
	} else {
		Node *t = MEM(gt_any(c), "Buffer");
		NodeList cons = L(ret_stmt(mcall(ID("Uint8Array"), "from", mcall(MEM(gt_any(c), "Buffer"), "from", ID("b64"), SQ("base64"), NULL),
					   NULL)),
				  NULL);
		body = L(dts_if(t, blockl(cons), blockl(bytes_from_base64_browser(c))), NULL);
	}
	dts_add(&c->list, fn_decl("bytesFromBase64", NULL, L(param("b64", KW("string"), false, NULL), NULL), tref("Uint8Array", NULL),
				  blockl(body)));
}

static NodeList base64_from_bytes_browser(TpCode *c)
{
	/* const bin: string[] = []; arr.forEach((byte) => { bin.push(gt.String.fromCharCode(byte)); });
	 * return gt.btoa(bin.join('')); */
	Node *push = ES(mcall(ID("bin"), "push", mcall(MEM(gt(c), "String"), "fromCharCode", ID("byte"), NULL), NULL));
	return L(const_("bin", dts_tarray(KW("string")), dts_arr(NULL)),
		 ES(mcall(ID("arr"), "forEach", arrow1("byte", block(push, NULL)), NULL)),
		 ret_stmt(call(MEM(gt(c), "btoa"), mcall(ID("bin"), "join", SQ(""), NULL), NULL)), NULL);
}

static void build_base64_from_bytes(TpCode *c)
{
	NodeList body;
	if (O.env == ENV_NODE) {
		body = L(ret_stmt(mcall(mcall(MEM(gt(c), "Buffer"), "from", ID("arr"), NULL), "toString", SQ("base64"), NULL)), NULL);
	} else if (O.env == ENV_BROWSER) {
		body = base64_from_bytes_browser(c);
	} else {
		Node *t = MEM(gt_any(c), "Buffer");
		NodeList cons = L(ret_stmt(mcall(mcall(MEM(gt_any(c), "Buffer"), "from", ID("arr"), NULL), "toString", SQ("base64"), NULL)), NULL);
		body = L(dts_if(t, blockl(cons), blockl(base64_from_bytes_browser(c))), NULL);
	}
	dts_add(&c->list, fn_decl("base64FromBytes", NULL, L(param("arr", tref("Uint8Array", NULL), false, NULL), NULL), KW("string"),
				  blockl(body)));
}

static void build_float_to_json(TpCode *c)
{
	Node *test = BIN(LOGICAL_AND, BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, ID("value")), DQ("number")),
			 dts_unary(UNARY_BANG, call(MEM(MEM(gt(c), "Number"), "isFinite"), ID("value"), NULL)));
	Node *e = dts_cond(test, call(MEM(gt(c), "String"), ID("value"), NULL), ID("value"));
	Node *pt = dts_tunion(KW("number"), KW("null"), KW("undefined"), NULL);
	Node *rt = dts_tunion(KW("number"), KW("string"), KW("null"), KW("undefined"), NULL);
	dts_add(&c->list, fn_decl("floatToJson", NULL, L(param("value", pt, false, NULL), NULL), rt, block(ret_stmt(e), NULL)));
}

static void build_gt_polyfill(TpCode *c)
{
	(void)c;
	static const char *const names[] = {"self", "window", "global"};
	for (int i = 0; i < 3; i++) {
		Node *d = dts_var(VAR_CONST, dts_binding(names[i], dts_tunion(KW("any"), KW("undefined"), NULL), false), NULL);
		d->flags |= F_DECLARE;
		dts_add(&c->list, d);
	}
	static const char *const globals[] = {"globalThis", "self", "window", "global"};
	Node *b = dts_block();
	for (int i = 0; i < 4; i++)
		dts_block_add(b, dts_if(BIN(NOTEQEQ, dts_unary(UNARY_TYPEOF, ID(globals[i])), DQ("undefined")), RET(ID(globals[i])), NULL));
	dts_block_add(b, dts_throw(DQ("Unable to locate global object")));
	dts_add(&c->list, const_("gt", KW("any"), call(dts_paren(arrow((NodeList){0}, NULL, b)), NULL)));
}

/* ---- makeDeepPartial ---- */

UNUSED static const char *maybe_export(void) { return O.export_common_symbols ? "export" : ""; }

static Node *maybe_exported(Node *decl) { return O.export_common_symbols ? dts_export(decl) : decl; }

static void build_builtin(TpCode *c)
{
	Node *u = dts_tunion(tref("Date", NULL), tref("Function", NULL), tref("Uint8Array", NULL), KW("string"), KW("number"),
			     KW("boolean"), NULL);
	if (O.force_long == LONG_BIGINT)
		dts_add(&u->list, KW("bigint"));
	if (O.use_date == DATE_TEMPORAL)
		dts_add(&u->list, tref("Temporal.Instant", NULL));
	dts_add(&u->list, KW("undefined"));
	dts_add(&c->list, dts_type_alias(ID("Builtin"), NULL, u));
}

static Node *tparams1(const char *a) { return dts_tparams(dts_tparam(a, NULL, NULL), NULL); }

/* T['k'] */
static Node *tindex_lit(const char *t, const char *k) { return dts_tindexed(tref(t, NULL), dts_tlit(sq(k))); }

static Node *prop_sig(const char *key, Node *type, bool optional, bool readonly)
{
	return dts_prop_sig(dts_id(key), type, optional, readonly);
}

static void build_deep_partial(TpCode *c)
{
	/* the false branches, innermost first */
	Node *keys = O.add_type_to_messages ? tref("Exclude", dts_tkeyof(tref("T", NULL)), dts_tlit(SQ("$type")), NULL)
					    : dts_tkeyof(tref("T", NULL));
	Node *mapped = dts_tmapped("K", keys, NULL, tref("DeepPartial", dts_tindexed(tref("T", NULL), tref("K", NULL)), NULL),
				   MAPPED_NONE, MAPPED_TRUE);
	Node *rest = dts_tcond(tref("T", NULL), dts_ttype_lit(), mapped, tref("Partial", tref("T", NULL), NULL));
	bool ro = O.use_readonly_types;
	if (O.oneof == ONEOF_UNIONS) {
		Node *ext = dts_ttype_lit();
		dts_ttype_lit_add(ext, HINT(prop_sig("$case", KW("string"), false, ro), H_NO_SEMI));
		Node *m1 = dts_tmapped("K", dts_tkeyof(tref("Omit", tref("T", NULL), dts_tlit(SQ("$case")), NULL)), NULL,
				       tref("DeepPartial", dts_tindexed(tref("T", NULL), tref("K", NULL)), NULL), MAPPED_NONE, MAPPED_TRUE);
		Node *m2 = dts_ttype_lit();
		dts_ttype_lit_add(m2, HINT(prop_sig("$case", tindex_lit("T", "$case"), false, ro), H_NO_SEMI));
		rest = dts_tcond(tref("T", NULL), ext, dts_tintersection(m1, m2, NULL), rest);
	} else if (O.oneof == ONEOF_UNIONS_VALUE) {
		Node *ext = dts_ttype_lit();
		dts_ttype_lit_add(ext, prop_sig("$case", KW("string"), false, ro));
		dts_ttype_lit_add(ext, prop_sig("value", KW("unknown"), false, false));
		Node *t = dts_ttype_lit();
		dts_ttype_lit_add(t, prop_sig("$case", tindex_lit("T", "$case"), false, ro));
		dts_ttype_lit_add(t, prop_sig("value", tref("DeepPartial", tindex_lit("T", "value"), NULL), true, false));
		rest = dts_tcond(tref("T", NULL), ext, t, rest);
	}
	rest = dts_tcond(tref("T", NULL), tref("ReadonlyArray", dts_tinfer("U"), NULL),
			 tref("ReadonlyArray", tref("DeepPartial", tref("U", NULL), NULL), NULL), rest);
	rest = dts_tcond(tref("T", NULL), tref("globalThis.Array", dts_tinfer("U"), NULL),
			 tref("globalThis.Array", tref("DeepPartial", tref("U", NULL), NULL), NULL), rest);
	if (O.force_long == LONG_LONG) {
		TpCode *l = tp_add(c, tp_code(NULL));
		Node *lt = tp_tref(l, C.u->long_, (NodeList){0});
		rest = dts_tcond(tref("T", NULL), lt, dts_tunion(KW("string"), KW("number"), tref("Long", NULL), NULL), rest);
	}
	Node *t = dts_tcond(tref("T", NULL), tp_co_type(c, C.u->builtin, (NodeList){0}), tref("T", NULL), rest);
	if (O.force_long == LONG_BIGINT)
		t = dts_tcond(tref("T", NULL), KW("bigint"), dts_tunion(KW("string"), KW("number"), KW("bigint"), NULL), t);
	if (O.use_unknown_for_value)
		t = dts_tcond(KW("unknown"), tref("T", NULL), tref("T", NULL), t);
	dts_add(&c->list, maybe_exported(dts_type_alias(ID("DeepPartial"), tparams1("T"), t)));
}

static void build_exact(TpCode *c)
{
	dts_add(&c->list, dts_type_alias(ID("KeysOfUnion"), tparams1("T"),
					 dts_tcond(tref("T", NULL), tref("T", NULL), dts_tkeyof(tref("T", NULL)), KW("never"))));
	Node *m1 = dts_tmapped("K", dts_tkeyof(tref("P", NULL)), tref("Extract", tref("K", NULL), dts_tkeyof(tref("I", NULL)), NULL),
			       tref("Exact", dts_tindexed(tref("P", NULL), tref("K", NULL)),
				    dts_tindexed(tref("I", NULL), dts_tintersection(tref("K", NULL), dts_tkeyof(tref("I", NULL)), NULL)), NULL),
			       MAPPED_NONE, MAPPED_NONE);
	Node *ku = tref("KeysOfUnion", tref("P", NULL), NULL);
	if (O.add_type_to_messages)
		ku = dts_tunion(ku, dts_tlit(SQ("$type")), NULL);
	Node *m2 = dts_tmapped("K", tref("Exclude", dts_tkeyof(tref("I", NULL)), ku, NULL), NULL, KW("never"), MAPPED_NONE, MAPPED_NONE);
	Node *t = dts_tcond(tref("P", NULL), tp_co_type(c, C.u->builtin, (NodeList){0}), tref("P", NULL),
			    dts_tintersection(tref("P", NULL), m1, m2, NULL));
	if (O.use_unknown_for_value)
		t = dts_tcond(KW("unknown"), tref("P", NULL), tref("P", NULL), t);
	dts_add(&c->list, maybe_exported(dts_type_alias(ID("Exact"), dts_tparams(dts_tparam("P", NULL, NULL), dts_tparam("I", NULL, NULL), NULL), t)));
}

/* ---- makeTimestampMethods ---- */

static Node *ts_ref(TpCode *c) { return tp_tref(c, C.u->timestamp, (NodeList){0}); }

/* `seconds` of toTimestamp */
static Node *ts_seconds(TpCode *c)
{
	Node *s = call(MEM(ID("Math"), "trunc"), BIN(DIV, mcall(ID("date"), "getTime", NULL), NUM("1_000")), NULL);
	if (O.force_long == LONG_LONG)
		return call(tp_co(c, C.u->number_to_long), s, NULL);
	if (O.force_long == LONG_BIGINT)
		return call(ID("BigInt"), s, NULL);
	if (O.force_long == LONG_STRING)
		return mcall(s, "toString", NULL);
	return s;
}

/* toNumberCode of fromTimestamp */
static Node *ts_to_number(TpCode *c)
{
	bool q = O.uo_all || O.no_defaults_for_optionals;
	if (O.force_long == LONG_LONG)
		return call(dts_opt_member(MEM(ID("t"), "seconds"), "toNumber", q), NULL);
	if (O.force_long == LONG_BIGINT)
		return call(MEM(gt(c), "Number"), call(dts_opt_member(MEM(ID("t"), "seconds"), "toString", q), NULL), NULL);
	if (O.force_long == LONG_STRING)
		return call(MEM(gt(c), "Number"), MEM(ID("t"), "seconds"), NULL);
	return MEM(ID("t"), "seconds");
}

static Node *ts_return(void)
{
	Node *o = dts_obj();
	if (O.add_type_to_messages)
		dts_obj_add(o, kv("$type", SQ("google.protobuf.Timestamp")));
	dts_obj_add(o, shorthand("seconds"));
	dts_obj_add(o, shorthand("nanos"));
	return ret_stmt(o);
}

static Node *ts_nanos(void)
{
	/* (date.getTime() % 1_000) * 1_000_000 */
	return BIN(MUL, dts_paren(BIN(MOD, mcall(ID("date"), "getTime", NULL), NUM("1_000"))), NUM("1_000_000"));
}

static void build_to_timestamp(TpCode *c)
{
	if (O.use_date == DATE_STRING) {
		NodeList body = L(const_("date", NULL, newx(MEM(gt(c), "Date"), ID("dateStr"), NULL)), const_("seconds", NULL, ts_seconds(c)),
				  const_("nanos", NULL, ts_nanos()), ts_return(), NULL);
		dts_add(&c->list, fn_decl("toTimestamp", NULL, L(param("dateStr", KW("string"), false, NULL), NULL), ts_ref(c), blockl(body)));
	} else if (O.use_date == DATE_STRING_NANO) {
		Node *rt = ts_ref(c);
		Node *nd = newx(tp_ref(c, C.u->nano_date), ID("dateStr"), NULL);
		Node *date = obj(NL(kv("getTime", arrow((NodeList){0}, KW("number"), mcall(ID("nanoDate"), "valueOf", NULL))), LINE), NULL);
		date->hints |= H_TRAILING_COMMA;
		NodeList body = L(const_("nanoDate", NULL, nd), BLANK(const_("date", NULL, as_const(date))), const_("seconds", NULL, ts_seconds(c)),
				  BLANK(let_("nanos", NULL, BIN(MUL, mcall(ID("nanoDate"), "getMilliseconds", NULL), NUM("1_000_000")))),
				  ES(dts_assign(TK_ADD_ASSIGN, ID("nanos"), BIN(MUL, mcall(ID("nanoDate"), "getMicroseconds", NULL), NUM("1_000")))),
				  ES(dts_assign(TK_ADD_ASSIGN, ID("nanos"), mcall(ID("nanoDate"), "getNanoseconds", NULL))), BLANK(ts_return()), NULL);
		dts_add(&c->list, fn_decl("toTimestamp", NULL, L(param("dateStr", KW("string"), false, NULL), NULL), rt, blockl(body)));
	} else if (O.use_date == DATE_TEMPORAL) {
		Node *rt = ts_ref(c);
		Node *date = obj(NL(kv("getTime", arrow((NodeList){0}, KW("number"), MEM(ID("instant"), "epochMilliseconds"))), LINE), NULL);
		date->hints |= H_TRAILING_COMMA;
		Node *round = mcall(ID("instant"), "round", obj(kv("smallestUnit", DQ("seconds")), kv("roundingMode", DQ("floor")), NULL), NULL);
		Node *rem = MEM(ID("remainder"), "nanoseconds");
		Node *nanos = BIN(ADD, BIN(ADD, dts_paren(BIN(MUL, MEM(ID("remainder"), "milliseconds"), NUM("1_000_000"))),
					   dts_paren(BIN(MUL, MEM(ID("remainder"), "microseconds"), NUM("1_000")))),
				  rem);
		NodeList body = L(const_("date", NULL, as_const(date)), const_("seconds", NULL, ts_seconds(c)),
				  const_("remainder", NULL, mcall(round, "until", ID("instant"), NULL)), const_("nanos", NULL, nanos), BLANK(ts_return()), NULL);
		dts_add(&c->list, fn_decl("toTimestamp", NULL, L(param("instant", tref("Temporal.Instant", NULL), false, NULL), NULL), rt, blockl(body)));
	} else {
		NodeList body = L(const_("seconds", NULL, ts_seconds(c)), const_("nanos", NULL, ts_nanos()), ts_return(), NULL);
		dts_add(&c->list, fn_decl("toTimestamp", NULL, L(param("date", tref("Date", NULL), false, NULL), NULL), ts_ref(c), blockl(body)));
	}
}

static void build_from_timestamp(TpCode *c)
{
	if (O.use_date == DATE_STRING_NANO) {
		Node *p = param("t", ts_ref(c), false, NULL);
		Node *tn = dts_paren(BIN(LOGICAL_OR, MEM(ID("t"), "nanos"), NUM("0")));
		Node *trunc = MEM(ID("Math"), "trunc");
		Node *micros = call(trunc, BIN(DIV, dts_paren(BIN(MOD, tn, NUM("1_000_000"))), NUM("1_000")), NULL);
		NodeList body = L(const_("seconds", NULL, BIN(LOGICAL_OR, ts_to_number(c), NUM("0"))),
				  const_("nanos", NULL, BIN(MOD, dts_paren(BIN(LOGICAL_OR, MEM(ID("t"), "nanos"), NUM("0"))), NUM("1_000"))),
				  HINT(const_("micros", NULL, micros), H_NO_SEMI), let_("millis", NULL, BIN(MUL, ID("seconds"), NUM("1_000"))),
				  ES(dts_assign(TK_ADD_ASSIGN, ID("millis"),
						call(MEM(ID("Math"), "trunc"), BIN(DIV, dts_paren(BIN(LOGICAL_OR, MEM(ID("t"), "nanos"), NUM("0"))), NUM("1_000_000")), NULL))),
				  BLANK(const_("nanoDate", NULL, newx(tp_ref(c, C.u->nano_date), ID("millis"), NULL))),
				  ES(mcall(ID("nanoDate"), "setMicroseconds", ID("micros"), NULL)), ES(mcall(ID("nanoDate"), "setNanoseconds", ID("nanos"), NULL)),
				  BLANK(ret_stmt(mcall(ID("nanoDate"), "toISOStringFull", NULL))), NULL);
		dts_add(&c->list, fn_decl("fromTimestamp", NULL, L(p, NULL), KW("string"), blockl(body)));
		return;
	}
	if (O.use_date == DATE_TEMPORAL) {
		Node *p = param("t", ts_ref(c), false, NULL);
		Node *s1 = const_("seconds", NULL, BIN(LOGICAL_OR, ts_to_number(c), NUM("0")));
		Node *base = MEM(MEM(gt(c), "Temporal"), "Instant");
		Node *from = HINT(MEM(base, "fromEpochMilliseconds"), H_OP_NL);
		Node *dur = mcall(MEM(MEM(gt(c), "Temporal"), "Duration"), "from", obj(kv("nanoseconds", MEM(ID("t"), "nanos")), NULL), NULL);
		Node *add = HINT(MEM(call(from, BIN(MUL, ID("seconds"), NUM("1_000")), NULL), "add"), H_OP_NL);
		dts_add(&c->list, fn_decl("fromTimestamp", NULL, L(p, NULL), tref("Temporal.Instant", NULL), block(s1, ret_stmt(call(add, dur, NULL)), NULL)));
		return;
	}
	/* let millis = (toNumber || 0) * 1_000; millis += (t.nanos || 0) / 1_000_000; */
	Node *params = param("t", ts_ref(c), false, NULL);
	Node *m1 = let_("millis", NULL, BIN(MUL, dts_paren(BIN(LOGICAL_OR, ts_to_number(c), NUM("0"))), NUM("1_000")));
	Node *m2 = ES(dts_assign(TK_ADD_ASSIGN, ID("millis"), BIN(DIV, dts_paren(BIN(LOGICAL_OR, MEM(ID("t"), "nanos"), NUM("0"))), NUM("1_000_000"))));
	Node *d = newx(MEM(gt(c), "Date"), ID("millis"), NULL);
	bool str = O.use_date == DATE_STRING;
	Node *r = ret_stmt(str ? mcall(d, "toISOString", NULL) : d);
	dts_add(&c->list, fn_decl("fromTimestamp", NULL, L(params, NULL), str ? KW("string") : tref("Date", NULL), block(m1, m2, r, NULL)));
}

static void build_timestamp_fraction_regex(TpCode *c)
{
	dts_add(&c->list, const_("timestampFractionRegex", NULL, dts_leafc(N_REGEX, "/:\\d{2}\\.\\d+/")));
}

static NodeComment line_comment(const char *text, int nl)
{
	NodeComment cm = {str_c(text), false, (uint8_t)nl};
	return cm;
}

static void build_from_json_timestamp(TpCode *c)
{
	Node *o = ID("o");
	Node *is_date = dts_instanceof(o, MEM(gt(c), "Date"));
	Node *is_str = BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, ID("o")), DQ("string"));
	Node *body;
	if (O.use_date == DATE_DATE) {
		Node *e3 = ret_stmt(call(tp_co(c, C.u->from_timestamp), call(MEM(ID(wrap_type_name("Timestamp", false)), "fromJSON"), ID("o"), NULL), NULL));
		body = block(dts_if(is_date, block(ret_stmt(ID("o")), NULL),
				    dts_if(is_str, block(ret_stmt(newx(MEM(gt(c), "Date"), ID("o"), NULL)), NULL), block(e3, NULL))),
			     NULL);
		dts_add(&c->list, fn_decl("fromJsonTimestamp", NULL, L(param("o", KW("any"), false, NULL), NULL), tref("Date", NULL), body));
		return;
	}
	if (O.use_date == DATE_TEMPORAL) {
		Node *e1 = ret_stmt(mcall(MEM(MEM(gt(c), "Temporal"), "Instant"), "fromEpochMilliseconds", mcall(ID("o"), "getTime", NULL), NULL));
		Node *e2 = ret_stmt(mcall(MEM(MEM(gt(c), "Temporal"), "Instant"), "from", ID("o"), NULL));
		Node *e3 = ret_stmt(call(tp_co(c, C.u->from_timestamp), call(MEM(ID(wrap_type_name("Timestamp", false)), "fromJSON"), ID("o"), NULL), NULL));
		body = block(dts_if(is_date, block(e1, NULL), dts_if(is_str, block(e2, NULL), block(e3, NULL))), NULL);
		dts_add(&c->list, fn_decl("fromJsonTimestamp", NULL, L(param("o", KW("any"), false, NULL), NULL), tref("Temporal.Instant", NULL), body));
		return;
	}
	/* const fraction = timestampFractionRegex.exec(o); ... */
	Node *s1 = const_("fraction", NULL, mcall(tp_co(c, C.u->timestamp_fraction_regex), "exec", ID("o"), NULL));
	dts_comment(s1, STR_LIT("// Date only keeps milliseconds, and parses longer fractions in an engine-specific way,"), false, 0);
	dts_comment(s1, STR_LIT("// so parse the fractional seconds ourselves and give Date only the whole seconds"), false, NL_LINE);
	Node *s2 = dts_if(BIN(EQEQEQ, ID("fraction"), dts_null()),
			  block(ret_stmt(call(tp_co(c, C.u->to_timestamp), newx(MEM(gt(c), "Date"), ID("o"), NULL), NULL)), NULL), NULL);
	Node *f0 = dts_index(ID("fraction"), NUM("0"));
	Node *s3 = const_("fractionNanos", NULL,
			  call(ID("Number"), mcall(mcall(f0, "slice", NUM("4"), NUM("13"), NULL), "padEnd", NUM("9"), DQ("0"), NULL), NULL));
	Node *s4 = const_("wholeSeconds", NULL,
			  newx(MEM(gt(c), "Date"), mcall(ID("o"), "replace", dts_index(ID("fraction"), NUM("0")),
							 mcall(dts_index(ID("fraction"), NUM("0")), "slice", NUM("0"), NUM("3"), NULL), NULL),
			       NULL));
	Node *arg = newx(MEM(gt(c), "Date"),
			 BIN(ADD, mcall(ID("wholeSeconds"), "getTime", NULL),
			     call(MEM(ID("Math"), "trunc"), BIN(DIV, ID("fractionNanos"), NUM("1_000_000")), NULL)),
			 NULL);
	Node *tcall = call(tp_co(c, C.u->to_timestamp), arg, NULL);
	tcall->hints |= H_TRAILING_COMMA;
	Node *s5 = const_("timestamp", NULL, tcall);
	Node *ro = obj(dts_spread(ID("timestamp")),
		       kv("nanos", BIN(ADD, dts_paren(BIN(NULLISH, MEM(ID("timestamp"), "nanos"), NUM("0"))),
				      dts_paren(BIN(MOD, ID("fractionNanos"), NUM("1_000_000"))))),
		       NULL);
	Node *s6 = ret_stmt(ro);
	Node *e3 = ret_stmt(call(MEM(ID(xcat(O.type_prefix, "Timestamp", O.type_suffix)), "fromJSON"), ID("o"), NULL));
	body = block(dts_if(is_date, block(ret_stmt(call(tp_co(c, C.u->to_timestamp), ID("o"), NULL)), NULL),
			    dts_if(is_str, block(s1, s2, s3, s4, s5, s6, NULL), block(e3, NULL))),
		     NULL);
	dts_add(&c->list, fn_decl("fromJsonTimestamp", NULL, L(param("o", KW("any"), false, NULL), NULL),
				  tref(wrap_type_name("Timestamp", false), NULL), body));
}

static void build_timestamp_json_regex(TpCode *c)
{
	dts_add(&c->list, const_("timestampJsonRegex", NULL, dts_leafc(N_REGEX, "/^.+T\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?(?:Z|[+-]\\d{2}:\\d{2})$/")));
}

static Node *tpl2(const char *q0, Node *e0, const char *q1)
{
	Str q[2] = {str_c(q0), str_c(q1)};
	return dts_tpl(1, q, &e0);
}

static void build_timestamp_from_json(TpCode *c)
{
	Node *s1 = const_("match", NULL, mcall(tp_co(c, C.u->timestamp_json_regex), "exec", ID("value"), NULL));
	Node *s2 = const_("fraction", NULL, dts_opt_index(ID("match"), NUM("1"), true));
	dts_comment(s2, STR_LIT("// Date only keeps milliseconds, so it parses the string without the fractional seconds"), false, 0);
	Node *d = mcall(newx(MEM(gt(c), "Date"), dts_cond(BIN(EQEQEQ, ID("fraction"), UNDEF()), ID("value"),
							  mcall(ID("value"), "replace", ID("fraction"), DQ(""), NULL)),
			     NULL),
			"getTime", NULL);
	Node *s3 = const_("millis", NULL, HINT(dts_cond(ID("match"), d, ID("NaN")), H_OP_NL | H_OP2_NL));
	Node *err = newx(MEM(gt(c), "Error"), tpl2("Invalid timestamp: ", ID("value"), ""), NULL);
	Node *s4 = dts_if(BIN(LOGICAL_OR, dts_unary(UNARY_BANG, ID("match")), call(MEM(MEM(gt(c), "Number"), "isNaN"), ID("millis"), NULL)),
			  block(dts_throw(err), NULL), NULL);
	Node *nanos = call(MEM(gt(c), "Number"), mcall(mcall(ID("fraction"), "slice", NUM("1"), NUM("10"), NULL), "padEnd", NUM("9"), DQ("0"), NULL),
			   NULL);
	Node *s5 = const_("nanos", NULL, dts_cond(BIN(EQEQEQ, ID("fraction"), UNDEF()), NUM("0"), nanos));
	Node *s6 = ret_stmt(obj(kv("seconds", BIN(DIV, ID("millis"), NUM("1_000"))), shorthand("nanos"), NULL));
	Node *rt = dts_ttype_lit();
	dts_ttype_lit_add(rt, prop_sig("seconds", KW("number"), false, false));
	dts_ttype_lit_add(rt, HINT(prop_sig("nanos", KW("number"), false, false), H_NO_SEMI));
	dts_add(&c->list, fn_decl("timestampFromJson", NULL, L(param("value", KW("string"), false, NULL), NULL), rt,
				  block(s1, s2, s3, s4, s5, s6, NULL)));
}

static void build_timestamp_to_json(TpCode *c)
{
	Node *s1 = const_("date", NULL,
			  mcall(mcall(newx(MEM(gt(c), "Date"), BIN(MUL, ID("seconds"), NUM("1_000")), NULL), "toISOString", NULL), "slice", NUM("0"),
				NUM("19"), NULL));
	Node *s2 = dts_if(BIN(EQEQEQ, ID("nanos"), NUM("0")), ret_stmt(tpl2("", ID("date"), "Z")), NULL);
	Node *s3 = const_("padded", NULL, mcall(mcall(ID("nanos"), "toString", NULL), "padStart", NUM("9"), DQ("0"), NULL));
	Node *s4 = const_("len", NULL,
			  dts_cond(BIN(EQEQEQ, BIN(MOD, ID("nanos"), NUM("1_000_000")), NUM("0")), NUM("3"),
				   dts_cond(BIN(EQEQEQ, BIN(MOD, ID("nanos"), NUM("1_000")), NUM("0")), NUM("6"), NUM("9"))));
	Str q[3] = {STR_LIT(""), STR_LIT("."), STR_LIT("Z")};
	Node *ex[2] = {ID("date"), mcall(ID("padded"), "slice", NUM("0"), ID("len"), NULL)};
	Node *s5 = ret_stmt(dts_tpl(2, q, ex));
	dts_add(&c->list, fn_decl("timestampToJson", NULL,
				  L(param("seconds", KW("number"), false, NULL), param("nanos", KW("number"), false, NULL), NULL), KW("string"),
				  block(s1, s2, s3, s4, s5, NULL)));
}

/* ---- makeLongUtils ---- */

static void build_number_to_long(TpCode *c)
{
	dts_add(&c->list, fn_decl("numberToLong", NULL, L(param("number", KW("number"), false, NULL), NULL), NULL,
				  block(ret_stmt(call(MEM(tp_ref(c, C.u->long_), "fromNumber"), ID("number"), NULL)), NULL)));
}

static void build_long_to_number(TpCode *c)
{
	Node *pt = dts_ttype_lit();
	dts_ttype_lit_add(pt, HINT(dts_method_sig(dts_id("toString"), NULL, (NodeList){0}, KW("string"), false), H_NO_SEMI));
	Node *s1 = const_("num", NULL, call(MEM(gt(c), "Number"), mcall(ID("int64"), "toString", NULL), NULL));
	Node *t1 = HINT(dts_throw(newx(MEM(gt(c), "Error"), DQ("Value is larger than Number.MAX_SAFE_INTEGER"), NULL)), H_NO_SEMI);
	Node *s2 = dts_if(BIN(GT, ID("num"), MEM(MEM(gt(c), "Number"), "MAX_SAFE_INTEGER")), block(t1, NULL), NULL);
	Node *t2 = HINT(dts_throw(newx(MEM(gt(c), "Error"), DQ("Value is smaller than Number.MIN_SAFE_INTEGER"), NULL)), H_NO_SEMI);
	Node *s3 = dts_if(BIN(LT, ID("num"), MEM(MEM(gt(c), "Number"), "MIN_SAFE_INTEGER")), block(t2, NULL), NULL);
	dts_add(&c->list, fn_decl("longToNumber", NULL, L(param("int64", pt, false, NULL), NULL), KW("number"),
				  block(s1, s2, s3, ret_stmt(ID("num")), NULL)));
}

/* ---- makeComparisonUtils, makeAssertionUtils ---- */

static void build_is_object(TpCode *c)
{
	Node *e = BIN(LOGICAL_AND, BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, ID("value")), SQ("object")), BIN(NOTEQEQ, ID("value"), dts_null()));
	dts_add(&c->list, fn_decl("isObject", NULL, L(param("value", KW("any"), false, NULL), NULL), KW("boolean"), block(ret_stmt(e), NULL)));
}

static void build_is_set(TpCode *c)
{
	Node *e = BIN(LOGICAL_AND, BIN(NOTEQEQ, ID("value"), dts_null()), BIN(NOTEQEQ, ID("value"), UNDEF()));
	dts_add(&c->list, fn_decl("isSet", NULL, L(param("value", KW("any"), false, NULL), NULL), KW("boolean"), block(ret_stmt(e), NULL)));
}

static void build_fail(TpCode *c)
{
	Node *t = dts_throw(newx(MEM(gt(c), "Error"), BIN(NULLISH, ID("message"), DQ("Failed")), NULL));
	dts_add(&c->list, fn_decl("fail", NULL, L(param("message", KW("string"), true, NULL), NULL), KW("never"), block(t, NULL)));
}

/* ---- makeMessageFns ---- */

static Node *msig(const char *name, Node *tparams, NodeList params, Node *ret)
{
	return dts_method_sig(dts_id(name), tparams, params, ret, false);
}

/* <I extends Exact<DeepPartial<T>, I>> */
static Node *exact_tparams(TpCode *c, const char *t)
{
	Node *dp = tp_co_type(c, C.u->deep_partial, L(tref(t, NULL), NULL));
	Node *ex = tp_co_type(c, C.u->exact, L(dp, tref("I", NULL), NULL));
	return dts_tparams(dts_tparam("I", ex, NULL), NULL);
}

static bool has_type_member(void) { return O.output_type_annotations || O.output_type_registry; }

static int message_fns_members(TpCode *c, NodeList *members)
{
	JsVal *oe = opt(&O.opts, "outputEncodeMethods");
	if (has_type_member())
		dts_add(members, prop_sig("$type", tref("V", NULL), false, true));
	if (js_truthy(oe)) {
		if (O.output_encode_true || (oe->t == JS_STRING && (str_eqc(oe->s, "encode-only") || str_eqc(oe->s, "encode-no-creation")))) {
			TpCode *m = tp_add(c, tp_code(NULL));
			Node *w1 = tp_tref(m, C.u->binary_writer_t, (NodeList){0});
			Node *w2 = tp_tref(m, C.u->binary_writer_t, (NodeList){0});
			dts_add(members, msig("encode", NULL, L(sparam("message", tref("T", NULL), false), sparam("writer", w1, true), NULL), w2));
		}
		if (O.output_encode_true || (oe->t == JS_STRING && str_eqc(oe->s, "decode-only"))) {
			TpCode *m = tp_add(c, tp_code(NULL));
			Node *r = tp_tref(m, C.u->binary_reader_t, (NodeList){0});
			dts_add(members, msig("decode", NULL,
					      L(sparam("input", dts_tunion(r, tref("Uint8Array", NULL), NULL), false),
						sparam("length", KW("number"), true), NULL),
					      tref("T", NULL)));
		}
	}
	if (opt_true(&O.opts, "useAsyncIterable")) {
		/* source: AsyncIterable<X | X[]> | Iterable<X | X[]> */
		for (int k = 0; k < 2; k++) {
			const char *x = k ? "Uint8Array" : "T";
			Node *u1 = dts_tunion(tref(x, NULL), dts_tarray(tref(x, NULL)), NULL);
			Node *u2 = dts_tunion(tref(x, NULL), dts_tarray(tref(x, NULL)), NULL);
			Node *src = dts_tunion(tref("AsyncIterable", u1, NULL), tref("Iterable", u2, NULL), NULL);
			tp_add(c, tp_code(NULL));
			dts_add(members, msig(k ? "decodeTransform" : "encodeTransform", NULL, L(sparam("source", src, false), NULL),
					      tref("AsyncIterable", tref(k ? "T" : "Uint8Array", NULL), NULL)));
		}
	}
	JsVal *oj = opt(&O.opts, "outputJsonMethods");
	if (js_truthy(oj)) {
		if (O.output_json_true || (oj->t == JS_STRING && str_eqc(oj->s, "from-only")))
			dts_add(members, msig("fromJSON", NULL, L(sparam("object", KW("any"), false), NULL), tref("T", NULL)));
		if (O.output_json_true || (oj->t == JS_STRING && str_eqc(oj->s, "to-only")))
			dts_add(members, msig("toJSON", NULL, L(sparam("message", tref("T", NULL), false), NULL), KW("unknown")));
	}
	if (O.output_partial) {
		if (O.use_exact_types) {
			TpCode *m1 = tp_add(c, tp_code(NULL));
			dts_add(members, msig("create", exact_tparams(m1, "T"), L(sparam("base", tref("I", NULL), true), NULL), tref("T", NULL)));
			TpCode *m2 = tp_add(c, tp_code(NULL));
			dts_add(members, msig("fromPartial", exact_tparams(m2, "T"), L(sparam("object", tref("I", NULL), false), NULL), tref("T", NULL)));
		} else {
			dts_add(members, msig("create", NULL, L(sparam("base", tref("DeepPartial", tref("T", NULL), NULL), true), NULL), tref("T", NULL)));
			dts_add(members, msig("fromPartial", NULL, L(sparam("object", tref("DeepPartial", tref("T", NULL), NULL), false), NULL),
					      tref("T", NULL)));
		}
	}
	return members->n;
}

static void build_message_fns(TpCode *c)
{
	TpCode *j = tp_add(c, tp_code(NULL));
	NodeList members = {0};
	message_fns_members(j, &members);
	Node *tp = has_type_member() ? dts_tparams(dts_tparam("T", NULL, NULL), dts_tparam("V", KW("string"), NULL), NULL) : tparams1("T");
	Node *i = dts_interface(ID("MessageFns"), tp);
	i->c->list = members;
	dts_add(&c->list, maybe_exported(i));
}

/* the extensionStaticMembers of makeMessageFns */
static void extension_fns_members(TpCode *c, NodeList *members)
{
	if (!opt_true(&O.opts, "outputExtensions") || !O.unknown_fields)
		return;
	JsVal *oe = opt(&O.opts, "outputEncodeMethods");
	if (!js_truthy(oe))
		return;
	if (O.output_encode_true || (oe->t == JS_STRING && (str_eqc(oe->s, "encode-only") || str_eqc(oe->s, "encode-no-creation")))) {
		TpCode *m = tp_add(c, tp_code(NULL));
		Node *e = tp_co_type(m, C.u->extension, L(tref("E", NULL), NULL));
		dts_add(members, msig("setExtension", tparams1("E"),
				      L(sparam("message", tref("T", NULL), false), sparam("extension", e, false), sparam("value", tref("E", NULL), false), NULL),
				      KW("void")));
	}
	if (O.output_encode_true || (oe->t == JS_STRING && str_eqc(oe->s, "decode-only"))) {
		TpCode *m = tp_add(c, tp_code(NULL));
		Node *e = tp_co_type(m, C.u->extension, L(tref("E", NULL), NULL));
		dts_add(members, msig("getExtension", tparams1("E"), L(sparam("message", tref("T", NULL), false), sparam("extension", e, false), NULL),
				      dts_tunion(tref("E", NULL), KW("undefined"), NULL)));
	}
}

static void build_extension_fns(TpCode *c)
{
	TpCode *j = tp_add(c, tp_code(NULL));
	NodeList members = {0};
	extension_fns_members(j, &members);
	Node *i = dts_interface(ID("ExtensionFns"), tparams1("T"));
	i->c->list = members;
	if (!members.n)
		i->c->hints |= H_CLOSE_NL;
	dts_add(&c->list, maybe_exported(i));
}

/* makeExtensionClass */
static void build_extension(TpCode *c)
{
	(void)c;
	Node *i = dts_interface(ID("Extension"), tparams1("T"));
	Node *u8 = dts_tarray(tref("Uint8Array", NULL));
	dts_interface_add(i, prop_sig("number", KW("number"), false, false));
	dts_interface_add(i, prop_sig("tag", KW("number"), false, false));
	dts_interface_add(i, prop_sig("singularTag", KW("number"), true, false));
	dts_interface_add(i, prop_sig("packedTag", KW("number"), true, false));
	dts_interface_add(i, prop_sig("encode", dts_tfn(L(dts_binding("message", tref("T", NULL), false), NULL), NULL, u8), true, false));
	dts_interface_add(i, prop_sig("decode", dts_tfn(L(dts_binding("tag", KW("number"), false), dts_binding("input", dts_tarray(tref("Uint8Array", NULL)), false), NULL), NULL,
							 tref("T", NULL)),
				      true, false));
	dts_interface_add(i, prop_sig("repeated", KW("boolean"), false, false));
	dts_interface_add(i, prop_sig("packed", KW("boolean"), false, false));
	dts_add(&c->list, dts_export(i));
}

/* ExtensionHolder of makeMessageFns */
static void build_extension_holder(TpCode *c)
{
	(void)c;
	Node *mapped = dts_tmapped("key", tref("T", NULL), NULL, tref("Extension", tref("V", NULL), NULL), MAPPED_NONE, MAPPED_NONE);
	mapped->hints |= H_MAPPED_SEMI;
	Node *tp = dts_tparams(dts_tparam("T", KW("string"), NULL), dts_tparam("V", NULL, NULL), NULL);
	dts_add(&c->list, maybe_exported(HINT(dts_type_alias(ID("ExtensionHolder"), tp, mapped), H_NO_SEMI)));
}

static Node *wrap_iface(const char *name, Node *m1, Node *m2)
{
	Node *i = dts_interface(ID(name), NULL);
	dts_interface_add(i, m1);
	dts_interface_add(i, m2);
	return maybe_exported(i);
}

/* {[key: string]: any} */
static Node *any_object_type(void)
{
	Node *lit = dts_ttype_lit();
	dts_ttype_lit_add(lit, HINT(dts_index_sig("key", KW("string"), KW("any"), false), H_NO_SEMI));
	return lit;
}

static void build_struct_wrapper_fns(TpCode *c)
{
	const char *s = wrap_type_name("Struct", false);
	Node *m1 = msig("wrap", NULL, L(sparam("object", dts_tunion(any_object_type(), KW("undefined"), NULL), false), NULL), tref(s, NULL));
	Node *m2 = msig("unwrap", NULL, L(sparam("message", tref(s, NULL), false), NULL), any_object_type());
	dts_add(&c->list, wrap_iface("StructWrapperFns", m1, m2));
}

static void build_any_value_wrapper_fns(TpCode *c)
{
	const char *v = wrap_type_name("Value", false);
	Node *m1 = msig("wrap", NULL, L(sparam("value", KW("any"), false), NULL), tref(v, NULL));
	Node *rt = dts_tunion(KW("string"), KW("number"), KW("boolean"), tref("Object", NULL), KW("null"), tref("Array", KW("any"), NULL),
			      KW("undefined"), NULL);
	Node *m2 = msig("unwrap", NULL, L(sparam("message", KW("any"), false), NULL), rt);
	dts_add(&c->list, wrap_iface("AnyValueWrapperFns", m1, m2));
}

static void build_list_value_wrapper_fns(TpCode *c)
{
	const char *l = wrap_type_name("ListValue", false);
	Node *arr = tref(O.use_readonly_types ? "ReadonlyArray" : "Array", KW("any"), NULL);
	Node *m1 = msig("wrap", NULL, L(sparam("array", dts_tunion(arr, KW("undefined"), NULL), false), NULL), tref(l, NULL));
	Node *m2 = msig("unwrap", NULL, L(sparam("message", O.use_readonly_types ? KW("any") : tref(l, NULL), false), NULL),
			tref("Array", KW("any"), NULL));
	dts_add(&c->list, wrap_iface("ListValueWrapperFns", m1, m2));
}

static void build_field_mask_wrapper_fns(TpCode *c)
{
	const char *f = wrap_type_name("FieldMask", false);
	Node *a = dts_tarray(KW("string"));
	Node *m1 = msig("wrap", NULL, L(sparam("paths", O.use_readonly_types ? dts_treadonly(a) : a, false), NULL), tref(f, NULL));
	Node *rt = dts_tarray(KW("string"));
	if (O.uo_all)
		rt = dts_tunion(rt, KW("undefined"), NULL);
	Node *m2 = msig("unwrap", NULL, L(sparam("message", O.use_readonly_types ? KW("any") : tref(f, NULL), false), NULL), rt);
	dts_add(&c->list, wrap_iface("FieldMaskWrapperFns", m1, m2));
}

/* ---- makeAnyJsonUtils ---- */

static void build_any_json_value_types(TpCode *c)
{
	static const char *const names[] = {"Any", "BoolValue", "BytesValue", "DoubleValue", "Duration", "FieldMask", "FloatValue",
					    "Int32Value", "Int64Value", "ListValue", "StringValue", "Struct", "Timestamp", "UInt32Value",
					    "UInt64Value", "Value"};
	Node *a = dts_arr(NULL);
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
		Node *w = dts_new(N_EXPR_OR_SPREAD);
		w->a = dq(xcat("google.protobuf.", names[i]));
		dts_add(&a->list, w);
	}
	a->hints |= H_TRAILING_COMMA;
	dts_add(&c->list, const_("anyJsonValueTypes", dts_treadonly(dts_tarray(KW("string"))), a));
}

/* makeNiceGrpcServerStreamingMethodResult */
static void build_server_streaming_method_result(TpCode *c)
{
	Node *lit = dts_ttype_lit();
	Node *key = dts_new(N_COMPUTED_PROP_NAME);
	key->a = MEM(ID("Symbol"), "asyncIterator");
	dts_ttype_lit_add(lit, NL(dts_method_sig(key, NULL, (NodeList){0}, tref("AsyncIterator", tref("Response", NULL), KW("void"), NULL), false), LINE));
	Node *t = dts_type_alias(ID("ServerStreamingMethodResult"), tparams1("Response"), lit);
	dts_add(&c->list, opt_true(&O.opts, "outputIndex") ? t : dts_export(t));
}

/* makeGrpcWebErrorClass */
static void build_grpc_web_error(TpCode *c)
{
	Node *cls = dts_class(MEM(gt(c), "Error"), NULL);
	Node *sup = dts_new(N_SUPER);
	NodeList params = L(param("message", KW("string"), false, NULL),
			    dts_param_prop(ACC_PUBLIC, 0, dts_binding("code", tref("grpc.Code", NULL), false)),
			    dts_param_prop(ACC_PUBLIC, 0, dts_binding("metadata", tref("grpc.Metadata", NULL), false)), NULL);
	dts_class_add(cls, NL(dts_constructor(params, block(ES(call(sup, ID("message"), NULL)), NULL), ACC_NONE), LINE));
	dts_add(&c->list, dts_export(dts_class_decl(ID("GrpcWebError"), cls)));
}

/* ---- makeObjectIdMethods ---- */

/* `${mongodb}.ObjectId` as a type */
static Node *mongo_object_id_type(TpCode *c)
{
	Node *q = dts_new(N_TS_QUALIFIED_NAME);
	q->a = tp_entity(c, C.u->mongodb);
	q->b = dts_id("ObjectId");
	return dts_tref_node(q, (NodeList){0});
}

static void build_from_proto_object_id(TpCode *c)
{
	Node *rt = mongo_object_id_type(c);
	Node *e = newx(MEM(tp_ref(c, C.u->mongodb), "ObjectId"), MEM(ID("oid"), "value"), NULL);
	dts_add(&c->list, fn_decl("fromProtoObjectId", NULL, L(param("oid", tref("ObjectId", NULL), false, NULL), NULL), rt, block(ret_stmt(e), NULL)));
}

static void build_from_json_object_id(TpCode *c)
{
	Node *rt = mongo_object_id_type(c);
	Node *t1 = dts_instanceof(ID("o"), MEM(tp_ref(c, C.u->mongodb), "ObjectId"));
	Node *r2 = ret_stmt(newx(MEM(tp_ref(c, C.u->mongodb), "ObjectId"), ID("o"), NULL));
	Node *r3 = ret_stmt(call(tp_co(c, C.u->from_proto_object_id), mcall(ID("ObjectId"), "fromJSON", ID("o"), NULL), NULL));
	Node *ifs = dts_if(t1, block(ret_stmt(ID("o")), NULL),
			   dts_if(BIN(EQEQEQ, dts_unary(UNARY_TYPEOF, ID("o")), DQ("string")), block(r2, NULL), block(r3, NULL)));
	dts_add(&c->list, fn_decl("fromJsonObjectId", NULL, L(param("o", KW("any"), false, NULL), NULL), rt, block(ifs, NULL)));
}

static void build_to_proto_object_id(TpCode *c)
{
	Node *pt = mongo_object_id_type(c);
	Node *body = block(const_("value", NULL, mcall(ID("oid"), "toString", NULL)), ret_stmt(obj(shorthand("value"), NULL)), NULL);
	dts_add(&c->list, fn_decl("toProtoObjectId", NULL, L(param("oid", pt, false, NULL), NULL), tref("ObjectId", NULL), body));
}


/* ---- makeUtils ---- */

static TpCond *helper(Utils *u, const char *name, Builder b, bool is_type)
{
	TpCond *co = tp_cond(name, NULL, is_type);
	u->helpers[u->nhelpers++] = (Helper){co, b};
	return co;
}

COLD static Utils *make_utils(void)
{
	Utils *u = xalloc(sizeof *u);
	C.u = u;
	/* makeByteUtils */
	if (opt_true(&O.opts, "globalThisPolyfill"))
		u->global_this = helper(u, "gt", build_gt_polyfill, false);
	else
		u->global_this = helper(u, "globalThis", NULL, false);
	u->bytes_from_base64 = helper(u, "bytesFromBase64", build_bytes_from_base64, false);
	u->base64_from_bytes = helper(u, "base64FromBytes", build_base64_from_bytes, false);
	u->float_to_json = helper(u, "floatToJson", build_float_to_json, false);
	/* makeDeepPartial */
	u->builtin = helper(u, "Builtin", build_builtin, false);
	u->deep_partial = helper(u, "DeepPartial", build_deep_partial, false);
	u->exact = helper(u, "Exact", build_exact, false);
	/* makeObjectIdMethods */
	u->mongodb = tp_imp("mongodb*mongodb");
	u->from_json_object_id = helper(u, "fromJsonObjectId", build_from_json_object_id, false);
	u->from_proto_object_id = helper(u, "fromProtoObjectId", build_from_proto_object_id, false);
	u->to_proto_object_id = helper(u, "toProtoObjectId", build_to_proto_object_id, false);
	/* makeTimestampMethods */
	u->timestamp = imp_proto("google/protobuf/timestamp", wrap_type_name("Timestamp", false));
	u->nano_date = tp_imp("NanoDate=nano-date");
	u->to_timestamp = helper(u, "toTimestamp", build_to_timestamp, false);
	u->from_timestamp = helper(u, "fromTimestamp", build_from_timestamp, false);
	u->timestamp_fraction_regex = helper(u, "timestampFractionRegex", build_timestamp_fraction_regex, false);
	u->from_json_timestamp = helper(u, "fromJsonTimestamp", build_from_json_timestamp, false);
	u->timestamp_json_regex = helper(u, "timestampJsonRegex", build_timestamp_json_regex, false);
	u->timestamp_from_json = helper(u, "timestampFromJson", build_timestamp_from_json, false);
	u->timestamp_to_json = helper(u, "timestampToJson", build_timestamp_to_json, false);
	/* makeLongUtils */
	u->long_ = tp_imp("Long=long");
	u->number_to_long = helper(u, "numberToLong", build_number_to_long, false);
	u->long_to_number = helper(u, "longToNumber", build_long_to_number, false);
	/* makeComparisonUtils */
	u->is_object = helper(u, "isObject", build_is_object, false);
	u->is_set = helper(u, "isSet", build_is_set, false);
	u->server_streaming_method_result = helper(u, "ServerStreamingMethodResult", build_server_streaming_method_result, false);
	u->grpc_web_error = helper(u, "GrpcWebError", build_grpc_web_error, false);
	u->extension = helper(u, "Extension", build_extension, false);
	u->fail = helper(u, "fail", build_fail, false);
	/* makeMessageFns */
	u->binary_writer_t = tp_imp("t:BinaryWriter@@bufbuild/protobuf/wire");
	u->binary_reader_t = tp_imp("t:BinaryReader@@bufbuild/protobuf/wire");
	TpCode probe = {0};
	NodeList members = {0};
	if (message_fns_members(&probe, &members) > 0)
		u->message_fns = helper(u, "MessageFns", build_message_fns, false);
	u->extension_fns = helper(u, "ExtensionFns", build_extension_fns, false);
	u->extension_holder = helper(u, "ExtensionHolder", build_extension_holder, false);
	u->struct_wrapper_fns = helper(u, "StructWrapperFns", build_struct_wrapper_fns, false);
	u->any_value_wrapper_fns = helper(u, "AnyValueWrapperFns", build_any_value_wrapper_fns, false);
	u->list_value_wrapper_fns = helper(u, "ListValueWrapperFns", build_list_value_wrapper_fns, false);
	u->field_mask_wrapper_fns = helper(u, "FieldMaskWrapperFns", build_field_mask_wrapper_fns, false);
	u->any_json_value_types = helper(u, "anyJsonValueTypes", build_any_json_value_types, false);
	/* the declaration-site codes (built once, like upstream) */
	for (int i = 0; i < u->nhelpers; i++)
		if (u->helpers[i].build)
			u->helpers[i].build(u->helpers[i].co->decl);
	return u;
}
