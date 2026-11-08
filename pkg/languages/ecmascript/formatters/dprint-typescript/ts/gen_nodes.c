/* Port of dprint-plugin-typescript 0.87.1 src/generation/generate.rs: the generators of the node
 * kinds (gen_class_method, gen_if_stmt, gen_union_type, ...) and gen_node's dispatch, plus
 * swc/flatten_binary_expr.rs and swc/flatten_member_like_expr.rs.
 *
 * dprint-plugin-typescript: Copyright (c) 2019-2022 David Sherret, MIT License. */

static bool has_surrounding_different_line_comments(Ctx *c, Node *node)
{
	Comments lc = leading_comments(c, node->lo);
	if (!cm_empty(&lc)) {
		int sl = start_line(c, nrange(node));
		CM_FOR(lc, c->src, cm)
		{
			if (cstart_line(c, cm) != sl)
				return true;
		}
	}
	Comments tc = trailing_comments(c, node->hi);
	if (!cm_empty(&tc)) {
		int el = end_line(c, nrange(node));
		CM_FOR(tc, c->src, cm)
		{
			if (!cm->block || cend_line(c, cm) != el)
				return true;
		}
	}
	return false;
}

static bool range_contains(Range outer, Range inner) { return outer.lo <= inner.lo && inner.hi <= outer.hi; }

/* ---- identifiers, literals ---- */

static Str ident_sym(Ctx *c, Node *n)
{
	int ti = c->src->tok_at_start[n->lo];
	if (ti < 0)
		fail("dprint: identifier without token");
	return text_of(c, trange(&c->src->toks[ti]));
}

static Items gen_identifier(Ctx *c, Node *n)
{
	Items items = {0};
	Str s = ident_sym(c, n);
	items_push_strn(&items, s.p, s.n);
	Node *parent = n->parent;
	if ((n->flags & F_OPTIONAL) && !(parent && (parent->kind == N_CLASS_PROP || parent->kind == N_CLASS_METHOD)))
		items_push_str(&items, "?");
	return items;
}

static Items gen_text(Ctx *c, Node *n)
{
	Str s = ntext(c, n);
	Items items = {0};
	items_push_strn(&items, s.p, s.n);
	return items;
}

/* string_literal::get_value + gen_non_jsx_text with quoteStyle preferDouble */
static Items gen_string_literal(Ctx *c, Node *n)
{
	Str raw = ntext(c, n);
	Buf v = {0};
	if (raw.n > 2) {
		Str inner = {raw.p + 1, raw.n - 2};
		bool is_double = raw.p[0] == '"';
		/* replace \" (or \') with the quote */
		Buf r = {0};
		for (size_t i = 0; i < inner.n; i++) {
			if (inner.p[i] == '\\' && i + 1 < inner.n && inner.p[i + 1] == (is_double ? '"' : '\'')) {
				buf_addc(&r, inner.p[i + 1]);
				i++;
			} else {
				buf_addc(&r, inner.p[i]);
			}
		}
		/* remove_needless_quote_backslashes */
		bool was_last_backslash = false;
		for (size_t i = 0; i < r.n; i++) {
			char ch = r.p[i];
			if (ch == '\\' && !was_last_backslash) {
				was_last_backslash = true;
			} else {
				if (was_last_backslash && ch != '\'' && ch != '"')
					buf_addc(&v, '\\');
				buf_addc(&v, ch);
				was_last_backslash = false;
			}
		}
		if (was_last_backslash)
			buf_addc(&v, '\\');
	}
	int dq = 0, sq = 0;
	for (size_t i = 0; i < v.n; i++) {
		if (v.p[i] == '"')
			dq++;
		else if (v.p[i] == '\'')
			sq++;
	}
	char q = dq - sq <= 0 ? '"' : '\'';
	Buf out = {0};
	buf_addc(&out, q);
	for (size_t i = 0; i < v.n; i++) {
		if (v.p[i] == q)
			buf_addc(&out, '\\');
		buf_addc(&out, v.p[i]);
	}
	buf_addc(&out, q);
	return gen_from_raw_string(out.p, out.n);
}

static Items gen_quotable_prop(Ctx *c, Node *n) { return gen_node(n, c); }

/* ---- helpers ---- */

static Items surround_with_parens(Items it)
{
	Items items = {0};
	items_push_str(&items, "(");
	items_extend(&items, it);
	items_push_str(&items, ")");
	return items;
}

static const char *accessibility_to_str(uint8_t a)
{
	switch (a) {
	case ACC_PRIVATE:
		return "private ";
	case ACC_PROTECTED:
		return "protected ";
	case ACC_PUBLIC:
		return "public ";
	default:
		return NULL;
	}
}

static bool should_skip_paren_expr(Ctx *c, Node *node)
{
	Node *expr = node->a;
	if (has_surrounding_different_line_comments(c, expr))
		return false;
	if (expr->kind == N_ASSIGN_EXPR && expr->a->kind == N_OBJECT_PAT)
		return false;
	if (expr->kind == N_SEQ_EXPR)
		return false;
	Comments lc = leading_comments(c, node->lo);
	CM_FOR(lc, c->src, cm)
	{
		Str t = comment_text(c, cm);
		if (cm->block && t.n > 0 && t.p[0] == '*') {
			for (size_t i = 0; i + 5 <= t.n; i++)
				if (memcmp(t.p + i, "@type", 5) == 0)
					return false;
		}
	}
	Node *parent = node->parent;
	if (parent->kind == N_UPDATE_EXPR && (expr->kind == N_TS_AS_EXPR || expr->kind == N_TS_TYPE_ASSERTION))
		return false;
	if (expr->kind == N_ARRAY_LIT || expr->kind == N_IDENT)
		return true;
	if (parent->kind == N_MEMBER_EXPR && expr->kind == N_MEMBER_EXPR)
		return true;
	if (parent->kind == N_PAREN_EXPR || parent->kind == N_EXPR_STMT || parent->kind == N_UPDATE_EXPR ||
	    parent->kind == N_COMPUTED_PROP_NAME)
		return true;
	if (parent->kind == N_ASSIGN_EXPR && range_contains(nrange(parent->b), nrange(node)))
		return true;
	if (parent->kind == N_VAR_DECLARATOR && expr->kind != N_ASSIGN_EXPR && parent->b &&
	    range_contains(nrange(parent->b), nrange(node)))
		return true;
	if (parent->kind == N_EXPR_OR_SPREAD) {
		Node *gp = parent->parent;
		bool known = gp->kind == N_NEW_EXPR || gp->kind == N_ARRAY_LIT || gp->kind == N_CALL_EXPR || gp->kind == N_OPT_CALL;
		if (known && !(parent->flags & F_SPREAD))
			return true;
	}
	if (parent->kind == N_MEMBER_EXPR && parent->b->kind == N_COMPUTED_PROP_NAME &&
	    range_contains(nrange(parent->b), nrange(node)))
		return true;
	return false;
}

static bool should_add_parens_around_expr(Ctx *c, Node *node)
{
	Range orig = nrange(node);
	for (Node *a = node->parent; a; a = a->parent) {
		switch (a->kind) {
		case N_PAREN_EXPR:
			if (!should_skip_paren_expr(c, a))
				return false;
			break;
		case N_CALL_EXPR:
		case N_OPT_CALL:
		case N_NEW_EXPR:
			if (!range_contains(nrange(a->a), orig))
				return false;
			break;
		case N_EXPR_STMT:
			return true;
		case N_MEMBER_EXPR:
			if (a->b->kind == N_COMPUTED_PROP_NAME && range_contains(nrange(a->b), orig))
				return false;
			break;
		case N_COND_EXPR:
			return range_contains(nrange(a->a), orig);
		case N_BIN_EXPR:
			if (range_contains(nrange(a->b), orig))
				return false;
			break;
		case N_OPT_CHAIN_EXPR:
			break;
		default:
			return false;
		}
	}
	return false;
}


static Items statements_block_fn(Ctx *c, Node **children, int n, void *data);

/* ---- classes and methods ---- */

enum { CM_GETTER, CM_SETTER, CM_METHOD, CM_CONSTRUCTOR };

typedef struct {
	LineStartIndentLevel start_lsil;
	Node *type_node;
	bool has_separator;
	int separator_kind; /* 0: `=>` for function/constructor types */
	Cond *indent_after_arrow;
	int param_count;
} CloseParenArgs;

static Items close_paren_with_type_fn(Ctx *c, void *data)
{
	CloseParenArgs *a = data;
	if (a->has_separator) {
		Items sep = {0};
		items_push_str(&sep, " =>");
		items_push_signal(&sep, SIG_SPACE_IF_NOT_TRAILING);
		items_push_signal(&sep, SIG_POSSIBLE_NEW_LINE);
		if (a->indent_after_arrow)
			items_push_cond(&sep, a->indent_after_arrow);
		return gen_close_paren_with_type(c, a->start_lsil, a->type_node, &sep, a->param_count);
	}
	return gen_close_paren_with_type(c, a->start_lsil, a->type_node, NULL, a->param_count);
}

static CloseParenArgs *close_args(LineStartIndentLevel lsil, Node *type_node, int param_count)
{
	CloseParenArgs *a = xalloc(sizeof *a);
	a->start_lsil = lsil;
	a->type_node = type_node;
	a->param_count = param_count;
	return a;
}

/* get_params_or_args_range for a function-like node */
static bool params_range(Ctx *c, uint32_t start, NodeList *params, uint32_t following, Range *out)
{
	return get_params_or_args_range(c, start, params->v, params->n, following, out);
}

static Items gen_class_or_object_method(Ctx *c, Node *node, const Range *prange_, uint8_t accessibility, bool is_static,
					bool is_async, bool is_abstract, int kind, bool is_generator, bool is_optional,
					bool is_override, Node *key, Node *type_params, Node **params, int nparams,
					Node *return_type, Node *body)
{
	Items items = {0};
	LineStartIndentLevel start_header_lsil = lsil_new();
	items_push_lsil(&items, start_header_lsil);
	const char *acc = accessibility_to_str(accessibility);
	if (acc)
		items_push_str(&items, acc);
	if (is_static)
		items_push_str(&items, "static ");
	if (is_abstract)
		items_push_str(&items, "abstract ");
	if (is_override)
		items_push_str(&items, "override ");
	if (is_async)
		items_push_str(&items, "async ");
	if (kind == CM_GETTER)
		items_push_str(&items, "get ");
	else if (kind == CM_SETTER)
		items_push_str(&items, "set ");
	if (is_generator)
		items_push_str(&items, "*");
	items_extend(&items, gen_quotable_prop(c, key));
	if (is_optional)
		items_push_str(&items, "?");
	if (type_params)
		items_extend(&items, gen_node(type_params, c));
	items_extend(&items, gen_parameters_or_arguments(c, node, prange_, params, nparams, close_paren_with_type_fn,
							 close_args(start_header_lsil, return_type, nparams), true));
	if (body) {
		items_extend(&items, gen_brace_separator());
		items_extend(&items, gen_node(body, c));
	} else {
		items_push_str(&items, ";");
	}
	return items;
}

/* Function: list params, a body, b type_params, c return_type */
static Items gen_function_method(Ctx *c, Node *node, Node *fn, uint8_t acc, bool is_static, bool is_abstract, int kind,
				 bool is_optional, bool is_override, Node *key)
{
	Range pr;
	uint32_t following = fn->c ? fn->c->lo : fn->a ? fn->a->lo : fn->hi;
	bool has = params_range(c, fn->lo, &fn->list, following, &pr);
	return gen_class_or_object_method(c, node, has ? &pr : NULL, acc, is_static, (fn->flags & F_ASYNC) != 0,
					  is_abstract, kind, (fn->flags & F_GENERATOR) != 0, is_optional, is_override, key,
					  fn->b, fn->list.v, fn->list.n, fn->c, fn->a);
}

static Items gen_class_method(Ctx *c, Node *n)
{
	int kind = n->op == METHOD_GETTER ? CM_GETTER : n->op == METHOD_SETTER ? CM_SETTER : CM_METHOD;
	return gen_function_method(c, n, n->b, n->accessibility, (n->flags & F_STATIC) != 0, (n->flags & F_ABSTRACT) != 0,
				   kind, (n->flags & F_OPTIONAL) != 0, (n->flags & F_OVERRIDE) != 0, n->a);
}

static Items gen_method_prop(Ctx *c, Node *n) { return gen_function_method(c, n, n->b, ACC_NONE, false, false, CM_METHOD, false, false, n->a); }

static Items gen_constructor(Ctx *c, Node *n)
{
	Range pr;
	bool has = params_range(c, n->lo, &n->list, n->b ? n->b->lo : n->hi, &pr);
	return gen_class_or_object_method(c, n, has ? &pr : NULL, n->accessibility, false, false, false, CM_CONSTRUCTOR,
					  false, false, false, n->a, NULL, n->list.v, n->list.n, NULL, n->b);
}

static Items gen_getter_prop(Ctx *c, Node *n)
{
	Range pr;
	bool has = get_params_or_args_range(c, n->lo, NULL, 0, n->b ? n->b->lo : n->c ? n->c->lo : n->hi, &pr);
	return gen_class_or_object_method(c, n, has ? &pr : NULL, ACC_NONE, false, false, false, CM_GETTER, false, false,
					  false, n->a, NULL, NULL, 0, n->b, n->c);
}

static Items gen_setter_prop(Ctx *c, Node *n)
{
	Range pr;
	Node *params[1] = {n->b};
	bool has = get_params_or_args_range(c, n->lo, params, 1, n->c ? n->c->lo : n->hi, &pr);
	Node **pv = xalloc(sizeof(Node *));
	pv[0] = n->b;
	return gen_class_or_object_method(c, n, has ? &pr : NULL, ACC_NONE, false, false, false, CM_SETTER, false, false,
					  false, n->a, NULL, pv, 1, NULL, n->c);
}

