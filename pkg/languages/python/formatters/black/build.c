/* Builders of the blib2to3 nodes for the grammar productions of black.h. */

BkNode *bk_name(const char *v) { return new_leaf(BK_NAME, v); }
BkNode *bk_number(const char *v) { return new_leaf(BK_NUMBER, v); }
BkNode *bk_string(const char *v) { return new_leaf(BK_STRING, v); }

/* power: [AWAIT] atom trailer* */
static BkNode *with_trailer(BkNode *base, BkNode *trailer)
{
	if (base->type == BK_power && !base->parent) {
		/* a power that ends with `** factor` cannot take a trailer; none is built here */
		append_child(base, trailer);
		return base;
	}
	BkNode *kids[2] = {base, trailer};
	return new_node(BK_power, 2, kids);
}

BkNode *bk_attr(BkNode *base, const char *name)
{
	BkNode *kids[2] = {new_leaf(BK_DOT, "."), new_leaf(BK_NAME, name)};
	return with_trailer(base, new_node(BK_trailer, 2, kids));
}

/* `a, b, c` as one node: a single item stays itself (pytree.convert). */
static BkNode *comma_list(BkType type, int n, BkNode *const *items)
{
	BkNode **kids = xalloc(sizeof *kids * (size_t)(2 * n));
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (i)
			kids[k++] = new_leaf(BK_COMMA, ",");
		kids[k++] = items[i];
	}
	return bk_node(type, k, kids);
}

static BkNode *call(BkNode *callee, int n, BkNode *const *args, bool trailing_comma)
{
	BkNode *lp = new_leaf(BK_LPAR, "("), *rp = new_leaf(BK_RPAR, ")");
	BkNode *t;
	if (n) {
		BkNode *list = comma_list(BK_arglist, n, args);
		if (trailing_comma) {
			if (list->type != BK_arglist) {
				BkNode *one[1] = {list};
				list = new_node(BK_arglist, 1, one);
			}
			append_child(list, new_leaf(BK_COMMA, ","));
		}
		BkNode *kids[3] = {lp, list, rp};
		t = new_node(BK_trailer, 3, kids);
	} else {
		BkNode *kids[2] = {lp, rp};
		t = new_node(BK_trailer, 2, kids);
	}
	return with_trailer(callee, t);
}

BkNode *bk_call(BkNode *callee, int n, BkNode *const *args) { return call(callee, n, args, false); }

BkNode *bk_call_trailing_comma(BkNode *callee, int n, BkNode *const *args) { return call(callee, n, args, true); }

BkNode *bk_subscript(BkNode *base, int n, BkNode *const *items)
{
	BkNode *kids[3] = {new_leaf(BK_LSQB, "["), comma_list(BK_subscriptlist, n, items), new_leaf(BK_RSQB, "]")};
	return with_trailer(base, new_node(BK_trailer, 3, kids));
}

BkNode *bk_dotted(const char *dotted)
{
	const char *dot = strchr(dotted, '.');
	BkNode *n = new_leaf(BK_NAME, dot ? xstrndup(dotted, (size_t)(dot - dotted)) : dotted);
	while (dot) {
		const char *s = dot + 1;
		dot = strchr(s, '.');
		n = bk_attr(n, dot ? xstrndup(s, (size_t)(dot - s)) : s);
	}
	return n;
}

BkNode *bk_kwarg(const char *name, BkNode *value)
{
	BkNode *kids[3] = {new_leaf(BK_NAME, name), new_leaf(BK_EQUAL, "="), value};
	return new_node(BK_argument, 3, kids);
}

BkNode *bk_neg(BkNode *operand)
{
	BkNode *kids[2] = {new_leaf(BK_MINUS, "-"), operand};
	return new_node(BK_factor, 2, kids);
}

BkNode *bk_await(BkNode *expr)
{
	BkNode *aw = new_leaf(BK_AWAIT, "await");
	if (expr->type == BK_power) {
		insert_child(expr, 0, aw);
		return expr;
	}
	BkNode *kids[2] = {aw, expr};
	return new_node(BK_power, 2, kids);
}

BkNode *bk_empty_list(void)
{
	BkNode *kids[2] = {new_leaf(BK_LSQB, "["), new_leaf(BK_RSQB, "]")};
	return new_node(BK_atom, 2, kids);
}

