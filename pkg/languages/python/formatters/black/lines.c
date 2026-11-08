/* black/lines.py: Line, EmptyLineTracker and the line checks used by the splits. Lines never
 * hold trailing comments here (black.h), so Line.comments is always empty. */

static void lines_push(Lines *ls, Line *l)
{
	if (ls->n == ls->cap) {
		int nc = ls->cap ? ls->cap * 2 : 2;
		ls->v = xrealloc(ls->v, sizeof *ls->v * (size_t)ls->cap, sizeof *ls->v * (size_t)nc);
		ls->cap = nc;
	}
	ls->v[ls->n++] = l;
}

static Line *line_new(int depth, bool inside_brackets)
{
	Line *l = xalloc(sizeof *l);
	l->depth = depth;
	l->inside_brackets = inside_brackets;
	return l;
}

static void push_leaf(Line *l, BkNode *leaf)
{
	if (l->n == l->cap) {
		int nc = l->cap ? l->cap * 2 : 8;
		l->leaves = xrealloc(l->leaves, sizeof *l->leaves * (size_t)l->cap, sizeof *l->leaves * (size_t)nc);
		l->cap = nc;
	}
	l->leaves[l->n++] = leaf;
}

static bool line_is_class(const Line *l)
{
	return l->n && l->leaves[0]->type == BK_NAME && !strcmp(l->leaves[0]->value, "class");
}

static bool line_is_class_paren_empty(const Line *l)
{
	return l->n == 4 && line_is_class(l) && l->leaves[2]->type == BK_LPAR && !strcmp(l->leaves[2]->value, "(") &&
	       l->leaves[3]->type == BK_RPAR && !strcmp(l->leaves[3]->value, ")");
}

static bool line_is_import(const Line *l) { return l->n && is_import_leaf(l->leaves[0]); }

/* is_complex_subscript(leaf) */
static bool any_test_descendant(BkNode *n)
{
	if (IN(n->type, BK_test, BK_lambdef, BK_or_test, BK_and_test, BK_not_test, BK_comparison, BK_star_expr,
	       BK_expr, BK_xor_expr, BK_and_expr, BK_shift_expr, BK_arith_expr, BK_trailer, BK_term, BK_power,
	       BK_namedexpr_test))
		return true;
	if (!IS_LEAF(n))
		for (int i = 0; i < n->n; i++)
			if (any_test_descendant(n->kids[i]))
				return true;
	return false;
}

static bool line_is_complex_subscript(const Line *l, BkNode *leaf)
{
	BkNode *open_lsqb = bt_get_open_lsqb(&l->bt);
	if (!open_lsqb)
		return false;
	BkNode *start = next_sibling(open_lsqb);
	if (start && !IS_LEAF(start)) {
		if (start->type == BK_listmaker)
			return false;
		if (start->type == BK_subscriptlist)
			start = child_towards(start, leaf);
	}
	return start && any_test_descendant(start);
}

/* has_magic_trailing_comma(closing) */
static bool line_has_magic_trailing_comma(const Line *l, BkNode *closing)
{
	if (!(is_closing(closing->type) && l->n && l->leaves[l->n - 1]->type == BK_COMMA))
		return false;
	if (closing->type == BK_RBRACE)
		return true;
	if (closing->type == BK_RSQB) {
		if (closing->parent && closing->parent->type == BK_trailer && closing->opening_bracket &&
		    is_one_sequence_between(closing->opening_bracket, closing, l->leaves, l->n, BK_LSQB, BK_RSQB))
			return false;
		return true;
	}
	if (line_is_import(l))
		return true;
	if (closing->opening_bracket &&
	    !is_one_sequence_between(closing->opening_bracket, closing, l->leaves, l->n, BK_LPAR, BK_RPAR))
		return true;
	return false;
}

