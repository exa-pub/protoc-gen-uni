/* black/nodes.py: node classification and whitespace(). */

static bool in_set(int t, const int *set, int n)
{
	for (int i = 0; i < n; i++)
		if (set[i] == t)
			return true;
	return false;
}

static bool is_opening(int t) { return t == BK_LPAR || t == BK_LSQB || t == BK_LBRACE; }
static bool is_closing(int t) { return t == BK_RPAR || t == BK_RSQB || t == BK_RBRACE; }
static bool is_bracket(int t) { return is_opening(t) || is_closing(t); }

static int bracket_of(int opening)
{
	return opening == BK_LPAR ? BK_RPAR : opening == BK_LSQB ? BK_RSQB : BK_RBRACE;
}

/* MATH_OPERATORS */
static bool is_math_operator(int t)
{
	return IN(t, BK_VBAR, BK_CIRCUMFLEX, BK_AMPER, BK_LEFTSHIFT, BK_RIGHTSHIFT, BK_PLUS, BK_MINUS,
		  BK_STAR, BK_SLASH, BK_DOUBLESLASH, BK_PERCENT, BK_AT, BK_TILDE, BK_DOUBLESTAR);
}

static bool is_varargs_parent(int t)
{
	return IN(t, BK_arglist, BK_argument, BK_trailer, BK_typedargslist, BK_varargslist);
}

static bool is_unpacking_parent(int t)
{
	return IN(t, BK_atom, BK_dictsetmaker, BK_listmaker, BK_testlist_gexp, BK_testlist_star_expr,
		  BK_subject_expr, BK_pattern);
}

static bool within(int t, int w)
{
	return ((w & W_VARARGS) && is_varargs_parent(t)) || ((w & W_UNPACKING) && is_unpacking_parent(t)) ||
	       ((w & W_TYPEDARGSLIST) && t == BK_typedargslist) ||
	       ((w & W_ARGLIST_ARGUMENT) && (t == BK_arglist || t == BK_argument));
}

static bool is_vararg(const BkNode *leaf, int w)
{
	/* VARARGS_SPECIALS = STARS | {SLASH} */
	if (!(leaf->type == BK_STAR || leaf->type == BK_DOUBLESTAR || leaf->type == BK_SLASH) || !leaf->parent)
		return false;
	const BkNode *p = leaf->parent;
	if (p->type == BK_star_expr) {
		if (!p->parent)
			return false;
		p = p->parent;
	}
	return within(p->type, w);
}

static bool has_triple_quotes(const char *s)
{
	while (*s && strchr("furbFURB", *s))
		s++;
	return !strncmp(s, "\"\"\"", 3) || !strncmp(s, "'''", 3);
}

static int string_prefix_len(const char *s)
{
	int i = 0;
	while (s[i] && strchr("furbFURB", s[i]))
		i++;
	return i;
}

static bool is_multiline_string(const BkNode *leaf) { return has_triple_quotes(leaf->value) && strchr(leaf->value, '\n'); }

static bool is_name_value(const BkNode *n, const char *v) { return n && n->type == BK_NAME && !strcmp(n->value, v); }

static int parent_type(const BkNode *n) { return n && n->parent ? n->parent->type : -1; }

static BkNode *preceding_leaf(BkNode *node)
{
	while (node) {
		BkNode *res = prev_sibling(node);
		if (res) {
			if (IS_LEAF(res))
				return res;
			return last_leaf(res);
		}
		node = node->parent;
	}
	return NULL;
}

static BkNode *child_towards(BkNode *ancestor, BkNode *descendant)
{
	BkNode *n = descendant;
	while (n && n->parent != ancestor)
		n = n->parent;
	return n;
}

static void replace_child(BkNode *old, BkNode *new_)
{
	BkNode *p = old->parent;
	if (!p)
		return;
	int i = remove_node(old);
	if (i >= 0)
		insert_child(p, i, new_);
}

/* prev_siblings_are(node, [None, NEWLINE, INDENT, simple_stmt]) and the funcdef variant. */
static bool is_docstring(BkNode *leaf)
{
	if (leaf->type != BK_STRING)
		return false;
	int pl = string_prefix_len(leaf->value);
	for (int i = 0; i < pl; i++)
		if (strchr("bBfF", leaf->value[i]))
			return false;
	BkNode *p = leaf->parent;
	if (p && p->type == BK_simple_stmt) {
		BkNode *a = prev_sibling(p);
		if (a && a->type == BK_INDENT) {
			BkNode *b = prev_sibling(a);
			if (b && b->type == BK_NEWLINE && !prev_sibling(b))
				return true;
		}
		if (a && a->type == BK_COLON) {
			BkNode *b = prev_sibling(a);
			if (b && b->type == BK_parameters)
				return true;
		}
	}
	return false;
}

