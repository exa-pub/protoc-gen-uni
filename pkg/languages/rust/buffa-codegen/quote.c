/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Glue for the `quote!` sites of buffa-codegen: the literals quote interpolates (proc-macro2 token
 * text: `#n` of an i32 is `Literal::i32_suffixed(n)`, `#s` of a &str is `Literal::string(s)`) and
 * shorthands for the syn nodes the quoted tokens parse to. Not an upstream file. */

static PpNode *lit_str(const char *s) { return pp_e_lit(pp_lit_str_repr(pm2_string(str_c(s)), str_c(s))); }
static PpNode *lit_strn(Str s) { return pp_e_lit(pp_lit_str_repr(pm2_string(s), s)); }
static PpNode *lit_suffixed(int64_t v, const char *suffix) { return pp_e_lit_int(pm2_int(v, suffix)); }
static PpNode *lit_usuffixed(uint64_t v, const char *suffix) { return pp_e_lit_int(pm2_uint(v, suffix)); }
static PpNode *lit_i32(int64_t v) { return lit_suffixed(v, "i32"); }
static PpNode *lit_u32(uint64_t v) { return lit_usuffixed(v, "u32"); }

static PpNode *doc_attr(const char *text) { return pp_attr_name_value(pp_path("doc"), lit_str(text), false); }

static PpNode *attr_src(const char *path, const char *tokens)
{
	PpNode *a = pp_attr_list(pp_path(path), PP_DELIM_PAREN, false);
	pp_tt_push_src(a, tokens);
	return a;
}

static PpNode *attr_word(const char *path) { return pp_attr_path(pp_path(path), false); }

static PpNode *mac_src(const char *path, int delim, const char *tokens)
{
	PpNode *m = pp_macro(pp_path(path), delim);
	pp_tt_push_src(m, tokens);
	return m;
}

static PpNode *self_f(const char *field) { return pp_e_field(pp_e("self"), field); }
static PpNode *arg(const char *name, PpNode *ty) { return pp_p_type(pp_p_ident(name, false, false), ty); }
static PpNode *ty_ref(PpNode *ty) { return pp_ty_ref(NULL, false, ty); }
static PpNode *ty_ref_mut(PpNode *ty) { return pp_ty_ref(NULL, true, ty); }
static PpNode *ty_static_str(void) { return pp_ty_ref("'static", false, pp_ty("str")); }
static PpNode *ty_impl(const char *path) { return pp_ty_impl(pp_bounds(pp_trait_bound(pp_path(path), false), NULL)); }
static PpNode *ty_g1(const char *path, PpNode *a) { return pp_ty_gen(path, a, NULL); }
static PpNode *ty_g2(const char *path, PpNode *a, PpNode *b) { return pp_ty_gen(path, a, b, NULL); }
static PpNode *e_call1(const char *f, PpNode *a) { return pp_e_call(pp_e(f), a, NULL); }
static PpNode *e_call2(const char *f, PpNode *a, PpNode *b) { return pp_e_call(pp_e(f), a, b, NULL); }
static PpNode *e_call3(const char *f, PpNode *a, PpNode *b, PpNode *c) { return pp_e_call(pp_e(f), a, b, c, NULL); }
static PpNode *e_some(PpNode *e) { return e_call1("::core::option::Option::Some", e); }
static PpNode *e_none(void) { return pp_e("::core::option::Option::None"); }
static PpNode *e_ok_unit(void) { return e_call1("::core::result::Result::Ok", pp_e_tuple(NULL)); }
static PpNode *e_meth0(PpNode *recv, const char *m) { return pp_e_method(recv, m, NULL); }
static PpNode *e_meth1(PpNode *recv, const char *m, PpNode *a) { return pp_e_method(recv, m, a, NULL); }
static PpNode *s_semi(PpNode *e) { return pp_s_expr(e, true); }
static PpNode *s_expr(PpNode *e) { return pp_s_expr(e, false); }
static PpNode *s_let(const char *name, bool mut, PpNode *init) { return pp_s_local(pp_p_ident(name, false, mut), init); }

static PpNode *allow_unused_use(const char *tree)
{
	bool lead = tree[0] == ':';
	PpNode *u = pp_item_use(0, pp_use_tree(lead ? tree + 2 : tree), lead);
	pp_push_attr(u, attr_src("allow", "unused_imports"));
	return pp_s_item(u);
}

static PpNode *item_mod_super(const char *name, Vec *items)
{
	PpNode *m = pp_item_mod(PP_VIS_PUB, name, true);
	PpNode *u = pp_item_use(0, pp_use_tree("super::*"), false);
	pp_push_attr(u, attr_src("allow", "unused_imports"));
	pp_push(m, u);
	push_all(m, items);
	return m;
}

static void push_all(PpNode *parent, const Vec *nodes)
{
	for (int i = 0; i < nodes->n; i++)
		pp_push(parent, nodes->v[i]);
}

static void attrs_all(PpNode *node, const Vec *attrs)
{
	for (int i = 0; i < attrs->n; i++)
		pp_push_attr(node, attrs->v[i]);
}

static void vcat(Vec *dst, const Vec *src)
{
	for (int i = 0; i < src->n; i++)
		vec_push(dst, src->v[i]);
}

/* fn name(&self) / (&mut self) -> ret block */
static PpNode *fn_self(const char *name, bool mut, PpNode *ret, PpNode *block)
{
	PpNode *sig = pp_sig(name, ret);
	pp_push(sig, pp_receiver(true, mut, NULL));
	return pp_item_fn(0, sig, block);
}

static PpNode *const_static_str(const char *name, const char *value)
{
	return pp_item_const(0, name, ty_static_str(), lit_str(value));
}

/* <Ty as Trait>::rest */
static PpNode *e_qpath(PpNode *self_ty, const char *trait, const char *rest)
{
	return pp_e_qpath(self_ty, pp_path(trait)->list.n, pp_path(xcat(trait, "::", rest)));
}

/* <Ty as Trait>::method(a) */
static PpNode *e_call1_q(PpNode *self_ty, const char *trait, const char *method, PpNode *a)
{
	return pp_e_call(e_qpath(self_ty, trait, method), a, NULL);
}