static void line_append(Line *l, BkNode *leaf, bool preformatted, bool track_bracket)
{
	bool has_value = is_bracket(leaf->type) || py_strip(str_c(leaf->value)).n;
	if (!has_value)
		return;
	if (leaf->type == BK_COLON && line_is_class_paren_empty(l))
		l->n -= 2;
	if (l->n && !preformatted) {
		const char *ws = whitespace(leaf, line_is_complex_subscript(l, leaf));
		if (*ws)
			leaf->prefix = *leaf->prefix ? xcat(leaf->prefix, ws) : ws;
	}
	if (l->inside_brackets || !preformatted || track_bracket) {
		bt_mark(&l->bt, leaf);
		if (line_has_magic_trailing_comma(l, leaf))
			l->magic_trailing_comma = leaf;
	}
	/* append_comment(leaf) */
	if (leaf->type == BK_STANDALONE_COMMENT && bt_any_open_brackets(&l->bt)) {
		leaf->prefix = "";
	} else if (leaf->type == BK_COMMENT) {
		if (l->n)
			fail("black port: trailing comments are not supported");
		leaf->type = BK_STANDALONE_COMMENT;
		leaf->prefix = "";
	}
	push_leaf(l, leaf);
}

static bool line_is_comment(const Line *l) { return l->n == 1 && l->leaves[0]->type == BK_STANDALONE_COMMENT; }

static bool line_append_safe(Line *l, BkNode *leaf, bool preformatted)
{
	if (l->bt.depth == 0 || bt_any_open_for_or_lambda(&l->bt)) {
		if (line_is_comment(l))
			return false;
		if (l->n && leaf->type == BK_STANDALONE_COMMENT)
			return false;
	}
	line_append(l, leaf, preformatted, false);
	return true;
}

static bool line_is_decorator(const Line *l) { return l->n && l->leaves[0]->type == BK_AT; }

static bool line_is_with_or_async_with_stmt(const Line *l) { return l->n && is_with_or_async_with_stmt(l->leaves[0]); }

static bool is_dots(BkNode *const *ls)
{
	for (int i = 0; i < 3; i++)
		if (ls[i]->type != BK_DOT || strcmp(ls[i]->value, "."))
			return false;
	return true;
}

static bool line_is_def(const Line *l)
{
	if (!l->n)
		return false;
	BkNode *a = l->leaves[0], *b = l->n > 1 ? l->leaves[1] : NULL;
	return (a->type == BK_NAME && !strcmp(a->value, "def")) ||
	       (a->type == BK_ASYNC && b && b->type == BK_NAME && !strcmp(b->value, "def"));
}

static bool line_is_stub_def(const Line *l)
{
	return line_is_def(l) && l->n >= 4 && l->leaves[l->n - 4]->type == BK_COLON &&
	       !strcmp(l->leaves[l->n - 4]->value, ":") && is_dots(l->leaves + l->n - 3);
}

/* is_docstring without preview: _is_triple_quoted_string */
static bool line_is_docstring(const Line *l)
{
	if (!l->n || l->leaves[0]->type != BK_STRING)
		return false;
	const char *v = l->leaves[0]->value;
	return has_prefix(v, "\"\"\"") || has_prefix(v, "'''") || has_prefix(v, "r'''") || has_prefix(v, "r\"\"\"") ||
	       has_prefix(v, "R'''") || has_prefix(v, "R\"\"\"");
}

static bool line_is_chained_assignment(const Line *l)
{
	int n = 0;
	for (int i = 0; i < l->n; i++)
		n += l->leaves[i]->type == BK_EQUAL;
	return n > 1;
}

static bool line_opens_block(const Line *l) { return l->n && l->leaves[l->n - 1]->type == BK_COLON; }

static bool line_contains_standalone_comments(const Line *l)
{
	for (int i = 0; i < l->n; i++)
		if (l->leaves[i]->type == BK_STANDALONE_COMMENT)
			return true;
	return false;
}

static bool line_contains_multiline_strings(const Line *l)
{
	for (int i = 0; i < l->n; i++)
		if (is_multiline_string(l->leaves[i]))
			return true;
	return false;
}

/* len(leaf.prefix) + len(leaf.value) of enumerate_with_length; -1 for a multiline string. */
static int leaf_length(const BkNode *leaf)
{
	if (strchr(leaf->value, '\n'))
		return -1;
	return (int)(py_len(leaf->prefix, strlen(leaf->prefix)) + py_len(leaf->value, strlen(leaf->value)));
}

