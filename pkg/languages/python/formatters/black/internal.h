/* Internal declarations of the black 24.3.0 port (unity build, black.c). */
#ifndef BK_INTERNAL_H
#define BK_INTERNAL_H

#include "black.h"
#include "python.h"

#include <stdarg.h>
#include <string.h>

/* blib2to3/pytree.py Base, Node and Leaf in one struct.
 * Copyright 2006 Google, Inc. All Rights Reserved.
 * Licensed to PSF under a Contributor Agreement. */
struct BkNode {
	int type;
	int idx; /* index in parent->kids */
	int n, cap; /* Node: children */
	int bracket_depth; /* BracketTracker.mark */
	bool added_comma; /* a comma added by a split (bracket_split_build_line, delimiter_split) */
	BkNode *parent;
	union {
		BkNode **kids;     /* Node */
		const char *value; /* Leaf */
	};
	const char *prefix; /* Leaf */
	BkNode *opening_bracket; /* BracketTracker.mark */
	union {
		/* a leaf of the tree: append_leaves replaced it (the second opinion of
		 * run_transformer) */
		BkNode *replaced_by;
		/* a comment leaf: the leaf whose prefix held it (generate_comments) */
		BkNode *src;
	};
};

#define IS_LEAF(n) ((n)->type < BK_NT_OFFSET)

/* ---- tree.c ---- */
static BkNode *prev_sibling(const BkNode *n);
static BkNode *next_sibling(const BkNode *n);
static const char *node_prefix(const BkNode *n);
static void insert_child(BkNode *parent, int i, BkNode *child);
static void append_child(BkNode *parent, BkNode *child);
static int remove_node(BkNode *n); /* returns the former index, or -1 */
static void replace_node(BkNode *old, BkNode *new_);
static BkNode *first_leaf(BkNode *n);
static BkNode *last_leaf(BkNode *n);
static BkNode *new_leaf(int type, const char *value);
static BkNode *new_node(int type, int n, BkNode *const *kids);
/* The leaves of n in order. */
static int leaves_of(BkNode *n, BkNode ***out);

/* ---- nodes.c ---- */
static bool in_set(int t, const int *set, int n);
#define IN(t, ...) in_set((t), (const int[]){__VA_ARGS__}, (int)(sizeof((const int[]){__VA_ARGS__}) / sizeof(int)))
static bool is_opening(int t);
static bool is_closing(int t);
static bool is_bracket(int t);
static int bracket_of(int opening);
static bool is_math_operator(int t);
static bool is_vararg(const BkNode *leaf, int within);
enum { W_VARARGS = 1, W_UNPACKING = 2, W_TYPEDARGSLIST = 4, W_ARGLIST_ARGUMENT = 8 };
static bool is_multiline_string(const BkNode *leaf);
static bool is_one_sequence_between(BkNode *opening, BkNode *closing, BkNode **leaves, int n, int lb, int rb);
static bool is_name_value(const BkNode *n, const char *v);
static int parent_type(const BkNode *n);
static BkNode *preceding_leaf(BkNode *node);
static BkNode *child_towards(BkNode *ancestor, BkNode *descendant);
static void replace_child(BkNode *old, BkNode *new_);
static bool is_docstring(BkNode *leaf);
static bool is_empty_tuple(const BkNode *n);
static bool is_one_tuple(BkNode *n);
static bool is_tuple_containing_walrus(BkNode *n);
static bool is_walrus_assignment(BkNode *n);
static bool is_yield(BkNode *n);
static bool is_parent_function_or_class(const BkNode *n);
static bool is_stub_suite(BkNode *n);
static bool is_stub_body(BkNode *n);
static bool is_atom_with_invisible_parens(const BkNode *n);
static bool is_import_leaf(const BkNode *leaf);
static bool is_with_or_async_with_stmt(const BkNode *leaf);
static bool is_async_stmt_or_funcdef(const BkNode *leaf);
static void wrap_in_parentheses(BkNode *parent, BkNode *child, bool visible);
static BkNode *unwrap_singleton_parenthesis(BkNode *n);
static void ensure_visible(BkNode *leaf);
static int get_annotation_type(const BkNode *leaf); /* 0, 'r' (return), 'p' (param) */
static bool is_arith_like(const BkNode *n);
static const char *whitespace(BkNode *leaf, bool complex_subscript);
static bool has_triple_quotes(const char *s);
static int string_prefix_len(const char *s);

