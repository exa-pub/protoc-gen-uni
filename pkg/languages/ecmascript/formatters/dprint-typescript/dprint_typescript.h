/* dprint-plugin-typescript 0.87.1 (with dprint-core 0.62.1), as embedded in dprint-node 1.0.8 and
 * called by ts-poet 6.12.0: the layout engine and the rules for the node kinds ts-proto generates.
 * Generators build the nodes (ts/ast.h) with the API below instead of printing TypeScript text;
 * the nodes carry the layout hints dprint takes from the text. See README.md. */
#ifndef DPRINT_TYPESCRIPT_H
#define DPRINT_TYPESCRIPT_H

#include <stdarg.h>

#include "util.h"
#include "ts/ast.h"

/* dprint_node.format("file.ts", text, ts-poet's options) for the text the module stands for:
 * appends the formatted text to out. The configuration is fixed: `deno: true` plus ts-poet
 * 6.12.0's baseOptions (Code.js). Fails (fail()) on a malformed tree. */
void dprint_ts_format_module(Node *module, Buf *out);

/* Whether swc_ecma_parser 0.139.0 (as dprint-plugin-typescript calls it) would fail to parse the
 * text the module stands for, as far as names go: a keyword, reserved word or TypeScript type
 * operator used as an identifier where swc rejects it, which depends on the position (binding,
 * import, expression, member object, type reference and the token after it). Returns that name,
 * or NULL. */
const char *dprint_ts_reserved_name(const Node *module);

/* A textual dump of a node tree with its hints (tests and debugging). */
void dprint_ts_dump(const Node *n, Buf *out);

/* ---- generic construction ---- */

Node *dts_new(int kind);
Node *dts_leaf(int kind, Str text);	    /* N_IDENT, N_STR (with quotes), N_NUMBER, ... */
Node *dts_leafc(int kind, const char *text);
void dts_add(NodeList *l, Node *n);	    /* NULL: an array hole */
/* comment before n (text with delimiters) */
void dts_comment(Node *n, Str text, bool block, int nl);
void dts_trailing_comment(Node *n, Str text, bool block, int nl);
Node *dts_nl(Node *n, int nl);		    /* sets n->nl, returns n */
Node *dts_hint(Node *n, unsigned hints);    /* adds hints, returns n */
/* Deep copy (a node can be in one tree only); cb (optional) sees every copy and its original. */
Node *dts_clone(const Node *n, void (*cb)(Node *copy, const Node *orig, void *arg), void *arg);

/* ---- expressions ---- */

Node *dts_id(const char *name);
Node *dts_idn(Str name);
Node *dts_str(Str raw);			    /* string literal as written, with its quotes */
Node *dts_num(const char *text);
Node *dts_bool(bool v);
Node *dts_null(void);
Node *dts_this(void);
Node *dts_member(Node *obj, const char *prop); /* obj.prop */
Node *dts_membern(Node *obj, Str prop);
Node *dts_index(Node *obj, Node *expr);	    /* obj[expr] */
Node *dts_opt_member(Node *obj, const char *prop, bool optional); /* obj?.prop (chain member if !optional) */
Node *dts_opt_index(Node *obj, Node *expr, bool optional);	   /* obj?.[expr] */
Node *dts_call(Node *callee, ...);	    /* NULL-terminated arguments */
Node *dts_callv(Node *callee, Node *type_args, NodeList args);
Node *dts_new_expr(Node *callee, ...);	    /* new callee(args...) */
Node *dts_bin(int op, Node *a, Node *b);    /* op: TK_* (TK_WORD + F_INSTANCEOF flag: instanceof/in) */
Node *dts_instanceof(Node *a, Node *b);
Node *dts_assign(int op, Node *a, Node *b);
Node *dts_unary(int op, Node *a);	    /* UNARY_* */
Node *dts_update(int op, Node *a, bool prefix); /* TK_PLUSPLUS, TK_MINUSMINUS */
Node *dts_cond(Node *test, Node *cons, Node *alt);
Node *dts_paren(Node *e);
Node *dts_as(Node *e, Node *type);
Node *dts_nonnull(Node *e);
Node *dts_await(Node *e);
Node *dts_yield(Node *e, bool delegate);
Node *dts_spread_arg(Node *e);		    /* ...e in arguments and arrays */
Node *dts_arr(Node *first, ...);	    /* NULL-terminated elements */
Node *dts_obj(void);
void dts_obj_add(Node *obj, Node *prop);
Node *dts_kv(Node *key, Node *value);	    /* key: value */
Node *dts_kvc(const char *key, Node *value);
Node *dts_spread(Node *e);		    /* ...e in object literals */
Node *dts_method_prop(Node *key, Node *function);
Node *dts_getter_prop(Node *key, Node *type, Node *body);
/* function (N_FUNCTION): params are N_PARAM (dts_param) */
Node *dts_function(NodeList params, Node *type_params, Node *return_type, Node *body, unsigned flags);
Node *dts_fn_expr(Node *ident, Node *function);
Node *dts_arrow(NodeList params, Node *return_type, Node *body, unsigned flags); /* params: patterns */
/* template literal: quasis are raw chunks (n_exprs + 1) */
Node *dts_tpl(int n_exprs, const Str *quasis, Node *const *exprs);
Node *dts_tagged_tpl(Node *tag, Node *tpl);
Node *dts_seq(Node *first, ...);

/* ---- patterns and parameters ---- */

