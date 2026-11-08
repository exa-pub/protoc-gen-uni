/* dart_style 3.1.13 port: the formatter that protocolbuffers/dart (protoc_plugin 25.1.0, built
 * with Dart 3.13.3) applies to generated code, `DartFormatter(languageVersion: 3.7.0).format()`.
 *
 * Generated files start with `// @dart = 3.3`, which selects dart_style's *short* style (the
 * pre-3.7 formatter, lib/src/short/), so only that style is ported. The formatter front end
 * (scanner, parser) is not ported: the generator builds the unresolved AST that package:analyzer
 * would produce for its output directly, with the node constructors below, and the linearizer
 * (ds_linearize) turns the tree into the token stream and the source text that the formatter
 * reads (line numbers for blank lines and comments). Layout hints that dart_style takes from the
 * source text are carried by the nodes: newlines before a token or node (ds_nl), the vertical
 * layout of bodies, collections and parameter lists, trailing commas and comments.
 *
 * Node shapes (fields per kind) are those of package:analyzer 14, see NodeKind. Everything in this
 * directory except this API is static (unity build dart_style.c). */
#ifndef DART_STYLE_H
#define DART_STYLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* ---- tokens ---------------------------------------------------------------------------------- */

typedef enum {
	TT_EOF,
	TT_IDENTIFIER, /* identifiers and all keywords (see Token.kw) */
	TT_STRING,
	TT_INT,
	TT_HEXADECIMAL,
	TT_DOUBLE,
	TT_SINGLE_LINE_COMMENT,
	TT_MULTI_LINE_COMMENT,
	TT_SCRIPT_TAG,
	TT_STRING_INTERPOLATION_IDENTIFIER, /* $ */
	TT_STRING_INTERPOLATION_EXPRESSION, /* ${ */
	/* punctuation and operators */
	TT_OPEN_PAREN,
	TT_CLOSE_PAREN,
	TT_OPEN_SQUARE_BRACKET,
	TT_CLOSE_SQUARE_BRACKET,
	TT_OPEN_CURLY_BRACKET,
	TT_CLOSE_CURLY_BRACKET,
	TT_COMMA,
	TT_SEMICOLON,
	TT_COLON,
	TT_PERIOD,
	TT_PERIOD_PERIOD,
	TT_PERIOD_PERIOD_PERIOD,
	TT_PERIOD_PERIOD_PERIOD_QUESTION,
	TT_QUESTION,
	TT_QUESTION_PERIOD,
	TT_QUESTION_PERIOD_PERIOD,
	TT_QUESTION_QUESTION,
	TT_QUESTION_QUESTION_EQ,
	TT_AT,
	TT_HASH,
	TT_FUNCTION, /* => */
	TT_EQ,
	TT_EQ_EQ,
	TT_BANG,
	TT_BANG_EQ,
	TT_LT,
	TT_LT_EQ,
	TT_LT_LT,
	TT_LT_LT_EQ,
	TT_GT,
	TT_GT_EQ,
	TT_GT_GT,
	TT_GT_GT_EQ,
	TT_GT_GT_GT,
	TT_GT_GT_GT_EQ,
	TT_PLUS,
	TT_PLUS_EQ,
	TT_PLUS_PLUS,
	TT_MINUS,
	TT_MINUS_EQ,
	TT_MINUS_MINUS,
	TT_STAR,
	TT_STAR_EQ,
	TT_SLASH,
	TT_SLASH_EQ,
	TT_TILDE_SLASH,
	TT_TILDE_SLASH_EQ,
	TT_PERCENT,
	TT_PERCENT_EQ,
	TT_AMPERSAND,
	TT_AMPERSAND_EQ,
	TT_AMPERSAND_AMPERSAND,
	TT_AMPERSAND_AMPERSAND_EQ,
	TT_BAR,
	TT_BAR_EQ,
	TT_BAR_BAR,
	TT_BAR_BAR_EQ,
	TT_CARET,
	TT_CARET_EQ,
	TT_TILDE,
	TT_INDEX, /* [] (operator names) */
	TT_INDEX_EQ,
	TT_COUNT
} TokenType;

