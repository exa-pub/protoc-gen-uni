/* The formatter's nodes: the subset of swc_ecma_ast 0.110 (as seen through dprint-swc-ext's view)
 * that dprint-plugin-typescript formats in the TypeScript ts-poet/ts-proto produce, plus the layout
 * hints dprint takes from the source text (line breaks, blank lines, comments, optional tokens).
 * Generators build them with the API in dprint_typescript.h; layout.c lays them out into a virtual
 * source (text, tokens, comments, swc spans [lo, hi)) because dprint's rules query positions.
 *
 * swc: Copyright (c) swc project developers, Apache-2.0 License. */
#ifndef DPRINT_TS_AST_H
#define DPRINT_TS_AST_H

#include "util.h"

/* ---- tokens (swc_ecma_parser token.rs) ---- */
enum {
	TK_EOF,
	TK_WORD, /* identifier, keyword, true/false/null (see Tok.word) */
	TK_NUM,
	TK_BIGINT,
	TK_STR,
	TK_REGEX,
	TK_TEMPLATE,
	TK_BACKQUOTE,
	TK_DOLLAR_LBRACE,
	TK_LBRACE,
	TK_RBRACE,
	TK_LPAREN,
	TK_RPAREN,
	TK_LBRACKET,
	TK_RBRACKET,
	TK_SEMI,
	TK_COMMA,
	TK_DOT,
	TK_DOTDOTDOT,
	TK_QUESTION,
	TK_QUESTION_DOT,
	TK_COLON,
	TK_ARROW,
	TK_AT,
	TK_HASH,
	TK_TILDE,
	TK_BANG,
	TK_PLUSPLUS,
	TK_MINUSMINUS,
	TK_ASSIGN,
	/* AssignOp */
	TK_ADD_ASSIGN,
	TK_SUB_ASSIGN,
	TK_MUL_ASSIGN,
	TK_DIV_ASSIGN,
	TK_MOD_ASSIGN,
	TK_EXP_ASSIGN,
	TK_LSHIFT_ASSIGN,
	TK_RSHIFT_ASSIGN,
	TK_ZFRSHIFT_ASSIGN,
	TK_BITOR_ASSIGN,
	TK_BITXOR_ASSIGN,
	TK_BITAND_ASSIGN,
	TK_AND_ASSIGN,
	TK_OR_ASSIGN,
	TK_NULLISH_ASSIGN,
	/* BinOp */
	TK_EQEQ,
	TK_NOTEQ,
	TK_EQEQEQ,
	TK_NOTEQEQ,
	TK_LT,
	TK_LTEQ,
	TK_GT,
	TK_GTEQ,
	TK_LSHIFT,
	TK_RSHIFT,
	TK_ZFRSHIFT,
	TK_ADD,
	TK_SUB,
	TK_MUL,
	TK_DIV,
	TK_MOD,
	TK_BITOR,
	TK_BITXOR,
	TK_BITAND,
	TK_EXP,
	TK_LOGICAL_OR,
	TK_LOGICAL_AND,
	TK_NULLISH,
};

enum { WORD_IDENT, WORD_KEYWORD, WORD_LITERAL };

typedef struct {
	uint32_t lo, hi;
	uint16_t kind;
	uint8_t word;
	bool unused;
} Tok;

typedef struct {
	uint32_t lo, hi; /* including the slashes and stars */
	bool block;
	bool leading;	/* attached as leading (pos = next token start) or trailing (pos = previous token end) */
	uint32_t pos;
	bool handled;	/* Context::handled_comments */
} Comment;

