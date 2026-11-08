/* Port of prettyplease 0.2.37 src/expr.rs and src/classify.rs (Copyright David Tolnay, MIT OR
 * Apache-2.0), without the "verbatim" feature.
 *
 * Not ported: src/fixup.rs and src/precedence.rs, which decide where to insert parentheses that the
 * syntax tree does not have. A generator builds the tree that syn parses from its tokens, where
 * every subexpression that needs parentheses already is an ExprParen, so prettyplease inserts none
 * and the `needs_paren`/`needs_group` decisions are all false. */

static void p_stmt(Pp *p, const PpNode *stmt, bool is_last);
static void p_small_block(Pp *p, const PpNode *block, const PpList *attrs);
static void p_angle_bracketed_generic_arguments(Pp *p, const PpNode *generic, int path_kind);
static bool stmt_add_semi(const PpNode *e);
static bool stmt_break_after(const PpNode *e);
static void p_expr_beginning_of_line(Pp *p, const PpNode *e, bool needs_paren, bool beginning_of_line);

/* ---- classify.rs ---- */

static bool requires_comma_to_be_match_arm(const PpNode *e)
{
	switch (e->kind) {
	case PP_E_IF: case PP_E_MATCH: case PP_E_BLOCK: case PP_E_UNSAFE: case PP_E_WHILE:
	case PP_E_LOOP: case PP_E_FOR: case PP_E_CONST:
		return false;
	default:
		return true;
	}
}

static bool requires_semi_to_be_stmt(const PpNode *e)
{
	if (e->kind == PP_E_MACRO)
		return e->a->aux != PP_DELIM_BRACE;
	return requires_comma_to_be_match_arm(e);
}

/* Whether the expression's first token is the label of a loop/block. */
static bool expr_leading_label(const PpNode *e)
{
	for (;;)
		switch (e->kind) {
		case PP_E_BLOCK: case PP_E_FOR: case PP_E_LOOP: case PP_E_WHILE:
			return e->text != NULL;
		case PP_E_ASSIGN: case PP_E_AWAIT: case PP_E_BINARY: case PP_E_CALL: case PP_E_CAST:
		case PP_E_FIELD: case PP_E_INDEX: case PP_E_METHOD: case PP_E_TRY:
			e = e->a;
			break;
		case PP_E_RANGE:
			if (!e->a)
				return false;
			e = e->a;
			break;
		default:
			return false;
		}
}

static bool type_trailing_brace(const PpNode *ty)
{
	for (;;)
		switch (ty->kind) {
		case PP_TY_BARE_FN:
			if (!ty->a)
				return false;
			ty = ty->a;
			break;
		case PP_TY_IMPL:
		case PP_TY_DYN: {
			const PpNode *last = ty->a->list.v[ty->a->list.n - 1];
			if (last->kind != PP_TRAIT_BOUND)
				return false;
			const PpNode *seg = last->a->list.v[last->a->list.n - 1];
			if (!seg->a || seg->a->kind != PP_PAREN_ARGS || !seg->a->a)
				return false;
			ty = seg->a->a;
			break;
		}
		case PP_TY_MACRO:
			return ty->a->aux == PP_DELIM_BRACE;
		case PP_TY_PATH: {
			const PpNode *seg = ty->b->list.v[ty->b->list.n - 1];
			if (!seg->a || seg->a->kind != PP_PAREN_ARGS || !seg->a->a)
				return false;
			ty = seg->a->a;
			break;
		}
		case PP_TY_PTR: case PP_TY_REF:
			ty = ty->a;
			break;
		default:
			return false;
		}
}

/* Whether the expression's last token is `}`. */
static bool expr_trailing_brace(const PpNode *e)
{
	for (;;)
		switch (e->kind) {
		case PP_E_ASYNC: case PP_E_BLOCK: case PP_E_CONST: case PP_E_FOR: case PP_E_IF:
		case PP_E_LOOP: case PP_E_MATCH: case PP_E_STRUCT: case PP_E_UNSAFE: case PP_E_WHILE:
			return true;
		case PP_E_ASSIGN: case PP_E_BINARY: case PP_E_LET:
			e = e->b;
			break;
		case PP_E_BREAK: case PP_E_RETURN:
			if (!e->a)
				return false;
			e = e->a;
			break;
		case PP_E_CAST:
			return type_trailing_brace(e->b);
		case PP_E_CLOSURE: case PP_E_REF: case PP_E_UNARY:
			e = e->a;
			break;
		case PP_E_MACRO:
			return e->a->aux == PP_DELIM_BRACE;
		case PP_E_RANGE:
			if (!e->b)
				return false;
			e = e->b;
			break;
		default:
			return false;
		}
}

