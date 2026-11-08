/* Port of prettyplease 0.2.37 src/algorithm.rs (adapted by prettyplease from rustc_ast_pretty
 * src/pp.rs, Rust 1.57.0), src/ring.rs and src/convenience.rs. Copyright David Tolnay and The Rust
 * Project Developers, MIT OR Apache-2.0. Debug output (cfg prettyplease_debug) is not ported. */

/* ---- ring.rs ---- */

static void ring_grow(RingBuffer *r)
{
	size_t cap = r->cap ? r->cap * 2 : 64;
	BufEntry *d = xalloc(cap * sizeof *d);
	for (size_t i = 0; i < r->len; i++)
		d[i] = r->data[(r->head + i) & (r->cap - 1)];
	r->data = d;
	r->cap = cap;
	r->head = 0;
}

static bool ring_is_empty(const RingBuffer *r) { return r->len == 0; }

static size_t ring_push(RingBuffer *r, BufEntry e)
{
	if (r->len == r->cap)
		ring_grow(r);
	size_t index = r->offset + r->len;
	r->data[(r->head + r->len) & (r->cap - 1)] = e;
	r->len++;
	return index;
}

static void ring_clear(RingBuffer *r)
{
	/* VecDeque::clear keeps the abstract offset. */
	r->len = 0;
}

static BufEntry *ring_at(RingBuffer *r, size_t index)
{
	if (index < r->offset || index - r->offset >= r->len)
		fail("prettyplease: ring buffer index out of range");
	return &r->data[(r->head + (index - r->offset)) & (r->cap - 1)];
}

static BufEntry *ring_first(RingBuffer *r) { return ring_at(r, r->offset); }
static BufEntry *ring_last(RingBuffer *r) { return ring_at(r, r->offset + r->len - 1); }
static BufEntry *ring_second_last(RingBuffer *r) { return ring_at(r, r->offset + r->len - 2); }

static BufEntry ring_pop_first(RingBuffer *r)
{
	BufEntry e = *ring_first(r);
	r->head = (r->head + 1) & (r->cap - 1);
	r->len--;
	r->offset++;
	return e;
}

static void ring_pop_last(RingBuffer *r)
{
	if (!r->len)
		fail("prettyplease: pop from an empty ring buffer");
	r->len--;
}

/* ---- VecDeque<usize> ---- */

static void dq_push_back(Deque *q, size_t v)
{
	if (q->len == q->cap) {
		size_t cap = q->cap ? q->cap * 2 : 64;
		size_t *d = xalloc(cap * sizeof *d);
		for (size_t i = 0; i < q->len; i++)
			d[i] = q->data[(q->head + i) & (q->cap - 1)];
		q->data = d;
		q->cap = cap;
		q->head = 0;
	}
	q->data[(q->head + q->len) & (q->cap - 1)] = v;
	q->len++;
}

static size_t dq_at(const Deque *q, size_t i) { return q->data[(q->head + i) & (q->cap - 1)]; }
static size_t dq_back(const Deque *q) { return dq_at(q, q->len - 1); }
static size_t dq_front(const Deque *q) { return dq_at(q, 0); }
static void dq_pop_back(Deque *q) { q->len--; }

static void dq_pop_front(Deque *q)
{
	q->head = (q->head + 1) & (q->cap - 1);
	q->len--;
}

/* ---- algorithm.rs ---- */

static void pp_init(Pp *p)
{
	memset(p, 0, sizeof *p);
	p->space = MARGIN;
}

static void print_indent(Pp *p)
{
	for (size_t i = 0; i < p->pending_indentation; i++)
		buf_addc(&p->out, ' ');
	p->pending_indentation = 0;
}

static void print_string(Pp *p, const char *s, size_t n)
{
	print_indent(p);
	buf_add(&p->out, s, n);
	p->space -= (intptr_t)n;
}

static PrintFrame get_top(const Pp *p)
{
	if (!p->print_len)
		return (PrintFrame){true, BREAKS_INCONSISTENT, 0};
	return p->print_stack[p->print_len - 1];
}

static void print_begin(Pp *p, const Token *token, intptr_t size)
{
	if (p->print_len == p->print_cap) {
		size_t cap = p->print_cap ? p->print_cap * 2 : 64;
		p->print_stack = xrealloc(p->print_stack, p->print_cap * sizeof *p->print_stack, cap * sizeof *p->print_stack);
		p->print_cap = cap;
	}
	if (size > p->space) {
		p->print_stack[p->print_len++] = (PrintFrame){true, token->breaks, p->indent};
		p->indent += token->brk.offset;
		if (p->indent < 0)
			fail("prettyplease: negative indentation");
	} else {
		p->print_stack[p->print_len++] = (PrintFrame){false, token->breaks, 0};
	}
}

