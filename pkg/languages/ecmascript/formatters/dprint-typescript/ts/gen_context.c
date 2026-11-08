/* Port of dprint-plugin-typescript 0.87.1 src/generation/{context.rs,comments.rs,tokens.rs,
 * node_helpers.rs,generate_types.rs} and src/utils/string_utils.rs (the parts used for
 * TypeScript without JSX).
 *
 * dprint-plugin-typescript: Copyright (c) 2019-2022 David Sherret, MIT License. */

typedef struct {
	uint32_t lo, hi;
} Range;

DP Range rng(uint32_t lo, uint32_t hi) { return (Range){lo, hi}; }
DP Range nrange(const Node *n) { return (Range){n->lo, n->hi}; }
DP Range trange(const Tok *t) { return (Range){t->lo, t->hi}; }
DP Range prange(uint32_t pos) { return (Range){pos, pos}; }

typedef struct {
	uint32_t lo, hi;
	uint32_t id; /* info id */
	bool used;
} StoredInfo;

/* Context's FxHashMap<SourceRange, Info>: open addressing on (lo, hi) */
typedef struct {
	StoredInfo *v;
	int n, cap; /* cap: 0 or a power of two */
} StoredInfos;

typedef struct {
	Range range;
	LineNumber ln;
	IsStartOfLine isol;
} BeforeCommentsInfo;

typedef struct Ctx {
	Source *src;
	Node *current_node;
	Node **parents;
	int nparents, cparents;
	int token_index; /* CommentTracker */
	StoredInfos stored_ln_ranges_start, stored_ln_ranges_end, stored_lsil, stored_ln, stored_il;
	LineNumber *end_lns;
	int nend_lns, cend_lns;
	BeforeCommentsInfo *before_comments;
	int nbefore, cbefore;
	bool has_if_stmt_last_brace_ref;
	ConditionReference if_stmt_last_brace_ref;
} Ctx;

#define INDENT_WIDTH 2
#define LINE_WIDTH 120

/* ---- source helpers (dprint-swc-ext SourceRanged) ---- */

DP int line_of(Ctx *c, uint32_t pos) { return src_line(c->src, pos); }
DP int start_line(Ctx *c, Range r) { return line_of(c, r.lo); }
DP int end_line(Ctx *c, Range r) { return line_of(c, r.hi); }
DP int cstart_line(Ctx *c, const Comment *cm) { return line_of(c, cm->lo); }
DP int cend_line(Ctx *c, const Comment *cm) { return line_of(c, cm->hi); }
DP Str text_of(Ctx *c, Range r) { return (Str){c->src->text + r.lo, r.hi - r.lo}; }
DP Str ntext(Ctx *c, const Node *n) { return text_of(c, nrange(n)); }
DP bool text_eq(Ctx *c, Range r, const char *s) { return str_eqc(text_of(c, r), s); }
DP bool tok_text_eq(Ctx *c, const Tok *t, const char *s) { return text_eq(c, trange(t), s); }

/* comment text without the delimiters (swc Comment::text) */
DP Str comment_text(Ctx *c, const Comment *cm)
{
	if (cm->block)
		return (Str){c->src->text + cm->lo + 2, cm->hi - cm->lo - 4};
	return (Str){c->src->text + cm->lo + 2, cm->hi - cm->lo - 2};
}

DP Comments leading_comments(Ctx *c, uint32_t pos) { return src_leading_comments(c->src, pos); }
DP Comments trailing_comments(Ctx *c, uint32_t pos) { return src_trailing_comments(c->src, pos); }

DP const Tok *previous_token(Ctx *c, Range r) { return src_previous_token(c->src, r.lo); }
DP const Tok *next_token(Ctx *c, Range r) { return src_next_token(c->src, r.hi); }

/* ---- context.rs ---- */

DP Node *ctx_parent(Ctx *c) { return c->nparents ? c->parents[c->nparents - 1] : NULL; }

DP void ctx_push_parent(Ctx *c, Node *n)
{
	if (c->nparents == c->cparents) {
		int cap = c->cparents ? c->cparents * 2 : 64;
		c->parents = xrealloc(c->parents, sizeof(Node *) * (size_t)c->cparents, sizeof(Node *) * (size_t)cap);
		c->cparents = cap;
	}
	c->parents[c->nparents++] = n;
}

DP bool has_handled_comment(Ctx *c, const Comment *cm) { return cm->handled; }
DP void mark_comment_handled(Ctx *c, Comment *cm) { cm->handled = true; }

static uint32_t stored_hash(Range r) { return (r.lo * 2654435761u) ^ (r.hi * 2246822519u); }