/* ---- nodes ---- */
typedef enum {
	N_INVALID,
	N_MODULE, /* list: body */
	/* statements */
	N_EXPR_STMT,	 /* a: expr */
	N_VAR_DECL,	 /* op: VAR_* kind, list: decls, flags: F_DECLARE */
	N_VAR_DECLARATOR, /* a: name (pat), b: init, flags: F_DEFINITE */
	N_FN_DECL,	 /* a: ident, b: function, flags: F_DECLARE */
	N_CLASS_DECL,	 /* a: ident, b: class, flags: F_DECLARE */
	N_RETURN_STMT,	 /* a: arg */
	N_IF_STMT,	 /* a: test, b: cons, c: alt */
	N_BLOCK_STMT,	 /* list: stmts */
	N_FOR_STMT,	 /* a: init, b: test, c: update, d: body */
	N_FOR_OF_STMT,	 /* a: left, b: right, d: body, flags: F_AWAIT */
	N_FOR_IN_STMT,	 /* a: left, b: right, d: body */
	N_WHILE_STMT,	 /* a: test, d: body */
	N_DO_WHILE_STMT, /* a: test, d: body */
	N_SWITCH_STMT,	 /* a: discriminant, list: cases */
	N_SWITCH_CASE,	 /* a: test (NULL: default), list: cons */
	N_BREAK_STMT,	 /* a: label */
	N_CONTINUE_STMT, /* a: label */
	N_THROW_STMT,	 /* a: arg */
	N_TRY_STMT,	 /* a: block, b: handler, c: finalizer */
	N_CATCH_CLAUSE,	 /* a: param, b: body */
	N_EMPTY_STMT,
	N_LABELED_STMT, /* a: label, b: body */
	N_DEBUGGER_STMT,
	/* module declarations */
	N_IMPORT_DECL,		  /* list: specifiers, a: src, flags: F_TYPE_ONLY */
	N_IMPORT_NAMED_SPECIFIER, /* a: local, b: imported, flags: F_TYPE_ONLY */
	N_IMPORT_DEFAULT_SPECIFIER, /* a: local */
	N_IMPORT_STAR_AS_SPECIFIER, /* a: local */
	N_EXPORT_DECL,		  /* a: decl */
	N_NAMED_EXPORT,		  /* list: specifiers, a: src, flags: F_TYPE_ONLY */
	N_EXPORT_NAMED_SPECIFIER, /* a: orig, b: exported, flags: F_TYPE_ONLY */
	N_EXPORT_NAMESPACE_SPECIFIER, /* a: name (`* as name`, first in a NamedExport) */
	N_EXPORT_DEFAULT_EXPR,	  /* a: expr */
	N_EXPORT_ALL,		  /* a: src, flags: F_TYPE_ONLY */
	/* TS declarations */
	N_TS_INTERFACE_DECL,  /* a: id, b: type_params, c: body, list: extends, flags: F_DECLARE */
	N_TS_INTERFACE_BODY,  /* list: body */
	N_TS_TYPE_ALIAS_DECL, /* a: id, b: type_params, c: type_ann, flags: F_DECLARE */
	N_TS_ENUM_DECL,	      /* a: id, list: members, flags: F_DECLARE, F_CONST */
	N_TS_ENUM_MEMBER,     /* a: id, b: init */
	N_TS_MODULE_DECL,     /* a: id, b: body (TsModuleBlock), flags: F_DECLARE, F_GLOBAL, F_NAMESPACE */
	N_TS_MODULE_BLOCK,    /* list: body */
	/* classes and functions */
	N_CLASS,	     /* list: body, list2: decorators, a: super_class, b: type_params, c: super_type_params,
			      * implements in list3 */
	N_CLASS_METHOD,	     /* a: key, b: function, op: METHOD_*, flags: F_STATIC, F_OPTIONAL, F_ABSTRACT,
			      * F_OVERRIDE, accessibility */
	N_CLASS_PROP,	     /* a: key, b: value, c: type_ann, flags... */
	N_CONSTRUCTOR,	     /* a: key, list: params, b: body, accessibility */
	N_TS_PARAM_PROP,     /* a: param, accessibility, F_READONLY, F_OVERRIDE */
	N_FUNCTION,	     /* list: params, list2: decorators, a: body, b: type_params, c: return_type,
			      * flags: F_ASYNC, F_GENERATOR */
	N_PARAM,	     /* a: pat, list2: decorators */
	N_DECORATOR,	     /* a: expr */
	/* expressions */
	N_IDENT,
	N_PRIVATE_NAME,
	N_THIS_EXPR,
	N_SUPER,
	N_ARRAY_LIT,	   /* list: elems (ExprOrSpread or NULL holes) */
	N_OBJECT_LIT,	   /* list: props */
	N_KEY_VALUE_PROP,  /* a: key, b: value */
	N_ASSIGN_PROP,	   /* a: key, b: value */
	N_METHOD_PROP,	   /* a: key, b: function */
	N_GETTER_PROP,	   /* a: key, b: type_ann, c: body */
	N_SETTER_PROP,	   /* a: key, b: param, c: body */
	N_SPREAD_ELEMENT,  /* a: expr */
	N_EXPR_OR_SPREAD,  /* a: expr, flags: F_SPREAD (spread token at lo) */
	N_FN_EXPR,	   /* a: ident, b: function */
	N_ARROW_EXPR,	   /* list: params, a: body, b: type_params, c: return_type, flags: F_ASYNC */
	N_CLASS_EXPR,	   /* a: ident, b: class */
	N_UNARY_EXPR,	   /* op: token kind or UNARY_*, a: arg */
	N_UPDATE_EXPR,	   /* op: TK_PLUSPLUS/TK_MINUSMINUS, a: arg, flags: F_PREFIX */
	N_BIN_EXPR,	   /* op: token kind (TK_WORD for in/instanceof with flags), a: left, b: right */
	N_ASSIGN_EXPR,	   /* op: token kind, a: left, b: right */
	N_MEMBER_EXPR,	   /* a: obj, b: prop (Ident, PrivateName or ComputedPropName) */
	N_SUPER_PROP_EXPR, /* a: super, b: prop */
	N_COND_EXPR,	   /* a: test, b: cons, c: alt */
	N_CALL_EXPR,	   /* a: callee, list: args, b: type_args */
	N_NEW_EXPR,	   /* a: callee, list: args (F_HAS_ARGS), b: type_args */
	N_SEQ_EXPR,	   /* list: exprs */
	N_STR,
	N_NUMBER,
	N_BIGINT,
	N_BOOL,
	N_NULL,
	N_REGEX,
	N_TPL,		  /* list: exprs, list2: quasis */
	N_TPL_ELEMENT,
	N_TAGGED_TPL,	  /* a: tag, b: type_params, c: tpl */
	N_PAREN_EXPR,	  /* a: expr */
	N_AWAIT_EXPR,	  /* a: arg */
	N_YIELD_EXPR,	  /* a: arg, flags: F_DELEGATE */
	N_TS_AS_EXPR,	  /* a: expr, b: type_ann */
	N_TS_SATISFIES_EXPR, /* a: expr, b: type_ann */
	N_TS_CONST_ASSERTION, /* a: expr */
	N_TS_NON_NULL_EXPR, /* a: expr */
	N_TS_TYPE_ASSERTION, /* a: expr, b: type_ann */
	N_OPT_CHAIN_EXPR,  /* a: base (MemberExpr or OptCall), flags: F_OPTIONAL (`?.` at this level) */
	N_OPT_CALL,	   /* a: callee, list: args, b: type_args */
	N_COMPUTED_PROP_NAME, /* a: expr */
	N_TS_EXPR_WITH_TYPE_ARGS, /* a: expr, b: type_args */
	/* patterns */
	N_BINDING_IDENT,   /* a: id (Ident), b: type_ann, flags: F_OPTIONAL */
	N_ARRAY_PAT,	   /* list: elems, b: type_ann, flags: F_OPTIONAL */
	N_OBJECT_PAT,	   /* list: props, b: type_ann, flags: F_OPTIONAL */
	N_ASSIGN_PAT,	   /* a: left, b: right */
	N_REST_PAT,	   /* a: arg, b: type_ann */
	N_KEY_VALUE_PAT_PROP, /* a: key, b: value */
	N_ASSIGN_PAT_PROP, /* a: key, b: value */
	/* types */
	N_TS_TYPE_ANN,	   /* a: type */
	N_TS_KEYWORD_TYPE,
	N_TS_THIS_TYPE,
	N_TS_TYPE_REF,	   /* a: type_name, b: type_params */
	N_TS_QUALIFIED_NAME, /* a: left, b: right */
	N_TS_ARRAY_TYPE,   /* a: elem_type */
	N_TS_UNION_TYPE,   /* list: types */
	N_TS_INTERSECTION_TYPE, /* list: types */
	N_TS_FN_TYPE,	   /* list: params, b: type_params, c: type_ann */
	N_TS_CONSTRUCTOR_TYPE, /* list: params, b: type_params, c: type_ann, flags: F_ABSTRACT */
	N_TS_TYPE_LIT,	   /* list: members */
	N_TS_PROPERTY_SIGNATURE, /* a: key, c: type_ann, flags: F_READONLY, F_OPTIONAL, F_COMPUTED */
	N_TS_METHOD_SIGNATURE, /* a: key, list: params, b: type_params, c: type_ann, flags: F_OPTIONAL, F_COMPUTED */
	N_TS_INDEX_SIGNATURE, /* list: params, c: type_ann, flags: F_READONLY, F_STATIC */
	N_TS_CALL_SIGNATURE_DECL, /* list: params, b: type_params, c: type_ann */
	N_TS_CONSTRUCT_SIGNATURE_DECL, /* list: params, b: type_params, c: type_ann */
	N_TS_GETTER_SIGNATURE, /* a: key, c: type_ann */
	N_TS_SETTER_SIGNATURE, /* a: key, b: param */
	N_TS_TUPLE_TYPE,   /* list: elem_types */
	N_TS_TUPLE_ELEMENT, /* a: label, b: ty */
	N_TS_LIT_TYPE,	   /* a: lit */
	N_TS_TYPE_OPERATOR, /* op: TYPEOP_*, a: type_ann */
	N_TS_INDEXED_ACCESS_TYPE, /* a: obj_type, b: index_type, flags: F_READONLY */
	N_TS_CONDITIONAL_TYPE, /* a: check, b: extends, c: true, d: false */
	N_TS_INFER_TYPE,   /* a: type_param */
	N_TS_MAPPED_TYPE,  /* a: type_param, b: name_type, c: type_ann, op: readonly modifier, op2 in flags: optional */
	N_TS_PARENTHESIZED_TYPE, /* a: type_ann */
	N_TS_TYPE_QUERY,   /* a: expr_name, b: type_args */
	N_TS_TYPE_PARAM_DECL, /* list: params */
	N_TS_TYPE_PARAM,   /* a: name, b: constraint, c: default, flags: F_IN, F_OUT, F_CONST */
	N_TS_TYPE_PARAM_INSTANTIATION, /* list: params */
	N_TS_TYPE_PREDICATE, /* a: param_name, b: type_ann, flags: F_ASSERTS */
	N_TS_REST_TYPE,	   /* a: type_ann */
	N_TS_OPTIONAL_TYPE, /* a: type_ann */
	N_TS_IMPORT_EQUALS_DECL, /* a: id, b: module_ref, flags: F_TYPE_ONLY */
	N_TS_EXTERNAL_MODULE_REF, /* a: expr (string literal) */
	N_COUNT
} NodeKind;

