/* Port of prettyplease 0.2.37 src/ty.rs and src/data.rs (Copyright David Tolnay, MIT OR
 * Apache-2.0), without the "verbatim" feature. */

static void p_mac(Pp *p, const PpNode *mac, const char *ident, bool semicolon);

static void p_ty(Pp *p, const PpNode *ty)
{
	switch (ty->kind) {
	case PP_TY_ARRAY:
		p_word(p, "[");
		p_ty(p, ty->a);
		p_word(p, "; ");
		p_expr(p, ty->b);
		p_word(p, "]");
		break;
	case PP_TY_BARE_FN:
		if (ty->flags & PP_F_UNSAFE)
			p_word(p, "unsafe ");
		p_word(p, "fn(");
		p_cbox(p, INDENT);
		p_zerobreak(p);
		for (int i = 0; i < ty->list.n; i++) {
			const PpNode *arg = ty->list.v[i];
			if (arg->kind == PP_BARE_FN_ARG) {
				p_outer_attrs(p, arg->attrs);
				p_ident(p, arg->text);
				p_word(p, ": ");
				p_ty(p, arg->a);
			} else {
				p_ty(p, arg);
			}
			p_trailing_comma(p, i == ty->list.n - 1);
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, ")");
		p_return_type(p, ty->a);
		break;
	case PP_TY_IMPL:
	case PP_TY_DYN:
		p_word(p, ty->kind == PP_TY_IMPL ? "impl " : "dyn ");
		for (int i = 0; i < ty->a->list.n; i++) {
			if (i)
				p_word(p, " + ");
			p_type_param_bound(p, ty->a->list.v[i]);
		}
		break;
	case PP_TY_INFER:
		p_word(p, "_");
		break;
	case PP_TY_MACRO:
		p_mac(p, ty->a, NULL, false);
		break;
	case PP_TY_NEVER:
		p_word(p, "!");
		break;
	case PP_TY_PAREN:
		p_word(p, "(");
		p_ty(p, ty->a);
		p_word(p, ")");
		break;
	case PP_TY_PATH:
		p_qpath(p, ty->a, ty->aux, ty->b, PATH_TYPE);
		break;
	case PP_TY_PTR:
		p_word(p, "*");
		p_word(p, ty->flags & PP_F_MUT ? "mut " : "const ");
		p_ty(p, ty->a);
		break;
	case PP_TY_REF:
		p_word(p, "&");
		if (ty->text) {
			p_lifetime(p, ty->text);
			p_nbsp(p);
		}
		if (ty->flags & PP_F_MUT)
			p_word(p, "mut ");
		p_ty(p, ty->a);
		break;
	case PP_TY_SLICE:
		p_word(p, "[");
		p_ty(p, ty->a);
		p_word(p, "]");
		break;
	case PP_TY_TUPLE:
		p_word(p, "(");
		p_cbox(p, INDENT);
		p_zerobreak(p);
		for (int i = 0; i < ty->list.n; i++) {
			p_ty(p, ty->list.v[i]);
			if (ty->list.n == 1) {
				p_word(p, ",");
				p_zerobreak(p);
			} else {
				p_trailing_comma(p, i == ty->list.n - 1);
			}
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, ")");
		break;
	default:
		fail("prettyplease: unknown Type %d", ty->kind);
	}
}

static void p_return_type(Pp *p, const PpNode *ty)
{
	if (ty) {
		p_word(p, " -> ");
		p_ty(p, ty);
	}
}

static void p_abi(Pp *p, const PpNode *abi)
{
	p_word(p, "extern ");
	if (abi->text) {
		p_word(p, abi->text);
		p_nbsp(p);
	}
}

/* ---- data.rs ---- */

static void p_field(Pp *p, const PpNode *field);

static void p_visibility(Pp *p, int vis)
{
	switch (vis) {
	case PP_VIS_PUB:
		p_word(p, "pub ");
		break;
	case PP_VIS_CRATE:
	case PP_VIS_SUPER:
	case PP_VIS_SELF:
		/* vis_restricted with a one-ident path omits `in` */
		p_word(p, "pub(");
		p_word(p, vis == PP_VIS_CRATE ? "crate" : vis == PP_VIS_SUPER ? "super" : "self");
		p_word(p, ") ");
		break;
	}
}

static void p_fields_unnamed(Pp *p, const PpList *fields)
{
	p_word(p, "(");
	p_zerobreak(p);
	for (int i = 0; i < fields->n; i++) {
		p_field(p, fields->v[i]);
		p_trailing_comma(p, i == fields->n - 1);
	}
	p_offset(p, -INDENT);
	p_word(p, ")");
}

static void p_variant(Pp *p, const PpNode *variant)
{
	p_outer_attrs(p, variant->attrs);
	p_ident(p, variant->text);
	if (variant->flags & PP_F_NAMED) {
		p_nbsp(p);
		p_word(p, "{");
		p_cbox(p, INDENT);
		p_space(p);
		for (int i = 0; i < variant->list.n; i++) {
			p_field(p, variant->list.v[i]);
			p_trailing_comma_or_space(p, i == variant->list.n - 1);
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, "}");
	} else if (variant->flags & PP_F_UNNAMED) {
		p_cbox(p, INDENT);
		p_fields_unnamed(p, &variant->list);
		p_end(p);
	}
	if (variant->a) {
		p_word(p, " = ");
		p_expr(p, variant->a);
	}
}

static void p_field(Pp *p, const PpNode *field)
{
	p_outer_attrs(p, field->attrs);
	p_visibility(p, field->aux);
	if (field->text) {
		p_ident(p, field->text);
		p_word(p, ": ");
	}
	p_ty(p, field->a);
}
