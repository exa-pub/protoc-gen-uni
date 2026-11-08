/* black/linegen.py: LineGenerator, transform_line and the splits (rhs, lhs, delimiter_split).
 * Python generators are evaluated eagerly where their consumers act on disjoint leaves;
 * generate_trailers_to_omit stays lazy (TrailerIter) because right_hand_split changes the leaves
 * it has yet to look at. CannotSplit / CannotTransform are `false` results. */

/* ---- normalize_invisible_parens ---- */

static bool maybe_make_parens_invisible_in_atom(BkNode *node, BkNode *parent, bool remove_brackets_around_comma)
{
	if ((node->type != BK_atom && node->type != BK_expr) || is_empty_tuple(node) || is_one_tuple(node) ||
	    (is_yield(node) && parent->type != BK_expr_stmt) ||
	    (!remove_brackets_around_comma && max_delimiter_priority_in_atom(node) >= COMMA_PRIORITY) ||
	    is_tuple_containing_walrus(node))
		return false;
	if (is_walrus_assignment(node) &&
	    IN(parent->type, BK_annassign, BK_expr_stmt, BK_assert_stmt, BK_return_stmt, BK_except_clause, BK_funcdef,
	       BK_with_stmt, BK_tname, BK_for_stmt, BK_del_stmt))
		return false;
	BkNode *first = node->kids[0], *last = node->kids[node->n - 1];
	if (first->type == BK_LPAR && last->type == BK_RPAR) {
		BkNode *middle = node->kids[1];
		Str mp = py_strip(str_c(node_prefix(middle)));
		if (!(mp.n >= 14 && !memcmp(mp.p, "# type: ignore", 14))) {
			first->value = "";
			if (py_strip(str_c(first->prefix)).n)
				bk_set_prefix(middle, xcat(first->prefix, node_prefix(middle)));
			last->value = "";
		}
		maybe_make_parens_invisible_in_atom(middle, parent, remove_brackets_around_comma);
		if (is_atom_with_invisible_parens(middle)) {
			BkNode *inner = middle->kids[1];
			BkNode *mlast = middle->kids[middle->n - 1];
			replace_node(middle, inner);
			if (py_strip(str_c(node_prefix(mlast))).n)
				last->prefix = xcat(node_prefix(mlast), last->prefix);
		}
		return false;
	}
	return true;
}

static bool value_in(const char *v, const char *const *set)
{
	for (; *set; set++)
		if (!strcmp(v, *set))
			return true;
	return false;
}

static const char *const ASSIGNMENTS[] = {"=", "+=", "-=", "*=", "@=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
					  "**=", "//=", ":", NULL};

/* normalize_invisible_parens(node, parens_after). The "# fmt: off" check is not needed: such
 * comments are not supported (bk_comment_ok). Context managers (with_stmt) do not occur. */
static void normalize_invisible_parens(BkNode *node, const char *const *parens_after)
{
	int n = node->n;
	BkNode **kids = xalloc(sizeof *kids * (size_t)(n ? n : 1));
	memcpy(kids, node->kids, sizeof *kids * (size_t)n);
	bool check_lpar = false;
	for (int index = 0; index < n; index++) {
		BkNode *child = kids[index];
		if (!IS_LEAF(child) && child->type == BK_annassign)
			normalize_invisible_parens(child, parens_after);
		if (index == 0 && !IS_LEAF(child) && child->type == BK_testlist_star_expr)
			check_lpar = true;
		if (check_lpar) {
			BkNode *ps = prev_sibling(child);
			if (child->type == BK_atom && node->type == BK_for_stmt && ps && IS_LEAF(ps) && ps->type == BK_NAME &&
			    !strcmp(ps->value, "for")) {
				if (maybe_make_parens_invisible_in_atom(child, node, true))
					wrap_in_parentheses(node, child, false);
			} else if (child->type == BK_atom) {
				if (maybe_make_parens_invisible_in_atom(child, node, false))
					wrap_in_parentheses(node, child, false);
			} else if (is_one_tuple(child)) {
				wrap_in_parentheses(node, child, true);
			} else if (node->type == BK_import_from) {
				/* _normalize_import_from */
				if (child->type == BK_LPAR) {
					child->value = "";
					node->kids[node->n - 1]->value = "";
				} else if (child->type != BK_STAR) {
					insert_child(node, index, new_leaf(BK_LPAR, ""));
					append_child(node, new_leaf(BK_RPAR, ""));
				}
				break;
			} else if (index == 1 && child->type == BK_STAR && node->type == BK_except_clause) {
				continue;
			} else if (IS_LEAF(child) && next_sibling(child) && next_sibling(child)->type == BK_COLON &&
				   !strcmp(child->value, "case")) {
				break;
			} else if (!(IS_LEAF(child) && is_multiline_string(child))) {
				wrap_in_parentheses(node, child, false);
			}
		}
		bool comma_check = child->type == BK_COMMA;
		check_lpar = IS_LEAF(child) && (value_in(child->value, parens_after) || comma_check);
	}
}

/* ---- LineGenerator ---- */

typedef struct {
	Line *current_line;
	EmitFn emit;
	void *ctx;
} LineGen;

static void lg_line(LineGen *g, int indent)
{
	Line *cur = g->current_line;
	if (!cur->n) {
		cur->depth += indent;
		return;
	}
	if (cur->n == 1 && is_async_stmt_or_funcdef(cur->leaves[0]))
		return;
	g->current_line = line_new(cur->depth + indent, false);
	g->emit(g->ctx, cur);
}