static Items gen_class_prop(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	const char *acc = accessibility_to_str(n->accessibility);
	if (acc)
		items_push_str(&items, acc);
	if (n->flags & F_STATIC)
		items_push_str(&items, "static ");
	if (n->flags & F_ABSTRACT)
		items_push_str(&items, "abstract ");
	if (n->flags & F_OVERRIDE)
		items_push_str(&items, "override ");
	if (n->flags & F_READONLY)
		items_push_str(&items, "readonly ");
	if (n->a->kind == N_COMPUTED_PROP_NAME)
		items_extend(&items, gen_computed_prop_like(c, gen_node_fn, n->a->a, nrange(n->a->a)));
	else
		items_extend(&items, gen_node(n->a, c));
	if (n->flags & F_OPTIONAL)
		items_push_str(&items, "?");
	if (n->flags & F_DEFINITE)
		items_push_str(&items, "!");
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->c));
	if (n->b)
		items_extend(&items, gen_assignment(c, n->b, "="));
	items_push_str(&items, ";");
	return items;
}

static Items gen_parameter_prop(Ctx *c, Node *n)
{
	Items items = {0};
	const char *acc = accessibility_to_str(n->accessibility);
	if (acc)
		items_push_str(&items, acc);
	if (n->flags & F_OVERRIDE)
		items_push_str(&items, "override ");
	if (n->flags & F_READONLY)
		items_push_str(&items, "readonly ");
	items_extend(&items, gen_node(n->a, c));
	return items;
}

typedef struct {
	IsStartOfLine isol;
	LineNumber ln, start_header_ln;
} ClassExprIndentData;

static int r_class_expr_conditional_indent(RCtx *rc, void *d)
{
	ClassExprIndentData *x = d;
	int isol = rctx_isol(rc, x->isol);
	if (isol < 0)
		return -1;
	if (isol)
		return 0;
	return ch_is_multiple_lines(rc, x->ln, x->start_header_ln);
}

/* gen_class_decl_or_expr: node is ClassDecl or ClassExpr (a: ident, b: class) */
static Items gen_class_decl_or_expr(Ctx *c, Node *node, bool is_class_expr)
{
	Node *cls = node->b;
	Items items = {0};
	LineNumber before_ln;
	IsStartOfLine before_isol;
	get_or_create_current_before_comments_start_info(c, &before_ln, &before_isol);
	LineStartIndentLevel start_header_lsil = lsil_new();
	items_push_lsil(&items, start_header_lsil);
	LineNumber start_header_ln = ln_new();
	items_push_ln(&items, start_header_ln);
	if (node->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	if (cls->flags & F_ABSTRACT)
		items_push_str(&items, "abstract ");
	items_push_str(&items, "class");
	if (node->a) {
		items_push_str(&items, " ");
		items_extend(&items, gen_node(node->a, c));
	}
	if (cls->b)
		items_extend(&items, gen_node(cls->b, c));
	if (cls->a) {
		Items space = items_cond(if_above_width_or(INDENT_WIDTH, items_signal(SIG_SPACE_OR_NEW_LINE), items_str(" ")));
		items_push_cond(&items, new_line_if_hanging_space_otherwise(start_header_lsil, NULL, &space));
		Items ext = {0};
		items_push_str(&ext, "extends ");
		Items sc = gen_node(cls->a, c);
		if (cls->c)
			items_extend(&sc, gen_node(cls->c, c));
		items_extend(&ext, new_line_group(sc));
		items_push_cond(&items, indent_if_start_of_line(ext));
	}
	items_extend(&items, gen_extends_or_implements(c, "implements", cls->list3.v, cls->list3.n, start_header_lsil));
	items_extend(&items, gen_membered_body(c, cls, cls->list.v, cls->list.n, blank_line_separating, SEP_NONE));
	if (is_class_expr) {
		PNode *p = items_into_path(items);
		ClassExprIndentData *x = xalloc(sizeof *x);
		x->isol = before_isol;
		x->ln = before_ln;
		x->start_header_ln = start_header_ln;
		return items_cond(if_true_or("classExprConditionalIndent", resolver(r_class_expr_conditional_indent, x),
					     with_indent(items_path(p)), items_path(p)));
	}
	return items;
}

/* ---- functions ---- */

static Items gen_function_decl_or_expr(Ctx *c, Node *node, bool is_func_decl, Node *ident, bool declare, Node *fn)
{
	Items items = {0};
	LineStartIndentLevel start_header_lsil = lsil_new();
	bool space_after_function_keyword = !is_func_decl; /* functionExpression.spaceAfterFunctionKeyword */
	items_push_lsil(&items, start_header_lsil);
	if (declare)
		items_push_str(&items, "declare ");
	if (fn->flags & F_ASYNC)
		items_push_str(&items, "async ");
	items_push_str(&items, "function");
	if (fn->flags & F_GENERATOR)
		items_push_str(&items, "*");
	if (space_after_function_keyword)
		items_push_str(&items, " ");
	if (ident) {
		if (!space_after_function_keyword)
			items_push_str(&items, " ");
		items_extend(&items, gen_node(ident, c));
	}
	if (fn->b)
		items_extend(&items, gen_node(fn->b, c));
	Range pr;
	uint32_t following = fn->c ? fn->c->lo : fn->a ? fn->a->lo : fn->hi;
	bool has = params_range(c, fn->lo, &fn->list, following, &pr);
	items_extend(&items, gen_parameters_or_arguments(c, node, has ? &pr : NULL, fn->list.v, fn->list.n, close_paren_with_type_fn,
							 close_args(start_header_lsil, fn->c, fn->list.n), true));
	if (fn->a) {
		items_extend(&items, gen_brace_separator());
		items_extend(&items, gen_node(fn->a, c));
	} else {
		items_push_str(&items, ";");
	}
	return items;
}

/* ---- arrow functions ---- */

static bool use_new_line_group_for_arrow_body(Node *arrow)
{
	Node *body = arrow->a;
	if (body->kind == N_BLOCK_STMT)
		return true;
	if (body->kind == N_PAREN_EXPR)
		return body->a->kind != N_OBJECT_LIT;
	return true;
}

typedef struct {
	LineNumber start, end;
} BodyLnData;

static int r_body_multiple_lines(RCtx *rc, void *d)
{
	BodyLnData *x = d;
	return ch_is_multiple_lines(rc, x->start, x->end);
}

static Items gen_arrow_func_expr(Ctx *c, Node *n)
{
	LineStartIndentLevel header_start_lsil = lsil_new();
	Items items = {0};
	items_push_lsil(&items, header_start_lsil);
	if (n->flags & F_ASYNC)
		items_push_str(&items, "async ");
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	/* arrowFunction.useParentheses force: has_parens? */
	bool has_parens = true;
	if (n->list.n == 1) {
		TokList toks = tokens_outside_children(c, n);
		has_parens = false;
		Children ch = node_children(n);
		uint32_t first_child_lo = ch.n ? ch.v[0]->lo : n->hi;
		for (int i = 0; i < toks.n; i++) {
			if (toks.v[i]->lo >= first_child_lo)
				break;
			if (toks.v[i]->kind == TK_LPAREN) {
				has_parens = true;
				break;
			}
		}
	}
	if (has_parens) {
		Range pr;
		bool has = params_range(c, n->lo, &n->list, n->c ? n->c->lo : n->a->lo, &pr);
		items_extend(&items, gen_parameters_or_arguments(c, n, has ? &pr : NULL, n->list.v, n->list.n, close_paren_with_type_fn,
								 close_args(header_start_lsil, n->c, n->list.n), true));
	} else {
		items_extend(&items, surround_with_parens(gen_node(n->list.v[0], c)));
	}
	items_push_str(&items, " =>");

	/* gen_arrow_body */
	Items generated_body = gen_node(n->a, c);
	if (use_new_line_group_for_arrow_body(n))
		generated_body = new_line_group(generated_body);
	PNode *body_path = items_into_path(generated_body);
	if (n->a->kind == N_BLOCK_STMT) {
		items_extend(&items, gen_brace_separator());
		items_push_path(&items, body_path);
	} else {
		BodyLnData *x = xalloc(sizeof *x);
		x->start = ln_new();
		x->end = ln_new();
		items_push_ln(&items, x->start);
		Node *b = n->a;
		bool no_newline = b->kind == N_PAREN_EXPR || b->kind == N_ARRAY_LIT;
		if (b->kind == N_TPL && b->list2.n > 0) {
			Str raw = ntext(c, b->list2.v[0]);
			no_newline = raw.n > 0 && (raw.p[0] == '\n' || raw.p[0] == '\r');
		}
		if (no_newline) {
			items_push_str(&items, " ");
		} else {
			items_push_cond(&items, if_above_width_or(INDENT_WIDTH,
								  items_cond(if_true_or("newlineOrSpace", resolver(r_body_multiple_lines, x),
											items_signal(SIG_NEW_LINE),
											items_signal(SIG_SPACE_OR_NEW_LINE))),
								  items_str(" ")));
		}
		items_push_cond(&items, indent_if_start_of_line(items_path(body_path)));
		items_push_ln(&items, x->end);
	}
	if (should_add_parens_around_expr(c, n))
		return surround_with_parens(items);
	return items;
}

/* ---- declarations ---- */

static Items gen_export_decl(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "export ");
	items_extend(&items, gen_node(n->a, c));
	return items;
}

static Items gen_enum_decl(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_lsil(&items, lsil_new());
	if (n->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	if (n->flags & F_CONST)
		items_push_str(&items, "const ");
	items_push_str(&items, "enum ");
	items_extend(&items, gen_node(n->a, c));
	items_extend(&items, gen_membered_body(c, n, n->list.v, n->list.n, blank_line_separating, SEP_COMMA_ONLY_MULTI_LINE));
	return items;
}

static Items gen_enum_member(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	if (n->b)
		items_extend(&items, gen_assignment(c, n->b, "="));
	return items;
}

/* sorting for named imports/exports: caseInsensitive */
static int sort_specifiers_case_insensitive(Ctx *c, int ai, Node *a, int bi, Node *b)
{
	/* get_comparison_nodes: ImportNamedSpecifier [imported, local] or [local];
	 * ExportNamedSpecifier [orig, exported] or [orig] */
	Node *an[2], *bn[2];
	int na = 0, nb = 0;
	if (a->kind == N_IMPORT_NAMED_SPECIFIER) {
		if (a->b)
			an[na++] = a->b;
		an[na++] = a->a;
	} else {
		an[na++] = a->a;
		if (a->b)
			an[na++] = a->b;
	}
	if (b->kind == N_IMPORT_NAMED_SPECIFIER) {
		if (b->b)
			bn[nb++] = b->b;
		bn[nb++] = b->a;
	} else {
		bn[nb++] = b->a;
		if (b->b)
			bn[nb++] = b->b;
	}
	int r = 0;
	int i;
	for (i = 0; i < na; i++) {
		if (i >= nb) {
			r = 1;
			break;
		}
		r = cmp_text_case_insensitive(ntext(c, an[i]), ntext(c, bn[i]));
		if (r)
			break;
	}
	if (r == 0 && i == na && na < nb)
		r = -1;
	if (r == 0)
		return ai < bi ? -1 : ai > bi ? 1 : 0;
	return r;
}

static Items gen_named_import_or_export_specifiers(Ctx *c, Node *parent, Node **specifiers, int n)
{
	return gen_object_like_node(c, parent, specifiers, n, SEP_COMMA_ONLY_MULTI_LINE, false, false, true, false,
				    sort_specifiers_case_insensitive);
}

static Items gen_import_decl(Ctx *c, Node *n)
{
	Node *default_import = NULL, *namespace_import = NULL;
	Node **named = xalloc(sizeof(Node *) * (size_t)(n->list.n + 1));
	int nnamed = 0;
	for (int i = 0; i < n->list.n; i++) {
		Node *s = n->list.v[i];
		if (s->kind == N_IMPORT_DEFAULT_SPECIFIER)
			default_import = s;
		else if (s->kind == N_IMPORT_STAR_AS_SPECIFIER)
			namespace_import = s;
		else
			named[nnamed++] = s;
	}
	bool should_single_line = !default_import && !namespace_import && nnamed <= 1 &&
				  start_line(c, nrange(n)) == end_line(c, nrange(n));
	bool has_named_imports = nnamed > 0;
	if (!has_named_imports) {
		const Tok *from = get_previous_token_if_text(c, nrange(n->a), "from");
		if (from && get_previous_token_if(c, trange(from), TK_RBRACE))
			has_named_imports = true;
	}
	bool has_from = default_import || namespace_import || has_named_imports;
	Items items = {0};
	items_push_str(&items, "import ");
	if (n->flags & F_TYPE_ONLY)
		items_push_str(&items, "type ");
	if (default_import) {
		items_extend(&items, gen_node(default_import, c));
		if (namespace_import || has_named_imports)
			items_push_str(&items, ", ");
	}
	if (namespace_import)
		items_extend(&items, gen_node(namespace_import, c));
	if (has_named_imports)
		items_extend(&items, gen_named_import_or_export_specifiers(c, n, named, nnamed));
	if (has_from)
		items_push_str(&items, " from ");
	items_extend(&items, gen_node(n->a, c));
	items_push_str(&items, ";");
	return should_single_line ? with_no_new_lines(items) : items;
}

static Items gen_export_named_decl(Ctx *c, Node *n)
{
	Node **named = n->list.v;
	int nnamed = n->list.n;
	Node *ns = nnamed && named[0]->kind == N_EXPORT_NAMESPACE_SPECIFIER ? named[0] : NULL;
	if (ns)
		named++, nnamed--;
	bool should_single_line = !ns && nnamed <= 1 && start_line(c, nrange(n)) == end_line(c, nrange(n));
	Items items = {0};
	items_push_str(&items, "export ");
	if (n->flags & F_TYPE_ONLY)
		items_push_str(&items, "type ");
	if (ns) {
		items_extend(&items, gen_node(ns, c));
		if (nnamed > 0)
			items_push_str(&items, ", ");
	}
	if (nnamed > 0)
		items_extend(&items, gen_named_import_or_export_specifiers(c, n, named, nnamed));
	else if (!ns)
		items_push_str(&items, "{}");
	if (n->a) {
		items_push_str(&items, " from ");
		items_extend(&items, gen_node(n->a, c));
	}
	items_push_str(&items, ";");
	return should_single_line ? with_no_new_lines(items) : items;
}

static Items gen_export_all(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "export ");
	if (n->flags & F_TYPE_ONLY)
		items_push_str(&items, "type ");
	items_push_str(&items, "* from ");
	items_extend(&items, gen_node(n->a, c));
	items_push_str(&items, ";");
	return items;
}

