/* Port of dprint-core 0.62.1 src/formatting/ir_helpers/gen_separated_values.rs.
 *
 * dprint-core: Copyright (c) 2019-2023 David Sherret, MIT License. */

DP SingleLineOptions slo_separated_same_line(Items separator) { return (SingleLineOptions){false, false, separator}; }
DP SingleLineOptions slo_same_line_maybe_space_separated(void)
{
	return slo_separated_same_line(items_signal(SIG_SPACE_OR_NEW_LINE));
}
DP SingleLineOptions slo_separated_line_starting_with_space(void)
{
	return (SingleLineOptions){true, false, items_signal(SIG_SPACE_OR_NEW_LINE)};
}
DP SingleLineOptions slo_surrounded_line(void) { return (SingleLineOptions){true, true, items_signal(SIG_SPACE_OR_NEW_LINE)}; }

static MultiLineOptions mlo(bool start, bool end, bool indent, bool hanging, bool maintain)
{
	MultiLineOptions o = {0};
	o.newline_at_start = start;
	o.newline_at_end = end;
	o.with_indent = indent;
	o.with_hanging_indent = hanging;
	o.maintain_line_breaks = maintain;
	return o;
}
DP MultiLineOptions mlo_new_line_start(void) { return mlo(true, false, true, false, false); }
DP MultiLineOptions mlo_surround_newlines_indented(void) { return mlo(true, true, true, false, false); }
DP MultiLineOptions mlo_same_line_start_hanging_indent(void) { return mlo(false, false, false, true, false); }
DP MultiLineOptions mlo_same_line_no_indent(void) { return mlo(false, false, false, false, false); }
DP MultiLineOptions mlo_maintain_line_breaks(void) { return mlo(false, false, false, false, true); }

DP void gv_push(GeneratedValues *vs, GeneratedValue v)
{
	if (vs->n == vs->cap) {
		int cap = vs->cap ? vs->cap * 2 : 8;
		vs->v = xrealloc(vs->v, sizeof(GeneratedValue) * (size_t)vs->cap, sizeof(GeneratedValue) * (size_t)cap);
		vs->cap = cap;
	}
	vs->v[vs->n++] = v;
}

typedef struct {
	IsStartOfLine is_start_of_line;
	LineNumber line_number;
	LineStartIndentLevel line_start_indent_level;
	bool allow_inline_multi_line, allow_inline_single_line;
} GeneratedValueData;

typedef struct {
	GeneratedValueData *v;
	int n, cap;
} ValueDatas;

static void vd_push(ValueDatas *vs, GeneratedValueData d)
{
	if (vs->n == vs->cap) {
		int cap = vs->cap ? vs->cap * 2 : 8;
		vs->v = xrealloc(vs->v, sizeof(GeneratedValueData) * (size_t)vs->cap, sizeof(GeneratedValueData) * (size_t)cap);
		vs->cap = cap;
	}
	vs->v[vs->n++] = d;
}

/* clear_resolutions_on_position_change */
typedef struct {
	ValueDatas *value_datas;
	LineNumber end_ln;
	ColumnNumber column;
	LineNumber line;
} ClearData;

static int r_clear_when_position_changes(RCtx *c, void *d)
{
	ClearData *x = d;
	uint32_t col, line;
	if (!rctx_cn(c, x->column, &col))
		return -1;
	if (!rctx_ln(c, x->line, &line))
		return -1;
	if (col != c->writer_info.column_number || line != c->writer_info.line_number) {
		for (int i = 0; i < x->value_datas->n; i++) {
			rctx_clear(c, x->value_datas->v[i].line_number.id);
			rctx_clear(c, x->value_datas->v[i].is_start_of_line.id);
			rctx_clear(c, x->value_datas->v[i].line_start_indent_level.id);
		}
		rctx_clear(c, x->end_ln.id);
	}
	return -1;
}

static Items clear_resolutions_on_position_change(ValueDatas *value_datas, LineNumber end_ln)
{
	Items items = {0};
	ClearData *x = xalloc(sizeof *x);
	x->value_datas = value_datas;
	x->end_ln = end_ln;
	x->column = cn_new();
	x->line = ln_new();
	items_push_cond(&items, cond_new("clearWhenPositionChanges", resolver(r_clear_when_position_changes, x), (Items){0},
					 (Items){0}, false, false));
	items_push_ln(&items, x->line);
	items_push_cn(&items, x->column);
	return items;
}

typedef struct {
	ColumnNumber start_cn;
	LineStartColumnNumber start_lscn;
} StandaloneData;

