/* Port of dprint-plugin-typescript 0.87.1 src/generation/generate.rs: gen_node and the shared
 * helpers (comments, statements, members, separated values, parameters, surrounded-by-tokens,
 * conditional brace bodies, assignments, ...). Function names follow generate.rs.
 *
 * dprint-plugin-typescript: Copyright (c) 2019-2022 David Sherret, MIT License.
 *
 * Effective configuration (dprint-node 1.0.8 `deno: true` + ts-poet 6.12.0 options), so
 * configuration switches are resolved at port time: lineWidth 120, indentWidth 2, spaces,
 * semiColons prefer, quoteStyle preferDouble, quoteProps preserve, trailingCommas onlyMultiLine,
 * operatorPosition: binary sameLine, conditional expression/type nextLine; bracePosition sameLine;
 * nextControlFlowPosition sameLine; preferHanging false; preferSingleLine true (everywhere);
 * useBraces always; singleBodyPosition nextLine; arrowFunction.useParentheses force;
 * module.sortImportDeclarations caseSensitive, sortExportDeclarations maintain;
 * sortNamedImports/Exports caseInsensitive; commentLine.forceSpaceAfterSlashes false;
 * conditionalExpression.linePerExpression true, binary/member linePerExpression false;
 * typeLiteral separator semiColon; spaceSurroundingProperties true; constructSignature and
 * constructorType spaceAfterNewKeyword true; functionExpression.spaceAfterFunctionKeyword true;
 * typeAssertion.spaceBeforeExpression true; ignore comments "deno-fmt-ignore(-file)". */

static Items gen_node(Node *node, Ctx *c);
static Items gen_node_inner(Node *node, Ctx *c);
static Items gen_comment_collection_list(const Comment **comments, int n, const Range *last_node, const Range *next_node,
					 Ctx *c);
static Items gen_statements(Ctx *c, Range inner_range, Node **stmts, int n);

typedef Items (*InnerGenFn)(Items items, Ctx *c, void *data);

/* ---- comment lists ---- */

typedef struct {
	const Comment **v;
	int n, cap;
} CommentList;

static void cl_push(CommentList *l, const Comment *cm)
{
	if (l->n == l->cap) {
		int cap = l->cap ? l->cap * 2 : 8;
		l->v = xrealloc(l->v, sizeof(Comment *) * (size_t)l->cap, sizeof(Comment *) * (size_t)cap);
		l->cap = cap;
	}
	l->v[l->n++] = cm;
}

static CommentList cl_from(Ctx *c, Comments it)
{
	CommentList l = {0};
	CM_FOR(it, c->src, cm)
	cl_push(&l, cm);
	return l;
}

/* ---- generate.rs: gen_node ---- */

static bool get_has_ignore_comment(Ctx *c, CommentList *leading)
{
	for (int i = 0; i < leading->n; i++) {
		Str t = comment_text(c, leading->v[i]);
		if (text_has_dprint_ignore(t.p, t.n, "deno-fmt-ignore"))
			return true;
	}
	return false;
}

static Items gen_comments_as_leading(Ctx *c, Range node, CommentList comments);
static Items gen_comments_as_trailing(Ctx *c, Range node, CommentList trailing);

static bool does_first_child_own_leading_comments_on_same_line(Ctx *c, Node *node)
{
	if (node->kind != N_TS_UNION_TYPE && node->kind != N_TS_INTERSECTION_TYPE)
		return false;
	int node_start_line = start_line(c, nrange(node));
	Comments lc = leading_comments(c, node->lo);
	CM_FOR(lc, c->src, cm)
	{
		if (cm->block && cstart_line(c, cm) == node_start_line)
			return true;
	}
	return false;
}

static Items gen_node_with_inner_gen(Node *node, Ctx *c, InnerGenFn inner_gen, void *data)
{
	Node *past_current = c->current_node;
	c->current_node = node;
	uint32_t parent_hi = past_current ? past_current->hi : 0;
	ctx_push_parent(c, past_current);

	Items items = {0};
	uint32_t node_start = node->lo, node_end = node->hi;
	bool has_ignore_comment = false;
	if (node->kind != N_MODULE) {
		if (does_first_child_own_leading_comments_on_same_line(c, node)) {
			CommentList lc = cl_from(c, leading_comments(c, node_start));
			has_ignore_comment = get_has_ignore_comment(c, &lc);
			int node_start_line = start_line(c, nrange(node));
			CommentList prev_lines = {0};
			for (int i = 0; i < lc.n; i++) {
				if (!lc.v[i]->block || cstart_line(c, lc.v[i]) < node_start_line)
					cl_push(&prev_lines, lc.v[i]);
				else
					break;
			}
			items_extend(&items, gen_comment_collection_list(prev_lines.v, prev_lines.n, NULL, NULL, c));
		} else {
			CommentList lc = cl_from(c, leading_comments_with_previous(c, node_start));
			has_ignore_comment = get_has_ignore_comment(c, &lc);
			items_extend(&items, gen_comments_as_leading(c, nrange(node), lc));
		}
	}

	if (has_ignore_comment) {
		items_push_str(&items, "");
		Str t = ntext(c, node);
		Items raw = gen_from_raw_string(t.p, t.n);
		items_extend(&items, inner_gen ? inner_gen(raw, c, data) : raw);
		CommentList tc = cl_from(c, trailing_comments_with_previous(c, node_end));
		for (int i = 0; i < tc.n; i++)
			if (tc.v[i]->lo < node_end)
				mark_comment_handled(c, (Comment *)tc.v[i]);
	} else {
		Items inner = gen_node_inner(node, c);
		items_extend(&items, inner_gen ? inner_gen(inner, c, data) : inner);
	}

	Node *parent = ctx_parent(c);
	if (node_end != parent_hi || (parent && parent->kind == N_MODULE)) {
		CommentList tc = cl_from(c, trailing_comments_with_previous(c, node_end));
		items_extend(&items, gen_comments_as_trailing(c, nrange(node), tc));
	}

	LineNumber ln;
	IsStartOfLine isol;
	if (take_current_before_comments_start_info(c, &ln, &isol)) {
		Items n = {0};
		items_push_ln(&n, ln);
		items_push_isol(&n, isol);
		items_extend(&n, items);
		items = n;
	}

	c->nparents--;
	c->current_node = c->parents[c->nparents];
	return items;
}

static Items gen_node(Node *node, Ctx *c) { return gen_node_with_inner_gen(node, c, NULL, NULL); }

/* ---- comments ---- */

static Items gen_comment(Ctx *c, const Comment *cm, bool *generated);

static bool has_leading_astrisk_each_line(Str text)
{
	if (!memchr(text.p, '\n', text.n))
		return false;
	/* text.trim().split('\n') */
	size_t lo = 0, hi = text.n;
	while (lo < hi && is_rust_whitespace((unsigned char)text.p[lo]))
		lo++;
	hi = lo + rust_trim_end_len(text.p + lo, hi - lo);
	size_t start = lo;
	for (size_t i = lo; i <= hi; i++) {
		if (i == hi || text.p[i] == '\n') {
			size_t k = start;
			while (k < i && is_rust_whitespace((unsigned char)text.p[k]))
				k++;
			if (k >= i || text.p[k] != '*')
				return false;
			start = i + 1;
		}
	}
	return true;
}

/* gen_js_doc_or_multiline_block */
static Items gen_js_doc_or_multiline_block(Str text)
{
	bool is_js_doc = text.n > 0 && text.p[0] == '*';
	/* build_lines */
	Str *lines = xalloc(sizeof(Str) * (text.n + 1));
	int nlines = 0;
	size_t byte_index = 0;
	size_t start = 0;
	int line_index = 0;
	for (size_t i = 0; i <= text.n; i++) {
		if (i < text.n && text.p[i] != '\n')
			continue;
		Str line = {text.p + start, i - start};
		byte_index += (line_index == 0 ? 0 : 1) + line.n;
		bool is_last = byte_index == text.n;
		if (line.n > 0 && line.p[line.n - 1] == '\r')
			line.n--;
		Str t;
		if (line_index == 0 && !(line.n > 0 && line.p[0] == '*')) {
			t = line;
		} else {
			/* get_line_start_index */
			size_t k = 0, idx = 0;
			while (k < line.n) {
				uint32_t ch;
				int w = utf8_decode(line.p + k, line.n - k, &ch);
				if (ch == '*') {
					idx = k + 1;
					break;
				} else if (!is_rust_whitespace(ch)) {
					idx = k;
					break;
				}
				k += (size_t)w;
			}
			t = (Str){line.p + idx, line.n - idx};
		}
		size_t tn = rust_trim_end_len(t.p, t.n);
		bool keep_full = is_last && tn > 0 && !(tn == 0);
		/* text.trim().is_empty() == (trim_end_len == 0 after trim_start) */
		size_t ts = 0;
		while (ts < t.n && is_rust_whitespace((unsigned char)t.p[ts]))
			ts++;
		bool trim_empty = ts >= tn;
		if (!(is_last && !trim_empty))
			t.n = tn;
		(void)keep_full;
		if (t.n > 0 || !(nlines > 0 && lines[nlines - 1].n == 0))
			lines[nlines++] = t;
		line_index++;
		start = i + 1;
	}
	/* lines_to_print_items */
	Items items = {0};
	items_push_str(&items, "/*");
	for (int i = 0; i < nlines; i++) {
		Str line = lines[i];
		if (i > 0)
			items_push_signal(&items, SIG_NEW_LINE);
		Buf b = {0};
		if (is_js_doc && i == 0)
			buf_addc(&b, '*');
		else if (i > 0)
			buf_adds(&b, " *");
		bool is_space_or_asterisk = line.n > 0 && (line.p[0] == '*' || line.p[0] == ' ');
		if (line.n > 0 && !is_space_or_asterisk)
			buf_addc(&b, ' ');
		items_push_strn(&items, b.p ? b.p : "", b.n);
		if (line.n > 0)
			items_extend(&items, gen_from_raw_string(line.p, line.n));
	}
	Str last = nlines > 0 ? lines[nlines - 1] : (Str){"", 0};
	if (nlines > 1 && last.n == 0)
		items_push_str(&items, "/");
	else if (nlines > 1 && last.n > 0 && (last.p[last.n - 1] == '*' || last.p[last.n - 1] == ' '))
		items_push_str(&items, "*/");
	else
		items_push_str(&items, " */");
	return items;
}