enum { VAR_VAR, VAR_LET, VAR_CONST };
enum { METHOD_METHOD, METHOD_GETTER, METHOD_SETTER };
enum { ACC_NONE, ACC_PUBLIC, ACC_PROTECTED, ACC_PRIVATE };
enum { TYPEOP_KEYOF, TYPEOP_UNIQUE, TYPEOP_READONLY };
enum { MAPPED_NONE, MAPPED_TRUE, MAPPED_PLUS, MAPPED_MINUS };
enum { UNARY_MINUS = 1, UNARY_PLUS, UNARY_BANG, UNARY_TILDE, UNARY_TYPEOF, UNARY_VOID, UNARY_DELETE };

enum {
	F_DECLARE = 1u << 0,
	F_DEFINITE = 1u << 1,
	F_AWAIT = 1u << 2,
	F_TYPE_ONLY = 1u << 3,
	F_CONST = 1u << 4,
	F_GLOBAL = 1u << 5,
	F_NAMESPACE = 1u << 6,
	F_STATIC = 1u << 7,
	F_OPTIONAL = 1u << 8,
	F_ABSTRACT = 1u << 9,
	F_OVERRIDE = 1u << 10,
	F_READONLY = 1u << 11,
	F_ASYNC = 1u << 12,
	F_GENERATOR = 1u << 13,
	F_SPREAD = 1u << 14,
	F_PREFIX = 1u << 15,
	F_HAS_ARGS = 1u << 16,
	F_DELEGATE = 1u << 17,
	F_COMPUTED = 1u << 18,
	F_IN = 1u << 19,
	F_OUT = 1u << 20,
	F_ASSERTS = 1u << 21,
	F_INSTANCEOF = 1u << 22, /* BinExpr with op TK_WORD: instanceof (otherwise `in`) */
	F_TPL_TAIL = 1u << 23,
	F_DECLARE_KW = 1u << 24,
};