/* ---- expr.rs helpers ---- */

static bool has_attrs(const PpNode *e) { return e->attrs && e->attrs->n; }

static bool needs_newline_if_wrap(const PpNode *e)
{
	for (;;)
		switch (e->kind) {
		case PP_E_ASSIGN: case PP_E_AWAIT: case PP_E_BINARY: case PP_E_CAST: case PP_E_FIELD:
		case PP_E_INDEX: case PP_E_METHOD:
			return true;
		case PP_E_BREAK: case PP_E_RETURN:
			if (!e->a)
				return false;
			e = e->a;
			break;
		case PP_E_RANGE:
			if (!e->b)
				return false;
			e = e->b;
			break;
		case PP_E_CALL: case PP_E_PAREN: case PP_E_REF: case PP_E_TRY: case PP_E_UNARY:
			e = e->a;
			break;
		case PP_E_LET:
			e = e->b;
			break;
		default:
			return false;
		}
}

static bool is_short_ident(const PpNode *e)
{
	if (e->kind != PP_E_PATH || has_attrs(e) || e->a)
		return false;
	const PpNode *path = e->b;
	if ((path->flags & PP_F_LEADING_COLON) || path->list.n != 1 || path->list.v[0]->a)
		return false;
	return strlen(path->list.v[0]->text) <= INDENT;
}

static bool is_blocklike(const PpNode *e)
{
	switch (e->kind) {
	case PP_E_ARRAY: case PP_E_ASYNC: case PP_E_BLOCK: case PP_E_CLOSURE: case PP_E_CONST:
	case PP_E_STRUCT: case PP_E_TUPLE: case PP_E_UNSAFE:
		return !attr_has_outer(e->attrs);
	default:
		return false;
	}
}

static const PpNode *simple_block(const PpNode *e)
{
	if (e->kind == PP_E_BLOCK && !has_attrs(e) && !e->text)
		return e;
	return NULL;
}

static bool simple_array(const PpList *elems)
{
	for (int i = 0; i < elems->n; i++) {
		const PpNode *e = elems->v[i];
		if (e->kind != PP_E_LIT)
			return false;
		int k = e->a->aux;
		if (k != PP_LIT_BYTE && k != PP_LIT_CHAR && k != PP_LIT_INT && k != PP_LIT_BOOL)
			return false;
	}
	return true;
}

/* Expressions for which `$expr` and `{ $expr }` mean the same thing. */
static bool parseable_as_stmt(const PpNode *e)
{
	for (;;)
		switch (e->kind) {
		case PP_E_ARRAY: case PP_E_ASYNC: case PP_E_BLOCK: case PP_E_BREAK: case PP_E_CLOSURE:
		case PP_E_CONST: case PP_E_CONTINUE: case PP_E_FOR: case PP_E_IF: case PP_E_INFER:
		case PP_E_LIT: case PP_E_LOOP: case PP_E_MACRO: case PP_E_MATCH: case PP_E_PAREN:
		case PP_E_PATH: case PP_E_REF: case PP_E_REPEAT: case PP_E_RETURN: case PP_E_STRUCT:
		case PP_E_TUPLE: case PP_E_UNARY: case PP_E_UNSAFE: case PP_E_WHILE:
			return true;
		case PP_E_LET:
			return false;
		case PP_E_ASSIGN: case PP_E_BINARY: case PP_E_CALL: case PP_E_CAST: case PP_E_INDEX:
			if (!requires_semi_to_be_stmt(e->a))
				return false;
			e = e->a;
			break;
		case PP_E_AWAIT: case PP_E_FIELD: case PP_E_METHOD: case PP_E_TRY:
			e = e->a;
			break;
		case PP_E_RANGE:
			if (!e->a)
				return true;
			if (!requires_semi_to_be_stmt(e->a))
				return false;
			e = e->a;
			break;
		default:
			return false;
		}
}

