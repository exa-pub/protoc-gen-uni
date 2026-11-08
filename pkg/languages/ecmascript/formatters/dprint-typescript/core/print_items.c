/* Port of dprint-core 0.62.1 src/formatting/print_items.rs, conditions.rs, condition_helpers.rs,
 * condition_resolvers.rs, actions.rs and ir_helpers/helpers.rs.
 *
 * dprint-core: Copyright (c) 2019-2023 David Sherret, MIT License. */

/* ---- thread_state.rs: id counters ---- */

static uint32_t dp_next_id;

DP void dprint_ids_reset(void) { dp_next_id = 0; }
static uint32_t next_id(void) { return dp_next_id++; }

DP LineNumber ln_new(void) { return (LineNumber){next_id()}; }
DP ColumnNumber cn_new(void) { return (ColumnNumber){next_id()}; }
DP IsStartOfLine isol_new(void) { return (IsStartOfLine){next_id()}; }
DP IndentLevel il_new(void) { return (IndentLevel){next_id()}; }
DP LineStartColumnNumber lscn_new(void) { return (LineStartColumnNumber){next_id()}; }
DP LineStartIndentLevel lsil_new(void) { return (LineStartIndentLevel){next_id()}; }
DP LineAndColumn lc_new(void)
{
	LineAndColumn lc;
	lc.line = ln_new();
	lc.column = cn_new();
	return lc;
}

/* ---- unicode-width 0.1.10 (approximation: main zero-width and wide ranges) ---- */

static const uint32_t uw_zero[][2] = {
	{0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
	{0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0670, 0x0670},
	{0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0900, 0x0902},
	{0x093A, 0x093A}, {0x093C, 0x093C}, {0x0941, 0x0948}, {0x094D, 0x094D}, {0x0951, 0x0957},
	{0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x1160, 0x11FF}, {0x1AB0, 0x1AFF},
	{0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x2060, 0x2064}, {0x20D0, 0x20F0}, {0x302A, 0x302D},
	{0x3099, 0x309A}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0x1D167, 0x1D169},
	{0xE0001, 0xE0001}, {0xE0020, 0xE007F}, {0xE0100, 0xE01EF},
};
static const uint32_t uw_wide[][2] = {
	{0x1100, 0x115F},   {0x231A, 0x231B},	{0x2329, 0x232A},   {0x23E9, 0x23EC},	{0x23F0, 0x23F0},
	{0x23F3, 0x23F3},   {0x25FD, 0x25FE},	{0x2614, 0x2615},   {0x2648, 0x2653},	{0x267F, 0x267F},
	{0x2693, 0x2693},   {0x26A1, 0x26A1},	{0x26AA, 0x26AB},   {0x26BD, 0x26BE},	{0x26C4, 0x26C5},
	{0x26CE, 0x26CE},   {0x26D4, 0x26D4},	{0x26EA, 0x26EA},   {0x26F2, 0x26F3},	{0x26F5, 0x26F5},
	{0x26FA, 0x26FA},   {0x26FD, 0x26FD},	{0x2705, 0x2705},   {0x270A, 0x270B},	{0x2728, 0x2728},
	{0x274C, 0x274C},   {0x274E, 0x274E},	{0x2753, 0x2755},   {0x2757, 0x2757},	{0x2795, 0x2797},
	{0x27B0, 0x27B0},   {0x27BF, 0x27BF},	{0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},	{0x2B55, 0x2B55},
	{0x2E80, 0x303E},   {0x3041, 0x33FF},	{0x3400, 0x4DBF},   {0x4E00, 0x9FFF},	{0xA000, 0xA4CF},
	{0xA960, 0xA97F},   {0xAC00, 0xD7A3},	{0xF900, 0xFAFF},   {0xFE10, 0xFE19},	{0xFE30, 0xFE6F},
	{0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},	{0x16FE0, 0x16FE4}, {0x17000, 0x18CD5}, {0x1B000, 0x1B2FB},
	{0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F251},
	{0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA},
	{0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440},
	{0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A},
	{0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC},
	{0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB},
	{0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD},
	{0x30000, 0x3FFFD},
};

static bool uw_in(const uint32_t (*t)[2], size_t n, uint32_t c)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (c < t[mid][0])
			hi = mid;
		else if (c > t[mid][1])
			lo = mid + 1;
		else
			return true;
	}
	return false;
}

