/* Port of prettyplease 0.2.37 src/generics.rs (Copyright David Tolnay, MIT OR Apache-2.0), without
 * the "verbatim" feature. */

static void p_expr_lit(Pp *p, const PpNode *e);
static void p_expr_path(Pp *p, const PpNode *e);
static void p_expr_block(Pp *p, const PpNode *e);
static void p_expr_as_small_block(Pp *p, const PpNode *e, intptr_t indent);

static void p_generic_param(Pp *p, const PpNode *param);

static void p_generics(Pp *p, const PpNode *generics)
{
	if (!generics || !generics->list.n)
		return;
	p_word(p, "<");
	p_cbox(p, 0);
	p_zerobreak(p);
	int last = -1;
	bool any_second = false;
	for (int i = 0; i < generics->list.n; i++)
		if (generics->list.v[i]->kind != PP_LIFETIME_PARAM)
			any_second = true;
	for (int i = 0; i < generics->list.n; i++)
		if ((generics->list.v[i]->kind != PP_LIFETIME_PARAM) == any_second)
			last = i;
	for (int group = 0; group < 2; group++)
		for (int i = 0; i < generics->list.n; i++) {
			const PpNode *param = generics->list.v[i];
			if ((param->kind != PP_LIFETIME_PARAM) == group) {
				p_generic_param(p, param);
				p_trailing_comma(p, i == last);
			}
		}
	p_offset(p, -INDENT);
	p_end(p);
	p_word(p, ">");
}

static void p_bound_lifetimes(Pp *p, const PpNode *bl)
{
	p_word(p, "for<");
	for (int i = 0; i < bl->list.n; i++) {
		p_generic_param(p, bl->list.v[i]);
		if (i != bl->list.n - 1)
			p_word(p, ", ");
	}
	p_word(p, "> ");
}

static void p_type_param_bound(Pp *p, const PpNode *bound)
{
	if (bound->kind == PP_LIFETIME) {
		p_lifetime(p, bound->text);
		return;
	}
	if (bound->kind == PP_PRECISE_CAPTURE) { /* precise_capture */
		p_word(p, "use<");
		for (int i = 0; i < bound->list.n; i++) {
			const PpNode *capture = bound->list.v[i];
			if (capture->kind == PP_LIFETIME)
				p_lifetime(p, capture->text);
			else
				p_ident(p, capture->text);
			if (i != bound->list.n - 1)
				p_word(p, ", ");
		}
		p_word(p, ">");
		return;
	}
	/* trait_bound */
	if (bound->flags & PP_F_PAREN)
		p_word(p, "(");
	if (bound->b)
		p_bound_lifetimes(p, bound->b);
	if (bound->flags & PP_F_MAYBE)
		p_word(p, "?");
	const PpNode *path = bound->a;
	for (int i = 0; i < path->list.n; i++) {
		if (i || (path->flags & PP_F_LEADING_COLON))
			p_word(p, "::");
		p_path_segment(p, path->list.v[i], PATH_TYPE);
	}
	if (bound->flags & PP_F_PAREN)
		p_word(p, ")");
}

static void p_generic_param(Pp *p, const PpNode *param)
{
	p_outer_attrs(p, param->attrs);
	switch (param->kind) {
	case PP_LIFETIME_PARAM:
		p_lifetime(p, param->text);
		for (int i = 0; i < param->list.n; i++) {
			p_word(p, i ? " + " : ": ");
			p_lifetime(p, param->list.v[i]->text);
		}
		break;
	case PP_TYPE_PARAM:
		p_ident(p, param->text);
		p_ibox(p, INDENT);
		if (param->a)
			for (int i = 0; i < param->a->list.n; i++) {
				if (!i) {
					p_word(p, ": ");
				} else {
					p_space(p);
					p_word(p, "+ ");
				}
				p_type_param_bound(p, param->a->list.v[i]);
			}
		if (param->b) {
			p_space(p);
			p_word(p, "= ");
			p_ty(p, param->b);
		}
		p_end(p);
		break;
	default: /* PP_CONST_PARAM */
		p_word(p, "const ");
		p_ident(p, param->text);
		p_word(p, ": ");
		p_ty(p, param->a);
		if (param->b) {
			p_word(p, " = ");
			p_const_argument(p, param->b);
		}
	}
}

