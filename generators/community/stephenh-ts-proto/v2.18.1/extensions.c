/* Port of ts-proto v2.18.1 src/main.ts: generateExtension, generateSetExtension and
 * generateGetExtension (outputExtensions).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* getEncodeSnippet of generateExtension: `writer.<call>(...)` without a tag */
static TpCode *ext_encode_snippet(const TpdField *f, Node *place)
{
	TpCode *c = tp_code(NULL);
	if (is_enum(f) && O.string_enums) {
		const char *rc = to_reader_call(f->type);
		Node *tn = tp_ref(c, get_enum_method(f->type_name, "ToNumber"));
		c->node = mcall(ID("writer"), rc, call(tn, place, NULL), NULL);
	} else if (is_long(f) && O.force_long == LONG_BIGINT) {
		c->node = mcall(ID("writer"), to_reader_call(f->type), mcall(place, "toString", NULL), NULL);
	} else if (is_scalar(f) || is_enum(f)) {
		const char *rc = to_reader_call(f->type);
		c->node = mcall(ID("writer"), rc, is_long(f) && O.force_long == LONG_LONG ? mcall(place, "toString", NULL) : place, NULL);
	} else if (is_object_id(f) && O.use_mongo_object_id) {
		Node *t = tp_expr(tp_sub(c, basic_type_name(f, true)));
		Node *oid = call(tp_co(c, C.u->to_proto_object_id), place, NULL);
		c->node = mcall(mcall(t, "encode", oid, mcall(ID("writer"), "fork", NULL), NULL), "join", NULL);
	} else if (is_timestamp(f) && (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL)) {
		Node *t = tp_expr(tp_sub(c, basic_type_name(f, true)));
		Node *ts = call(tp_co(c, C.u->to_timestamp), place, NULL);
		c->node = mcall(mcall(t, "encode", ts, mcall(ID("writer"), "fork", NULL), NULL), "join", NULL);
	} else if (is_value_type(f)) {
		TpCode *type = basic_type_name(f, true);
		Node *t = tp_expr(tp_sub(c, type));
		Node *v;
		if (is_any_value_type(f) || is_list_value_type(f) || is_struct_type(f) || is_field_mask_type(f)) {
			v = mcall(tp_expr(tp_sub(c, type)), "wrap", place, NULL);
		} else {
			v = dts_obj();
			if (O.add_type_to_messages)
				dts_obj_add(v, kv("$type", sq(f->type_name + 1)));
			dts_obj_add(v, kv("value", dts_nonnull(place)));
		}
		c->node = mcall(mcall(t, "encode", v, mcall(ID("writer"), "fork", NULL), NULL), "join", NULL);
	} else if (is_message(f)) {
		Node *t = tp_expr(tp_sub(c, basic_type_name(f, false)));
		if (f->type == T_GROUP)
			c->node = mcall(mcall(t, "encode", place, ID("writer"), NULL), "uint32", num_text(xfmt("%lld", (long long)(((unsigned)f->number << 3) | 4u))), NULL);
		else
			c->node = mcall(mcall(t, "encode", place, mcall(ID("writer"), "fork", NULL), NULL), "join", NULL);
	} else {
		fail("Unhandled field [object Object]");
	}
	return c;
}

/* `const writer = new BinaryWriter;` (no parentheses in the template) */
static Node *ext_new_writer(TpCode *c, TpImport *bw, bool parens)
{
	Node *n = newx(tp_ref(c, bw), NULL);
	if (!parens)
		n->flags &= ~(unsigned)F_HAS_ARGS;
	return const_("writer", NULL, n);
}

static Node *ext_push_finish(void) { return ES(mcall(ID("encoded"), "push", mcall(ID("writer"), "finish", NULL), NULL)); }

typedef struct {
	const char *name;
	TpCode *type;
	TpCode *info; /* the extensionInfo object literal */
} ExtensionCode;