DP uint32_t unicode_str_width(const char *s, size_t n)
{
	uint32_t w = 0;
	for (size_t i = 0; i < n;) {
		unsigned char b = (unsigned char)s[i];
		if (b < 0x80) {
			if (b >= 0x20 && b != 0x7f)
				w++;
			i++;
			continue;
		}
		uint32_t c;
		i += (size_t)utf8_decode(s + i, n - i, &c);
		if (c < 0xA0)
			continue; /* C1 controls: None -> 0 */
		if (uw_in(uw_zero, sizeof uw_zero / sizeof uw_zero[0], c))
			continue;
		w += uw_in(uw_wide, sizeof uw_wide / sizeof uw_wide[0], c) ? 2 : 1;
	}
	return w;
}

/* ---- PrintItems ---- */

static PNode *pnode_new(uint8_t kind)
{
	PNode *n = xalloc(sizeof *n);
	n->kind = kind;
	return n;
}

static PNode *pnode_last_next(PNode *n)
{
	PNode *cur = n->next;
	while (cur && cur->next)
		cur = cur->next;
	return cur;
}

/* PrintNode::set_next: inserts new_next before the previous next. */
static void pnode_set_next(PNode *n, PNode *new_next)
{
	PNode *past = n->next;
	n->next = new_next;
	if (past && new_next) {
		PNode *last = pnode_last_next(new_next);
		pnode_set_next(last ? last : new_next, past);
	}
}

static void items_push_node(Items *it, PNode *node)
{
	if (it->first) {
		PNode *new_last = pnode_last_next(node);
		pnode_set_next(it->last ? it->last : it->first, node);
		it->last = new_last ? new_last : node;
	} else {
		it->first = node;
		PNode *l = pnode_last_next(node);
		it->last = l ? l : node;
	}
}

DP void items_extend(Items *it, Items other)
{
	if (!other.first)
		return;
	if (it->first) {
		pnode_set_next(it->last ? it->last : it->first, other.first);
		it->last = other.last ? other.last : other.first;
	} else {
		*it = other;
	}
}

static void push_stritem(Items *it, const char *s, size_t n)
{
	StrItem *si = xalloc(sizeof *si);
	si->text = s;
	si->len = (uint32_t)n;
	si->width = unicode_str_width(s, n);
	PNode *node = pnode_new(PI_STRING);
	node->u.str = si;
	items_push_node(it, node);
}

DP void items_push_str(Items *it, const char *s) { push_stritem(it, s, strlen(s)); }
DP void items_push_strn(Items *it, const char *s, size_t n) { push_stritem(it, xstrndup(s, n), n); }

DP void items_push_signal(Items *it, Signal s)
{
	PNode *node = pnode_new(PI_SIGNAL);
	node->sub = (uint8_t)s;
	items_push_node(it, node);
}

DP void items_push_cond(Items *it, Cond *c)
{
	PNode *node = pnode_new(PI_CONDITION);
	node->u.cond = c;
	items_push_node(it, node);
}

DP void items_push_path(Items *it, PNode *path)
{
	if (!path)
		return;
	PNode *node = pnode_new(PI_PATH);
	node->u.path = path;
	items_push_node(it, node);
}

static void push_info(Items *it, InfoKind k, uint32_t id)
{
	PNode *node = pnode_new(PI_INFO);
	node->sub = (uint8_t)k;
	node->id = id;
	items_push_node(it, node);
}

DP void items_push_ln(Items *it, LineNumber x) { push_info(it, INFO_LINE_NUMBER, x.id); }
DP void items_push_cn(Items *it, ColumnNumber x) { push_info(it, INFO_COLUMN_NUMBER, x.id); }
DP void items_push_isol(Items *it, IsStartOfLine x) { push_info(it, INFO_IS_START_OF_LINE, x.id); }
DP void items_push_il(Items *it, IndentLevel x) { push_info(it, INFO_INDENT_LEVEL, x.id); }
DP void items_push_lscn(Items *it, LineStartColumnNumber x) { push_info(it, INFO_LINE_START_COLUMN_NUMBER, x.id); }
DP void items_push_lsil(Items *it, LineStartIndentLevel x) { push_info(it, INFO_LINE_START_INDENT_LEVEL, x.id); }
DP void items_push_lc(Items *it, LineAndColumn x)
{
	items_push_ln(it, x.line);
	items_push_cn(it, x.column);
}

