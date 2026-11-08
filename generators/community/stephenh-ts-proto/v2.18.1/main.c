/* Port of ts-proto v2.18.1 src/main.ts: generateFile, the interfaces (generateInterfaceDeclaration,
 * generateOneofProperty) and the base instances (generateBaseInstanceFactory). The message
 * methods are in main_binary.c (encode, decode) and main_json.c (fromJSON, toJSON, fromPartial).
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* ---- the top-level chunks of a file: joinCode(chunks, { on: "\n\n" }) ---- */

struct FileOut {
	Vec chunks; /* TpCode*: the root code's placeholders */
	NodeList body;
	NodeComment pend[8]; /* comment-only chunks waiting for the next statement */
	int npend;
	int nchunks; /* upstream's chunks.length, comment chunks included */
};

static void out_comment(FileOut *o, const NodeComment *cm)
{
	if (!cm)
		return;
	o->nchunks++;
	if (o->npend == (int)(sizeof o->pend / sizeof o->pend[0]))
		fail("too many comments");
	o->pend[o->npend++] = *cm;
}

static void out_stmt(FileOut *o, Node *s, bool first_of_chunk)
{
	if (first_of_chunk) {
		if (o->npend) {
			/* the comment chunks before it, each after a blank line */
			NodeComment *v = xalloc(sizeof(NodeComment) * (size_t)(o->npend + s->ncm));
			for (int i = 0; i < o->npend; i++) {
				v[i] = o->pend[i];
				v[i].nl = i == 0 ? 0 : NL_BLANK;
			}
			for (int i = 0; i < s->ncm; i++) {
				v[o->npend + i] = s->cm[i];
				if (i == 0)
					v[o->npend].nl = NL_BLANK;
			}
			if (!s->ncm)
				s->nl_after_cm = NL_BLANK;
			s->cm = v;
			s->ncm += o->npend;
			o->npend = 0;
		}
		if (o->body.n)
			s->nl = NL_BLANK;
	}
	dts_add(&o->body, s);
}

static void out_chunk(FileOut *o, TpCode *c)
{
	if (!c)
		return;
	o->nchunks++;
	vec_push(&o->chunks, c);
	if (c->node)
		out_stmt(o, c->node, true);
	for (int i = 0; i < c->list.n; i++)
		out_stmt(o, c->list.v[i], i == 0 && !c->node);
}

/* ---- generateInterfaceDeclaration ---- */

static Node *maybe_readonly_sig(Node *sig)
{
	if (O.use_readonly_types)
		sig->flags |= F_READONLY;
	return sig;
}

/* generateOneofProperty: the property signature, added to c */
static Node *generate_oneof_property(TpCode *c, TpdMessage *m, int oneof_index, SourceInfo *si)
{
	TpCode *pc = tp_add(c, tp_code(NULL));
	NodeComment *outer = jsdoc_comment(si_lookup(si, MESSAGE_ONEOF_DECL, oneof_index), false, NULL);
	TpCode *uc = tp_add(pc, tp_code(NULL)); /* unionType */
	Node *u = dts_new(N_TS_UNION_TYPE);
	u->hints |= H_LEADING_OP;
	int nf;
	const int *fi = oneof_fields(m, oneof_index, &nf);
	for (int j = 0; j < nf; j++) {
		int i = fi[j];
		TpdField *f = m->field[i];
		NodeComment *fc = jsdoc_comment(si_lookup(si, MESSAGE_FIELD, i), false, NULL);
		const char *field_name = maybe_snake_to_camel(f->name);
		TpCode *mc = tp_add(uc, tp_code(NULL));
		Node *type = tp_sub(mc, to_type_name(m, f, false));
		Node *lit = dts_ttype_lit();
		dts_ttype_lit_add(lit, HINT(maybe_readonly_sig(dts_prop_sig(dts_id("$case"), dts_tlit(sq(field_name)), false, false)), H_SEP_COMMA));
		dts_ttype_lit_add(lit, HINT(maybe_readonly_sig(dts_prop_sig(dts_id(oneof_value_name(field_name)), type, false, false)), H_NO_SEMI));
		if (fc) {
			lit->nl = NL_SAME;
			dts_comment(lit, STR_LIT("// "), false, 0);
			dts_comment(lit, fc->text, true, NL_LINE);
			lit->nl_after_cm = NL_LINE;
		}
		dts_add(&u->list, lit);
	}
	dts_add(&u->list, null_or_undefined_type());
	const char *name = maybe_snake_to_camel(m->oneof_decl[oneof_index]->name);
	Node *sig = maybe_readonly_sig(dts_prop_sig(dts_id(name), u, !O.uo_none, false));
	sig->hints |= H_SEP_COMMA;
	add_comment(sig, outer, 0);
	return sig;
}