Node *dts_binding(const char *name, Node *type, bool optional); /* N_BINDING_IDENT; type: a type */
Node *dts_bindingn(Str name, Node *type, bool optional);
Node *dts_param(Node *pat, Node *default_value); /* N_PARAM (with N_ASSIGN_PAT for a default) */
Node *dts_assign_pat(Node *pat, Node *value);
Node *dts_rest_pat(Node *pat, Node *type);
Node *dts_param_prop(int accessibility, unsigned flags, Node *pat);

/* ---- statements and declarations ---- */

Node *dts_module(void);
Node *dts_expr_stmt(Node *e);
Node *dts_return(Node *e);
Node *dts_throw(Node *e);
Node *dts_if(Node *test, Node *cons, Node *alt);
Node *dts_block(void);
void dts_block_add(Node *block, Node *stmt); /* also modules, switch cases, namespace bodies */
Node *dts_var(int kind, Node *pat, Node *init); /* VAR_*; pat: dts_binding (its type is the declarator's) */
Node *dts_var_decl(int kind);		     /* several declarators: dts_add(&v->list, dts_declarator(...)) */
Node *dts_declarator(Node *pat, Node *init);
Node *dts_for(Node *init, Node *test, Node *update, Node *body);
Node *dts_for_of(Node *left, Node *right, Node *body, bool await);
Node *dts_for_in(Node *left, Node *right, Node *body);
Node *dts_while(Node *test, Node *body);
Node *dts_do_while(Node *body, Node *test);
Node *dts_switch(Node *disc);
Node *dts_case(Node *test);		     /* NULL: default */
Node *dts_break(void);
Node *dts_continue(void);
Node *dts_try(Node *block, Node *catch_param, Node *catch_body, Node *finally_block);
Node *dts_export(Node *decl);
Node *dts_export_default(Node *e);
Node *dts_fn_decl(Node *ident, Node *function);
Node *dts_class_decl(Node *ident, Node *class_);
Node *dts_class(Node *super_class, Node *type_params);
void dts_class_add(Node *class_, Node *member);
Node *dts_class_method(Node *key, Node *function, unsigned flags, int accessibility, int kind);
Node *dts_class_prop(Node *key, Node *type, Node *value, unsigned flags, int accessibility);
Node *dts_constructor(NodeList params, Node *body, int accessibility);
Node *dts_interface(Node *ident, Node *type_params);
void dts_interface_add(Node *iface, Node *member);
Node *dts_type_alias(Node *ident, Node *type_params, Node *type);
Node *dts_enum(Node *ident, bool is_const);
void dts_enum_add(Node *e, Node *key, Node *init);
Node *dts_namespace(Node *ident);	     /* body: dts_block_add(ns->b, ...) */
Node *dts_import(Node *src, bool type_only); /* specifiers: dts_import_named, dts_import_default, dts_import_star */
void dts_import_named(Node *imp, Node *imported, Node *local, bool type_only); /* imported: NULL if no `as` */
void dts_import_default(Node *imp, Node *local);
void dts_import_star(Node *imp, Node *local);
Node *dts_import_equals(Node *id, Node *src, bool type_only); /* import id = require(src); */
Node *dts_export_named(Node *src, bool type_only);
void dts_export_specifier(Node *exp, Node *orig, Node *exported);
void dts_export_namespace(Node *exp, Node *name); /* export * as name from src; */
Node *dts_export_all(Node *src, bool type_only);

/* ---- types ---- */

Node *dts_tkw(const char *keyword);	     /* string, number, any, unknown, void, never, ... */
Node *dts_tref(const char *name, ...);	     /* NULL-terminated type arguments; dotted names allowed */
Node *dts_trefn(Str name, NodeList args);
Node *dts_tref_node(Node *entity, NodeList args);
Node *dts_targs(NodeList args);		     /* N_TS_TYPE_PARAM_INSTANTIATION (NULL if empty) */
Node *dts_tarray(Node *elem);
Node *dts_tunion(Node *first, ...);	     /* NULL-terminated */
Node *dts_tunionv(NodeList types);
Node *dts_tintersection(Node *first, ...);
Node *dts_tlit(Node *literal);		     /* N_STR, N_NUMBER, N_BOOL, N_TPL */
Node *dts_tparen(Node *t);
Node *dts_tfn(NodeList params, Node *type_params, Node *ret); /* params: patterns */
Node *dts_ttype_lit(void);
void dts_ttype_lit_add(Node *lit, Node *member);
Node *dts_prop_sig(Node *key, Node *type, bool optional, bool readonly);
Node *dts_method_sig(Node *key, Node *type_params, NodeList params, Node *ret, bool optional);
Node *dts_index_sig(const char *param, Node *param_type, Node *type, bool readonly);
Node *dts_tparam(const char *name, Node *constraint, Node *default_type);
Node *dts_tparams(Node *first, ...);	     /* N_TS_TYPE_PARAM_DECL */
Node *dts_tcond(Node *check, Node *extends, Node *t, Node *f);
Node *dts_tindexed(Node *obj, Node *index);
Node *dts_tkeyof(Node *t);
Node *dts_treadonly(Node *t);
Node *dts_tinfer(const char *name);
Node *dts_tquery(Node *entity);
Node *dts_tmapped(const char *param, Node *in, Node *as, Node *type, int readonly, int optional);
Node *dts_ttuple(Node *first, ...);
Node *dts_type_ann(Node *type);		     /* N_TS_TYPE_ANN */
Node *dts_entity(const char *dotted);	     /* N_IDENT or N_TS_QUALIFIED_NAME */

#endif