DP void items_push_anchor(Items *it, LineNumber x)
{
	PNode *node = pnode_new(PI_ANCHOR);
	node->id = next_id();
	node->id2 = x.id;
	items_push_node(it, node);
}

DP void items_push_reevaluation(Items *it, ConditionReevaluation r)
{
	PNode *node = pnode_new(PI_REEVALUATION);
	node->id = r.id;
	node->id2 = r.condition;
	items_push_node(it, node);
}

DP Items items_str(const char *s)
{
	Items it = {0};
	items_push_str(&it, s);
	return it;
}

DP Items items_signal(Signal s)
{
	Items it = {0};
	items_push_signal(&it, s);
	return it;
}

DP Items items_cond(Cond *c)
{
	Items it = {0};
	items_push_cond(&it, c);
	return it;
}

DP Items items_path(PNode *path)
{
	Items it = {0};
	items_push_path(&it, path);
	return it;
}

/* ---- Condition ---- */

DP Resolver resolver(ResolveFn fn, void *data) { return (Resolver){fn, data}; }

DP Cond *cond_new(const char *name, Resolver r, Items true_path, Items false_path, bool has_true, bool has_false)
{
	Cond *c = xalloc(sizeof *c);
	c->id = next_id();
	c->name = name;
	c->resolver = r;
	c->true_path = has_true ? true_path.first : NULL;
	c->false_path = has_false ? false_path.first : NULL;
	return c;
}

static int r_true(RCtx *c, void *d) { return 1; }
static int r_false(RCtx *c, void *d) { return 0; }

DP Cond *cond_new_true(void) { return cond_new("trueCondition", resolver(r_true, NULL), (Items){0}, (Items){0}, false, false); }
DP Cond *cond_new_false(void) { return cond_new("falseCondition", resolver(r_false, NULL), (Items){0}, (Items){0}, false, false); }

DP ConditionReference cond_create_reference(Cond *c)
{
	c->is_stored = true;
	return (ConditionReference){c->id};
}

DP ConditionReevaluation cond_create_reevaluation(Cond *c)
{
	c->store_save_point = true;
	c->is_stored = true;
	return (ConditionReevaluation){next_id(), c->id};
}

static int r_condref(RCtx *c, void *d) { return rctx_condition(c, (ConditionReference){(uint32_t)(uintptr_t)d}); }

DP Resolver condref_resolver(ConditionReference r) { return resolver(r_condref, (void *)(uintptr_t)r.id); }

/* ---- WriterInfo ---- */

DP uint32_t wi_line_start_column_number(const WriterInfo *w) { return (uint32_t)w->line_start_indent_level * w->indent_width; }
DP bool wi_is_column_number_at_line_start(const WriterInfo *w) { return w->column_number == wi_line_start_column_number(w); }
DP bool wi_is_start_of_line(const WriterInfo *w) { return w->expect_newline_next || wi_is_column_number_at_line_start(w); }
DP bool wi_is_start_of_line_indented(const WriterInfo *w) { return w->line_start_indent_level > w->indent_level; }

DP bool rctx_lc(RCtx *c, LineAndColumn x, uint32_t *line, uint32_t *col)
{
	if (!rctx_ln(c, x.line, line))
		return false;
	return rctx_cn(c, x.column, col);
}

/* ---- condition_resolvers.rs ---- */

static int r_isol(RCtx *c, void *d) { return wi_is_start_of_line(&c->writer_info); }
static int r_not_isol(RCtx *c, void *d) { return !wi_is_start_of_line(&c->writer_info); }
static int r_isol_indented(RCtx *c, void *d) { return wi_is_start_of_line_indented(&c->writer_info); }
static int r_isol_or_indented(RCtx *c, void *d)
{
	return wi_is_start_of_line_indented(&c->writer_info) || wi_is_start_of_line(&c->writer_info);
}
static int r_forcing(RCtx *c, void *d) { return rctx_is_forcing_no_newlines(c); }

