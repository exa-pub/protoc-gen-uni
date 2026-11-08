/* prettyplease 0.2.37 (https://github.com/dtolnay/prettyplease, MIT OR Apache-2.0, Copyright
 * David Tolnay), the Rust pretty printer that buffa-codegen, tonic-build and connectrpc-codegen run
 * over the syn::File they build: `prettyplease::unparse(&syn::parse2::<syn::File>(tokens)?)`.
 *
 * Formatter library (docs/LAYOUT.md): the generator builds the syn nodes directly with the
 * constructors below and pp_unparse() prints them with the exact port of the layout engine
 * (algorithm.rs, ring.rs, convenience.rs) and of the printing rules of each node kind. There is no
 * token stream and no parser: a generator builds the tree that syn::parse2 would build from the
 * tokens upstream quotes (see README.md for what that means for precedence and statements).
 *
 * One node type, PpNode, holds every syn node; `kind` says which, the fields are used as listed per
 * kind below. Lists (`list`) hold the children in order. Memory comes from the arena. */
#ifndef PRETTYPLEASE_H
#define PRETTYPLEASE_H

#include "util.h"

typedef struct PpNode PpNode;

typedef struct {
	PpNode **v;
	int n, cap;
} PpList;

struct PpNode {
	uint8_t kind;
	uint8_t flags;  /* PP_F_* */
	uint16_t aux;   /* per kind: delimiter, visibility, qself position, punct char, ... */
	uint32_t len;   /* PP_STRVAL: length of text */
	const char *text;
	PpNode *a, *b, *c;
	PpList list;
	PpList *attrs;  /* Attribute nodes (outer and inner), NULL if none */
};

