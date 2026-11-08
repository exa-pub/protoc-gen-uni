/* Port of ts-proto v2.18.1 src/enums.ts.
 *
 * ts-proto: Copyright (c) Stephen Haberman, Apache-2.0 License. */

/* ToNumber(str) is not NaN (isNumeric of enums.ts) */
static bool js_is_numeric(const char *s)
{
	double d = js_string_to_number(str_c(s));
	return d == d;
}

static const char *without_enum_prefix(const char *value_name, const char *enum_name)
{
	return js_replace_first(value_name, xcat(camel_to_snake(enum_name), "_"), "");
}

/* areAnyMembersNumericWithoutPrefix, computed once per enum (upstream: per member) */
typedef struct {
	TpdEnum *e;
	bool any_numeric;
} EnumPrefixInfo;
static EnumPrefixInfo enum_prefix_cache;

static const char *get_member_name(TpdEnum *e, TpdEnumValue *v)
{
	if (!O.remove_enum_prefix)
		return v->name;
	if (enum_prefix_cache.e != e) {
		bool any = false;
		for (int i = 0; i < e->nvalue && !any; i++) {
			const char *w = without_enum_prefix(e->value[i]->name, e->name);
			/* isNumeric(withoutPrefix[0]): Number(undefined) is NaN */
			any = js_is_numeric(w) || (*w && js_is_numeric(xstrndup(w, 1)));
		}
		enum_prefix_cache = (EnumPrefixInfo){e, any};
	}
	if (!enum_prefix_cache.any_numeric)
		return without_enum_prefix(v->name, e->name);
	return v->name;
}

typedef struct {
	bool present;
	const char *name, *original_name;
} UnrecognizedEnum;

/* `case <test>:` with statements */
static Node *case_(Node *test, NodeList cons)
{
	Node *c = dts_case(test);
	c->list = cons;
	return c;
}

static Node *throw_unrecognized(TpCode *c, const char *full_name)
{
	Node *msg = BIN(ADD, BIN(ADD, DQ("Unrecognized enum value "), ID("object")), dq(xcat(" for enum ", full_name)));
	return dts_throw(newx(dts_member(tp_co(c, C.u->global_this), "Error"), msg, NULL));
}

static Node *enum_value_num(void)
{
	/* options.unrecognizedEnumValue printed as a number */
	return num_text(js_value_to_string(opt(&O.opts, "unrecognizedEnumValue")));
}

static TpCode *generate_enum_from_json(const char *full_name, TpdEnum *e, UnrecognizedEnum *u)
{
	TpCode *c = tp_code(NULL);
	const char *fn = xcat(uncapitalize(full_name), "FromJSON");
	TpDef *d = tp_def(fn);
	Node *sw = dts_switch(ID("object"));
	for (int i = 0; i < e->nvalue; i++) {
		TpdEnumValue *v = e->value[i];
		dts_add(&sw->list, case_(numi(v->number), (NodeList){0}));
		dts_add(&sw->list, case_(dq(v->name), L(RET(dts_member(ID(full_name), get_member_name(e, v))), NULL)));
	}
	const char *uname = opt_str("unrecognizedEnumName");
	if (opt_true(&O.opts, "unrecognizedEnum")) {
		if (!u->present) {
			dts_add(&sw->list, case_(enum_value_num(), (NodeList){0}));
			dts_add(&sw->list, case_(dq(uname), (NodeList){0}));
			dts_add(&sw->list, case_(NULL, L(RET(dts_member(ID(full_name), uname)), NULL)));
		} else {
			dts_add(&sw->list, case_(NULL, L(RET(dts_member(ID(full_name), u->name)), NULL)));
		}
	} else {
		dts_add(&sw->list, case_(NULL, L(throw_unrecognized(c, full_name), NULL)));
	}
	Node *f = dts_function(L(param("object", KW("any"), false, NULL), NULL), NULL, tref(full_name, NULL), block(sw, NULL), 0);
	c->node = dts_export(dts_fn_decl(tp_defn(c, d), f));
	return c;
}