static void print_end(Pp *p)
{
	if (!p->print_len)
		fail("prettyplease: unbalanced end");
	PrintFrame f = p->print_stack[--p->print_len];
	if (f.broken)
		p->indent = f.indent;
}

static void print_break(Pp *p, BreakToken token, intptr_t size)
{
	PrintFrame top = get_top(p);
	bool fits = token.never_break || !top.broken ||
		    (top.breaks == BREAKS_INCONSISTENT && size <= p->space);
	if (fits) {
		p->pending_indentation += (size_t)token.blank_space;
		p->space -= token.blank_space;
		if (token.no_break) {
			buf_addc(&p->out, token.no_break);
			p->space -= 1;
		}
	} else {
		if (token.pre_break) {
			print_indent(p);
			buf_addc(&p->out, token.pre_break);
		}
		buf_addc(&p->out, '\n');
		intptr_t indent = p->indent + token.offset;
		if (indent < 0)
			fail("prettyplease: negative indentation");
		p->pending_indentation = (size_t)indent;
		p->space = MARGIN - indent > MIN_SPACE ? MARGIN - indent : MIN_SPACE;
		if (*token.post_break) {
			print_indent(p);
			size_t n = strlen(token.post_break);
			buf_add(&p->out, token.post_break, n);
			p->space -= (intptr_t)n;
		}
	}
}

static void advance_left(Pp *p)
{
	while (ring_first(&p->buf)->size >= 0) {
		BufEntry left = ring_pop_first(&p->buf);
		switch (left.token.kind) {
		case TOK_STRING:
			p->left_total += left.size;
			print_string(p, left.token.s, left.token.n);
			break;
		case TOK_BREAK:
			p->left_total += left.token.brk.blank_space;
			print_break(p, left.token.brk, left.size);
			break;
		case TOK_BEGIN:
			print_begin(p, &left.token, left.size);
			break;
		default:
			print_end(p);
		}
		if (ring_is_empty(&p->buf))
			break;
	}
}

static void check_stack(Pp *p, size_t depth)
{
	while (p->scan_stack.len) {
		size_t index = dq_back(&p->scan_stack);
		BufEntry *entry = ring_at(&p->buf, index);
		switch (entry->token.kind) {
		case TOK_BEGIN:
			if (depth == 0)
				return;
			dq_pop_back(&p->scan_stack);
			entry->size += p->right_total;
			depth--;
			break;
		case TOK_END:
			dq_pop_back(&p->scan_stack);
			entry->size = 1;
			depth++;
			break;
		case TOK_BREAK:
			dq_pop_back(&p->scan_stack);
			entry->size += p->right_total;
			if (depth == 0)
				return;
			break;
		default:
			fail("prettyplease: string on the scan stack");
		}
	}
}

static void check_stream(Pp *p)
{
	while (p->right_total - p->left_total > p->space) {
		if (!p->scan_stack.len)
			fail("prettyplease: empty scan stack");
		if (dq_front(&p->scan_stack) == p->buf.offset) {
			dq_pop_front(&p->scan_stack);
			ring_first(&p->buf)->size = SIZE_INFINITY;
		}
		advance_left(p);
		if (ring_is_empty(&p->buf))
			break;
	}
}

static void scan_begin(Pp *p, intptr_t offset, int breaks)
{
	if (!p->scan_stack.len) {
		p->left_total = 1;
		p->right_total = 1;
		ring_clear(&p->buf);
	}
	BufEntry e = {0};
	e.token.kind = TOK_BEGIN;
	e.token.breaks = (uint8_t)breaks;
	e.token.brk.offset = offset;
	e.size = -p->right_total;
	dq_push_back(&p->scan_stack, ring_push(&p->buf, e));
}

static void scan_end(Pp *p)
{
	if (!p->scan_stack.len) {
		print_end(p);
		return;
	}
	if (!ring_is_empty(&p->buf)) {
		BufEntry *last = ring_last(&p->buf);
		if (last->token.kind == TOK_BREAK) {
			BreakToken break_token = last->token.brk;
			if (p->buf.len >= 2 && ring_second_last(&p->buf)->token.kind == TOK_BEGIN) {
				ring_pop_last(&p->buf);
				ring_pop_last(&p->buf);
				dq_pop_back(&p->scan_stack);
				dq_pop_back(&p->scan_stack);
				p->right_total -= break_token.blank_space;
				return;
			}
			if (break_token.if_nonempty) {
				ring_pop_last(&p->buf);
				dq_pop_back(&p->scan_stack);
				p->right_total -= break_token.blank_space;
			}
		}
	}
	BufEntry e = {0};
	e.token.kind = TOK_END;
	e.size = -1;
	dq_push_back(&p->scan_stack, ring_push(&p->buf, e));
}