/* Keywords (reserved words, built-in identifiers and pseudo-keywords): Token.kw. */
typedef enum {
	KW_NONE,
	KW_ABSTRACT, KW_AS, KW_ASSERT, KW_ASYNC, KW_AWAIT, KW_BASE, KW_BREAK, KW_CASE, KW_CATCH,
	KW_CLASS, KW_CONST, KW_CONTINUE, KW_COVARIANT, KW_DEFAULT, KW_DEFERRED, KW_DO, KW_DYNAMIC,
	KW_ELSE, KW_ENUM, KW_EXPORT, KW_EXTENDS, KW_EXTENSION, KW_EXTERNAL, KW_FACTORY, KW_FALSE,
	KW_FINAL, KW_FINALLY, KW_FOR, KW_FUNCTION, KW_GET, KW_HIDE, KW_IF, KW_IMPLEMENTS, KW_IMPORT,
	KW_IN, KW_INTERFACE, KW_IS, KW_LATE, KW_LIBRARY, KW_MIXIN, KW_NATIVE, KW_NEW, KW_NULL, KW_OF,
	KW_ON, KW_OPERATOR, KW_PART, KW_REQUIRED, KW_RETHROW, KW_RETURN, KW_SEALED, KW_SET, KW_SHOW,
	KW_STATIC, KW_SUPER, KW_SWITCH, KW_SYNC, KW_THIS, KW_THROW, KW_TRUE, KW_TRY, KW_TYPE,
	KW_TYPEDEF, KW_VAR, KW_VOID, KW_WHEN, KW_WHILE, KW_WITH, KW_YIELD,
	KW_COUNT
} Keyword;

typedef struct Token Token;
struct Token {
	TokenType type;
	Keyword kw;
	const char *lexeme; /* NUL-terminated */
	int len;            /* bytes */
	int offset;         /* byte offset in the source text (set by ds_linearize); -1 for the head */
	int off16;          /* offset in UTF-16 code units */
	Token *next, *previous;
	/* Comments before this token, linked through `next` (null-terminated). */
	Token *precedingComments;
	/* Layout: wsSet: nl newlines before the token (then the indentation of the enclosing body plus
	 * sp), or sp spaces if nl == 0. trail: text after the token (trailing spaces). */
	bool wsSet;
	int8_t nl, sp;
	const char *trail;
};

/* ---- AST ------------------------------------------------------------------------------------
 * The node kinds of package:analyzer that the short style visitor knew; only those with
 * constructors below are visited (others fail with "unsupported AST node"), the rest stay listed
 * for the upstream structure of the visitor. */