/* ---- brackets.c ---- */
enum {
	COMPREHENSION_PRIORITY = 20, COMMA_PRIORITY = 18, TERNARY_PRIORITY = 16, LOGIC_PRIORITY = 14,
	STRING_PRIORITY = 12, COMPARATOR_PRIORITY = 10, DOT_PRIORITY = 1,
};
typedef struct {
	BkNode *key;
	int prio;
} DelimSlot;
typedef struct {
	int depth, type;
	BkNode *leaf;
} BracketMatch;
typedef struct {
	int depth;
	BracketMatch *match; /* bracket_match, in insertion order */
	int nmatch, capmatch;
	DelimSlot *slots; /* delimiters: open addressing by leaf */
	int ndelim, capslots;
	BkNode *previous;
	int *for_depths, nfor, capfor;
	int *lambda_depths, nlambda, caplambda;
	BkNode **invisible;
	int ninv, capinv;
} BracketTracker;
static void bt_mark(BracketTracker *bt, BkNode *leaf);
static bool bt_any_open_brackets(const BracketTracker *bt);
static bool bt_any_open_for_or_lambda(const BracketTracker *bt);
/* max_delimiter_priority(exclude): -1 for ValueError (no delimiters left). */
static int bt_max_priority(const BracketTracker *bt, BkNode *const *exclude, int nexclude);
static int bt_count_with_priority(const BracketTracker *bt, int prio);
static int bt_get(const BracketTracker *bt, const BkNode *leaf); /* 0 if not a delimiter */
static BkNode *bt_get_open_lsqb(const BracketTracker *bt);
static int max_delimiter_priority_in_atom(BkNode *n);
/* get_leaves_inside_matching_brackets: in[i] is set for the leaves inside. */
static void leaves_inside_matching_brackets(BkNode **leaves, int n, bool *in);

/* ---- strings.c ---- */
static size_t py_len(const char *s, size_t n); /* code points */
static char *fix_docstring(const char *s, size_t n, const char *prefix);
static int str_width(const char *s, size_t n);

/* ---- comments.c ---- */
typedef struct {
	int type; /* BK_COMMENT or BK_STANDALONE_COMMENT */
	const char *value;
	int newlines;
	size_t consumed;
	bool form_feed;
} ProtoComment;
static int list_comments(const char *prefix, bool is_endmarker, ProtoComment **out);
/* generate_comments: comment leaves (parentless) of leaf's prefix; normalizes the prefix. */
static int generate_comments(BkNode *leaf, BkNode ***out);

/* ---- lines.c ---- */
typedef struct Line {
	int depth;
	BkNode **leaves;
	int n, cap;
	BracketTracker bt;
	bool inside_brackets, should_split_rhs;
	BkNode *magic_trailing_comma;
} Line;
typedef struct {
	Line **v;
	int n, cap;
} Lines;
static void lines_push(Lines *ls, Line *l);
static Line *line_new(int depth, bool inside_brackets);
static void line_append(Line *l, BkNode *leaf, bool preformatted, bool track_bracket);
static bool line_append_safe(Line *l, BkNode *leaf, bool preformatted); /* false: ValueError */
static bool line_is_comment(const Line *l);
static bool line_is_decorator(const Line *l);
static bool line_is_import(const Line *l);
static bool line_is_with_or_async_with_stmt(const Line *l);
static bool line_is_class(const Line *l);
static bool line_is_def(const Line *l);
static bool line_is_stub_def(const Line *l);
static bool line_is_docstring(const Line *l);
static bool line_is_chained_assignment(const Line *l);
static bool line_opens_block(const Line *l);
static bool line_contains_standalone_comments(const Line *l);
static bool line_contains_multiline_strings(const Line *l);
static Line *line_clone(const Line *l);
static char *line_str(const Line *l); /* str(line) */
static char *line_to_string(const Line *l);
static bool is_line_short_enough(const Line *l, const char *line_str);
static bool can_be_split(const Line *l);
static void append_leaves(Line *new_line, const Line *old_line, BkNode **leaves, int n, bool preformatted);
typedef struct LinesBlock {
	struct LinesBlock *previous_block;
	Line *original_line;
	int before, after;
	bool form_feed;
	Buf content;
} LinesBlock;
typedef struct {
	Line *previous_line;
	LinesBlock *previous_block;
	Line **previous_defs;
	int ndefs, capdefs;
	LinesBlock *semantic_leading_comment;
} EmptyLineTracker;
static LinesBlock *maybe_empty_lines(EmptyLineTracker *elt, Line *current_line);
typedef struct {
	Line *head, *body, *tail;
	BkNode *opening_bracket, *closing_bracket;
} RHSResult;
static bool can_omit_invisible_parens(const RHSResult *rhs, int line_length);

/* ---- linegen.c ---- */
enum { F_TRAILING_COMMA_IN_CALL = 1, F_TRAILING_COMMA_IN_DEF = 2, F_FORCE_OPTIONAL_PARENTHESES = 4 };
typedef void (*EmitFn)(void *ctx, Line *line);
static void linegen_run(BkNode *root, EmitFn emit, void *ctx);
static void transform_line(Line *line, int features, Lines *out);

#define LINE_LENGTH 88

#endif