/* ---- expr.rs ---- */

static void p_call_args(Pp *p, const PpList *args);
static void p_prefix_subexpr(Pp *p, const PpNode *e, bool needs_paren, bool beginning_of_line);

static void p_member(Pp *p, const char *member) { p_word(p, member); }

static void p_label(Pp *p, const char *label)
{
	p_lifetime(p, label);
	p_word(p, ": ");
}

static void p_subexpr(Pp *p, const PpNode *e, bool needs_paren)
{
	if (needs_paren)
		p_word(p, "(");
	p_expr(p, e);
	if (needs_paren)
		p_word(p, ")");
}

static void p_expr_condition(Pp *p, const PpNode *e)
{
	p_cbox(p, 0);
	p_expr(p, e);
	if (needs_newline_if_wrap(e))
		p_space(p);
	else
		p_nbsp(p);
	p_end(p);
}

static void p_expr_lit(Pp *p, const PpNode *e)
{
	p_outer_attrs(p, e->attrs);
	p_lit(p, e->a);
}

static void p_expr_path(Pp *p, const PpNode *e)
{
	p_outer_attrs(p, e->attrs);
	p_qpath(p, e->a, e->aux, e->b, PATH_EXPR);
}

static void p_expr_block(Pp *p, const PpNode *e)
{
	p_outer_attrs(p, e->attrs);
	if (e->text)
		p_label(p, e->text);
	p_cbox(p, INDENT);
	p_small_block(p, e->a, e->attrs);
	p_end(p);
}

static void p_expr_as_small_block(Pp *p, const PpNode *e, intptr_t indent)
{
	p_word(p, "{");
	p_space(p);
	p_ibox(p, indent);
	p_expr_beginning_of_line(p, e, false, true);
	p_end(p);
	p_space(p);
	p_offset(p, -INDENT);
	p_word(p, "}");
}

static void p_small_block(Pp *p, const PpNode *block, const PpList *attrs)
{
	p_word(p, "{");
	if (attr_has_inner(attrs) || block->list.n) {
		p_space(p);
		p_inner_attrs(p, attrs);
		const PpNode *only = block->list.n == 1 ? block->list.v[0] : NULL;
		if (only && only->kind == PP_S_EXPR && !(only->flags & PP_F_SEMI) && stmt_break_after(only->a)) {
			p_ibox(p, 0);
			p_expr_beginning_of_line(p, only->a, false, true);
			p_end(p);
			p_space(p);
		} else {
			for (int i = 0; i < block->list.n; i++)
				p_stmt(p, block->list.v[i], i == block->list.n - 1);
		}
		p_offset(p, -INDENT);
	}
	p_word(p, "}");
}

static void p_block_body(Pp *p, const PpNode *block, const PpList *attrs)
{
	/* `{` hardbreak_if_nonempty inner_attrs stmts offset(-INDENT) end `}` (loops, match arms) */
	p_hardbreak_if_nonempty(p);
	p_inner_attrs(p, attrs);
	for (int i = 0; i < block->list.n; i++)
		p_stmt(p, block->list.v[i], i == block->list.n - 1);
	p_offset(p, -INDENT);
	p_end(p);
	p_word(p, "}");
}

static void p_expr_range(Pp *p, const PpNode *e)
{
	p_outer_attrs(p, e->attrs);
	if (has_attrs(e))
		p_word(p, "(");
	if (e->a)
		p_subexpr(p, e->a, false);
	else if (p_ends_with(p, '.'))
		p_nbsp(p);
	p_word(p, (e->flags & PP_F_CLOSED) ? "..=" : "..");
	if (e->b)
		p_subexpr(p, e->b, false);
	if (has_attrs(e))
		p_word(p, ")");
}

static void p_dot_break(Pp *p, const PpNode *base, bool beginning_of_line)
{
	if (!(beginning_of_line && is_short_ident(base))) {
		BreakToken t = {0};
		t.no_break = p_ends_with(p, '.') ? ' ' : 0;
		scan_break(p, t);
	}
}