static int r_is_start_standalone_line(RCtx *c, void *d)
{
	StandaloneData *x = d;
	uint32_t cn, lscn;
	bool has_cn = rctx_cn(c, x->start_cn, &cn);
	bool has_lscn = rctx_lscn(c, x->start_lscn, &lscn);
	if (!has_cn || !has_lscn)
		return -1;
	return cn == lscn;
}

typedef struct {
	ValueDatas *value_datas;
	ConditionReference standalone;
} HangingMultiData;

static int r_is_multi_line_for_hanging(RCtx *c, void *d)
{
	HangingMultiData *x = d;
	int standalone = rctx_condition(c, x->standalone);
	if (standalone < 0)
		return -1;
	if (standalone) {
		if (x->value_datas->n > 1)
			return rctx_isol(c, x->value_datas->v[1].is_start_of_line);
	} else {
		if (x->value_datas->n > 0)
			return rctx_isol(c, x->value_datas->v[0].is_start_of_line);
	}
	return 0;
}

typedef struct {
	LineNumber start_ln, end_ln;
	ValueDatas *value_datas;
	ConditionReference standalone;
	bool last_result;
} MultiLineData;

static bool check_value_should_make_multi_line(bool is_multi_line_value, bool allows_multi_line, bool allows_single_line,
					       bool has_multi_line_value)
{
	if (is_multi_line_value)
		return !allows_multi_line;
	return has_multi_line_value && !allows_single_line;
}

static int ml_evaluate(MultiLineData *x, RCtx *c)
{
	int standalone = rctx_condition(c, x->standalone);
	if (standalone < 0)
		return -1;
	uint32_t start_ln, end_ln;
	if (!rctx_ln(c, x->start_ln, &start_ln))
		return -1;
	if (!rctx_ln(c, x->end_ln, &end_ln))
		return -1;
	uint32_t last_ln = start_ln;
	bool last_allows_multi_line = true, last_allows_single_line = false, has_multi_line_value = false;
	for (int i = 0; i < x->value_datas->n; i++) {
		GeneratedValueData *vd = &x->value_datas->v[i];
		if (i == 0 && standalone)
			continue;
		int isol = rctx_isol(c, vd->is_start_of_line);
		if (isol < 0)
			return -1;
		if (isol)
			return 1;
		uint32_t value_start_ln;
		if (!rctx_ln(c, vd->line_number, &value_start_ln))
			return -1;
		if (i >= 1) {
			bool last_is_multi_line_value = last_ln < value_start_ln;
			if (last_is_multi_line_value)
				has_multi_line_value = true;
			if (check_value_should_make_multi_line(last_is_multi_line_value, last_allows_multi_line,
							       last_allows_single_line, has_multi_line_value))
				return 1;
		}
		last_ln = value_start_ln;
		last_allows_multi_line = vd->allow_inline_multi_line;
		last_allows_single_line = vd->allow_inline_single_line;
	}
	bool last_is_multi_line_value = last_ln < end_ln;
	if (last_is_multi_line_value)
		has_multi_line_value = true;
	return check_value_should_make_multi_line(last_is_multi_line_value, last_allows_multi_line, last_allows_single_line,
						  has_multi_line_value);
}

static int r_is_multi_line_for_multi_line(RCtx *c, void *d)
{
	MultiLineData *x = d;
	int result = ml_evaluate(x, c);
	if (x->last_result && result == 0)
		return 1;
	x->last_result = result == 1;
	return result;
}

typedef struct {
	ConditionReference standalone;
} NotStandaloneData;

static int r_not_standalone(RCtx *c, void *d)
{
	int v = rctx_condition(c, ((NotStandaloneData *)d)->standalone);
	if (v < 0)
		return -1;
	return !v;
}

typedef struct {
	LineNumber first;
} DiffLineData;

static int r_is_on_different_line(RCtx *c, void *d) { return ch_is_on_different_line(c, ((DiffLineData *)d)->first); }

static Items value_infos_and_items(LineNumber ln, IsStartOfLine isol, LineStartIndentLevel lsil, PNode *value)
{
	Items items = {0};
	items_push_ln(&items, ln);
	items_push_isol(&items, isol);
	items_push_lsil(&items, lsil);
	items_push_path(&items, value);
	return items;
}

