/* Port of ts-proto v2.18.1 src/main.ts: getDecodeReadSnippet, generateDecode,
 * getEncodeWriteSnippet and generateEncode (the binary methods of a message).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

static Node *reader_call(const char *method, ...)
{
	va_list ap;
	va_start(ap, method);
	NodeList l = nl_of(va_arg(ap, Node *), ap);
	va_end(ap);
	return dts_callv(dts_member(ID("reader"), method), NULL, l);
}

/* code`${type}.decode(reader, reader.uint32())` (group: `.decode(reader)`) */
static TpCode *decode_call(TpCode *type, bool group)
{
	TpCode *c = tp_code(NULL);
	Node *t = tp_expr(tp_sub(c, type));
	c->node = group ? mcall(t, "decode", ID("reader"), NULL) : mcall(t, "decode", ID("reader"), reader_call("uint32", NULL), NULL);
	return c;
}

static TpCode *get_decode_read_snippet(const TpdField *f)
{
	TpCode *r;
	if (is_primitive(f)) {
		r = tp_code(reader_call(to_reader_call(f->type), NULL));
		if (is_bytes(f)) {
			if (O.env == ENV_NODE) {
				TpCode *w = tp_code(NULL);
				w->node = call(MEM(ID("Buffer"), "from"), tp_sub(w, r), NULL);
				r = w;
			}
		} else if (basic_long_wire_type(f->type) >= 0) {
			TpCode *w = tp_code(NULL);
			if (is_js_type_field_option(f)) {
				if (f->options->jstype == TPD_JS_NUMBER) {
					Node *fn = tp_co(w, C.u->long_to_number);
					w->node = call(fn, tp_sub(w, r), NULL);
				} else {
					w->node = mcall(tp_sub(w, r), "toString", NULL);
				}
			} else if (O.force_long == LONG_LONG) {
				Node *l = tp_ref(w, C.u->long_);
				Node *s = mcall(tp_sub(w, r), "toString", NULL);
				if (f->type == T_UINT64 || f->type == T_FIXED64)
					w->node = mcall(l, "fromString", s, dts_bool(true), NULL);
				else
					w->node = mcall(l, "fromString", s, NULL);
			} else if (O.force_long == LONG_STRING) {
				w->node = mcall(tp_sub(w, r), "toString", NULL);
			} else if (O.force_long == LONG_BIGINT) {
				w->node = dts_as(tp_sub(w, r), KW("bigint"));
			} else {
				Node *fn = tp_co(w, C.u->long_to_number);
				w->node = call(fn, tp_sub(w, r), NULL);
			}
			r = w;
		} else if (is_enum(f)) {
			TpCode *w = tp_code(NULL);
			if (O.string_enums) {
				Node *fn = tp_ref(w, get_enum_method(f->type_name, "FromJSON"));
				w->node = call(fn, tp_sub(w, r), NULL);
			} else {
				w->node = dts_as(tp_sub(w, r), KW("any"));
			}
			r = w;
		}
		return r;
	}
	if (is_value_type(f)) {
		TpCode *type = basic_type_name(f, true);
		TpCode *decoder = decode_call(type, false);
		TpCode *u = tp_code(NULL);
		if (is_list_value_type(f) || is_struct_type(f) || is_any_value_type(f) || is_field_mask_type(f)) {
			Node *t = tp_expr(tp_sub(u, type));
			u->node = mcall(t, "unwrap", tp_sub(u, decoder), NULL);
		} else {
			u->node = MEM(tp_sub(u, decoder), "value");
		}
		r = tp_code(NULL);
		r->node = tp_sub(r, u);
		return r;
	}
	if (is_timestamp(f) && (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL)) {
		TpCode *type = basic_type_name(f, true);
		r = tp_code(NULL);
		Node *fn = tp_co(r, C.u->from_timestamp);
		Node *t = tp_expr(tp_sub(r, type));
		r->node = call(fn, mcall(t, "decode", ID("reader"), reader_call("uint32", NULL), NULL), NULL);
		return r;
	}
	if (is_object_id(f) && O.use_mongo_object_id) {
		TpCode *type = basic_type_name(f, true);
		r = tp_code(NULL);
		Node *fn = tp_co(r, C.u->from_proto_object_id);
		Node *t = tp_expr(tp_sub(r, type));
		r->node = call(fn, mcall(t, "decode", ID("reader"), reader_call("uint32", NULL), NULL), NULL);
		return r;
	}
	if (is_message(f))
		return decode_call(basic_type_name(f, false), f->type == T_GROUP);
	fail("Unhandled field [object Object]");
}

