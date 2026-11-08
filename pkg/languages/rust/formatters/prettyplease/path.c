/* Port of prettyplease 0.2.37 src/path.rs, src/lifetime.rs and src/lit.rs (Copyright David Tolnay,
 * MIT OR Apache-2.0). */

static void p_ty(Pp *p, const PpNode *ty);
static void p_return_type(Pp *p, const PpNode *ty);
static void p_type_param_bound(Pp *p, const PpNode *bound);
static void p_const_argument(Pp *p, const PpNode *e);

static void p_lifetime(Pp *p, const char *lifetime)
{
	p_word(p, "'");
	p_ident(p, lifetime + (*lifetime == '\''));
}

static void p_lit(Pp *p, const PpNode *lit) { p_word(p, lit->text); }

static void p_angle_bracketed_generic_arguments(Pp *p, const PpNode *generic, int path_kind);

static void p_path_arguments(Pp *p, const PpNode *arguments, int kind)
{
	if (!arguments)
		return;
	if (arguments->kind == PP_ANGLE_ARGS) {
		p_angle_bracketed_generic_arguments(p, arguments, kind);
	} else {
		/* parenthesized_generic_arguments */
		p_cbox(p, INDENT);
		p_word(p, "(");
		p_zerobreak(p);
		for (int i = 0; i < arguments->list.n; i++) {
			p_ty(p, arguments->list.v[i]);
			p_trailing_comma(p, i == arguments->list.n - 1);
		}
		p_offset(p, -INDENT);
		p_word(p, ")");
		p_return_type(p, arguments->a);
		p_end(p);
	}
}

static void p_path_segment(Pp *p, const PpNode *segment, int kind)
{
	p_ident(p, segment->text);
	p_path_arguments(p, segment->a, kind);
}

static void p_path(Pp *p, const PpNode *path, int kind)
{
	if (!path->list.n)
		fail("prettyplease: empty path");
	for (int i = 0; i < path->list.n; i++) {
		if (i || (path->flags & PP_F_LEADING_COLON))
			p_word(p, "::");
		p_path_segment(p, path->list.v[i], kind);
	}
}

static void p_generic_argument(Pp *p, const PpNode *arg)
{
	switch (arg->kind) {
	case PP_LIFETIME:
		p_lifetime(p, arg->text);
		break;
	case PP_CONST_ARG:
		p_const_argument(p, arg->a);
		break;
	case PP_ASSOC_TYPE:
		p_ident(p, arg->text);
		if (arg->a)
			p_angle_bracketed_generic_arguments(p, arg->a, PATH_TYPE);
		p_word(p, " = ");
		p_ty(p, arg->b);
		break;
	case PP_CONSTRAINT:
		p_ident(p, arg->text);
		if (arg->a)
			p_angle_bracketed_generic_arguments(p, arg->a, PATH_TYPE);
		p_ibox(p, INDENT);
		for (int i = 0; i < arg->b->list.n; i++) {
			if (!i) {
				p_word(p, ": ");
			} else {
				p_space(p);
				p_word(p, "+ ");
			}
			p_type_param_bound(p, arg->b->list.v[i]);
		}
		p_end(p);
		break;
	default:
		p_ty(p, arg);
	}
}

static void p_angle_bracketed_generic_arguments(Pp *p, const PpNode *generic, int path_kind)
{
	if (!generic->list.n || path_kind == PATH_SIMPLE)
		return;
	if (path_kind == PATH_EXPR)
		p_word(p, "::");
	p_word(p, "<");
	p_cbox(p, INDENT);
	p_zerobreak(p);
	/* Lifetimes first; `last` is the last argument of the highest group (max_by_key). */
	int last = -1;
	bool any_second = false;
	for (int i = 0; i < generic->list.n; i++)
		if (generic->list.v[i]->kind != PP_LIFETIME)
			any_second = true;
	for (int i = 0; i < generic->list.n; i++)
		if ((generic->list.v[i]->kind != PP_LIFETIME) == any_second)
			last = i;
	for (int group = 0; group < 2; group++)
		for (int i = 0; i < generic->list.n; i++) {
			const PpNode *arg = generic->list.v[i];
			if ((arg->kind != PP_LIFETIME) == group) {
				p_generic_argument(p, arg);
				p_trailing_comma(p, i == last);
			}
		}
	p_offset(p, -INDENT);
	p_end(p);
	p_word(p, ">");
}

static void p_qpath(Pp *p, const PpNode *qself, int position, const PpNode *path, int kind)
{
	if (!qself) {
		p_path(p, path, kind);
		return;
	}
	if (position >= path->list.n)
		fail("prettyplease: qself position out of range");
	p_word(p, "<");
	p_ty(p, qself);
	int i = 0;
	if (position > 0) {
		p_word(p, " as ");
		for (; i < position; i++) {
			if (i || (path->flags & PP_F_LEADING_COLON))
				p_word(p, "::");
			p_path_segment(p, path->list.v[i], PATH_TYPE);
			if (i == position - 1)
				p_word(p, ">");
		}
	} else {
		p_word(p, ">");
	}
	for (; i < path->list.n; i++) {
		p_word(p, "::");
		p_path_segment(p, path->list.v[i], kind);
	}
}