static TpCode *generate_interface_declaration(const char *full_name, TpdMessage *m, SourceInfo *si, const char *full_type_name)
{
	TpCode *c = tp_code(NULL);
	NodeComment *cm = jsdoc_comment(si->self, m->options && m->options->deprecated, NULL);
	TpCode *head = tp_add(c, tp_code(NULL));
	Node *i = dts_interface(tp_defn(head, tp_def(full_name)), NULL);
	if (O.add_type_to_messages) {
		Node *t = dts_prop_sig(dts_id("$type"), dts_tlit(sq(full_type_name)), opt_is(&O.opts, "outputTypeAnnotations", "optional"), false);
		dts_interface_add(i, HINT(t, H_SEP_COMMA));
	}
	bool *processed = xalloc(sizeof(bool) * (size_t)(m->noneof_decl + 1));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		if (is_within_one_of_that_should_be_union(f)) {
			int oi = f->oneof_index;
			if (oi < 0 || oi >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading 'name')");
			if (!processed[oi]) {
				processed[oi] = true;
				dts_interface_add(i, generate_oneof_property(c, m, oi, si));
			}
			continue;
		}
		NodeComment *fc = jsdoc_comment(si_lookup(si, MESSAGE_FIELD, k), f->options && f->options->deprecated, NULL);
		bool optional = is_optional_property(f, m->options, C.is_proto3);
		TpCode *fcode = tp_add(c, tp_code(NULL));
		Node *type = tp_sub(fcode, to_type_name(m, f, optional));
		Node *sig = maybe_readonly_sig(dts_prop_sig(safe_key(get_field_name(f)), type, optional, false));
		sig->hints |= H_SEP_COMMA;
		add_comment(sig, fc, 0);
		dts_interface_add(i, sig);
	}
	if (O.unknown_fields) {
		/* _unknownFields?: {[key: number]: Uint8Array[]} | undefined, */
		Node *lit = dts_ttype_lit();
		dts_ttype_lit_add(lit, HINT(dts_index_sig("key", KW("number"), dts_tarray(tref("Uint8Array", NULL)), false), H_NO_SEMI));
		Node *sig = dts_prop_sig(dts_id("_unknownFields"), dts_tunion(lit, KW("undefined"), NULL), true, false);
		sig->hints |= H_SEP_COMMA;
		dts_interface_add(i, sig);
	}
	if (!i->c->list.n)
		i->c->hints |= H_CLOSE_NL;
	Node *decl = dts_export(i);
	add_comment(decl, cm, 0);
	c->node = decl;
	return c;
}

/* ---- generateBaseInstanceFactory ---- */

static TpCode *generate_base_instance_factory(const char *full_name, TpdMessage *m, const char *full_type_name)
{
	TpCode *c = tp_code(NULL);
	TpCode *fields = tp_add(c, tp_code(NULL)); /* joinCode(fields) */
	Node *o = dts_obj();
	bool *processed = xalloc(sizeof(bool) * (size_t)(m->noneof_decl + 1));
	for (int k = 0; k < m->nfield; k++) {
		TpdField *f = m->field[k];
		if (is_within_one_of_that_should_be_union(f)) {
			int oi = f->oneof_index;
			if (oi < 0 || oi >= m->noneof_decl)
				fail("TypeError: Cannot read properties of undefined (reading 'name')");
			if (!processed[oi]) {
				processed[oi] = true;
				dts_obj_add(o, dts_kv(safe_key(maybe_snake_to_camel(m->oneof_decl[oi]->name)), null_or_undefined()));
			}
			continue;
		}
		if (!O.initialize_fields_as_undefined && is_optional_property(f, m->options, C.is_proto3))
			continue;
		Node *val;
		if (is_within_one_of(f)) {
			val = null_or_undefined();
		} else if (is_map_type(m, f)) {
			val = should_generate_js_map_type(m, f) ? newx(ID("Map"), NULL) : dts_obj();
		} else if (is_repeated(f)) {
			val = dts_arr(NULL);
		} else {
			TpCode *fc = tp_add(fields, tp_code(NULL));
			val = tp_sub(fc, default_value(f).c);
		}
		dts_obj_add(o, dts_kv(safe_key(get_field_name(f)), val));
	}
	if (O.add_type_to_messages) {
		Node *t = kv("$type", sq(full_type_name));
		Node *o2 = dts_obj();
		dts_obj_add(o2, t);
		addl(&o2->list, o->list);
		o = o2;
	}
	if (O.unknown_fields && O.initialize_fields_as_undefined)
		dts_obj_add(o, kv("_unknownFields", dts_obj()));
	c->node = fn_decl(xcat("createBase", full_name), NULL, (NodeList){0}, tref(full_name, NULL), block(RET(o), NULL));
	return c;
}

static Node *create_base_call(const char *full_name)
{
	Node *e = call(ID(xcat("createBase", full_name)), NULL);
	if (O.use_prototype_for_defaults)
		e = dts_as(call(MEM(ID("Object"), "create"), e, NULL), tref(full_name, NULL));
	if (O.use_readonly_types)
		e = dts_as(e, KW("any"));
	return e;
}