/* Line break in the source before a node (or a comment): NL_DEFAULT is the usual layout of
 * ts-poet output (statements, class, interface and enum members and switch cases on their own
 * line, everything else on the same line). */
enum { NL_DEFAULT, NL_SAME, NL_LINE, NL_BLANK };

/* Layout hints for optional tokens and line breaks inside a node. */
enum {
	H_NO_SEMI = 1u << 0,	    /* statement, class property or type member without its `;` (ASI) */
	H_SEP_COMMA = 1u << 1,	    /* type member ended by `,` instead of `;` */
	H_TRAILING_COMMA = 1u << 2, /* list with a trailing comma (object, array, arguments, enum, ...) */
	H_LEADING_OP = 1u << 3,	    /* union/intersection type with a leading `|`/`&` */
	H_CLOSE_NL = 1u << 4,	    /* line break before the closing bracket of the node's list */
	H_NO_PARENS = 1u << 5,	    /* arrow function `x => ...` with one parameter and no parentheses */
	H_MAPPED_SEMI = 1u << 6,    /* mapped type with a `;` before its `}` */
	H_OP_NL = 1u << 7,	    /* line break before the operator (binary, `?`, `:`, `=`, `=>`, `.`) */
	H_OP2_NL = 1u << 8,	    /* line break before the second operator (`:` of a conditional) */
};