DP Resolver res_true(void) { return resolver(r_true, NULL); }
DP Resolver res_false(void) { return resolver(r_false, NULL); }
DP Resolver res_is_start_of_line(void) { return resolver(r_isol, NULL); }
DP Resolver res_is_not_start_of_line(void) { return resolver(r_not_isol, NULL); }
DP Resolver res_is_start_of_line_indented(void) { return resolver(r_isol_indented, NULL); }
DP Resolver res_is_start_of_line_or_is_start_of_line_indented(void) { return resolver(r_isol_or_indented, NULL); }
DP Resolver res_is_forcing_no_newlines(void) { return resolver(r_forcing, NULL); }

/* ---- condition_helpers.rs ---- */

DP int ch_is_multiple_lines(RCtx *c, LineNumber start, LineNumber end)
{
	uint32_t s, e;
	if (!rctx_ln(c, start, &s) || !rctx_ln(c, end, &e))
		return -1;
	return e > s;
}

DP int ch_is_hanging(RCtx *c, LineStartIndentLevel start, const LineStartIndentLevel *end)
{
	uint32_t s, e;
	if (!rctx_lsil(c, start, &s))
		return -1;
	if (end) {
		if (!rctx_lsil(c, *end, &e))
			return -1;
	} else {
		e = c->writer_info.line_start_indent_level;
	}
	return e > s;
}

DP int ch_are_line_and_columns_equal(RCtx *c, LineAndColumn a, LineAndColumn b)
{
	uint32_t al, ac, bl, bc;
	if (!rctx_lc(c, a, &al, &ac) || !rctx_lc(c, b, &bl, &bc))
		return -1;
	return al == bl && ac == bc;
}

DP int ch_is_at_same_position(RCtx *c, LineAndColumn lc)
{
	uint32_t l, col;
	if (!rctx_lc(c, lc, &l, &col))
		return -1;
	return l == c->writer_info.line_number && col == c->writer_info.column_number;
}

DP int ch_is_on_same_line(RCtx *c, LineNumber ln)
{
	uint32_t l;
	if (!rctx_ln(c, ln, &l))
		return -1;
	return l == c->writer_info.line_number;
}

DP int ch_is_on_different_line(RCtx *c, LineNumber ln)
{
	uint32_t l;
	if (!rctx_ln(c, ln, &l))
		return -1;
	return l != c->writer_info.line_number;
}

/* ---- conditions.rs ---- */

DP Cond *if_true(const char *name, Resolver r, Items true_path) { return cond_new(name, r, true_path, (Items){0}, true, false); }
DP Cond *if_true_or(const char *name, Resolver r, Items true_path, Items false_path)
{
	return cond_new(name, r, true_path, false_path, true, true);
}
DP Cond *if_false(const char *name, Resolver r, Items false_path) { return cond_new(name, r, (Items){0}, false_path, false, true); }

DP Cond *indent_if_start_of_line(Items items)
{
	PNode *p = items_into_path(items);
	return if_true_or("indentIfStartOfLine", res_is_start_of_line(), with_indent(items_path(p)), items_path(p));
}

DP Cond *indent_if_start_of_line_or_start_of_line_indented(Items items)
{
	PNode *p = items_into_path(items);
	return if_true_or("withIndentIfStartOfLineOrStartOfLineIndented", res_is_start_of_line_or_is_start_of_line_indented(),
			  with_indent(items_path(p)), items_path(p));
}

DP Cond *with_indent_if_start_of_line_indented(Items items)
{
	PNode *p = items_into_path(items);
	return if_true_or("withIndentIfStartOfLineIndented", res_is_start_of_line_indented(), with_indent(items_path(p)),
			  items_path(p));
}

typedef struct {
	LineStartIndentLevel start;
	bool has_end;
	LineStartIndentLevel end;
} HangingData;

static int r_is_hanging(RCtx *c, void *d)
{
	HangingData *h = d;
	return ch_is_hanging(c, h->start, h->has_end ? &h->end : NULL);
}

static Resolver hanging_resolver(LineStartIndentLevel start, const LineStartIndentLevel *end)
{
	HangingData *h = xalloc(sizeof *h);
	h->start = start;
	if (end) {
		h->has_end = true;
		h->end = *end;
	}
	return resolver(r_is_hanging, h);
}