static bool is_empty_tuple(const BkNode *n)
{
	return n->type == BK_atom && n->n == 2 && n->kids[0]->type == BK_LPAR && n->kids[1]->type == BK_RPAR;
}

static BkNode *unwrap_singleton_parenthesis(BkNode *n)
{
	if (IS_LEAF(n) || n->n != 3)
		return NULL;
	if (!(n->kids[0]->type == BK_LPAR && n->kids[2]->type == BK_RPAR))
		return NULL;
	return n->kids[1];
}

static bool is_one_tuple(BkNode *n)
{
	if (n->type == BK_atom) {
		BkNode *g = unwrap_singleton_parenthesis(n);
		if (!g || g->type != BK_testlist_gexp)
			return false;
		return g->n == 2 && g->kids[1]->type == BK_COMMA;
	}
	return IN(n->type, BK_testlist, BK_testlist_star_expr, BK_exprlist) && n->n == 2 &&
	       n->kids[1]->type == BK_COMMA;
}

static bool is_tuple_containing_walrus(BkNode *n)
{
	if (n->type != BK_atom)
		return false;
	BkNode *g = unwrap_singleton_parenthesis(n);
	if (!g || g->type != BK_testlist_gexp)
		return false;
	for (int i = 0; i < g->n; i++)
		if (g->kids[i]->type == BK_namedexpr_test)
			return true;
	return false;
}

static bool is_walrus_assignment(BkNode *n)
{
	BkNode *inner = unwrap_singleton_parenthesis(n);
	return inner && inner->type == BK_namedexpr_test;
}

static bool is_yield(BkNode *n)
{
	if (n->type == BK_yield_expr)
		return true;
	if (is_name_value(n, "yield"))
		return true;
	if (n->type != BK_atom || n->n != 3)
		return false;
	if (n->kids[0]->type == BK_LPAR && n->kids[2]->type == BK_RPAR)
		return is_yield(n->kids[1]);
	return false;
}

static bool is_one_sequence_between(BkNode *opening, BkNode *closing, BkNode **leaves, int n, int lb, int rb)
{
	if (opening->type != lb || closing->type != rb)
		return false;
	int depth = closing->bracket_depth + 1;
	int i = 0;
	for (; i < n; i++)
		if (leaves[i] == opening)
			break;
	if (i == n)
		fail("black: LookupError: Opening paren not found in `leaves`");
	int commas = 0;
	for (i++; i < n; i++) {
		BkNode *l = leaves[i];
		if (l == closing)
			break;
		if (l->bracket_depth == depth && l->type == BK_COMMA) {
			commas++;
			if (l->parent && (l->parent->type == BK_arglist || l->parent->type == BK_typedargslist)) {
				commas++;
				break;
			}
		}
	}
	return commas < 2;
}

static bool is_parent_function_or_class(const BkNode *n)
{
	return n->parent->type == BK_funcdef || n->parent->type == BK_classdef;
}

static bool is_stub_body(BkNode *n)
{
	if (IS_LEAF(n) || n->type != BK_simple_stmt || n->n != 2)
		return false;
	BkNode *c = n->kids[0];
	if (py_strip(str_c(node_prefix(c))).n || c->type != BK_atom || c->n != 3)
		return false;
	for (int i = 0; i < 3; i++)
		if (c->kids[i]->type != BK_DOT || strcmp(c->kids[i]->value, ".") || *c->kids[i]->prefix)
			return false;
	return true;
}

static bool is_stub_suite(BkNode *n)
{
	if (n->parent && !is_parent_function_or_class(n))
		return false;
	if (py_strip(str_c(node_prefix(n))).n)
		return false;
	if (n->n != 4 || n->kids[0]->type != BK_NEWLINE || n->kids[1]->type != BK_INDENT ||
	    n->kids[3]->type != BK_DEDENT)
		return false;
	if (py_strip(str_c(n->kids[3]->prefix)).n)
		return false;
	return is_stub_body(n->kids[2]);
}

