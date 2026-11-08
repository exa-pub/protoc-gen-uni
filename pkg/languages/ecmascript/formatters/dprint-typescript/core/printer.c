/* Port of dprint-core 0.62.1 src/formatting/printer.rs, writer.rs, write_items.rs, print.rs,
 * collections/rc_stack.rs, collections/graph_node.rs and infinite_reevaluation_protection.rs.
 *
 * dprint-core: Copyright (c) 2019-2023 David Sherret, MIT License. */

/* ---- write_items.rs / collections/graph_node.rs ---- */

enum { WI_STRING, WI_INDENT, WI_NEW_LINE, WI_TAB, WI_SPACE };

typedef struct WNode {
	struct WNode *previous;
	uint8_t kind;
	uint8_t indent;
	const StrItem *str;
} WNode;

/* ---- writer.rs ---- */

typedef struct {
	uint32_t current_line_column;
	uint32_t current_line_number;
	uint8_t last_line_indent_level;
	uint8_t indent_level;
	bool expect_newline_next;
	uint8_t indent_queue_count;
	bool last_was_not_trailing_space;
	uint8_t ignore_indent_count;
	WNode *items;
} WriterState;

typedef struct {
	WriterState state;
	uint8_t indent_width;
} Writer;

static uint32_t ws_column_number(const WriterState *s, uint8_t indent_width)
{
	return s->current_line_column == 0 ? (uint32_t)indent_width * s->indent_level : s->current_line_column;
}

static WriterInfo ws_writer_info(const WriterState *s, uint8_t indent_width)
{
	WriterInfo w;
	w.line_number = s->current_line_number;
	w.column_number = ws_column_number(s, indent_width);
	w.indent_level = s->indent_level;
	w.line_start_indent_level = s->last_line_indent_level;
	w.indent_width = indent_width;
	w.expect_newline_next = s->expect_newline_next;
	return w;
}

static void wr_push_item(Writer *w, uint8_t kind, uint8_t indent, const StrItem *str)
{
	WNode *n = xalloc(sizeof *n);
	n->previous = w->state.items;
	n->kind = kind;
	n->indent = indent;
	n->str = str;
	w->state.items = n;
	if (w->state.indent_queue_count > 0) {
		uint8_t c = w->state.indent_queue_count;
		w->state.indent_queue_count = 0;
		w->state.indent_level += c;
	}
}

static void wr_pop_item(Writer *w)
{
	if (w->state.items)
		w->state.items = w->state.items->previous;
}

static void wr_set_indent_level(Writer *w, uint8_t level)
{
	w->state.indent_level = level;
	if (w->state.current_line_column == 0)
		w->state.last_line_indent_level = level;
}

static void wr_start_indent(Writer *w) { wr_set_indent_level(w, (uint8_t)(w->state.indent_level + 1)); }

static void wr_finish_indent(Writer *w)
{
	if (w->state.indent_queue_count > 0) {
		w->state.indent_queue_count--;
	} else {
		if (w->state.indent_level == 0)
			fail("dprint: finish_indent was called without a corresponding start_indent");
		wr_set_indent_level(w, (uint8_t)(w->state.indent_level - 1));
	}
}

static void wr_new_line(Writer *w)
{
	if (w->state.last_was_not_trailing_space) {
		wr_pop_item(w);
		w->state.last_was_not_trailing_space = false;
	}
	w->state.current_line_column = 0;
	w->state.current_line_number++;
	w->state.last_line_indent_level = w->state.indent_level;
	w->state.expect_newline_next = false;
	wr_push_item(w, WI_NEW_LINE, 0, NULL);
}

static void wr_handle_first_column(Writer *w)
{
	if (w->state.expect_newline_next)
		wr_new_line(w);
	w->state.last_was_not_trailing_space = false;
	if (w->state.current_line_column == 0 && w->state.indent_level > 0 && w->state.ignore_indent_count == 0) {
		w->state.last_line_indent_level = w->state.indent_level;
		w->state.current_line_column = (uint32_t)w->state.indent_level * w->indent_width;
		wr_push_item(w, WI_INDENT, w->state.indent_level, NULL);
	}
}