BkNode *bk_empty_tuple(void)
{
	BkNode *kids[2] = {new_leaf(BK_LPAR, "("), new_leaf(BK_RPAR, ")")};
	return new_node(BK_atom, 2, kids);
}

BkNode *bk_is_not(BkNode *a, BkNode *b)
{
	BkNode *op[2] = {new_leaf(BK_NAME, "is"), new_leaf(BK_NAME, "not")};
	BkNode *kids[3] = {a, new_node(BK_comp_op, 2, op), b};
	return new_node(BK_comparison, 3, kids);
}

BkNode *bk_yield(BkNode *v)
{
	BkNode *kids[2] = {new_leaf(BK_NAME, "yield"), v};
	return new_node(BK_yield_expr, 2, kids);
}

BkNode *bk_stmt(const char *prefix, BkNode *small)
{
	BkNode *kids[2] = {small, new_leaf(BK_NEWLINE, "\n")};
	BkNode *s = new_node(BK_simple_stmt, 2, kids);
	bk_set_prefix(s, prefix);
	return s;
}

BkNode *bk_assign(BkNode *target, BkNode *value)
{
	BkNode *kids[3] = {target, new_leaf(BK_EQUAL, "="), value};
	return new_node(BK_expr_stmt, 3, kids);
}

BkNode *bk_annassign(BkNode *target, BkNode *annotation, BkNode *value)
{
	BkNode *a[4] = {new_leaf(BK_COLON, ":"), annotation, new_leaf(BK_EQUAL, "="), value};
	BkNode *kids[2] = {target, new_node(BK_annassign, 4, a)};
	return new_node(BK_expr_stmt, 2, kids);
}

BkNode *bk_return(BkNode *value)
{
	BkNode *kids[2] = {new_leaf(BK_NAME, "return"), value};
	return new_node(BK_return_stmt, 2, kids);
}

static BkNode *dotted_name(const char *dotted)
{
	size_t parts = 1;
	for (const char *s = dotted; (s = strchr(s, '.')); s++)
		parts++;
	BkNode **kids = xalloc(sizeof *kids * (2 * parts - 1));
	int k = 0;
	for (const char *s = dotted;;) {
		const char *dot = strchr(s, '.');
		kids[k++] = new_leaf(BK_NAME, dot ? xstrndup(s, (size_t)(dot - s)) : s);
		if (!dot)
			break;
		kids[k++] = new_leaf(BK_DOT, ".");
		s = dot + 1;
	}
	return bk_node(BK_dotted_name, k, kids);
}

BkNode *bk_import_from(int dots, const char *module, int n, const char *const *names)
{
	BkNode *node = new_node(BK_import_from, 0, NULL);
	append_child(node, new_leaf(BK_NAME, "from"));
	for (int i = 0; i < dots; i++)
		append_child(node, new_leaf(BK_DOT, "."));
	if (*module) {
		/* the leading dots are separate tokens, then a dotted_name or a NAME */
		append_child(node, dotted_name(module));
	}
	append_child(node, new_leaf(BK_NAME, "import"));
	BkNode **items = xalloc(sizeof *items * (size_t)n);
	for (int i = 0; i < n; i++)
		items[i] = new_leaf(BK_NAME, names[i]);
	append_child(node, comma_list(BK_import_as_names, n, items));
	return node;
}

BkNode *bk_import(const char *module)
{
	BkNode *kids[2] = {new_leaf(BK_NAME, "import"), dotted_name(module)};
	return new_node(BK_import_name, 2, kids);
}

BkNode *bk_suite(int n, BkNode *const *stmts)
{
	BkNode **kids = xalloc(sizeof *kids * (size_t)(n + 3));
	kids[0] = new_leaf(BK_NEWLINE, "\n");
	kids[1] = new_leaf(BK_INDENT, "");
	for (int i = 0; i < n; i++)
		kids[i + 2] = stmts[i];
	kids[n + 2] = new_leaf(BK_DEDENT, "");
	return new_node(BK_suite, n + 3, kids);
}

BkNode *bk_classdef(const char *prefix, const char *name, BkNode *base, BkNode *suite)
{
	BkNode *node = new_node(BK_classdef, 0, NULL);
	append_child(node, new_leaf(BK_NAME, "class"));
	append_child(node, new_leaf(BK_NAME, name));
	if (base) {
		append_child(node, new_leaf(BK_LPAR, "("));
		append_child(node, base);
		append_child(node, new_leaf(BK_RPAR, ")"));
	}
	append_child(node, new_leaf(BK_COLON, ":"));
	append_child(node, suite);
	bk_set_prefix(node, prefix);
	return node;
}