static void p_where_predicate(Pp *p, const PpNode *pred)
{
	if (pred->kind == PP_PRED_TYPE) {
		if (pred->c)
			p_bound_lifetimes(p, pred->c);
		p_ty(p, pred->a);
		p_word(p, ":");
		const PpList *bounds = &pred->b->list;
		p_ibox(p, bounds->n == 1 ? 0 : INDENT);
		for (int i = 0; i < bounds->n; i++) {
			if (!i) {
				p_nbsp(p);
			} else {
				p_space(p);
				p_word(p, "+ ");
			}
			p_type_param_bound(p, bounds->v[i]);
		}
		p_end(p);
	} else {
		p_lifetime(p, pred->text);
		p_word(p, ":");
		p_ibox(p, INDENT);
		for (int i = 0; i < pred->list.n; i++) {
			if (!i) {
				p_nbsp(p);
			} else {
				p_space(p);
				p_word(p, "+ ");
			}
			p_lifetime(p, pred->list.v[i]->text);
		}
		p_end(p);
	}
}

static void p_where_clause_impl(Pp *p, const PpNode *generics, bool hardbreaks, bool semi)
{
	const PpNode *where_clause = generics ? generics->a : NULL;
	if (!where_clause || !where_clause->list.n) {
		if (semi)
			p_word(p, ";");
		else
			p_nbsp(p);
		return;
	}
	const PpList *preds = &where_clause->list;
	if (hardbreaks) {
		p_hardbreak(p);
		p_offset(p, -INDENT);
		p_word(p, "where");
		p_hardbreak(p);
		for (int i = 0; i < preds->n; i++) {
			p_where_predicate(p, preds->v[i]);
			if (i == preds->n - 1 && semi) {
				p_word(p, ";");
			} else {
				p_word(p, ",");
				p_hardbreak(p);
			}
		}
		if (!semi)
			p_offset(p, -INDENT);
	} else {
		p_space(p);
		p_offset(p, -INDENT);
		p_word(p, "where");
		p_space(p);
		for (int i = 0; i < preds->n; i++) {
			p_where_predicate(p, preds->v[i]);
			if (i == preds->n - 1 && semi)
				p_word(p, ";");
			else
				p_trailing_comma_or_space(p, i == preds->n - 1);
		}
		if (!semi)
			p_offset(p, -INDENT);
	}
}

static void p_where_clause_for_body(Pp *p, const PpNode *g) { p_where_clause_impl(p, g, true, false); }
static void p_where_clause_semi(Pp *p, const PpNode *g) { p_where_clause_impl(p, g, true, true); }
static void p_where_clause_oneline(Pp *p, const PpNode *g) { p_where_clause_impl(p, g, false, false); }
static void p_where_clause_oneline_semi(Pp *p, const PpNode *g) { p_where_clause_impl(p, g, false, true); }

static void p_const_argument(Pp *p, const PpNode *e)
{
	if (e->kind == PP_E_LIT) {
		p_expr_lit(p, e);
	} else if (e->kind == PP_E_PATH && !(e->attrs && e->attrs->n) && !e->a && e->b->list.n == 1 &&
		   !(e->b->flags & PP_F_LEADING_COLON) && !e->b->list.v[0]->a) {
		p_expr_path(p, e);
	} else if (e->kind == PP_E_BLOCK) {
		p_expr_block(p, e);
	} else {
		p_cbox(p, INDENT);
		p_expr_as_small_block(p, e, 0);
		p_end(p);
	}
}