static void wr_space(Writer *w)
{
	wr_handle_first_column(w);
	w->state.current_line_column++;
	wr_push_item(w, WI_SPACE, 0, NULL);
}

static void wr_space_if_not_trailing(Writer *w)
{
	if (!w->state.expect_newline_next) {
		wr_space(w);
		w->state.last_was_not_trailing_space = true;
	}
}

static void wr_single_indent(Writer *w)
{
	wr_handle_first_column(w);
	w->state.current_line_column += w->indent_width;
	wr_push_item(w, WI_INDENT, 1, NULL);
}

static void wr_tab(Writer *w)
{
	wr_handle_first_column(w);
	w->state.current_line_column += w->indent_width;
	wr_push_item(w, WI_TAB, 0, NULL);
}

static void wr_write(Writer *w, const StrItem *s)
{
	wr_handle_first_column(w);
	w->state.current_line_column += s->width;
	wr_push_item(w, WI_STRING, 0, s);
}

static bool wr_is_start_of_line(const Writer *w)
{
	return w->state.expect_newline_next ||
	       ws_column_number(&w->state, w->indent_width) == (uint32_t)w->state.last_line_indent_level * w->indent_width;
}

/* ---- persistent maps and stacks ---- */

typedef struct SavePoint SavePoint;

/* Look-ahead save points by id: immutable sorted array (shared between save points). */
typedef struct {
	int n;
	uint32_t *ids;
	SavePoint **sps;
} LookAhead;

static const LookAhead lookahead_empty = {0, NULL, NULL};

static int la_find(const LookAhead *m, uint32_t id)
{
	int lo = 0, hi = m->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (m->ids[mid] < id)
			lo = mid + 1;
		else
			hi = mid;
	}
	return (lo < m->n && m->ids[lo] == id) ? lo : -1;
}

static const LookAhead *la_insert(const LookAhead *m, uint32_t id, SavePoint *sp)
{
	LookAhead *r = xalloc(sizeof *r);
	r->n = m->n + 1;
	r->ids = xalloc(sizeof(uint32_t) * (size_t)r->n);
	r->sps = xalloc(sizeof(SavePoint *) * (size_t)r->n);
	int j = 0;
	bool done = false;
	for (int i = 0; i < m->n; i++) {
		if (!done && id < m->ids[i]) {
			r->ids[j] = id;
			r->sps[j++] = sp;
			done = true;
		}
		r->ids[j] = m->ids[i];
		r->sps[j++] = m->sps[i];
	}
	if (!done) {
		r->ids[j] = id;
		r->sps[j] = sp;
	}
	return r;
}

static const LookAhead *la_remove(const LookAhead *m, int idx)
{
	if (m->n == 1)
		return &lookahead_empty;
	LookAhead *r = xalloc(sizeof *r);
	r->n = m->n - 1;
	r->ids = xalloc(sizeof(uint32_t) * (size_t)r->n);
	r->sps = xalloc(sizeof(SavePoint *) * (size_t)r->n);
	for (int i = 0, j = 0; i < m->n; i++) {
		if (i == idx)
			continue;
		r->ids[j] = m->ids[i];
		r->sps[j++] = m->sps[i];
	}
	return r;
}

typedef struct RcNode {
	PNode *path;
	struct RcNode *next;
} RcNode;

struct SavePoint {
	const char *name;
	uint16_t new_line_group_depth;
	uint8_t force_no_newlines_depth;
	WriterState writer_state;
	SavePoint *possible_new_line_save_point;
	PNode *node;
	const LookAhead *look_ahead;
	RcNode *next_node_stack;
};

/* ---- printer.rs ---- */

enum { VAL_NONE = 0, VAL_SET = 1 };