/* Node kinds. Fields: t = text, a/b/c = children, l = list, f = flags, x = aux. */
enum {
	PP_NONE,
	/* ---- items (also impl and trait items) ---- */
	PP_FILE,         /* l: items; attrs: inner attributes */
	PP_ITEM_CONST,   /* x: vis, a: vis path, t: ident, b: type, c: expr (NULL: trait const without
			    default), l[0]? no: generics in `generics` via pp_set_generics; f: DEFAULT */
	PP_ITEM_ENUM,    /* x: vis, t: ident, l: variants */
	PP_ITEM_FN,      /* x: vis, a: signature (PP_SIG), b: block (PP_BLOCK; NULL: `;`), f: DEFAULT */
	PP_ITEM_IMPL,    /* a: trait path (NULL: inherent), b: self type, l: items,
			    f: UNSAFE, NEGATIVE, DEFAULT */
	PP_ITEM_MACRO,   /* a: macro (PP_MACRO), t: ident (macro_rules! name) or NULL */
	PP_ITEM_MOD,     /* x: vis, t: ident, l: items, f: CONTENT (else `mod m;`), UNSAFE */
	PP_ITEM_STATIC,  /* x: vis, t: ident, a: type, b: expr, f: MUT */
	PP_ITEM_STRUCT,  /* x: vis, t: ident, aux2 in flags: PP_F_NAMED/PP_F_UNNAMED (else unit),
			    l: fields */
	PP_ITEM_TRAIT,   /* x: vis, t: ident, a: supertraits (PP_BOUNDS) or NULL, l: items, f: UNSAFE */
	PP_ITEM_TYPE,    /* x: vis, t: ident, a: type (item `type T = ...;`) */
	PP_IMPL_TYPE,    /* x: vis, t: ident, a: type (impl item), f: DEFAULT */
	PP_TRAIT_TYPE,   /* t: ident, a: bounds (PP_BOUNDS) or NULL, b: default type or NULL */
	PP_ITEM_USE,     /* x: vis, a: use tree, f: LEADING_COLON */
	PP_ITEM_EXTERN_CRATE, /* x: vis, t: ident, a: rename ident node (PP_IDENT) or NULL */
	/* ---- use trees ---- */
	PP_USE_PATH,     /* t: ident, a: subtree */
	PP_USE_NAME,     /* t: ident */
	PP_USE_RENAME,   /* t: ident, a: PP_IDENT rename */
	PP_USE_GLOB,
	PP_USE_GROUP,    /* l: trees */
	/* ---- item parts ---- */
	PP_SIG,          /* t: ident, l: inputs (PP_RECEIVER or PP_PAT_TYPE), a: return type or NULL,
			    f: CONST, ASYNC, UNSAFE; b: abi (PP_ABI) or NULL */
	PP_RECEIVER,     /* f: REF, MUT; t: lifetime or NULL; a: explicit type (`self: T`) or NULL */
	PP_ABI,          /* t: name literal repr or NULL */
	PP_VARIANT,      /* t: ident, f: NAMED/UNNAMED, l: fields, a: discriminant or NULL */
	PP_FIELD,        /* x: vis, t: ident (NULL: tuple field), a: type */
	PP_VIS_PATH,     /* (internal) */
	/* ---- generics ---- */
	PP_GENERICS,     /* l: params, a: where clause (PP_WHERE) or NULL */
	PP_LIFETIME_PARAM, /* t: lifetime ("'a"), l: bounds (PP_LIFETIME) */
	PP_TYPE_PARAM,   /* t: ident, a: bounds (PP_BOUNDS) or NULL, b: default or NULL */
	PP_CONST_PARAM,  /* t: ident, a: type, b: default or NULL */
	PP_WHERE,        /* l: predicates */
	PP_PRED_TYPE,    /* a: bounded type, b: bounds (PP_BOUNDS), c: bound lifetimes (PP_BOUND_LIFETIMES) */
	PP_PRED_LIFETIME,/* t: lifetime, l: bounds (PP_LIFETIME) */
	PP_BOUNDS,       /* l: PP_TRAIT_BOUND or PP_LIFETIME */
	PP_TRAIT_BOUND,  /* a: path, f: MAYBE (`?Sized`), PAREN; b: bound lifetimes */
	PP_BOUND_LIFETIMES, /* l: PP_LIFETIME_PARAM */
	PP_LIFETIME,     /* t: "'a" (also a generic argument) */
	/* ---- paths ---- */
	PP_PATH,         /* l: segments, f: LEADING_COLON */
	PP_SEGMENT,      /* t: ident, a: arguments (PP_ANGLE_ARGS or PP_PAREN_ARGS) or NULL */
	PP_ANGLE_ARGS,   /* l: generic arguments: types, PP_LIFETIME, PP_ASSOC_TYPE, PP_CONSTRAINT,
			    PP_CONST_ARG */
	PP_PAREN_ARGS,   /* l: input types, a: output type or NULL */
	PP_ASSOC_TYPE,   /* t: ident, a: generics (PP_ANGLE_ARGS) or NULL, b: type */
	PP_CONSTRAINT,   /* t: ident, a: generics or NULL, b: bounds */
	PP_CONST_ARG,    /* a: expr */
	/* ---- types ---- */
	PP_TY_ARRAY,     /* a: elem, b: len expr */
	PP_TY_BARE_FN,   /* l: args (types or PP_BARE_FN_ARG), a: return type or NULL, f: UNSAFE */
	PP_BARE_FN_ARG,  /* t: name, a: type */
	PP_TY_IMPL,      /* a: bounds */
	PP_TY_INFER,
	PP_TY_MACRO,     /* a: macro */
	PP_TY_NEVER,
	PP_TY_PAREN,     /* a: elem */
	PP_TY_PATH,      /* a: qself type or NULL, x: qself position, b: path */
	PP_TY_PTR,       /* a: elem, f: MUT */
	PP_TY_REF,       /* t: lifetime or NULL, a: elem, f: MUT */
	PP_TY_SLICE,     /* a: elem */
	PP_TY_DYN,       /* a: bounds */
	PP_TY_TUPLE,     /* l: elems */
	/* ---- expressions ---- */
	PP_E_ARRAY,      /* l: elems */
	PP_E_ASSIGN,     /* a = b */
	PP_E_ASYNC,      /* a: block, f: MOVE */
	PP_E_AWAIT,      /* a: base */
	PP_E_BINARY,     /* a op b, t: operator ("+", "+=", "&&", ...) */
	PP_E_BLOCK,      /* a: block, t: label or NULL */
	PP_E_BREAK,      /* t: label or NULL, a: value or NULL */
	PP_E_CALL,       /* a: func, l: args */
	PP_E_CAST,       /* a: expr, b: type */
	PP_E_CLOSURE,    /* l: input pats, a: body, b: return type or NULL, f: MOVE, ASYNC */
	PP_E_CONST,      /* a: block */
	PP_E_CONTINUE,   /* t: label or NULL */
	PP_E_FIELD,      /* a: base, t: member (ident or index) */
	PP_E_FOR,        /* a: pat, b: expr, c: body block, t: label */
	PP_E_IF,         /* a: cond, b: then block, c: else (PP_E_IF, PP_E_BLOCK, other) or NULL */
	PP_E_INDEX,      /* a[b] */
	PP_E_INFER,
	PP_E_LET,        /* let a = b */
	PP_E_LIT,        /* a: literal (PP_LIT) */
	PP_E_LOOP,       /* a: body block, t: label */
	PP_E_MACRO,      /* a: macro */
	PP_E_MATCH,      /* a: expr, l: arms */
	PP_E_METHOD,     /* a: receiver, t: method, b: turbofish (PP_ANGLE_ARGS) or NULL, l: args */
	PP_E_PAREN,      /* a */
	PP_E_PATH,       /* a: qself type or NULL, x: qself position, b: path */
	PP_E_RANGE,      /* a: start or NULL, b: end or NULL, f: CLOSED */
	PP_E_REF,        /* a, f: MUT */
	PP_E_REPEAT,     /* [a; b] */
	PP_E_RETURN,     /* a or NULL */
	PP_E_STRUCT,     /* a: qself type, x: qself position, b: path, l: field values, c: rest or NULL */
	PP_E_TRY,        /* a? */
	PP_E_TUPLE,      /* l: elems */
	PP_E_UNARY,      /* t: "*", "!" or "-", a */
	PP_E_UNSAFE,     /* a: block */
	PP_E_WHILE,      /* a: cond, b: body block, t: label */
	PP_FIELD_VALUE,  /* t: member, a: expr; f: SHORTHAND (no `: expr`) */
	PP_ARM,          /* a: pat, b: guard or NULL, c: body */
	PP_BLOCK,        /* l: statements */
	/* ---- statements ---- */
	PP_S_LOCAL,      /* a: pat, b: init or NULL, c: else expr (let-else) or NULL */
	PP_S_ITEM,       /* a: item */
	PP_S_EXPR,       /* a: expr, f: SEMI */
	PP_S_MACRO,      /* a: macro, f: SEMI */
	/* ---- patterns ---- */
	PP_P_IDENT,      /* t: ident, f: REF, MUT, a: subpattern or NULL */
	PP_P_LIT,        /* a: PP_E_LIT (or PP_E_UNARY of one) */
	PP_P_MACRO,      /* a: macro */
	PP_P_OR,         /* l: cases */
	PP_P_PAREN,      /* a */
	PP_P_PATH,       /* a: qself type, x: qself position, b: path */
	PP_P_RANGE,      /* a, b, f: CLOSED */
	PP_P_REF,        /* a, f: MUT */
	PP_P_REST,
	PP_P_SLICE,      /* l */
	PP_P_STRUCT,     /* b: path, l: field pats, f: REST */
	PP_P_TUPLE,      /* l, f: TRAILING (a trailing comma after a single element) */
	PP_P_TUPLE_STRUCT, /* b: path, l */
	PP_P_TYPE,       /* a: pat, b: type */
	PP_P_WILD,
	PP_FIELD_PAT,    /* t: member, a: pat, f: SHORTHAND */
	/* ---- attributes, macros, literals, tokens ---- */
	PP_ATTR,         /* f: INNER; x: meta kind (PP_META_*), a: path, b: name-value expr,
			    l: list tokens, c: unused; delimiter of a list in flags DELIM */
	PP_MACRO,        /* a: path, x: delimiter, l: tokens */
	PP_LIT,          /* x: literal kind (PP_LIT_*), t: repr, a: PP_STRVAL value (strings) */
	PP_STRVAL,       /* t, len: a string literal's value */
	PP_TT_IDENT,     /* t */
	PP_TT_PUNCT,     /* x: char, f: JOINT */
	PP_TT_LIT,       /* t: repr */
	PP_TT_GROUP,     /* x: delimiter, l: tokens */
	PP_IDENT,        /* t (a bare identifier, e.g. a `use` rename) */
	PP_PRECISE_CAPTURE, /* l: captured params (PP_LIFETIME or PP_IDENT): the bound `use<'a, T>` */
	PP_KIND_COUNT
};