static TpCode *generate_enum_to_json(const char *full_name, TpdEnum *e, UnrecognizedEnum *u)
{
	TpCode *c = tp_code(NULL);
	const char *fn = xcat(uncapitalize(full_name), "ToJSON");
	TpDef *d = tp_def(fn);
	Node *sw = dts_switch(ID("object"));
	for (int i = 0; i < e->nvalue; i++) {
		TpdEnumValue *v = e->value[i];
		Node *val = O.use_numeric_enum_for_json ? numi(v->number) : dq(v->name);
		dts_add(&sw->list, case_(dts_member(ID(full_name), get_member_name(e, v)), L(RET(val), NULL)));
	}
	const char *uname = opt_str("unrecognizedEnumName");
	if (opt_true(&O.opts, "unrecognizedEnum")) {
		if (!u->present) {
			dts_add(&sw->list, case_(dts_member(ID(full_name), uname), (NodeList){0}));
			Node *val = O.use_numeric_enum_for_json ? enum_value_num() : dq(uname);
			dts_add(&sw->list, case_(NULL, L(RET(val), NULL)));
		} else {
			Node *val = O.use_numeric_enum_for_json ? enum_value_num() : dq(u->original_name);
			dts_add(&sw->list, case_(NULL, L(RET(val), NULL)));
		}
	} else {
		dts_add(&sw->list, case_(NULL, L(throw_unrecognized(c, full_name), NULL)));
	}
	Node *f = dts_function(L(param("object", tref(full_name, NULL), false, NULL), NULL), NULL,
			       KW(O.use_numeric_enum_for_json ? "number" : "string"), block(sw, NULL), 0);
	c->node = dts_export(dts_fn_decl(tp_defn(c, d), f));
	return c;
}

static TpCode *generate_enum_to_number(const char *full_name, TpdEnum *e, UnrecognizedEnum *u)
{
	TpCode *c = tp_code(NULL);
	const char *fn = xcat(uncapitalize(full_name), "ToNumber");
	TpDef *d = tp_def(fn);
	Node *sw = dts_switch(ID("object"));
	for (int i = 0; i < e->nvalue; i++) {
		TpdEnumValue *v = e->value[i];
		dts_add(&sw->list, case_(dts_member(ID(full_name), get_member_name(e, v)), L(RET(numi(v->number)), NULL)));
	}
	if (opt_true(&O.opts, "unrecognizedEnum")) {
		if (!u->present)
			dts_add(&sw->list, case_(dts_member(ID(full_name), opt_str("unrecognizedEnumName")), (NodeList){0}));
		dts_add(&sw->list, case_(NULL, L(RET(enum_value_num()), NULL)));
	} else {
		dts_add(&sw->list, case_(NULL, L(throw_unrecognized(c, full_name), NULL)));
	}
	Node *f = dts_function(L(param("object", tref(full_name, NULL), false, NULL), NULL), NULL, KW("number"), block(sw, NULL), 0);
	c->node = dts_export(dts_fn_decl(tp_defn(c, d), f));
	return c;
}

static void enum_json_methods(TpCode *c, const char *full_name, TpdEnum *e, UnrecognizedEnum *u)
{
	JsVal *oj = opt(&O.opts, "outputJsonMethods");
	bool oj_from = O.output_json_true || (oj->t == JS_STRING && str_eqc(oj->s, "from-only"));
	bool oj_to = O.output_json_true || (oj->t == JS_STRING && str_eqc(oj->s, "to-only"));
	bool encode = opt_true(&O.opts, "outputEncodeMethods");
	if (oj_from || (O.string_enums && encode))
		dts_add(&c->list, BLANK(tp_sub(c, generate_enum_from_json(full_name, e, u))));
	if (oj_to)
		dts_add(&c->list, BLANK(tp_sub(c, generate_enum_to_json(full_name, e, u))));
	if (O.string_enums && encode)
		dts_add(&c->list, BLANK(tp_sub(c, generate_enum_to_number(full_name, e, u))));
}

