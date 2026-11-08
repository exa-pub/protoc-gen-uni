/* Port of the dprint-core 0.62.1 formatting IR (src/formatting/print_items.rs, conditions.rs,
 * condition_helpers.rs, condition_resolvers.rs, actions.rs, ir_helpers/{helpers,gen_separated_values}.rs) and printer
 * (printer.rs, writer.rs, write_items.rs, print.rs, infinite_reevaluation_protection.rs).
 *
 * dprint-core: Copyright (c) 2019-2023 David Sherret, MIT License (see ../README.md).
 *
 * Differences of representation (behaviour is the same):
 * - every info, condition, anchor and reevaluation takes its id from one counter, so the printer
 *   keeps one table per kind of value instead of one hash map per kind;
 * - closures are a function pointer plus a data pointer (Resolver);
 * - the look-ahead save point maps are immutable sorted arrays shared by save points
 *   (copy on write), the next-node stack is a persistent list as in Rust. */
#ifndef DPRINT_IR_H
#define DPRINT_IR_H

#include "util.h"

/* Everything is static in the unity build (dprint_typescript.c); helpers may be unused. */
#define DP static __attribute__((unused))

typedef struct PNode PNode;
typedef struct Cond Cond;
typedef struct RCtx RCtx;

/* PrintItems: a singly linked list of print nodes. */
typedef struct {
	PNode *first, *last;
} Items;

typedef enum {
	SIG_NEW_LINE,
	SIG_TAB,
	SIG_POSSIBLE_NEW_LINE,
	SIG_SPACE_OR_NEW_LINE,
	SIG_EXPECT_NEW_LINE,
	SIG_QUEUE_START_INDENT,
	SIG_START_INDENT,
	SIG_FINISH_INDENT,
	SIG_START_NEW_LINE_GROUP,
	SIG_FINISH_NEW_LINE_GROUP,
	SIG_SINGLE_INDENT,
	SIG_START_IGNORING_INDENT,
	SIG_FINISH_IGNORING_INDENT,
	SIG_START_FORCE_NO_NEW_LINES,
	SIG_FINISH_FORCE_NO_NEW_LINES,
	SIG_SPACE_IF_NOT_TRAILING,
} Signal;

typedef enum {
	INFO_LINE_NUMBER,
	INFO_COLUMN_NUMBER,
	INFO_IS_START_OF_LINE,
	INFO_INDENT_LEVEL,
	INFO_LINE_START_COLUMN_NUMBER,
	INFO_LINE_START_INDENT_LEVEL,
} InfoKind;

/* Typed info handles (the id is unique among all ids). */
typedef struct {
	uint32_t id;
} LineNumber;
typedef struct {
	uint32_t id;
} ColumnNumber;
typedef struct {
	uint32_t id;
} IsStartOfLine;
typedef struct {
	uint32_t id;
} IndentLevel;
typedef struct {
	uint32_t id;
} LineStartColumnNumber;
typedef struct {
	uint32_t id;
} LineStartIndentLevel;
typedef struct {
	LineNumber line;
	ColumnNumber column;
} LineAndColumn;

typedef struct {
	uint32_t id;
} ConditionReference;

typedef struct {
	uint32_t id;	    /* the reevaluation's own id */
	uint32_t condition; /* the condition's id */
} ConditionReevaluation;

/* Condition resolver result: -1 = None, 0 = Some(false), 1 = Some(true). */
typedef int (*ResolveFn)(RCtx *ctx, void *data);
typedef struct {
	ResolveFn fn;
	void *data;
} Resolver;

typedef struct {
	const char *text;
	uint32_t len;
	uint32_t width; /* unicode_width of the text */
} StrItem;

enum { PI_STRING, PI_CONDITION, PI_SIGNAL, PI_PATH, PI_ANCHOR, PI_INFO, PI_REEVALUATION };

struct PNode {
	PNode *next;
	uint8_t kind;
	uint8_t sub; /* Signal or InfoKind */
	uint32_t id; /* info id, anchor id or reevaluation id */
	uint32_t id2; /* anchor: line number id; reevaluation: condition id */
	union {
		StrItem *str;
		Cond *cond;
		PNode *path;
	} u;
};

struct Cond {
	uint32_t id;
	const char *name;
	bool is_stored;
	bool store_save_point;
	Resolver resolver;
	PNode *true_path, *false_path;
};

typedef struct {
	uint32_t line_number;
	uint32_t column_number;
	uint8_t indent_level;
	uint8_t line_start_indent_level;
	uint8_t indent_width;
	bool expect_newline_next;
} WriterInfo;