typedef struct Printer {
	SavePoint *possible_new_line_save_point;
	uint16_t new_line_group_depth;
	uint8_t force_no_newlines_depth;
	PNode *current_node;
	Writer writer;
	uint32_t nids;
	/* resolved infos and anchors by id */
	uint32_t *values;
	uint8_t *has_value;
	/* resolved conditions by id: -2 absent, -1 Some(None), 0/1 */
	int8_t *conditions;
	/* stored condition save points by condition id */
	Cond **stored_conditions;
	SavePoint **stored_save_points;
	/* infinite_reevaluation_protection.rs: counts by reevaluation id */
	uint16_t *reevaluation_counts;
	const LookAhead *look_ahead;
	RcNode *next_node_stack;
	uint32_t max_width;
	bool skip_moving_next;
	SavePoint *resolving_save_point;
} Printer;

static SavePoint *pr_create_save_point(Printer *p, const char *name, PNode *next_node)
{
	SavePoint *sp = xalloc(sizeof *sp);
	sp->name = name;
	sp->possible_new_line_save_point = p->possible_new_line_save_point;
	sp->new_line_group_depth = p->new_line_group_depth;
	sp->force_no_newlines_depth = p->force_no_newlines_depth;
	sp->node = next_node;
	sp->writer_state = p->writer.state;
	sp->look_ahead = p->look_ahead;
	sp->next_node_stack = p->next_node_stack;
	return sp;
}

static SavePoint *pr_save_point_for_restoring_condition(Printer *p, const char *name)
{
	if (p->resolving_save_point)
		return p->resolving_save_point;
	return pr_create_save_point(p, name, p->current_node);
}

/* resolved_line_number & co.: the value, or registers a look-ahead save point. */
static bool pr_resolved_info(Printer *p, uint32_t id, uint32_t *out)
{
	if (id < p->nids && p->has_value[id]) {
		*out = p->values[id];
		return true;
	}
	if (la_find(p->look_ahead, id) < 0) {
		SavePoint *sp = pr_save_point_for_restoring_condition(p, "info");
		p->look_ahead = la_insert(p->look_ahead, id, sp);
	}
	return false;
}

static int pr_resolved_condition(Printer *p, uint32_t id)
{
	bool contains = id < p->nids && p->conditions[id] != -2;
	if (!contains && la_find(p->look_ahead, id) < 0) {
		SavePoint *sp = pr_save_point_for_restoring_condition(p, "condition");
		p->look_ahead = la_insert(p->look_ahead, id, sp);
	}
	if (!contains)
		return -1;
	return p->conditions[id];
}

DP int rctx_condition(RCtx *c, ConditionReference r) { return pr_resolved_condition(c->printer, r.id); }
DP bool rctx_ln(RCtx *c, LineNumber x, uint32_t *out) { return pr_resolved_info(c->printer, x.id, out); }
DP bool rctx_cn(RCtx *c, ColumnNumber x, uint32_t *out) { return pr_resolved_info(c->printer, x.id, out); }
DP int rctx_isol(RCtx *c, IsStartOfLine x)
{
	uint32_t v;
	if (!pr_resolved_info(c->printer, x.id, &v))
		return -1;
	return v != 0;
}
DP bool rctx_il(RCtx *c, IndentLevel x, uint32_t *out) { return pr_resolved_info(c->printer, x.id, out); }
DP bool rctx_lscn(RCtx *c, LineStartColumnNumber x, uint32_t *out) { return pr_resolved_info(c->printer, x.id, out); }
DP bool rctx_lsil(RCtx *c, LineStartIndentLevel x, uint32_t *out) { return pr_resolved_info(c->printer, x.id, out); }
DP void rctx_clear(RCtx *c, uint32_t id)
{
	if (id < c->printer->nids)
		c->printer->has_value[id] = 0;
}
DP bool rctx_is_forcing_no_newlines(RCtx *c) { return c->printer->force_no_newlines_depth > 0; }

static void pr_write_new_line(Printer *p)
{
	wr_new_line(&p->writer);
	p->possible_new_line_save_point = NULL;
}