/* generateEnum with enumsAsLiterals: `export const X = { ... } as const`, the type and the namespace */
static TpCode *generate_enum_literals(const char *full_name, TpdEnum *e, SourceInfo *si)
{
	TpCode *c = tp_code(NULL);
	UnrecognizedEnum u = {0};
	NodeComment *cm = jsdoc_comment(si->self, e->options && e->options->deprecated, NULL);
	TpDef *d = tp_def(full_name);
	TpCode *head = tp_add(c, tp_code(NULL));
	Node *name = tp_defn(head, d);
	Node *o = dts_obj();
	JsVal *unrec = opt(&O.opts, "unrecognizedEnumValue");
	const char **members = xalloc(sizeof(char *) * (size_t)(e->nvalue + 1));
	for (int i = 0; i < e->nvalue; i++) {
		TpdEnumValue *v = e->value[i];
		const char *member = get_member_name(e, v);
		members[i] = member;
		if (unrec->t == JS_NUMBER && v->number == unrec->num)
			u = (UnrecognizedEnum){true, member, v->name};
		NodeComment *vc = jsdoc_comment(si_lookup(si, ENUM_VALUE, i), v->options && v->options->deprecated, xcat(member, " - "));
		Node *p = NL(kv(member, O.string_enums ? dq(v->name) : numi(v->number)), LINE);
		add_comment(p, vc, 0);
		dts_obj_add(o, p);
	}
	const char *uname = NULL;
	if (opt_true(&O.opts, "unrecognizedEnum") && !u.present) {
		uname = opt_str("unrecognizedEnumName");
		dts_obj_add(o, NL(kv(uname, O.string_enums ? dq(uname) : enum_value_num()), LINE));
	}
	o->hints |= H_CLOSE_NL;
	if (o->list.n)
		o->hints |= H_TRAILING_COMMA;
	Node *n = dts_new(N_TS_CONST_ASSERTION);
	n->a = o;
	Node *decl = dts_export(HINT(dts_var(VAR_CONST, dts_bindingn(name->text, NULL, false), n), H_NO_SEMI));
	add_comment(decl, cm, 0);
	dts_add(&c->list, decl);
	/* export type X = typeof X[keyof typeof X] */
	TpCode *tc = tp_add(c, tp_code(NULL));
	Node *t1 = tp_defn(tc, d), *t2 = tp_defn(tc, d), *t3 = tp_defn(tc, d);
	Node *ta = dts_type_alias(t1, NULL, dts_tindexed(dts_tquery(t2), dts_tkeyof(dts_tquery(t3))));
	dts_add(&c->list, BLANK(HINT(dts_export(ta), H_NO_SEMI)));
	/* export namespace X { export type A = typeof X.A; } */
	TpCode *nc = tp_add(c, tp_code(NULL));
	Node *ns = dts_namespace(tp_defn(nc, d));
	for (int i = 0; i <= e->nvalue; i++) {
		const char *m = i < e->nvalue ? members[i] : uname;
		if (!m)
			break;
		TpCode *mc = tp_add(c, tp_code(NULL));
		Node *q = dts_new(N_TS_QUALIFIED_NAME);
		q->a = tp_defn(mc, d);
		q->b = dts_id(m);
		dts_block_add(ns->b, dts_export(dts_type_alias(ID(m), NULL, dts_tquery(q))));
	}
	tp_add(c, tp_code(NULL));
	if (!e->nvalue && !uname)
		ns->b->hints |= H_CLOSE_NL;
	dts_add(&c->list, BLANK(dts_export(ns)));
	enum_json_methods(c, full_name, e, &u);
	return c;
}

/* generateEnum: the declarations in c->list */
static TpCode *generate_enum(const char *full_name, TpdEnum *e, SourceInfo *si)
{
	TpCode *c = tp_code(NULL);
	UnrecognizedEnum u = {0};
	if (opt_true(&O.opts, "enumsAsLiterals"))
		return generate_enum_literals(full_name, e, si);
	NodeComment *cm = jsdoc_comment(si->self, e->options && e->options->deprecated, NULL);
	TpDef *d = tp_def(full_name);
	TpCode *head = tp_add(c, tp_code(NULL));
	Node *en = dts_enum(tp_defn(head, d), opt_true(&O.opts, "constEnums"));
	JsVal *unrec = opt(&O.opts, "unrecognizedEnumValue");
	for (int i = 0; i < e->nvalue; i++) {
		TpdEnumValue *v = e->value[i];
		const char *member = get_member_name(e, v);
		if (unrec->t == JS_NUMBER && v->number == unrec->num)
			u = (UnrecognizedEnum){true, member, v->name};
		NodeComment *vc = jsdoc_comment(si_lookup(si, ENUM_VALUE, i), v->options && v->options->deprecated, xcat(member, " - "));
		dts_enum_add(en, dts_id(member), O.string_enums ? dq(v->name) : numi(v->number));
		add_comment(en->list.v[en->list.n - 1], vc, 0);
	}
	if (opt_true(&O.opts, "unrecognizedEnum") && !u.present) {
		const char *uname = opt_str("unrecognizedEnumName");
		dts_enum_add(en, dts_id(uname), O.string_enums ? dq(uname) : enum_value_num());
	}
	if (en->list.n)
		en->hints |= H_TRAILING_COMMA;
	else
		en->hints |= H_CLOSE_NL;
	Node *decl = dts_export(en);
	add_comment(decl, cm, 0);
	dts_add(&c->list, decl);
	enum_json_methods(c, full_name, e, &u);
	return c;
}