/* ---- ids ---- */
DP LineNumber ln_new(void);
DP ColumnNumber cn_new(void);
DP IsStartOfLine isol_new(void);
DP IndentLevel il_new(void);
DP LineStartColumnNumber lscn_new(void);
DP LineStartIndentLevel lsil_new(void);
DP LineAndColumn lc_new(void);

/* ---- PrintItems ---- */
static inline bool items_empty(Items it) { return it.first == NULL; }
DP void items_push_str(Items *it, const char *s);			 /* static or arena string */
DP void items_push_strn(Items *it, const char *s, size_t n);	 /* copied */
DP void items_push_signal(Items *it, Signal s);
DP void items_push_cond(Items *it, Cond *c);
DP void items_push_path(Items *it, PNode *path); /* no-op for NULL */
DP void items_push_ln(Items *it, LineNumber x);
DP void items_push_cn(Items *it, ColumnNumber x);
DP void items_push_isol(Items *it, IsStartOfLine x);
DP void items_push_il(Items *it, IndentLevel x);
DP void items_push_lscn(Items *it, LineStartColumnNumber x);
DP void items_push_lsil(Items *it, LineStartIndentLevel x);
DP void items_push_lc(Items *it, LineAndColumn x);
DP void items_push_anchor(Items *it, LineNumber x); /* LineNumberAnchor::new(x) */
DP void items_push_reevaluation(Items *it, ConditionReevaluation r);
DP void items_extend(Items *it, Items other);
/* Conversions (From<...> for PrintItems). */
DP Items items_str(const char *s);
DP Items items_signal(Signal s);
DP Items items_cond(Cond *c);
DP Items items_path(PNode *path);
static inline PNode *items_into_path(Items it) { return it.first; }

/* ---- Condition ---- */
DP Cond *cond_new(const char *name, Resolver r, Items true_path, Items false_path, bool has_true, bool has_false);
DP Cond *cond_new_true(void);
DP Cond *cond_new_false(void);
DP ConditionReference cond_create_reference(Cond *c);
DP ConditionReevaluation cond_create_reevaluation(Cond *c);
DP Resolver condref_resolver(ConditionReference r);
DP Resolver resolver(ResolveFn fn, void *data);

/* ---- resolver context ---- */
struct RCtx {
	struct Printer *printer;
	WriterInfo writer_info;
};
DP int rctx_condition(RCtx *c, ConditionReference r); /* -1 none */
DP bool rctx_ln(RCtx *c, LineNumber x, uint32_t *out);
DP bool rctx_cn(RCtx *c, ColumnNumber x, uint32_t *out);
DP bool rctx_lc(RCtx *c, LineAndColumn x, uint32_t *line, uint32_t *col);
DP int rctx_isol(RCtx *c, IsStartOfLine x); /* -1 none */
DP bool rctx_il(RCtx *c, IndentLevel x, uint32_t *out);
DP bool rctx_lscn(RCtx *c, LineStartColumnNumber x, uint32_t *out);
DP bool rctx_lsil(RCtx *c, LineStartIndentLevel x, uint32_t *out);
DP void rctx_clear(RCtx *c, uint32_t info_id);
DP bool rctx_is_forcing_no_newlines(RCtx *c);
DP bool wi_is_start_of_line(const WriterInfo *w);
DP bool wi_is_start_of_line_indented(const WriterInfo *w);
DP bool wi_is_column_number_at_line_start(const WriterInfo *w);
DP uint32_t wi_line_start_column_number(const WriterInfo *w);

/* ---- condition_resolvers.rs ---- */
DP Resolver res_true(void);
DP Resolver res_false(void);
DP Resolver res_is_start_of_line(void);
DP Resolver res_is_not_start_of_line(void);
DP Resolver res_is_start_of_line_indented(void);
DP Resolver res_is_start_of_line_or_is_start_of_line_indented(void);
DP Resolver res_is_forcing_no_newlines(void);

/* ---- condition_helpers.rs ---- */
DP int ch_is_multiple_lines(RCtx *c, LineNumber start, LineNumber end);
DP int ch_is_hanging(RCtx *c, LineStartIndentLevel start, const LineStartIndentLevel *end);
DP int ch_are_line_and_columns_equal(RCtx *c, LineAndColumn a, LineAndColumn b);
DP int ch_is_at_same_position(RCtx *c, LineAndColumn lc);
DP int ch_is_on_same_line(RCtx *c, LineNumber ln);
DP int ch_is_on_different_line(RCtx *c, LineNumber ln);