BkNode *bk_decorated(const char *prefix, const char *decorator, BkNode *def)
{
	BkNode *d[3] = {new_leaf(BK_AT, "@"), bk_dotted(decorator), new_leaf(BK_NEWLINE, "\n")};
	BkNode *kids[2] = {new_node(BK_decorator, 3, d), def};
	BkNode *node = new_node(BK_decorated, 2, kids);
	bk_set_prefix(node, prefix);
	return node;
}

/* A parameter as a temporary typedargslist fragment: [tname or NAME] or [tname, '=', default]. */
BkNode *bk_param(const char *name, BkNode *annotation, BkNode *dflt)
{
	BkNode *p = new_leaf(BK_NAME, name);
	if (annotation) {
		BkNode *t[3] = {p, new_leaf(BK_COLON, ":"), annotation};
		p = new_node(BK_tname, 3, t);
	}
	if (!dflt)
		return p;
	BkNode *kids[3] = {p, new_leaf(BK_EQUAL, "="), dflt};
	return new_node(BK_typedargslist, 3, kids); /* unpacked by bk_funcdef */
}

BkNode *bk_star_param(void) { return new_leaf(BK_STAR, "*"); }

BkNode *bk_funcdef(const char *prefix, const char *name, int n, BkNode *const *params, BkNode *returns,
		   BkNode *suite)
{
	BkNode **items = xalloc(sizeof *items * (size_t)(4 * n + 1));
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (i)
			items[k++] = new_leaf(BK_COMMA, ",");
		BkNode *p = params[i];
		if (p->type == BK_typedargslist) {
			for (int j = 0; j < 3; j++) {
				BkNode *c = p->kids[0];
				remove_node(c);
				items[k++] = c;
			}
		} else {
			items[k++] = p;
		}
	}
	BkNode *lp = new_leaf(BK_LPAR, "("), *rp = new_leaf(BK_RPAR, ")");
	BkNode *parameters;
	if (k) {
		BkNode *kids[3] = {lp, bk_node(BK_typedargslist, k, items), rp};
		parameters = new_node(BK_parameters, 3, kids);
	} else {
		BkNode *kids[2] = {lp, rp};
		parameters = new_node(BK_parameters, 2, kids);
	}
	BkNode *node = new_node(BK_funcdef, 0, NULL);
	append_child(node, new_leaf(BK_NAME, "def"));
	append_child(node, new_leaf(BK_NAME, name));
	append_child(node, parameters);
	if (returns) {
		append_child(node, new_leaf(BK_RARROW, "->"));
		append_child(node, returns);
	}
	append_child(node, new_leaf(BK_COLON, ":"));
	append_child(node, suite);
	bk_set_prefix(node, prefix);
	return node;
}

BkNode *bk_async(const char *prefix, BkNode *stmt)
{
	BkNode *kids[2] = {new_leaf(BK_ASYNC, "async"), stmt};
	BkNode *node = new_node(BK_async_stmt, 2, kids);
	bk_set_prefix(node, prefix);
	return node;
}

BkNode *bk_if(const char *prefix, BkNode *test, BkNode *suite)
{
	BkNode *kids[4] = {new_leaf(BK_NAME, "if"), test, new_leaf(BK_COLON, ":"), suite};
	BkNode *node = new_node(BK_if_stmt, 4, kids);
	bk_set_prefix(node, prefix);
	return node;
}

BkNode *bk_for(const char *prefix, BkNode *target, BkNode *iter, BkNode *suite)
{
	BkNode *kids[6] = {new_leaf(BK_NAME, "for"), target, new_leaf(BK_NAME, "in"), iter, new_leaf(BK_COLON, ":"),
			   suite};
	BkNode *node = new_node(BK_for_stmt, 6, kids);
	bk_set_prefix(node, prefix);
	return node;
}

BkNode *bk_file(int n, BkNode *const *stmts, const char *trailing)
{
	BkNode **kids = xalloc(sizeof *kids * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		kids[i] = stmts[i];
	kids[n] = new_leaf(BK_ENDMARKER, "");
	kids[n]->prefix = trailing;
	return new_node(BK_file_input, n + 1, kids);
}