static StoredInfo *stored_slot(const StoredInfos *s, Range r)
{
	uint32_t mask = (uint32_t)s->cap - 1;
	for (uint32_t i = stored_hash(r) & mask;; i = (i + 1) & mask) {
		StoredInfo *e = &s->v[i];
		if (!e->used || (e->lo == r.lo && e->hi == r.hi))
			return e;
	}
}

DP void stored_put(StoredInfos *s, Range r, uint32_t id)
{
	if ((s->n + 1) * 2 > s->cap) {
		StoredInfos g = {xalloc(sizeof(StoredInfo) * (size_t)(s->cap ? s->cap * 2 : 64)), 0, s->cap ? s->cap * 2 : 64};
		for (int i = 0; i < s->cap; i++)
			if (s->v[i].used) {
				*stored_slot(&g, (Range){s->v[i].lo, s->v[i].hi}) = s->v[i];
				g.n++;
			}
		*s = g;
	}
	StoredInfo *e = stored_slot(s, r);
	if (!e->used)
		s->n++;
	*e = (StoredInfo){r.lo, r.hi, id, true};
}

DP bool stored_get(const StoredInfos *s, Range r, uint32_t *id)
{
	if (!s->cap)
		return false;
	StoredInfo *e = stored_slot(s, r);
	if (!e->used)
		return false;
	*id = e->id;
	return true;
}

DP void store_ln_range_for_node(Ctx *c, Range r, LineNumber a, LineNumber b)
{
	stored_put(&c->stored_ln_ranges_start, r, a.id);
	stored_put(&c->stored_ln_ranges_end, r, b.id);
}

DP bool get_ln_range_for_node(Ctx *c, Range r, LineNumber *a, LineNumber *b)
{
	return stored_get(&c->stored_ln_ranges_start, r, &a->id) && stored_get(&c->stored_ln_ranges_end, r, &b->id);
}

DP void store_lsil_for_node(Ctx *c, Range r, LineStartIndentLevel x) { stored_put(&c->stored_lsil, r, x.id); }
DP bool get_lsil_for_node(Ctx *c, Range r, LineStartIndentLevel *x) { return stored_get(&c->stored_lsil, r, &x->id); }
DP void store_ln_for_node(Ctx *c, Range r, LineNumber x) { stored_put(&c->stored_ln, r, x.id); }
DP bool get_ln_for_node(Ctx *c, Range r, LineNumber *x) { return stored_get(&c->stored_ln, r, &x->id); }
DP void store_il_for_node(Ctx *c, Range r, IndentLevel x) { stored_put(&c->stored_il, r, x.id); }
DP bool get_il_for_node(Ctx *c, Range r, IndentLevel *x) { return stored_get(&c->stored_il, r, &x->id); }

DP void push_end_ln(Ctx *c, LineNumber ln)
{
	if (c->nend_lns == c->cend_lns) {
		int cap = c->cend_lns ? c->cend_lns * 2 : 32;
		c->end_lns = xrealloc(c->end_lns, sizeof(LineNumber) * (size_t)c->cend_lns, sizeof(LineNumber) * (size_t)cap);
		c->cend_lns = cap;
	}
	c->end_lns[c->nend_lns++] = ln;
}

DP void get_or_create_current_before_comments_start_info(Ctx *c, LineNumber *ln, IsStartOfLine *isol)
{
	Range cur = nrange(c->current_node);
	if (c->nbefore > 0) {
		BeforeCommentsInfo *b = &c->before_comments[c->nbefore - 1];
		if (b->range.lo == cur.lo && b->range.hi == cur.hi) {
			*ln = b->ln;
			*isol = b->isol;
			return;
		}
	}
	if (c->nbefore == c->cbefore) {
		int cap = c->cbefore ? c->cbefore * 2 : 16;
		c->before_comments = xrealloc(c->before_comments, sizeof(BeforeCommentsInfo) * (size_t)c->cbefore,
					      sizeof(BeforeCommentsInfo) * (size_t)cap);
		c->cbefore = cap;
	}
	BeforeCommentsInfo b = {cur, ln_new(), isol_new()};
	c->before_comments[c->nbefore++] = b;
	*ln = b.ln;
	*isol = b.isol;
}

DP bool take_current_before_comments_start_info(Ctx *c, LineNumber *ln, IsStartOfLine *isol)
{
	if (c->nbefore == 0)
		return false;
	BeforeCommentsInfo *b = &c->before_comments[c->nbefore - 1];
	Range cur = nrange(c->current_node);
	if (b->range.lo != cur.lo || b->range.hi != cur.hi)
		return false;
	*ln = b->ln;
	*isol = b->isol;
	c->nbefore--;
	return true;
}