static Items gen_comment(Ctx *c, const Comment *cm, bool *generated)
{
	if (has_handled_comment(c, cm)) {
		if (generated)
			*generated = false;
		return (Items){0};
	}
	mark_comment_handled(c, (Comment *)cm);
	if (generated)
		*generated = true;
	Str t = comment_text(c, cm);
	if (cm->block) {
		if (has_leading_astrisk_each_line(t))
			return gen_js_doc_or_multiline_block(t);
		return gen_js_like_comment_block(t.p, t.n);
	}
	return gen_js_like_comment_line(t.p, t.n, false);
}

static Items gen_comment_based_on_last_node(Ctx *c, const Comment *cm, const Range *last_node, bool separate_with_newlines)
{
	Items items = {0};
	bool pushed_ignore_new_lines = false;
	if (last_node) {
		int comment_start_line = cstart_line(c, cm);
		int last_node_end_line = end_line(c, *last_node);
		if (separate_with_newlines || comment_start_line > last_node_end_line) {
			items_push_signal(&items, SIG_NEW_LINE);
			if (comment_start_line > last_node_end_line + 1)
				items_push_signal(&items, SIG_NEW_LINE);
		} else if (!cm->block) {
			items_push_signal(&items, SIG_START_FORCE_NO_NEW_LINES);
			items_push_str(&items, " ");
			pushed_ignore_new_lines = true;
		} else {
			Str lt = text_of(c, *last_node);
			if (lt.n >= 2 && lt.p[0] == '/' && lt.p[1] == '*')
				items_push_str(&items, " ");
		}
	}
	items_extend(&items, gen_comment(c, cm, NULL));
	if (pushed_ignore_new_lines)
		items_push_signal(&items, SIG_FINISH_FORCE_NO_NEW_LINES);
	return items;
}

static Items gen_comment_collection_list(const Comment **comments, int n, const Range *last_node_in, const Range *next_node,
					 Ctx *c)
{
	Range last;
	const Range *last_node = NULL;
	if (last_node_in) {
		last = *last_node_in;
		last_node = &last;
	}
	Items items = {0};
	int next_line = next_node ? start_line(c, *next_node) : -1;
	for (int i = 0; i < n; i++) {
		const Comment *cm = comments[i];
		if (!has_handled_comment(c, cm)) {
			bool sep = next_node ? cstart_line(c, cm) != next_line : false;
			items_extend(&items, gen_comment_based_on_last_node(c, cm, last_node, sep));
			last = rng(cm->lo, cm->hi);
			last_node = &last;
		}
	}
	return items;
}

static Items gen_comment_collection(Ctx *c, CommentList l, const Range *last_node, const Range *next_node)
{
	return gen_comment_collection_list(l.v, l.n, last_node, next_node, c);
}

static Items gen_comments_as_leading(Ctx *c, Range node, CommentList comments)
{
	Items items = {0};
	if (comments.n == 0)
		return items;
	const Comment *last_comment = comments.v[comments.n - 1];
	bool last_previously_handled = has_handled_comment(c, last_comment);
	items_extend(&items, gen_comment_collection(c, comments, NULL, &node));
	if (!last_previously_handled) {
		int node_start_line = start_line(c, node);
		int last_comment_end_line = cend_line(c, last_comment);
		if (node_start_line > last_comment_end_line) {
			Items nl = {0};
			items_push_signal(&nl, SIG_NEW_LINE);
			if (node_start_line - 1 > last_comment_end_line)
				items_push_signal(&nl, SIG_NEW_LINE);
			items_push_cond(&items, if_true_or("spaceIfForcingNoNewlines", res_is_forcing_no_newlines(),
							   items_signal(SIG_SPACE_IF_NOT_TRAILING), nl));
		} else if (last_comment->block) {
			if (node_start_line == last_comment_end_line)
				items_push_signal(&items, SIG_SPACE_IF_NOT_TRAILING);
			else
				items_push_cond(&items, if_true("conditionalCommentBlockSpace", res_is_forcing_no_newlines(),
								items_signal(SIG_SPACE_IF_NOT_TRAILING)));
		}
	}
	return items;
}

static Items gen_leading_comments(Ctx *c, Range node)
{
	return gen_comments_as_leading(c, node, cl_from(c, leading_comments(c, node.lo)));
}

static Items gen_comments_as_statements(Ctx *c, CommentList comments, const Range *last_node_in)
{
	Range last;
	const Range *last_node = NULL;
	if (last_node_in) {
		last = *last_node_in;
		last_node = &last;
	}
	Items items = {0};
	bool was_last_block = false;
	for (int i = 0; i < comments.n; i++) {
		const Comment *cm = comments.v[i];
		if (!has_handled_comment(c, cm)) {
			items_extend(&items, gen_comment_based_on_last_node(c, cm, last_node, true));
			last = rng(cm->lo, cm->hi);
			last_node = &last;
			was_last_block = cm->block;
		}
	}
	if (was_last_block)
		items_push_signal(&items, SIG_EXPECT_NEW_LINE);
	return items;
}

static CommentList get_trailing_comments_as_statements(Ctx *c, Range node)
{
	CommentList l = {0};
	int node_end_line = end_line(c, node);
	Comments tc = trailing_comments(c, node.hi);
	CM_FOR(tc, c->src, cm)
	{
		if (!has_handled_comment(c, cm) && node_end_line < cend_line(c, cm))
			cl_push(&l, cm);
	}
	return l;
}

static Items gen_trailing_comments_as_statements(Ctx *c, Range node)
{
	return gen_comments_as_statements(c, get_trailing_comments_as_statements(c, node), &node);
}

static CommentList get_leading_comments_on_previous_lines(Ctx *c, Range node)
{
	CommentList l = {0};
	Comments lc = leading_comments(c, node.lo);
	if (cm_empty(&lc))
		return l;
	int node_start_line = start_line(c, node);
	const Tok *prev = previous_token(c, node);
	int prev_line = prev ? end_line(c, trange(prev)) : -1;
	CM_FOR(lc, c->src, cm)
	{
		int line = cstart_line(c, cm);
		if (line < node_start_line && (!prev || prev_line < node_start_line))
			cl_push(&l, cm);
		else
			break;
	}
	return l;
}

static CommentList get_leading_comments_on_same_line(Ctx *c, Range node)
{
	CommentList l = {0};
	Comments lc = leading_comments(c, node.lo);
	int node_start_line = start_line(c, node);
	CM_FOR(lc, c->src, cm)
	{
		if (cstart_line(c, cm) == node_start_line)
			cl_push(&l, cm);
	}
	return l;
}

static CommentList get_trailing_comments_on_same_line(Ctx *c, Range node)
{
	CommentList l = {0};
	Comments tc = trailing_comments(c, node.hi);
	int node_start_line = start_line(c, node);
	CM_FOR(tc, c->src, cm)
	{
		if (cstart_line(c, cm) == node_start_line)
			cl_push(&l, cm);
		else
			break;
	}
	return l;
}

static Items gen_comments_same_line(Ctx *c, CommentList comments)
{
	Items items = {0};
	bool had_comment_last = false;
	for (int i = 0; i < comments.n; i++) {
		if (had_comment_last)
			items_push_str(&items, " ");
		bool generated;
		Items ci = gen_comment(c, comments.v[i], &generated);
		if (generated) {
			items_extend(&items, ci);
			had_comment_last = true;
		} else {
			had_comment_last = false;
		}
	}
	return items;
}

static Items gen_leading_comments_same_line(Ctx *c, Range node)
{
	CommentList l = get_leading_comments_on_same_line(c, node);
	if (l.n == 0)
		return (Items){0};
	Items items = gen_comments_same_line(c, l);
	if (!items_empty(items))
		items_push_str(&items, " ");
	return items;
}

static Items gen_trailing_comments_same_line(Ctx *c, Range node)
{
	CommentList l = get_trailing_comments_on_same_line(c, node);
	Items items = {0};
	if (l.n == 0)
		return items;
	Items ci = gen_comments_same_line(c, l);
	if (!items_empty(ci)) {
		items_push_str(&items, " ");
		items_extend(&items, ci);
	}
	return items;
}

static Items gen_trailing_comments_if_line_comment_same_line(Ctx *c, Range node)
{
	CommentList l = get_trailing_comments_on_same_line(c, node);
	bool any_line = false;
	for (int i = 0; i < l.n; i++)
		if (!l.v[i]->block)
			any_line = true;
	Items items = {0};
	if (!any_line)
		return items;
	Items ci = gen_comments_same_line(c, l);
	if (!items_empty(ci)) {
		items_push_str(&items, " ");
		items_extend(&items, ci);
	}
	return items;
}

static Items gen_comments_between_lines_indented(Ctx *c, uint32_t start_between_pos)
{
	CommentList comments = {0};
	Comments tc = trailing_comments(c, start_between_pos);
	if (!cm_empty(&tc)) {
		uint32_t next_pos = get_next_token_pos_after(c, prange(start_between_pos));
		int next_line = line_of(c, next_pos);
		CM_FOR(tc, c->src, cm)
		{
			if (!has_handled_comment(c, cm) && cstart_line(c, cm) < next_line)
				cl_push(&comments, cm);
		}
	}
	Items items = {0};
	if (comments.n > 0) {
		Items inner = {0};
		if (comments.v[0]->block)
			items_push_signal(&inner, SIG_SPACE_IF_NOT_TRAILING);
		Range r = prange(start_between_pos);
		items_extend(&inner, gen_comment_collection(c, comments, &r, NULL));
		items_extend(&items, with_indent(inner));
		items_push_signal(&items, SIG_NEW_LINE);
	}
	return items;
}

static CommentList get_trailing_comments_same_line(Ctx *c, Range node, CommentList trailing)
{
	int node_end_line = end_line(c, node);
	CommentList same = {0};
	for (int i = 0; i < trailing.n; i++)
		if (cstart_line(c, trailing.v[i]) <= node_end_line)
			cl_push(&same, trailing.v[i]);
	if (same.n == 0)
		return same;
	uint32_t comma_end = 0;
	bool has_comma = false;
	if (text_eq(c, node, ",")) {
		comma_end = node.hi;
		has_comma = true;
	} else {
		const Tok *t = get_next_token_if_comma(c, node);
		if (t) {
			comma_end = t->hi;
			has_comma = true;
		}
	}
	if (has_comma) {
		uint32_t next_pos = get_next_token_pos_after(c, prange(comma_end));
		if (line_of(c, next_pos) == node_end_line) {
			CommentList f = {0};
			for (int i = 0; i < same.n; i++)
				if (same.v[i]->lo < comma_end)
					cl_push(&f, same.v[i]);
			return f;
		}
	}
	return same;
}