/* Flags (meaning per kind). */
enum {
	PP_F_MUT = 1, PP_F_REF = 2, PP_F_MOVE = 4, PP_F_ASYNC = 8, PP_F_UNSAFE = 16, PP_F_CONST = 32,
	PP_F_DEFAULT = 64, PP_F_NEGATIVE = 128,
	PP_F_LEADING_COLON = 1, PP_F_INNER = 1, PP_F_JOINT = 1, PP_F_CLOSED = 1, PP_F_SEMI = 1,
	PP_F_SHORTHAND = 1, PP_F_CONTENT = 1, PP_F_MAYBE = 1, PP_F_PAREN = 2, PP_F_REST = 2,
	PP_F_TRAILING = 2, PP_F_NAMED = 2, PP_F_UNNAMED = 4,
};

enum { PP_VIS_INHERITED, PP_VIS_PUB, PP_VIS_CRATE, PP_VIS_SUPER, PP_VIS_SELF, PP_VIS_IN };
enum { PP_DELIM_PAREN, PP_DELIM_BRACE, PP_DELIM_BRACKET, PP_DELIM_NONE };
enum { PP_META_PATH, PP_META_LIST, PP_META_NAME_VALUE };
enum {
	PP_LIT_STR, PP_LIT_BYTE_STR, PP_LIT_C_STR, PP_LIT_BYTE, PP_LIT_CHAR, PP_LIT_INT,
	PP_LIT_FLOAT, PP_LIT_BOOL
};