typedef enum {
	N_NONE,
	/* expressions */
	N_SimpleIdentifier,      /* name */
	N_PrefixedIdentifier,    /* target=prefix (SimpleIdentifier), period, ident=identifier */
	N_PropertyAccess,        /* target (NULL if cascaded), op, ident=propertyName */
	N_MethodInvocation,      /* target, op, ident=methodName, typeArgs, args */
	N_FunctionExpressionInvocation, /* target=function, typeArgs, args */
	N_InstanceCreationExpression,   /* kw=new/const, ctorName, args */
	N_IndexExpression,       /* target (NULL if cascaded), op=period (cascade), question, lb, expr=index, rb */
	N_ArgumentList,          /* lp, list=arguments, rp */
	N_NamedArgument,         /* name, colon, expr */
	N_IntegerLiteral,        /* tok */
	N_DoubleLiteral,         /* tok */
	N_BooleanLiteral,        /* tok */
	N_NullLiteral,           /* tok */
	N_SimpleStringLiteral,   /* tok; flags NF_MULTILINE */
	N_AdjacentStrings,       /* list=strings */
	N_StringInterpolation,   /* list=elements; flags NF_MULTILINE */
	N_InterpolationString,   /* tok=contents */
	N_InterpolationExpression, /* lb ($ or ${), expr, rb (} or NULL) */
	N_ListLiteral,           /* kw=const, typeArgs, lb, list=elements, rb */
	N_SetOrMapLiteral,       /* kw=const, typeArgs, lb, list=elements, rb */
	N_MapLiteralEntry,       /* expr=key, op=separator, expr2=value */
	N_SpreadElement,         /* op, expr */
	N_IfElement,             /* kw=if, lp, expr, rp, expr2=then, kw2=else, expr3=else */
	N_ForElement,            /* kw2=await, kw=for, lp, parts, rp, expr2=body */
	N_ParenthesizedExpression, /* lp, expr, rp */
	N_BinaryExpression,      /* expr=left, op, expr2=right */
	N_PrefixExpression,      /* op, expr=operand */
	N_PostfixExpression,     /* expr=operand, op */
	N_AssignmentExpression,  /* expr=lhs, op, expr2=rhs */
	N_ConditionalExpression, /* expr=condition, question, expr2=then, colon, expr3=else */
	N_AsExpression,          /* expr, op=as, type */
	N_IsExpression,          /* expr, op=is, kw2=! , type */
	N_CascadeExpression,     /* target, list=cascadeSections */
	N_FunctionExpression,    /* typeParams, params, body */
	N_ThisExpression,        /* kw */
	N_SuperExpression,       /* kw */
	N_ThrowExpression,       /* kw, expr */
	N_RethrowExpression,     /* kw */
	N_AwaitExpression,       /* kw, expr */
	N_SymbolLiteral,         /* tok=#, list2 tokens via toks */
	/* types */
	N_NamedType,             /* prefixName, prefixPeriod, name, typeArgs, question */
	N_GenericFunctionType,   /* type=returnType, kw=Function, typeParams, params, question */
	N_TypeArgumentList,      /* lb, list=arguments, rb */
	N_TypeParameterList,     /* lb, list=typeParameters, rb */
	N_TypeParameter,         /* meta, name, kw=extends, type=bound */
	/* parameters */
	N_FormalParameterList,   /* lp, list=parameters, lb=leftDelimiter, rb=rightDelimiter, rp */
	N_RegularFormalParameter, /* meta, required, covariant, kw=final/const/var, type, name, suffix, defaultClause */
	N_FieldFormalParameter,  /* ... + kw2=this, period */
	N_SuperFormalParameter,  /* ... + kw2=super, period */
	N_FunctionTypedFormalParameterSuffix, /* typeParams, params, question */
	N_FormalParameterDefaultClause, /* op=separator, expr=value */
	/* statements */
	N_Block,                 /* lb, list=statements, rb */
	N_ExpressionStatement,   /* expr, semi */
	N_VariableDeclarationStatement, /* vars, semi */
	N_VariableDeclarationList, /* meta, late, kw=keyword, type, list=variables */
	N_VariableDeclaration,   /* name, op=equals, expr=initializer */
	N_ReturnStatement,       /* kw, expr, semi */
	N_IfStatement,           /* kw, lp, expr, rp, stmt=then, kw2=else, stmt2=else */
	N_ForStatement,          /* kw2=await, kw=for, lp, parts, rp, stmt=body */
	N_ForPartsWithDeclarations, /* vars, semi=leftSeparator, expr=condition, semi2=rightSeparator, list=updaters */
	N_ForPartsWithExpression,   /* expr2=initialization, semi, expr, semi2, list */
	N_ForEachPartsWithDeclaration, /* vars=loopVariable (DeclaredIdentifier), kw=in, expr=iterable */
	N_ForEachPartsWithIdentifier,  /* ident, kw=in, expr */
	N_DeclaredIdentifier,    /* meta, kw=keyword, type, name */
	N_WhileStatement,        /* kw, lp, expr, rp, stmt */
	N_DoStatement,           /* kw=do, stmt, kw2=while, lp, expr, rp, semi */
	N_SwitchStatement,       /* kw, lp, expr, rp, lb, list=members, rb */
	N_SwitchPatternCase,     /* meta=labels, kw=case, expr=pattern (ConstantPattern), colon, list=statements */
	N_SwitchDefault,         /* meta=labels, kw=default, colon, list=statements */
	N_ConstantPattern,       /* kw=const, expr */
	N_BreakStatement,        /* kw, ident=label, semi */
	N_ContinueStatement,     /* kw, ident=label, semi */
	N_YieldStatement,        /* kw, star, expr, semi */
	N_TryStatement,          /* kw=try, body, list=catchClauses, kw2=finally, body2=finallyBlock */
	N_CatchClause,           /* kw2=on, type=exceptionType, kw=catch, lp, name=exceptionParameter, op=comma, name2=stackTrace, rp, body */
	N_AssertStatement,       /* kw, lp, expr=condition, expr2=message, rp, semi */
	N_AssertInitializer,     /* kw, lp, expr, expr2, rp */
	N_EmptyStatement,        /* semi */
	N_FunctionDeclarationStatement, /* decl */
	N_LabeledStatement,      /* meta=labels, stmt */
	N_Label,                 /* name, colon */
	/* declarations */
	N_CompilationUnit,       /* tok=scriptTag, list=directives, list2=declarations */
	N_ImportDirective,       /* meta, kw, expr=uri, kw2=deferred, op=as, ident=prefix, list=combinators, semi */
	N_ExportDirective,       /* meta, kw, expr=uri, list=combinators, semi */
	N_LibraryDirective,      /* meta, kw, name2? -> list2=name components (DottedName via expr), semi */
	N_PartDirective,         /* meta, kw, expr=uri, semi */
	N_PartOfDirective,       /* meta, kw=part, kw2=of, expr=uri or libraryName, semi */
	N_DottedName,            /* toks via list of SimpleIdentifier (list) */
	N_ShowCombinator,        /* kw, list=names */
	N_HideCombinator,        /* kw, list=names */
	N_ClassDeclaration,      /* meta, m_abstract.., kw=class, namePart, clause1=extends, clause2=with, clause3=implements, body */
	N_MixinDeclaration,      /* meta, m_base, kw=mixin, name, typeParams, clause1=on, clause3=implements, body */
	N_NameWithTypeParameters, /* name, typeParams */
	N_ExtendsClause,         /* kw, type */
	N_WithClause,            /* kw, list=types */
	N_ImplementsClause,      /* kw, list=types */
	N_MixinOnClause,         /* kw, list=types */
	N_BlockClassBody,        /* lb, list=members, rb */
	N_EmptyClassBody,        /* semi */
	N_EnumDeclaration,       /* meta, kw=enum, namePart, clause2=with, clause3=implements, body */
	N_BlockEnumBody,         /* lb, list=constants, semi, list2=members, rb */
	N_EnumConstantDeclaration, /* meta, name, args (EnumConstantArguments: typeArgs, period, ident, args) */
	N_FieldDeclaration,      /* meta, external, static, abstract, covariant, vars, semi */
	N_TopLevelVariableDeclaration, /* meta, external, vars, semi */
	N_MethodDeclaration,     /* meta, external, modifier (static/abstract), property (get/set), operatorKw, name, type=returnType, typeParams, params, body */
	N_FunctionDeclaration,   /* meta, external, property, name, type=returnType, func=FunctionExpression */
	N_ConstructorDeclaration, /* meta, external, const, factory, ident=typeName, period, name, params, separator, list=initializers, redirect, body */
	N_ConstructorFieldInitializer, /* kw=this, period, ident=fieldName, op=equals, expr */
	N_SuperConstructorInvocation,  /* kw=super, period, ident=constructorName, args */
	N_RedirectingConstructorInvocation, /* kw=this, period, ident, args */
	N_ConstructorName,       /* type (NamedType), period, ident=name */
	N_GenericTypeAlias,      /* meta, kw=typedef, name, typeParams, op=equals, type, semi */
	N_FunctionTypeAlias,     /* meta, kw=typedef, type=returnType, name, typeParams, params, semi */
	N_Annotation,            /* op=@, ident=name (Simple/PrefixedIdentifier), typeArgs, period, ident2=constructorName, args */
	N_BlockFunctionBody,     /* kw=async/sync, star, body=block */
	N_ExpressionFunctionBody, /* kw, star, op=>, expr, semi */
	N_EmptyFunctionBody,     /* semi */
	N_COUNT
} NodeKind;