static void p_method_call_suffix(Pp *p, const PpNode *e, bool beginning_of_line, bool unindent_call_args)
{
	p_dot_break(p, e->a, beginning_of_line);
	p_word(p, ".");
	p_ident(p, e->text);
	if (e->b)
		p_angle_bracketed_generic_arguments(p, e->b, PATH_EXPR);
	p_cbox(p, unindent_call_args ? -INDENT : 0);
	p_word(p, "(");
	p_call_args(p, &e->list);
	p_word(p, ")");
	p_end(p);
}

static void p_prefix_subexpr_method_call(Pp *p, const PpNode *e, bool beginning_of_line, bool unindent_call_args)
{
	p_prefix_subexpr(p, e->a, false, beginning_of_line);
	p_method_call_suffix(p, e, beginning_of_line, unindent_call_args);
}

static bool call_func_needs_paren(const PpNode *e)
{
	/* `(self.f)(x)` parses as a call of a parenthesized field; a call of a named field cannot come
	 * from a parse, but prettyplease parenthesizes it. */
	return e->a->kind == PP_E_FIELD && !(e->a->text[0] >= '0' && e->a->text[0] <= '9');
}

static bool is_prefix_subexpr(const PpNode *e)
{
	switch (e->kind) {
	case PP_E_AWAIT: case PP_E_CALL: case PP_E_FIELD: case PP_E_INDEX: case PP_E_METHOD: case PP_E_TRY:
		return true;
	default:
		return false;
	}
}

/* prettyplease recurses into the receiver of each `.await`, call, field, index, method call and
 * `?`; a generated method chain (a router with a route per method) is as deep as the input is
 * long, so the chain is unrolled into a loop: the opening parentheses outside in, the innermost
 * operand, then each suffix inside out. Each level is printed as by the recursion. */
typedef struct {
	const PpNode *e;
	bool needs_paren, beginning_of_line;
} PrefixLevel;

static void p_prefix_subexpr(Pp *p, const PpNode *e, bool needs_paren, bool beginning_of_line)
{
	int n = 1;
	for (const PpNode *x = e; is_prefix_subexpr(x); x = x->a)
		n++;
	PrefixLevel small[8], *lv = n <= 8 ? small : xalloc((size_t)n * sizeof *lv);
	lv[0] = (PrefixLevel){e, needs_paren, beginning_of_line};
	for (int i = 0; i + 1 < n; i++) {
		const PpNode *x = lv[i].e;
		bool call = x->kind == PP_E_CALL;
		lv[i + 1] = (PrefixLevel){x->a, call && call_func_needs_paren(x), !call && lv[i].beginning_of_line};
	}
	for (int i = 0; i < n; i++)
		if (lv[i].needs_paren)
			p_word(p, "(");
	p_cbox(p, -INDENT);
	p_expr(p, lv[n - 1].e);
	p_end(p);
	if (lv[n - 1].needs_paren)
		p_word(p, ")");
	for (int i = n - 2; i >= 0; i--) {
		const PpNode *x = lv[i].e;
		bool bol = lv[i].beginning_of_line;
		switch (x->kind) {
		case PP_E_AWAIT:
			p_dot_break(p, x->a, bol);
			p_word(p, ".await");
			break;
		case PP_E_CALL:
			p_word(p, "(");
			p_call_args(p, &x->list);
			p_word(p, ")");
			break;
		case PP_E_FIELD:
			p_dot_break(p, x->a, bol);
			p_word(p, ".");
			p_member(p, x->text);
			break;
		case PP_E_INDEX:
			p_word(p, "[");
			p_expr(p, x->b);
			p_word(p, "]");
			break;
		case PP_E_METHOD:
			p_method_call_suffix(p, x, bol, false);
			break;
		default: /* PP_E_TRY */
			p_word(p, "?");
		}
		if (lv[i].needs_paren)
			p_word(p, ")");
	}
}