static void pr_mark_possible_new_line_if_able(Printer *p)
{
	if (p->possible_new_line_save_point &&
	    p->new_line_group_depth > p->possible_new_line_save_point->new_line_group_depth)
		return;
	PNode *next = p->current_node->next;
	p->possible_new_line_save_point = pr_create_save_point(p, "newline", next);
}

static bool pr_is_above_max_width(Printer *p, uint32_t offset)
{
	return ws_column_number(&p->writer.state, p->writer.indent_width) + offset > p->max_width;
}

static void pr_update_state_to_save_point(Printer *p, SavePoint *sp, bool is_for_new_line)
{
	p->writer.state = sp->writer_state;
	p->possible_new_line_save_point = is_for_new_line ? NULL : sp->possible_new_line_save_point;
	p->current_node = sp->node;
	p->new_line_group_depth = sp->new_line_group_depth;
	p->force_no_newlines_depth = sp->force_no_newlines_depth;
	p->look_ahead = sp->look_ahead;
	p->next_node_stack = sp->next_node_stack;
	if (is_for_new_line)
		pr_write_new_line(p);
	p->skip_moving_next = true;
}

static bool pr_allow_new_lines(Printer *p) { return p->force_no_newlines_depth == 0; }

static void pr_push_next(Printer *p, PNode *path)
{
	RcNode *n = xalloc(sizeof *n);
	n->path = path;
	n->next = p->next_node_stack;
	p->next_node_stack = n;
}

static void pr_handle_signal(Printer *p, Signal s)
{
	Writer *w = &p->writer;
	switch (s) {
	case SIG_NEW_LINE:
		if (pr_allow_new_lines(p))
			pr_write_new_line(p);
		break;
	case SIG_TAB:
		wr_tab(w);
		break;
	case SIG_EXPECT_NEW_LINE:
		w->state.expect_newline_next = true;
		p->possible_new_line_save_point = NULL;
		break;
	case SIG_POSSIBLE_NEW_LINE:
		if (pr_allow_new_lines(p))
			pr_mark_possible_new_line_if_able(p);
		break;
	case SIG_SPACE_OR_NEW_LINE:
		if (pr_allow_new_lines(p)) {
			if (pr_is_above_max_width(p, 1)) {
				SavePoint *sp = p->possible_new_line_save_point;
				p->possible_new_line_save_point = NULL;
				if (!sp || sp->new_line_group_depth >= p->new_line_group_depth)
					pr_write_new_line(p);
				else
					pr_update_state_to_save_point(p, sp, true);
			} else {
				pr_mark_possible_new_line_if_able(p);
				wr_space_if_not_trailing(w);
			}
		} else {
			wr_space_if_not_trailing(w);
		}
		break;
	case SIG_QUEUE_START_INDENT:
		w->state.indent_queue_count++;
		break;
	case SIG_START_INDENT:
		wr_start_indent(w);
		break;
	case SIG_FINISH_INDENT:
		wr_finish_indent(w);
		break;
	case SIG_START_NEW_LINE_GROUP:
		p->new_line_group_depth++;
		break;
	case SIG_FINISH_NEW_LINE_GROUP:
		p->new_line_group_depth--;
		break;
	case SIG_SINGLE_INDENT:
		wr_single_indent(w);
		break;
	case SIG_START_IGNORING_INDENT:
		w->state.ignore_indent_count++;
		break;
	case SIG_FINISH_IGNORING_INDENT:
		w->state.ignore_indent_count--;
		break;
	case SIG_START_FORCE_NO_NEW_LINES:
		p->force_no_newlines_depth++;
		break;
	case SIG_FINISH_FORCE_NO_NEW_LINES:
		p->force_no_newlines_depth--;
		break;
	case SIG_SPACE_IF_NOT_TRAILING:
		wr_space_if_not_trailing(w);
		break;
	}
}