/* ---- conditions.rs ---- */
DP Cond *if_true(const char *name, Resolver r, Items true_path);
DP Cond *if_true_or(const char *name, Resolver r, Items true_path, Items false_path);
DP Cond *if_false(const char *name, Resolver r, Items false_path);
DP Cond *indent_if_start_of_line(Items items);
DP Cond *indent_if_start_of_line_or_start_of_line_indented(Items items);
DP Cond *with_indent_if_start_of_line_indented(Items items);
DP Cond *new_line_if_hanging_space_otherwise(LineStartIndentLevel start, const LineStartIndentLevel *end,
					  Items *space_char /* NULL: " " */);
DP Cond *new_line_if_hanging(LineStartIndentLevel start, const LineStartIndentLevel *end);
DP Cond *new_line_if_multiple_lines_space_or_new_line_otherwise(LineNumber start, const LineNumber *end);
DP Cond *single_indent_if_start_of_line(void);
DP Cond *if_above_width(uint8_t width, Items items);
DP Cond *if_above_width_or(uint8_t width, Items true_items, Items false_items);

/* ---- actions.rs ---- */
typedef void (*ActionFn)(RCtx *ctx, void *data);
DP Items action(const char *name, ActionFn fn, void *data);
DP Items if_column_number_changes(ActionFn fn, void *data);

/* ---- ir_helpers/helpers.rs ---- */
DP Items surround_with_new_lines(Items it);
DP Items with_indent(Items it);
DP Items with_queued_indent(Items it);
DP Items with_indent_times(Items it, int times);
DP Items with_no_new_lines(Items it);
DP Items new_line_group(Items it);
DP Items gen_from_raw_string(const char *s, size_t n);
DP Items gen_from_raw_string_trim_line_ends(const char *s, size_t n);
DP Items gen_from_string(const char *s, size_t n);
DP Items gen_from_string_trim_line_ends(const char *s, size_t n);
DP Items surround_with_newlines_indented_if_multi_line(Items inner, uint8_t indent_width);
DP Items gen_js_like_comment_line(const char *text, size_t n, bool force_space_after_slashes);
DP Items gen_js_like_comment_block(const char *text, size_t n);
DP bool text_has_dprint_ignore(const char *text, size_t n, const char *searching);

/* ---- ir_helpers/gen_separated_values.rs ---- */
typedef struct {
	bool space_at_start, space_at_end;
	Items separator;
} SingleLineOptions;
DP SingleLineOptions slo_separated_same_line(Items separator);
DP SingleLineOptions slo_same_line_maybe_space_separated(void);
DP SingleLineOptions slo_separated_line_starting_with_space(void);
DP SingleLineOptions slo_surrounded_line(void);

typedef struct {
	bool newline_at_start, newline_at_end, with_indent;
	bool hanging_is_condition; /* with_hanging_indent: BoolOrCondition */
	bool with_hanging_indent;
	Resolver hanging_condition;
	bool maintain_line_breaks;
} MultiLineOptions;
DP MultiLineOptions mlo_new_line_start(void);
DP MultiLineOptions mlo_surround_newlines_indented(void);
DP MultiLineOptions mlo_same_line_start_hanging_indent(void);
DP MultiLineOptions mlo_same_line_no_indent(void);
DP MultiLineOptions mlo_maintain_line_breaks(void);

typedef struct {
	bool prefer_hanging, force_use_new_lines, allow_blank_lines;
	uint8_t indent_width;
	SingleLineOptions single_line_options;
	MultiLineOptions multi_line_options;
	bool force_possible_newline_at_start;
} GenSeparatedValuesOptions;

typedef struct {
	bool has;
	size_t start_line, end_line;
} LinesSpan;

typedef struct {
	Items items;
	LinesSpan lines_span;
	bool allow_inline_multi_line, allow_inline_single_line;
} GeneratedValue;

typedef struct {
	GeneratedValue *v;
	int n, cap;
} GeneratedValues;
DP void gv_push(GeneratedValues *vs, GeneratedValue v);

typedef struct {
	Items items;
	ConditionReference is_multi_line_condition_ref;
} GenSeparatedValuesResult;

/* generated_values(ref, data) builds the values (it runs after the multi-line condition exists). */
typedef GeneratedValues (*GenValuesFn)(ConditionReference is_multi_line, void *data);
DP GenSeparatedValuesResult gen_separated_values(GenValuesFn values, void *data, GenSeparatedValuesOptions opts);

/* ---- printing ---- */
typedef struct {
	uint32_t max_width;
	uint8_t indent_width;
	bool use_tabs;
	const char *new_line_text;
} PrintOptions;
/* Resets the id counters (format() in print.rs); call before generating print items. */
DP void dprint_ids_reset(void);
DP void dprint_print(Items items, PrintOptions opts, Buf *out);

/* unicode_width::UnicodeWidthStr::width of UTF-8 text. */
DP uint32_t unicode_str_width(const char *s, size_t n);

#endif
