/* black 24.3.0 (https://github.com/psf/black/tree/24.3.0, MIT): the line generator and line
 * splitting of black.format_str(src, mode=Mode(target_versions={TargetVersion.PY37})) with the
 * default mode (line length 88, string normalization, magic trailing commas, no preview
 * features), over a blib2to3 tree that the generator builds instead of source text: there is no
 * tokenizer and no parser (docs/LAYOUT.md).
 *
 * The tree is the one blib2to3 (pgen driver + pytree.convert) would build from the source:
 * nodes of grammar symbols with one child collapse to that child (bk_node does it), leaves carry
 * their token value and their prefix, the source text between the previous token and this one
 * (spaces, blank lines and comments, as blib2to3/pgen2/driver.py assigns it: comments and blank
 * lines before an INDENT go to the first token of the block; before a DEDENT, the comment lines
 * indented like the block go to the DEDENT, everything from the first less indented comment on
 * to the next token). Only the prefixes of the first token of a statement matter: black keeps
 * only their comments and newline count.
 *
 * Supported: the node kinds listed in BkType, used the way the Python grammar uses them, with
 * NAME, NUMBER (decimal integers) and STRING leaves (double-quoted without backslashes or inner
 * double quotes, or triple-quoted docstrings), and comments only on their own lines (in
 * prefixes of statements). Non-ASCII text may only occur in comments and in STRING leaves that
 * are a whole statement (docstrings), where black's East Asian width rules cannot change the
 * output. A docstring whose quotes the tokenizer would not end where the leaf ends, "# fmt: off"
 * and "# fmt: skip" comments are not supported: the builder of the tree checks them
 * (bk_docstring_ok, bk_comment_ok). */
#ifndef BK_BLACK_H
#define BK_BLACK_H

#include "util.h"

/* Token and symbol types (blib2to3 token.py and the symbols of Grammar.txt used here). */
typedef enum {
	BK_ENDMARKER, BK_NAME, BK_NUMBER, BK_STRING, BK_NEWLINE, BK_INDENT, BK_DEDENT,
	BK_LPAR, BK_RPAR, BK_LSQB, BK_RSQB, BK_LBRACE, BK_RBRACE, BK_COLON, BK_COMMA, BK_SEMI,
	BK_PLUS, BK_MINUS, BK_STAR, BK_SLASH, BK_VBAR, BK_AMPER, BK_LESS, BK_GREATER, BK_EQUAL,
	BK_DOT, BK_PERCENT, BK_EQEQUAL, BK_NOTEQUAL, BK_LESSEQUAL, BK_GREATEREQUAL, BK_TILDE,
	BK_CIRCUMFLEX, BK_LEFTSHIFT, BK_RIGHTSHIFT, BK_DOUBLESTAR, BK_DOUBLESLASH, BK_AT,
	BK_RARROW, BK_COLONEQUAL, BK_AWAIT, BK_ASYNC, BK_COMMENT, BK_STANDALONE_COMMENT,
	BK_NT_OFFSET = 256,
	BK_file_input = BK_NT_OFFSET, BK_simple_stmt, BK_expr_stmt, BK_annassign, BK_import_from,
	BK_import_name, BK_import_as_names, BK_dotted_name, BK_decorated, BK_decorator,
	BK_decorators, BK_classdef, BK_funcdef, BK_async_stmt, BK_async_funcdef, BK_parameters,
	BK_typedargslist, BK_tname, BK_tname_star, BK_suite, BK_if_stmt, BK_for_stmt,
	BK_while_stmt, BK_try_stmt, BK_except_clause, BK_with_stmt, BK_match_stmt, BK_case_block,
	BK_return_stmt, BK_del_stmt, BK_assert_stmt, BK_comparison, BK_comp_op, BK_power,
	BK_trailer, BK_arglist, BK_argument, BK_subscriptlist, BK_subscript, BK_sliceop, BK_atom,
	BK_factor, BK_star_expr, BK_yield_expr, BK_testlist_gexp, BK_listmaker, BK_dictsetmaker,
	BK_testlist_star_expr, BK_testlist, BK_exprlist, BK_varargslist, BK_test, BK_lambdef,
	BK_or_test, BK_and_test, BK_not_test, BK_expr, BK_xor_expr, BK_and_expr, BK_shift_expr,
	BK_arith_expr, BK_term, BK_namedexpr_test, BK_comp_for, BK_old_comp_for, BK_comp_if,
	BK_old_comp_if, BK_subject_expr, BK_pattern, BK_asexpr_test,
} BkType;

typedef struct BkNode BkNode;