enum {
	NF_MULTILINE = 1, /* string literals */
	NF_NAMED = 2,     /* formal parameters: isNamed */
	NF_OPTIONAL_POSITIONAL = 4,
	/* Layout flags (DS_*) used by the linearizer. */
	DS_VERTICAL = 8,        /* elements, arguments or parameters one per line, closing bracket on its own line */
	DS_TRAILING_COMMA = 16, /* a comma after the last element */
	DS_INLINE = 32,         /* a body or block on one line: `{}` or `{ s; }` */
};

typedef struct Node Node;
typedef struct {
	Node **v;
	int n, cap;
} NodeList;

struct Node {
	NodeKind kind;
	unsigned flags;
	Node *parent;
	Token *begin, *end; /* beginToken, endToken (set by ds_linearize) */
	/* tokens (meaning per kind, see NodeKind) */
	Token *tok, *name, *op, *kw, *kw2, *lp, *rp, *lb, *rb, *semi, *period, *question, *colon, *star, *separator;
	/* modifiers */
	Token *m_external, *m_static, *m_abstract, *m_covariant, *m_late, *m_const, *m_factory, *m_required,
		*m_property, *m_operator, *m_base, *m_interface, *m_final, *m_sealed, *m_mixin, *m_new;
	/* NamedType import prefix */
	Token *prefixName, *prefixPeriod;
	/* children */
	Node *target, *ident, *ident2, *expr, *expr2, *expr3, *type, *typeArgs, *typeParams, *params, *args, *body,
		*body2, *stmt, *stmt2, *vars, *parts, *suffix, *defaultClause, *namePart, *clause1, *clause2, *clause3,
		*ctorName, *func, *redirect, *decl, *enumArgs;
	NodeList list, list2, meta;
	/* Layout of the first token (see Token.wsSet); comments before the first token; the comma
	 * after the node in a list (created by the linearizer if NULL). */
	bool wsSet;
	int8_t nl, sp;
	Token *comments, *commaTok;
};