static Items gen_export_default_expr(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "export default ");
	items_extend(&items, gen_node(n->a, c));
	items_push_str(&items, ";");
	return items;
}

static Items gen_import_named_specifier(Ctx *c, Node *n)
{
	Items items = {0};
	if ((n->flags & F_TYPE_ONLY) && !(n->parent->flags & F_TYPE_ONLY))
		items_push_str(&items, "type ");
	if (n->b) {
		items_extend(&items, gen_node(n->b, c));
		items_push_signal(&items, SIG_SPACE_OR_NEW_LINE);
		Items as = {0};
		items_push_str(&as, "as ");
		items_extend(&as, gen_node(n->a, c));
		items_push_cond(&items, indent_if_start_of_line(as));
	} else {
		items_extend(&items, gen_node(n->a, c));
	}
	return items;
}

static Items gen_export_named_specifier(Ctx *c, Node *n)
{
	Items items = {0};
	if ((n->flags & F_TYPE_ONLY) && !(n->parent->flags & F_TYPE_ONLY))
		items_push_str(&items, "type ");
	items_extend(&items, gen_node(n->a, c));
	if (n->b) {
		items_push_signal(&items, SIG_SPACE_OR_NEW_LINE);
		Items as = {0};
		items_push_str(&as, "as ");
		items_extend(&as, gen_node(n->b, c));
		items_push_cond(&items, indent_if_start_of_line(as));
	}
	return items;
}

static Items gen_prefixed(Ctx *c, const char *prefix, Node *n)
{
	Items items = {0};
	items_push_str(&items, prefix);
	items_extend(&items, gen_node(n, c));
	return items;
}

static Items gen_interface_decl(Ctx *c, Node *n)
{
	Items items = {0};
	LineStartIndentLevel start_header_lsil = lsil_new();
	items_push_lsil(&items, start_header_lsil);
	store_lsil_for_node(c, nrange(n), start_header_lsil);
	if (n->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	items_push_str(&items, "interface ");
	items_extend(&items, gen_node(n->a, c));
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	items_extend(&items, gen_extends_or_implements(c, "extends", n->list.v, n->list.n, start_header_lsil));
	items_extend(&items, gen_node(n->c, c));
	return items;
}

static Items gen_interface_body(Ctx *c, Node *n)
{
	return gen_membered_body(c, n, n->list.v, n->list.n, blank_line_separating, SEP_SEMI_PREFER);
}

static Items gen_module_decl(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	items_push_str(&items, (n->flags & F_NAMESPACE) ? "namespace " : "module ");
	items_extend(&items, gen_node(n->a, c));
	Node *body = n->b;
	items_extend(&items, gen_brace_separator());
	MembersOpts *o = xalloc(sizeof *o);
	o->inner_range = rng(body->lo + 1, body->hi - 1);
	o->should_use_blank_line = blank_line_separating;
	o->separator = SEP_NONE;
	(void)o;
	Range r = nrange(body);
	items_extend(&items, gen_block(c, statements_block_fn, body, &r, body->list.v, body->list.n));
	return items;
}

static Items gen_type_alias(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	items_push_str(&items, "type ");
	items_extend(&items, gen_node(n->a, c));
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	items_extend(&items, gen_assignment(c, n->c, "="));
	items_push_str(&items, ";");
	return items;
}

/* ---- expressions ---- */

static Items gen_assign_expr(Ctx *c, Node *n)
{
	/* nested `a = b = c`: get_flattened_assign_expr (not emitted by ts-proto) */
	Items items = gen_node(n->a, c);
	const char *op;
	switch (n->op) {
	case TK_ASSIGN: op = "="; break;
	case TK_ADD_ASSIGN: op = "+="; break;
	case TK_SUB_ASSIGN: op = "-="; break;
	case TK_MUL_ASSIGN: op = "*="; break;
	case TK_DIV_ASSIGN: op = "/="; break;
	case TK_MOD_ASSIGN: op = "%="; break;
	case TK_EXP_ASSIGN: op = "**="; break;
	case TK_LSHIFT_ASSIGN: op = "<<="; break;
	case TK_RSHIFT_ASSIGN: op = ">>="; break;
	case TK_ZFRSHIFT_ASSIGN: op = ">>>="; break;
	case TK_BITOR_ASSIGN: op = "|="; break;
	case TK_BITXOR_ASSIGN: op = "^="; break;
	case TK_BITAND_ASSIGN: op = "&="; break;
	case TK_AND_ASSIGN: op = "&&="; break;
	case TK_OR_ASSIGN: op = "||="; break;
	default: op = "?\?="; break;
	}
	items_extend(&items, gen_assignment(c, n->b, op));
	return items;
}

/* ---- binary expressions (swc/flatten_binary_expr.rs) ---- */

static const char *binop_str(Node *n)
{
	switch (n->op) {
	case TK_EQEQ: return "==";
	case TK_NOTEQ: return "!=";
	case TK_EQEQEQ: return "===";
	case TK_NOTEQEQ: return "!==";
	case TK_LT: return "<";
	case TK_LTEQ: return "<=";
	case TK_GT: return ">";
	case TK_GTEQ: return ">=";
	case TK_LSHIFT: return "<<";
	case TK_RSHIFT: return ">>";
	case TK_ZFRSHIFT: return ">>>";
	case TK_ADD: return "+";
	case TK_SUB: return "-";
	case TK_MUL: return "*";
	case TK_DIV: return "/";
	case TK_MOD: return "%";
	case TK_BITOR: return "|";
	case TK_BITXOR: return "^";
	case TK_BITAND: return "&";
	case TK_EXP: return "**";
	case TK_LOGICAL_OR: return "||";
	case TK_LOGICAL_AND: return "&&";
	case TK_NULLISH: return "??";
	default: return (n->flags & F_INSTANCEOF) ? "instanceof" : "in";
	}
}

/* BinaryOp identity: the token kind, plus in/instanceof distinguished by the flag */
static int binop_id(Node *n) { return n->op == TK_WORD ? ((n->flags & F_INSTANCEOF) ? -2 : -1) : n->op; }
static bool op_is_add_sub(int op) { return op == TK_ADD || op == TK_SUB; }
static bool op_is_mul_div(int op) { return op == TK_MUL || op == TK_DIV; }
DP bool op_is_bitwise_or_arithmetic(int op)
{
	return op == TK_LSHIFT || op == TK_RSHIFT || op == TK_ZFRSHIFT || op == TK_ADD || op == TK_SUB || op == TK_MUL ||
	       op == TK_DIV || op == TK_MOD || op == TK_BITOR || op == TK_BITXOR || op == TK_BITAND;
}
static bool op_is_logical(int op) { return op == TK_LOGICAL_AND || op == TK_LOGICAL_OR; }
static bool op_is_bit_logical(int op) { return op == TK_BITOR || op == TK_BITAND || op == TK_BITXOR; }
static bool op_is_bit_shift(int op) { return op == TK_LSHIFT || op == TK_RSHIFT || op == TK_ZFRSHIFT; }
static bool op_is_equality(int op)
{
	return op == TK_EQEQ || op == TK_NOTEQ || op == TK_EQEQEQ || op == TK_NOTEQEQ || op == TK_LT || op == TK_LTEQ ||
	       op == TK_GT || op == TK_GTEQ;
}

typedef struct {
	const Tok *pre_op, *post_op;
	const char *pre_str, *post_str;
	Node *expr;
} BinItem;

typedef struct {
	BinItem *v;
	int n, cap;
} BinItems;

static void bi_push(BinItems *l, BinItem it)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 8;
		l->v = xrealloc(l->v, sizeof(BinItem) * (size_t)l->cap, sizeof(BinItem) * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = it;
}

static bool is_expression_breakable(int top_op, int op)
{
	if (op_is_add_sub(top_op))
		return op_is_add_sub(op);
	if (op_is_mul_div(top_op))
		return op_is_mul_div(op);
	return top_op == op;
}

/* get_flattened_bin_expr with binaryExpression.operatorPosition sameLine */
static void get_flattened_bin_expr(Ctx *c, Node *node, BinItems *items)
{
	const char *ops = binop_str(node);
	const Tok *op_tok = get_first_operator_after(c, nrange(node->a), ops);
	int op = binop_id(node);
	bool handled_left = false;
	if (node->a->kind == N_BIN_EXPR && is_expression_breakable(op, binop_id(node->a))) {
		get_flattened_bin_expr(c, node->a, items);
		items->v[items->n - 1].post_op = op_tok;
		items->v[items->n - 1].post_str = ops;
		handled_left = true;
	}
	if (!handled_left)
		bi_push(items, (BinItem){NULL, op_tok, NULL, ops, node->a});
	if (node->b->kind == N_BIN_EXPR && is_expression_breakable(op, binop_id(node->b))) {
		get_flattened_bin_expr(c, node->b, items);
	} else {
		bi_push(items, (BinItem){NULL, NULL, NULL, NULL, node->b});
	}
}

typedef struct {
	bool allow_no_indent;
	IsStartOfLine isol;
	IndentLevel il;
} BinIndentData;

static int r_bin_hanging_indent(RCtx *rc, void *d)
{
	BinIndentData *x = d;
	if (x->allow_no_indent) {
		int v = rctx_isol(rc, x->isol);
		if (v < 0)
			return -1;
		if (v)
			return 0;
	}
	return wi_is_start_of_line(&rc->writer_info);
}

static int r_indent_if_necessary(RCtx *rc, void *d)
{
	BinIndentData *x = d;
	if (x->allow_no_indent) {
		int v = rctx_isol(rc, x->isol);
		if (v < 0)
			return -1;
		if (v)
			return 0;
	}
	uint32_t il;
	if (!rctx_il(rc, x->il, &il))
		return -1;
	return !(il < rc->writer_info.indent_level);
}

typedef struct {
	BinItem item;
	const BinItem *next;
	bool is_inner;
	bool use_space;
	BinIndentData *ind;
	Items leading_pre, trailing_pre;
} BinInnerData;

static Items bin_item_inner(Items node_items, Ctx *c, void *data)
{
	BinInnerData *d = data;
	Items items = {0};
	if (d->item.pre_op) {
		if (!items_empty(d->leading_pre))
			items_extend(&items, d->leading_pre);
		items_push_str(&items, d->item.pre_str);
		if (items_empty(d->trailing_pre)) {
			if (d->use_space)
				items_push_str(&items, " ");
		} else {
			items_extend(&items, d->trailing_pre);
			items_push_str(&items, " ");
		}
	}
	if (d->is_inner) {
		PNode *p = items_into_path(node_items);
		items_extend(&items, with_queued_indent(items_cond(if_true_or("indentIfNecessary", resolver(r_indent_if_necessary, d->ind),
									      with_queued_indent(items_path(p)), items_path(p)))));
	} else {
		items_extend(&items, node_items);
	}
	if (d->item.post_op) {
		Items lead = gen_leading_comments_same_line(c, trange(d->item.post_op));
		Items trail = gen_trailing_comments_same_line(c, trange(d->item.post_op));
		if (items_empty(lead)) {
			if (d->use_space)
				items_push_str(&items, " ");
		} else {
			items_push_str(&items, " ");
			items_extend(&items, lead);
		}
		items_push_str(&items, d->item.post_str);
		if (!items_empty(trail))
			items_extend(&items, trail);
	} else if (d->next && d->next->pre_op) {
		items_extend(&items, gen_trailing_comments_if_line_comment_same_line(c, trange(d->next->pre_op)));
	}
	return items;
}

typedef struct {
	Ctx *c;
	BinItems flat;
	bool use_space;
	BinIndentData *ind;
} BinValuesData;

static GeneratedValues bin_values_fn(ConditionReference ref, void *data)
{
	BinValuesData *d = data;
	Ctx *c = d->c;
	GeneratedValues values = {0};
	for (int i = 0; i < d->flat.n; i++) {
		BinItem *it = &d->flat.v[i];
		LinesSpan span = {true, (size_t)start_line(c, nrange(it->expr)), (size_t)end_line(c, nrange(it->expr))};
		BinInnerData *bd = xalloc(sizeof *bd);
		bd->item = *it;
		bd->next = i + 1 < d->flat.n ? &d->flat.v[i + 1] : NULL;
		bd->is_inner = it->expr->kind == N_BIN_EXPR;
		bd->use_space = d->use_space;
		bd->ind = d->ind;
		if (it->pre_op) {
			bd->leading_pre = gen_leading_comments_same_line(c, trange(it->pre_op));
			bd->trailing_pre = gen_trailing_comments_same_line(c, trange(it->pre_op));
		}
		Items items = gen_node_with_inner_gen(it->expr, c, bin_item_inner, bd);
		gv_push(&values, (GeneratedValue){new_line_group(items), span, true, true});
	}
	return values;
}

static Items gen_binary_expr(Ctx *c, Node *n)
{
	BinValuesData *d = xalloc(sizeof *d);
	d->c = c;
	get_flattened_bin_expr(c, n, &d->flat);
	int op = binop_id(n);
	BinIndentData *ind = xalloc(sizeof *ind);
	ind->il = il_new();
	ind->isol = isol_new();
	/* get_allow_no_indent */
	Node *parent = n->parent;
	bool allow_no_indent;
	if (!op_is_add_sub(op) && !op_is_mul_div(op) && !op_is_logical(op) && !op_is_bit_logical(op) && !op_is_bit_shift(op) &&
	    op != TK_MOD)
		allow_no_indent = false;
	else if (parent->kind == N_EXPR_STMT || parent->kind == N_BIN_EXPR)
		allow_no_indent = false;
	else if (parent->kind == N_EXPR_OR_SPREAD)
		allow_no_indent = !(parent->parent->kind == N_CALL_EXPR || parent->parent->kind == N_OPT_CALL ||
				    parent->parent->kind == N_NEW_EXPR);
	else
		allow_no_indent = true;
	ind->allow_no_indent = allow_no_indent;
	d->use_space = true; /* spaceSurroundingBitwiseAndArithmeticOperator */
	d->ind = ind;
	bool is_parent_bin_expr = parent->kind == N_BIN_EXPR;
	MultiLineOptions mlo = mlo_maintain_line_breaks();
	if (is_parent_bin_expr) {
		mlo.hanging_is_condition = false;
		mlo.with_hanging_indent = false;
	} else {
		mlo.hanging_is_condition = true;
		mlo.hanging_condition = resolver(r_bin_hanging_indent, ind);
	}
	Items items = {0};
	items_push_il(&items, ind->il);
	items_push_isol(&items, ind->isol);
	GenSeparatedValuesOptions o = {0};
	o.indent_width = INDENT_WIDTH;
	o.single_line_options = slo_separated_same_line(items_signal(SIG_SPACE_OR_NEW_LINE));
	o.multi_line_options = mlo;
	items_extend(&items, gen_separated_values(bin_values_fn, d, o).items);
	return op_is_equality(op) ? new_line_group(items) : items;
}

/* ---- member and call expressions (swc/flatten_member_like_expr.rs) ---- */

enum { MI_NODE, MI_CALL };

typedef struct MemberItem MemberItem;
struct MemberItem {
	int kind;
	Node *node;	   /* MI_NODE: the node; MI_CALL: the original call (CallExpr or OptCall) */
	MemberItem *callee; /* MI_CALL */
};

typedef struct {
	MemberItem **v;
	int n, cap;
} MemberItems;

static void mi_push(MemberItems *l, MemberItem *it)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 8;
		l->v = xrealloc(l->v, sizeof(MemberItem *) * (size_t)l->cap, sizeof(MemberItem *) * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = it;
}

static MemberItem *mi_new(int kind, Node *node, MemberItem *callee)
{
	MemberItem *m = xalloc(sizeof *m);
	m->kind = kind;
	m->node = node;
	m->callee = callee;
	return m;
}

static void push_descendant_nodes(Node *node, MemberItems *nodes)
{
	switch (node->kind) {
	case N_MEMBER_EXPR:
		push_descendant_nodes(node->a, nodes);
		if (node->b->kind == N_COMPUTED_PROP_NAME)
			mi_push(nodes, mi_new(MI_NODE, node->b, NULL));
		else
			push_descendant_nodes(node->b, nodes);
		break;
	case N_OPT_CHAIN_EXPR:
		push_descendant_nodes(node->a, nodes);
		break;
	case N_OPT_CALL:
	case N_CALL_EXPR:
		push_descendant_nodes(node->a, nodes);
		{
			MemberItem *callee = nodes->v[--nodes->n];
			mi_push(nodes, mi_new(MI_CALL, node, callee));
		}
		break;
	default:
		mi_push(nodes, mi_new(MI_NODE, node, NULL));
		break;
	}
}

static Range mi_range(MemberItem *m)
{
	if (m->kind == MI_NODE)
		return nrange(m->node);
	return rng(mi_range(m->callee).lo, m->node->hi);
}

static bool mi_is_computed(MemberItem *m) { return m->kind == MI_NODE && m->node->kind == N_COMPUTED_PROP_NAME; }

static bool mi_is_optional(MemberItem *m)
{
	Node *top = m->node;
	if (!top->parent || !top->parent->parent)
		return false;
	Node *gp = top->parent->parent;
	return gp->kind == N_OPT_CHAIN_EXPR && (gp->flags & F_OPTIONAL);
}

/* CallOrOptCallExpr::is_optional */
static bool call_is_optional(Node *call)
{
	return call->kind == N_OPT_CALL && call->parent && (call->parent->flags & F_OPTIONAL);
}

static Items gen_call_expr_like(Ctx *c, Node *call, Items generated_callee)
{
	Items items = {0};
	items_extend(&items, generated_callee);
	if (call->b)
		items_extend(&items, gen_node(call->b, c));
	if (call_is_optional(call))
		items_push_str(&items, "?.");
	Range pr;
	bool has = get_params_or_args_range(c, call->lo, call->list.v, call->list.n, call->hi, &pr);
	items_push_cond(&items, with_indent_if_start_of_line_indented(gen_parameters_or_arguments(
					c, call, has ? &pr : NULL, call->list.v, call->list.n, NULL, NULL, false)));
	return items;
}

typedef struct {
	bool is_first, is_optional;
	Node *node;
} MemberNodeData;

static Items member_node_inner(Items node_items, Ctx *c, void *data)
{
	MemberNodeData *d = data;
	Items items = {0};
	bool is_first_and_number = false;
	if (d->is_first && d->node->kind == N_NUMBER) {
		Str t = ntext(c, d->node);
		is_first_and_number = !memchr(t.p, '.', t.n);
	}
	if (is_first_and_number)
		items_push_str(&items, "(");
	if (!d->is_first) {
		if (d->is_optional)
			items_push_str(&items, "?.");
		else if (d->node->kind != N_COMPUTED_PROP_NAME)
			items_push_str(&items, ".");
	}
	items_extend(&items, node_items);
	if (is_first_and_number)
		items_push_str(&items, ")");
	return items;
}

static Items gen_for_member_like_expr_item(Ctx *c, MemberItem *item, int index, int count)
{
	bool is_first = index == 0, is_last = index == count - 1;
	if (item->kind == MI_NODE) {
		MemberNodeData *d = xalloc(sizeof *d);
		d->is_first = is_first;
		d->is_optional = mi_is_optional(item);
		d->node = item->node;
		return gen_node_with_inner_gen(item->node, c, member_node_inner, d);
	}
	Items items = {0};
	items_extend(&items, gen_call_expr_like(c, item->node, gen_for_member_like_expr_item(c, item->callee, index, count)));
	if (!is_last)
		items_extend(&items, gen_trailing_comments(c, mi_range(item)));
	return items;
}

static Items gen_for_flattened_member_like_expr(Ctx *c, MemberItems *nodes)
{
	Items items = {0};
	LineNumber start_ln = ln_new();
	LineNumber last_item_start_ln = ln_new();
	int total = nodes->n;
	if (total > 1)
		items_push_ln(&items, start_ln);
	items_extend(&items, gen_for_member_like_expr_item(c, nodes->v[0], 0, total));
	for (int i = 1; i < total; i++) {
		MemberItem *item = nodes->v[i];
		/* memberExpression.preferSingleLine true: never forced; linePerExpression false */
		if (mi_is_optional(item) || !mi_is_computed(item))
			items_push_cond(&items, if_above_width(INDENT_WIDTH, items_signal(SIG_POSSIBLE_NEW_LINE)));
		if (i == total - 1)
			items_push_ln(&items, last_item_start_ln);
		Items generated = gen_for_member_like_expr_item(c, item, i, total);
		if (mi_is_computed(item))
			items_push_cond(&items, indent_if_start_of_line_or_start_of_line_indented(generated));
		else
			items_push_cond(&items, indent_if_start_of_line(generated));
	}
	return items;
}

static Items gen_member_expr(Ctx *c, Node *n)
{
	MemberItems nodes = {0};
	push_descendant_nodes(n, &nodes);
	return gen_for_flattened_member_like_expr(c, &nodes);
}

static Items gen_call_or_opt_expr(Ctx *c, Node *n)
{
	/* is_test_library_call_expr: not ported (ts-proto emits no test library calls) */
	MemberItems nodes = {0};
	push_descendant_nodes(n, &nodes);
	if (nodes.n > 1)
		return gen_for_flattened_member_like_expr(c, &nodes);
	return gen_call_expr_like(c, n, gen_node(n->a, c));
}

static Items gen_new_expr(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "new ");
	items_extend(&items, gen_node(n->a, c));
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	Range pr;
	bool has = get_params_or_args_range(c, n->lo, n->list.v, n->list.n, n->hi, &pr);
	items_extend(&items, gen_parameters_or_arguments(c, n, has ? &pr : NULL, n->list.v, n->list.n, NULL, NULL, false));
	return items;
}