static void lg_visit(LineGen *g, BkNode *node);

static void lg_visit_default(LineGen *g, BkNode *node)
{
	if (IS_LEAF(node)) {
		bool any_open = bt_any_open_brackets(&g->current_line->bt);
		BkNode **cs;
		int nc = generate_comments(node, &cs);
		for (int i = 0; i < nc; i++) {
			if (any_open) {
				line_append(g->current_line, cs[i], false, false);
			} else if (cs[i]->type == BK_COMMENT) {
				line_append(g->current_line, cs[i], false, false);
				lg_line(g, 0);
			} else {
				lg_line(g, 0);
				line_append(g->current_line, cs[i], false, false);
				lg_line(g, 0);
			}
		}
		if (any_open)
			node->prefix = "";
		/* normalize_string_prefix, normalize_string_quotes and normalize_numeric_literal leave
		 * the supported STRING and NUMBER leaves unchanged (black.h). */
		if (node->type != BK_DEDENT && node->type != BK_INDENT && node->type != BK_NEWLINE)
			line_append(g->current_line, node, false, false);
		return;
	}
	for (int i = 0; i < node->n; i++)
		lg_visit(g, node->kids[i]);
}

static void lg_visit_stmt(LineGen *g, BkNode *node, const char *const *keywords, const char *const *parens)
{
	normalize_invisible_parens(node, parens);
	for (int i = 0; i < node->n; i++) {
		BkNode *child = node->kids[i];
		if (child->type == BK_NAME && value_in(child->value, keywords))
			lg_line(g, 0);
		lg_visit(g, child);
	}
}

static void lg_visit_funcdef(LineGen *g, BkNode *node)
{
	lg_line(g, 0);
	bool is_return_annotation = false;
	for (int i = 0; i < node->n; i++) {
		BkNode *child = node->kids[i];
		if (child->type == BK_RARROW) {
			is_return_annotation = true;
		} else if (is_return_annotation) {
			if (child->type == BK_atom && child->kids[0]->type == BK_LPAR) {
				if (maybe_make_parens_invisible_in_atom(child, node, false))
					wrap_in_parentheses(node, child, false);
			} else {
				wrap_in_parentheses(node, child, false);
			}
			is_return_annotation = false;
		}
	}
	for (int i = 0; i < node->n; i++)
		lg_visit(g, node->kids[i]);
}

static bool is_statement_type(int t)
{
	return IN(t, BK_if_stmt, BK_while_stmt, BK_for_stmt, BK_try_stmt, BK_except_clause, BK_with_stmt, BK_funcdef,
		  BK_classdef, BK_match_stmt, BK_case_block);
}

static void lg_visit_simple_stmt(LineGen *g, BkNode *node)
{
	int prev_type = -1;
	for (int i = 0; i < node->n; i++) {
		BkNode *child = node->kids[i];
		if ((prev_type == -1 || prev_type == BK_SEMI) && is_arith_like(child))
			wrap_in_parentheses(node, child, false);
		prev_type = child->type;
	}
	if (node->parent && is_statement_type(node->parent->type)) {
		if (is_parent_function_or_class(node) && is_stub_body(node)) {
			lg_visit_default(g, node);
		} else {
			lg_line(g, 1);
			lg_visit_default(g, node);
			lg_line(g, -1);
		}
	} else {
		if (!node->parent || !is_stub_suite(node->parent))
			lg_line(g, 0);
		lg_visit_default(g, node);
	}
}

static void lg_visit_async_stmt(LineGen *g, BkNode *node)
{
	lg_line(g, 0);
	int i = 0;
	for (; i < node->n; i++) {
		BkNode *child = node->kids[i];
		lg_visit(g, child);
		if (child->type == BK_ASYNC || child->type == BK_STANDALONE_COMMENT)
			break;
	}
	if (i + 1 < node->n)
		lg_visit(g, node->kids[i + 1]);
}

static void remove_await_parens(BkNode *node)
{
	if (node->kids[0]->type == BK_AWAIT && node->n > 1) {
		BkNode *a = node->kids[1];
		if (a->type == BK_atom && a->kids[0]->type == BK_LPAR) {
			if (maybe_make_parens_invisible_in_atom(a, node, true))
				wrap_in_parentheses(node, a, false);
			a = node->kids[1];
			BkNode *ob = a->kids[0], *cb = a->kids[a->n - 1], *contents = a->kids[1];
			if (!IS_LEAF(contents)) {
				bool ds = false;
				for (int i = 0; i < contents->n; i++)
					if (IS_LEAF(contents->kids[i]) && contents->kids[i]->type == BK_DOUBLESTAR)
						ds = true;
				if (contents->type != BK_power || contents->kids[0]->type == BK_AWAIT || ds) {
					ensure_visible(ob);
					ensure_visible(cb);
				}
			}
		}
	}
}

static void lg_visit_power(LineGen *g, BkNode *node)
{
	for (int idx = 0; idx + 1 < node->n; idx++) {
		BkNode *leaf = node->kids[idx], *next = node->kids[idx + 1];
		if (!IS_LEAF(leaf))
			continue;
		if (leaf->type == BK_NUMBER && next->type == BK_trailer && next->kids[0]->type == BK_DOT) {
			char *v = xstrdup(leaf->value);
			for (char *q = v; *q; q++)
				if (*q >= 'A' && *q <= 'Z')
					*q = (char)(*q - 'A' + 'a');
			if (!has_prefix(v, "0x") && !has_prefix(v, "0b") && !has_prefix(v, "0o") && !strchr(v, 'j'))
				wrap_in_parentheses(node, leaf, true);
		}
	}
	remove_await_parens(node);
	lg_visit_default(g, node);
}

