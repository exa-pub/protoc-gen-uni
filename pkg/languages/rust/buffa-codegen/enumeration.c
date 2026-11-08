/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Port of buffa-codegen 0.9.2 src/enumeration.rs: enum types, aliases, idiomatic CamelCase
 * aliases, Default, the serde impls (json) and impl Enumeration. */

static PpNode *proto_elem_json_impl(const char *ty);

/* fn <name><'de, D: ::serde::Deserializer<'de>>(...) helpers shared with message JSON */
static PpNode *sig_generic(const char *name, PpNode *ret, PpNode *generics)
{
	PpNode *sig = pp_sig(name, ret);
	pp_set_generics(sig, generics);
	return sig;
}

static PpNode *generics_tp(const char *lifetime, const char *ident, const char *bound)
{
	PpNode *g = pp_generics();
	if (lifetime)
		pp_push(g, pp_lifetime_param(lifetime));
	if (ident)
		pp_push(g, pp_type_param(ident, bound ? pp_bounds(pp_trait_bound(pp_path(bound), false), NULL) : NULL, NULL));
	return g;
}

static PpNode *ty_result(PpNode *ok, PpNode *err) { return ty_g2("::core::result::Result", ok, err); }

/* fn visit_<k><E: ::serde::de::Error>(self, v: <ty>) -> ::core::result::Result<name, E> */
static PpNode *visit_sig(const char *name, const char *vty, const char *ret)
{
	PpNode *sig = sig_generic(name, ty_result(pp_ty(ret), pp_ty("E")), generics_tp(NULL, "E", "::serde::de::Error"));
	pp_push(sig, pp_receiver(false, false, NULL));
	if (vty)
		pp_push(sig, arg("v", pp_ty_path(pp_path(vty))));
	return sig;
}

static PpNode *blk(PpNode *e) { return pp_e_block(pp_block(s_expr(e), NULL)); }

static PpNode *serde_custom_format(const char *fmt)
{
	PpNode *m = pp_macro(pp_path("::buffa::alloc::format"), PP_DELIM_PAREN);
	pp_push(m, pp_tt_lit(pm2_string(str_c(fmt))));
	return e_call1("::serde::de::Error::custom", pp_e_macro(m));
}

static PpNode *visit_int_body(const char *name)
{
	PpNode *closure = pp_e_closure(blk(serde_custom_format("enum value {v} out of i32 range")), false);
	pp_push(closure, pp_p_wild());
	PpNode *v32 = pp_e_try(e_meth1(e_call1("i32::try_from", pp_e("v")), "map_err", closure));
	PpNode *closure2 = pp_e_closure(blk(serde_custom_format("unknown enum value {v32}")), false);
	PpNode *from = pp_e_call(e_qpath(pp_ty(name), "::buffa::Enumeration", "from_i32"), pp_e("v32"), NULL);
	return pp_block(s_let("v32", false, v32), s_expr(e_meth1(from, "ok_or_else", closure2)), NULL);
}