/* generateExtension */
static ExtensionCode generate_extension(TpdMessage *m, const TpdField *f)
{
	TpCode *type = to_type_name(m, f, false);
	int pt = is_repeated(f) ? packed_type(f->type) : -1;
	unsigned packed_tag = ((unsigned)f->number << 3) | 2u;
	unsigned singular_tag = ((unsigned)f->number << 3) | (unsigned)basic_wire_type(f->type);
	bool packed = is_repeated(f) && packed_field(f) > 0; /* packedField is a number: 0 (varint) is false */
	TpCode *c = tp_code(NULL);
	Node *o = dts_obj();
	dts_obj_add(o, NL(kv("number", numi(f->number)), LINE));
	dts_obj_add(o, NL(kv("tag", num_text(xfmt("%lld", (long long)(packed ? packed_tag : singular_tag)))), LINE));
	if (pt >= 0) {
		dts_obj_add(o, NL(kv("singularTag", num_text(xfmt("%lld", (long long)singular_tag))), LINE));
		dts_obj_add(o, NL(kv("packedTag", num_text(xfmt("%lld", (long long)packed_tag))), LINE));
	}
	dts_obj_add(o, NL(kv("repeated", dts_bool(f->label == LABEL_REP)), LINE));
	dts_obj_add(o, NL(kv("packed", dts_bool(f->options && f->options->packed)), LINE));
	TpImport *br = tp_imp("BinaryReader@@bufbuild/protobuf/wire");
	TpImport *bw = tp_imp("BinaryWriter@@bufbuild/protobuf/wire");
	JsVal *oe = opt(&O.opts, "outputEncodeMethods");
	if (O.output_encode_true || (oe->t == JS_STRING && (str_eqc(oe->s, "encode-only") || str_eqc(oe->s, "encode-no-creation")))) {
		TpCode *h = tp_add(c, tp_code(NULL));
		Node *ptype = tp_sub(h, type);
		NodeList body = L(const_("encoded", dts_tarray(tref("Uint8Array", NULL)), dts_arr(NULL)), NULL);
		TpCode *w = tp_add(c, tp_code(NULL));
		if (is_repeated(f) && !packed) {
			Node *vd = dts_var_decl(VAR_CONST);
			dts_add(&vd->list, dts_declarator(dts_binding("v", NULL, false), NULL));
			Node *nw = ext_new_writer(w, bw, true);
			Node *ws = ES(tp_sub(w, ext_encode_snippet(f, ID("v"))));
			dts_add(&body, dts_for_of(vd, ID("value"), block(nw, ws, ext_push_finish(), NULL), false));
		} else if (is_repeated(f)) {
			Node *v = is_long(f) && O.force_long == LONG_LONG ? mcall(ID("v"), "toString", NULL) : ID("v");
			Node *vd = dts_var_decl(VAR_CONST);
			dts_add(&vd->list, dts_declarator(dts_binding("v", NULL, false), NULL));
			Node *nw = ext_new_writer(w, bw, false);
			Node *loop = dts_for_of(vd, ID("value"), block(ES(tp_sub(w, ext_encode_snippet(f, v))), NULL), false);
			dts_add(&body, nw);
			dts_add(&body, ES(mcall(ID("writer"), "fork", NULL)));
			dts_add(&body, loop);
			dts_add(&body, ES(mcall(ID("writer"), "join", NULL)));
			dts_add(&body, ext_push_finish());
		} else if (is_scalar(f) || is_enum(f)) {
			Node *cond = tp_sub(w, not_default_check(f, m ? m->options : NULL, ID("value")));
			Node *nw = ext_new_writer(w, bw, false);
			Node *ws = ES(tp_sub(w, ext_encode_snippet(f, ID("value"))));
			dts_add(&body, dts_if(cond, block(nw, ws, ext_push_finish(), NULL), NULL));
		} else {
			Node *nw = ext_new_writer(w, bw, false);
			dts_add(&body, nw);
			dts_add(&body, ES(tp_sub(w, ext_encode_snippet(f, ID("value")))));
			dts_add(&body, ext_push_finish());
		}
		tp_add(c, tp_code(NULL));
		dts_add(&body, RET(ID("encoded")));
		Node *fn = arrow(L(dts_binding("value", ptype, false), NULL), dts_tarray(tref("Uint8Array", NULL)), blockl(body));
		dts_obj_add(o, NL(kv("encode", fn), LINE));
	}
	if (O.output_encode_true || (oe->t == JS_STRING && str_eqc(oe->s, "decode-only"))) {
		TpCode *h = tp_add(c, tp_code(NULL));
		Node *rtype = tp_sub(h, type);
		NodeList body = {0};
		TpCode *read = get_decode_read_snippet(f);
		if (is_repeated(f)) {
			TpCode *vc = tp_add(c, tp_code(NULL));
			dts_add(&body, const_("values", tp_sub(vc, type), dts_arr(NULL)));
			TpCode *lc = tp_add(c, tp_code(NULL));
			NodeList lb = L(const_("reader", NULL, newx(tp_ref(lc, br), ID("buffer"), NULL)), NULL);
			TpCode *pc = tp_add(c, tp_code(NULL));
			if (pt < 0) {
				dts_add(&lb, ES(mcall(ID("values"), "push", tp_sub(pc, read), NULL)));
			} else {
				Node *inner = dts_while(BIN(LT, MEM(ID("reader"), "pos"), ID("end2")), block(ES(mcall(ID("values"), "push", tp_sub(pc, read), NULL)), NULL));
				Node *ifs = dts_if(BIN(EQEQ, ID("tag"), num_text(xfmt("%lld", (long long)packed_tag))),
						   block(const_("end2", NULL, BIN(ADD, mcall(ID("reader"), "uint32", NULL), MEM(ID("reader"), "pos"))), inner, NULL),
						   block(ES(mcall(ID("values"), "push", tp_sub(pc, read), NULL)), NULL));
				dts_add(&lb, ifs);
			}
			Node *bd = dts_var_decl(VAR_CONST);
			dts_add(&bd->list, dts_declarator(dts_binding("buffer", NULL, false), NULL));
			dts_add(&body, dts_for_of(bd, ID("input"), blockl(lb), false));
			tp_add(c, tp_code(NULL));
			dts_add(&body, BLANK(RET(ID("values"))));
		} else {
			TpCode *rc = tp_add(c, tp_code(NULL));
			Node *last = dts_index(ID("input"), BIN(SUB, MEM(ID("input"), "length"), NUM("1")));
			Node *arg = BIN(NULLISH, last, call(tp_co(rc, C.u->fail), NULL));
			dts_add(&body, const_("reader", NULL, newx(tp_ref(rc, br), arg, NULL)));
			dts_add(&body, RET(tp_sub(rc, read)));
		}
		Node *fn = arrow(L(dts_binding("tag", KW("number"), false), dts_binding("input", dts_tarray(tref("Uint8Array", NULL)), false), NULL), rtype,
				 blockl(body));
		dts_obj_add(o, NL(kv("decode", fn), LINE));
	}
	tp_add(c, tp_code(NULL));
	o->hints |= H_TRAILING_COMMA | H_CLOSE_NL;
	c->node = o;
	ExtensionCode e = {maybe_snake_to_camel(f->name), type, c};
	return e;
}