void ds_push(NodeList *l, Node *n);

/* ---- node constructors ------------------------------------------------------------------------
 * They create the tokens of the node (lexemes only; ds_linearize places them). Lists of children
 * are NULL-terminated argument lists. */

Token *ds_t(TokenType type, const char *lexeme);
Token *ds_k(Keyword kw);              /* keyword token */
Token *ds_name(const char *s);        /* identifier token (built-in identifiers get their Keyword) */
/* A single-line comment ("// ..." or "/// ...") before the first token of n (after the comments
 * added before). By default the first comment takes the layout of the node, the following comments
 * and the first token go on their own lines. */
Token *ds_comment(Node *n, const char *text);
/* The comma after n in its list (to give it a layout or trailing text). */
Token *ds_comma(Node *n);
/* Layout: nl newlines before the node or token, indented by the body's indentation plus sp; or sp
 * spaces if nl == 0. */
Node *ds_nl(Node *n, int nl, int sp);
Token *ds_tnl(Token *t, int nl, int sp);

/* expressions */
Node *ds_id(const char *name);                          /* SimpleIdentifier */
Node *ds_prop(Node *target, const char *name);          /* target.name: PrefixedIdentifier or PropertyAccess */
Node *ds_path(const char *first, ...);                  /* a.b.c (NULL-terminated) */
Node *ds_call(Node *target, const char *name, Node *typeArgs, Node *args); /* MethodInvocation */
Node *ds_args(Node *first, ...);                        /* ArgumentList */
Node *ds_argl(NodeList l);
Node *ds_named(const char *name, Node *expr);           /* NamedArgument */
Node *ds_new(Keyword kw, Node *type, const char *ctor, Node *args); /* InstanceCreationExpression */
Node *ds_lit(NodeKind kind, TokenType type, const char *lexeme); /* Integer/Double/Boolean/Null/SimpleString */
Node *ds_int(long long v);
Node *ds_str(const char *lexeme);                       /* SimpleStringLiteral, lexeme with quotes */
Node *ds_adjacent(NodeList strings);                    /* AdjacentStrings */
/* StringInterpolation of `'text$name'`: parts alternate string text (with the quotes at the ends) and
 * identifier names. */