static void lg_visit_factor(LineGen *g, BkNode *node)
{
	BkNode *operand = node->kids[1];
	if (operand->type == BK_power && operand->n == 3 && operand->kids[1]->type == BK_DOUBLESTAR) {
		BkNode *lpar = new_leaf(BK_LPAR, "("), *rpar = new_leaf(BK_RPAR, ")");
		int index = remove_node(operand);
		if (index < 0)
			index = 0;
		BkNode *kids[3] = {lpar, operand, rpar};
		insert_child(node, index, new_node(BK_atom, 3, kids));
	}
	lg_visit_default(g, node);
}

static void lg_visit_tname(LineGen *g, BkNode *node)
{
	if (node->n != 3)
		fail("black: AssertionError: tname");
	if (maybe_make_parens_invisible_in_atom(node->kids[2], node, false))
		wrap_in_parentheses(node, node->kids[2], false);
	lg_visit_default(g, node);
}

static void lg_visit_test(LineGen *g, BkNode *node)
{
	BkNode *ps = prev_sibling(node);
	if (!(ps && ps->type == BK_LPAR)) {
		BkNode *lpar = new_leaf(BK_LPAR, ""), *rpar = new_leaf(BK_RPAR, "");
		const char *prefix = node_prefix(node);
		bk_set_prefix(node, "");
		lpar->prefix = prefix;
		insert_child(node, 0, lpar);
		append_child(node, rpar);
	}
	lg_visit_default(g, node);
}

/* re.search(r"\\\s*\n", value) */
static bool has_backslash_newline(const char *v)
{
	for (const char *p = strchr(v, '\\'); p; p = strchr(p + 1, '\\')) {
		const char *q = p + 1;
		while (*q) {
			uint32_t r;
			int w = utf8_decode(q, strlen(q), &r);
			if (r == '\n')
				return true;
			if (!py_isspace(r) || (r == 0xfffd && w == 1))
				break;
			q += w;
		}
	}
	return false;
}

static void lg_visit_STRING(LineGen *g, BkNode *leaf)
{
	if (is_docstring(leaf) && !has_backslash_newline(leaf->value)) {
		/* normalize_string_prefix and normalize_string_quotes keep supported docstrings */
		const char *ds = leaf->value;
		int pl = string_prefix_len(ds);
		const char *prefix = xstrndup(ds, (size_t)pl);
		ds += pl;
		char quote_char = ds[0];
		size_t quote_len = ds[1] != quote_char ? 1 : 3;
		size_t dn = strlen(ds);
		const char *body = ds + (dn >= 2 * quote_len ? quote_len : dn);
		size_t bn = dn >= 2 * quote_len ? dn - 2 * quote_len : 0;
		bool started_empty = bn == 0;
		char *indent = xalloc((size_t)(4 * g->current_line->depth) + 1);
		memset(indent, ' ', (size_t)(4 * g->current_line->depth));
		char *doc;
		if (is_multiline_string(leaf)) {
			doc = fix_docstring(body, bn, indent);
		} else {
			Str s = py_strip((Str){body, bn});
			doc = xstrndup(s.p, s.n);
		}
		bool has_trailing_backslash = false;
		size_t n = strlen(doc);
		if (n) {
			if (doc[0] == quote_char)
				doc = xcat(" ", doc);
			n = strlen(doc);
			if (doc[n - 1] == quote_char)
				doc = xcat(doc, " ");
			n = strlen(doc);
			if (doc[n - 1] == '\\') {
				size_t k = 0;
				while (k < n && doc[n - 1 - k] == '\\')
					k++;
				if (k % 2) {
					doc = xcat(doc, " ");
					has_trailing_backslash = true;
				}
			}
		} else if (!started_empty) {
			doc = " ";
		}
		char quote[4] = {quote_char, quote_char, quote_char, 0};
		quote[quote_len] = 0;
		if (quote_len == 3) {
			Str *lines;
			int nl = py_splitlines(str_c(doc), &lines);
			size_t last_line_length = *doc && nl ? py_len(lines[nl - 1].p, lines[nl - 1].n) : 0;
			if (nl > 1 && last_line_length + quote_len > LINE_LENGTH && strlen(indent) + quote_len <= LINE_LENGTH &&
			    !has_trailing_backslash)
				leaf->value = xcat(prefix, quote, doc, "\n", indent, quote);
			else
				leaf->value = xcat(prefix, quote, doc, quote);
		} else {
			leaf->value = xcat(prefix, quote, doc, quote);
		}
	}
	lg_visit_default(g, leaf);
}

static const char *const KW_EMPTY[] = {NULL};