DP Cond *new_line_if_hanging_space_otherwise(LineStartIndentLevel start, const LineStartIndentLevel *end, Items *space_char)
{
	Items sp = space_char ? *space_char : items_str(" ");
	return if_true_or("newLineIfHangingSpaceOtherwise", hanging_resolver(start, end), items_signal(SIG_NEW_LINE), sp);
}

DP Cond *new_line_if_hanging(LineStartIndentLevel start, const LineStartIndentLevel *end)
{
	return if_true("newlineIfHanging", hanging_resolver(start, end), items_signal(SIG_NEW_LINE));
}

typedef struct {
	LineNumber start;
	bool has_end;
	LineNumber end;
} MultiLinesData;

static int r_multiple_lines(RCtx *c, void *d)
{
	MultiLinesData *m = d;
	uint32_t s, e;
	if (!rctx_ln(c, m->start, &s))
		return -1;
	if (m->has_end) {
		if (!rctx_ln(c, m->end, &e))
			return -1;
	} else {
		e = c->writer_info.line_number;
	}
	return e > s;
}

DP Cond *new_line_if_multiple_lines_space_or_new_line_otherwise(LineNumber start, const LineNumber *end)
{
	MultiLinesData *m = xalloc(sizeof *m);
	m->start = start;
	if (end) {
		m->has_end = true;
		m->end = *end;
	}
	return if_true_or("newLineIfMultipleLinesSpaceOrNewLineOtherwise", resolver(r_multiple_lines, m),
			  items_signal(SIG_NEW_LINE), items_signal(SIG_SPACE_OR_NEW_LINE));
}

DP Cond *single_indent_if_start_of_line(void)
{
	return if_true("singleIndentIfStartOfLine", res_is_start_of_line(), items_signal(SIG_SINGLE_INDENT));
}

static int r_above_width(RCtx *c, void *d)
{
	uint32_t width = (uint32_t)(uintptr_t)d;
	uint32_t first_indent_col = wi_line_start_column_number(&c->writer_info) + width;
	return c->writer_info.column_number > first_indent_col;
}

DP Cond *if_above_width_or(uint8_t width, Items true_items, Items false_items)
{
	return cond_new("ifAboveWidth", resolver(r_above_width, (void *)(uintptr_t)width), true_items, false_items, true,
			!items_empty(false_items));
}

DP Cond *if_above_width(uint8_t width, Items items) { return if_above_width_or(width, items, (Items){0}); }

/* ---- actions.rs ---- */

typedef struct {
	ActionFn fn;
	void *data;
} ActionData;

static int r_action(RCtx *c, void *d)
{
	ActionData *a = d;
	a->fn(c, a->data);
	return 1;
}

DP Items action(const char *name, ActionFn fn, void *data)
{
	ActionData *a = xalloc(sizeof *a);
	a->fn = fn;
	a->data = data;
	return items_cond(cond_new(name, resolver(r_action, a), (Items){0}, (Items){0}, false, false));
}

typedef struct {
	ColumnNumber column;
	ActionFn fn;
	void *data;
} ColChangesData;

static void a_col_changes(RCtx *c, void *d)
{
	ColChangesData *x = d;
	uint32_t col;
	if (rctx_cn(c, x->column, &col) && col != c->writer_info.column_number)
		x->fn(c, x->data);
}

DP Items if_column_number_changes(ActionFn fn, void *data)
{
	Items items = {0};
	ColChangesData *x = xalloc(sizeof *x);
	x->column = cn_new();
	x->fn = fn;
	x->data = data;
	items_extend(&items, action("actionIfColChanges", a_col_changes, x));
	items_push_cn(&items, x->column);
	return items;
}

/* ---- ir_helpers/helpers.rs ---- */

DP Items surround_with_new_lines(Items item)
{
	if (items_empty(item))
		return item;
	Items items = {0};
	items_push_signal(&items, SIG_NEW_LINE);
	items_extend(&items, item);
	items_push_signal(&items, SIG_NEW_LINE);
	return items;
}

DP Items with_indent_times(Items item, int times)
{
	if (items_empty(item))
		return item;
	Items items = {0};
	for (int i = 0; i < times; i++)
		items_push_signal(&items, SIG_START_INDENT);
	items_extend(&items, item);
	for (int i = 0; i < times; i++)
		items_push_signal(&items, SIG_FINISH_INDENT);
	return items;
}