static Line *line_clone(const Line *l)
{
	Line *c = line_new(l->depth, l->inside_brackets);
	c->should_split_rhs = l->should_split_rhs;
	c->magic_trailing_comma = l->magic_trailing_comma;
	return c;
}

/* str(line) appended to b */
static void line_str_to(const Line *l, Buf *b)
{
	if (!l->n) {
		buf_addc(b, '\n');
		return;
	}
	size_t len = 4 * (size_t)l->depth + 1;
	for (int i = 0; i < l->n; i++)
		len += strlen(l->leaves[i]->prefix) + strlen(l->leaves[i]->value);
	buf_grow(b, len);
	buf_adds(b, l->leaves[0]->prefix);
	for (int i = 0; i < l->depth; i++)
		buf_adds(b, "    ");
	buf_adds(b, l->leaves[0]->value);
	for (int i = 1; i < l->n; i++) {
		buf_adds(b, l->leaves[i]->prefix);
		buf_adds(b, l->leaves[i]->value);
	}
	buf_addc(b, '\n');
}

static char *line_str(const Line *l)
{
	Buf b = {0};
	line_str_to(l, &b);
	return (char *)buf_str(&b);
}

/* str(line).strip("\n") */
static char *strip_newlines(char *s)
{
	while (*s == '\n')
		s++;
	size_t n = strlen(s);
	while (n && s[n - 1] == '\n')
		n--;
	s[n] = 0;
	return s;
}

static char *line_to_string(const Line *l) { return strip_newlines(line_str(l)); }

/* The width of line_to_string(l) without building it, or -1 when the line has a newline. */
static int line_width(const Line *l)
{
	size_t w = 4 * (size_t)l->depth;
	for (int i = 0; i < l->n; i++) {
		const char *p = l->leaves[i]->prefix, *v = l->leaves[i]->value;
		if (strchr(p, '\n') || strchr(v, '\n'))
			return -1;
		w += py_len(p, strlen(p)) + py_len(v, strlen(v));
	}
	return w > INT32_MAX ? INT32_MAX : (int)w;
}

static bool is_line_short_enough_n(const Line *l, int line_length)
{
	int w = line_width(l);
	if (w < 0) {
		char *s = line_to_string(l);
		w = strchr(s, '\n') ? INT32_MAX : str_width(s, strlen(s));
	}
	return w <= line_length && !line_contains_standalone_comments(l);
}

static bool is_line_short_enough(const Line *l, const char *s)
{
	if (!s || !*s)
		return is_line_short_enough_n(l, LINE_LENGTH);
	return str_width(s, strlen(s)) <= LINE_LENGTH && !strchr(s, '\n') && !line_contains_standalone_comments(l);
}

static bool can_be_split(const Line *l)
{
	BkNode **ls = l->leaves;
	int n = l->n;
	if (n < 2)
		return false;
	if (ls[0]->type == BK_STRING && ls[1]->type == BK_DOT) {
		int call_count = 0, dot_count = 0;
		BkNode *next = ls[n - 1];
		for (int i = n - 2; i >= 0; i--) {
			BkNode *leaf = ls[i];
			if (is_opening(leaf->type)) {
				if (!is_closing(next->type))
					return false;
				call_count++;
			} else if (leaf->type == BK_DOT) {
				dot_count++;
			} else if (leaf->type == BK_NAME) {
				if (!(next->type == BK_DOT || is_opening(next->type)))
					return false;
			} else if (!is_closing(leaf->type)) {
				return false;
			}
			if (dot_count > 1 && call_count > 1)
				return false;
			/* (the upstream loop never updates `next`) */
		}
	}
	return true;
}

static void append_leaves(Line *new_line, const Line *old_line, BkNode **leaves, int n, bool preformatted)
{
	(void)old_line;
	for (int i = 0; i < n; i++) {
		BkNode *old = leaves[i];
		BkNode *nl = new_leaf(old->type, old->value);
		nl->added_comma = old->added_comma;
		replace_child(old, nl);
		old->replaced_by = nl;
		line_append(new_line, nl, preformatted, false);
	}
}

static bool line_contains_in(const Line *l, const BkNode *leaf)
{
	for (int i = 0; i < l->n; i++)
		if (l->leaves[i] == leaf)
			return true;
	return false;
}