static void p_expr_beginning_of_line(Pp *p, const PpNode *e, bool needs_paren, bool beginning_of_line)
{
	if (needs_paren)
		p_word(p, "(");
	switch (e->kind) {
	case PP_E_AWAIT:
	case PP_E_FIELD:
		p_outer_attrs(p, e->attrs);
		p_cbox(p, INDENT);
		p_prefix_subexpr(p, e, false, beginning_of_line);
		p_end(p);
		break;
	case PP_E_INDEX:
		p_outer_attrs(p, e->attrs);
		p_expr_beginning_of_line(p, e->a, false, beginning_of_line);
		p_word(p, "[");
		p_expr(p, e->b);
		p_word(p, "]");
		break;
	case PP_E_METHOD:
		p_outer_attrs(p, e->attrs);
		p_cbox(p, INDENT);
		p_prefix_subexpr_method_call(p, e, beginning_of_line, beginning_of_line && is_short_ident(e->a));
		p_end(p);
		break;
	case PP_E_TRY:
		p_outer_attrs(p, e->attrs);
		p_expr_beginning_of_line(p, e->a, false, beginning_of_line);
		p_word(p, "?");
		break;
	default:
		p_expr(p, e);
	}
	if (needs_paren)
		p_word(p, ")");
}

static void p_arm(Pp *p, const PpNode *arm)
{
	p_outer_attrs(p, arm->attrs);
	p_ibox(p, 0);
	p_pat(p, arm->a);
	if (arm->b) {
		p_word(p, " if ");
		p_expr(p, arm->b);
	}
	p_word(p, " => ");
	const PpNode *body = arm->c;
	while (body->kind == PP_E_BLOCK && !has_attrs(body) && !body->text) {
		const PpList *stmts = &body->a->list;
		if (stmts->n == 1 && stmts->v[0]->kind == PP_S_EXPR && !(stmts->v[0]->flags & PP_F_SEMI)) {
			body = stmts->v[0]->a;
			continue;
		}
		break;
	}
	PpNode empty_block, empty_stmts;
	if (body->kind == PP_E_TUPLE && !body->list.n && !has_attrs(body)) {
		memset(&empty_block, 0, sizeof empty_block);
		memset(&empty_stmts, 0, sizeof empty_stmts);
		empty_stmts.kind = PP_BLOCK;
		empty_block.kind = PP_E_BLOCK;
		empty_block.a = &empty_stmts;
		body = &empty_block;
	}
	if (body->kind == PP_E_BLOCK) {
		if (body->text)
			p_label(p, body->text);
		p_word(p, "{");
		p_neverbreak(p);
		p_cbox(p, INDENT);
		p_block_body(p, body->a, body->attrs);
	} else {
		p_neverbreak(p);
		p_cbox(p, INDENT);
		bool okay_to_brace = parseable_as_stmt(body);
		BreakToken t = {0};
		t.pre_break = okay_to_brace ? '{' : '(';
		scan_break(p, t);
		p_expr_beginning_of_line(p, body, false, true);
		BreakToken t2 = {0};
		t2.offset = -INDENT;
		t2.pre_break = okay_to_brace && stmt_add_semi(body) ? ';' : 0;
		t2.post_break = okay_to_brace ? "}" : "),";
		t2.no_break = requires_comma_to_be_match_arm(body) ? ',' : 0;
		scan_break(p, t2);
		p_end(p);
	}
	p_end(p);
}

static void p_call_args(Pp *p, const PpList *args)
{
	if (args->n == 1 && is_blocklike(args->v[0])) {
		p_expr(p, args->v[0]);
		return;
	}
	p_cbox(p, INDENT);
	p_zerobreak(p);
	for (int i = 0; i < args->n; i++) {
		p_expr(p, args->v[i]);
		p_trailing_comma(p, i == args->n - 1);
	}
	p_offset(p, -INDENT);
	p_end(p);
}