/* ---- conditional expressions and types ---- */

typedef struct {
	Items previous_lines, leading_line, trailing_line;
} CondTokenComments;

typedef struct {
	IndentLevel il;
} SolSameIndentData;

static int r_indent_if_sol_same_indent(RCtx *rc, void *d)
{
	if (wi_is_start_of_line(&rc->writer_info)) {
		uint32_t il;
		if (!rctx_il(rc, ((SolSameIndentData *)d)->il, &il))
			return -1;
		return rc->writer_info.indent_level == il;
	}
	return 0;
}

static Cond *indent_if_sol_and_same_indent_as_top_most(Items items, IndentLevel il)
{
	PNode *p = items_into_path(items);
	SolSameIndentData *d = xalloc(sizeof *d);
	d->il = il;
	return if_true_or("indentIfSameIndentationLevelAndStartOfLine", resolver(r_indent_if_sol_same_indent, d),
			  with_indent(items_path(p)), items_path(p));
}

static CondTokenComments gen_cond_token_comments(Ctx *c, const Tok *token, IndentLevel top_most_il)
{
	CondTokenComments r = {{0}, {0}, {0}};
	int token_line = end_line(c, trange(token));
	const Tok *prev = src_previous_token(c->src, token->lo);
	const Tok *next = src_next_token(c->src, token->hi);
	int prev_line = end_line(c, trange(prev));
	int next_line = start_line(c, trange(next));
	if (token_line > prev_line) {
		CommentList l = {0};
		Comments lc = leading_comments(c, token->lo);
		CM_FOR(lc, c->src, cm)
		{
			int cl = cstart_line(c, cm);
			if (cl > prev_line && cl < next_line)
				cl_push(&l, cm);
		}
		Comments tc = trailing_comments(c, token->hi);
		CM_FOR(tc, c->src, cm)
		{
			if (cstart_line(c, cm) < next_line)
				cl_push(&l, cm);
		}
		Items it = gen_comments_as_statements(c, l, NULL);
		if (!items_empty(it)) {
			Items n = {0};
			items_push_signal(&n, SIG_NEW_LINE);
			items_push_cond(&n, indent_if_sol_and_same_indent_as_top_most(it, top_most_il));
			r.previous_lines = n;
		}
		if (token_line == next_line)
			r.leading_line = gen_leading_comments_same_line(c, trange(token));
	} else if (token_line < next_line) {
		r.trailing_line = gen_trailing_comments_same_line(c, trange(token));
	}
	return r;
}

/* get_top_most_data of conditional expressions (alternate chain) and types (false chain) */
static void get_cond_top_most(Ctx *c, Node *node, int kind, LineNumber *ln, IndentLevel *il, bool *is_top_most)
{
	Node *top = node;
	for (int i = c->nparents - 1; i >= 0; i--) {
		Node *a = c->parents[i];
		if (a && a->kind == kind) {
			Node *alt = kind == N_COND_EXPR ? a->c : a->d;
			if (alt->lo == top->lo) {
				top = a;
				continue;
			}
		}
		break;
	}
	*is_top_most = top->lo == node->lo && top->hi == node->hi;
	Range key = prange(top->lo);
	if (*is_top_most) {
		*ln = ln_new();
		*il = il_new();
		store_ln_for_node(c, key, *ln);
		store_il_for_node(c, key, *il);
	} else {
		get_ln_for_node(c, key, ln);
		get_il_for_node(c, key, il);
	}
}

static Items gen_conditional_expr(Ctx *c, Node *n)
{
	const Tok *question = get_first_operator_after(c, nrange(n->a), "?");
	const Tok *colon = get_first_operator_after(c, nrange(n->b), ":");
	/* conditionalExpression.preferSingleLine true, linePerExpression true, operators nextLine */
	LineNumber top_ln;
	IndentLevel top_il;
	bool is_top_most;
	get_cond_top_most(c, n, N_COND_EXPR, &top_ln, &top_il, &is_top_most);
	LineNumber before_alternate_ln = ln_new();
	LineNumber end_ln = ln_new();
	CondTokenComments qc = gen_cond_token_comments(c, question, top_il);
	CondTokenComments cc = gen_cond_token_comments(c, colon, top_il);
	Items items = {0};
	if (is_top_most) {
		items_push_ln(&items, top_ln);
		items_push_il(&items, top_il);
	}
	Items test = gen_node(n->a, c);
	items_extend(&test, qc.trailing_line);
	items_extend(&items, new_line_group(with_queued_indent(test)));
	items_extend(&items, qc.previous_lines);
	items_push_anchor(&items, end_ln);
	items_push_anchor(&items, before_alternate_ln);
	Cond *cnd = new_line_if_multiple_lines_space_or_new_line_otherwise(top_ln, &before_alternate_ln);
	ConditionReevaluation reeval = cond_create_reevaluation(cnd);
	items_push_cond(&items, cnd);

	Items ca = {0};
	{
		CommentList l = get_leading_comments_on_previous_lines(c, trange(question));
		Items it = gen_comment_collection(c, l, NULL, NULL);
		if (!items_empty(it))
			items_push_signal(&it, SIG_NEW_LINE);
		items_extend(&ca, it);
	}
	Items cons = {0};
	items_extend(&cons, qc.leading_line);
	items_push_str(&cons, "? ");
	items_extend(&cons, gen_node(n->b, c));
	items_extend(&cons, cc.trailing_line);
	items_push_cond(&ca, indent_if_sol_and_same_indent_as_top_most(new_line_group(cons), top_il));
	items_extend(&ca, cc.previous_lines);
	items_push_cond(&ca, new_line_if_multiple_lines_space_or_new_line_otherwise(top_ln, &before_alternate_ln));
	Items alt = {0};
	items_extend(&alt, cc.leading_line);
	items_push_str(&alt, ": ");
	items_push_ln(&alt, before_alternate_ln);
	items_extend(&alt, gen_node(n->c, c));
	items_push_cond(&ca, indent_if_sol_and_same_indent_as_top_most(new_line_group(alt), top_il));
	items_push_ln(&ca, end_ln);
	items_push_reevaluation(&ca, reeval);
	if (is_top_most)
		items_push_cond(&items, indent_if_start_of_line(ca));
	else
		items_push_cond(&items, indent_if_sol_and_same_indent_as_top_most(ca, top_il));
	return items;
}