/* generate_enum_serde */
static void generate_enum_serde(Vec *out, const char *name)
{
	PpNode *imp = pp_item_impl(pp_path("::serde::Serialize"), pp_ty(name));
	PpNode *sig = sig_generic("serialize", ty_result(pp_ty("S::Ok"), pp_ty("S::Error")),
				  generics_tp(NULL, "S", "::serde::Serializer"));
	pp_push(sig, pp_receiver(true, false, NULL));
	pp_push(sig, arg("s", pp_ty("S")));
	pp_push(imp, pp_item_fn(0, sig,
				pp_block(s_expr(e_meth1(pp_e("s"), "serialize_str",
							e_call1("::buffa::Enumeration::proto_name", pp_e("self")))),
					 NULL)));
	vec_push(out, imp);

	imp = pp_item_impl(pp_path_args(pp_path("::serde::Deserialize"), pp_angle(pp_lifetime("'de"), NULL)), pp_ty(name));
	pp_set_generics(imp, generics_tp("'de", NULL, NULL));
	sig = sig_generic("deserialize", ty_result(pp_ty("Self"), pp_ty("D::Error")), pp_generics());
	pp_push(pp_generics_of(sig), pp_type_param("D", pp_bounds(pp_trait_bound(pp_path_args(pp_path("::serde::Deserializer"), pp_angle(pp_lifetime("'de"), NULL)), false), NULL), NULL));
	pp_push(sig, arg("d", pp_ty("D")));
	PpNode *body = pp_block(NULL);
	pp_push(body, pp_s_item(pp_item_struct(0, "_V", 0)));
	PpNode *vis = pp_item_impl(pp_path_args(pp_path("::serde::de::Visitor"), pp_angle(pp_lifetime("'_"), NULL)), pp_ty("_V"));
	pp_push(vis, pp_impl_type(0, "Value", pp_ty(name)));
	PpNode *esig = pp_sig("expecting", pp_ty("::core::fmt::Result"));
	pp_push(esig, pp_receiver(true, false, NULL));
	pp_push(esig, arg("f", ty_ref_mut(pp_ty_gen("::core::fmt::Formatter", pp_lifetime("'_"), NULL))));
	PpNode *concat = pp_macro(pp_path("concat"), PP_DELIM_PAREN);
	pp_push(concat, pp_tt_lit("\"a string, integer, or null for \""));
	pp_push(concat, pp_tt_punct(',', false));
	pp_push(concat, pp_tt_ident("stringify"));
	pp_push(concat, pp_tt_punct('!', false));
	PpNode *g = pp_tt_group(PP_DELIM_PAREN);
	pp_push(g, pp_tt_ident(name));
	pp_push(concat, g);
	pp_push(vis, pp_item_fn(0, esig, pp_block(s_expr(e_meth1(pp_e("f"), "write_str", pp_e_macro(concat))), NULL)));

	PpNode *unk = pp_e_closure(blk(e_call2("::serde::de::Error::unknown_variant", pp_e("v"), pp_e_ref(false, pp_e_array(NULL)))), false);
	PpNode *from_name = pp_e_call(e_qpath(pp_ty(name), "::buffa::Enumeration", "from_proto_name"), pp_e("v"), NULL);
	pp_push(vis, pp_item_fn(0, visit_sig("visit_str", NULL, name), pp_block(s_expr(e_meth1(from_name, "ok_or_else", unk)), NULL)));
	/* visit_str takes v: &str */
	{
		PpNode *f = vis->list.v[vis->list.n - 1];
		pp_push(f->a, arg("v", ty_ref(pp_ty("str"))));
	}
	pp_push(vis, pp_item_fn(0, visit_sig("visit_i64", "i64", name), visit_int_body(name)));
	pp_push(vis, pp_item_fn(0, visit_sig("visit_u64", "u64", name), visit_int_body(name)));
	pp_push(vis, pp_item_fn(0, visit_sig("visit_unit", NULL, name),
				pp_block(s_expr(e_call1("::core::result::Result::Ok",
							pp_e_call(pp_e("::core::default::Default::default"), NULL))),
					 NULL)));
	pp_push(body, pp_s_item(vis));
	pp_push(body, s_expr(e_meth1(pp_e("d"), "deserialize_any", pp_e("_V"))));
	pp_push(imp, pp_item_fn(0, sig, body));
	vec_push(out, imp);
	vec_push(out, proto_elem_json_impl(name));
}

/* impl ::buffa::json_helpers::ProtoElemJson for T (enums and messages) */
static PpNode *proto_elem_json_impl(const char *ty)
{
	PpNode *imp = pp_item_impl(pp_path("::buffa::json_helpers::ProtoElemJson"), pp_ty(ty));
	PpNode *sig = sig_generic("serialize_proto_json", ty_result(pp_ty("S::Ok"), pp_ty("S::Error")),
				  generics_tp(NULL, "S", "::serde::Serializer"));
	pp_push(sig, arg("v", ty_ref(pp_ty("Self"))));
	pp_push(sig, arg("s", pp_ty("S")));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(e_call2("::serde::Serialize::serialize", pp_e("v"), pp_e("s"))), NULL)));
	sig = sig_generic("deserialize_proto_json", ty_result(pp_ty("Self"), pp_ty("D::Error")), generics_tp("'de", NULL, NULL));
	pp_push(pp_generics_of(sig), pp_type_param("D", pp_bounds(pp_trait_bound(pp_path_args(pp_path("::serde::Deserializer"), pp_angle(pp_lifetime("'de"), NULL)), false), NULL), NULL));
	pp_push(sig, arg("d", pp_ty("D")));
	pp_push(imp, pp_item_fn(0, sig,
				pp_block(s_expr(pp_e_call(e_qpath(pp_ty("Self"), "::serde::Deserialize", "deserialize"),
							  pp_e("d"), NULL)),
					 NULL)));
	return imp;
}