static void lg_visit(LineGen *g, BkNode *node)
{
	switch (node->type) {
	case BK_INDENT:
		lg_line(g, 1);
		lg_visit_default(g, node);
		return;
	case BK_DEDENT:
		lg_line(g, 0);
		lg_visit_default(g, node);
		lg_line(g, -1);
		return;
	case BK_ENDMARKER:
		lg_visit_default(g, node);
		lg_line(g, 0);
		return;
	case BK_STANDALONE_COMMENT:
		if (!bt_any_open_brackets(&g->current_line->bt))
			lg_line(g, 0);
		lg_visit_default(g, node);
		return;
	case BK_SEMI:
		lg_line(g, 0);
		return;
	case BK_STRING:
		lg_visit_STRING(g, node);
		return;
	case BK_test:
		lg_visit_test(g, node);
		return;
	case BK_funcdef:
		lg_visit_funcdef(g, node);
		return;
	case BK_suite:
		if (is_stub_suite(node))
			lg_visit(g, node->kids[2]);
		else
			lg_visit_default(g, node);
		return;
	case BK_simple_stmt:
		lg_visit_simple_stmt(g, node);
		return;
	case BK_async_stmt:
	case BK_async_funcdef:
		lg_visit_async_stmt(g, node);
		return;
	case BK_decorators:
	case BK_decorated:
		for (int i = 0; i < node->n; i++) {
			lg_line(g, 0);
			lg_visit(g, node->kids[i]);
		}
		return;
	case BK_power:
		lg_visit_power(g, node);
		return;
	case BK_factor:
		lg_visit_factor(g, node);
		return;
	case BK_tname:
		lg_visit_tname(g, node);
		return;
	case BK_assert_stmt:
		lg_visit_stmt(g, node, (const char *const[]){"assert", NULL}, (const char *const[]){"assert", ",", NULL});
		return;
	case BK_if_stmt:
		lg_visit_stmt(g, node, (const char *const[]){"if", "else", "elif", NULL}, (const char *const[]){"if", "elif", NULL});
		return;
	case BK_while_stmt:
		lg_visit_stmt(g, node, (const char *const[]){"while", "else", NULL}, (const char *const[]){"while", NULL});
		return;
	case BK_for_stmt:
		lg_visit_stmt(g, node, (const char *const[]){"for", "else", NULL}, (const char *const[]){"for", "in", NULL});
		return;
	case BK_try_stmt:
		lg_visit_stmt(g, node, (const char *const[]){"try", "except", "else", "finally", NULL}, KW_EMPTY);
		return;
	case BK_except_clause:
		lg_visit_stmt(g, node, (const char *const[]){"except", NULL}, (const char *const[]){"except", NULL});
		return;
	case BK_classdef:
		lg_visit_stmt(g, node, (const char *const[]){"class", NULL}, KW_EMPTY);
		return;
	case BK_expr_stmt:
		lg_visit_stmt(g, node, KW_EMPTY, ASSIGNMENTS);
		return;
	case BK_return_stmt:
		lg_visit_stmt(g, node, (const char *const[]){"return", NULL}, (const char *const[]){"return", NULL});
		return;
	case BK_import_from:
		lg_visit_stmt(g, node, KW_EMPTY, (const char *const[]){"import", NULL});
		return;
	case BK_del_stmt:
		lg_visit_stmt(g, node, KW_EMPTY, (const char *const[]){"del", NULL});
		return;
	case BK_with_stmt:
	case BK_match_stmt:
	case BK_case_block:
		fail("black port: unsupported statement");
	}
	lg_visit_default(g, node);
}

static void linegen_run(BkNode *root, EmitFn emit, void *ctx)
{
	LineGen g = {line_new(0, false), emit, ctx};
	lg_visit(&g, root);
}

/* ---- splits ---- */

typedef enum { COMP_HEAD, COMP_BODY, COMP_TAIL } Component;

static bool should_split_line(Line *line, BkNode *opening_bracket)
{
	if (!(opening_bracket->parent && (!*opening_bracket->value || strchr("[{(", opening_bracket->value[0]))))
		return false;
	BkNode *exclude[1];
	int nex = 0;
	bool trailing_comma = false;
	if (!line->n)
		return false;
	BkNode *last_leaf = line->leaves[line->n - 1];
	if (last_leaf->type == BK_COMMA) {
		trailing_comma = true;
		exclude[nex++] = last_leaf;
	}
	int max_priority = bt_max_priority(&line->bt, exclude, nex);
	if (max_priority < 0)
		return false;
	return max_priority == COMMA_PRIORITY &&
	       (trailing_comma || opening_bracket->parent->type == BK_atom || opening_bracket->parent->type == BK_import_from);
}

static BkNode *added_comma(void)
{
	BkNode *c = new_leaf(BK_COMMA, ",");
	c->added_comma = true;
	return c;
}

/* bracket_split_build_line(leaves, original, opening_bracket, component=...); leaves may be
 * reallocated (a comma is inserted into the body). */
static Line *bracket_split_build_line(BkNode **leaves, int n, const Line *original, BkNode *opening_bracket,
				      Component component)
{
	Line *result = line_new(original->depth, false);
	BkNode **ls = leaves;
	if (component == COMP_BODY) {
		result->inside_brackets = true;
		result->depth++;
		if (n) {
			bool no_commas = line_is_def(original) && !strcmp(opening_bracket->value, "(");
			if (no_commas)
				for (int i = 0; i < n; i++)
					if (ls[i]->type == BK_COMMA)
						no_commas = false;
			if (no_commas && get_annotation_type(ls[0]) == 'r')
				no_commas = false;
			if (no_commas && ls[0]->parent && next_sibling(ls[0]->parent) &&
			    next_sibling(ls[0]->parent)->type == BK_VBAR)
				no_commas = false;
			if (line_is_import(original) || no_commas) {
				for (int i = n - 1; i >= 0; i--) {
					if (ls[i]->type == BK_STANDALONE_COMMENT)
						continue;
					if (ls[i]->type != BK_COMMA) {
						BkNode **nls = xalloc(sizeof *nls * (size_t)(n + 1));
						memcpy(nls, ls, sizeof *nls * (size_t)(i + 1));
						nls[i + 1] = added_comma();
						memcpy(nls + i + 2, ls + i + 1, sizeof *nls * (size_t)(n - i - 1));
						ls = nls;
						n++;
					}
					break;
				}
			}
		}
	}
	bool *track = NULL;
	if (component == COMP_HEAD && n) {
		track = xalloc((size_t)n);
		leaves_inside_matching_brackets(ls, n, track);
	}
	for (int i = 0; i < n; i++)
		line_append(result, ls[i], true, track && track[i]);
	if (component == COMP_BODY && should_split_line(result, opening_bracket))
		result->should_split_rhs = true;
	return result;
}