typedef struct {
	Str text;     /* with the delimiters (line or block comment) */
	bool block;
	uint8_t nl;   /* line break before the comment (NL_SAME, NL_LINE, NL_BLANK) */
} NodeComment;

typedef struct Node Node;
typedef struct {
	Node **v;
	int n, cap;
} NodeList;

struct Node {
	uint16_t kind;
	uint16_t op;
	uint32_t lo, hi;
	uint32_t flags;
	uint8_t accessibility;
	uint8_t mapped_readonly, mapped_optional;
	uint8_t nl;	     /* NL_*: line break before the node's first comment or token */
	uint8_t nl_after_cm; /* NL_*: line break between the last comment and the first token */
	uint16_t hints;	     /* H_* */
	Str text;	     /* leaves: identifier, literal as written, keyword, raw template chunk */
	NodeComment *cm;     /* comments before the node */
	int ncm;
	NodeComment *tcm;    /* comments after the node's last token */
	int ntcm;
	Node *parent;
	Node *a, *b, *c, *d;
	NodeList list, list2, list3;
	void *user;	     /* free for the generator (copied by dts_clone) */
};

/* ---- source (dprint-swc-ext SourceTextInfo, TokenContainer, CommentContainer) ---- */
typedef struct {
	const char *text;
	uint32_t n;
	uint32_t *line_starts;
	int nlines;
	Tok *toks;
	int ntoks;
	Comment *comments;
	int ncomments;
	/* token index by start/end position (last wins, like the FxHashMap collect) */
	int32_t *tok_at_start, *tok_at_end; /* size n + 1, -1 if none */
	/* comments by attachment position, in order: indexes into comments */
	int32_t *lead_first, *trail_first; /* size n + 1: first index in lead_idx/trail_idx or -1 */
	int32_t *lead_idx, *trail_idx;
	int32_t *lead_count, *trail_count;
	Node *module;
} Source;

#endif
