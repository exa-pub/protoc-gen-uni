/* Port of prettyplease 0.2.37 src/item.rs and src/file.rs (Copyright David Tolnay, MIT OR
 * Apache-2.0), without the "verbatim" feature. Foreign items, unions and trait aliases are not
 * ported (generated code has none). */

static void p_signature(Pp *p, const PpNode *sig)
{
	if (sig->flags & PP_F_CONST)
		p_word(p, "const ");
	if (sig->flags & PP_F_ASYNC)
		p_word(p, "async ");
	if (sig->flags & PP_F_UNSAFE)
		p_word(p, "unsafe ");
	if (sig->b)
		p_abi(p, sig->b);
	p_word(p, "fn ");
	p_ident(p, sig->text);
	p_generics(p, sig->c);
	p_word(p, "(");
	p_neverbreak(p);
	p_cbox(p, 0);
	p_zerobreak(p);
	for (int i = 0; i < sig->list.n; i++) {
		const PpNode *input = sig->list.v[i];
		if (input->kind == PP_RECEIVER) {
			p_outer_attrs(p, input->attrs);
			if (input->flags & PP_F_REF) {
				p_word(p, "&");
				if (input->text) {
					p_lifetime(p, input->text);
					p_nbsp(p);
				}
			}
			if (input->flags & PP_F_MUT)
				p_word(p, "mut ");
			p_word(p, "self");
			if (input->a) {
				p_word(p, ": ");
				p_ty(p, input->a);
			}
		} else {
			p_pat(p, input);
		}
		p_trailing_comma(p, i == sig->list.n - 1);
	}
	p_offset(p, -INDENT);
	p_end(p);
	p_word(p, ")");
	p_cbox(p, -INDENT);
	p_return_type(p, sig->a);
	p_end(p);
}

static void p_use_tree(Pp *p, const PpNode *t)
{
	switch (t->kind) {
	case PP_USE_PATH:
		p_ident(p, t->text);
		p_word(p, "::");
		p_use_tree(p, t->a);
		break;
	case PP_USE_NAME:
		p_ident(p, t->text);
		break;
	case PP_USE_RENAME:
		p_ident(p, t->text);
		p_word(p, " as ");
		p_ident(p, t->a->text);
		break;
	case PP_USE_GLOB:
		p_word(p, "*");
		break;
	default: { /* PP_USE_GROUP */
		const PpList *items = &t->list;
		if (!items->n) {
			p_word(p, "{}");
		} else if (items->n == 1 && !((items->v[0]->kind == PP_USE_NAME || items->v[0]->kind == PP_USE_RENAME) &&
					      str_eq(items->v[0]->text, "self"))) {
			p_use_tree(p, items->v[0]);
		} else {
			p_cbox(p, INDENT);
			p_word(p, "{");
			p_zerobreak(p);
			p_ibox(p, 0);
			for (int i = 0; i < items->n; i++) {
				p_use_tree(p, items->v[i]);
				if (i != items->n - 1) {
					p_word(p, ",");
					const PpNode *u = items->v[i];
					while (u->kind == PP_USE_PATH)
						u = u->a;
					if (u->kind == PP_USE_GROUP)
						p_hardbreak(p);
					else
						p_space(p);
				}
			}
			p_end(p);
			p_trailing_comma(p, true);
			p_offset(p, -INDENT);
			p_word(p, "}");
			p_end(p);
		}
	}
	}
}

static void p_fn_body(Pp *p, const PpNode *item, const PpNode *block)
{
	p_word(p, "{");
	p_hardbreak_if_nonempty(p);
	p_inner_attrs(p, item->attrs);
	for (int i = 0; i < block->list.n; i++)
		p_stmt(p, block->list.v[i], i == block->list.n - 1);
	p_offset(p, -INDENT);
	p_end(p);
	p_word(p, "}");
}