static Items gen_conditional_type(Ctx *c, Node *n)
{
	LineNumber top_ln;
	IndentLevel top_il;
	bool is_top_most;
	get_cond_top_most(c, n, N_TS_CONDITIONAL_TYPE, &top_ln, &top_il, &is_top_most);
	bool is_parent_conditional_type = n->parent->kind == N_TS_CONDITIONAL_TYPE;
	Items items = {0};
	LineNumber before_false_ln = ln_new();
	const Tok *question = get_first_operator_after(c, nrange(n->b), "?");
	const Tok *colon = get_first_operator_after(c, nrange(n->c), ":");
	CondTokenComments qc = gen_cond_token_comments(c, question, top_il);
	CondTokenComments cc = gen_cond_token_comments(c, colon, top_il);
	items_extend(&items, new_line_group(gen_node(n->a, c)));
	items_push_str(&items, " extends");
	items_push_signal(&items, SIG_SPACE_OR_NEW_LINE);
	if (is_top_most) {
		items_push_ln(&items, top_ln);
		items_push_il(&items, top_il);
	}
	Items ext = gen_node(n->b, c);
	items_extend(&ext, qc.trailing_line);
	items_push_cond(&items, indent_if_start_of_line(new_line_group(ext)));
	if (items_empty(qc.previous_lines))
		items_push_signal(&items, SIG_SPACE_OR_NEW_LINE);
	else
		items_extend(&items, qc.previous_lines);
	Items inner = {0};
	items_extend(&inner, qc.leading_line);
	items_push_str(&inner, "?");
	items_push_signal(&inner, SIG_SPACE_IF_NOT_TRAILING);
	items_extend(&inner, new_line_group(gen_node(n->c, c)));
	items_extend(&inner, cc.trailing_line);
	PNode *ip = items_into_path(inner);
	items_push_cond(&items, if_true_or("isStartOfLineIndentElseQueue", res_is_start_of_line(), with_indent(items_path(ip)),
					   with_queued_indent(items_path(ip))));
	items_extend(&items, cc.previous_lines);
	items_push_cond(&items, new_line_if_multiple_lines_space_or_new_line_otherwise(top_ln, &before_false_ln));
	{
		CommentList l = get_leading_comments_on_previous_lines(c, trange(colon));
		Items it = gen_comments_as_statements(c, l, NULL);
		if (!items_empty(it))
			items_extend(&items, with_indent(it));
	}
	Items f = {0};
	items_push_ln(&f, before_false_ln);
	items_extend(&f, cc.leading_line);
	items_push_str(&f, ":");
	items_push_signal(&f, SIG_SPACE_IF_NOT_TRAILING);
	items_extend(&f, new_line_group(gen_node(n->d, c)));
	if (is_parent_conditional_type)
		items_extend(&items, f);
	else
		items_push_cond(&items, indent_if_start_of_line(f));
	return items;
}

/* ---- object literal, paren, templates ---- */

static Items gen_object_lit(Ctx *c, Node *n)
{
	Items items = gen_object_like_node(c, n, n->list.v, n->list.n, SEP_COMMA_ONLY_MULTI_LINE, false,
					   is_node_definitely_above_line_width(c, nrange(n)), true, true, NULL);
	if (should_add_parens_around_expr(c, n))
		return surround_with_parens(items);
	return items;
}

static Items gen_paren_expr(Ctx *c, Node *n)
{
	if (should_skip_paren_expr(c, n))
		return gen_node(n->a, c);
	Items g = items_cond(with_indent_if_start_of_line_indented(
		gen_node_in_parens(c, gen_node_fn, n->a, nrange(n->a), true, true, false)));
	bool use_group = n->parent->kind == N_ARROW_EXPR ? use_new_line_group_for_arrow_body(n->parent) : true;
	return use_group ? new_line_group(g) : g;
}

static bool tpl_keep_on_one_line(Node *n);

static bool tpl_keep_call_on_one_line(Node *call) { return call->list.n == 0 && tpl_keep_on_one_line(call->a); }

static bool tpl_keep_member_on_one_line(Node *m)
{
	return tpl_keep_on_one_line(m->a) && tpl_keep_on_one_line(m->b) && m->b->kind != N_COMPUTED_PROP_NAME;
}

static bool tpl_keep_on_one_line(Node *n)
{
	switch (n->kind) {
	case N_IDENT:
	case N_THIS_EXPR:
	case N_SUPER_PROP_EXPR:
	case N_STR:
	case N_PRIVATE_NAME:
		return true;
	case N_OPT_CHAIN_EXPR:
		return tpl_keep_on_one_line(n->a);
	case N_MEMBER_EXPR:
		return tpl_keep_member_on_one_line(n);
	case N_CALL_EXPR:
	case N_OPT_CALL:
		return tpl_keep_call_on_one_line(n);
	default:
		return false;
	}
}

static bool tpl_possible_surround_newlines(Node *n)
{
	switch (n->kind) {
	case N_OPT_CHAIN_EXPR:
		return tpl_possible_surround_newlines(n->a);
	case N_COND_EXPR:
	case N_BIN_EXPR:
		return true;
	case N_MEMBER_EXPR:
		return !tpl_keep_member_on_one_line(n);
	case N_CALL_EXPR:
	case N_OPT_CALL:
		return !tpl_keep_call_on_one_line(n);
	default:
		return false;
	}
}

typedef struct {
	bool is_head, tail;
} TplElemData;

static Items tpl_elem_inner(Items node_items, Ctx *c, void *data)
{
	TplElemData *d = data;
	Items items = {0};
	items_push_str(&items, d->is_head ? "`" : "}");
	items_push_signal(&items, SIG_START_IGNORING_INDENT);
	items_extend(&items, node_items);
	items_push_str(&items, d->tail ? "`" : "${");
	items_push_signal(&items, SIG_FINISH_IGNORING_INDENT);
	return items;
}

static Items gen_template_literal(Ctx *c, Node *n)
{
	Items items = {0};
	bool is_head = true;
	int qi = 0, ei = 0;
	while (qi < n->list2.n || ei < n->list.n) {
		bool is_quasi = qi < n->list2.n && (ei >= n->list.n || n->list2.v[qi]->lo < n->list.v[ei]->lo);
		if (is_quasi) {
			Node *q = n->list2.v[qi++];
			TplElemData *d = xalloc(sizeof *d);
			d->is_head = is_head;
			d->tail = (q->flags & F_TPL_TAIL) != 0;
			items_extend(&items, gen_node_with_inner_gen(q, c, tpl_elem_inner, d));
		} else {
			Node *e = n->list.v[ei++];
			Items leading = gen_comments_as_statements(c, get_leading_comments_on_previous_lines(c, nrange(e)), NULL);
			Items trailing = gen_trailing_comments_as_statements(c, nrange(e));
			Items g = gen_node(e, c);
			if (!items_empty(leading) || !items_empty(trailing)) {
				items_push_signal(&items, SIG_START_INDENT);
				if (!items_empty(leading))
					items_extend(&items, surround_with_new_lines(leading));
				items_extend(&items, g);
				if (!items_empty(trailing))
					items_extend(&items, trailing);
				items_push_signal(&items, SIG_FINISH_INDENT);
			} else if (tpl_keep_on_one_line(e)) {
				items_extend(&items, with_no_new_lines(g));
			} else if (tpl_possible_surround_newlines(e)) {
				items_extend(&items, surround_with_newlines_indented_if_multi_line(new_line_group(g), INDENT_WIDTH));
			} else {
				items_extend(&items, g);
			}
		}
		is_head = false;
	}
	return items;
}

static Items gen_tagged_tpl(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	Items between = gen_comments_between_lines_indented(c, n->a->hi);
	if (!items_empty(between))
		items_extend(&items, between);
	items_push_cond(&items, indent_if_start_of_line(gen_node(n->c, c)));
	return items;
}

static Items gen_as_expr_like(Ctx *c, Node *expr, Node *type)
{
	Items items = gen_node(expr, c);
	const Tok *t = next_token(c, nrange(expr));
	bool is_as = t && tok_text_eq(c, t, "as");
	items_push_str(&items, is_as ? " as" : " satisfies");
	items_push_signal(&items, SIG_SPACE_IF_NOT_TRAILING);
	items_push_cond(&items, with_indent_if_start_of_line_indented(gen_node(type, c)));
	return items;
}

static Items gen_unary_expr(Ctx *c, Node *n)
{
	static const char *const ops[] = {"", "-", "+", "!", "~", "typeof ", "void ", "delete "};
	Items items = {0};
	items_push_str(&items, ops[n->op]);
	items_extend(&items, gen_node(n->a, c));
	return items;
}

static Items gen_update_expr(Ctx *c, Node *n)
{
	const char *op = n->op == TK_PLUSPLUS ? "++" : "--";
	Items items = {0};
	if (n->flags & F_PREFIX)
		items_push_str(&items, op);
	items_extend(&items, gen_node(n->a, c));
	if (!(n->flags & F_PREFIX))
		items_push_str(&items, op);
	return items;
}

static Items gen_seq_expr(Ctx *c, Node *n)
{
	SepValuesParams o = {0};
	o.nodes = n->list.v;
	o.n = n->list.n;
	o.force_use_new_lines = is_node_definitely_above_line_width(c, nrange(n));
	o.separator = SEP_COMMA_NEVER;
	o.single_line_options = slo_same_line_maybe_space_separated();
	o.multi_line_options = mlo_same_line_start_hanging_indent();
	return gen_separated_values_items(c, o);
}

/* ---- statements ---- */

static Items statements_block_fn(Ctx *c, Node **children, int n, void *data)
{
	Node *block = data;
	return gen_statements(c, rng(block->lo + 1, block->hi - 1), children, n);
}

static Items gen_block_stmt(Ctx *c, Node *n)
{
	Range r = nrange(n);
	return gen_block(c, statements_block_fn, n, &r, n->list.v, n->list.n);
}

static Items gen_expr_stmt(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	items_push_str(&items, ";");
	return items;
}

static Items gen_keyword_stmt(Ctx *c, const char *kw, Node *label)
{
	Items items = {0};
	items_push_str(&items, kw);
	if (label) {
		items_push_str(&items, " ");
		items_extend(&items, gen_node(label, c));
	}
	items_push_str(&items, ";");
	return items;
}

static Items gen_return_stmt(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "return");
	if (n->a) {
		items_push_str(&items, " ");
		items_extend(&items, gen_node(n->a, c));
	}
	items_push_str(&items, ";");
	return items;
}

static Items gen_throw_stmt(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "throw ");
	items_extend(&items, gen_node(n->a, c));
	items_push_str(&items, ";");
	return items;
}

static Items gen_if_stmt(Ctx *c, Node *n)
{
	Items items = {0};
	Node *cons = n->b;
	Items header = {0};
	items_push_str(&header, "if ");
	items_extend(&header, gen_node_in_parens(c, gen_node_fn, n->a, nrange(n->a), false, false, false));
	c->has_if_stmt_last_brace_ref = false; /* take_if_stmt_last_brace_condition_ref */
	BraceBodyResult result = gen_header_with_conditional_brace_body(c, cons, header, true);
	LineNumber if_stmt_start_ln = ln_new();
	items_push_ln(&items, if_stmt_start_ln);
	items_extend(&items, result.generated_node);
	if (n->c) {
		Node *alt = n->c;
		items_extend(&items, gen_control_flow_separator(&if_stmt_start_ln, &result.close_brace_condition_ref));
		TokList toks = tokens_outside_children(c, n);
		const Tok *else_kw = NULL;
		for (int i = 0; i < toks.n; i++)
			if (tok_text_eq(c, toks.v[i], "else")) {
				else_kw = toks.v[i];
				break;
			}
		if (!else_kw)
			fail("dprint: expected an else keyword");
		items_extend(&items, gen_leading_comments(c, trange(else_kw)));
		items_extend(&items, gen_leading_comments(c, nrange(alt)));
		items_push_ln(&items, ln_new());
		items_push_lsil(&items, lsil_new());
		items_push_str(&items, "else");
		if (alt->kind == N_IF_STMT) {
			items_push_str(&items, " ");
			items_extend(&items, gen_node(alt, c));
		} else {
			items_extend(&items, gen_conditional_brace_body(c, alt, true, NULL).generated_node);
		}
	}
	return items;
}

static Items gen_labeled_stmt(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	items_push_str(&items, ":");
	if (start_line(c, nrange(n)) == start_line(c, nrange(n->b)))
		items_push_str(&items, " ");
	else
		items_push_signal(&items, SIG_NEW_LINE);
	items_extend(&items, gen_node(n->b, c));
	return items;
}

static Items gen_while_stmt(Ctx *c, Node *n)
{
	LineNumber start_header_ln = ln_new();
	LineStartIndentLevel start_header_lsil = lsil_new();
	LineNumber end_header_ln = ln_new();
	Items items = {0};
	items_push_ln(&items, start_header_ln);
	items_push_lsil(&items, start_header_lsil);
	items_push_str(&items, "while ");
	items_extend(&items, gen_node_in_parens(c, gen_node_fn, n->a, nrange(n->a), false, false, false));
	items_push_ln(&items, end_header_ln);
	items_extend(&items, gen_conditional_brace_body(c, n->d, true, &end_header_ln).generated_node);
	return items;
}

static Items gen_do_while_stmt(Ctx *c, Node *n)
{
	/* not emitted by ts-proto; generated like dprint with default positions */
	Items items = {0};
	items_push_str(&items, "do");
	items_extend(&items, gen_conditional_brace_body(c, n->d, false, NULL).generated_node);
	items_push_str(&items, " while ");
	items_extend(&items, gen_node_in_parens(c, gen_node_fn, n->a, nrange(n->a), false, false, false));
	items_push_str(&items, ";");
	return items;
}

typedef struct {
	Node *left, *right;
	const char *kw;
} ForInOfData;

static Items for_in_of_inner(Ctx *c, void *data)
{
	ForInOfData *d = data;
	Items items = gen_node(d->left, c);
	items_push_signal(&items, SIG_SPACE_OR_NEW_LINE);
	Items r = {0};
	items_push_str(&r, d->kw);
	items_extend(&r, gen_node(d->right, c));
	items_push_cond(&items, indent_if_start_of_line(r));
	return items;
}

