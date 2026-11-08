/* Port of prettyplease 0.2.37 src/pat.rs (Copyright David Tolnay, MIT OR Apache-2.0), without the
 * "verbatim" feature. */

static void p_expr_range(Pp *p, const PpNode *e);
static void p_member(Pp *p, const char *member);

static void p_pat(Pp *p, const PpNode *pat)
{
	p_outer_attrs(p, pat->attrs);
	switch (pat->kind) {
	case PP_P_IDENT:
		if (pat->flags & PP_F_REF)
			p_word(p, "ref ");
		if (pat->flags & PP_F_MUT)
			p_word(p, "mut ");
		p_ident(p, pat->text);
		if (pat->a) {
			p_word(p, " @ ");
			p_pat(p, pat->a);
		}
		break;
	case PP_P_LIT:
		p_expr(p, pat->a);
		break;
	case PP_P_MACRO:
		p_mac(p, pat->a, NULL, false);
		break;
	case PP_P_OR: {
		bool consistent_break = false;
		for (int i = 0; i < pat->list.n; i++) {
			int k = pat->list.v[i]->kind;
			if (k != PP_P_LIT && k != PP_P_WILD) {
				consistent_break = true;
				break;
			}
		}
		if (consistent_break)
			p_cbox(p, 0);
		else
			p_ibox(p, 0);
		for (int i = 0; i < pat->list.n; i++) {
			if (i) {
				p_space(p);
				p_word(p, "| ");
			}
			p_pat(p, pat->list.v[i]);
		}
		p_end(p);
		break;
	}
	case PP_P_PAREN:
		p_word(p, "(");
		p_pat(p, pat->a);
		p_word(p, ")");
		break;
	case PP_P_PATH:
		p_qpath(p, pat->a, pat->aux, pat->b, PATH_EXPR);
		break;
	case PP_P_RANGE: {
		PpNode e = *pat;
		e.kind = PP_E_RANGE;
		e.attrs = NULL;
		p_expr_range(p, &e);
		break;
	}
	case PP_P_REF:
		p_word(p, "&");
		if (pat->flags & PP_F_MUT)
			p_word(p, "mut ");
		p_pat(p, pat->a);
		break;
	case PP_P_REST:
		p_word(p, "..");
		break;
	case PP_P_SLICE:
		p_word(p, "[");
		for (int i = 0; i < pat->list.n; i++) {
			p_pat(p, pat->list.v[i]);
			p_trailing_comma(p, i == pat->list.n - 1);
		}
		p_word(p, "]");
		break;
	case PP_P_STRUCT: {
		bool rest = pat->flags & PP_F_REST;
		p_cbox(p, INDENT);
		p_path(p, pat->b, PATH_EXPR);
		p_word(p, " {");
		p_space_if_nonempty(p);
		for (int i = 0; i < pat->list.n; i++) {
			const PpNode *fp = pat->list.v[i];
			p_outer_attrs(p, fp->attrs);
			if (!(fp->flags & PP_F_SHORTHAND)) {
				p_member(p, fp->text);
				p_word(p, ": ");
			}
			p_pat(p, fp->a);
			p_trailing_comma_or_space(p, i == pat->list.n - 1 && !rest);
		}
		if (rest) {
			p_word(p, "..");
			p_space(p);
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, "}");
		break;
	}
	case PP_P_TUPLE:
		p_word(p, "(");
		p_cbox(p, INDENT);
		p_zerobreak(p);
		for (int i = 0; i < pat->list.n; i++) {
			p_pat(p, pat->list.v[i]);
			if (pat->list.n == 1) {
				if (pat->flags & PP_F_TRAILING)
					p_word(p, ",");
				p_zerobreak(p);
			} else {
				p_trailing_comma(p, i == pat->list.n - 1);
			}
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, ")");
		break;
	case PP_P_TUPLE_STRUCT:
		p_path(p, pat->b, PATH_EXPR);
		p_word(p, "(");
		p_cbox(p, INDENT);
		p_zerobreak(p);
		for (int i = 0; i < pat->list.n; i++) {
			p_pat(p, pat->list.v[i]);
			p_trailing_comma(p, i == pat->list.n - 1);
		}
		p_offset(p, -INDENT);
		p_end(p);
		p_word(p, ")");
		break;
	case PP_P_TYPE:
		p_pat(p, pat->a);
		p_word(p, ": ");
		p_ty(p, pat->b);
		break;
	case PP_P_WILD:
		p_word(p, "_");
		break;
	default:
		fail("prettyplease: unknown Pat %d", pat->kind);
	}
}