DP Items with_indent(Items item) { return with_indent_times(item, 1); }

static Items surround_signals(Items item, Signal a, Signal b)
{
	if (items_empty(item))
		return item;
	Items items = {0};
	items_push_signal(&items, a);
	items_extend(&items, item);
	items_push_signal(&items, b);
	return items;
}

DP Items with_queued_indent(Items item) { return surround_signals(item, SIG_QUEUE_START_INDENT, SIG_FINISH_INDENT); }
DP Items with_no_new_lines(Items item)
{
	return surround_signals(item, SIG_START_FORCE_NO_NEW_LINES, SIG_FINISH_FORCE_NO_NEW_LINES);
}
DP Items new_line_group(Items item) { return surround_signals(item, SIG_START_NEW_LINE_GROUP, SIG_FINISH_NEW_LINE_GROUP); }

/* gen_from_string_line: splits at tabs. */
static Items gen_from_string_line(const char *s, size_t n, bool trim_end)
{
	if (trim_end) {
		/* str::trim_end: Unicode White_Space; generated text only has ASCII whitespace. */
		while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n' ||
				 s[n - 1] == '\v' || s[n - 1] == '\f'))
			n--;
	}
	Items items = {0};
	size_t start = 0;
	for (size_t i = 0; i <= n; i++) {
		if (i == n || s[i] == '\t') {
			if (start > 0 || i < n) {
				/* not the first piece: preceded by a tab */
			}
			if (i > start)
				items_push_strn(&items, s + start, i - start);
			if (i < n)
				items_push_signal(&items, SIG_TAB);
			start = i + 1;
		}
	}
	return items;
}

/* str::lines(): splits at \n, strips a trailing \r of each line, no final empty line. */
static Items gen_string_lines(const char *s, size_t n, bool trim_end)
{
	Items items = {0};
	size_t start = 0;
	int i = 0;
	while (start < n) {
		const char *nl = memchr(s + start, '\n', n - start);
		size_t end = nl ? (size_t)(nl - s) : n;
		size_t len = end - start;
		if (nl && len > 0 && s[end - 1] == '\r')
			len--;
		if (i > 0)
			items_push_signal(&items, SIG_NEW_LINE);
		items_extend(&items, gen_from_string_line(s + start, len, trim_end));
		i++;
		start = nl ? end + 1 : n;
	}
	if (n > 0 && s[n - 1] == '\n')
		items_push_signal(&items, SIG_NEW_LINE);
	return items;
}

static Items gen_from_raw_string_lines(const char *s, size_t n, bool trim_end)
{
	bool add_ignore_indent = memchr(s, '\n', n) != NULL;
	Items items = {0};
	if (add_ignore_indent)
		items_push_signal(&items, SIG_START_IGNORING_INDENT);
	items_extend(&items, gen_string_lines(s, n, trim_end));
	if (add_ignore_indent)
		items_push_signal(&items, SIG_FINISH_IGNORING_INDENT);
	return items;
}

DP Items gen_from_raw_string(const char *s, size_t n) { return gen_from_raw_string_lines(s, n, false); }
DP Items gen_from_raw_string_trim_line_ends(const char *s, size_t n) { return gen_from_raw_string_lines(s, n, true); }
DP Items gen_from_string(const char *s, size_t n) { return gen_string_lines(s, n, false); }
DP Items gen_from_string_trim_line_ends(const char *s, size_t n) { return gen_string_lines(s, n, true); }

typedef struct {
	LineNumber start_ln, end_ln;
} SurroundData;

static void a_clear_end_ln(RCtx *c, void *d) { rctx_clear(c, ((SurroundData *)d)->end_ln.id); }

static int r_newline_if_multi_line(RCtx *c, void *d)
{
	SurroundData *s = d;
	return ch_is_multiple_lines(c, s->start_ln, s->end_ln);
}