/* ---- printing ---- */

/* prettyplease::unparse: the formatted file (a PP_FILE node). */
char *pp_unparse(const PpNode *file);
/* The same for one item (as it prints inside a file), for tests. */
char *pp_unparse_item(const PpNode *item);

/* ---- generic construction ---- */

PpNode *pp_node(int kind);
void pp_push(PpNode *parent, PpNode *child);        /* appends to parent->list */
void pp_push_attr(PpNode *node, PpNode *attr);      /* appends to node->attrs */
/* Sets the generics of an item (const, enum, fn signature, impl, struct, trait, type). */
void pp_set_generics(PpNode *item, PpNode *generics);
PpNode *pp_generics_of(const PpNode *item);
void pp_set_vis(PpNode *item, int vis);             /* PP_VIS_* (not PP_VIS_IN) */

/* ---- paths ---- */

/* A path from "a::b::C" ("::a::b" has a leading colon); segments without arguments. */
PpNode *pp_path(const char *s);
PpNode *pp_path_ident(const char *ident);           /* one segment, no splitting */
/* Appends a segment (args: PP_ANGLE_ARGS, PP_PAREN_ARGS or NULL); returns the path. */
PpNode *pp_path_push(PpNode *path, const char *ident, PpNode *args);
/* Sets the arguments of the last segment; returns the path. */
PpNode *pp_path_args(PpNode *path, PpNode *args);
/* Angle-bracketed arguments, NULL-terminated. */
PpNode *pp_angle(PpNode *first, ...);
PpNode *pp_lifetime(const char *lifetime);          /* "'a" */

/* ---- types ---- */

PpNode *pp_ty_path(PpNode *path);
PpNode *pp_ty(const char *path);                    /* pp_ty_path(pp_path(path)) */
/* A path type whose last segment has the given angle arguments (NULL-terminated). */
PpNode *pp_ty_gen(const char *path, PpNode *first, ...);
PpNode *pp_ty_qpath(PpNode *qself, int position, PpNode *path);
PpNode *pp_ty_ref(const char *lifetime, bool mut, PpNode *elem);
PpNode *pp_ty_ptr(bool mut, PpNode *elem);
PpNode *pp_ty_slice(PpNode *elem);
PpNode *pp_ty_array(PpNode *elem, PpNode *len);
PpNode *pp_ty_tuple(PpNode *first, ...);            /* NULL-terminated; pp_ty_tuple(NULL) is () */
PpNode *pp_ty_paren(PpNode *elem);
PpNode *pp_ty_impl(PpNode *bounds);
PpNode *pp_ty_dyn(PpNode *bounds);
PpNode *pp_ty_never(void);
PpNode *pp_ty_infer(void);
PpNode *pp_bounds(PpNode *first, ...);              /* NULL-terminated trait bounds/lifetimes */
PpNode *pp_trait_bound(PpNode *path, bool maybe);
/* use<'a, T> (a precise capturing bound): lifetimes ("'a") and idents, NULL-terminated. */
PpNode *pp_precise_capture(const char *first, ...);

/* ---- expressions ---- */