static void pr_handle_anchor(Printer *p, PNode *n)
{
	uint32_t id = n->id;
	uint32_t current = p->writer.state.current_line_number;
	if (p->has_value[id]) {
		int64_t diff = (int64_t)current - (int64_t)p->values[id];
		if (diff != 0) {
			uint32_t lid = n->id2;
			if (p->has_value[lid])
				p->values[lid] = (uint32_t)((int64_t)p->values[lid] + diff);
		}
	}
	p->values[id] = current;
	p->has_value[id] = 1;
}

static void pr_handle_info(Printer *p, PNode *n)
{
	Writer *w = &p->writer;
	uint32_t v = 0;
	switch ((InfoKind)n->sub) {
	case INFO_LINE_NUMBER:
		v = w->state.current_line_number;
		break;
	case INFO_COLUMN_NUMBER:
		v = ws_column_number(&w->state, w->indent_width);
		break;
	case INFO_IS_START_OF_LINE:
		v = wr_is_start_of_line(w);
		break;
	case INFO_INDENT_LEVEL:
		v = w->state.indent_level;
		break;
	case INFO_LINE_START_COLUMN_NUMBER:
		v = (uint32_t)w->state.last_line_indent_level * w->indent_width;
		break;
	case INFO_LINE_START_INDENT_LEVEL:
		v = w->state.last_line_indent_level;
		break;
	}
	p->values[n->id] = v;
	p->has_value[n->id] = 1;
	int idx = la_find(p->look_ahead, n->id);
	if (idx >= 0) {
		SavePoint *sp = p->look_ahead->sps[idx];
		p->look_ahead = la_remove(p->look_ahead, idx);
		pr_update_state_to_save_point(p, sp, false);
	}
}

static int cond_resolve(Printer *p, Cond *c, WriterInfo wi)
{
	RCtx ctx = {p, wi};
	return c->resolver.fn(&ctx, c->resolver.data);
}

/* InfiniteReevaluationProtector::should_reevaluate */
static bool pr_should_reevaluate(Printer *p, uint32_t id, int current_value, bool last_value)
{
	enum { MAX_COUNT = 1000 };
	bool cur = current_value == 1;
	if (cur == last_value) {
		p->reevaluation_counts[id] = 0;
		return true;
	}
	uint16_t next = (uint16_t)(p->reevaluation_counts[id] + 1);
	if (next == MAX_COUNT + 1)
		return false;
	p->reevaluation_counts[id] = next;
	return next != MAX_COUNT;
}

static void pr_handle_reevaluation(Printer *p, PNode *n)
{
	uint32_t cid = n->id2;
	SavePoint *sp = p->stored_save_points[cid];
	if (!sp)
		return;
	Cond *c = p->stored_conditions[cid];
	int past = p->conditions[cid];
	if (past < 0)
		return; /* absent or None */
	p->resolving_save_point = sp;
	int latest = cond_resolve(p, c, ws_writer_info(&sp->writer_state, p->writer.indent_width));
	p->resolving_save_point = NULL;
	if (pr_should_reevaluate(p, n->id, latest, past == 1)) {
		if (latest >= 0) {
			if (latest != past)
				pr_update_state_to_save_point(p, sp, false);
		} else {
			p->conditions[cid] = -2;
		}
	}
}

static void pr_handle_condition(Printer *p, Cond *c, PNode *next)
{
	uint32_t id = c->id;
	if (c->store_save_point) {
		SavePoint *sp = pr_save_point_for_restoring_condition(p, c->name);
		p->stored_conditions[id] = c;
		p->stored_save_points[id] = sp;
	}
	int value = cond_resolve(p, c, ws_writer_info(&p->writer.state, p->writer.indent_width));
	if (c->is_stored)
		p->conditions[id] = (int8_t)value;
	int idx = la_find(p->look_ahead, id);
	if (value >= 0 && idx >= 0) {
		SavePoint *sp = p->look_ahead->sps[idx];
		p->look_ahead = la_remove(p->look_ahead, idx);
		pr_update_state_to_save_point(p, sp, false);
		return;
	}
	if (value == 1) {
		if (c->true_path) {
			p->current_node = c->true_path;
			if (next)
				pr_push_next(p, next);
			p->skip_moving_next = true;
		}
	} else if (c->false_path) {
		p->current_node = c->false_path;
		if (next)
			pr_push_next(p, next);
		p->skip_moving_next = true;
	}
}