static Items inner_gen(GeneratedValues values, Resolver is_multi_line, Items single_line_separator_items,
		       const MultiLineOptions *mlopts, bool allow_blank_lines, ValueDatas *out)
{
	Items items = {0};
	int values_count = values.n;
	PNode *single_line_separator = items_into_path(single_line_separator_items);
	LinesSpan last_lines_span = {0};
	bool maintain_line_breaks = mlopts->maintain_line_breaks;
	bool had_newline = false;
	LineNumber first_line_number = ln_new();
	bool has_last_lsil = false;
	LineStartIndentLevel last_lsil = {0};

	for (int i = 0; i < values.n; i++) {
		GeneratedValue *gv = &values.v[i];
		LineNumber start_ln = i == 0 ? first_line_number : ln_new();
		IsStartOfLine start_isol = isol_new();
		LineStartIndentLevel start_lsil = lsil_new();
		vd_push(out, (GeneratedValueData){start_isol, start_ln, start_lsil, gv->allow_inline_multi_line,
						  gv->allow_inline_single_line});
		if (i == 0) {
			if (mlopts->newline_at_start && values_count > 1)
				items_push_cond(&items, if_false("isNotStartOfLine", res_is_start_of_line(),
								 items_signal(SIG_POSSIBLE_NEW_LINE)));
			items_push_ln(&items, start_ln);
			items_push_isol(&items, start_isol);
			items_push_lsil(&items, start_lsil);
			items_extend(&items, gv->items);
		} else {
			bool has_new_line = false, has_blank_line = false;
			if (last_lines_span.has && gv->lines_span.has) {
				size_t cur_start = gv->lines_span.start_line;
				has_new_line = last_lines_span.end_line < cur_start;
				size_t m = cur_start > 1 ? cur_start : 1;
				has_blank_line = last_lines_span.end_line < m - 1;
			}
			bool use_blank_line = allow_blank_lines && has_blank_line;
			PNode *value = items_into_path(gv->items);
			Items true_path = {0};
			if (use_blank_line)
				items_push_signal(&true_path, SIG_NEW_LINE);
			if (!maintain_line_breaks || has_new_line) {
				items_push_signal(&true_path, SIG_NEW_LINE);
				had_newline = true;
			} else {
				Items space_or_newline;
				if (has_last_lsil) {
					LineStartIndentLevel *l = xalloc(sizeof *l);
					*l = last_lsil;
					space_or_newline = items_cond(if_true_or("newlineIfHanging", hanging_resolver(*l, NULL),
										 items_signal(SIG_NEW_LINE),
										 items_path(single_line_separator)));
				} else {
					space_or_newline = items_path(single_line_separator);
				}
				if (i == values_count - 1 && !had_newline) {
					DiffLineData *dl = xalloc(sizeof *dl);
					dl->first = first_line_number;
					items_push_cond(&true_path, if_true_or("forcedNewLineIfNoNewLine",
									       resolver(r_is_on_different_line, dl),
									       space_or_newline, items_signal(SIG_NEW_LINE)));
				} else {
					items_extend(&true_path, space_or_newline);
				}
			}
			if (!mlopts->hanging_is_condition) {
				if (mlopts->with_hanging_indent)
					items_push_cond(&true_path, indent_if_start_of_line(value_infos_and_items(
									    start_ln, start_isol, start_lsil, value)));
				else
					items_extend(&true_path, value_infos_and_items(start_ln, start_isol, start_lsil, value));
			} else {
				PNode *inner = items_into_path(value_infos_and_items(start_ln, start_isol, start_lsil, value));
				items_push_cond(&true_path, cond_new("valueHangingIndent", mlopts->hanging_condition,
								     with_indent(items_path(inner)), items_path(inner), true,
								     true));
			}
			Items false_path = {0};
			items_push_path(&false_path, single_line_separator);
			items_push_cond(&false_path,
					indent_if_start_of_line(value_infos_and_items(start_ln, start_isol, start_lsil, value)));
			items_push_cond(&items, cond_new("multiLineOrHangingCondition", is_multi_line, true_path, false_path, true,
							 true));
		}
		last_lines_span = gv->lines_span;
		has_last_lsil = true;
		last_lsil = start_lsil;
	}
	return items;
}