typedef struct {
	const char *name, *target, *own;
} ValueRecord;

/* idiomatic_aliases: the alias consts, or a doc note when suppressed */
static void idiomatic_aliases(const char *simple, ValueRecord *rec, int n, Vec *consts, Vec *note)
{
	if (!n)
		return;
	char *prefix = xcat(to_shouty_snake_case(simple), "_");
	bool strip = true;
	for (int i = 0; i < n && strip; i++) {
		if (!has_prefix(rec[i].name, prefix)) {
			strip = false;
			break;
		}
		char *c = to_upper_camel_case(rec[i].name + strlen(prefix));
		strip = *c && !(*c >= '0' && *c <= '9');
	}
	Map existing = {0}, owner = {0}, buckets = {0};
	Vec bucket_keys = {0};
	for (int i = 0; i < n; i++) {
		map_put(&existing, rec[i].own, (void *)1);
		map_put(&owner, rec[i].own, (void *)rec[i].name);
	}
	const char **camel = xalloc(sizeof *camel * (size_t)n);
	Vec invalid = {0};
	for (int i = 0; i < n; i++) {
		const char *base = strip ? rec[i].name + strlen(prefix) : rec[i].name;
		camel[i] = to_upper_camel_case(base);
		if (!*camel[i] || (*camel[i] >= '0' && *camel[i] <= '9')) {
			vec_push(&invalid, (void *)rec[i].name);
			continue;
		}
		char *esc = make_field_ident(camel[i]);
		Vec *b = map_get(&buckets, esc);
		if (!b) {
			b = xalloc(sizeof *b);
			map_put(&buckets, esc, b);
			vec_push(&bucket_keys, esc);
		}
		const char *ex = map_get(&owner, esc);
		if (ex && vec_index(b, ex) < 0) {
			bool dup = false;
			for (int k = 0; k < b->n; k++)
				dup |= str_eq(b->v[k], ex);
			if (!dup)
				vec_push(b, (void *)ex);
		}
		bool dup = false;
		for (int k = 0; k < b->n; k++)
			dup |= str_eq(b->v[k], rec[i].name);
		if (!dup)
			vec_push(b, (void *)rec[i].name);
	}
	bool conflict = false;
	for (int i = 0; i < bucket_keys.n; i++)
		conflict |= ((Vec *)map_get(&buckets, bucket_keys.v[i]))->n > 1;
	if (!conflict && !invalid.n) {
		for (int i = 0; i < n; i++) {
			char *esc = make_field_ident(camel[i]);
			if (map_has(&existing, esc))
				continue;
			const char *doc = has_prefix(rec[i].target, "r#")
						  ? xcat("Idiomatic alias for `", rec[i].target + 2, "`; `Debug` prints the variant name.")
						  : xcat("Idiomatic alias for [`Self::", rec[i].target, "`]; `Debug` prints the variant name.");
			PpNode *c = pp_item_const(PP_VIS_PUB, esc, pp_ty("Self"), pp_e(xcat("Self::", rec[i].target)));
			pp_push_attr(c, doc_attr(doc));
			pp_push_attr(c, attr_src("allow", "non_upper_case_globals"));
			vec_push(consts, c);
		}
		return;
	}
	/* suppressed: BTreeMap order of the buckets, BTreeSet order of their members */
	if (bucket_keys.n > 1)
		qsort(bucket_keys.v, (size_t)bucket_keys.n, sizeof *bucket_keys.v, str_cmp_qsort);
	Buf b = {0};
	buf_adds(&b, "Idiomatic CamelCase aliases are not generated for this enum: two or more proto values collide "
		     "after conversion (or would be invalid identifiers). Use the `SHOUTY_SNAKE_CASE` variants directly. "
		     "Collisions:\n");
	for (int i = 0; i < bucket_keys.n; i++) {
		Vec *m = map_get(&buckets, bucket_keys.v[i]);
		if (m->n < 2)
			continue;
		if (m->n > 1)
			qsort(m->v, (size_t)m->n, sizeof *m->v, str_cmp_qsort);
		buf_adds(&b, "- ");
		for (int k = 0; k < m->n; k++)
			buf_fmt(&b, "%s`%s`", k ? ", " : "", (char *)m->v[k]);
		buf_fmt(&b, " \xe2\x86\x92 `%s`\n", (char *)bucket_keys.v[i]);
	}
	if (invalid.n > 1)
		qsort(invalid.v, (size_t)invalid.n, sizeof *invalid.v, str_cmp_qsort);
	for (int i = 0; i < invalid.n; i++)
		buf_fmt(&b, "- `%s` produces an invalid identifier\n", (char *)invalid.v[i]);
	vec_push(note, doc_attr(buf_str(&b)));
}