static void p_item(Pp *p, const PpNode *item)
{
	p_outer_attrs(p, item->attrs);
	switch (item->kind) {
	case PP_ITEM_CONST: /* item_const, impl_item_const, trait_item_const */
		p_cbox(p, 0);
		p_visibility(p, item->aux);
		if (item->flags & PP_F_DEFAULT)
			p_word(p, "default ");
		p_word(p, "const ");
		p_ident(p, item->text);
		p_generics(p, item->a);
		p_word(p, ": ");
		p_ty(p, item->b);
		if (item->c) {
			p_word(p, " = ");
			p_neverbreak(p);
			p_expr(p, item->c);
		}
		p_word(p, ";");
		p_end(p);
		p_hardbreak(p);
		break;
	case PP_ITEM_ENUM:
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		p_word(p, "enum ");
		p_ident(p, item->text);
		p_generics(p, item->a);
		p_where_clause_for_body(p, item->a);
		p_word(p, "{");
		p_hardbreak_if_nonempty(p);
		for (int i = 0; i < item->list.n; i++) {
			p_variant(p, item->list.v[i]);
			p_word(p, ",");
			p_hardbreak(p);
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, "}");
		p_hardbreak(p);
		break;
	case PP_ITEM_EXTERN_CRATE:
		p_visibility(p, item->aux);
		p_word(p, "extern crate ");
		p_ident(p, item->text);
		if (item->a) {
			p_word(p, " as ");
			p_ident(p, item->a->text);
		}
		p_word(p, ";");
		p_hardbreak(p);
		break;
	case PP_ITEM_FN: /* item_fn, impl_item_fn, trait_item_fn */
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		if (item->flags & PP_F_DEFAULT)
			p_word(p, "default ");
		p_signature(p, item->a);
		if (item->b) {
			p_where_clause_for_body(p, item->a->c);
			p_fn_body(p, item, item->b);
		} else {
			p_where_clause_semi(p, item->a->c);
			p_end(p);
		}
		p_hardbreak(p);
		break;
	case PP_ITEM_IMPL:
		p_cbox(p, INDENT);
		p_ibox(p, -INDENT);
		p_cbox(p, INDENT);
		if (item->flags & PP_F_DEFAULT)
			p_word(p, "default ");
		if (item->flags & PP_F_UNSAFE)
			p_word(p, "unsafe ");
		p_word(p, "impl");
		p_generics(p, item->c);
		p_end(p);
		p_nbsp(p);
		if (item->a) {
			if (item->flags & PP_F_NEGATIVE)
				p_word(p, "!");
			p_path(p, item->a, PATH_TYPE);
			p_space(p);
			p_word(p, "for ");
		}
		p_ty(p, item->b);
		p_end(p);
		p_where_clause_for_body(p, item->c);
		p_word(p, "{");
		p_hardbreak_if_nonempty(p);
		p_inner_attrs(p, item->attrs);
		for (int i = 0; i < item->list.n; i++)
			p_item(p, item->list.v[i]);
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, "}");
		p_hardbreak(p);
		break;
	case PP_ITEM_MACRO:
		p_mac(p, item->a, item->text, requires_semi(item->a->aux));
		p_hardbreak(p);
		break;
	case PP_ITEM_MOD:
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		if (item->flags & PP_F_UNSAFE)
			p_word(p, "unsafe ");
		p_word(p, "mod ");
		p_ident(p, item->text);
		if (item->flags & PP_F_CONTENT) {
			p_word(p, " {");
			p_hardbreak_if_nonempty(p);
			p_inner_attrs(p, item->attrs);
			for (int i = 0; i < item->list.n; i++)
				p_item(p, item->list.v[i]);
			p_offset(p, -INDENT);
			p_end(p);
			p_word(p, "}");
		} else {
			p_word(p, ";");
			p_end(p);
		}
		p_hardbreak(p);
		break;
	case PP_ITEM_STATIC:
		p_cbox(p, 0);
		p_visibility(p, item->aux);
		p_word(p, "static ");
		if (item->flags & PP_F_MUT)
			p_word(p, "mut ");
		p_ident(p, item->text);
		p_word(p, ": ");
		p_ty(p, item->a);
		p_word(p, " = ");
		p_neverbreak(p);
		p_expr(p, item->b);
		p_word(p, ";");
		p_end(p);
		p_hardbreak(p);
		break;
	case PP_ITEM_STRUCT:
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		p_word(p, "struct ");
		p_ident(p, item->text);
		p_generics(p, item->a);
		if (item->flags & PP_F_NAMED) {
			p_where_clause_for_body(p, item->a);
			p_word(p, "{");
			p_hardbreak_if_nonempty(p);
			for (int i = 0; i < item->list.n; i++) {
				p_field(p, item->list.v[i]);
				p_word(p, ",");
				p_hardbreak(p);
			}
			p_offset(p, -INDENT);
			p_end(p);
			p_word(p, "}");
		} else if (item->flags & PP_F_UNNAMED) {
			p_fields_unnamed(p, &item->list);
			p_where_clause_semi(p, item->a);
			p_end(p);
		} else {
			p_where_clause_semi(p, item->a);
			p_end(p);
		}
		p_hardbreak(p);
		break;
	case PP_ITEM_TRAIT:
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		if (item->flags & PP_F_UNSAFE)
			p_word(p, "unsafe ");
		p_word(p, "trait ");
		p_ident(p, item->text);
		p_generics(p, item->b);
		if (item->a)
			for (int i = 0; i < item->a->list.n; i++) {
				p_word(p, i ? " + " : ": ");
				p_type_param_bound(p, item->a->list.v[i]);
			}
		p_where_clause_for_body(p, item->b);
		p_word(p, "{");
		p_hardbreak_if_nonempty(p);
		p_inner_attrs(p, item->attrs);
		for (int i = 0; i < item->list.n; i++)
			p_item(p, item->list.v[i]);
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, "}");
		p_hardbreak(p);
		break;
	case PP_ITEM_TYPE:
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		p_word(p, "type ");
		p_ident(p, item->text);
		p_generics(p, item->b);
		p_where_clause_oneline(p, item->b);
		p_word(p, "= ");
		p_neverbreak(p);
		p_ibox(p, -INDENT);
		p_ty(p, item->a);
		p_end(p);
		p_word(p, ";");
		p_end(p);
		p_hardbreak(p);
		break;
	case PP_IMPL_TYPE:
		p_cbox(p, INDENT);
		p_visibility(p, item->aux);
		if (item->flags & PP_F_DEFAULT)
			p_word(p, "default ");
		p_word(p, "type ");
		p_ident(p, item->text);
		p_generics(p, item->b);
		p_word(p, " = ");
		p_neverbreak(p);
		p_ibox(p, -INDENT);
		p_ty(p, item->a);
		p_end(p);
		p_where_clause_oneline_semi(p, item->b);
		p_end(p);
		p_hardbreak(p);
		break;
	case PP_TRAIT_TYPE:
		p_cbox(p, INDENT);
		p_word(p, "type ");
		p_ident(p, item->text);
		p_generics(p, item->c);
		if (item->a)
			for (int i = 0; i < item->a->list.n; i++) {
				if (!i) {
					p_word(p, ": ");
				} else {
					p_space(p);
					p_word(p, "+ ");
				}
				p_type_param_bound(p, item->a->list.v[i]);
			}
		if (item->b) {
			p_word(p, " = ");
			p_neverbreak(p);
			p_ibox(p, -INDENT);
			p_ty(p, item->b);
			p_end(p);
		}
		p_where_clause_oneline_semi(p, item->c);
		p_end(p);
		p_hardbreak(p);
		break;
	case PP_ITEM_USE:
		p_visibility(p, item->aux);
		p_word(p, "use ");
		if (item->flags & PP_F_LEADING_COLON)
			p_word(p, "::");
		p_use_tree(p, item->a);
		p_word(p, ";");
		p_hardbreak(p);
		break;
	default:
		fail("prettyplease: unknown Item %d", item->kind);
	}
}

/* file.rs */
static void p_file(Pp *p, const PpNode *file)
{
	p_cbox(p, 0);
	p_inner_attrs(p, file->attrs);
	for (int i = 0; i < file->list.n; i++)
		p_item(p, file->list.v[i]);
	p_end(p);
}