DP GenSeparatedValuesResult gen_separated_values(GenValuesFn values_fn, void *data, GenSeparatedValuesOptions opts)
{
	uint8_t indent_width = opts.indent_width;
	LineNumber start_ln = ln_new();
	ColumnNumber start_cn = cn_new();
	LineStartColumnNumber start_lscn = lscn_new();
	LineNumber end_ln = ln_new();
	ValueDatas *value_datas = xalloc(sizeof *value_datas);
	MultiLineOptions mlopts = opts.multi_line_options;

	StandaloneData *sd = xalloc(sizeof *sd);
	sd->start_cn = start_cn;
	sd->start_lscn = start_lscn;
	Cond *is_start_standalone_line = cond_new("isStartStandaloneLine", resolver(r_is_start_standalone_line, sd),
						  (Items){0}, (Items){0}, false, false);
	ConditionReference standalone_ref = cond_create_reference(is_start_standalone_line);

	Cond *is_multi_line_condition;
	if (opts.force_use_new_lines) {
		is_multi_line_condition = cond_new_true();
	} else if (opts.prefer_hanging) {
		if (!mlopts.newline_at_start) {
			is_multi_line_condition = cond_new_false();
		} else {
			HangingMultiData *h = xalloc(sizeof *h);
			h->value_datas = value_datas;
			h->standalone = standalone_ref;
			is_multi_line_condition = cond_new("isMultiLineForHanging", resolver(r_is_multi_line_for_hanging, h),
							   (Items){0}, (Items){0}, false, false);
		}
	} else {
		MultiLineData *m = xalloc(sizeof *m);
		m->start_ln = start_ln;
		m->end_ln = end_ln;
		m->value_datas = value_datas;
		m->standalone = standalone_ref;
		is_multi_line_condition = cond_new("isMultiLineForMultiLine", resolver(r_is_multi_line_for_multi_line, m),
						   (Items){0}, (Items){0}, false, false);
	}
	ConditionReevaluation is_multi_line_reevaluation = cond_create_reevaluation(is_multi_line_condition);
	ConditionReference is_multi_line_ref = cond_create_reference(is_multi_line_condition);
	Resolver is_multi_line = condref_resolver(is_multi_line_ref);

	Items items = {0};
	items_push_cn(&items, start_cn);
	items_push_lscn(&items, start_lscn);
	items_push_ln(&items, start_ln);
	items_extend(&items, clear_resolutions_on_position_change(value_datas, end_ln));
	items_push_cond(&items, is_start_standalone_line);
	items_push_cond(&items, is_multi_line_condition);

	GeneratedValues values = values_fn(is_multi_line_ref, data);
	bool has_values = values.n > 0;
	ValueDatas inner_datas = {0};
	Items inner_items = inner_gen(values, is_multi_line, opts.single_line_options.separator, &mlopts,
				      opts.allow_blank_lines, &inner_datas);
	for (int i = 0; i < inner_datas.n; i++)
		vd_push(value_datas, inner_datas.v[i]);
	PNode *gen_items = items_into_path(inner_items);

	Items multi = {0};
	if (mlopts.newline_at_start)
		items_push_signal(&multi, SIG_NEW_LINE);
	if (mlopts.with_indent)
		items_push_signal(&multi, SIG_START_INDENT);
	items_push_path(&multi, gen_items);
	if (mlopts.with_indent)
		items_push_signal(&multi, SIG_FINISH_INDENT);
	if (mlopts.newline_at_end)
		items_push_signal(&multi, SIG_NEW_LINE);
	NotStandaloneData *ns = xalloc(sizeof *ns);
	ns->standalone = standalone_ref;
	Items true_path = items_cond(if_true_or("newLineIndentedIfNotStandalone", resolver(r_not_standalone, ns), multi,
						items_path(gen_items)));

	Items false_path = {0};
	bool has_start_space = opts.single_line_options.space_at_start;
	if (has_start_space) {
		items_push_signal(&false_path, SIG_SPACE_IF_NOT_TRAILING);
		items_push_signal(&false_path, SIG_POSSIBLE_NEW_LINE);
	}
	if (has_values && mlopts.newline_at_start) {
		uint8_t w = opts.force_possible_newline_at_start ? 0 : (uint8_t)(indent_width + (has_start_space ? 1 : 0));
		items_push_cond(&false_path, if_above_width(w, items_signal(SIG_POSSIBLE_NEW_LINE)));
	}
	items_push_path(&false_path, gen_items);
	if (opts.single_line_options.space_at_end)
		items_push_str(&false_path, " ");

	items_push_cond(&items, cond_new("multiLineOrHanging", is_multi_line, true_path, false_path, true, true));
	items_push_ln(&items, end_ln);
	items_push_reevaluation(&items, is_multi_line_reevaluation);

	GenSeparatedValuesResult r;
	r.items = items;
	r.is_multi_line_condition_ref = is_multi_line_ref;
	return r;
}