/* `message._unknownFields` */
static Node *uf(void) { return MEM(ID("message"), "_unknownFields"); }
static Node *ext_m(const char *p) { return MEM(ID("extension"), p); }

/* generateSetExtension */
static TpCode *generate_set_extension(const char *full_name)
{
	TpCode *c = tp_code(NULL);
	Node *ext_t = tp_co_type(c, C.u->extension, L(tref("T", NULL), NULL));
	NodeList del = L(ES(dts_unary(UNARY_DELETE, dts_index(uf(), ext_m("tag")))), NULL);
	dts_add(&del, BLANK(dts_if(BIN(NOTEQEQ, ext_m("singularTag"), UNDEF()), block(ES(dts_unary(UNARY_DELETE, dts_index(uf(), ext_m("singularTag")))), NULL), NULL)));
	dts_add(&del, dts_if(BIN(NOTEQEQ, ext_m("packedTag"), UNDEF()), block(ES(dts_unary(UNARY_DELETE, dts_index(uf(), ext_m("packedTag")))), NULL), NULL));
	NodeList set = L(dts_if(BIN(EQEQEQ, uf(), UNDEF()), block(ES(SET(uf(), dts_obj())), NULL), NULL),
			 BLANK(ES(SET(dts_index(uf(), ext_m("tag")), ID("encoded")))), NULL);
	Node *body = block(const_("encoded", NULL, call(dts_nonnull(ext_m("encode")), ID("value"), NULL)),
			   BLANK(dts_if(BIN(NOTEQEQ, uf(), UNDEF()), blockl(del), NULL)),
			   BLANK(dts_if(BIN(NOTEQEQ, MEM(ID("encoded"), "length"), NUM("0")), blockl(set), NULL)), NULL);
	NodeList params = L(param("message", tref(full_name, NULL), false, NULL), param("extension", ext_t, false, NULL),
			    param("value", tref("T", NULL), false, NULL), NULL);
	c->node = method("setExtension", tparams1("T"), params, KW("void"), body);
	return c;
}