static bool bracket_split_succeeded(Line *head, Line *body, Line *tail)
{
	(void)head;
	char *t = line_str(tail);
	Str ts = py_strip(str_c(t));
	size_t tail_len = py_len(ts.p, ts.n);
	if (!body->n) {
		if (tail_len == 0)
			return false; /* Splitting brackets produced the same line */
		if (tail_len < 3)
			return false; /* not worth it */
	}
	return true;
}

static bool in_omit(BkNode *const *omit, int nomit, const BkNode *leaf)
{
	for (int i = 0; i < nomit; i++)
		if (omit[i] == leaf)
			return true;
	return false;
}

/* _first_right_hand_split(line, omit) */
static bool first_right_hand_split(Line *line, BkNode *const *omit, int nomit, RHSResult *out)
{
	int n = line->n;
	BkNode **tail = xalloc(sizeof(BkNode *) * (size_t)(n + 1)), **body = xalloc(sizeof(BkNode *) * (size_t)(n + 1)),
	       **head = xalloc(sizeof(BkNode *) * (size_t)(n + 1));
	int nt = 0, nb = 0, nh = 0;
	int cur = 0; /* 0 tail, 1 body, 2 head */
	BkNode *opening = NULL, *closing = NULL;
	for (int i = n - 1; i >= 0; i--) {
		BkNode *leaf = line->leaves[i];
		if (cur == 1 && leaf == opening)
			cur = nb ? 2 : 0;
		if (cur == 0)
			tail[nt++] = leaf;
		else if (cur == 1)
			body[nb++] = leaf;
		else
			head[nh++] = leaf;
		if (cur == 0 && is_closing(leaf->type) && !in_omit(omit, nomit, leaf)) {
			opening = leaf->opening_bracket;
			closing = leaf;
			cur = 1;
		}
	}
	if (!(opening && closing && nh))
		return false; /* No brackets found */
	for (int i = 0; i < nt / 2; i++) {
		BkNode *x = tail[i];
		tail[i] = tail[nt - 1 - i];
		tail[nt - 1 - i] = x;
	}
	for (int i = 0; i < nb / 2; i++) {
		BkNode *x = body[i];
		body[i] = body[nb - 1 - i];
		body[nb - 1 - i] = x;
	}
	for (int i = 0; i < nh / 2; i++) {
		BkNode *x = head[i];
		head[i] = head[nh - 1 - i];
		head[nh - 1 - i] = x;
	}
	Line *h = bracket_split_build_line(head, nh, line, opening, COMP_HEAD);
	Line *b = bracket_split_build_line(body, nb, line, opening, COMP_BODY);
	Line *t = bracket_split_build_line(tail, nt, line, opening, COMP_TAIL);
	if (!bracket_split_succeeded(h, b, t))
		return false;
	*out = (RHSResult){h, b, t, opening, closing};
	return true;
}

static bool prefer_split_rhs_oop_over_rhs(const RHSResult *rhs_oop, const RHSResult *rhs)
{
	int rhs_head_equal_count = 0, oop_head_equal_count = 0;
	for (int i = 0; i < rhs->head->n; i++)
		rhs_head_equal_count += rhs->head->leaves[i]->type == BK_EQUAL;
	for (int i = 0; i < rhs_oop->head->n; i++)
		oop_head_equal_count += rhs_oop->head->leaves[i]->type == BK_EQUAL;
	if (rhs_head_equal_count > 1 && rhs_head_equal_count > oop_head_equal_count)
		return false;
	bool has_closing_bracket_after_assign = false;
	for (int i = rhs_oop->head->n - 1; i >= 0; i--) {
		BkNode *leaf = rhs_oop->head->leaves[i];
		if (leaf->type == BK_EQUAL)
			break;
		if (is_closing(leaf->type)) {
			has_closing_bracket_after_assign = true;
			break;
		}
	}
	return has_closing_bracket_after_assign ||
	       (oop_head_equal_count > 0 && is_line_short_enough(rhs_oop->head, NULL));
}