static Items gen_for_in_of_stmt(Ctx *c, Node *n)
{
	LineNumber start_header_ln = ln_new();
	LineStartIndentLevel start_header_lsil = lsil_new();
	LineNumber end_header_ln = ln_new();
	Items items = {0};
	items_push_ln(&items, start_header_ln);
	items_push_lsil(&items, start_header_lsil);
	items_push_str(&items, "for ");
	if (n->flags & F_AWAIT)
		items_push_str(&items, "await ");
	ForInOfData *d = xalloc(sizeof *d);
	d->left = n->a;
	d->right = n->b;
	d->kw = n->kind == N_FOR_OF_STMT ? "of " : "in ";
	items_extend(&items, gen_node_in_parens(c, for_in_of_inner, d, rng(n->a->lo, n->b->hi), false, false, false));
	items_push_ln(&items, end_header_ln);
	items_extend(&items, gen_conditional_brace_body(c, n->d, true, &end_header_ln).generated_node);
	return items;
}

typedef struct {
	GeneratedValues values;
} ForValuesData;

static GeneratedValues for_values_fn(ConditionReference ref, void *data) { return ((ForValuesData *)data)->values; }

static Items for_header_inner(Ctx *c, void *data)
{
	GenSeparatedValuesOptions o = {0};
	o.indent_width = INDENT_WIDTH;
	o.single_line_options = slo_separated_same_line(items_signal(SIG_SPACE_OR_NEW_LINE));
	o.multi_line_options = mlo_same_line_no_indent();
	return gen_separated_values(for_values_fn, data, o).items;
}

static Items gen_for_stmt(Ctx *c, Node *n)
{
	LineNumber start_header_ln = ln_new();
	LineStartIndentLevel start_header_lsil = lsil_new();
	LineNumber end_header_ln = ln_new();
	Range first_inner, last_inner;
	if (n->a) {
		first_inner = nrange(n->a);
	} else {
		const Tok *semi = get_first_token_within(c, nrange(n), TK_SEMI);
		first_inner = trange(semi);
	}
	if (n->c)
		last_inner = nrange(n->c);
	else if (n->b)
		last_inner = trange(get_first_semi_colon_after(c, nrange(n->b)));
	else if (n->a)
		last_inner = trange(get_first_semi_colon_after(c, trange(get_first_semi_colon_after(c, nrange(n->a)))));
	else
		last_inner = trange(get_first_semi_colon_after(c, first_inner));
	Items items = {0};
	items_push_ln(&items, start_header_ln);
	items_push_lsil(&items, start_header_lsil);
	items_push_str(&items, "for ");
	Items init = {0};
	if (n->a)
		items_extend(&init, gen_node(n->a, c));
	items_push_str(&init, ";");
	if (!n->b)
		items_push_str(&init, ";");
	ForValuesData *d = xalloc(sizeof *d);
	gv_push(&d->values, (GeneratedValue){new_line_group(init), {0}, false, false});
	if (n->b) {
		Items t = gen_node(n->b, c);
		items_push_str(&t, ";");
		gv_push(&d->values, (GeneratedValue){new_line_group(t), {0}, false, false});
	}
	if (n->c)
		gv_push(&d->values, (GeneratedValue){new_line_group(gen_node(n->c, c)), {0}, false, false});
	items_extend(&items, gen_node_in_parens(c, for_header_inner, d, rng(first_inner.lo, last_inner.hi), false, false, false));
	items_push_ln(&items, end_header_ln);
	items_extend(&items, gen_conditional_brace_body(c, n->d, true, &end_header_ln).generated_node);
	return items;
}

static bool switch_case_blank_line(Ctx *c, Node *prev, Node *next)
{
	if (prev->kind == N_SWITCH_CASE && prev->list.n == 0)
		return false;
	Comments lc = leading_comments(c, next->lo);
	const Comment *last = cm_last(&lc, c->src);
	int previous_end_line = last ? cend_line(c, last) : end_line(c, nrange(prev));
	return previous_end_line + 1 < start_line(c, nrange(next));
}

static Items gen_switch_stmt(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_lsil(&items, lsil_new());
	items_push_str(&items, "switch ");
	items_extend(&items, gen_node_in_parens(c, gen_node_fn, n->a, nrange(n->a), false, false, false));
	items_extend(&items, gen_membered_body(c, n, n->list.v, n->list.n, switch_case_blank_line, SEP_NONE));
	return items;
}

static int column_of(Ctx *c, uint32_t pos)
{
	int line = line_of(c, pos);
	uint32_t ls = c->src->line_starts[line];
	int col = 0;
	for (uint32_t i = ls; i < pos;) {
		uint32_t ch;
		i += (uint32_t)utf8_decode(c->src->text + i, pos - i, &ch);
		col++;
	}
	return col;
}

static Items gen_switch_case(Ctx *c, Node *n)
{
	bool block_body = n->list.n == 1 && n->list.v[0]->kind == N_BLOCK_STMT;
	Items items = {0};
	const Tok *colon = get_first_colon_token_after(c, n->a ? prange(n->a->hi) : prange(n->lo));
	if (n->a) {
		items_push_str(&items, "case ");
		items_extend(&items, gen_node(n->a, c));
		items_push_str(&items, ":");
	} else {
		items_push_str(&items, "default:");
	}
	items_extend(&items, gen_trailing_comments_same_line(c, trange(colon)));
	/* gen_trailing_comments_for_case */
	Items generated_trailing = {0};
	{
		Range node_range = nrange(n);
		CommentList tc = get_trailing_comments_as_statements(c, node_range);
		if (tc.n > 0) {
			Node *sw = n->parent;
			Node *last_case = sw->list.n ? sw->list.v[sw->list.n - 1] : NULL;
			bool is_last_case = last_case && last_case->lo == node_range.lo;
			bool is_equal_indent = block_body;
			Range last_range = node_range;
			int last_node_column = column_of(c, node_range.lo);
			for (int i = 0; i < tc.n; i++) {
				const Comment *cm = tc.v[i];
				is_equal_indent = is_equal_indent || column_of(c, cm->lo) <= last_node_column;
				Items g = gen_comment_based_on_last_node(c, cm, &last_range, true);
				items_extend(&generated_trailing, (!is_last_case && is_equal_indent) ? g : with_indent(g));
				last_range = rng(cm->lo, cm->hi);
			}
		}
	}
	if (n->list.n > 0) {
		if (block_body) {
			items_extend(&items, gen_brace_separator());
			items_extend(&items, gen_node(n->list.v[0], c));
		} else {
			items_push_signal(&items, SIG_NEW_LINE);
			items_extend(&items, with_indent(gen_statements(c, rng(colon->hi, n->hi), n->list.v, n->list.n)));
		}
	}
	items_extend(&items, generated_trailing);
	return items;
}

static Items gen_try_stmt(Ctx *c, Node *n)
{
	Items items = {0};
	LineNumber last_block_start_ln = ln_new();
	items_push_ln(&items, last_block_start_ln);
	items_push_str(&items, "try");
	items_extend(&items, gen_conditional_brace_body(c, n->a, true, NULL).generated_node);
	if (n->b) {
		LineNumber handler_start_ln = ln_new();
		items_push_ln(&items, handler_start_ln);
		items_extend(&items, gen_control_flow_separator(&last_block_start_ln, NULL));
		items_extend(&items, gen_node(n->b, c));
		last_block_start_ln = handler_start_ln;
	}
	if (n->c) {
		items_extend(&items, gen_control_flow_separator(&last_block_start_ln, NULL));
		items_push_str(&items, "finally");
		items_extend(&items, gen_conditional_brace_body(c, n->c, true, NULL).generated_node);
	}
	return items;
}

static Items gen_catch_clause(Ctx *c, Node *n)
{
	LineNumber start_header_ln = ln_new();
	LineStartIndentLevel start_header_lsil = lsil_new();
	LineNumber end_header_ln = ln_new();
	Items items = {0};
	items_push_ln(&items, start_header_ln);
	items_push_lsil(&items, start_header_lsil);
	items_push_str(&items, "catch");
	if (n->a) {
		items_push_str(&items, " (");
		items_extend(&items, gen_node(n->a, c));
		items_push_str(&items, ")");
	}
	items_push_ln(&items, end_header_ln);
	bool has_finalizer = n->parent->c != NULL;
	items_extend(&items, gen_conditional_brace_body(c, n->b, has_finalizer, &end_header_ln).generated_node);
	return items;
}

static Items gen_var_decl(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_DECLARE)
		items_push_str(&items, "declare ");
	items_push_str(&items, n->op == VAR_CONST ? "const " : n->op == VAR_LET ? "let " : "var ");
	if (n->list.n == 1) {
		items_extend(&items, gen_node(n->list.v[0], c));
	} else if (n->list.n > 1) {
		SepValuesParams o = {0};
		o.nodes = n->list.v;
		o.n = n->list.n;
		o.force_use_new_lines = get_use_new_lines_for_nodes(c, n->list.v, n->list.n, true) ||
					(n->list.n >= 2 && is_node_definitely_above_line_width(c, nrange(n)));
		o.separator = SEP_COMMA_NEVER;
		o.single_line_options = slo_same_line_maybe_space_separated();
		o.multi_line_options = mlo_same_line_start_hanging_indent();
		items_extend(&items, gen_separated_values_items(c, o));
	}
	Node *p = n->parent;
	bool requires_semi = true;
	if (p->kind == N_FOR_IN_STMT || p->kind == N_FOR_OF_STMT || p->kind == N_FOR_STMT)
		requires_semi = n->lo >= p->d->lo;
	if (requires_semi)
		items_push_str(&items, ";");
	return items;
}

static Items gen_var_declarator(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	if (n->b)
		items_extend(&items, gen_assignment(c, n->b, "="));
	Node *p = n->parent;
	if (p->kind == N_VAR_DECL && p->list.n > 1 && p->list.v[0] == n) {
		PNode *path = items_into_path(items);
		return items_cond(if_true_or("indentIfNotStartOfLine", res_is_not_start_of_line(), with_indent(items_path(path)),
					     items_path(path)));
	}
	return items;
}

/* ---- patterns ---- */

static Items gen_binding_ident(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	if (n->parent->kind == N_VAR_DECLARATOR && (n->parent->flags & F_DEFINITE))
		items_push_str(&items, "!");
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->b));
	return items;
}

static Items gen_array_pat(Ctx *c, Node *n)
{
	Items items = gen_array_like_nodes(c, n, n->list.v, n->list.n);
	if (n->flags & F_OPTIONAL)
		items_push_str(&items, "?");
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->b));
	return items;
}

static Items gen_object_pat(Ctx *c, Node *n)
{
	int sep = SEP_COMMA_ONLY_MULTI_LINE;
	if (n->list.n > 0 && n->list.v[n->list.n - 1]->kind == N_REST_PAT)
		sep = SEP_COMMA_NEVER;
	Items items = gen_object_like_node(c, n, n->list.v, n->list.n, sep, false,
					   is_node_definitely_above_line_width(c, nrange(n)), true, true, NULL);
	if (n->flags & F_OPTIONAL)
		items_push_str(&items, "?");
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->b));
	return items;
}

static Items gen_assign_like(Ctx *c, Node *n, const char *op)
{
	Items items = gen_node(n->a, c);
	if (n->b)
		items_extend(&items, gen_assignment(c, n->b, op));
	return items;
}

static Items gen_rest_pat(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "...");
	items_extend(&items, gen_node(n->a, c));
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->b));
	return items;
}

/* ---- type members ---- */

static Items gen_method_signature_like(Ctx *c, Node *node, int kind, bool computed, bool optional, Node *key,
				       Node *type_params, const Range *pr, Node **params, int nparams, Node *type_ann)
{
	Items items = {0};
	LineStartIndentLevel start_lsil = lsil_new();
	items_push_lsil(&items, start_lsil);
	if (kind == CM_GETTER)
		items_push_str(&items, "get ");
	else if (kind == CM_SETTER)
		items_push_str(&items, "set ");
	if (computed)
		items_extend(&items, gen_computed_prop_like(c, gen_node_fn, key, nrange(key)));
	else
		items_extend(&items, gen_quotable_prop(c, key));
	if (optional)
		items_push_str(&items, "?");
	if (type_params)
		items_extend(&items, gen_node(type_params, c));
	items_extend(&items, gen_parameters_or_arguments(c, node, pr, params, nparams, close_paren_with_type_fn,
							 close_args(start_lsil, type_ann, nparams), true));
	return items;
}

static Node *computed_key_inner(Node *key) { return key->kind == N_COMPUTED_PROP_NAME ? key->a : key; }

static Items gen_method_signature(Ctx *c, Node *n)
{
	Range pr;
	bool has = params_range(c, n->lo, &n->list, n->c ? n->c->lo : n->hi, &pr);
	bool computed = (n->flags & F_COMPUTED) != 0;
	return gen_method_signature_like(c, n, CM_METHOD, computed, (n->flags & F_OPTIONAL) != 0,
					 computed ? computed_key_inner(n->a) : n->a, n->b, has ? &pr : NULL, n->list.v, n->list.n,
					 n->c);
}

static Items gen_getter_setter_signature(Ctx *c, Node *n)
{
	Range pr;
	bool getter = n->kind == N_TS_GETTER_SIGNATURE;
	Node *params[1] = {n->b};
	Node **pv = NULL;
	int np = 0;
	if (!getter) {
		pv = xalloc(sizeof(Node *));
		pv[0] = n->b;
		np = 1;
	}
	bool has = get_params_or_args_range(c, n->lo, getter ? NULL : params, np, getter && n->c ? n->c->lo : n->hi, &pr);
	bool computed = (n->flags & F_COMPUTED) != 0;
	return gen_method_signature_like(c, n, getter ? CM_GETTER : CM_SETTER, computed, (n->flags & F_OPTIONAL) != 0,
					 computed ? computed_key_inner(n->a) : n->a, NULL, has ? &pr : NULL, pv, np,
					 getter ? n->c : NULL);
}

static Items gen_signature_decl(Ctx *c, Node *n, bool construct)
{
	Items items = {0};
	LineStartIndentLevel start_lsil = lsil_new();
	items_push_lsil(&items, start_lsil);
	if (construct)
		items_push_str(&items, "new "); /* constructSignature.spaceAfterNewKeyword */
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	Range pr;
	bool has = params_range(c, n->lo, &n->list, n->c ? n->c->lo : n->hi, &pr);
	items_extend(&items, gen_parameters_or_arguments(c, n, has ? &pr : NULL, n->list.v, n->list.n, close_paren_with_type_fn,
							 close_args(start_lsil, n->c, n->list.n), true));
	return items;
}