static Items gen_comments_as_trailing(Ctx *c, Range node, CommentList trailing)
{
	Items items = {0};
	if (trailing.n == 0)
		return items;
	CommentList same = get_trailing_comments_same_line(c, node, trailing);
	if (same.n == 0)
		return items;
	for (int i = 0; i < same.n; i++) {
		if (!has_handled_comment(c, same.v[i])) {
			if (same.v[i]->block)
				items_push_str(&items, " ");
			break;
		}
	}
	items_extend(&items, gen_comment_collection(c, same, &node, NULL));
	return items;
}

static Items gen_trailing_comments(Ctx *c, Range node)
{
	return gen_comments_as_trailing(c, node, cl_from(c, trailing_comments(c, node.hi)));
}

/* ---- separators ---- */

enum { SEP_NONE, SEP_SEMI_PREFER, SEP_COMMA_ONLY_MULTI_LINE, SEP_COMMA_ALWAYS, SEP_COMMA_NEVER };

static Items get_generated_separator(int sep, bool is_trailing, Resolver is_multi_line)
{
	Items items = {0};
	switch (sep) {
	case SEP_SEMI_PREFER:
		if (is_trailing)
			return items_cond(if_true("semiColonIfMultiLine", is_multi_line, items_str(";")));
		return items_str(";");
	case SEP_COMMA_ONLY_MULTI_LINE:
		if (!is_trailing)
			return items_str(",");
		return items_cond(if_true("trailingCommaIfMultiLine", is_multi_line, items_str(",")));
	case SEP_COMMA_ALWAYS:
		return items_str(",");
	case SEP_COMMA_NEVER:
		return is_trailing ? items : items_str(",");
	default:
		return items;
	}
}

/* ---- gen_node_with_separator ---- */

static Items inner_push_separator(Items items, Ctx *c, void *data)
{
	items_push_path(&items, (PNode *)data);
	return items;
}

static Items gen_node_with_separator(Ctx *c, Node *value, Items generated_separator)
{
	Items items = {0};
	const Tok *comma = get_next_token_if_comma(c, nrange(value));
	if (!comma)
		comma = get_last_token_within_if_comma(c, nrange(value));
	Items trailing = comma ? gen_trailing_comments(c, trange(comma)) : (Items){0};
	/* ignored nodes ending with `;`: not supported (deno-fmt-ignore inside members) */
	PNode *sep = items_into_path(generated_separator);
	items_extend(&items, gen_node_with_inner_gen(value, c, inner_push_separator, sep));
	items_extend(&items, trailing);
	return items;
}

/* ---- allows_inline_multi_line ---- */

static bool should_skip_paren_expr(Ctx *c, Node *node);

static bool allow_for_callee_expr(Node *e)
{
	switch (e->kind) {
	case N_MEMBER_EXPR:
		return allow_for_callee_expr(e->a);
	case N_CALL_EXPR:
		return false;
	case N_OPT_CHAIN_EXPR:
		return e->a->kind != N_OPT_CALL;
	default:
		return true;
	}
}

static bool allows_inline_multi_line(Ctx *c, Node *node, bool has_siblings)
{
	switch (node->kind) {
	case N_PARAM:
		return allows_inline_multi_line(c, node->a, has_siblings);
	case N_TS_AS_EXPR:
		return allows_inline_multi_line(c, node->a, has_siblings) &&
		       (node->b->kind == N_TS_TYPE_REF || node->b->kind == N_TS_KEYWORD_TYPE ||
			allows_inline_multi_line(c, node->b, has_siblings));
	case N_FN_EXPR:
	case N_ARROW_EXPR:
	case N_OBJECT_LIT:
	case N_ARRAY_LIT:
	case N_OBJECT_PAT:
	case N_ARRAY_PAT:
	case N_TS_TYPE_LIT:
	case N_TS_TUPLE_TYPE:
	case N_TS_ARRAY_TYPE:
		return true;
	case N_EXPR_OR_SPREAD:
		return allows_inline_multi_line(c, node->a, has_siblings);
	case N_PAREN_EXPR:
		return should_skip_paren_expr(c, node) && allows_inline_multi_line(c, node->a, has_siblings);
	case N_TAGGED_TPL:
	case N_TPL:
		return !has_siblings;
	case N_CALL_EXPR:
	case N_OPT_CALL:
		return !has_siblings && allow_for_callee_expr(node->a);
	case N_BINDING_IDENT:
		return node->b ? allows_inline_multi_line(c, node->b->a, has_siblings) : false;
	case N_ASSIGN_PAT:
		return allows_inline_multi_line(c, node->a, has_siblings) || allows_inline_multi_line(c, node->b, has_siblings);
	case N_TS_TYPE_ANN:
		return allows_inline_multi_line(c, node->a, has_siblings);
	case N_TS_TUPLE_ELEMENT:
		return allows_inline_multi_line(c, node->b, has_siblings);
	default:
		return false;
	}
}

/* ---- has_any_node_comment_on_different_line & co ---- */

static bool check_pos_has_trailing_comments(Ctx *c, uint32_t end, bool has_next, uint32_t next_pos)
{
	int end_l = line_of(c, end);
	int stop_line = has_next ? line_of(c, next_pos) : -1;
	Comments tc = trailing_comments(c, end);
	CM_FOR(tc, c->src, cm)
	{
		if (!cm->block)
			return true;
		if (has_next && cstart_line(c, cm) >= stop_line)
			return false;
		if (cend_line(c, cm) > end_l)
			return true;
	}
	return false;
}

static bool has_any_node_comment_on_different_line(Ctx *c, const Range *nodes, int n)
{
	for (int i = 0; i < n; i++) {
		if (i == 0) {
			int first_line = start_line(c, nodes[0]);
			Comments lc = leading_comments(c, nodes[0].lo);
			if (cm_count(&lc) > 0) {
				const Comment *cm = cm_get(&lc, c->src, 0);
				if (!cm->block || cstart_line(c, cm) < first_line)
					return true;
			}
		}
		uint32_t node_end = nodes[i].hi;
		bool has_next = i + 1 < n;
		uint32_t next_pos = has_next ? nodes[i + 1].lo : 0;
		if (check_pos_has_trailing_comments(c, node_end, has_next, next_pos))
			return true;
		const Tok *comma = get_next_token_if_comma(c, prange(node_end));
		if (comma && check_pos_has_trailing_comments(c, comma->hi, has_next, next_pos))
			return true;
	}
	return false;
}