static bool is_atom_with_invisible_parens(const BkNode *n)
{
	if (IS_LEAF(n) || n->type != BK_atom)
		return false;
	const BkNode *f = n->kids[0], *l = n->kids[n->n - 1];
	return IS_LEAF(f) && f->type == BK_LPAR && !*f->value && IS_LEAF(l) && l->type == BK_RPAR && !*l->value;
}

static bool is_import_leaf(const BkNode *leaf)
{
	const BkNode *p = leaf->parent;
	return leaf->type == BK_NAME && p &&
	       ((!strcmp(leaf->value, "import") && p->type == BK_import_name) ||
		(!strcmp(leaf->value, "from") && p->type == BK_import_from));
}

static bool is_with_or_async_with_stmt(const BkNode *leaf)
{
	if (is_name_value(leaf, "with") && leaf->parent && leaf->parent->type == BK_with_stmt)
		return true;
	BkNode *nx = next_sibling(leaf);
	return leaf->type == BK_ASYNC && nx && nx->type == BK_with_stmt;
}

static bool is_async_stmt_or_funcdef(const BkNode *leaf)
{
	return leaf->type == BK_ASYNC && leaf->parent &&
	       (leaf->parent->type == BK_async_stmt || leaf->parent->type == BK_async_funcdef);
}

static void wrap_in_parentheses(BkNode *parent, BkNode *child, bool visible)
{
	BkNode *lpar = new_leaf(BK_LPAR, visible ? "(" : "");
	BkNode *rpar = new_leaf(BK_RPAR, visible ? ")" : "");
	const char *prefix = node_prefix(child);
	bk_set_prefix(child, "");
	int index = remove_node(child);
	if (index < 0)
		index = 0;
	BkNode *kids[3] = {lpar, child, rpar};
	BkNode *atom = new_node(BK_atom, 3, kids);
	lpar->prefix = prefix;
	insert_child(parent, index, atom);
}

static void ensure_visible(BkNode *leaf)
{
	if (leaf->type == BK_LPAR)
		leaf->value = "(";
	else if (leaf->type == BK_RPAR)
		leaf->value = ")";
}

static int get_annotation_type(const BkNode *leaf)
{
	const BkNode *a = leaf->parent;
	while (a) {
		BkNode *ps = prev_sibling(a);
		if (ps && ps->type == BK_RARROW)
			return 'r';
		if (a->parent && a->parent->type == BK_tname)
			return 'p';
		a = a->parent;
	}
	return 0;
}

static bool is_arith_like(const BkNode *n)
{
	return IN(n->type, BK_arith_expr, BK_shift_expr, BK_xor_expr, BK_and_expr);
}

static bool is_typed_name(int t) { return t == BK_tname || t == BK_tname_star; }