/* A leaf with an empty prefix. value must stay valid (it is not copied). */
BkNode *bk_leaf(BkType type, const char *value);
/* A node of symbol type with the children kids[0..n) (n >= 1); a single child is returned
 * itself, like pytree.convert. The children must not have a parent yet. */
BkNode *bk_node(BkType type, int n, BkNode *const *kids);
/* bk_node with a NULL-terminated list of children. */
BkNode *bk_nodev(BkType type, ...);
/* Sets the prefix of the first leaf of n (pytree Node.prefix). */
void bk_set_prefix(BkNode *n, const char *prefix);

/* ---- builders of the grammar productions (build.c); arguments become children ---- */

BkNode *bk_name(const char *v);   /* NAME */
BkNode *bk_number(const char *v); /* NUMBER */
BkNode *bk_string(const char *v); /* STRING, with its quotes */
/* `base.name`, `callee(args)`, `base[items]`: a power node with one more trailer (base may be a
 * power already, then the trailer is appended to it). n == 0: `f()`, `x[]` is invalid. */
BkNode *bk_attr(BkNode *base, const char *name);
BkNode *bk_call(BkNode *callee, int n, BkNode *const *args);
BkNode *bk_call_trailing_comma(BkNode *callee, int n, BkNode *const *args); /* `f(a, b,)` */
BkNode *bk_subscript(BkNode *base, int n, BkNode *const *items);
/* `a.b.c` as NAME or power. */
BkNode *bk_dotted(const char *dotted);
BkNode *bk_kwarg(const char *name, BkNode *value); /* argument `name=value` */
BkNode *bk_neg(BkNode *operand);                   /* factor `-x` */
BkNode *bk_await(BkNode *expr);                    /* power `await expr` */
BkNode *bk_empty_list(void);                       /* atom `[]` */
BkNode *bk_empty_tuple(void);                      /* atom `()` */
BkNode *bk_is_not(BkNode *a, BkNode *b);           /* comparison `a is not b` */
BkNode *bk_yield(BkNode *v);                       /* yield_expr `yield v` */

/* Statements; prefix is the prefix of their first token (blank lines and comment lines). */
BkNode *bk_stmt(const char *prefix, BkNode *small); /* simple_stmt: small NEWLINE */
BkNode *bk_assign(BkNode *target, BkNode *value);   /* expr_stmt `target = value` */
BkNode *bk_annassign(BkNode *target, BkNode *annotation, BkNode *value);
BkNode *bk_return(BkNode *value);
/* `from <dots><module> import a, b` (module may be "" with dots > 0). */
BkNode *bk_import_from(int dots, const char *module, int n, const char *const *names);
BkNode *bk_import(const char *module);
/* A block: NEWLINE INDENT stmts DEDENT (n >= 1). */
BkNode *bk_suite(int n, BkNode *const *stmts);
BkNode *bk_classdef(const char *prefix, const char *name, BkNode *base, BkNode *suite);
BkNode *bk_decorated(const char *prefix, const char *decorator, BkNode *def);
/* Parameters: NAME `self`, the bare `*` (bk_star_param) or `name: annotation = default`. */
BkNode *bk_param(const char *name, BkNode *annotation, BkNode *dflt);
BkNode *bk_star_param(void);
BkNode *bk_funcdef(const char *prefix, const char *name, int n, BkNode *const *params, BkNode *returns,
		   BkNode *suite);
BkNode *bk_async(const char *prefix, BkNode *stmt); /* async_stmt: ASYNC funcdef/for_stmt */
BkNode *bk_if(const char *prefix, BkNode *test, BkNode *suite);
BkNode *bk_for(const char *prefix, BkNode *target, BkNode *iter, BkNode *suite);
/* file_input: stmts ENDMARKER; trailing is the prefix of ENDMARKER. */
BkNode *bk_file(int n, BkNode *const *stmts, const char *trailing);

/* Whether a triple-quoted docstring with this text between the quotes ends where its leaf
 * ends: no unescaped '"""' inside, no backslash escaping the closing quote, no NUL. */
bool bk_docstring_ok(Str text);
/* Whether a comment line ("#..." without newline) is one black formats as a plain comment:
 * not one that turns formatting off ("# fmt: off", "# fmt: skip", "# yapf: disable"), and without
 * NUL, CR, LF or form feed. */
bool bk_comment_ok(Str comment);

/* black.format_str(src, mode=Mode(target_versions={PY37})) of the source of file_input (a
 * BK_file_input node ending with ENDMARKER), including the second pass that format_str runs
 * on its own output. The tree is consumed. */
char *bk_format_str(BkNode *file_input);

#endif