static void scan_break(Pp *p, BreakToken token)
{
	if (!p->scan_stack.len) {
		p->left_total = 1;
		p->right_total = 1;
		ring_clear(&p->buf);
	} else {
		check_stack(p, 0);
	}
	if (!token.post_break)
		token.post_break = "";
	BufEntry e = {0};
	e.token.kind = TOK_BREAK;
	e.token.brk = token;
	e.size = -p->right_total;
	dq_push_back(&p->scan_stack, ring_push(&p->buf, e));
	p->right_total += token.blank_space;
}

static void scan_string(Pp *p, const char *s, size_t n)
{
	if (!p->scan_stack.len) {
		print_string(p, s, n);
		return;
	}
	BufEntry e = {0};
	e.token.kind = TOK_STRING;
	e.token.s = s;
	e.token.n = n;
	e.size = (intptr_t)n;
	ring_push(&p->buf, e);
	p->right_total += (intptr_t)n;
	check_stream(p);
}

static void p_offset(Pp *p, intptr_t offset)
{
	BufEntry *last = ring_last(&p->buf);
	if (last->token.kind == TOK_BREAK)
		last->token.brk.offset += offset;
	else if (last->token.kind != TOK_BEGIN)
		fail("prettyplease: offset after a string");
}

static void p_end_with_max_width(Pp *p, intptr_t max)
{
	int depth = 1;
	for (size_t i = p->scan_stack.len; i-- > 0;) {
		BufEntry *entry = ring_at(&p->buf, dq_at(&p->scan_stack, i));
		if (entry->token.kind == TOK_BEGIN) {
			depth--;
			if (depth == 0) {
				if (entry->size < 0) {
					intptr_t actual_width = entry->size + p->right_total;
					if (actual_width > max) {
						BufEntry e = {0};
						e.token.kind = TOK_STRING;
						e.token.s = "";
						e.size = SIZE_INFINITY;
						ring_push(&p->buf, e);
						p->right_total += SIZE_INFINITY;
					}
				}
				break;
			}
		} else if (entry->token.kind == TOK_END) {
			depth++;
		}
	}
	scan_end(p);
}

static bool p_ends_with(Pp *p, char ch)
{
	for (size_t i = p->buf.offset + p->buf.len; i-- > p->buf.offset;) {
		BufEntry *e = ring_at(&p->buf, i);
		if (e->token.kind == TOK_STRING)
			return e->token.n && e->token.s[e->token.n - 1] == ch;
	}
	return p->out.n && p->out.p[p->out.n - 1] == ch;
}

static char *pp_eof(Pp *p)
{
	if (p->scan_stack.len) {
		check_stack(p, 0);
		advance_left(p);
	}
	return (char *)buf_str(&p->out);
}

/* ---- convenience.rs ---- */

static void p_ibox(Pp *p, intptr_t indent) { scan_begin(p, indent, BREAKS_INCONSISTENT); }
static void p_cbox(Pp *p, intptr_t indent) { scan_begin(p, indent, BREAKS_CONSISTENT); }
static void p_end(Pp *p) { scan_end(p); }
static void p_wordn(Pp *p, const char *s, size_t n) { scan_string(p, s, n); }
static void p_word(Pp *p, const char *s) { scan_string(p, s, strlen(s)); }

static void p_spaces(Pp *p, intptr_t n)
{
	BreakToken t = {0};
	t.blank_space = n;
	scan_break(p, t);
}

static void p_zerobreak(Pp *p) { p_spaces(p, 0); }
static void p_space(Pp *p) { p_spaces(p, 1); }
static void p_nbsp(Pp *p) { p_word(p, " "); }
static void p_hardbreak(Pp *p) { p_spaces(p, SIZE_INFINITY); }

static void p_space_if_nonempty(Pp *p)
{
	BreakToken t = {0};
	t.blank_space = 1;
	t.if_nonempty = true;
	scan_break(p, t);
}

static void p_hardbreak_if_nonempty(Pp *p)
{
	BreakToken t = {0};
	t.blank_space = SIZE_INFINITY;
	t.if_nonempty = true;
	scan_break(p, t);
}

static void p_trailing_comma(Pp *p, bool is_last)
{
	if (is_last) {
		BreakToken t = {0};
		t.pre_break = ',';
		scan_break(p, t);
	} else {
		p_word(p, ",");
		p_space(p);
	}
}

static void p_trailing_comma_or_space(Pp *p, bool is_last)
{
	if (is_last) {
		BreakToken t = {0};
		t.blank_space = 1;
		t.pre_break = ',';
		scan_break(p, t);
	} else {
		p_word(p, ",");
		p_space(p);
	}
}

static void p_neverbreak(Pp *p)
{
	BreakToken t = {0};
	t.never_break = true;
	scan_break(p, t);
}