static Node *tag_check(unsigned tag)
{
	return dts_if(BIN(NOTEQEQ, ID("tag"), numi(tag)), block(dts_break(), NULL), NULL);
}

static Node *as_any_reader(void) { return dts_paren(dts_as(ID("reader"), KW("any"))); }

/* initializerSnippet: `if (mp === undefined || mp === null) { mp = value; }` */
static Node *initializer(Node *mp, Node *value)
{
	Node *test = or_is_null(BIN(EQEQEQ, tp_clone(mp), UNDEF()), tp_clone(mp));
	return dts_if(test, block(ES(SET(tp_clone(mp), value)), NULL), NULL);
}

static TpCode *generate_decode(const char *full_name, TpdMessage *m)
{
	TpCode *c = tp_code(NULL); /* joinCode(chunks) */
	TpImport *br = tp_imp("BinaryReader@@bufbuild/protobuf/wire");
	TpCode *head = tp_add(c, tp_code(NULL));
	Node *br1 = tp_tref(head, br, (NodeList){0});
	Node *br2 = tp_ref(head, br);
	Node *br3 = tp_ref(head, br);
	Node *err = newx(MEM(tp_co(head, C.u->global_this), "Error"), DQ("protobuf decode recursion limit exceeded"), NULL);

	NodeList try_body = {0};
	dts_add(&try_body, const_("end", NULL,
				  dts_cond(BIN(EQEQEQ, ID("length"), UNDEF()), MEM(ID("reader"), "len"), BIN(ADD, MEM(ID("reader"), "pos"), ID("length")))));
	dts_add(&try_body, const_("message", NULL, create_base_call(full_name)));

	Node *sw = dts_switch(BIN(ZFRSHIFT, ID("tag"), NUM("3")));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *field_name = get_field_name(f);
		Node *mp = prop("message", field_name);
		unsigned tag = (((unsigned)f->number << 3) | (unsigned)basic_wire_type(f->type));
		TpCode *read = get_decode_read_snippet(f);
		bool init_needed = !O.initialize_fields_as_undefined && is_optional_property(f, m->options, C.is_proto3);
		TpCode *fc = tp_add(c, tp_code(NULL));
		NodeList body = {0};
		if (is_repeated(f)) {
			bool nonnull = O.uo_all || O.uo_deprecated_only;
			Node *target = nonnull ? dts_nonnull(tp_clone(mp)) : tp_clone(mp);
			MapType mt;
			if (detect_map_type(m, f, &mt)) {
				const char *var = xfmt("entry%d", (int)f->number);
				Node *setter = should_generate_js_map_type(m, f)
						       ? ES(mcall(target, "set", MEM(ID(var), "key"), MEM(ID(var), "value"), NULL))
						       : ES(SET(dts_index(target, MEM(ID(var), "key")), MEM(ID(var), "value")));
				Node *check = and_not_null(BIN(NOTEQEQ, MEM(ID(var), "value"), UNDEF()), MEM(ID(var), "value"));
				if (O.no_defaults_for_optionals)
					check = BIN(LOGICAL_AND, check, and_not_null(BIN(NOTEQEQ, MEM(ID(var), "key"), UNDEF()), MEM(ID(var), "key")));
				dts_add(&body, tag_check(tag));
				dts_add(&body, BLANK(const_(var, NULL, tp_sub(fc, read))));
				Node *init = init_needed ? initializer(mp, should_generate_js_map_type(m, f) ? newx(ID("Map"), NULL) : dts_obj()) : NULL;
				dts_add(&body, dts_if(check, init ? block(init, setter, NULL) : block(setter, NULL), NULL));
			} else if (packed_type(f->type) < 0) {
				dts_add(&body, tag_check(tag));
				/* `${tagCheck}\n${initializerSnippet}\n...`: a blank line after the check */
				bool blank = true;
				if (init_needed) {
					dts_add(&body, BLANK(initializer(mp, dts_arr(NULL))));
					blank = false;
				}
				if (O.uo_all) {
					Node *el = const_("el", NULL, tp_sub(fc, read));
					dts_add(&body, blank ? BLANK(el) : el);
					dts_add(&body, dts_if(BIN(NOTEQEQ, ID("el"), UNDEF()), block(ES(mcall(target, "push", ID("el"), NULL)), NULL), NULL));
				} else {
					Node *push = ES(mcall(target, "push", tp_sub(fc, read), NULL));
					dts_add(&body, blank ? BLANK(push) : push);
				}
			} else {
				unsigned packed_tag = ((unsigned)f->number << 3) | 2u;
				Node *t2 = nonnull ? dts_nonnull(tp_clone(mp)) : tp_clone(mp);
				Node *b1 = block(ES(mcall(target, "push", tp_sub(fc, read), NULL)), BLANK(dts_continue()), NULL);
				Node *loop = dts_while(BIN(LT, MEM(ID("reader"), "pos"), ID("end2")), block(ES(mcall(t2, "push", tp_sub(fc, read), NULL)), NULL));
				Node *b2 = block(const_("end2", NULL, BIN(ADD, reader_call("uint32", NULL), MEM(ID("reader"), "pos"))), loop,
						 BLANK(dts_continue()), NULL);
				if (init_needed) {
					/* the initializer first in both blocks */
					NodeList l1 = L(initializer(mp, dts_arr(NULL)), NULL), l2 = L(initializer(mp, dts_arr(NULL)), NULL);
					addl(&l1, b1->list);
					addl(&l2, b2->list);
					b1->list = l1;
					b2->list = l2;
				}
				Node *if1 = dts_if(BIN(EQEQEQ, ID("tag"), numi(tag)), b1, NULL);
				Node *if2 = dts_if(BIN(EQEQEQ, ID("tag"), numi(packed_tag)), b2, NULL);
				dts_add(&body, if1);
				dts_add(&body, BLANK(if2));
				dts_add(&body, BLANK(dts_break()));
			}
		} else if (is_within_one_of_that_should_be_union(f)) {
			if (f->oneof_index < 0 || f->oneof_index >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading 'name')");
			Node *on = prop("message", maybe_snake_to_camel(m->oneof_decl[f->oneof_index]->name));
			Node *v = obj(kv("$case", sq(field_name)), kv(oneof_value_name(field_name), tp_sub(fc, read)), NULL);
			dts_add(&body, tag_check(tag));
			dts_add(&body, BLANK(ES(SET(on, v))));
		} else {
			dts_add(&body, tag_check(tag));
			dts_add(&body, BLANK(ES(SET(tp_clone(mp), tp_sub(fc, read)))));
		}
		if (!is_repeated(f) || packed_type(f->type) < 0)
			dts_add(&body, dts_continue());
		Node *cs = dts_case(numi(f->number));
		dts_add(&cs->list, blockl(body));
		dts_add(&sw->list, cs);
	}
	if (!sw->list.n)
		sw->hints |= H_CLOSE_NL;
	NodeList loop_body = L(const_("tag", NULL, reader_call("uint32", NULL)), sw,
			       dts_if(BIN(LOGICAL_OR, BIN(EQEQEQ, dts_paren(BIN(BITAND, ID("tag"), NUM("7"))), NUM("4")), BIN(EQEQEQ, ID("tag"), NUM("0"))),
				      block(dts_break(), NULL), NULL),
			       NULL);
	if (O.unknown_fields) {
		bool nn = O.initialize_fields_as_undefined;
		dts_add(&loop_body, const_("buf", NULL, reader_call("skip", BIN(BITAND, ID("tag"), NUM("7")), NULL)));
		Node *uf = MEM(ID("message"), "_unknownFields");
		if (!nn)
			dts_add(&loop_body, BLANK(dts_if(or_is_null(BIN(EQEQEQ, tp_clone(uf), UNDEF()), tp_clone(uf)),
							 block(ES(SET(tp_clone(uf), dts_obj())), NULL), NULL)));
		Node *ufa = nn ? dts_nonnull(tp_clone(uf)) : tp_clone(uf);
		dts_add(&loop_body, BLANK(const_("list", NULL, dts_index(ufa, ID("tag")))));
		Node *ufb = nn ? dts_nonnull(tp_clone(uf)) : tp_clone(uf);
		dts_add(&loop_body, BLANK(dts_if(or_is_null(BIN(EQEQEQ, ID("list"), UNDEF()), tp_clone(uf)),
						 block(ES(SET(dts_index(ufb, ID("tag")), dts_arr(ID("buf"), NULL))), NULL),
						 block(ES(mcall(ID("list"), "push", ID("buf"), NULL)), NULL))));
	} else {
		dts_add(&loop_body, ES(reader_call("skip", BIN(BITAND, ID("tag"), NUM("7")), NULL)));
	}
	Node *loop = dts_while(BIN(LT, MEM(ID("reader"), "pos"), ID("end")), blockl(loop_body));
	dts_add(&try_body, loop);
	dts_add(&try_body, RET(ID("message")));

	Node *body = block(const_("reader", NULL, dts_cond(dts_instanceof(ID("input"), br2), ID("input"), newx(br3, ID("input"), NULL))),
			   const_("previousRecursionDepth", NULL, BIN(NULLISH, MEM(as_any_reader(), "__tsProtoDecodeDepth"), NUM("0"))),
			   dts_if(BIN(GTEQ, ID("previousRecursionDepth"), NUM("100")), block(dts_throw(err), NULL), NULL),
			   ES(SET(MEM(as_any_reader(), "__tsProtoDecodeDepth"), BIN(ADD, ID("previousRecursionDepth"), NUM("1")))),
			   dts_try(blockl(try_body), NULL, NULL,
				   block(ES(SET(MEM(as_any_reader(), "__tsProtoDecodeDepth"), ID("previousRecursionDepth"))), NULL)),
			   NULL);
	NodeList params = L(param("input", dts_tunion(br1, tref("Uint8Array", NULL), NULL), false, NULL), param("length", KW("number"), true, NULL),
			    NULL);
	c->node = method("decode", NULL, params, tref(full_name, NULL), body);
	return c;
}