PpNode *pp_e_path(PpNode *path);
PpNode *pp_e(const char *path);                     /* pp_e_path(pp_path(path)) */
PpNode *pp_e_qpath(PpNode *qself, int position, PpNode *path);
PpNode *pp_e_lit(PpNode *lit);
PpNode *pp_e_call(PpNode *func, ...);               /* NULL-terminated arguments */
PpNode *pp_e_method(PpNode *receiver, const char *method, ...); /* NULL-terminated args */
PpNode *pp_e_method_tf(PpNode *receiver, const char *method, PpNode *turbofish, ...);
PpNode *pp_e_field(PpNode *base, const char *member);
PpNode *pp_e_ref(bool mut, PpNode *e);
PpNode *pp_e_unary(const char *op, PpNode *e);
PpNode *pp_e_binary(PpNode *l, const char *op, PpNode *r);
PpNode *pp_e_assign(PpNode *l, PpNode *r);
PpNode *pp_e_cast(PpNode *e, PpNode *ty);
PpNode *pp_e_try(PpNode *e);
PpNode *pp_e_await(PpNode *e);
PpNode *pp_e_index(PpNode *e, PpNode *index);
PpNode *pp_e_paren(PpNode *e);
PpNode *pp_e_tuple(PpNode *first, ...);             /* NULL-terminated */
PpNode *pp_e_array(PpNode *first, ...);             /* NULL-terminated */
PpNode *pp_e_repeat(PpNode *e, PpNode *len);
PpNode *pp_e_range(PpNode *start, PpNode *end, bool closed);
PpNode *pp_e_block(PpNode *block);
PpNode *pp_e_unsafe(PpNode *block);
PpNode *pp_e_async(PpNode *block, bool move);
PpNode *pp_e_if(PpNode *cond, PpNode *then_block, PpNode *else_branch);
PpNode *pp_e_let(PpNode *pat, PpNode *e);
PpNode *pp_e_match(PpNode *e);                      /* arms via pp_push */
PpNode *pp_arm(PpNode *pat, PpNode *guard, PpNode *body);
PpNode *pp_e_closure(PpNode *body, bool move);      /* inputs via pp_push */
PpNode *pp_e_struct(PpNode *path, PpNode *rest);    /* field values via pp_push */
PpNode *pp_field_value(const char *member, PpNode *e); /* e NULL: shorthand `member` */
PpNode *pp_e_return(PpNode *e);
PpNode *pp_e_break(const char *label, PpNode *e);
PpNode *pp_e_continue(const char *label);
PpNode *pp_e_for(PpNode *pat, PpNode *e, PpNode *body);
PpNode *pp_e_while(PpNode *cond, PpNode *body);
PpNode *pp_e_loop(PpNode *body);
PpNode *pp_e_macro(PpNode *mac);
PpNode *pp_e_infer(void);

/* ---- literals (repr: the token as proc-macro2 prints it) ---- */

PpNode *pp_lit(int kind, const char *repr);
/* A string literal with its value (Literal::string(value) gives the repr). */
PpNode *pp_lit_str_repr(const char *repr, Str value);
PpNode *pp_e_lit_int(const char *repr);            /* pp_e_lit(pp_lit(PP_LIT_INT, repr)) */
PpNode *pp_e_lit_bool(bool v);

/* ---- statements and blocks ---- */

PpNode *pp_block(PpNode *first, ...);               /* NULL-terminated statements */
PpNode *pp_s_local(PpNode *pat, PpNode *init);
/* An expression statement as syn parses it: a macro followed by `;` or with braces becomes a
 * macro statement (Stmt::Macro), everything else Stmt::Expr. */
PpNode *pp_s_expr(PpNode *e, bool semi);
PpNode *pp_s_item(PpNode *item);

/* ---- patterns ---- */

PpNode *pp_p_ident(const char *ident, bool ref, bool mut);
PpNode *pp_p_wild(void);
PpNode *pp_p_rest(void);
PpNode *pp_p_lit(PpNode *e);
PpNode *pp_p_path(PpNode *path);
PpNode *pp_p_tuple_struct(PpNode *path, ...);       /* NULL-terminated */
PpNode *pp_p_tuple(PpNode *first, ...);             /* NULL-terminated */
PpNode *pp_p_struct(PpNode *path, bool rest);       /* field pats via pp_push */
PpNode *pp_field_pat(const char *member, PpNode *pat); /* pat NULL: shorthand */
PpNode *pp_p_or(PpNode *first, ...);                /* NULL-terminated */
PpNode *pp_p_ref(bool mut, PpNode *pat);
PpNode *pp_p_type(PpNode *pat, PpNode *ty);
PpNode *pp_p_macro(PpNode *mac);
PpNode *pp_p_range(PpNode *lo, PpNode *hi, bool closed);