/* _maybe_split_omitting_optional_parens */
static bool maybe_split_omitting_optional_parens(RHSResult *rhs, Line *line, int features, BkNode *const *omit,
						 int nomit, Lines *out)
{
	if (!(features & F_FORCE_OPTIONAL_PARENTHESES) && rhs->opening_bracket->type == BK_LPAR &&
	    !*rhs->opening_bracket->value && rhs->closing_bracket->type == BK_RPAR && !*rhs->closing_bracket->value &&
	    !line_is_import(line) && can_omit_invisible_parens(rhs, LINE_LENGTH)) {
		BkNode **omit2 = xalloc(sizeof *omit2 * (size_t)(nomit + 1));
		omit2[0] = rhs->closing_bracket;
		if (nomit)
			memcpy(omit2 + 1, omit, sizeof *omit2 * (size_t)nomit);
		int nomit2 = nomit + 1;
		RHSResult rhs_oop;
		bool ok = first_right_hand_split(line, omit2, nomit2, &rhs_oop);
		if (ok) {
			bool is_split_right_after_equal =
				rhs->head->n >= 2 && rhs->head->leaves[rhs->head->n - 2]->type == BK_EQUAL;
			bool rhs_head_contains_brackets = false;
			for (int i = 0; i + 1 < rhs->head->n; i++)
				if (is_bracket(rhs->head->leaves[i]->type))
					rhs_head_contains_brackets = true;
			bool rhs_head_short_enough = is_line_short_enough_n(rhs->head, LINE_LENGTH - 1);
			bool rhs_head_explode_blocked_by_magic_trailing_comma = rhs->head->magic_trailing_comma == NULL;
			if (!(is_split_right_after_equal && rhs_head_contains_brackets && rhs_head_short_enough &&
			      rhs_head_explode_blocked_by_magic_trailing_comma) ||
			    prefer_split_rhs_oop_over_rhs(&rhs_oop, rhs)) {
				Lines sub = {0};
				ok = maybe_split_omitting_optional_parens(&rhs_oop, line, features, omit2, nomit2, &sub);
				if (ok) {
					for (int i = 0; i < sub.n; i++)
						lines_push(out, sub.v[i]);
					return true;
				}
			} else {
				goto visible;
			}
		}
		/* except CannotSplit */
		if (line_is_chained_assignment(line)) {
			/* use the previous successful split */
		} else if (!can_be_split(rhs->body) && !is_line_short_enough(rhs->body, NULL)) {
			return false; /* body is still too long and can't be split */
		} else if (line_contains_multiline_strings(rhs->head) || line_contains_multiline_strings(rhs->tail)) {
			return false;
		}
	}
visible:
	ensure_visible(rhs->opening_bracket);
	ensure_visible(rhs->closing_bracket);
	if (rhs->head->n)
		lines_push(out, rhs->head);
	if (rhs->body->n)
		lines_push(out, rhs->body);
	if (rhs->tail->n)
		lines_push(out, rhs->tail);
	return true;
}

static bool right_hand_split(Line *line, int features, BkNode *const *omit, int nomit, Lines *out)
{
	RHSResult rhs;
	if (!first_right_hand_split(line, omit, nomit, &rhs))
		return false;
	return maybe_split_omitting_optional_parens(&rhs, line, features, omit, nomit, out);
}

/* generate_trailers_to_omit(line, line_length) as a resumable iterator. */
typedef struct {
	Line *line;
	int state; /* 0 start, 1 loop, 2 resume after a yield inside the loop, 3 done */
	int index, length;
	BkNode *opening_bracket, *closing_bracket;
	BkNode **inner;
	int ninner;
	BkNode **omit;
	int nomit;
	BkNode *prev; /* of the leaf being resumed */
	BkNode *leaf;
} TrailerIter;

static void ti_add(BkNode ***v, int *n, BkNode *x)
{
	*v = xrealloc(*v, sizeof **v * (size_t)*n, sizeof **v * (size_t)(*n + 1));
	(*v)[(*n)++] = x;
}

/* Returns false when exhausted; the omit set is it->omit[0..nomit). */
static bool ti_next(TrailerIter *it)
{
	Line *line = it->line;
	if (it->state == 0) {
		it->state = 1;
		it->index = line->n - 1;
		it->length = 4 * line->depth;
		if (!line->magic_trailing_comma)
			return true;
	}
	for (;;) {
		if (it->state == 3)
			return false;
		if (it->state == 2) {
			/* the rest of the loop body after `yield omit` */
			it->state = 1;
			BkNode *leaf = it->leaf, *prev = it->prev;
			if (prev && prev->type == BK_COMMA && leaf->opening_bracket &&
			    !is_one_sequence_between(leaf->opening_bracket, leaf, line->leaves, line->n, BK_LPAR, BK_RPAR)) {
				it->state = 3;
				return false;
			}
			if (*leaf->value) {
				it->opening_bracket = leaf->opening_bracket;
				it->closing_bracket = leaf;
			}
			it->index--;
			continue;
		}
		if (it->index < 0) {
			it->state = 3;
			return false;
		}
		int index = it->index;
		BkNode *leaf = line->leaves[index];
		int ll = leaf_length(leaf);
		if (ll < 0) { /* enumerate_with_length stops at a multiline string */
			it->state = 3;
			return false;
		}
		it->length += ll;
		if (it->length > LINE_LENGTH || leaf->type == BK_STANDALONE_COMMENT) {
			it->state = 3;
			return false;
		}
		if (it->opening_bracket) {
			if (leaf == it->opening_bracket) {
				it->opening_bracket = NULL;
			} else if (is_closing(leaf->type)) {
				BkNode *prev = index > 0 ? line->leaves[index - 1] : NULL;
				if (prev && prev->type == BK_COMMA && leaf->opening_bracket &&
				    !is_one_sequence_between(leaf->opening_bracket, leaf, line->leaves, line->n, BK_LPAR,
							     BK_RPAR)) {
					it->state = 3;
					return false;
				}
				ti_add(&it->inner, &it->ninner, leaf);
			}
		} else if (is_closing(leaf->type)) {
			BkNode *prev = index > 0 ? line->leaves[index - 1] : NULL;
			if (prev && is_opening(prev->type)) {
				ti_add(&it->inner, &it->ninner, leaf);
				it->index--;
				continue;
			}
			if (it->closing_bracket) {
				ti_add(&it->omit, &it->nomit, it->closing_bracket);
				for (int i = 0; i < it->ninner; i++)
					ti_add(&it->omit, &it->nomit, it->inner[i]);
				it->ninner = 0;
				it->state = 2;
				it->leaf = leaf;
				it->prev = prev;
				return true;
			}
			if (prev && prev->type == BK_COMMA && leaf->opening_bracket &&
			    !is_one_sequence_between(leaf->opening_bracket, leaf, line->leaves, line->n, BK_LPAR, BK_RPAR)) {
				it->state = 3;
				return false;
			}
			if (*leaf->value) {
				it->opening_bracket = leaf->opening_bracket;
				it->closing_bracket = leaf;
			}
		}
		it->index--;
	}
}