UNUSED static TpCode *generate_empty_decode(const char *full_name)
{
	TpCode *c = tp_code(NULL);
	Node *br = tp_tref(c, tp_imp("t:BinaryReader@@bufbuild/protobuf/wire"), (NodeList){0});
	NodeList params = L(param("_", dts_tunion(br, tref("Uint8Array", NULL), NULL), false, NULL), param("length", KW("number"), true, NULL), NULL);
	Node *t = dts_throw(newx(ID("Error"), sq(xcat("decode not generated for ", full_name)), NULL));
	c->node = method("decode", NULL, params, tref(full_name, NULL), block(t, NULL));
	return c;
}

/* ---- encode ---- */

/* getEncodeWriteSnippet: a function of the place (and placeAlt) to a code */
typedef struct {
	const TpdField *f;
	int kind;
	unsigned tag, end_tag;
	TpCode *type;
	TpImport *to_number;
} WriteSnippet;

enum { WS_STRING_ENUM, WS_BIGINT_SIGNED, WS_BIGINT_UNSIGNED, WS_SCALAR, WS_SCALAR_LONG, WS_TIMESTAMP, WS_VALUE_WRAP, WS_VALUE,
       WS_GROUP, WS_MESSAGE, WS_OBJECT_ID };

static WriteSnippet get_encode_write_snippet(const TpdField *f)
{
	WriteSnippet w = {f, 0, 0, 0, NULL, NULL};
	if (is_enum(f) && O.string_enums) {
		w.kind = WS_STRING_ENUM;
		w.tag = ((unsigned)f->number << 3) | (unsigned)basic_wire_type(f->type);
		w.to_number = get_enum_method(f->type_name, "ToNumber");
	} else if (is_long(f) && O.force_long == LONG_BIGINT) {
		w.tag = ((unsigned)f->number << 3) | (unsigned)basic_wire_type(f->type);
		const char *t = to_reader_call(f->type);
		w.kind = str_eq(t, "uint64") || str_eq(t, "fixed64") ? WS_BIGINT_UNSIGNED : WS_BIGINT_SIGNED;
	} else if (is_scalar(f) || is_enum(f)) {
		w.tag = ((unsigned)f->number << 3) | (unsigned)basic_wire_type(f->type);
		w.kind = is_long(f) && O.force_long == LONG_LONG ? WS_SCALAR_LONG : WS_SCALAR;
	} else if (is_object_id(f) && O.use_mongo_object_id) {
		w.kind = WS_OBJECT_ID;
		w.tag = ((unsigned)f->number << 3) | 2u;
		w.type = basic_type_name(f, true);
	} else if (is_timestamp(f) && (O.use_date == DATE_DATE || O.use_date == DATE_STRING || O.use_date == DATE_STRING_NANO || O.use_date == DATE_TEMPORAL)) {
		w.kind = WS_TIMESTAMP;
		w.tag = ((unsigned)f->number << 3) | 2u;
		w.type = basic_type_name(f, true);
	} else if (is_value_type(f)) {
		w.type = basic_type_name(f, true);
		w.kind = is_any_value_type(f) || is_list_value_type(f) || is_struct_type(f) || is_field_mask_type(f) ? WS_VALUE_WRAP : WS_VALUE;
		w.tag = ((unsigned)f->number << 3) | 2u;
	} else if (is_message(f)) {
		w.type = basic_type_name(f, false);
		if (f->type == T_GROUP) {
			w.kind = WS_GROUP;
			w.tag = ((unsigned)f->number << 3) | 3u;
			w.end_tag = ((unsigned)f->number << 3) | 4u;
		} else {
			w.kind = WS_MESSAGE;
			w.tag = ((unsigned)f->number << 3) | 2u;
		}
	} else {
		fail("Unhandled field [object Object]");
	}
	return w;
}