/* whitespace(leaf, complex_subscript=..., mode=...) */
static const char *whitespace(BkNode *leaf, bool complex_subscript)
{
	static const char NO[] = "", SPACE[] = " ", DOUBLESPACE[] = "  ";
	int t = leaf->type;
	BkNode *p = leaf->parent;
	const char *v = leaf->value;
	if (is_closing(t) || t == BK_COMMA || t == BK_STANDALONE_COMMENT)
		return NO;
	if (t == BK_COMMENT)
		return DOUBLESPACE;
	if (!p)
		fail("black: INTERNAL ERROR: hand-made leaf without parent");
	if (t == BK_COLON && !IN(p->type, BK_subscript, BK_subscriptlist, BK_sliceop))
		return NO;

	BkNode *prev = prev_sibling(leaf);
	if (!prev) {
		BkNode *prevp = preceding_leaf(p);
		if (!prevp || is_opening(prevp->type))
			return NO;
		if (t == BK_COLON) {
			if (prevp->type == BK_COLON)
				return NO;
			else if (prevp->type != BK_COMMA && !complex_subscript)
				return NO;
			return SPACE;
		}
		if (prevp->type == BK_EQUAL) {
			if (prevp->parent) {
				if (IN(prevp->parent->type, BK_arglist, BK_argument, BK_parameters, BK_varargslist))
					return NO;
				else if (prevp->parent->type == BK_typedargslist)
					return prevp->prefix;
			}
		} else if (prevp->type == BK_STAR && parent_type(prevp) == BK_star_expr &&
			   parent_type(prevp->parent) == BK_subscriptlist) {
			return NO;
		} else if (prevp->type == BK_STAR || prevp->type == BK_DOUBLESTAR || prevp->type == BK_SLASH) {
			if (is_vararg(prevp, W_VARARGS | W_UNPACKING))
				return NO;
		} else if (prevp->type == BK_COLON) {
			if (prevp->parent && (prevp->parent->type == BK_subscript || prevp->parent->type == BK_sliceop))
				return complex_subscript ? SPACE : NO;
		} else if (prevp->parent && prevp->parent->type == BK_factor && is_math_operator(prevp->type)) {
			return NO;
		} else if (prevp->type == BK_AT && p->parent && p->parent->type == BK_decorator) {
			return NO;
		}
	} else if (is_opening(prev->type)) {
		return NO;
	}

	if (p->type == BK_parameters || p->type == BK_arglist) {
		if (!prev || prev->type != BK_COMMA)
			return NO;
	} else if (p->type == BK_varargslist) {
		if (prev && prev->type != BK_COMMA)
			return NO;
	} else if (p->type == BK_typedargslist) {
		if (!prev)
			return NO;
		if (t == BK_EQUAL) {
			if (!is_typed_name(prev->type))
				return NO;
		} else if (prev->type == BK_EQUAL) {
			return prev->prefix;
		} else if (prev->type != BK_COMMA) {
			return NO;
		}
	} else if (is_typed_name(p->type)) {
		if (!prev) {
			BkNode *prevp = preceding_leaf(p);
			if (!prevp || prevp->type != BK_COMMA)
				return NO;
		}
	} else if (p->type == BK_trailer) {
		if (t == BK_LPAR || t == BK_RPAR)
			return NO;
		if (!prev) {
			if (t == BK_DOT || t == BK_LSQB)
				return NO;
		} else if (prev->type != BK_COMMA) {
			return NO;
		}
	} else if (p->type == BK_argument) {
		if (t == BK_EQUAL)
			return NO;
		if (!prev) {
			BkNode *prevp = preceding_leaf(p);
			if (!prevp || prevp->type == BK_LPAR)
				return NO;
		} else if (prev->type == BK_EQUAL || prev->type == BK_STAR || prev->type == BK_DOUBLESTAR ||
			   prev->type == BK_SLASH) {
			return NO;
		}
	} else if (p->type == BK_decorator) {
		return NO;
	} else if (p->type == BK_dotted_name) {
		if (prev)
			return NO;
		BkNode *prevp = preceding_leaf(p);
		if (!prevp || prevp->type == BK_AT || prevp->type == BK_DOT)
			return NO;
	} else if (p->type == BK_classdef) {
		if (t == BK_LPAR)
			return NO;
		if (prev && prev->type == BK_LPAR)
			return NO;
	} else if (p->type == BK_subscript || p->type == BK_sliceop) {
		if (!prev) {
			if (p->parent->type == BK_subscriptlist)
				return SPACE;
			return NO;
		} else if (t == BK_COLONEQUAL || prev->type == BK_COLONEQUAL) {
			return SPACE;
		} else if (!complex_subscript) {
			return NO;
		}
	} else if (p->type == BK_atom) {
		if (prev && t == BK_DOT)
			return NO;
	} else if (p->type == BK_dictsetmaker) {
		if (prev && prev->type == BK_DOUBLESTAR)
			return NO;
	} else if (p->type == BK_factor || p->type == BK_star_expr) {
		if (!prev) {
			BkNode *prevp = preceding_leaf(p);
			if (!prevp || is_opening(prevp->type))
				return NO;
			BkNode *pp = prevp->parent;
			if (prevp->type == BK_COLON && (pp->type == BK_subscript || pp->type == BK_sliceop))
				return NO;
			else if (prevp->type == BK_EQUAL && pp->type == BK_argument)
				return NO;
		} else if (t == BK_NAME || t == BK_NUMBER || t == BK_STRING) {
			return NO;
		}
	} else if (p->type == BK_import_from) {
		if (t == BK_DOT) {
			if (prev && prev->type == BK_DOT)
				return NO;
		} else if (t == BK_NAME) {
			if (!strcmp(v, "import"))
				return SPACE;
			if (prev && prev->type == BK_DOT)
				return NO;
		}
	} else if (p->type == BK_except_clause) {
		if (t == BK_STAR)
			return NO;
	}
	return SPACE;
}