static Range *node_ranges(Node **nodes, int n)
{
	Range *r = xalloc(sizeof(Range) * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		r[i] = nrange(nodes[i]);
	return r;
}

static bool get_use_new_lines_for_nodes_with_preceeding_token(Ctx *c, const char *open, Node **nodes, int n,
							      bool prefer_single_line)
{
	if (n == 0)
		return false;
	if (prefer_single_line)
		return has_any_node_comment_on_different_line(c, node_ranges(nodes, n), n);
	const Tok *prev = previous_token(c, nrange(nodes[0]));
	if (prev && tok_text_eq(c, prev, open))
		return node_helpers_get_use_new_lines_for_nodes(c, trange(prev), nrange(nodes[0]));
	return false;
}

static bool get_use_new_lines_for_nodes(Ctx *c, Node **nodes, int n, bool prefer_single_line)
{
	if (n < 2)
		return false;
	if (prefer_single_line)
		return has_any_node_comment_on_different_line(c, node_ranges(nodes, n), n);
	return node_helpers_get_use_new_lines_for_nodes(c, nrange(nodes[0]), nrange(nodes[1]));
}

static bool is_node_definitely_above_line_width(Ctx *c, Range r)
{
	Str t = text_of(c, r);
	size_t max_width = LINE_WIDTH * 2;
	if (t.n < max_width)
		return false;
	size_t count = 0;
	for (size_t i = 0; i < t.n;) {
		uint32_t ch;
		i += (size_t)utf8_decode(t.p + i, t.n - i, &ch);
		if (!is_rust_whitespace(ch)) {
			if (++count > max_width)
				return true;
		} else if (ch == '\n') {
			return false;
		}
	}
	return false;
}

/* ---- gen_separated_values ---- */

typedef struct {
	Node **nodes; /* NULL entries are separators (comma tokens in seps) */
	const Tok **seps;
	int n;
	bool prefer_hanging, force_use_new_lines, allow_blank_lines;
	int separator;
	SingleLineOptions single_line_options;
	MultiLineOptions multi_line_options;
	bool force_possible_newline_at_start;
	int (*node_sorter)(Ctx *c, int ai, Node *a, int bi, Node *b);
	Ctx *ctx;
} SepValuesParams;

static int *get_sorted_indexes(Ctx *c, Node **nodes, int n, int (*sorter)(Ctx *, int, Node *, int, Node *))
{
	int *order = xalloc(sizeof(int) * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		order[i] = i;
	/* sort_unstable_by with a total order (ties broken by index): insertion sort is equivalent */
	for (int i = 1; i < n; i++) {
		int x = order[i];
		int j = i - 1;
		while (j >= 0 && sorter(c, order[j], nodes[order[j]], x, nodes[x]) > 0) {
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = x;
	}
	int *old_to_new = xalloc(sizeof(int) * (size_t)(n + 1));
	for (int new_index = 0; new_index < n; new_index++)
		old_to_new[order[new_index]] = new_index;
	return old_to_new;
}

static GeneratedValues sep_values_fn(ConditionReference ref, void *data)
{
	SepValuesParams *o = data;
	Ctx *c = o->ctx;
	Resolver is_multi_line = condref_resolver(ref);
	GeneratedValues values = {0};
	int nodes_count = o->n;
	int *sorted = o->node_sorter ? get_sorted_indexes(c, o->nodes, o->n, o->node_sorter) : NULL;
	GeneratedValue *tmp = xalloc(sizeof(GeneratedValue) * (size_t)(o->n + 1));
	for (int i = 0; i < o->n; i++) {
		Node *value = o->nodes[i];
		int node_index = sorted ? sorted[i] : i;
		bool allow_multi = false, allow_single = false;
		if (value) {
			allow_multi = allows_inline_multi_line(c, value, nodes_count > 1);
			allow_single = node_index + 1 == nodes_count;
		}
		LinesSpan span = {0};
		if (o->allow_blank_lines && value) {
			span.has = true;
			span.start_line = (size_t)start_line_with_comments(c, nrange(value));
			span.end_line = (size_t)end_line_with_comments(c, nrange(value));
		}
		Items items;
		if (o->separator == SEP_NONE) {
			items = gen_node(value, c);
		} else {
			Items gs = get_generated_separator(o->separator, node_index == nodes_count - 1, is_multi_line);
			if (value) {
				items = gen_node_with_separator(c, value, gs);
			} else {
				const Tok *st = o->seps[i];
				items = (Items){0};
				Range sr = trange(st);
				items_extend(&items, gen_comment_collection(c, cl_from(c, leading_comments(c, st->lo)), NULL, &sr));
				items_extend(&items, gs);
				items_extend(&items, gen_trailing_comments_same_line(c, sr));
			}
		}
		bool use_new_line_group = true;
		if (value && value->kind == N_EXPR_OR_SPREAD && (value->a->kind == N_OBJECT_LIT || value->a->kind == N_ARRAY_LIT))
			use_new_line_group = false;
		GeneratedValue gv = {use_new_line_group ? new_line_group(items) : items, span, allow_multi, allow_single};
		tmp[sorted ? sorted[i] : i] = gv;
	}
	for (int i = 0; i < o->n; i++)
		gv_push(&values, tmp[i]);
	return values;
}

static GenSeparatedValuesResult gen_separated_values_with_result(Ctx *c, SepValuesParams o)
{
	SepValuesParams *p = xalloc(sizeof *p);
	*p = o;
	p->ctx = c;
	GenSeparatedValuesOptions opts;
	opts.prefer_hanging = o.prefer_hanging;
	opts.force_use_new_lines = o.force_use_new_lines;
	opts.allow_blank_lines = o.allow_blank_lines;
	opts.indent_width = INDENT_WIDTH;
	opts.single_line_options = o.single_line_options;
	opts.multi_line_options = o.multi_line_options;
	opts.force_possible_newline_at_start = o.force_possible_newline_at_start;
	return gen_separated_values(sep_values_fn, p, opts);
}

static Items gen_separated_values_items(Ctx *c, SepValuesParams o) { return gen_separated_values_with_result(c, o).items; }

/* ---- gen_surrounded_by_tokens ---- */

typedef Items (*GenInnerFn)(Ctx *c, void *data);

typedef struct {
	const char *open_token, *close_token;
	bool has_range;
	Range range;
	bool has_first_member;
	Range first_member;
	bool prefer_single_line_when_empty;
	bool allow_open_token_trailing_comments;
	bool single_line_space_around;
} SurroundedOpts;

static Items gen_first_line_trailing_comment(Ctx *c, int open_token_start_line, Comments comments)
{
	Items items = {0};
	if (cm_count(&comments) == 0)
		return items;
	const Comment *first = cm_get(&comments, c->src, 0);
	if (!first->block && cstart_line(c, first) == open_token_start_line) {
		bool generated;
		Items gi = gen_comment(c, first, &generated);
		if (generated) {
			items_push_signal(&items, SIG_START_FORCE_NO_NEW_LINES);
			items_push_str(&items, " ");
			items_extend(&items, gi);
			items_push_signal(&items, SIG_FINISH_FORCE_NO_NEW_LINES);
		}
	}
	return items;
}

typedef struct {
	LineAndColumn before;
} NlHasCommentsData;

static int r_new_line_if_has_comments(RCtx *rc, void *d)
{
	int same = ch_is_at_same_position(rc, ((NlHasCommentsData *)d)->before);
	if (same < 0)
		return -1;
	return !same && !wi_is_start_of_line(&rc->writer_info);
}

typedef struct {
	Ctx *c;
	CommentList comments;
} CommentValuesData;

static GeneratedValues comment_values_fn(ConditionReference ref, void *data)
{
	CommentValuesData *d = data;
	GeneratedValues values = {0};
	for (int i = 0; i < d->comments.n; i++) {
		const Comment *cm = d->comments.v[i];
		size_t sl = (size_t)cstart_line(d->c, cm), el = (size_t)cend_line(d->c, cm);
		bool generated;
		Items it = gen_comment(d->c, cm, &generated);
		if (generated)
			gv_push(&values, (GeneratedValue){it, {true, sl, el}, false, false});
	}
	return values;
}

/* custom_close (may be NULL) generates the close token; it runs last, like the Rust FnOnce. */
static Items gen_surrounded_by_tokens(Ctx *c, GenInnerFn gen_inner, void *data, GenInnerFn custom_close, void *close_data,
				      SurroundedOpts opts)
{
	Items items = {0};
	if (opts.has_range) {
		Range range = opts.range;
		uint32_t open_token_end = range.lo + (uint32_t)strlen(opts.open_token);
		uint32_t close_token_start = range.hi - (uint32_t)strlen(opts.close_token);
		int open_token_start_line = line_of(c, open_token_end);
		bool is_single_line;
		items_extend(&items, gen_leading_comments(c, range));
		items_push_str(&items, opts.open_token);
		if (opts.has_first_member) {
			int first_member_start_line = start_line(c, opts.first_member);
			is_single_line = open_token_start_line == first_member_start_line;
			if (is_single_line && opts.single_line_space_around)
				items_push_str(&items, " ");
			if (opts.allow_open_token_trailing_comments && !is_single_line)
				items_extend(&items, gen_first_line_trailing_comment(c, open_token_start_line,
										     trailing_comments(c, open_token_end)));
			items_extend(&items, gen_inner(c, data));
			NlHasCommentsData *d = xalloc(sizeof *d);
			d->before = lc_new();
			items_push_lc(&items, d->before);
			items_extend(&items, with_indent(gen_trailing_comments_as_statements(c, prange(open_token_end))));
			items_extend(&items, with_indent(gen_comments_as_statements(
						     c, cl_from(c, leading_comments(c, close_token_start)), NULL)));
			items_push_cond(&items, if_true("newLineIfHasCommentsAndNotStartOfNewLine",
							resolver(r_new_line_if_has_comments, d), items_signal(SIG_NEW_LINE)));
		} else {
			Comments comments = trailing_comments(c, open_token_end);
			is_single_line = open_token_start_line == line_of(c, close_token_start);
			if (is_single_line && opts.single_line_space_around)
				items_push_str(&items, " ");
			if (!cm_empty(&comments)) {
				if (!is_single_line)
					items_extend(&items, gen_first_line_trailing_comment(c, open_token_start_line, comments));
				CommentList cl = cl_from(c, comments);
				bool any_unhandled = false;
				for (int i = 0; i < cl.n; i++)
					if (!has_handled_comment(c, cl.v[i]))
						any_unhandled = true;
				if (any_unhandled) {
					if (is_single_line) {
						CommentValuesData *d = xalloc(sizeof *d);
						d->c = c;
						d->comments = cl;
						GenSeparatedValuesOptions go = {0};
						go.force_use_new_lines = !is_single_line;
						go.allow_blank_lines = true;
						go.indent_width = INDENT_WIDTH;
						go.single_line_options = (SingleLineOptions){opts.single_line_space_around,
											    opts.single_line_space_around,
											    items_signal(SIG_SPACE_OR_NEW_LINE)};
						go.multi_line_options = mlo_surround_newlines_indented();
						items_extend(&items, gen_separated_values(comment_values_fn, d, go).items);
					} else {
						items_push_signal(&items, SIG_NEW_LINE);
						items_extend(&items, with_indent(gen_comments_as_statements(c, cl, NULL)));
						items_push_signal(&items, SIG_NEW_LINE);
					}
				}
			} else if (!is_single_line && !opts.prefer_single_line_when_empty) {
				items_push_signal(&items, SIG_NEW_LINE);
			}
		}
		if (is_single_line && opts.single_line_space_around)
			items_push_str(&items, " ");
	} else {
		items_push_str(&items, opts.open_token);
		items_extend(&items, gen_inner(c, data));
	}
	if (custom_close)
		items_extend(&items, custom_close(c, close_data));
	else
		items_push_str(&items, opts.close_token);
	return items;
}

/* ---- gen_parameters_or_arguments ---- */

typedef struct {
	LineNumber start_ln;
	LineStartIndentLevel start_lsil;
} StartArrowData;

static int r_is_different_line_and_start_line_indentation(RCtx *rc, void *d)
{
	StartArrowData *x = d;
	uint32_t ln, lsil;
	if (!rctx_ln(rc, x->start_ln, &ln))
		return -1;
	if (!rctx_lsil(rc, x->start_lsil, &lsil))
		return -1;
	return ln != rc->writer_info.line_number && lsil != rc->writer_info.line_start_indent_level;
}

static bool is_arrow_function_with_expr_body(Node *n)
{
	return n->kind == N_EXPR_OR_SPREAD && n->a->kind == N_ARROW_EXPR && n->a->a->kind != N_BLOCK_STMT;
}

typedef struct {
	Node **nodes;
	int n;
	bool force_use_new_lines, is_parameters;
	int trailing_commas;
} ParamsArgsData;

static Items params_args_inner(Ctx *c, void *data)
{
	ParamsArgsData *d = data;
	Items items = {0};
	if (!d->force_use_new_lines && d->n == 1 && is_arrow_function_with_expr_body(d->nodes[0])) {
		StartArrowData *x = xalloc(sizeof *x);
		x->start_ln = ln_new();
		x->start_lsil = lsil_new();
		Items generated = gen_node(d->nodes[0], c);
		items_push_ln(&items, x->start_ln);
		items_push_lsil(&items, x->start_lsil);
		items_push_signal(&items, SIG_POSSIBLE_NEW_LINE);
		items_push_cond(&items, indent_if_start_of_line(generated));
		items_push_cond(&items, if_true("isDifferentLineAndStartLineIndentation",
						resolver(r_is_different_line_and_start_line_indentation, x),
						items_signal(SIG_NEW_LINE)));
	} else {
		const Tok *last_comma = d->n > 0 ? get_next_token_if_comma(c, nrange(d->nodes[d->n - 1])) : NULL;
		SepValuesParams o = {0};
		o.nodes = d->nodes;
		o.n = d->n;
		o.force_use_new_lines = d->force_use_new_lines;
		o.separator = d->trailing_commas;
		o.single_line_options = (SingleLineOptions){false, false, items_signal(SIG_SPACE_OR_NEW_LINE)};
		o.multi_line_options = mlo_surround_newlines_indented();
		o.force_possible_newline_at_start = d->is_parameters;
		items_extend(&items, gen_separated_values_items(c, o));
		if (last_comma)
			items_extend(&items, gen_trailing_comments_same_line(c, trange(last_comma)));
	}
	return items;
}

static Items gen_parameters_or_arguments(Ctx *c, Node *node, const Range *range, Node **nodes, int n,
					 GenInnerFn custom_close, void *close_data, bool is_parameters)
{
	bool force_use_new_lines = get_use_new_lines_for_nodes_with_preceeding_token(c, "(", nodes, n, true);
	int trailing_commas = SEP_COMMA_ONLY_MULTI_LINE;
	if (n > 0) {
		Node *last = nodes[n - 1];
		if ((last->kind == N_PARAM && last->a->kind == N_REST_PAT) || last->kind == N_REST_PAT)
			trailing_commas = SEP_COMMA_NEVER;
	}
	ParamsArgsData *d = xalloc(sizeof *d);
	d->nodes = nodes;
	d->n = n;
	d->force_use_new_lines = force_use_new_lines;
	d->is_parameters = is_parameters;
	d->trailing_commas = trailing_commas;
	SurroundedOpts so = {0};
	so.open_token = "(";
	so.close_token = ")";
	if (range) {
		so.has_range = true;
		so.range = *range;
	}
	if (n > 0) {
		so.has_first_member = true;
		so.first_member = nrange(nodes[0]);
	}
	so.prefer_single_line_when_empty = true;
	so.allow_open_token_trailing_comments = true;
	return gen_surrounded_by_tokens(c, params_args_inner, d, custom_close, close_data, so);
}

/* ---- gen_close_paren_with_type ---- */

typedef struct {
	bool has_type_node;
	LineStartIndentLevel start_lsil;
	LineNumber type_start_ln, type_end_ln;
} CloseParenData;

static int r_new_line_if_header_hanging(RCtx *rc, void *d)
{
	CloseParenData *x = d;
	if (!x->has_type_node || wi_is_start_of_line(&rc->writer_info))
		return 0;
	int hanging = ch_is_hanging(rc, x->start_lsil, NULL);
	if (hanging >= 0) {
		int multi = ch_is_multiple_lines(rc, x->type_start_ln, x->type_end_ln);
		if (multi >= 0)
			return hanging && multi;
	}
	return -1;
}

static Items gen_close_paren_with_type(Ctx *c, LineStartIndentLevel start_lsil, Node *type_node, Items *type_node_separator,
				       int param_count)
{
	CloseParenData *x = xalloc(sizeof *x);
	x->type_start_ln = ln_new();
	x->has_type_node = type_node != NULL;
	x->type_end_ln = ln_new();
	x->start_lsil = start_lsil;
	Items generated_type = {0};
	if (type_node) {
		bool use_new_line_group = param_count != 0;
		items_push_ln(&generated_type, x->type_start_ln);
		if (type_node_separator) {
			items_extend(&generated_type, *type_node_separator);
		} else {
			items_push_str(&generated_type, ":");
			items_push_signal(&generated_type, SIG_SPACE_IF_NOT_TRAILING);
		}
		items_extend(&generated_type, gen_node(type_node, c));
		items_push_ln(&generated_type, x->type_end_ln);
		if (use_new_line_group)
			generated_type = new_line_group(generated_type);
	}
	Items items = {0};
	items_push_cond(&items, if_true("newLineIfHeaderHangingAndTypeNodeMultipleLines",
					resolver(r_new_line_if_header_hanging, x), items_signal(SIG_NEW_LINE)));
	items_push_str(&items, ")");
	items_extend(&items, generated_type);
	return items;
}

/* ---- gen_assignment_like_with_token ---- */

static Items gen_assignment_like_with_token(Ctx *c, Node *expr, const char *op, const Tok *op_token)
{
	bool use_new_line_group = expr->kind == N_MEMBER_EXPR;
	Items items = {0};
	if (strcmp(op, ":") == 0) {
		items_push_str(&items, op);
	} else {
		items_push_str(&items, xcat(" ", op));
	}
	uint32_t op_end = op_token ? op_token->hi : get_previous_token_end_before(c, nrange(expr));
	Items op_trailing = gen_comments_between_lines_indented(c, op_end);
	bool had_op_trailing = !items_empty(op_trailing);
	items_extend(&items, op_trailing);
	Items ga = {0};
	if (!had_op_trailing) {
		Items t = {0};
		items_push_signal(&t, SIG_SPACE_IF_NOT_TRAILING);
		items_push_signal(&t, SIG_POSSIBLE_NEW_LINE);
		items_push_cond(&ga, if_above_width_or(INDENT_WIDTH, t, items_signal(SIG_SPACE_IF_NOT_TRAILING)));
	}
	Items assignment = gen_node(expr, c);
	if (!had_op_trailing)
		assignment = items_cond(indent_if_start_of_line(assignment));
	if (use_new_line_group)
		assignment = new_line_group(assignment);
	items_extend(&ga, assignment);
	PNode *path = items_into_path(ga);
	items_push_cond(&items, if_true_or("indentIfStartOfLineIndentedOrTokenHadTrailingLineComment",
					   had_op_trailing ? res_true() : res_is_start_of_line_indented(),
					   with_indent(items_path(path)), items_path(path)));
	return items;
}

static Items gen_assignment(Ctx *c, Node *expr, const char *op)
{
	return gen_assignment_like_with_token(c, expr, op, previous_token(c, nrange(expr)));
}

static Items gen_type_ann_with_colon(Ctx *c, Node *type_ann, const Tok *colon)
{
	return gen_assignment_like_with_token(c, type_ann, ":", colon);
}

/* for a TsTypeAnn node */
static Items gen_type_ann_with_colon_if_exists(Ctx *c, Node *type_ann)
{
	if (!type_ann)
		return (Items){0};
	return gen_type_ann_with_colon(c, type_ann, get_first_colon_token_within(c, nrange(type_ann)));
}

/* for a type (Box<TsType>) preceded by a colon */
static Items gen_type_ann_with_colon_for_type(Ctx *c, Node *type)
{
	return gen_type_ann_with_colon(c, type, get_previous_token_if_colon(c, nrange(type)));
}

/* ---- braces ---- */

static Items space_if_not_start_line(void)
{
	return items_cond(if_true("spaceIfNotStartLine", res_is_not_start_of_line(), items_str(" ")));
}

/* gen_brace_separator with bracePosition sameLine */
static Items gen_brace_separator(void) { return space_if_not_start_line(); }

/* ---- gen_node_in_parens ---- */

static bool get_paren_range(Ctx *c, Range inner, Range *out)
{
	const Tok *open = get_previous_token_if_open_paren(c, inner);
	const Tok *close = get_next_token_if_close_paren(c, inner);
	if (open && close) {
		*out = rng(open->lo, close->hi);
		return true;
	}
	return false;
}

typedef struct {
	GenInnerFn gen;
	void *data;
	bool force_use_new_lines, prefer_hanging;
} InParensData;

static Items in_parens_inner(Ctx *c, void *data)
{
	InParensData *d = data;
	Items g = d->gen(c, d->data);
	if (d->force_use_new_lines)
		return surround_with_new_lines(with_indent(g));
	if (d->prefer_hanging)
		return g;
	return surround_with_newlines_indented_if_multi_line(g, INDENT_WIDTH);
}

static Items gen_node_in_parens(Ctx *c, GenInnerFn gen, void *data, Range inner_range, bool prefer_hanging,
				bool allow_open_paren_trailing_comments, bool single_line_space_around)
{
	Range paren_range;
	bool has_paren = get_paren_range(c, inner_range, &paren_range);
	/* parentheses.preferSingleLine is true */
	bool force = has_any_node_comment_on_different_line(c, &inner_range, 1);
	InParensData *d = xalloc(sizeof *d);
	d->gen = gen;
	d->data = data;
	d->force_use_new_lines = force;
	d->prefer_hanging = prefer_hanging;
	SurroundedOpts so = {0};
	so.open_token = "(";
	so.close_token = ")";
	so.has_range = has_paren;
	so.range = paren_range;
	so.has_first_member = true;
	so.first_member = inner_range;
	so.prefer_single_line_when_empty = true;
	so.allow_open_token_trailing_comments = allow_open_paren_trailing_comments;
	so.single_line_space_around = single_line_space_around;
	return gen_surrounded_by_tokens(c, in_parens_inner, d, NULL, NULL, so);
}

static Items gen_node_fn(Ctx *c, void *data) { return gen_node((Node *)data, c); }

/* ---- gen_extends_or_implements ---- */

static Items gen_extends_or_implements(Ctx *c, const char *text, Node **types, int n, LineStartIndentLevel start_header_lsil)
{
	Items items = {0};
	if (n == 0)
		return items;
	Items space = items_cond(if_above_width_or(INDENT_WIDTH, items_signal(SIG_SPACE_OR_NEW_LINE), items_str(" ")));
	items_push_cond(&items, new_line_if_hanging_space_otherwise(start_header_lsil, NULL, &space));
	Items inner = {0};
	items_push_str(&inner, text);
	SepValuesParams o = {0};
	o.nodes = types;
	o.n = n;
	o.separator = SEP_COMMA_NEVER;
	o.single_line_options = slo_separated_line_starting_with_space();
	o.multi_line_options = mlo_new_line_start();
	items_extend(&inner, gen_separated_values_items(c, o));
	items_push_cond(&items, indent_if_start_of_line(new_line_group(inner)));
	return items;
}

/* ---- gen_object_like_node ---- */

typedef struct {
	Node **members;
	int n;
	int separator;
	bool force_multi_line, surround_single_line_with_spaces, allow_blank_lines;
	int (*node_sorter)(Ctx *c, int ai, Node *a, int bi, Node *b);
} ObjectLikeData;

static Items object_like_inner(Ctx *c, void *data)
{
	ObjectLikeData *d = data;
	if (d->n == 0)
		return (Items){0};
	SepValuesParams o = {0};
	o.nodes = d->members;
	o.n = d->n;
	o.force_use_new_lines = d->force_multi_line;
	o.allow_blank_lines = d->allow_blank_lines;
	o.separator = d->separator;
	o.single_line_options = (SingleLineOptions){d->surround_single_line_with_spaces, d->surround_single_line_with_spaces,
						    items_signal(SIG_SPACE_OR_NEW_LINE)};
	o.multi_line_options = mlo_surround_newlines_indented();
	o.node_sorter = d->node_sorter;
	return gen_separated_values_items(c, o);
}

static Items gen_object_like_node(Ctx *c, Node *node, Node **members, int n, int separator, bool force_single_line,
				  bool force_multi_line_opt, bool surround_single_line_with_spaces, bool allow_blank_lines,
				  int (*node_sorter)(Ctx *, int, Node *, int, Node *))
{
	TokList toks = tokens_outside_children(c, node);
	const Tok *open = NULL, *close = NULL;
	for (int i = 0; i < toks.n; i++)
		if (toks.v[i]->kind == TK_LBRACE) {
			open = toks.v[i];
			break;
		}
	for (int i = toks.n - 1; i >= 0; i--)
		if (toks.v[i]->kind == TK_RBRACE) {
			close = toks.v[i];
			break;
		}
	bool force_multi_line = force_multi_line_opt ||
				(!force_single_line && get_use_new_lines_for_nodes_with_preceeding_token(c, "{", members, n, true));
	ObjectLikeData *d = xalloc(sizeof *d);
	d->members = members;
	d->n = n;
	d->separator = separator;
	d->force_multi_line = force_multi_line;
	d->surround_single_line_with_spaces = surround_single_line_with_spaces;
	d->allow_blank_lines = allow_blank_lines;
	d->node_sorter = node_sorter;
	SurroundedOpts so = {0};
	so.open_token = "{";
	so.close_token = "}";
	if (open && close) {
		so.has_range = true;
		so.range = rng(open->lo, close->hi);
	}
	if (n > 0) {
		so.has_first_member = true;
		so.first_member = nrange(members[0]);
	}
	so.prefer_single_line_when_empty = true;
	so.allow_open_token_trailing_comments = true;
	return gen_surrounded_by_tokens(c, object_like_inner, d, NULL, NULL, so);
}

/* ---- gen_computed_prop_like ---- */

typedef struct {
	GenInnerFn gen;
	void *data;
} ComputedData;

static Items computed_inner(Ctx *c, void *data)
{
	ComputedData *d = data;
	return surround_with_newlines_indented_if_multi_line(d->gen(c, d->data), INDENT_WIDTH);
}

static Items gen_computed_prop_like(Ctx *c, GenInnerFn gen, void *data, Range inner_node_range)
{
	const Tok *open = get_previous_token_if_open_bracket(c, inner_node_range);
	const Tok *close = get_next_token_if_close_bracket(c, inner_node_range);
	ComputedData *d = xalloc(sizeof *d);
	d->gen = gen;
	d->data = data;
	SurroundedOpts so = {0};
	so.open_token = "[";
	so.close_token = "]";
	if (open && close) {
		so.has_range = true;
		so.range = rng(open->lo, close->hi);
	}
	so.has_first_member = true;
	so.first_member = inner_node_range;
	so.prefer_single_line_when_empty = false;
	so.allow_open_token_trailing_comments = true;
	return new_line_group(gen_surrounded_by_tokens(c, computed_inner, d, NULL, NULL, so));
}

/* ---- gen_array_like_nodes ---- */

typedef struct {
	Node **nodes;
	const Tok **seps;
	int n;
	bool force_use_new_lines;
	int trailing_commas;
} ArrayLikeData;

static Items array_like_inner(Ctx *c, void *data)
{
	ArrayLikeData *d = data;
	SepValuesParams o = {0};
	o.nodes = d->nodes;
	o.seps = d->seps;
	o.n = d->n;
	o.force_use_new_lines = d->force_use_new_lines;
	o.allow_blank_lines = true;
	o.separator = d->trailing_commas;
	o.single_line_options = (SingleLineOptions){false, false, items_signal(SIG_SPACE_OR_NEW_LINE)};
	o.multi_line_options = mlo_surround_newlines_indented();
	return gen_separated_values_items(c, o);
}

/* nodes may contain NULL holes (array holes): they become their comma tokens */
static Items gen_array_like_nodes(Ctx *c, Node *node, Node **nodes, int n)
{
	const Tok **seps = NULL;
	bool any_hole = false;
	for (int i = 0; i < n; i++)
		if (!nodes[i])
			any_hole = true;
	if (any_hole) {
		TokList toks = tokens_outside_children(c, node);
		const Tok **commas = xalloc(sizeof(Tok *) * (size_t)(toks.n + 1));
		int nc = 0;
		for (int i = 0; i < toks.n; i++)
			if (toks.v[i]->kind == TK_COMMA)
				commas[nc++] = toks.v[i];
		seps = xalloc(sizeof(Tok *) * (size_t)(n + 1));
		for (int i = 0; i < n; i++)
			if (!nodes[i])
				seps[i] = i < nc ? commas[i] : NULL;
	}
	int trailing_commas;
	if (n > 0 && !nodes[n - 1])
		trailing_commas = SEP_COMMA_ALWAYS;
	else if (n > 0 && nodes[n - 1]->kind == N_REST_PAT)
		trailing_commas = SEP_COMMA_NEVER;
	else
		trailing_commas = SEP_COMMA_ONLY_MULTI_LINE;
	bool force = false;
	if (n > 0) {
		Range *rs = xalloc(sizeof(Range) * (size_t)n);
		for (int i = 0; i < n; i++)
			rs[i] = nodes[i] ? nrange(nodes[i]) : trange(seps[i]);
		force = has_any_node_comment_on_different_line(c, rs, n);
	}
	ArrayLikeData *d = xalloc(sizeof *d);
	d->nodes = nodes;
	d->seps = seps;
	d->n = n;
	d->force_use_new_lines = force;
	d->trailing_commas = trailing_commas;
	SurroundedOpts so = {0};
	so.open_token = "[";
	so.close_token = "]";
	so.has_range = true;
	so.range = nrange(node);
	if (n > 0) {
		so.has_first_member = true;
		so.first_member = nodes[0] ? nrange(nodes[0]) : trange(seps[0]);
	}
	so.prefer_single_line_when_empty = true;
	so.allow_open_token_trailing_comments = true;
	return gen_surrounded_by_tokens(c, array_like_inner, d, NULL, NULL, so);
}

/* ---- gen_block ---- */

typedef Items (*GenMembersFn)(Ctx *c, Node **children, int n, void *data);

typedef struct {
	GenMembersFn gen;
	void *data;
	Node **children;
	int n;
	bool has_range;
	Range range;
	LineNumber before_open_token_ln;
} BlockData;

typedef struct {
	LineAndColumn start, end;
} LcPair;

static int r_new_line_if_different_line(RCtx *rc, void *d) { return ch_is_on_different_line(rc, *(LineNumber *)d); }

static int r_end_newline(RCtx *rc, void *d)
{
	LcPair *p = d;
	return ch_are_line_and_columns_equal(rc, p->start, p->end);
}

static Items block_inner(Ctx *c, void *data)
{
	BlockData *d = data;
	Items items = {0};
	LcPair *p = xalloc(sizeof *p);
	p->start = lc_new();
	p->end = lc_new();
	bool same_line_and_empty =
		d->has_range ? start_line(c, d->range) == end_line(c, d->range) && d->n == 0 : true;
	if (!same_line_and_empty)
		items_push_signal(&items, SIG_NEW_LINE);
	items_push_lc(&items, p->start);
	items_extend(&items, with_indent(d->gen(c, d->children, d->n, d->data)));
	items_push_lc(&items, p->end);
	if (same_line_and_empty) {
		LineNumber *ln = xalloc(sizeof *ln);
		*ln = d->before_open_token_ln;
		items_push_cond(&items, if_true("newLineIfDifferentLine", resolver(r_new_line_if_different_line, ln),
						items_signal(SIG_NEW_LINE)));
	} else {
		items_push_cond(&items, if_false("endNewline", resolver(r_end_newline, p), items_signal(SIG_NEW_LINE)));
	}
	return items;
}

static Items gen_block(Ctx *c, GenMembersFn gen, void *data, const Range *range, Node **children, int n)
{
	Items items = {0};
	BlockData *d = xalloc(sizeof *d);
	d->gen = gen;
	d->data = data;
	d->children = children;
	d->n = n;
	if (range) {
		d->has_range = true;
		d->range = *range;
	}
	d->before_open_token_ln = ln_new();
	items_push_ln(&items, d->before_open_token_ln);
	SurroundedOpts so = {0};
	so.open_token = "{";
	so.close_token = "}";
	so.has_range = d->has_range;
	so.range = d->range;
	if (n > 0) {
		so.has_first_member = true;
		so.first_member = nrange(children[0]);
	}
	so.prefer_single_line_when_empty = false;
	so.allow_open_token_trailing_comments = true;
	items_extend(&items, gen_surrounded_by_tokens(c, block_inner, d, NULL, NULL, so));
	return items;
}

/* ---- gen_members ---- */

typedef bool (*BlankLineFn)(Ctx *c, Node *prev, Node *next);

static bool blank_line_separating(Ctx *c, Node *prev, Node *next)
{
	return has_separating_blank_line(c, nrange(prev), nrange(next));
}

static Items gen_member_or_member_expr_stmt_comments(Ctx *c, Node *node)
{
	Items items = {0};
	CommentList lc = cl_from(c, leading_comments(c, node->lo));
	items_extend(&items, gen_comments_as_statements(c, lc, NULL));
	CommentList tc = get_trailing_comments_same_line(c, nrange(node), cl_from(c, trailing_comments(c, node->hi)));
	if (tc.n > 0) {
		if (lc.n > 0)
			items_push_signal(&items, SIG_NEW_LINE);
		items_extend(&items, gen_comment_collection(c, tc, NULL, NULL));
	}
	return items;
}

typedef struct {
	Range inner_range;
	BlankLineFn should_use_blank_line;
	int separator;
} MembersOpts;

static Items gen_members(Ctx *c, Node **nodes, int n, MembersOpts *o)
{
	Node *last_node = NULL;
	Items items = {0};
	for (int i = 0; i < n; i++) {
		Node *node = nodes[i];
		if (node->kind != N_EMPTY_STMT) {
			if (last_node) {
				items_push_signal(&items, SIG_NEW_LINE);
				if (o->should_use_blank_line(c, last_node, node))
					items_push_signal(&items, SIG_NEW_LINE);
			}
			LineNumber end_ln = ln_new();
			push_end_ln(c, end_ln);
			if (o->separator == SEP_NONE)
				items_extend(&items, gen_node(node, c));
			else
				items_extend(&items, gen_node_with_separator(c, node,
									    get_generated_separator(o->separator, i == n - 1,
												    res_true())));
			items_push_ln(&items, end_ln);
			c->nend_lns--;
			last_node = node;
		} else {
			items_extend(&items, gen_member_or_member_expr_stmt_comments(c, node));
			if (i == n - 1)
				last_node = node;
		}
	}
	if (last_node)
		items_extend(&items, gen_trailing_comments_as_statements(c, nrange(last_node)));
	if (n == 0)
		items_extend(&items,
			     gen_comments_as_statements(c, cl_from(c, leading_comments(c, o->inner_range.hi)), NULL));
	return items;
}

static Items members_block_fn(Ctx *c, Node **children, int n, void *data) { return gen_members(c, children, n, data); }

/* gen_membered_body with bracePosition sameLine */
static Items gen_membered_body(Ctx *c, Node *node, Node **members, int n, BlankLineFn blank, int separator)
{
	Items items = {0};
	TokList toks = tokens_outside_children(c, node);
	const Tok *open = NULL, *close = NULL;
	for (int i = 0; i < toks.n; i++)
		if (toks.v[i]->kind == TK_LBRACE) {
			open = toks.v[i];
			break;
		}
	for (int i = toks.n - 1; i >= 0; i--)
		if (toks.v[i]->kind == TK_RBRACE) {
			close = toks.v[i];
			break;
		}
	if (!open || !close)
		fail("dprint: expected braces");
	items_extend(&items, gen_brace_separator());
	MembersOpts *o = xalloc(sizeof *o);
	o->inner_range = rng(open->hi, close->lo);
	o->should_use_blank_line = blank;
	o->separator = separator;
	Range r = rng(open->lo, close->hi);
	items_extend(&items, gen_block(c, members_block_fn, o, &r, members, n));
	return items;
}

/* ---- gen_statements ---- */

enum { GROUP_IMPORTS, GROUP_EXPORTS, GROUP_OTHER };

/* sorting/mod.rs + module_specifiers.rs (caseSensitive for import declarations) */
static int cmp_text_case_sensitive(Str a, Str b)
{
	size_t n = a.n < b.n ? a.n : b.n;
	int r = memcmp(a.p, b.p, n);
	if (r)
		return r < 0 ? -1 : 1;
	return a.n < b.n ? -1 : a.n > b.n ? 1 : 0;
}

/* a.to_lowercase().cmp(b.to_lowercase()), then case-sensitive; non-ASCII characters are
 * compared unchanged (approximation of str::to_lowercase) */
static int cmp_text_case_insensitive(Str a, Str b)
{
	size_t n = a.n < b.n ? a.n : b.n;
	for (size_t i = 0; i < n; i++) {
		int x = tolower((unsigned char)a.p[i]), y = tolower((unsigned char)b.p[i]);
		if (x != y)
			return x < y ? -1 : 1;
	}
	if (a.n != b.n)
		return a.n < b.n ? -1 : 1;
	return cmp_text_case_sensitive(a, b);
}

static int cmp_module_specifiers(Str a, Str b, int (*cmp)(Str, Str))
{
	Str an = {a.p + 1, a.n - 2}, bn = {b.p + 1, b.n - 2};
	bool a_rel = false, b_rel = false;
	size_t a_count = 0, b_count = 0;
	Str a_folder = an, b_folder = bn;
	for (int k = 0; k < 2; k++) {
		Str t = k == 0 ? an : bn;
		const char *slash = memchr(t.p, '/', t.n);
		size_t first_len = slash ? (size_t)(slash - t.p) : t.n;
		bool rel = (first_len == 1 && t.p[0] == '.') || (first_len == 2 && t.p[0] == '.' && t.p[1] == '.');
		size_t count = 0, start_index = 0;
		if (rel) {
			size_t pos = 0;
			for (;;) {
				const char *s2 = memchr(t.p + pos, '/', t.n - pos);
				size_t end = s2 ? (size_t)(s2 - t.p) : t.n;
				Str part = {t.p + pos, end - pos};
				if (part.n == 2 && part.p[0] == '.' && part.p[1] == '.')
					count++;
				else if (!(part.n == 1 && part.p[0] == '.'))
					break;
				start_index += part.n + (s2 ? 1 : 0);
				if (!s2)
					break;
				pos = end + 1;
			}
		}
		if (k == 0) {
			a_rel = rel;
			a_count = count;
			a_folder = (Str){t.p + start_index, t.n - start_index};
		} else {
			b_rel = rel;
			b_count = count;
			b_folder = (Str){t.p + start_index, t.n - start_index};
		}
	}
	if (!a_rel)
		return !b_rel ? cmp(an, bn) : -1;
	if (!b_rel)
		return 1;
	if (a_count > b_count)
		return -1;
	if (a_count < b_count)
		return 1;
	return cmp(a_folder, b_folder);
}

static Node *import_export_src(Node *n)
{
	if (n->kind == N_IMPORT_DECL || n->kind == N_NAMED_EXPORT || n->kind == N_EXPORT_ALL)
		return n->a;
	return NULL;
}

static int sort_imports_case_sensitive(Ctx *c, int ai, Node *a, int bi, Node *b)
{
	int r;
	/* cmp_nodes with get_comparison_nodes: the module specifier */
	Node *as = import_export_src(a), *bs = import_export_src(b);
	if (as && bs)
		r = cmp_module_specifiers(ntext(c, as), ntext(c, bs), cmp_text_case_sensitive);
	else if (as)
		r = 1;
	else if (bs)
		r = -1;
	else
		r = 0;
	if (r == 0)
		return ai < bi ? -1 : ai > bi ? 1 : 0;
	return r;
}

typedef struct {
	int kind;
	Node **nodes;
	int n, cap;
} StmtGroup;

static void sg_push(StmtGroup *g, Node *n)
{
	if (g->n == g->cap) {
		int cap = g->cap ? g->cap * 2 : 8;
		g->nodes = xrealloc(g->nodes, sizeof(Node *) * (size_t)g->cap, sizeof(Node *) * (size_t)cap);
		g->cap = cap;
	}
	g->nodes[g->n++] = n;
}

static Items gen_statements(Ctx *c, Range inner_range, Node **stmts, int nstmts)
{
	/* get_stmt_groups */
	StmtGroup *groups = xalloc(sizeof(StmtGroup) * (size_t)(nstmts + 1));
	int ngroups = 0;
	int previous_last_end_line = -1;
	for (int i = 0; i < nstmts; i++) {
		Node *stmt = stmts[i];
		int last_end_line = previous_last_end_line;
		previous_last_end_line = -1;
		int kind = GROUP_OTHER;
		if (stmt->kind == N_IMPORT_DECL && stmt->list.n > 0)
			kind = GROUP_IMPORTS;
		else if (stmt->kind == N_EXPORT_ALL || (stmt->kind == N_NAMED_EXPORT && stmt->a))
			kind = GROUP_EXPORTS;
		if (kind != GROUP_OTHER)
			previous_last_end_line = end_line(c, nrange(stmt));
		if (ngroups > 0) {
			StmtGroup *g = &groups[ngroups - 1];
			bool same = g->kind == kind &&
				    (kind == GROUP_OTHER || last_end_line < 0 || last_end_line + 1 >= start_line(c, nrange(stmt)));
			if (same) {
				sg_push(g, stmt);
				continue;
			}
		}
		groups[ngroups].kind = kind;
		sg_push(&groups[ngroups], stmt);
		ngroups++;
	}

	Items items = {0};
	Range last;
	bool has_last = false;
	for (int gi = 0; gi < ngroups; gi++) {
		StmtGroup *g = &groups[gi];
		if (g->kind == GROUP_IMPORTS || g->kind == GROUP_EXPORTS) {
			CommentList comments = get_leading_comments_on_previous_lines(c, prange(g->nodes[0]->lo));
			const Comment *last_comment = NULL;
			for (int i = 0; i < comments.n; i++)
				if (!has_handled_comment(c, comments.v[i]))
					last_comment = comments.v[i];
			Range lc_range = last_comment ? rng(last_comment->lo, last_comment->hi) : last;
			items_extend(&items, gen_comments_as_statements(c, comments, has_last ? &last : NULL));
			if (last_comment) {
				last = lc_range;
				has_last = true;
			}
		}
		int n = g->n;
		Items *generated = xalloc(sizeof(Items) * (size_t)(n + 1));
		Items *separators = xalloc(sizeof(Items) * (size_t)(n + 1));
		bool *has_sep = xalloc(sizeof(bool) * (size_t)(n + 1));
		int *sorted = g->kind == GROUP_IMPORTS ? get_sorted_indexes(c, g->nodes, n, sort_imports_case_sensitive) : NULL;
		for (int i = 0; i < n; i++) {
			Node *node = g->nodes[i];
			if (node->kind != N_EMPTY_STMT) {
				if (has_last) {
					Items sep = {0};
					items_push_signal(&sep, SIG_NEW_LINE);
					if (has_separating_blank_line(c, last, nrange(node)))
						items_push_signal(&sep, SIG_NEW_LINE);
					separators[i] = sep;
					has_sep[i] = true;
				}
				Items it = {0};
				LineNumber end_ln = ln_new();
				push_end_ln(c, end_ln);
				items_extend(&it, gen_node(node, c));
				items_push_ln(&it, end_ln);
				generated[i] = it;
				c->nend_lns--;
				last = nrange(node);
				has_last = true;
			} else {
				Items it = {0};
				CommentList lc = cl_from(c, leading_comments(c, node->lo));
				items_extend(&it, gen_comments_as_statements(c, lc, NULL));
				CommentList tc = get_trailing_comments_same_line(c, nrange(node),
										 cl_from(c, trailing_comments(c, node->hi)));
				if (tc.n > 0) {
					if (lc.n > 0)
						items_push_signal(&it, SIG_NEW_LINE);
					items_extend(&it, gen_comment_collection(c, tc, NULL, NULL));
				}
				generated[i] = it;
				if (gi == ngroups - 1 && i == n - 1) {
					last = nrange(node);
					has_last = true;
				}
			}
		}
		Items *ordered = generated;
		if (sorted) {
			ordered = xalloc(sizeof(Items) * (size_t)(n + 1));
			for (int i = 0; i < n; i++)
				ordered[sorted[i]] = generated[i];
		}
		for (int i = 0; i < n; i++) {
			if (has_sep[i])
				items_extend(&items, separators[i]);
			items_extend(&items, ordered[i]);
		}
	}
	if (has_last)
		items_extend(&items, gen_trailing_comments_as_statements(c, last));
	if (ngroups == 0)
		items_extend(&items, gen_comments_as_statements(c, cl_from(c, leading_comments(c, inner_range.hi)), NULL));
	return items;
}

/* ---- control flow ---- */

typedef struct {
	bool has_prev_start_ln;
	LineNumber prev_start_ln;
	bool has_prev_close_brace_ref;
	ConditionReference prev_close_brace_ref;
} CtrlSepData;

static int r_new_line_or_space(RCtx *rc, void *d)
{
	CtrlSepData *x = d;
	if (x->has_prev_start_ln) {
		int same = ch_is_on_same_line(rc, x->prev_start_ln);
		if (same < 0)
			return -1;
		if (same)
			return 1;
	}
	if (x->has_prev_close_brace_ref) {
		int v = rctx_condition(rc, x->prev_close_brace_ref);
		if (v < 0)
			return -1;
		if (!v)
			return 1;
	}
	return 0;
}

/* gen_control_flow_separator with nextControlFlowPosition sameLine */
static Items gen_control_flow_separator(const LineNumber *previous_start_ln, const ConditionReference *previous_close_brace_ref)
{
	CtrlSepData *x = xalloc(sizeof *x);
	if (previous_start_ln) {
		x->has_prev_start_ln = true;
		x->prev_start_ln = *previous_start_ln;
	}
	if (previous_close_brace_ref) {
		x->has_prev_close_brace_ref = true;
		x->prev_close_brace_ref = *previous_close_brace_ref;
	}
	return items_cond(if_true_or("newLineOrSpace", resolver(r_new_line_or_space, x), items_signal(SIG_NEW_LINE), items_str(" ")));
}

typedef struct {
	Items generated_node;
	ConditionReference open_brace_condition_ref, close_brace_condition_ref;
} BraceBodyResult;

typedef struct {
	LineAndColumn start_lc;
	LineNumber end_ln;
	bool has_end_header_ln;
	LineNumber end_header_ln;
	LineAndColumn start_inner_text_lc, start_statements_lc, end_statements_lc;
	bool is_body_empty_stmt, should_use_new_line;
	ConditionReference inner_brace_space_ref, newline_ref, open_brace_ref;
} BraceBodyData;

static int r_space_condition(RCtx *rc, void *d)
{
	BraceBodyData *x = d;
	if (x->is_body_empty_stmt)
		return 0;
	uint32_t sl, sc, el, ec;
	if (!rctx_lc(rc, x->start_inner_text_lc, &sl, &sc))
		return -1;
	if (!rctx_lc(rc, x->end_statements_lc, &el, &ec))
		return -1;
	if (sl < el)
		return 0;
	return sc < ec;
}

static int r_newline_condition(RCtx *rc, void *d)
{
	BraceBodyData *x = d;
	if (x->is_body_empty_stmt)
		return 0;
	if (x->should_use_new_line)
		return 1;
	if (!x->has_end_header_ln)
		return -1;
	uint32_t ehl, esl;
	if (!rctx_ln(rc, x->end_header_ln, &ehl))
		return -1;
	if (ehl < rc->writer_info.line_number)
		return 1;
	if (!rctx_ln(rc, x->end_statements_lc.line, &esl))
		return -1;
	return esl > ehl;
}

static int r_open_brace(RCtx *rc, void *d)
{
	BraceBodyData *x = d;
	if (x->is_body_empty_stmt)
		return 0;
	return 1; /* useBraces: always */
}

static int r_close_brace(RCtx *rc, void *d) { return rctx_condition(rc, ((BraceBodyData *)d)->open_brace_ref); }

static int r_close_brace_new_line(RCtx *rc, void *d)
{
	BraceBodyData *x = d;
	int is_new_line = rctx_condition(rc, x->newline_ref);
	if (is_new_line < 0)
		return -1;
	if (!is_new_line)
		return 0;
	int eq = ch_are_line_and_columns_equal(rc, x->start_statements_lc, x->end_statements_lc);
	if (eq < 0)
		return -1;
	return !eq;
}

static int r_close_brace_space(RCtx *rc, void *d)
{
	BraceBodyData *x = d;
	int same = ch_is_at_same_position(rc, x->start_inner_text_lc);
	if (same < 0)
		return -1;
	if (same)
		return 0;
	return rctx_condition(rc, x->inner_brace_space_ref);
}

static int r_space_if_at_start(RCtx *rc, void *d) { return ch_is_at_same_position(rc, ((BraceBodyData *)d)->start_lc); }

/* gen_conditional_brace_body with useBraces always, bracePosition sameLine and
 * singleBodyPosition nextLine (for statements that have it) */
static BraceBodyResult gen_conditional_brace_body(Ctx *c, Node *body, bool has_single_body_position,
						  const LineNumber *end_header_ln)
{
	BraceBodyData *x = xalloc(sizeof *x);
	x->start_lc = lc_new();
	x->end_ln = ln_new();
	if (end_header_ln) {
		x->has_end_header_ln = true;
		x->end_header_ln = *end_header_ln;
	}
	x->start_inner_text_lc = lc_new();
	x->start_statements_lc = lc_new();
	x->end_statements_lc = lc_new();

	/* get_header_trailing_comments */
	CommentList header_trailing = {0};
	if (body->kind == N_BLOCK_STMT) {
		Comments lc = leading_comments(c, body->lo);
		const Comment *line_comment = NULL;
		CM_FOR(lc, c->src, cm)
		{
			if (!cm->block && !line_comment)
				line_comment = cm;
		}
		if (line_comment) {
			cl_push(&header_trailing, line_comment);
		} else {
			const Tok *ob = get_first_open_brace_token_within(c, nrange(body));
			int body_start_line = start_line(c, nrange(body));
			Comments tc = trailing_comments(c, ob->hi);
			CM_FOR(tc, c->src, cm)
			{
				if (cstart_line(c, cm) == body_start_line && !cm->block)
					cl_push(&header_trailing, cm);
				else
					break;
			}
		}
	} else {
		Comments lc = leading_comments(c, body->lo);
		uint32_t last_header_end = get_previous_token_end_before(c, nrange(body));
		int last_header_line = line_of(c, last_header_end);
		CM_FOR(lc, c->src, cm)
		{
			if (cstart_line(c, cm) <= last_header_line && !cm->block)
				cl_push(&header_trailing, cm);
			else
				break;
		}
	}
	/* get_body_should_be_multi_line */
	bool body_should_be_multi_line;
	if (body->kind == N_BLOCK_STMT) {
		if (body->list.n == 1 && !has_leading_comment_on_different_line(c, nrange(body->list.v[0]), header_trailing.v,
										 header_trailing.n))
			body_should_be_multi_line = false;
		else if (body->list.n == 0 && start_line(c, nrange(body)) == end_line(c, nrange(body)))
			body_should_be_multi_line = false;
		else
			body_should_be_multi_line = true;
	} else {
		body_should_be_multi_line =
			has_leading_comment_on_different_line(c, nrange(body), header_trailing.v, header_trailing.n);
	}
	/* get_should_use_new_line */
	bool should_use_new_line;
	if (body_should_be_multi_line)
		should_use_new_line = true;
	else if (has_single_body_position)
		should_use_new_line = true; /* nextLine */
	else if (body->kind == N_BLOCK_STMT && body->list.n == 0)
		should_use_new_line = start_line(c, nrange(body)) < end_line(c, nrange(body));
	else
		should_use_new_line = true;
	x->should_use_new_line = should_use_new_line;
	x->is_body_empty_stmt = body->kind == N_EMPTY_STMT;

	Cond *inner_space = if_true("spaceCondition", resolver(r_space_condition, x), items_signal(SIG_SPACE_OR_NEW_LINE));
	x->inner_brace_space_ref = cond_create_reference(inner_space);
	Cond *newline = if_true("newLineCondition", resolver(r_newline_condition, x), items_signal(SIG_NEW_LINE));
	x->newline_ref = cond_create_reference(newline);
	Items open_items = {0};
	items_extend(&open_items, gen_brace_separator());
	items_push_str(&open_items, "{");
	items_push_cond(&open_items, inner_space);
	Cond *open_brace = if_true("openBrace", resolver(r_open_brace, x), open_items);
	ConditionReevaluation open_reeval = cond_create_reevaluation(open_brace);
	x->open_brace_ref = cond_create_reference(open_brace);

	Items items = {0};
	items_push_lc(&items, x->start_lc);
	items_push_anchor(&items, x->end_ln);
	items_push_cond(&items, open_brace);
	items_push_lc(&items, x->start_inner_text_lc);
	Items gc = gen_comment_collection(c, header_trailing, NULL, NULL);
	if (!items_empty(gc)) {
		items_push_signal(&items, SIG_START_FORCE_NO_NEW_LINES);
		items_push_str(&items, " ");
		items_extend(&items, gc);
		items_push_signal(&items, SIG_FINISH_FORCE_NO_NEW_LINES);
	}
	items_push_cond(&items, newline);
	items_push_lc(&items, x->start_statements_lc);
	if (!x->is_body_empty_stmt)
		items_push_cond(&items, if_true("spaceIfAtStart", resolver(r_space_if_at_start, x),
						items_signal(SIG_SPACE_OR_NEW_LINE)));
	if (body->kind == N_BLOCK_STMT) {
		Items inner = {0};
		items_extend(&inner, gen_leading_comments(c, nrange(body)));
		Range ir = rng(body->lo + 1, body->hi - 1);
		if (body->list.n == 0) {
			items_extend(&inner, gen_comments_same_line(c, get_trailing_comments_on_same_line(c, prange(ir.lo))));
			items_extend(&inner, gen_comments_as_statements(c, cl_from(c, trailing_comments(c, ir.lo)), NULL));
		} else {
			items_extend(&inner, gen_statements(c, ir, body->list.v, body->list.n));
		}
		items_extend(&items, with_indent(inner));
	} else {
		Items inner = {0};
		items_extend(&inner, gen_node(body, c));
		items_extend(&inner, gen_trailing_comments(c, nrange(body)));
		items_extend(&items, with_indent(inner));
	}
	items_push_lc(&items, x->end_statements_lc);
	Items close_items = {0};
	items_push_cond(&close_items,
			if_true_or("closeBraceNewLine", resolver(r_close_brace_new_line, x), items_signal(SIG_NEW_LINE),
				   items_cond(if_true("closeBraceSpace", resolver(r_close_brace_space, x), items_str(" ")))));
	items_push_str(&close_items, "}");
	Cond *close_brace = if_true("closeBrace", resolver(r_close_brace, x), close_items);
	BraceBodyResult r;
	r.close_brace_condition_ref = cond_create_reference(close_brace);
	items_push_cond(&items, close_brace);
	items_push_ln(&items, x->end_ln);
	items_push_reevaluation(&items, open_reeval);
	r.generated_node = items;
	r.open_brace_condition_ref = x->open_brace_ref;
	return r;
}

static BraceBodyResult gen_header_with_conditional_brace_body(Ctx *c, Node *body, Items generated_header,
							      bool has_single_body_position)
{
	LineNumber start_header_ln = ln_new();
	LineStartIndentLevel start_header_lsil = lsil_new();
	LineNumber end_header_ln = ln_new();
	Items items = {0};
	items_push_ln(&items, start_header_ln);
	items_push_anchor(&items, end_header_ln);
	items_push_lsil(&items, start_header_lsil);
	items_extend(&items, new_line_group(generated_header));
	items_push_ln(&items, end_header_ln);
	BraceBodyResult r = gen_conditional_brace_body(c, body, has_single_body_position, &end_header_ln);
	items_extend(&items, r.generated_node);
	r.generated_node = items;
	return r;
}