Node *ds_interp(const char *s0, const char *ident, const char *s1);
Node *ds_cond(Node *c, Node *then, Node *els);          /* c ? then : els */
Node *ds_bin(Node *l, TokenType op, const char *lexeme, Node *r);
Node *ds_prefix(TokenType op, const char *lexeme, Node *e);
Node *ds_postfix(Node *e, TokenType op, const char *lexeme);
Node *ds_assign(Node *l, TokenType op, const char *lexeme, Node *r);
Node *ds_as(Node *e, Node *type);
Node *ds_index(Node *target, Node *index);
Node *ds_paren(Node *e);
Node *ds_await(Node *e);
Node *ds_throw(Node *e);
Node *ds_this(void);
Node *ds_super(void);
Node *ds_cascade(Node *target, NodeList sections);
Node *ds_csec_call(const char *name, Node *typeArgs, Node *args);  /* ..name<T>(args) */
Node *ds_csec_assign(const char *name, Node *rhs);                 /* ..name = rhs */
Node *ds_list(Token *constKw, Node *typeArgs, NodeList elements);   /* ListLiteral */
Node *ds_map(Token *constKw, Node *typeArgs, NodeList elements);    /* SetOrMapLiteral */
Node *ds_entry(Node *key, Node *value);                            /* MapLiteralEntry */
Node *ds_fnexpr(Node *params, Node *body);                         /* FunctionExpression */

/* types */
Node *ds_type(const char *prefix, const char *name, Node *typeArgs, bool nullable); /* NamedType */
Node *ds_targs(Node *first, ...);                       /* TypeArgumentList */
Node *ds_fntype(Node *returnType, Node *params);        /* GenericFunctionType `R Function(...)` */

/* parameters */
Node *ds_params(NodeList positional, NodeList optional, bool named); /* FormalParameterList */
Node *ds_param(Node *type, const char *name, Node *defaultValue);     /* RegularFormalParameter */
Node *ds_fparam(Keyword thisOrSuper, const char *name);              /* this.name / super.name */

/* statements */
Node *ds_block(NodeList statements);
Node *ds_expr_stmt(Node *e);
Node *ds_return(Node *e);
Node *ds_if(Node *cond, Node *then);
Node *ds_var_stmt(Keyword kw, Node *type, const char *name, Node *init);
Node *ds_switch(Node *e, NodeList members);
Node *ds_case(Node *e, NodeList statements);   /* case e: ... (constant pattern) */
Node *ds_default(NodeList statements);
Node *ds_yield(bool star, Node *e);

/* function bodies */
Node *ds_arrow(Node *e);                        /* => e; */
Node *ds_bbody(Keyword async, bool star, Node *block); /* [async[*]] { ... } */
Node *ds_empty_body(void);                      /* ; */

/* declarations; meta: list of annotations */
Node *ds_annot(Node *name, Node *args);         /* @name(args) */
Node *ds_unit(NodeList directives, NodeList declarations);
Node *ds_import(const char *uri, const char *prefix);   /* uri: the string literal lexeme */
Node *ds_export(const char *uri, NodeList show);
/* A class: abstractKw, name, superclass (extends), mixins (with), members. */
Node *ds_class(NodeList meta, bool isAbstract, const char *name, Node *superclass, NodeList mixins, NodeList members);
Node *ds_enum(NodeList meta, const char *name, NodeList constants);
Node *ds_enum_const(const char *name);
/* FieldDeclaration / TopLevelVariableDeclaration: static, keyword (const/final/var or KW_NONE). */
Node *ds_field(NodeList meta, bool isStatic, Keyword kw, Node *type, const char *name, Node *init);
Node *ds_toplevel(NodeList meta, Keyword kw, Node *type, const char *name, Node *init);
/* MethodDeclaration: modifier (static/abstract or KW_NONE), property (get/set or KW_NONE). */
Node *ds_method(NodeList meta, Keyword modifier, Node *returnType, Keyword property, const char *name, Node *params,
		Node *body);
Node *ds_ctor(NodeList meta, Keyword constOrFactory, const char *className, const char *name, Node *params,
	      Node *body);

/* ---- linearizer and formatter ------------------------------------------------------------------ */

/* Places the tokens of the compilation unit: links them in source order, sets offsets, node begin
 * and end tokens and parents, and writes the source text (the generator's unformatted output) to
 * *text. Layout: declarations, members and statements each on their own line at the indentation of
 * their body (+2 per level), everything else on one line with single spaces, except where ds_nl,
 * DS_VERTICAL and DS_INLINE say otherwise. */
void ds_linearize(Node *unit, Buf *text);

/* Formats a linearized compilation unit like DartFormatter(languageVersion: 3.7.0).format: the
 * `// @dart = x.y` comment before the first token selects the style (only the short style, below
 * 3.7, is ported). Appends the result to *out. Returns NULL or an error message. */
const char *ds_format(Node *unit, Str text, Buf *out);

#endif