DP Items surround_with_newlines_indented_if_multi_line(Items inner, uint8_t indent_width)
{
	if (items_empty(inner))
		return inner;
	Items items = {0};
	SurroundData *d = xalloc(sizeof *d);
	d->start_ln = ln_new();
	d->end_ln = ln_new();
	PNode *inner_path = items_into_path(inner);
	items_push_ln(&items, d->start_ln);
	items_push_anchor(&items, d->end_ln);
	items_extend(&items, if_column_number_changes(a_clear_end_ln, d));
	Items false_path = {0};
	items_push_cond(&false_path, if_above_width(indent_width, items_signal(SIG_POSSIBLE_NEW_LINE)));
	items_push_path(&false_path, inner_path);
	Cond *cond = cond_new("newlineIfMultiLine", resolver(r_newline_if_multi_line, d),
			      surround_with_new_lines(with_indent(items_path(inner_path))), false_path, true, true);
	ConditionReevaluation re = cond_create_reevaluation(cond);
	items_push_cond(&items, cond);
	items_push_ln(&items, d->end_ln);
	items_push_reevaluation(&items, re);
	return items;
}

static bool is_rust_whitespace(uint32_t c)
{
	return (c >= 0x09 && c <= 0x0d) || c == ' ' || c == 0x85 || c == 0xa0 || c == 0x1680 ||
	       (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

/* Length of s without trailing Unicode whitespace (str::trim_end). */
static size_t rust_trim_end_len(const char *s, size_t n)
{
	size_t end = n;
	while (end > 0) {
		size_t i = end - 1;
		while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
			i--;
		uint32_t c;
		utf8_decode(s + i, end - i, &c);
		if (!is_rust_whitespace(c))
			break;
		end = i;
	}
	return end;
}

DP Items gen_js_like_comment_line(const char *text, size_t n, bool force_space_after_slashes)
{
	size_t non_slash = 0;
	while (non_slash < n && text[non_slash] == '/')
		non_slash++;
	bool skip_space = force_space_after_slashes && non_slash < n && text[non_slash] == ' ';
	size_t start = skip_space ? non_slash + 1 : non_slash;
	const char *ct = text + start;
	size_t cn = rust_trim_end_len(ct, n - start);
	Buf b = {0};
	buf_adds(&b, "//");
	buf_add(&b, text, non_slash);
	if (cn > 0) {
		if (force_space_after_slashes)
			buf_addc(&b, ' ');
		buf_add(&b, ct, cn);
	}
	Items items = {0};
	items_extend(&items, gen_from_raw_string(b.p, b.n));
	items_push_signal(&items, SIG_EXPECT_NEW_LINE);
	return with_no_new_lines(items);
}

DP Items gen_js_like_comment_block(const char *text, size_t n)
{
	Items items = {0};
	bool add_ignore_indent = memchr(text, '\n', n) != NULL;
	size_t trimmed = rust_trim_end_len(text, n);
	const char *end_text = text + trimmed;
	size_t end_n = n - trimmed;
	const char *last_ws = end_text;
	size_t last_n = end_n;
	for (size_t i = end_n; i > 0; i--) {
		if (end_text[i - 1] == '\n') {
			last_ws = end_text + i;
			last_n = end_n - i;
			break;
		}
	}
	items_push_str(&items, "/*");
	if (add_ignore_indent)
		items_push_signal(&items, SIG_START_IGNORING_INDENT);
	items_extend(&items, gen_from_string_trim_line_ends(text, n));
	if (last_n > 0)
		items_extend(&items, gen_from_raw_string(last_ws, last_n));
	if (add_ignore_indent)
		items_push_signal(&items, SIG_FINISH_IGNORING_INDENT);
	items_push_str(&items, "*/");
	return items;
}

static bool is_alnum_at(const char *text, size_t n, size_t pos)
{
	if (pos >= n)
		return false;
	uint32_t c;
	utf8_decode(text + pos, n - pos, &c);
	if (c < 0x80)
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	return true; /* char::is_alphanumeric for non-ASCII: approximation */
}

DP bool text_has_dprint_ignore(const char *text, size_t n, const char *searching)
{
	size_t sn = strlen(searching);
	for (size_t pos = 0; pos + sn <= n; pos++) {
		if (memcmp(text + pos, searching, sn) == 0) {
			/* only the first occurrence counts (str::find) */
			if (pos > 0 && is_alnum_at(text, n, pos - 1))
				return false;
			if (is_alnum_at(text, n, pos + sn))
				return false;
			return true;
		}
	}
	return false;
}