static Items gen_index_signature(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_STATIC)
		items_push_str(&items, "static ");
	if (n->flags & F_READONLY)
		items_push_str(&items, "readonly ");
	Node *param = n->list.v[0];
	items_extend(&items, gen_computed_prop_like(c, gen_node_fn, param, nrange(param)));
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->c));
	if (n->parent && n->parent->kind == N_CLASS)
		items_push_str(&items, ";");
	return items;
}

static Items gen_property_signature(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_READONLY)
		items_push_str(&items, "readonly ");
	if (n->flags & F_COMPUTED)
		items_extend(&items, gen_computed_prop_like(c, gen_node_fn, n->a->a, nrange(n->a->a)));
	else
		items_extend(&items, gen_quotable_prop(c, n->a));
	if (n->flags & F_OPTIONAL)
		items_push_str(&items, "?");
	items_extend(&items, gen_type_ann_with_colon_if_exists(c, n->c));
	return items;
}

static Items gen_type_lit(Ctx *c, Node *n)
{
	return gen_object_like_node(c, n, n->list.v, n->list.n, SEP_SEMI_PREFER, false, false, true, true, NULL);
}

/* ---- types ---- */

static Items gen_function_type(Ctx *c, Node *n, bool constructor)
{
	LineStartIndentLevel start_lsil = lsil_new();
	Items items = {0};
	Cond *indent_after_arrow = NULL;
	ConditionReference ref = {0};
	if (!constructor) {
		indent_after_arrow = if_true("indentIfIsStartOfLineAfterArrow", res_is_start_of_line(), items_signal(SIG_START_INDENT));
		ref = cond_create_reference(indent_after_arrow);
	}
	items_push_lsil(&items, start_lsil);
	if (constructor) {
		if (n->flags & F_ABSTRACT)
			items_push_str(&items, "abstract ");
		items_push_str(&items, "new "); /* constructorType.spaceAfterNewKeyword */
	}
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	Range pr;
	bool has = params_range(c, n->lo, &n->list, n->c->lo, &pr);
	CloseParenArgs *a = close_args(start_lsil, n->c, n->list.n);
	a->has_separator = true;
	a->indent_after_arrow = indent_after_arrow;
	items_extend(&items, gen_parameters_or_arguments(c, n, has ? &pr : NULL, n->list.v, n->list.n, close_paren_with_type_fn, a, true));
	if (!constructor)
		items_push_cond(&items, if_true("shouldFinishIndent", condref_resolver(ref), items_signal(SIG_FINISH_INDENT)));
	return items;
}

static Items gen_indexed_access_type(Ctx *c, Node *n)
{
	Items items = gen_node(n->a, c);
	items_extend(&items, gen_computed_prop_like(c, gen_node_fn, n->b, nrange(n->b)));
	return items;
}

typedef struct {
	Node *node;
	bool force_use_new_lines;
	LineNumber start_ln;
} MappedData;

static int r_on_different_line(RCtx *rc, void *d) { return ch_is_on_different_line(rc, *(LineNumber *)d); }

typedef struct {
	Node *type_param, *name_type;
} MappedKeyData;

static Items mapped_key_inner(Ctx *c, void *data)
{
	MappedKeyData *d = data;
	if (d->name_type)
		return gen_as_expr_like(c, d->type_param, d->name_type);
	return gen_node(d->type_param, c);
}

static Items mapped_inner(Ctx *c, void *data)
{
	MappedData *d = data;
	Node *node = d->node;
	LineNumber *lnp = xalloc(sizeof *lnp);
	*lnp = d->start_ln;
	Resolver is_different = d->force_use_new_lines ? res_true() : resolver(r_on_different_line, lnp);
	Items items = {0};
	int first, last;
	src_tokens_in_range(c->src, node->lo, node->hi, &first, &last);
	Items semi_comments = gen_trailing_comments(c, trange(&c->src->toks[last - 2]));
	if (node->mapped_readonly == MAPPED_TRUE)
		items_push_str(&items, "readonly ");
	else if (node->mapped_readonly == MAPPED_PLUS)
		items_push_str(&items, "+readonly ");
	else if (node->mapped_readonly == MAPPED_MINUS)
		items_push_str(&items, "-readonly ");
	Range cir = rng(node->a->lo, node->b ? node->b->hi : node->a->hi);
	MappedKeyData *kd = xalloc(sizeof *kd);
	kd->type_param = node->a;
	kd->name_type = node->b;
	items_extend(&items, gen_computed_prop_like(c, mapped_key_inner, kd, cir));
	if (node->mapped_optional == MAPPED_TRUE)
		items_push_str(&items, "?");
	else if (node->mapped_optional == MAPPED_PLUS)
		items_push_str(&items, "+?");
	else if (node->mapped_optional == MAPPED_MINUS)
		items_push_str(&items, "-?");
	if (node->c)
		items_extend(&items, gen_type_ann_with_colon_for_type(c, node->c));
	items_push_cond(&items, if_true("semiColonIfMultiLine", is_different, items_str(";")));
	items_extend(&items, semi_comments);
	PNode *ip = items_into_path(items);
	Items f = {0};
	items_push_signal(&f, SIG_SPACE_OR_NEW_LINE);
	items_push_path(&f, ip);
	items_push_signal(&f, SIG_SPACE_OR_NEW_LINE);
	Items inner = items_cond(if_true_or("noSpacesWhenMultiLine", is_different, items_path(ip), f));
	if (d->force_use_new_lines)
		return surround_with_new_lines(with_indent(inner));
	return surround_with_newlines_indented_if_multi_line(inner, INDENT_WIDTH);
}

static Items gen_mapped_type(Ctx *c, Node *n)
{
	Items items = {0};
	MappedData *d = xalloc(sizeof *d);
	d->node = n;
	d->force_use_new_lines = false; /* mappedType.preferSingleLine */
	d->start_ln = ln_new();
	items_push_ln(&items, d->start_ln);
	SurroundedOpts so = {0};
	so.open_token = "{";
	so.close_token = "}";
	so.has_range = true;
	so.range = nrange(n);
	so.has_first_member = true;
	so.first_member = nrange(n->a);
	so.prefer_single_line_when_empty = false;
	so.allow_open_token_trailing_comments = true;
	items_extend(&items, gen_surrounded_by_tokens(c, mapped_inner, d, NULL, NULL, so));
	return items;
}

static bool should_skip_parenthesized_type(Ctx *c, Node *n)
{
	if (has_surrounding_different_line_comments(c, n->a))
		return false;
	return n->parent->kind == N_TS_TYPE_ANN || n->parent->kind == N_TS_TYPE_ALIAS_DECL;
}

static Items gen_parenthesized_type(Ctx *c, Node *n)
{
	if (should_skip_parenthesized_type(c, n))
		return gen_node(n->a, c);
	Items g = items_cond(with_indent_if_start_of_line_indented(
		gen_node_in_parens(c, gen_node_fn, n->a, nrange(n->a), true, true, false)));
	return n->parent->kind != N_TS_TYPE_ALIAS_DECL ? new_line_group(g) : g;
}

static Items gen_tuple_element(Ctx *c, Node *n)
{
	if (n->a) {
		Items items = gen_node(n->a, c);
		items_extend(&items, gen_type_ann_with_colon_for_type(c, n->b));
		return items;
	}
	return gen_node(n->b, c);
}

static Items gen_type_param(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_CONST)
		items_push_str(&items, "const ");
	if (n->flags & F_IN)
		items_push_str(&items, "in ");
	if (n->flags & F_OUT)
		items_push_str(&items, "out ");
	items_extend(&items, gen_node(n->a, c));
	if (n->b) {
		items_push_signal(&items, SIG_SPACE_OR_NEW_LINE);
		Items ci = {0};
		items_push_str(&ci, n->parent->kind == N_TS_MAPPED_TYPE ? "in" : "extends");
		items_push_signal(&ci, SIG_SPACE_IF_NOT_TRAILING);
		items_extend(&ci, gen_node(n->b, c));
		items_push_cond(&items, indent_if_start_of_line(ci));
	}
	if (n->c)
		items_extend(&items, gen_assignment(c, n->c, "="));
	return items;
}

/* NodeExtensions::get_type_parameters */
static Node *get_type_parameters(Node *p)
{
	switch (p->kind) {
	case N_CLASS_DECL:
	case N_CLASS_EXPR:
		return p->b->b;
	case N_CLASS:
	case N_TS_INTERFACE_DECL:
	case N_TS_TYPE_ALIAS_DECL:
	case N_FUNCTION:
	case N_ARROW_EXPR:
	case N_TS_CALL_SIGNATURE_DECL:
	case N_TS_CONSTRUCT_SIGNATURE_DECL:
	case N_TS_METHOD_SIGNATURE:
	case N_TS_CONSTRUCTOR_TYPE:
	case N_TS_FN_TYPE:
		return p->b;
	case N_FN_DECL:
	case N_CLASS_METHOD:
	case N_METHOD_PROP:
		return p->b->b;
	default:
		return NULL;
	}
}

static Items gen_type_parameters(Ctx *c, Node *n)
{
	Items items = {0};
	int sep = SEP_COMMA_NEVER;
	Node *tp = n->parent ? get_type_parameters(n->parent) : NULL;
	if (tp && tp->lo == n->lo)
		sep = SEP_COMMA_ONLY_MULTI_LINE;
	items_push_str(&items, "<");
	SepValuesParams o = {0};
	o.nodes = n->list.v;
	o.n = n->list.n;
	o.separator = sep;
	o.single_line_options = slo_same_line_maybe_space_separated();
	o.multi_line_options = mlo_surround_newlines_indented();
	items_extend(&items, gen_separated_values_items(c, o));
	items_push_str(&items, ">");
	return items;
}

static Items gen_type_operator(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, n->op == TYPEOP_KEYOF ? "keyof" : n->op == TYPEOP_UNIQUE ? "unique" : "readonly");
	items_push_signal(&items, SIG_SPACE_IF_NOT_TRAILING);
	items_extend(&items, gen_node(n->a, c));
	return items;
}

static Items gen_type_predicate(Ctx *c, Node *n)
{
	Items items = {0};
	if (n->flags & F_ASSERTS)
		items_push_str(&items, "asserts ");
	items_extend(&items, gen_node(n->a, c));
	if (n->b) {
		items_push_str(&items, " is");
		items_push_signal(&items, SIG_SPACE_IF_NOT_TRAILING);
		items_extend(&items, gen_node(n->b, c));
	}
	return items;
}

static Items gen_type_query(Ctx *c, Node *n)
{
	Items items = {0};
	items_push_str(&items, "typeof");
	items_push_signal(&items, SIG_SPACE_IF_NOT_TRAILING);
	items_extend(&items, gen_node(n->a, c));
	if (n->b)
		items_extend(&items, gen_node(n->b, c));
	return items;
}

typedef struct {
	LineNumber after_separator_ln;
	LineAndColumn start_lc;
} AfterSepData;

static int r_after_separator_space(RCtx *rc, void *d)
{
	AfterSepData *x = d;
	int same_line = ch_is_on_same_line(rc, x->after_separator_ln);
	if (same_line < 0)
		return -1;
	int same_pos = ch_is_at_same_position(rc, x->start_lc);
	if (same_pos < 0)
		return -1;
	return same_line && !same_pos;
}

typedef struct {
	Ctx *c;
	Node *node;
	bool is_union, is_parent_union_or_intersection;
} UnionData;

static GeneratedValues union_values_fn(ConditionReference ref, void *data)
{
	UnionData *d = data;
	Ctx *c = d->c;
	Resolver is_multi_line = condref_resolver(ref);
	const char *separator = d->is_union ? "|" : "&";
	GeneratedValues values = {0};
	int count = d->node->list.n;
	for (int i = 0; i < count; i++) {
		Node *t = d->node->list.v[i];
		bool allow_multi = allows_inline_multi_line(c, t, count > 1);
		bool allow_single = i + 1 == count;
		const Tok *sep_tok = get_previous_token_if_text(c, nrange(t), separator);
		AfterSepData *x = xalloc(sizeof *x);
		x->start_lc = lc_new();
		x->after_separator_ln = ln_new();
		Items items = {0};
		items_push_lc(&items, x->start_lc);
		if (sep_tok)
			items_extend(&items, gen_leading_comments(c, trange(sep_tok)));
		if (i == 0 && !d->is_parent_union_or_intersection)
			items_push_cond(&items, if_true("separatorIfMultiLine", is_multi_line, items_str(separator)));
		else if (i > 0)
			items_push_str(&items, separator);
		if (sep_tok)
			items_extend(&items, gen_trailing_comments(c, trange(sep_tok)));
		items_push_ln(&items, x->after_separator_ln);
		items_push_cond(&items, if_true("afterSeparatorSpace", resolver(r_after_separator_space, x),
						items_signal(SIG_SPACE_IF_NOT_TRAILING)));
		items_extend(&items, gen_node(t, c));
		gv_push(&values, (GeneratedValue){items, {0}, allow_multi, allow_single});
	}
	return values;
}

static Items gen_union_or_intersection_type(Ctx *c, Node *n, bool is_union)
{
	bool force = get_use_new_lines_for_nodes(c, n->list.v, n->list.n, true);
	Node *parent = n->parent;
	bool is_parent_ui = parent->kind == N_TS_UNION_TYPE || parent->kind == N_TS_INTERSECTION_TYPE;
	MultiLineOptions mlo;
	if (!is_parent_ui) {
		bool surround = parent->kind == N_TS_TYPE_ASSERTION ||
				(parent->kind == N_TS_PARENTHESIZED_TYPE && !should_skip_parenthesized_type(c, parent));
		mlo = surround ? mlo_surround_newlines_indented() : mlo_new_line_start();
	} else {
		mlo = mlo_same_line_start_hanging_indent();
	}
	UnionData *d = xalloc(sizeof *d);
	d->c = c;
	d->node = n;
	d->is_union = is_union;
	d->is_parent_union_or_intersection = is_parent_ui;
	GenSeparatedValuesOptions o = {0};
	o.force_use_new_lines = force;
	o.indent_width = INDENT_WIDTH;
	o.single_line_options = slo_separated_same_line(items_signal(SIG_SPACE_OR_NEW_LINE));
	o.multi_line_options = mlo;
	return gen_separated_values(union_values_fn, d, o).items;
}