static void p_closure(Pp *p, const PpNode *e)
{
	p_ibox(p, 0);
	if (e->flags & PP_F_ASYNC)
		p_word(p, "async ");
	if (e->flags & PP_F_MOVE)
		p_word(p, "move ");
	p_cbox(p, INDENT);
	p_word(p, "|");
	for (int i = 0; i < e->list.n; i++) {
		if (!i)
			p_zerobreak(p);
		p_pat(p, e->list.v[i]);
		if (i != e->list.n - 1) {
			p_word(p, ",");
			p_space(p);
		}
	}
	const PpNode *body = e->a;
	if (!e->b) {
		p_word(p, "|");
		p_space(p);
		p_offset(p, -INDENT);
		p_end(p);
		p_neverbreak(p);
		bool wrap_in_brace = body->kind == PP_E_MATCH || body->kind == PP_E_CALL ? attr_has_outer(body->attrs)
										      : !is_blocklike(body);
		if (wrap_in_brace) {
			p_cbox(p, INDENT);
			bool okay_to_brace = parseable_as_stmt(body);
			BreakToken t = {0};
			t.pre_break = okay_to_brace ? '{' : '(';
			scan_break(p, t);
			p_expr(p, body);
			BreakToken t2 = {0};
			t2.offset = -INDENT;
			t2.pre_break = okay_to_brace && stmt_add_semi(body) ? ';' : 0;
			t2.post_break = okay_to_brace ? "}" : ")";
			scan_break(p, t2);
			p_end(p);
		} else {
			p_expr(p, body);
		}
	} else {
		if (e->list.n) {
			p_trailing_comma(p, true);
			p_offset(p, -INDENT);
		}
		p_word(p, "|");
		p_end(p);
		p_word(p, " -> ");
		p_ty(p, e->b);
		p_nbsp(p);
		p_neverbreak(p);
		if (body->kind == PP_E_BLOCK && !has_attrs(body) && !body->text) {
			p_expr(p, body);
		} else {
			p_cbox(p, INDENT);
			p_expr_as_small_block(p, body, 0);
			p_end(p);
		}
	}
	p_end(p);
}

static void p_expr_if(Pp *p, const PpNode *e)
{
	p_cbox(p, INDENT);
	p_word(p, "if ");
	p_cbox(p, -INDENT);
	p_expr_condition(p, e->a);
	p_end(p);
	if (e->c) {
		const PpNode *else_branch = e->c;
		p_small_block(p, e->b, NULL);
		for (;;) {
			p_word(p, " else ");
			if (else_branch->kind == PP_E_IF) {
				p_word(p, "if ");
				p_cbox(p, -INDENT);
				p_expr_condition(p, else_branch->a);
				p_end(p);
				p_small_block(p, else_branch->b, NULL);
				if (else_branch->c) {
					else_branch = else_branch->c;
					continue;
				}
			} else if (else_branch->kind == PP_E_BLOCK) {
				p_small_block(p, else_branch->a, NULL);
			} else {
				p_expr_as_small_block(p, else_branch, INDENT);
			}
			break;
		}
	} else if (!e->b->list.n) {
		p_word(p, "{}");
	} else {
		p_word(p, "{");
		p_hardbreak(p);
		for (int i = 0; i < e->b->list.n; i++)
			p_stmt(p, e->b->list.v[i], i == e->b->list.n - 1);
		p_offset(p, -INDENT);
		p_word(p, "}");
	}
	p_end(p);
}