static bool can_omit_opening_paren(const Line *line, BkNode *first, int line_length)
{
	bool remainder = false;
	int length = 4 * line->depth;
	int index = -1;
	bool broke = false;
	for (int i = 0; i < line->n; i++) {
		BkNode *leaf = line->leaves[i];
		int ll = leaf_length(leaf);
		if (ll < 0)
			break; /* enumerate_with_length stops: the else branch runs */
		index = i;
		if (is_closing(leaf->type) && leaf->opening_bracket == first)
			remainder = true;
		if (remainder) {
			length += ll;
			if (length > line_length) {
				broke = true;
				break;
			}
			if (is_opening(leaf->type))
				remainder = false;
		}
	}
	if (!broke && line->n == index + 1)
		return true;
	return false;
}

static bool can_omit_closing_paren(const Line *line, BkNode *last, int line_length)
{
	int length = 4 * line->depth;
	bool seen_other_brackets = false;
	for (int i = 0; i < line->n; i++) {
		BkNode *leaf = line->leaves[i];
		int ll = leaf_length(leaf);
		if (ll < 0)
			break;
		length += ll;
		if (leaf == last->opening_bracket) {
			if (seen_other_brackets || length <= line_length)
				return true;
		} else if (is_opening(leaf->type)) {
			seen_other_brackets = true;
		}
	}
	return false;
}

static bool can_omit_invisible_parens(const RHSResult *rhs, int line_length)
{
	const Line *line = rhs->body;
	BkNode *closing_bracket = NULL;
	for (int i = line->n - 1; i >= 0; i--) {
		BkNode *leaf = line->leaves[i];
		if (closing_bracket && leaf == closing_bracket->opening_bracket)
			closing_bracket = NULL;
		if (leaf->type == BK_STANDALONE_COMMENT && !closing_bracket)
			return false;
		if (!closing_bracket && is_closing(leaf->type) && line_contains_in(line, leaf->opening_bracket) &&
		    *leaf->value)
			closing_bracket = leaf;
	}
	const BracketTracker *bt = &line->bt;
	if (!bt->ndelim)
		return true;
	int max_priority = bt_max_priority(bt, NULL, 0);
	int delimiter_count = bt_count_with_priority(bt, max_priority);
	if (delimiter_count > 1)
		return false;
	if (delimiter_count == 1 && max_priority == COMMA_PRIORITY && line_is_with_or_async_with_stmt(rhs->head))
		return false;
	if (max_priority == DOT_PRIORITY)
		return true;
	if (line->n < 2)
		fail("black: AssertionError: Stranded delimiter");
	BkNode *first = line->leaves[0], *second = line->leaves[1];
	if (is_opening(first->type) && !is_closing(second->type)) {
		if (can_omit_opening_paren(line, first, line_length))
			return true;
	}
	BkNode *penultimate = line->leaves[line->n - 2], *last = line->leaves[line->n - 1];
	if (last->type == BK_RPAR || last->type == BK_RBRACE ||
	    (last->type == BK_RSQB && last->parent && last->parent->type != BK_trailer)) {
		if (is_opening(penultimate->type))
			return false;
		if (is_multiline_string(first))
			return true;
		if (can_omit_closing_paren(line, last, line_length))
			return true;
	}
	return false;
}

/* ---- EmptyLineTracker ---- */

static int maybe_empty_lines_for_class_or_def(EmptyLineTracker *elt, Line *current_line, int before,
					       bool user_had_newline)
{
	Line *prev = elt->previous_line;
	if (line_is_decorator(prev))
		return 0;
	if (prev->depth < current_line->depth && (line_is_class(prev) || line_is_def(prev)))
		return user_had_newline ? 1 : 0;
	LinesBlock *comment_to_add_newlines = NULL;
	if (line_is_comment(prev) && prev->depth == current_line->depth && before == 0) {
		LinesBlock *slc = elt->semantic_leading_comment;
		if (slc && slc->previous_block && !line_is_class(slc->previous_block->original_line) &&
		    !line_opens_block(slc->previous_block->original_line) && slc->before <= 1)
			comment_to_add_newlines = slc;
		else
			return 0;
	}
	int newlines = current_line->depth ? 1 : 2;
	if (line_is_stub_def(prev) && !user_had_newline)
		newlines = 0;
	if (comment_to_add_newlines) {
		LinesBlock *pb = comment_to_add_newlines->previous_block;
		if (pb) {
			int b = comment_to_add_newlines->before;
			comment_to_add_newlines->before = (b > newlines ? b : newlines) - pb->after;
			newlines = 0;
		}
	}
	return newlines;
}