static Node *writer_tag(unsigned tag) { return mcall(ID("writer"), "uint32", numi(tag), NULL); }

/* the code for a place; stmts receives the statements of the BigInt checks (they precede it) */
static TpCode *write_snippet(const WriteSnippet *w, Node *place, Node *place_alt, NodeList *stmts)
{
	TpCode *c = tp_code(NULL);
	const char *rc;
	switch (w->kind) {
	case WS_STRING_ENUM: {
		rc = to_reader_call(w->f->type);
		Node *tn = tp_ref(c, w->to_number);
		c->node = mcall(writer_tag(w->tag), rc, call(tn, tp_clone(place), NULL), NULL);
		return c;
	}
	case WS_BIGINT_SIGNED:
	case WS_BIGINT_UNSIGNED: {
		rc = to_reader_call(w->f->type);
		Node *as = call(MEM(ID("BigInt"), w->kind == WS_BIGINT_SIGNED ? "asIntN" : "asUintN"), NUM("64"), tp_clone(place), NULL);
		Node *msg = sq(xcat("value provided for field ", node_text(place), " of type ", rc, " too large"));
		Node *t = dts_throw(newx(MEM(tp_co(c, C.u->global_this), "Error"), msg, NULL));
		dts_add(stmts, dts_if(BIN(NOTEQEQ, as, tp_clone(place_alt ? place_alt : place)), block(t, NULL), NULL));
		c->node = mcall(writer_tag(w->tag), rc, tp_clone(place), NULL);
		return c;
	}
	case WS_SCALAR:
		c->node = mcall(writer_tag(w->tag), to_reader_call(w->f->type), tp_clone(place), NULL);
		return c;
	case WS_SCALAR_LONG:
		c->node = mcall(writer_tag(w->tag), to_reader_call(w->f->type), mcall(tp_clone(place), "toString", NULL), NULL);
		return c;
	case WS_OBJECT_ID:
	case WS_TIMESTAMP: {
		Node *t = tp_expr(tp_sub(c, w->type));
		Node *fn = tp_co(c, w->kind == WS_OBJECT_ID ? C.u->to_proto_object_id : C.u->to_timestamp);
		c->node = mcall(mcall(t, "encode", call(fn, tp_clone(place), NULL), mcall(writer_tag(w->tag), "fork", NULL), NULL), "join", NULL);
		return c;
	}
	case WS_VALUE_WRAP:
	case WS_VALUE: {
		Node *t = tp_expr(tp_sub(c, w->type));
		TpCode *wv = tp_code(NULL);
		if (w->kind == WS_VALUE_WRAP) {
			wv->node = mcall(tp_expr(tp_sub(wv, w->type)), "wrap", tp_clone(place), NULL);
		} else {
			Node *o = dts_obj();
			if (O.add_type_to_messages)
				dts_obj_add(o, kv("$type", sq(w->f->type_name + 1)));
			dts_obj_add(o, kv("value", dts_nonnull(tp_clone(place))));
			wv->node = o;
		}
		c->node = mcall(mcall(t, "encode", tp_sub(c, wv), mcall(writer_tag(w->tag), "fork", NULL), NULL), "join", NULL);
		return c;
	}
	case WS_GROUP: {
		Node *t = tp_expr(tp_sub(c, w->type));
		c->node = mcall(mcall(t, "encode", tp_clone(place), writer_tag(w->tag), NULL), "uint32", numi(w->end_tag), NULL);
		return c;
	}
	default: {
		Node *t = tp_expr(tp_sub(c, w->type));
		c->node = mcall(mcall(t, "encode", tp_clone(place), mcall(writer_tag(w->tag), "fork", NULL), NULL), "join", NULL);
		return c;
	}
	}
}