/* ---- comments.rs: CommentTracker ---- */

DP Comments leading_comments_with_previous(Ctx *c, uint32_t pos)
{
	Source *s = c->src;
	Comments it = {0};
	if (c->token_index == 0) {
		cm_extend(&it, src_leading_comments(s, 0));
		cm_extend(&it, src_trailing_comments(s, 0));
	} else if (c->token_index - 1 < s->ntoks) {
		cm_extend(&it, src_trailing_comments(s, s->toks[c->token_index - 1].hi));
	}
	while (c->token_index < s->ntoks) {
		const Tok *t = &s->toks[c->token_index];
		cm_extend(&it, src_leading_comments(s, t->lo));
		if (t->hi < pos) {
			cm_extend(&it, src_trailing_comments(s, t->hi));
			c->token_index++;
		} else {
			break;
		}
	}
	return it;
}

DP Comments trailing_comments_with_previous(Ctx *c, uint32_t end)
{
	Source *s = c->src;
	Comments it = {0};
	while (c->token_index < s->ntoks) {
		const Tok *t = &s->toks[c->token_index];
		cm_extend(&it, src_leading_comments(s, t->lo));
		bool is_comma = t->kind == TK_COMMA;
		if (!is_comma && t->lo >= end)
			break;
		if (is_comma || t->hi <= end) {
			cm_extend(&it, src_trailing_comments(s, t->hi));
			c->token_index++;
		} else {
			break;
		}
	}
	return it;
}

/* ---- tokens.rs: TokenFinder ---- */

DP const Tok *get_previous_token_if(Ctx *c, Range r, int kind)
{
	const Tok *t = previous_token(c, r);
	return t && t->kind == kind ? t : NULL;
}

DP const Tok *get_next_token_if(Ctx *c, Range r, int kind)
{
	const Tok *t = next_token(c, r);
	return t && t->kind == kind ? t : NULL;
}

DP const Tok *get_previous_token_if_open_paren(Ctx *c, Range r) { return get_previous_token_if(c, r, TK_LPAREN); }
DP const Tok *get_next_token_if_close_paren(Ctx *c, Range r) { return get_next_token_if(c, r, TK_RPAREN); }
DP const Tok *get_previous_token_if_open_brace(Ctx *c, Range r) { return get_previous_token_if(c, r, TK_LBRACE); }
DP const Tok *get_previous_token_if_open_bracket(Ctx *c, Range r) { return get_previous_token_if(c, r, TK_LBRACKET); }
DP const Tok *get_previous_token_if_colon(Ctx *c, Range r) { return get_previous_token_if(c, r, TK_COLON); }
DP const Tok *get_next_token_if_comma(Ctx *c, Range r) { return get_next_token_if(c, r, TK_COMMA); }
DP const Tok *get_next_token_if_close_bracket(Ctx *c, Range r) { return get_next_token_if(c, r, TK_RBRACKET); }

DP const Tok *get_previous_token_if_text(Ctx *c, Range r, const char *text)
{
	const Tok *t = previous_token(c, r);
	return t && tok_text_eq(c, t, text) ? t : NULL;
}

/* first token after the node (next_tokens_fast) matching kind or text */
DP const Tok *get_first_token_after(Ctx *c, Range r, int kind, const char *text)
{
	Source *s = c->src;
	for (int i = src_token_index_after(s, r.hi); i < s->ntoks; i++) {
		const Tok *t = &s->toks[i];
		if (text ? tok_text_eq(c, t, text) : t->kind == kind)
			return t;
	}
	return NULL;
}

DP const Tok *get_first_token_before(Ctx *c, Range r, int kind)
{
	Source *s = c->src;
	for (int i = src_token_index_before(s, r.lo) - 1; i >= 0; i--)
		if (s->toks[i].kind == kind)
			return &s->toks[i];
	return NULL;
}

DP const Tok *get_first_token_within(Ctx *c, Range r, int kind)
{
	int first, last;
	src_tokens_in_range(c->src, r.lo, r.hi, &first, &last);
	for (int i = first; i < last; i++)
		if (c->src->toks[i].kind == kind)
			return &c->src->toks[i];
	return NULL;
}

DP const Tok *get_last_token_within_if(Ctx *c, Range r, int kind)
{
	int first, last;
	src_tokens_in_range(c->src, r.lo, r.hi, &first, &last);
	if (last > first && c->src->toks[last - 1].kind == kind)
		return &c->src->toks[last - 1];
	return NULL;
}