static bool rhs_transform(Line *line, int features, Lines *out)
{
	TrailerIter it = {.line = line};
	while (ti_next(&it)) {
		Lines ls = {0};
		BkNode **omit = xalloc(sizeof *omit * (size_t)(it.nomit + 1));
		if (it.nomit)
			memcpy(omit, it.omit, sizeof *omit * (size_t)it.nomit);
		if (!right_hand_split(line, features, omit, it.nomit, &ls))
			return false;
		if (is_line_short_enough(ls.v[0], NULL)) {
			for (int i = 0; i < ls.n; i++)
				lines_push(out, ls.v[i]);
			return true;
		}
	}
	return right_hand_split(line, features, NULL, 0, out);
}

static bool left_hand_split(Line *line, Lines *out)
{
	int n = line->n;
	BkNode **tail = xalloc(sizeof(BkNode *) * (size_t)(n + 1)), **body = xalloc(sizeof(BkNode *) * (size_t)(n + 1)),
	       **head = xalloc(sizeof(BkNode *) * (size_t)(n + 1));
	int nt = 0, nb = 0, nh = 0, cur = 2; /* 2 head, 1 body, 0 tail */
	BkNode *matching = NULL;
	for (int i = 0; i < n; i++) {
		BkNode *leaf = line->leaves[i];
		if (cur == 1 && is_closing(leaf->type) && leaf->opening_bracket == matching && matching) {
			ensure_visible(leaf);
			ensure_visible(matching);
			cur = nb ? 0 : 2;
		}
		if (cur == 0)
			tail[nt++] = leaf;
		else if (cur == 1)
			body[nb++] = leaf;
		else
			head[nh++] = leaf;
		if (cur == 2 && is_opening(leaf->type)) {
			matching = leaf;
			cur = 1;
		}
	}
	if (!matching || !nt)
		return false;
	Line *h = bracket_split_build_line(head, nh, line, matching, COMP_HEAD);
	Line *b = bracket_split_build_line(body, nb, line, matching, COMP_BODY);
	Line *t = bracket_split_build_line(tail, nt, line, matching, COMP_TAIL);
	if (!bracket_split_succeeded(h, b, t))
		return false;
	if (h->n)
		lines_push(out, h);
	if (b->n)
		lines_push(out, b);
	if (t->n)
		lines_push(out, t);
	return true;
}

static bool can_add_trailing_comma(const BkNode *leaf, int features)
{
	if (is_vararg(leaf, W_TYPEDARGSLIST))
		return features & F_TRAILING_COMMA_IN_DEF;
	if (is_vararg(leaf, W_ARGLIST_ARGUMENT))
		return features & F_TRAILING_COMMA_IN_CALL;
	return true;
}

static void safe_add_trailing_comma(bool safe, int prio, Line *line)
{
	if (safe && prio == COMMA_PRIORITY && line->leaves[line->n - 1]->type != BK_COMMA &&
	    line->leaves[line->n - 1]->type != BK_STANDALONE_COMMENT)
		line_append(line, added_comma(), false, false);
}

/* delimiter_split with dont_increase_indentation */
static bool delimiter_split(Line *line, int features, Lines *out)
{
	if (!line->n)
		return false;
	BkNode *last_leaf = line->leaves[line->n - 1];
	BracketTracker *bt = &line->bt;
	int delimiter_priority = bt_max_priority(bt, &last_leaf, 1);
	if (delimiter_priority < 0)
		return false;
	if (delimiter_priority == DOT_PRIORITY && bt_count_with_priority(bt, delimiter_priority) == 1)
		return false;
	int first_out = out->n;
	Line *current = line_new(line->depth, line->inside_brackets);
	int lowest_depth = INT32_MAX;
	bool trailing_comma_safe = true;
	int last_non_comment_leaf = -1;
	for (int i = line->n - 1; i > 0; i--)
		if (line->leaves[i]->type != BK_STANDALONE_COMMENT) {
			last_non_comment_leaf = i;
			break;
		}
	for (int idx = 0; idx < line->n; idx++) {
		BkNode *leaf = line->leaves[idx];
		if (!line_append_safe(current, leaf, true)) {
			lines_push(out, current);
			current = line_new(line->depth, line->inside_brackets);
			line_append(current, leaf, false, false);
		}
		if (leaf->bracket_depth < lowest_depth)
			lowest_depth = leaf->bracket_depth;
		if (trailing_comma_safe && leaf->bracket_depth == lowest_depth)
			trailing_comma_safe = can_add_trailing_comma(leaf, features);
		if (last_leaf->type == BK_STANDALONE_COMMENT && idx == last_non_comment_leaf)
			safe_add_trailing_comma(trailing_comma_safe, delimiter_priority, current);
		if (bt_get(bt, leaf) == delimiter_priority) {
			lines_push(out, current);
			current = line_new(line->depth, line->inside_brackets);
		}
	}
	if (current->n) {
		safe_add_trailing_comma(trailing_comma_safe, delimiter_priority, current);
		lines_push(out, current);
	}
	for (int i = first_out; i < out->n; i++)
		out->v[i]->leaves[0]->prefix = "";
	return true;
}