static void p_expr(Pp *p, const PpNode *e)
{
	bool beginning_of_line = false;
	switch (e->kind) {
	case PP_E_ARRAY:
		p_outer_attrs(p, e->attrs);
		if (!e->list.n) {
			p_word(p, "[]");
		} else if (simple_array(&e->list)) {
			p_cbox(p, INDENT);
			p_word(p, "[");
			p_zerobreak(p);
			p_ibox(p, 0);
			for (int i = 0; i < e->list.n; i++) {
				p_expr(p, e->list.v[i]);
				if (i != e->list.n - 1) {
					p_word(p, ",");
					p_space(p);
				}
			}
			p_end(p);
			p_trailing_comma(p, true);
			p_offset(p, -INDENT);
			p_word(p, "]");
			p_end(p);
		} else {
			p_word(p, "[");
			p_cbox(p, INDENT);
			p_zerobreak(p);
			for (int i = 0; i < e->list.n; i++) {
				p_expr(p, e->list.v[i]);
				p_trailing_comma(p, i == e->list.n - 1);
			}
			p_offset(p, -INDENT);
			p_end(p);
			p_word(p, "]");
		}
		break;
	case PP_E_ASSIGN:
		p_outer_attrs(p, e->attrs);
		p_ibox(p, 0);
		if (has_attrs(e))
			p_word(p, "(");
		p_subexpr(p, e->a, false);
		p_word(p, " = ");
		p_neverbreak(p);
		p_expr(p, e->b);
		if (has_attrs(e))
			p_word(p, ")");
		p_end(p);
		break;
	case PP_E_ASYNC:
		p_outer_attrs(p, e->attrs);
		p_word(p, "async ");
		if (e->flags & PP_F_MOVE)
			p_word(p, "move ");
		p_cbox(p, INDENT);
		p_small_block(p, e->a, e->attrs);
		p_end(p);
		break;
	case PP_E_BINARY:
		p_outer_attrs(p, e->attrs);
		p_ibox(p, INDENT);
		p_ibox(p, -INDENT);
		if (has_attrs(e))
			p_word(p, "(");
		p_subexpr(p, e->a, false);
		p_end(p);
		p_space(p);
		p_word(p, e->text);
		p_nbsp(p);
		p_subexpr(p, e->b, false);
		if (has_attrs(e))
			p_word(p, ")");
		p_end(p);
		break;
	case PP_E_BLOCK:
		p_expr_block(p, e);
		break;
	case PP_E_BREAK:
		p_outer_attrs(p, e->attrs);
		p_word(p, "break");
		if (e->text) {
			p_nbsp(p);
			p_lifetime(p, e->text);
		}
		if (e->a) {
			p_nbsp(p);
			p_subexpr(p, e->a, !e->text && expr_leading_label(e->a));
		}
		break;
	case PP_E_CALL:
		p_outer_attrs(p, e->attrs);
		p_expr_beginning_of_line(p, e->a, call_func_needs_paren(e), beginning_of_line);
		p_word(p, "(");
		p_call_args(p, &e->list);
		p_word(p, ")");
		break;
	case PP_E_CAST:
		p_outer_attrs(p, e->attrs);
		p_ibox(p, INDENT);
		p_ibox(p, -INDENT);
		if (has_attrs(e))
			p_word(p, "(");
		p_subexpr(p, e->a, false);
		p_end(p);
		p_space(p);
		p_word(p, "as ");
		p_ty(p, e->b);
		if (has_attrs(e))
			p_word(p, ")");
		p_end(p);
		break;
	case PP_E_CLOSURE:
		p_outer_attrs(p, e->attrs);
		p_closure(p, e);
		break;
	case PP_E_CONST:
		p_outer_attrs(p, e->attrs);
		p_word(p, "const ");
		p_cbox(p, INDENT);
		p_small_block(p, e->a, e->attrs);
		p_end(p);
		break;
	case PP_E_CONTINUE:
		p_outer_attrs(p, e->attrs);
		p_word(p, "continue");
		if (e->text) {
			p_nbsp(p);
			p_lifetime(p, e->text);
		}
		break;
	case PP_E_AWAIT:
	case PP_E_FIELD:
	case PP_E_INDEX:
	case PP_E_METHOD:
	case PP_E_TRY:
		p_expr_beginning_of_line(p, e, false, beginning_of_line);
		break;
	case PP_E_FOR:
		p_outer_attrs(p, e->attrs);
		p_ibox(p, 0);
		if (e->text)
			p_label(p, e->text);
		p_word(p, "for ");
		p_pat(p, e->a);
		p_word(p, " in ");
		p_neverbreak(p);
		p_expr_condition(p, e->b);
		p_word(p, "{");
		p_neverbreak(p);
		p_cbox(p, INDENT);
		p_block_body(p, e->c, e->attrs);
		p_end(p);
		break;
	case PP_E_IF:
		p_outer_attrs(p, e->attrs);
		p_expr_if(p, e);
		break;
	case PP_E_INFER:
		p_outer_attrs(p, e->attrs);
		p_word(p, "_");
		break;
	case PP_E_LET:
		p_outer_attrs(p, e->attrs);
		p_ibox(p, 0);
		p_word(p, "let ");
		p_ibox(p, 0);
		p_pat(p, e->a);
		p_end(p);
		p_word(p, " = ");
		p_neverbreak(p);
		p_ibox(p, 0);
		p_subexpr(p, e->b, false);
		p_end(p);
		p_end(p);
		break;
	case PP_E_LIT:
		p_expr_lit(p, e);
		break;
	case PP_E_LOOP:
		p_outer_attrs(p, e->attrs);
		if (e->text)
			p_label(p, e->text);
		p_word(p, "loop {");
		p_cbox(p, INDENT);
		p_block_body(p, e->a, e->attrs);
		break;
	case PP_E_MACRO:
		p_outer_attrs(p, e->attrs);
		p_mac(p, e->a, NULL, false);
		break;
	case PP_E_MATCH:
		p_outer_attrs(p, e->attrs);
		p_ibox(p, 0);
		p_word(p, "match ");
		p_expr_condition(p, e->a);
		p_word(p, "{");
		p_neverbreak(p);
		p_cbox(p, INDENT);
		p_hardbreak_if_nonempty(p);
		p_inner_attrs(p, e->attrs);
		for (int i = 0; i < e->list.n; i++) {
			p_arm(p, e->list.v[i]);
			p_hardbreak(p);
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, "}");
		p_end(p);
		break;
	case PP_E_PAREN:
		p_outer_attrs(p, e->attrs);
		p_word(p, "(");
		p_expr(p, e->a);
		p_word(p, ")");
		break;
	case PP_E_PATH:
		p_expr_path(p, e);
		break;
	case PP_E_RANGE:
		p_expr_range(p, e);
		break;
	case PP_E_REF:
		p_outer_attrs(p, e->attrs);
		p_word(p, "&");
		if (e->flags & PP_F_MUT)
			p_word(p, "mut ");
		p_subexpr(p, e->a, false);
		break;
	case PP_E_REPEAT:
		p_outer_attrs(p, e->attrs);
		p_word(p, "[");
		p_expr(p, e->a);
		p_word(p, "; ");
		p_expr(p, e->b);
		p_word(p, "]");
		break;
	case PP_E_RETURN:
		p_outer_attrs(p, e->attrs);
		p_word(p, "return");
		if (e->a) {
			p_nbsp(p);
			p_expr(p, e->a);
		}
		break;
	case PP_E_STRUCT:
		p_outer_attrs(p, e->attrs);
		p_cbox(p, INDENT);
		p_ibox(p, -INDENT);
		p_qpath(p, e->a, e->aux, e->b, PATH_EXPR);
		p_end(p);
		p_word(p, " {");
		p_space_if_nonempty(p);
		for (int i = 0; i < e->list.n; i++) {
			const PpNode *fv = e->list.v[i];
			p_outer_attrs(p, fv->attrs);
			p_member(p, fv->text);
			if (!(fv->flags & PP_F_SHORTHAND)) {
				p_word(p, ": ");
				p_ibox(p, 0);
				p_expr(p, fv->a);
				p_end(p);
			}
			p_trailing_comma_or_space(p, i == e->list.n - 1 && !e->c);
		}
		if (e->c) {
			p_word(p, "..");
			p_expr(p, e->c);
			p_space(p);
		}
		p_offset(p, -INDENT);
		p_end_with_max_width(p, 34);
		p_word(p, "}");
		break;
	case PP_E_TUPLE:
		p_outer_attrs(p, e->attrs);
		p_word(p, "(");
		p_cbox(p, INDENT);
		p_zerobreak(p);
		for (int i = 0; i < e->list.n; i++) {
			p_expr(p, e->list.v[i]);
			if (e->list.n == 1) {
				p_word(p, ",");
				p_zerobreak(p);
			} else {
				p_trailing_comma(p, i == e->list.n - 1);
			}
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, ")");
		break;
	case PP_E_UNARY:
		p_outer_attrs(p, e->attrs);
		p_word(p, e->text);
		p_subexpr(p, e->a, false);
		break;
	case PP_E_UNSAFE:
		p_outer_attrs(p, e->attrs);
		p_word(p, "unsafe ");
		p_cbox(p, INDENT);
		p_small_block(p, e->a, e->attrs);
		p_end(p);
		break;
	case PP_E_WHILE:
		p_outer_attrs(p, e->attrs);
		if (e->text)
			p_label(p, e->text);
		p_word(p, "while ");
		p_expr_condition(p, e->a);
		p_word(p, "{");
		p_neverbreak(p);
		p_cbox(p, INDENT);
		p_block_body(p, e->b, e->attrs);
		break;
	default:
		fail("prettyplease: unknown Expr %d", e->kind);
	}
}