static PpNode *some_self(const char *variant) { return e_some(pp_e(xcat("Self::", variant))); }

/* generate_enum */
static void generate_enum(Vec *out, const PbMsg *e, const char *rust_name, const char *proto_fqn, Feat features)
{
	int nv = pb_count(e, EDP_VALUE);
	Map seen = {0};
	PpNode *en = pp_item_enum(PP_VIS_PUB, rust_name);
	Vec alias_consts = {0};
	PpNode *from_i32 = pp_e_match(pp_e("value"));
	PpNode *from_name = pp_e_match(pp_e("name"));
	PpNode *proto_name = pp_e_match(pp_e("self"));
	PpNode *values = pp_e_array(NULL);
	const char *first = NULL;
	ValueRecord *rec = xalloc(sizeof *rec * (size_t)(nv + 1));
	int nrec = 0;
	for (int i = 0; i < nv; i++) {
		const PbMsg *v = pb_msg_at(e, EDP_VALUE, i);
		if (!pb_has(v, EVDP_NAME))
			fail("missing required field: enum_value.name");
		if (!pb_has(v, EVDP_NUMBER))
			fail("missing required field: enum_value.number");
		const char *value_name = dname(v);
		int number = (int)pb_int(v, EVDP_NUMBER, 0);
		char *ident = make_field_ident(value_name);
		Vec doc = {0};
		doc_attrs(&doc, ctx_comment(xcat(proto_fqn, ".", value_name)), proto_fqn);
		char *key = xfmt("%d", number);
		const char *primary = map_get(&seen, key);
		if (primary) {
			char *pid = make_field_ident(primary);
			PpNode *c = pp_item_const(PP_VIS_PUB, ident, pp_ty("Self"), pp_e(xcat("Self::", pid)));
			attrs_all(c, &doc);
			pp_push_attr(c, attr_src("allow", "non_upper_case_globals"));
			vec_push(&alias_consts, c);
			pp_push(from_name, pp_arm(pp_p_lit(lit_str(value_name)), NULL, some_self(pid)));
			if (CFG->idiomatic_enum_aliases)
				rec[nrec++] = (ValueRecord){value_name, pid, ident};
		} else {
			map_put(&seen, key, (void *)value_name);
			if (!first)
				first = ident;
			PpNode *var = pp_variant(ident, 0, lit_i32(number));
			attrs_all(var, &doc);
			pp_push(en, var);
			pp_push(from_i32, pp_arm(pp_p_lit(lit_i32(number)), NULL, some_self(ident)));
			pp_push(from_name, pp_arm(pp_p_lit(lit_str(value_name)), NULL, some_self(ident)));
			pp_push(proto_name, pp_arm(pp_p_path(pp_path(xcat("Self::", ident))), NULL, lit_str(value_name)));
			if (CFG->idiomatic_enum_aliases)
				rec[nrec++] = (ValueRecord){value_name, ident, ident};
			pp_push(values, pp_e(xcat("Self::", ident)));
		}
	}
	Vec idiomatic = {0}, note = {0};
	idiomatic_aliases(pb_has(e, EDP_NAME) ? dname(e) : rust_name, rec, nrec, &idiomatic, &note);

	Vec doc = {0};
	doc_attrs(&doc, ctx_comment(proto_fqn), proto_fqn);
	vcat(&doc, &note);
	attrs_all(en, &doc);
	pp_push_attr(en, attr_src("allow", "non_camel_case_types"));
	pp_push_attr(en, attr_src("derive", "Clone, Copy, PartialEq, Eq, Hash, Debug"));
	if (CFG->generate_arbitrary)
		pp_push_attr(en, attr_src("cfg_attr", "feature = \"arbitrary\", derive(::arbitrary::Arbitrary)"));
	pp_push_attr(en, attr_src("repr", "i32"));
	vec_push(out, en);

	if (alias_consts.n || idiomatic.n) {
		PpNode *imp = pp_item_impl(NULL, pp_ty(rust_name));
		push_all(imp, &alias_consts);
		push_all(imp, &idiomatic);
		vec_push(out, imp);
	}
	if (first) {
		PpNode *imp = pp_item_impl(pp_path("::core::default::Default"), pp_ty(rust_name));
		PpNode *sig = pp_sig("default", pp_ty("Self"));
		pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(pp_e(xcat("Self::", first))), NULL)));
		vec_push(out, imp);
	}
	if (CFG->generate_json) {
		Vec serde = {0};
		generate_enum_serde(&serde, rust_name);
		cfg_const_block(out, &serde, gates().json);
	}

	PpNode *imp = pp_item_impl(pp_path("::buffa::Enumeration"), pp_ty(rust_name));
	pp_push(from_i32, pp_arm(pp_p_wild(), NULL, e_none()));
	PpNode *sig = pp_sig("from_i32", ty_g1(OPTION_PATH, pp_ty("Self")));
	pp_push(sig, arg("value", pp_ty("i32")));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(from_i32), NULL)));
	pp_push(imp, fn_self("to_i32", false, pp_ty("i32"), pp_block(s_expr(pp_e_cast(pp_e_unary("*", pp_e("self")), pp_ty("i32"))), NULL)));
	pp_push(imp, fn_self("proto_name", false, ty_static_str(), pp_block(s_expr(proto_name), NULL)));
	pp_push(from_name, pp_arm(pp_p_wild(), NULL, e_none()));
	sig = pp_sig("from_proto_name", ty_g1(OPTION_PATH, pp_ty("Self")));
	pp_push(sig, arg("name", ty_ref(pp_ty("str"))));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(from_name), NULL)));
	sig = pp_sig("values", pp_ty_ref("'static", false, pp_ty_slice(pp_ty("Self"))));
	pp_push(imp, pp_item_fn(0, sig, pp_block(s_expr(pp_e_ref(false, values)), NULL)));
	vec_push(out, imp);

	if (CFG->generate_reflection && CFG->generate_reflection_vtable && features.enum_type == PROTO_ENUM_TYPE_CLOSED) {
		PpNode *r = pp_item_impl(pp_path("::buffa_descriptor::reflect::ReflectElement"), pp_ty(rust_name));
		PpNode *s = pp_sig("as_value_ref", pp_ty_gen("::buffa_descriptor::reflect::ValueRef", pp_lifetime("'_"), NULL));
		pp_push(s, pp_receiver(true, false, NULL));
		pp_push(r, pp_item_fn(0, s,
				      pp_block(s_expr(e_call1("::buffa_descriptor::reflect::ValueRef::EnumNumber",
							      e_call1("::buffa::Enumeration::to_i32", pp_e("self")))),
					       NULL)));
		Vec one = {0};
		vec_push(&one, r);
		cfg_block(out, &one, gates().reflect);
	}
}