/* generateGetExtension */
static TpCode *generate_get_extension(const char *full_name)
{
	TpCode *c = tp_code(NULL);
	Node *ext_t = tp_co_type(c, C.u->extension, L(tref("T", NULL), NULL));
	Node *tu = dts_tunion(tref("T", NULL), KW("undefined"), NULL);
	Node *s1 = let_("results", dts_tunion(tref("T", NULL), KW("undefined"), NULL), UNDEF());
	Node *s2 = BLANK(dts_if(BIN(EQEQEQ, uf(), UNDEF()), block(RET(UNDEF()), NULL), NULL));
	Node *s3 = BLANK(let_("list", NULL, dts_index(uf(), ext_m("tag"))));
	Node *s4 = BLANK(dts_if(BIN(NOTEQEQ, ID("list"), UNDEF()),
				block(ES(SET(ID("results"), call(dts_nonnull(ext_m("decode")), ext_m("tag"), ID("list"), NULL))), NULL), NULL));
	Node *t5 = BIN(LOGICAL_OR, BIN(EQEQEQ, ext_m("singularTag"), UNDEF()), BIN(EQEQEQ, ext_m("packedTag"), UNDEF()));
	Node *s5 = BLANK(dts_if(t5, block(RET(ID("results")), NULL), NULL));
	Node *nd = dts_cond(dts_paren(BIN(EQEQEQ, ext_m("singularTag"), ext_m("tag"))), ext_m("packedTag"), ext_m("singularTag"));
	Node *s6 = BLANK(const_("nonDefaultTag", NULL, nd));
	Node *s7 = ES(SET(ID("list"), dts_index(uf(), ID("nonDefaultTag"))));
	Node *r_any = dts_paren(dts_as(ID("results"), KW("any")));
	Node *inner = dts_if(BIN(LOGICAL_AND, BIN(NOTEQEQ, ID("results"), UNDEF()), BIN(NOTEQEQ, MEM(r_any, "length"), NUM("0"))),
			     block(ES(SET(ID("results"), mcall(dts_paren(dts_as(ID("results"), KW("any"))), "concat", ID("results2"), NULL))), NULL),
			     block(ES(SET(ID("results"), ID("results2"))), NULL));
	Node *s8 = BLANK(dts_if(BIN(NOTEQEQ, ID("list"), UNDEF()),
				block(const_("results2", NULL, call(dts_nonnull(ext_m("decode")), ID("nonDefaultTag"), ID("list"), NULL)), BLANK(inner), NULL),
				NULL));
	Node *s9 = BLANK(RET(ID("results")));
	NodeList params = L(param("message", tref(full_name, NULL), false, NULL), param("extension", ext_t, false, NULL), NULL);
	c->node = method("getExtension", tparams1("T"), params, tu, block(s1, s2, s3, s4, s5, s6, s7, s8, s9, NULL));
	return c;
}