/* _maybe_empty_lines: returns before, sets *after. */
static int maybe_empty_lines_inner(EmptyLineTracker *elt, Line *current_line, int *after)
{
	*after = 0;
	int max_allowed = current_line->depth == 0 ? 2 : 1;
	int before = 0;
	if (current_line->n) {
		BkNode *first = current_line->leaves[0];
		for (const char *q = first->prefix; *q; q++)
			before += *q == '\n';
		if (before > max_allowed)
			before = max_allowed;
		first->prefix = "";
	}
	bool user_had_newline = before != 0;
	int depth = current_line->depth;
	Line *previous_def = NULL;
	while (elt->ndefs && elt->previous_defs[elt->ndefs - 1]->depth >= depth)
		previous_def = elt->previous_defs[--elt->ndefs];
	if (line_is_def(current_line) || line_is_class(current_line)) {
		if (elt->ndefs == elt->capdefs) {
			int nc = elt->capdefs ? elt->capdefs * 2 : 8;
			elt->previous_defs = xrealloc(elt->previous_defs, sizeof *elt->previous_defs * (size_t)elt->capdefs,
						      sizeof *elt->previous_defs * (size_t)nc);
			elt->capdefs = nc;
		}
		elt->previous_defs[elt->ndefs++] = current_line;
	}
	Line *prev = elt->previous_line;
	if (!prev)
		return 0;
	if (line_is_docstring(current_line)) {
		if (line_is_class(prev)) {
			*after = 1;
			return 0;
		}
		if (line_opens_block(prev) && line_is_def(prev))
			return 0;
	}
	if (previous_def) {
		if (depth) {
			before = 1;
		} else if (previous_def->depth && current_line->leaves[current_line->n - 1]->type == BK_COLON) {
			const char *v = current_line->leaves[0]->value;
			if (strcmp(v, "with") && strcmp(v, "try") && strcmp(v, "for") && strcmp(v, "while") && strcmp(v, "if") &&
			    strcmp(v, "match"))
				before = 1;
			else
				before = 2;
		} else {
			before = 2;
		}
	}
	if (line_is_decorator(current_line) || line_is_def(current_line) || line_is_class(current_line))
		return maybe_empty_lines_for_class_or_def(elt, current_line, before, user_had_newline);
	if (line_is_import(prev) && !line_is_import(current_line) && depth == prev->depth)
		return before ? before : 1;
	return before;
}

static LinesBlock *maybe_empty_lines(EmptyLineTracker *elt, Line *current_line)
{
	bool form_feed = current_line->depth == 0 && current_line->n && strstr(current_line->leaves[0]->prefix, "\f\n");
	int after;
	int before = maybe_empty_lines_inner(elt, current_line, &after);
	int previous_after = elt->previous_block ? elt->previous_block->after : 0;
	before = before - previous_after > 0 ? before - previous_after : 0;
	LinesBlock *pb = elt->previous_block;
	if (pb && !pb->previous_block && pb->original_line->n == 1 && line_is_docstring(pb->original_line) &&
	    !(line_is_class(current_line) || line_is_def(current_line)))
		before = 1;
	LinesBlock *block = xalloc(sizeof *block);
	block->previous_block = elt->previous_block;
	block->original_line = current_line;
	block->before = before;
	block->after = after;
	block->form_feed = form_feed;
	if (line_is_comment(current_line)) {
		Line *pl = elt->previous_line;
		if (!pl || (!line_is_decorator(pl) && (!line_is_comment(pl) || before) &&
			    (!elt->semantic_leading_comment || before)))
			elt->semantic_leading_comment = block;
	} else if (!line_is_decorator(current_line) || before) {
		elt->semantic_leading_comment = NULL;
	}
	elt->previous_line = current_line;
	elt->previous_block = block;
	return block;
}