/* ---- attributes ---- */

/* #[path] */
PpNode *pp_attr_path(PpNode *path, bool inner);
/* #[path(tokens)] (delimiter PP_DELIM_*), tokens via pp_push */
PpNode *pp_attr_list(PpNode *path, int delimiter, bool inner);
/* #[path = expr] */
PpNode *pp_attr_name_value(PpNode *path, PpNode *value, bool inner);

/* ---- token trees (macro bodies, attribute lists) ---- */

PpNode *pp_macro(PpNode *path, int delimiter);      /* tokens via pp_push */
PpNode *pp_tt_ident(const char *ident);
PpNode *pp_tt_punct(char ch, bool joint);
PpNode *pp_tt_lit(const char *repr);
PpNode *pp_tt_group(int delimiter);                 /* tokens via pp_push */
/* Appends the tokens of a path ("::a::b": `:` joint, `:` alone, idents) to a token list. */
void pp_tt_push_path(PpNode *group, const char *path);
/* Appends the tokens of an operator ("=>", "&&", "::", "->", ...: all chars but the last joint). */
void pp_tt_push_op(PpNode *group, const char *op);
/* Appends the tokens of constant Rust source text as rustc lexes them for a macro (quote!):
 * identifiers (r#raw too), lifetimes ('a: a joint ' then the ident), literals (strings with
 * escapes, numbers), punctuation by maximal munch of Rust's multi-character operators (all
 * characters but the last joint) and (), [], {} groups. A lexer for fixed macro and attribute
 * bodies, not a parser: values that come from the input are pushed as their own tokens. */
void pp_tt_push_src(PpNode *group, const char *src);

/* ---- items ---- */

PpNode *pp_file(void);                              /* items via pp_push */
PpNode *pp_item_const(int vis, const char *ident, PpNode *ty, PpNode *e);
PpNode *pp_item_static(int vis, bool mut, const char *ident, PpNode *ty, PpNode *e);
PpNode *pp_item_struct(int vis, const char *ident, int fields_kind); /* 0, PP_F_NAMED, PP_F_UNNAMED */
PpNode *pp_field(int vis, const char *ident, PpNode *ty);
PpNode *pp_item_enum(int vis, const char *ident);   /* variants via pp_push */
PpNode *pp_variant(const char *ident, int fields_kind, PpNode *discriminant);
PpNode *pp_item_impl(PpNode *trait_path, PpNode *self_ty); /* items via pp_push */
PpNode *pp_item_trait(int vis, const char *ident, PpNode *supertraits);
PpNode *pp_sig(const char *ident, PpNode *ret);     /* inputs via pp_push */
PpNode *pp_receiver(bool ref, bool mut, const char *lifetime);
PpNode *pp_item_fn(int vis, PpNode *sig, PpNode *block);
PpNode *pp_item_type(int vis, const char *ident, PpNode *ty);
PpNode *pp_impl_type(int vis, const char *ident, PpNode *ty);
PpNode *pp_trait_type(const char *ident, PpNode *bounds, PpNode *def);
PpNode *pp_item_macro(PpNode *mac, const char *ident);
PpNode *pp_item_mod(int vis, const char *ident, bool content);
PpNode *pp_item_use(int vis, PpNode *tree, bool leading_colon);
PpNode *pp_use_path(const char *ident, PpNode *tree);
PpNode *pp_use_name(const char *ident);
PpNode *pp_use_rename(const char *ident, const char *rename);
PpNode *pp_use_glob(void);
PpNode *pp_use_group(void);                         /* trees via pp_push */
/* A use tree from "a::b::C", "a::b::*" or "a::b::C as D". */
PpNode *pp_use_tree(const char *s);

/* ---- generics ---- */

PpNode *pp_generics(void);                          /* params via pp_push */
PpNode *pp_lifetime_param(const char *lifetime);    /* bounds via pp_push (PP_LIFETIME) */
PpNode *pp_type_param(const char *ident, PpNode *bounds, PpNode *def);
PpNode *pp_where(void);                             /* predicates via pp_push */
PpNode *pp_pred_type(PpNode *ty, PpNode *bounds);

#endif