DP const Tok *get_first_open_brace_token_within(Ctx *c, Range r) { return get_first_token_within(c, r, TK_LBRACE); }
DP const Tok *get_last_token_within_if_comma(Ctx *c, Range r) { return get_last_token_within_if(c, r, TK_COMMA); }
DP const Tok *get_first_semi_colon_after(Ctx *c, Range r) { return get_first_token_after(c, r, TK_SEMI, NULL); }
DP const Tok *get_first_colon_token_after(Ctx *c, Range r) { return get_first_token_after(c, r, TK_COLON, NULL); }
DP const Tok *get_first_colon_token_within(Ctx *c, Range r) { return get_first_token_within(c, r, TK_COLON); }
DP const Tok *get_first_operator_after(Ctx *c, Range r, const char *op) { return get_first_token_after(c, r, 0, op); }
DP const Tok *get_first_keyword_after(Ctx *c, Range r, const char *kw) { return get_first_token_after(c, r, 0, kw); }
DP const Tok *get_first_open_paren_before(Ctx *c, Range r) { return get_first_token_before(c, r, TK_LPAREN); }
DP const Tok *get_first_close_paren_before(Ctx *c, Range r) { return get_first_token_before(c, r, TK_RPAREN); }
DP const Tok *get_first_close_paren_after(Ctx *c, Range r) { return get_first_token_after(c, r, TK_RPAREN, NULL); }

DP uint32_t get_previous_token_end_before(Ctx *c, Range r)
{
	const Tok *t = previous_token(c, r);
	return t ? t->hi : 0;
}

DP uint32_t get_next_token_pos_after(Ctx *c, Range r)
{
	const Tok *t = next_token(c, r);
	return t ? t->lo : c->src->module->hi;
}

/* ---- node children (dprint-swc-ext view children, in source order) ---- */

typedef struct {
	Node **v;
	int n;
} Children;

DP int cmp_node_lo(const void *a, const void *b)
{
	const Node *x = *(Node *const *)a, *y = *(Node *const *)b;
	return x->lo < y->lo ? -1 : x->lo > y->lo ? 1 : 0;
}

DP Children node_children(Node *n)
{
	int cap = 4 + n->list.n + n->list2.n + n->list3.n;
	Children ch = {xalloc(sizeof(Node *) * (size_t)cap), 0};
	Node *slots[4] = {n->a, n->b, n->c, n->d};
	for (int i = 0; i < 4; i++)
		if (slots[i])
			ch.v[ch.n++] = slots[i];
	const NodeList *lists[3] = {&n->list, &n->list2, &n->list3};
	for (int l = 0; l < 3; l++)
		for (int i = 0; i < lists[l]->n; i++)
			if (lists[l]->v[i])
				ch.v[ch.n++] = lists[l]->v[i];
	/* stable insertion sort by start (qsort is not stable) */
	for (int i = 1; i < ch.n; i++) {
		Node *x = ch.v[i];
		int j = i - 1;
		while (j >= 0 && ch.v[j]->lo > x->lo) {
			ch.v[j + 1] = ch.v[j];
			j--;
		}
		ch.v[j + 1] = x;
	}
	return ch;
}

/* get_tokens_from_children_with_tokens: the tokens of the node outside its children */
typedef struct {
	const Tok **v;
	int n;
} TokList;

DP TokList tokens_outside_children(Ctx *c, Node *n)
{
	Children ch = node_children(n);
	int first, last;
	src_tokens_in_range(c->src, n->lo, n->hi, &first, &last);
	TokList r = {xalloc(sizeof(Tok *) * (size_t)(last - first + 1)), 0};
	int ti = first;
	for (int k = 0; k < ch.n; k++) {
		Range cr = nrange(ch.v[k]);
		while (ti < last && c->src->toks[ti].lo < cr.lo)
			r.v[r.n++] = &c->src->toks[ti++];
		while (ti < last && c->src->toks[ti].hi <= cr.hi)
			ti++;
	}
	while (ti < last)
		r.v[r.n++] = &c->src->toks[ti++];
	return r;
}

/* ---- node_helpers.rs ---- */

DP bool is_first_node_on_line(Ctx *c, Range r)
{
	const char *s = c->src->text;
	for (uint32_t i = r.lo; i > 0; i--) {
		char ch = s[i - 1];
		if (ch != ' ' && ch != '\t')
			return ch == '\n';
	}
	return true;
}

DP bool has_separating_blank_line(Ctx *c, Range first, Range second)
{
	int first_end = end_line(c, first);
	int second_start = start_line(c, second);
	Comments lc = leading_comments(c, second.lo);
	CM_FOR(lc, c->src, cm)
	{
		int l = cstart_line(c, cm);
		if (l > first_end) {
			second_start = l;
			break;
		}
	}
	return second_start > first_end + 1;
}

