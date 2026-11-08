/* Port of prettyplease 0.2.37 src/stmt.rs (Copyright David Tolnay, MIT OR Apache-2.0). */

static void p_item(Pp *p, const PpNode *item);

/* stmt::add_semi */
static bool stmt_add_semi(const PpNode *e)
{
	switch (e->kind) {
	case PP_E_ASSIGN: case PP_E_BREAK: case PP_E_CONTINUE: case PP_E_RETURN:
		return true;
	case PP_E_BINARY: {
		const char *op = e->text;
		size_t n = strlen(op);
		/* the compound assignments: +=, -=, ..., <<=, >>= (not ==, <=, >=, !=) */
		return n >= 2 && op[n - 1] == '=' && !str_eq(op, "==") && !str_eq(op, "<=") &&
		       !str_eq(op, ">=") && !str_eq(op, "!=");
	}
	default:
		return false;
	}
}

/* stmt::break_after: true unless an empty Expr::Verbatim in a group (not produced here). */
static bool stmt_break_after(const PpNode *e)
{
	(void)e;
	return true;
}

static bool remove_semi(const PpNode *e)
{
	for (;;)
		switch (e->kind) {
		case PP_E_FOR: case PP_E_WHILE:
			return true;
		case PP_E_IF:
			if (!e->c)
				return true;
			e = e->c;
			break;
		default:
			return false;
		}
}

static void p_stmt(Pp *p, const PpNode *stmt, bool is_last)
{
	switch (stmt->kind) {
	case PP_S_LOCAL:
		p_outer_attrs(p, stmt->attrs);
		p_ibox(p, 0);
		p_word(p, "let ");
		p_pat(p, stmt->a);
		if (stmt->b) {
			p_word(p, " = ");
			p_neverbreak(p);
			p_subexpr(p, stmt->b, stmt->c && expr_trailing_brace(stmt->b));
			if (stmt->c) {
				p_space(p);
				p_word(p, "else ");
				p_end(p);
				p_neverbreak(p);
				p_cbox(p, INDENT);
				const PpNode *blk = simple_block(stmt->c);
				if (blk)
					p_small_block(p, blk->a, NULL);
				else
					p_expr_as_small_block(p, stmt->c, INDENT);
			}
		}
		p_end(p);
		p_word(p, ";");
		p_hardbreak(p);
		break;
	case PP_S_ITEM:
		p_item(p, stmt->a);
		break;
	case PP_S_EXPR:
		if (!(stmt->flags & PP_F_SEMI)) {
			p_ibox(p, 0);
			p_expr_beginning_of_line(p, stmt->a, false, true);
			if (stmt_add_semi(stmt->a))
				p_word(p, ";");
			p_end(p);
			p_hardbreak(p);
		} else {
			p_ibox(p, 0);
			p_expr_beginning_of_line(p, stmt->a, false, true);
			if (!remove_semi(stmt->a))
				p_word(p, ";");
			p_end(p);
			p_hardbreak(p);
		}
		break;
	case PP_S_MACRO: {
		p_outer_attrs(p, stmt->attrs);
		bool semicolon = (stmt->flags & PP_F_SEMI) || (!is_last && requires_semi(stmt->a->aux));
		p_mac(p, stmt->a, NULL, semicolon);
		p_hardbreak(p);
		break;
	}
	default:
		fail("prettyplease: unknown Stmt %d", stmt->kind);
	}
}