/* ---- dispatch (gen_node_inner) ---- */

static Items gen_node_inner(Node *node, Ctx *c)
{
	Node *n = node;
	switch (n->kind) {
	case N_CLASS_METHOD:
		return gen_class_method(c, n);
	case N_CLASS_PROP:
		return gen_class_prop(c, n);
	case N_CONSTRUCTOR:
		return gen_constructor(c, n);
	case N_TS_PARAM_PROP:
		return gen_parameter_prop(c, n);
	case N_PRIVATE_NAME:
		return gen_prefixed(c, "#", n->a);
	case N_CATCH_CLAUSE:
		return gen_catch_clause(c, n);
	case N_COMPUTED_PROP_NAME:
		return gen_computed_prop_like(c, gen_node_fn, n->a, nrange(n->a));
	case N_IDENT:
		return gen_identifier(c, n);
	case N_BINDING_IDENT:
		return gen_binding_ident(c, n);
	case N_CLASS_DECL:
		return gen_class_decl_or_expr(c, n, false);
	case N_CLASS_EXPR:
		return gen_class_decl_or_expr(c, n, true);
	case N_EXPORT_DECL:
		return gen_export_decl(c, n);
	case N_EXPORT_DEFAULT_EXPR:
		return gen_export_default_expr(c, n);
	case N_FN_DECL:
		return gen_function_decl_or_expr(c, n, true, n->a, (n->flags & F_DECLARE) != 0, n->b);
	case N_FN_EXPR: {
		Items items = gen_function_decl_or_expr(c, n, false, n->a, false, n->b);
		return should_add_parens_around_expr(c, n) ? surround_with_parens(items) : items;
	}
	case N_IMPORT_DECL:
		return gen_import_decl(c, n);
	case N_TS_IMPORT_EQUALS_DECL: {
		/* gen_import_equals_decl (semiColons: prefer) */
		Items items = {0};
		items_push_str(&items, "import ");
		if (n->flags & F_TYPE_ONLY)
			items_push_str(&items, "type ");
		items_extend(&items, gen_node(n->a, c));
		items_push_str(&items, " = ");
		items_extend(&items, gen_node(n->b, c));
		items_push_str(&items, ";");
		return items;
	}
	case N_TS_EXTERNAL_MODULE_REF: {
		/* gen_external_module_ref */
		Items items = {0};
		items_push_str(&items, "require(");
		items_extend(&items, gen_node(n->a, c));
		items_push_str(&items, ")");
		return items;
	}
	case N_NAMED_EXPORT:
		return gen_export_named_decl(c, n);
	case N_EXPORT_ALL:
		return gen_export_all(c, n);
	case N_PARAM:
		return gen_node(n->a, c);
	case N_TS_ENUM_DECL:
		return gen_enum_decl(c, n);
	case N_TS_ENUM_MEMBER:
		return gen_enum_member(c, n);
	case N_TS_INTERFACE_DECL:
		return gen_interface_decl(c, n);
	case N_TS_MODULE_DECL:
		return gen_module_decl(c, n);
	case N_TS_TYPE_ALIAS_DECL:
		return gen_type_alias(c, n);
	case N_ARRAY_LIT:
		return gen_array_like_nodes(c, n, n->list.v, n->list.n);
	case N_ARROW_EXPR:
		return gen_arrow_func_expr(c, n);
	case N_ASSIGN_EXPR:
		return gen_assign_expr(c, n);
	case N_AWAIT_EXPR:
		return gen_prefixed(c, "await ", n->a);
	case N_BIN_EXPR:
		return gen_binary_expr(c, n);
	case N_CALL_EXPR:
	case N_OPT_CALL:
		return gen_call_or_opt_expr(c, n);
	case N_COND_EXPR:
		return gen_conditional_expr(c, n);
	case N_EXPR_OR_SPREAD:
		return (n->flags & F_SPREAD) ? gen_prefixed(c, "...", n->a) : gen_node(n->a, c);
	case N_KEY_VALUE_PROP: {
		Items items = gen_quotable_prop(c, n->a);
		items_extend(&items, gen_assignment(c, n->b, ":"));
		return items;
	}
	case N_ASSIGN_PROP: {
		Items items = gen_node(n->a, c);
		items_extend(&items, gen_assignment_like_with_token(c, n->b, ":", previous_token(c, nrange(n->b))));
		return items;
	}
	case N_GETTER_PROP:
		return gen_getter_prop(c, n);
	case N_SETTER_PROP:
		return gen_setter_prop(c, n);
	case N_METHOD_PROP:
		return gen_method_prop(c, n);
	case N_MEMBER_EXPR:
		return gen_member_expr(c, n);
	case N_NEW_EXPR:
		return gen_new_expr(c, n);
	case N_OBJECT_LIT:
		return gen_object_lit(c, n);
	case N_OPT_CHAIN_EXPR:
		return gen_node(n->a, c);
	case N_PAREN_EXPR:
		return gen_paren_expr(c, n);
	case N_SEQ_EXPR:
		return gen_seq_expr(c, n);
	case N_SPREAD_ELEMENT:
		return gen_prefixed(c, "...", n->a);
	case N_SUPER:
		return items_str("super");
	case N_TAGGED_TPL:
		return gen_tagged_tpl(c, n);
	case N_THIS_EXPR:
		return items_str("this");
	case N_TPL:
		return gen_template_literal(c, n);
	case N_TPL_ELEMENT: {
		Str t = ntext(c, n);
		return gen_from_raw_string(t.p, t.n);
	}
	case N_TS_AS_EXPR:
	case N_TS_SATISFIES_EXPR:
		return gen_as_expr_like(c, n->a, n->b);
	case N_TS_CONST_ASSERTION: {
		Items items = gen_node(n->a, c);
		items_push_str(&items, " as const");
		return items;
	}
	case N_TS_EXPR_WITH_TYPE_ARGS: {
		Items items = gen_node(n->a, c);
		if (n->b)
			items_extend(&items, gen_node(n->b, c));
		return items;
	}
	case N_TS_NON_NULL_EXPR: {
		Items items = gen_node(n->a, c);
		items_push_str(&items, "!");
		return items;
	}
	case N_TS_TYPE_ASSERTION: {
		Items items = {0};
		items_push_str(&items, "<");
		items_extend(&items, gen_node(n->b, c));
		items_push_str(&items, "> ");
		items_extend(&items, gen_node(n->a, c));
		return items;
	}
	case N_UNARY_EXPR:
		return gen_unary_expr(c, n);
	case N_UPDATE_EXPR:
		return gen_update_expr(c, n);
	case N_YIELD_EXPR: {
		Items items = {0};
		items_push_str(&items, "yield");
		if (n->flags & F_DELEGATE)
			items_push_str(&items, "*");
		if (n->a) {
			items_push_str(&items, " ");
			items_extend(&items, gen_node(n->a, c));
		}
		return items;
	}
	case N_EXPORT_NAMED_SPECIFIER:
		return gen_export_named_specifier(c, n);
	case N_IMPORT_NAMED_SPECIFIER:
		return gen_import_named_specifier(c, n);
	case N_IMPORT_STAR_AS_SPECIFIER:
	case N_EXPORT_NAMESPACE_SPECIFIER:
		return gen_prefixed(c, "* as ", n->a);
	case N_IMPORT_DEFAULT_SPECIFIER:
		return gen_node(n->a, c);
	case N_TS_CALL_SIGNATURE_DECL:
		return gen_signature_decl(c, n, false);
	case N_TS_CONSTRUCT_SIGNATURE_DECL:
		return gen_signature_decl(c, n, true);
	case N_TS_INDEX_SIGNATURE:
		return gen_index_signature(c, n);
	case N_TS_INTERFACE_BODY:
		return gen_interface_body(c, n);
	case N_TS_METHOD_SIGNATURE:
		return gen_method_signature(c, n);
	case N_TS_PROPERTY_SIGNATURE:
		return gen_property_signature(c, n);
	case N_TS_GETTER_SIGNATURE:
	case N_TS_SETTER_SIGNATURE:
		return gen_getter_setter_signature(c, n);
	case N_TS_TYPE_LIT:
		return gen_type_lit(c, n);
	case N_BIGINT:
	case N_NUMBER:
		return gen_text(c, n);
	case N_BOOL:
		return gen_text(c, n);
	case N_NULL:
		return items_str("null");
	case N_REGEX:
		return gen_text(c, n);
	case N_STR:
		return gen_string_literal(c, n);
	case N_MODULE:
		return gen_statements(c, nrange(n), n->list.v, n->list.n);
	case N_ARRAY_PAT:
		return gen_array_pat(c, n);
	case N_ASSIGN_PAT:
		return gen_assign_like(c, n, "=");
	case N_ASSIGN_PAT_PROP:
		return gen_assign_like(c, n, "=");
	case N_KEY_VALUE_PAT_PROP:
		return gen_assign_like(c, n, ":");
	case N_REST_PAT:
		return gen_rest_pat(c, n);
	case N_OBJECT_PAT:
		return gen_object_pat(c, n);
	case N_BLOCK_STMT:
		return gen_block_stmt(c, n);
	case N_BREAK_STMT:
		return gen_keyword_stmt(c, "break", n->a);
	case N_CONTINUE_STMT:
		return gen_keyword_stmt(c, "continue", n->a);
	case N_DEBUGGER_STMT:
		return gen_keyword_stmt(c, "debugger", NULL);
	case N_DO_WHILE_STMT:
		return gen_do_while_stmt(c, n);
	case N_EXPR_STMT:
		return gen_expr_stmt(c, n);
	case N_EMPTY_STMT:
		return items_str(";");
	case N_FOR_IN_STMT:
	case N_FOR_OF_STMT:
		return gen_for_in_of_stmt(c, n);
	case N_FOR_STMT:
		return gen_for_stmt(c, n);
	case N_IF_STMT:
		return gen_if_stmt(c, n);
	case N_LABELED_STMT:
		return gen_labeled_stmt(c, n);
	case N_RETURN_STMT:
		return gen_return_stmt(c, n);
	case N_SWITCH_STMT:
		return gen_switch_stmt(c, n);
	case N_SWITCH_CASE:
		return gen_switch_case(c, n);
	case N_THROW_STMT:
		return gen_throw_stmt(c, n);
	case N_TRY_STMT:
		return gen_try_stmt(c, n);
	case N_VAR_DECL:
		return gen_var_decl(c, n);
	case N_VAR_DECLARATOR:
		return gen_var_declarator(c, n);
	case N_WHILE_STMT:
		return gen_while_stmt(c, n);
	case N_TS_ARRAY_TYPE: {
		Items items = gen_node(n->a, c);
		items_push_str(&items, "[]");
		return items;
	}
	case N_TS_CONDITIONAL_TYPE:
		return gen_conditional_type(c, n);
	case N_TS_CONSTRUCTOR_TYPE:
		return gen_function_type(c, n, true);
	case N_TS_FN_TYPE:
		return gen_function_type(c, n, false);
	case N_TS_KEYWORD_TYPE:
		return gen_text(c, n);
	case N_TS_INDEXED_ACCESS_TYPE:
		return gen_indexed_access_type(c, n);
	case N_TS_INFER_TYPE:
		return gen_prefixed(c, "infer ", n->a);
	case N_TS_INTERSECTION_TYPE:
		return gen_union_or_intersection_type(c, n, false);
	case N_TS_LIT_TYPE:
		if (n->a->kind == N_NUMBER || n->a->kind == N_BIGINT)
			return gen_text(c, n);
		return gen_node(n->a, c);
	case N_TS_MAPPED_TYPE:
		return gen_mapped_type(c, n);
	case N_TS_OPTIONAL_TYPE: {
		Items items = gen_node(n->a, c);
		items_push_str(&items, "?");
		return items;
	}
	case N_TS_QUALIFIED_NAME: {
		Items items = gen_node(n->a, c);
		items_push_str(&items, ".");
		items_extend(&items, gen_node(n->b, c));
		return items;
	}
	case N_TS_PARENTHESIZED_TYPE:
		return gen_parenthesized_type(c, n);
	case N_TS_REST_TYPE:
		return gen_prefixed(c, "...", n->a);
	case N_TS_THIS_TYPE:
		return items_str("this");
	case N_TS_TUPLE_TYPE:
		return gen_array_like_nodes(c, n, n->list.v, n->list.n);
	case N_TS_TUPLE_ELEMENT:
		return gen_tuple_element(c, n);
	case N_TS_TYPE_ANN:
		return gen_node(n->a, c);
	case N_TS_TYPE_PARAM:
		return gen_type_param(c, n);
	case N_TS_TYPE_PARAM_DECL:
	case N_TS_TYPE_PARAM_INSTANTIATION:
		return gen_type_parameters(c, n);
	case N_TS_TYPE_OPERATOR:
		return gen_type_operator(c, n);
	case N_TS_TYPE_PREDICATE:
		return gen_type_predicate(c, n);
	case N_TS_TYPE_QUERY:
		return gen_type_query(c, n);
	case N_TS_TYPE_REF: {
		Items items = gen_node(n->a, c);
		if (n->b)
			items_extend(&items, gen_node(n->b, c));
		return items;
	}
	case N_TS_UNION_TYPE:
		return gen_union_or_intersection_type(c, n, true);
	default: {
		Str t = ntext(c, n);
		return gen_from_raw_string(t.p, t.n);
	}
	}
}

/* ---- generate.rs: generate ---- */

static int r_end_of_file_new_line(RCtx *rc, void *d)
{
	return rc->writer_info.column_number > 0 || rc->writer_info.line_number > 0;
}

static Items ts_generate(Source *src)
{
	Ctx *c = xalloc(sizeof *c);
	c->src = src;
	c->current_node = src->module;
	Items items = gen_node(src->module, c);
	items_push_cond(&items, if_true("endOfFileNewLine", resolver(r_end_of_file_new_line, NULL), items_signal(SIG_NEW_LINE)));
	return items;
}