static bool standalone_comment_split(Line *line, Lines *out)
{
	if (!line_contains_standalone_comments(line))
		return false;
	int first_out = out->n;
	Line *current = line_new(line->depth, line->inside_brackets);
	for (int i = 0; i < line->n; i++) {
		if (!line_append_safe(current, line->leaves[i], true)) {
			lines_push(out, current);
			current = line_new(line->depth, line->inside_brackets);
			line_append(current, line->leaves[i], false, false);
		}
	}
	if (current->n)
		lines_push(out, current);
	for (int i = first_out; i < out->n; i++)
		out->v[i]->leaves[0]->prefix = "";
	return true;
}

static bool should_split_funcdef_with_rhs(Line *line)
{
	BkNode **rt = xalloc(sizeof *rt * (size_t)(line->n + 1));
	int nrt = 0;
	bool in_return_type = false;
	for (int i = 0; i < line->n; i++) {
		BkNode *leaf = line->leaves[i];
		if (leaf->type == BK_COLON)
			in_return_type = false;
		if (in_return_type)
			rt[nrt++] = leaf;
		if (leaf->type == BK_RARROW)
			in_return_type = true;
	}
	Line *result = line_new(line->depth, false);
	bool *track = xalloc((size_t)nrt + 1);
	leaves_inside_matching_brackets(rt, nrt, track);
	for (int i = 0; i < nrt; i++)
		line_append(result, rt[i], true, track[i]);
	return result->magic_trailing_comma != NULL;
}

typedef enum { T_DELIMITER, T_STANDALONE, T_RHS, T_LHS } Transform;

static bool run_transformer(Line *line, Transform transform, int features, const char *line_str_, Lines *out);

static bool apply_transform(Line *line, Transform t, int features, Lines *out)
{
	switch (t) {
	case T_DELIMITER: return delimiter_split(line, features, out);
	case T_STANDALONE: return standalone_comment_split(line, out);
	case T_RHS: return rhs_transform(line, features, out);
	case T_LHS: return left_hand_split(line, out);
	}
	return false;
}

static bool run_transformer(Line *line, Transform transform, int features, const char *line_str_, Lines *out)
{
	Lines tl = {0};
	if (!apply_transform(line, transform, features, &tl))
		return false;
	Lines result = {0};
	size_t ref_len = py_len(line_str_, strlen(line_str_));
	for (int i = 0; i < tl.n; i++) {
		int w = line_width(tl.v[i]); /* a line of another width differs */
		if ((w < 0 || (size_t)w == ref_len) && !strcmp(line_to_string(tl.v[i]), line_str_))
			return false; /* Line transformer returned an unchanged result */
		transform_line(tl.v[i], features, &result);
	}
	bool done = (features & F_FORCE_OPTIONAL_PARENTHESES) || transform != T_RHS || !line->bt.ninv;
	for (int i = 0; !done && i < line->bt.ninv; i++)
		if (*line->bt.invisible[i]->value)
			done = true;
	if (!done)
		done = line_contains_multiline_strings(line) || is_line_short_enough(result.v[0], NULL);
	for (int i = 0; !done && i < line->n; i++)
		if (!line->leaves[i]->parent)
			done = true;
	if (!done) {
		Line *copy = line_clone(line);
		append_leaves(copy, line, line->leaves, line->n, false);
		Lines second = {0};
		if (run_transformer(copy, transform, features | F_FORCE_OPTIONAL_PARENTHESES, line_str_, &second)) {
			bool all_short = true;
			for (int i = 0; i < second.n; i++)
				if (!is_line_short_enough(second.v[i], NULL))
					all_short = false;
			if (all_short)
				result = second;
		}
		/* (a CannotTransform of the second opinion propagates upstream: the transform fails) */
		else
			return false;
	}
	for (int i = 0; i < result.n; i++)
		lines_push(out, result.v[i]);
	return true;
}

static void transform_line(Line *line, int features, Lines *out)
{
	if (line_is_comment(line)) {
		lines_push(out, line);
		return;
	}
	for (int i = 0; i < line->n; i++)
		if (line->leaves[i]->type == BK_DOUBLESTAR)
			fail("black port: power operators are not supported");
	Transform ts[3];
	int nt = 0;
	if (!line->should_split_rhs && !line->magic_trailing_comma && is_line_short_enough(line, NULL) &&
	    !(line->inside_brackets && line_contains_standalone_comments(line))) {
		/* only hug_power_op, which raises CannotTransform: the line as is */
		lines_push(out, line);
		return;
	}
	char *ls = line_to_string(line);
	if (line_is_def(line) && !should_split_funcdef_with_rhs(line)) {
		ts[nt++] = T_LHS;
	} else if (line->inside_brackets) {
		ts[nt++] = T_DELIMITER;
		ts[nt++] = T_STANDALONE;
		ts[nt++] = T_RHS;
	} else {
		ts[nt++] = T_RHS;
	}
	/* hug_power_op raises CannotTransform without a power operator */
	for (int i = 0; i < nt; i++) {
		Lines result = {0};
		if (run_transformer(line, ts[i], features, ls, &result)) {
			for (int j = 0; j < result.n; j++)
				lines_push(out, result.v[j]);
			return;
		}
	}
	lines_push(out, line);
}