/* `${writeSnippet(place)};` as statements */
static NodeList write_stmts(TpCode *parent, const WriteSnippet *w, Node *place, Node *place_alt)
{
	NodeList l = {0};
	TpCode *c = write_snippet(w, place, place_alt, &l);
	dts_add(&l, ES(tp_sub(parent, c)));
	return l;
}

static TpCode *generate_encode(const char *full_name, TpdMessage *m)
{
	TpCode *c = tp_code(NULL);
	TpImport *bw = tp_imp("BinaryWriter@@bufbuild/protobuf/wire");
	TpCode *head = tp_add(c, tp_code(NULL));
	Node *bw1 = tp_tref(head, bw, (NodeList){0});
	Node *bw2 = tp_ref(head, bw);
	Node *bw3 = tp_tref(head, bw, (NodeList){0});
	NodeList params = L(param(m->nfield > 0 || O.unknown_fields ? "message" : "_", tref(full_name, NULL), false, NULL),
			    param("writer", bw1, false, newx(bw2, NULL)), NULL);
	NodeList stmts = {0};
	bool *processed = xalloc(sizeof(bool) * (size_t)(m->noneof_decl + 1));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		const char *field_name = get_field_name(f);
		Node *mp = prop("message", field_name);
		WriteSnippet ws = get_encode_write_snippet(f);
		bool optional = is_optional_property(f, m->options, C.is_proto3);
		TpCode *fc = tp_add(c, tp_code(NULL));
		if (is_repeated(f)) {
			MapType mt;
			if (detect_map_type(m, f, &mt)) {
				Node *place = obj(kv("key", dts_as(ID("key"), KW("any"))), shorthand("value"), NULL);
				if (O.add_type_to_messages) {
					Node *o = obj(kv("$type", sq(f->type_name + 1)), NULL);
					addl(&o->list, place->list);
					place = o;
				}
				NodeList entry;
				TpCode *ec = tp_code(NULL); /* entryWriteSnippet */
				if (is_value_type(mt.value_field)) {
					Node *cond = or_not_null(BIN(NOTEQEQ, ID("value"), UNDEF()), ID("value"));
					entry = L(dts_if(cond, blockl(write_stmts(ec, &ws, place, NULL)), NULL), NULL);
				} else {
					entry = write_stmts(ec, &ws, place, NULL);
				}
				bool use_map = should_generate_js_map_type(m, f);
				Node *target = tp_clone(mp);
				if (optional)
					target = BIN(LOGICAL_OR, target, use_map ? newx(ID("Map"), NULL) : dts_obj());
				Node *fe;
				if (use_map) {
					tp_add(fc, ec);
					fe = mcall(dts_paren(target), "forEach",
						   arrow(L(dts_binding("value", NULL, false), dts_binding("key", NULL, false), NULL), NULL, blockl(entry)), NULL);
				} else {
					Node *gtn = tp_co(fc, C.u->global_this);
					Node *pat = dts_new(N_ARRAY_PAT);
					dts_add(&pat->list, dts_binding("key", NULL, false));
					dts_add(&pat->list, dts_binding("value", NULL, false));
					pat->b = dts_type_ann(dts_ttuple(KW("string"), tp_sub(fc, mt.value_type), NULL));
					tp_add(fc, ec);
					fe = mcall(mcall(MEM(gtn, "Object"), "entries", target, NULL), "forEach", arrow(L(pat, NULL), NULL, blockl(entry)), NULL);
				}
				dts_add(&stmts, ES(fe));
			} else if (packed_field(f) < 0) {
				TpCode *lc = optional ? tp_add(fc, tp_code(NULL)) : fc; /* listWriteSnippet */
				Node *loop = dts_for_of(dts_var_decl(VAR_CONST), tp_clone(mp), blockl(write_stmts(lc, &ws, dts_nonnull(ID("v")), NULL)), false);
				dts_add(&loop->a->list, dts_declarator(dts_binding("v", NULL, false), NULL));
				if (optional)
					dts_add(&stmts, dts_if(BIN(LOGICAL_AND, BIN(NOTEQEQ, tp_clone(mp), UNDEF()), BIN(NOTEQEQ, MEM(tp_clone(mp), "length"), NUM("0"))),
							       block(loop, NULL), NULL));
				else
					dts_add(&stmts, loop);
			} else {
				unsigned tag = ((unsigned)f->number << 3) | 2u;
				const char *rc = to_reader_call(f->type);
				TpCode *lc = tp_add(fc, tp_code(NULL));
				NodeList inner = {0};
				Node *v = ID("v");
				if (is_enum(f) && O.string_enums) {
					Node *tn = tp_ref(lc, get_enum_method(f->type_name, "ToNumber"));
					dts_add(&inner, ES(mcall(ID("writer"), rc, call(tn, v, NULL), NULL)));
				} else {
					if (is_long(f) && O.force_long == LONG_BIGINT) {
						const char *as = str_eq(rc, "uint64") || str_eq(rc, "fixed64") ? "asUintN" : "asIntN";
						Node *msg = sq(xcat("a value provided in array field ", field_name, " of type ", rc, " is too large"));
						Node *t = dts_throw(newx(MEM(tp_co(lc, C.u->global_this), "Error"), msg, NULL));
						dts_add(&inner, dts_if(BIN(NOTEQEQ, call(MEM(ID("BigInt"), as), NUM("64"), ID("v"), NULL), ID("v")), block(t, NULL), NULL));
					}
					Node *arg = is_long(f) && O.force_long == LONG_LONG ? mcall(v, "toString", NULL) : v;
					dts_add(&inner, ES(mcall(ID("writer"), rc, arg, NULL)));
				}
				Node *loop = dts_for_of(dts_var_decl(VAR_CONST), tp_clone(mp), blockl(inner), false);
				dts_add(&loop->a->list, dts_declarator(dts_binding("v", NULL, false), NULL));
				NodeList lw = L(ES(mcall(writer_tag(tag), "fork", NULL)), loop, ES(mcall(ID("writer"), "join", NULL)), NULL);
				Node *cond;
				if (optional) {
					if (is_enum(f) && O.string_enums)
						cond = BIN(LOGICAL_AND, BIN(NOTEQEQ, tp_clone(mp), UNDEF()), BIN(NOTEQEQ, MEM(tp_clone(mp), "length"), NUM("0")));
					else
						cond = BIN(LOGICAL_AND, and_not_null(BIN(NOTEQEQ, tp_clone(mp), UNDEF()), tp_clone(mp)),
							   BIN(NOTEQEQ, MEM(tp_clone(mp), "length"), NUM("0")));
				} else {
					cond = BIN(NOTEQEQ, MEM(tp_clone(mp), "length"), NUM("0"));
				}
				dts_add(&stmts, dts_if(cond, blockl(lw), NULL));
			}
		} else if (is_within_one_of_that_should_be_union(f)) {
			int oi = f->oneof_index;
			if (oi < 0 || oi >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading 'name')");
			if (!processed[oi]) {
				processed[oi] = true;
				const char *oname = maybe_snake_to_camel(m->oneof_decl[oi]->name);
				Node *sw = dts_switch(dts_opt_member(prop("message", oname), "$case", true));
				int nf;
				const int *fi = oneof_fields(m, oi, &nf);
				for (int j = 0; j < nf; j++) {
					TpdField *of = m->field[fi[j]];
					if (!is_within_one_of_that_should_be_union(of))
						continue;
					WriteSnippet ows = get_encode_write_snippet(of);
					const char *ofn = maybe_snake_to_camel(of->name);
					TpCode *cc = tp_add(c, tp_code(NULL));
					NodeList cons = write_stmts(cc, &ows, MEM(prop("message", oname), oneof_value_name(ofn)), NULL);
					dts_add(&cons, dts_break());
					Node *cs = dts_case(dq(ofn));
					cs->list = cons;
					dts_add(&sw->list, cs);
				}
				dts_add(&stmts, sw);
			}
		} else if (is_within_one_of(f) || is_message(f)) {
			Node *cond = and_not_null(BIN(NOTEQEQ, tp_clone(mp), UNDEF()), tp_clone(mp));
			dts_add(&stmts, dts_if(cond, blockl(write_stmts(fc, &ws, mp, NULL)), NULL));
		} else if (is_scalar(f) || is_enum(f)) {
			bool js_type = is_scalar(f) && is_js_type_field_option(f);
			Node *place = js_type && O.force_long == LONG_BIGINT ? call(ID("BigInt"), tp_clone(mp), NULL) : mp;
			Node *cond = tp_sub(fc, not_default_check(f, m->options, mp));
			dts_add(&stmts, dts_if(cond, blockl(write_stmts(fc, &ws, place, NULL)), NULL));
		} else {
			addl(&stmts, write_stmts(fc, &ws, mp, NULL));
		}
	}
	if (O.unknown_fields) {
		TpCode *uc = tp_add(c, tp_code(NULL));
		Node *pat = dts_new(N_ARRAY_PAT);
		dts_add(&pat->list, dts_binding("key", NULL, false));
		dts_add(&pat->list, dts_binding("values", NULL, false));
		Node *decl = dts_var_decl(VAR_CONST);
		dts_add(&decl->list, dts_declarator(pat, NULL));
		Node *vdecl = dts_var_decl(VAR_CONST);
		dts_add(&vdecl->list, dts_declarator(dts_binding("value", NULL, false), NULL));
		Node *inner = dts_for_of(vdecl, ID("values"), block(ES(mcall(mcall(ID("writer"), "uint32", ID("tag"), NULL), "raw", ID("value"), NULL)), NULL),
					 false);
		Node *entries = mcall(MEM(tp_co(uc, C.u->global_this), "Object"), "entries", MEM(ID("message"), "_unknownFields"), NULL);
		Node *outer = dts_for_of(decl, entries, block(const_("tag", NULL, call(ID("parseInt"), ID("key"), NUM("10"), NULL)), inner, NULL), false);
		dts_add(&stmts, dts_if(BIN(NOTEQEQ, MEM(ID("message"), "_unknownFields"), UNDEF()), block(outer, NULL), NULL));
	}
	dts_add(&stmts, RET(ID("writer")));
	c->node = method("encode", NULL, params, bw3, blockl(stmts));
	return c;
}

UNUSED static TpCode *generate_empty_encode(const char *full_name)
{
	TpCode *c = tp_code(NULL);
	TpImport *bw = tp_imp("BinaryWriter@@bufbuild/protobuf/wire");
	Node *t1 = tp_tref(c, bw, (NodeList){0});
	Node *t2 = tp_ref(c, bw);
	Node *t3 = tp_tref(c, bw, (NodeList){0});
	NodeList params = L(param("_", tref(full_name, NULL), false, NULL), param("writer", t1, false, newx(t2, NULL)), NULL);
	Node *t = dts_throw(newx(ID("Error"), sq(xcat("encode not generated for ", full_name)), NULL));
	c->node = method("encode", NULL, params, t3, block(t, NULL));
	return c;
}