static void pr_handle_string(Printer *p, StrItem *s)
{
	if (p->possible_new_line_save_point && pr_is_above_max_width(p, s->width) && pr_allow_new_lines(p)) {
		SavePoint *sp = p->possible_new_line_save_point;
		p->possible_new_line_save_point = NULL;
		pr_update_state_to_save_point(p, sp, true);
	} else {
		wr_write(&p->writer, s);
	}
}

static void pr_handle_node(Printer *p, PNode *n)
{
	switch (n->kind) {
	case PI_STRING:
		pr_handle_string(p, n->u.str);
		break;
	case PI_CONDITION:
		pr_handle_condition(p, n->u.cond, n->next);
		break;
	case PI_SIGNAL:
		pr_handle_signal(p, (Signal)n->sub);
		break;
	case PI_PATH:
		if (n->next)
			pr_push_next(p, n->next);
		p->current_node = n->u.path;
		p->skip_moving_next = true;
		break;
	case PI_ANCHOR:
		pr_handle_anchor(p, n);
		break;
	case PI_INFO:
		pr_handle_info(p, n);
		break;
	case PI_REEVALUATION:
		pr_handle_reevaluation(p, n);
		break;
	}
}

/* print.rs print_with_allocator + WriteItemsPrinter::print */
DP void dprint_print(Items items, PrintOptions opts, Buf *out)
{
	Printer *p = xalloc(sizeof *p);
	p->current_node = items.first;
	p->writer.indent_width = opts.indent_width;
	p->max_width = opts.max_width;
	p->look_ahead = &lookahead_empty;
	uint32_t n = dp_next_id + 1;
	p->nids = n;
	p->values = xalloc(sizeof(uint32_t) * n);
	p->has_value = xalloc(n);
	p->conditions = xalloc(n);
	memset(p->conditions, -2, n);
	p->stored_conditions = xalloc(sizeof(Cond *) * n);
	p->stored_save_points = xalloc(sizeof(SavePoint *) * n);
	p->reevaluation_counts = xalloc(sizeof(uint16_t) * n);

	while (p->current_node) {
		pr_handle_node(p, p->current_node);
		if (p->skip_moving_next)
			p->skip_moving_next = false;
		else if (p->current_node)
			p->current_node = p->current_node->next;
		while (!p->current_node && p->next_node_stack) {
			p->current_node = p->next_node_stack->path;
			p->next_node_stack = p->next_node_stack->next;
		}
	}

	/* the writer items are a backwards list */
	size_t count = 0;
	for (WNode *w = p->writer.state.items; w; w = w->previous)
		count++;
	WNode **v = xalloc(sizeof(WNode *) * (count ? count : 1));
	size_t i = count;
	for (WNode *w = p->writer.state.items; w; w = w->previous)
		v[--i] = w;
	for (i = 0; i < count; i++) {
		WNode *w = v[i];
		switch (w->kind) {
		case WI_INDENT:
			if (opts.use_tabs) {
				for (int k = 0; k < w->indent; k++)
					buf_addc(out, '\t');
			} else {
				for (int k = 0; k < w->indent * opts.indent_width; k++)
					buf_addc(out, ' ');
			}
			break;
		case WI_NEW_LINE:
			buf_adds(out, opts.new_line_text);
			break;
		case WI_TAB:
			buf_addc(out, '\t');
			break;
		case WI_SPACE:
			buf_addc(out, ' ');
			break;
		case WI_STRING:
			buf_add(out, w->str->text, w->str->len);
			break;
		}
	}
}