DP bool node_helpers_get_use_new_lines_for_nodes(Ctx *c, Range a, Range b) { return end_line(c, a) != start_line(c, b); }

DP const Comment *get_leading_comment_on_different_line(Ctx *c, Range r, const Comment **ignore, int nignore)
{
	int start = start_line(c, r);
	Comments lc = leading_comments(c, r.lo);
	CM_FOR(lc, c->src, cm)
	{
		bool skip = false;
		for (int i = 0; i < nignore; i++)
			if (ignore[i]->lo == cm->lo)
				skip = true;
		if (skip)
			continue;
		if (cstart_line(c, cm) < start)
			return cm;
	}
	return NULL;
}

DP bool has_leading_comment_on_different_line(Ctx *c, Range r, const Comment **ignore, int nignore)
{
	return get_leading_comment_on_different_line(c, r, ignore, nignore) != NULL;
}

DP bool has_surrounding_comments(Ctx *c, Range r)
{
	Comments l = leading_comments(c, r.lo), t = trailing_comments(c, r.hi);
	return !cm_empty(&l) || !cm_empty(&t);
}

DP bool is_expr_stmt_or_body_with_single_expr_stmt(Node *n)
{
	if (n->kind == N_EXPR_STMT)
		return true;
	return n->kind == N_BLOCK_STMT && n->list.n == 1 && n->list.v[0]->kind == N_EXPR_STMT;
}

/* ---- generate_types.rs ---- */

/* RangedExtensions::start_line_with_comments */
DP int start_line_with_comments(Ctx *c, Range r)
{
	Comments lc = leading_comments(c, r.lo);
	if (cm_empty(&lc))
		return start_line(c, r);
	const Tok *prev = previous_token(c, r);
	if (prev) {
		int prev_end_line = end_line(c, trange(prev));
		bool past_trailing = false;
		CM_FOR(lc, c->src, cm)
		{
			int csl = cstart_line(c, cm);
			if (!past_trailing && csl <= prev_end_line) {
				if (cend_line(c, cm) > prev_end_line)
					past_trailing = true;
			} else {
				return csl;
			}
		}
		return start_line(c, r);
	}
	return cstart_line(c, cm_get(&lc, c->src, 0));
}

/* RangedExtensions::end_line_with_comments */
DP int end_line_with_comments(Ctx *c, Range r)
{
	const Tok *comma = get_next_token_if_comma(c, r);
	uint32_t search_end = comma ? comma->hi : r.hi;
	Comments tc = trailing_comments(c, search_end);
	int prev_end_line = line_of(c, search_end);
	CM_FOR(tc, c->src, cm)
	{
		if (!cm->block)
			break;
		int csl = cstart_line(c, cm);
		if (csl <= prev_end_line) {
			int cel = cend_line(c, cm);
			if (cel > prev_end_line)
				return cel;
		} else {
			break;
		}
	}
	return prev_end_line;
}

/* get_params_or_args_range */
DP bool get_params_or_args_range(Ctx *c, uint32_t start_pos, Node **params, int nparams, uint32_t following_pos,
				     Range *out)
{
	const Tok *close_paren = nparams > 0 ? get_first_close_paren_after(c, nrange(params[nparams - 1]))
					     : get_first_close_paren_before(c, prange(following_pos));
	if (!close_paren || close_paren->hi <= start_pos)
		return false;
	uint32_t close_end = close_paren->hi;
	const Tok *open_paren = get_first_open_paren_before(c, prange(nparams > 0 ? params[0]->lo : close_end));
	if (!open_paren || open_paren->lo < start_pos)
		return false;
	*out = rng(open_paren->lo, close_end);
	return true;
}

/* ---- utils/string_utils.rs ---- */

DP bool rust_ws(uint32_t ch) { return is_rust_whitespace(ch); }

DP bool has_no_new_lines_in_leading_whitespace(Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t ch;
		i += (size_t)utf8_decode(s.p + i, s.n - i, &ch);
		if (!rust_ws(ch))
			return true;
		if (ch == '\n')
			return false;
	}
	return true;
}

DP bool has_no_new_lines_in_trailing_whitespace(Str s)
{
	for (size_t i = s.n; i > 0;) {
		size_t j = i - 1;
		while (j > 0 && ((unsigned char)s.p[j] & 0xC0) == 0x80)
			j--;
		uint32_t ch;
		utf8_decode(s.p + j, i - j, &ch);
		if (!rust_ws(ch))
			return true;
		if (ch == '\n')
			return false;
		i = j;
	}
	return true;
}
