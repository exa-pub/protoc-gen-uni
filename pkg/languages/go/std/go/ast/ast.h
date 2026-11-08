/* Package go/ast, ported from go1.27.1 src/go/ast/{ast,walk}.go: node types, Pos/End, Inspect.
 * Object resolution (Scope, Object, Ident.Obj, File.Scope/Unresolved) is not ported: the
 * parser runs as with SkipObjectResolution, and the printer does not use it.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 *
 * Representation: every node struct starts with `ast_Kind kind`; ast_Node, ast_Expr, ast_Stmt,
 * ast_Decl and ast_Spec are the generic (interface) views, cast to the concrete type after
 * checking kind (Go type switch -> switch (n->kind)). Slices are Vec of node pointers. */
#ifndef GO_STD_AST_H
#define GO_STD_AST_H

typedef enum {
	AST_Invalid,
	/* comments and fields */
	AST_Comment,
	AST_CommentGroup,
	AST_Field,
	AST_FieldList,
	/* expressions and types */
	AST_BadExpr,
	AST_Ident,
	AST_Ellipsis,
	AST_BasicLit,
	AST_FuncLit,
	AST_CompositeLit,
	AST_ParenExpr,
	AST_SelectorExpr,
	AST_IndexExpr,
	AST_IndexListExpr,
	AST_SliceExpr,
	AST_TypeAssertExpr,
	AST_CallExpr,
	AST_StarExpr,
	AST_UnaryExpr,
	AST_BinaryExpr,
	AST_KeyValueExpr,
	AST_ArrayType,
	AST_StructType,
	AST_FuncType,
	AST_InterfaceType,
	AST_MapType,
	AST_ChanType,
	/* statements */
	AST_BadStmt,
	AST_DeclStmt,
	AST_EmptyStmt,
	AST_LabeledStmt,
	AST_ExprStmt,
	AST_SendStmt,
	AST_IncDecStmt,
	AST_AssignStmt,
	AST_GoStmt,
	AST_DeferStmt,
	AST_ReturnStmt,
	AST_BranchStmt,
	AST_BlockStmt,
	AST_IfStmt,
	AST_CaseClause,
	AST_SwitchStmt,
	AST_TypeSwitchStmt,
	AST_CommClause,
	AST_SelectStmt,
	AST_ForStmt,
	AST_RangeStmt,
	/* specifications */
	AST_ImportSpec,
	AST_ValueSpec,
	AST_TypeSpec,
	/* declarations */
	AST_BadDecl,
	AST_GenDecl,
	AST_FuncDecl,
	/* file */
	AST_File,
	AST_Kind_count
} ast_Kind;

typedef struct ast_Node {
	ast_Kind kind;
} ast_Node;
typedef ast_Node ast_Expr;
typedef ast_Node ast_Stmt;
typedef ast_Node ast_Decl;
typedef ast_Node ast_Spec;

#define AST_IS_EXPR(n) ((n)->kind >= AST_BadExpr && (n)->kind <= AST_ChanType)
#define AST_IS_STMT(n) ((n)->kind >= AST_BadStmt && (n)->kind <= AST_RangeStmt)
#define AST_IS_SPEC(n) ((n)->kind >= AST_ImportSpec && (n)->kind <= AST_TypeSpec)
#define AST_IS_DECL(n) ((n)->kind >= AST_BadDecl && (n)->kind <= AST_FuncDecl)

/* Allocates a zeroed node of type T (ast_X) with kind AST_X. */
#define AST_NEW(T) ((ast_##T *)ast_new(AST_##T, sizeof(ast_##T)))
static ast_Node *ast_new(ast_Kind kind, size_t size);

typedef struct ast_Comment {
	ast_Kind kind;
	token_Pos Slash; /* position of "/" starting the comment */
	GoStr Text; /* comment text (excluding '\n' for //-style comments) */
} ast_Comment;

typedef struct ast_CommentGroup {
	ast_Kind kind;
	Vec List; /* ast_Comment *; len(List) > 0 */
} ast_CommentGroup;

typedef struct ast_Ident ast_Ident;
typedef struct ast_BasicLit ast_BasicLit;
typedef struct ast_FuncType ast_FuncType;
typedef struct ast_BlockStmt ast_BlockStmt;
typedef struct ast_CallExpr ast_CallExpr;

typedef struct ast_Field {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* associated documentation; or NULL */
	Vec Names; /* ast_Ident *: field/method/(type) parameter names; or empty */
	ast_Expr *Type; /* field/method/parameter type; or NULL */
	ast_BasicLit *Tag; /* field tag; or NULL */
	ast_CommentGroup *Comment; /* line comments; or NULL */
} ast_Field;

typedef struct ast_FieldList {
	ast_Kind kind;
	token_Pos Opening; /* position of opening parenthesis/brace/bracket, if any */
	Vec List; /* ast_Field * */
	token_Pos Closing; /* position of closing parenthesis/brace/bracket, if any */
} ast_FieldList;

/* ---- Expressions and types ---- */

typedef struct {
	ast_Kind kind;
	token_Pos From, To;
} ast_BadExpr;

struct ast_Ident {
	ast_Kind kind;
	token_Pos NamePos;
	GoStr Name;
};

typedef struct {
	ast_Kind kind;
	token_Pos Ellipsis; /* position of "..." */
	ast_Expr *Elt; /* ellipsis element type (parameter lists only); or NULL */
} ast_Ellipsis;

struct ast_BasicLit {
	ast_Kind kind;
	token_Pos ValuePos; /* literal position */
	token_Pos ValueEnd; /* position immediately after the literal */
	token_Token Kind; /* token_INT, token_FLOAT, token_IMAG, token_CHAR, or token_STRING */
	GoStr Value; /* literal string; e.g. 42, 0x7f, 3.14, 1e-9, 2.4i, 'a', '\x7f', "foo" or `\m\n\o` */
};

typedef struct {
	ast_Kind kind;
	ast_FuncType *Type;
	ast_BlockStmt *Body;
} ast_FuncLit;

typedef struct {
	ast_Kind kind;
	ast_Expr *Type; /* literal type; or NULL */
	token_Pos Lbrace;
	Vec Elts; /* ast_Expr * */
	token_Pos Rbrace;
	bool Incomplete; /* true if (source) expressions are missing in the Elts list */
} ast_CompositeLit;

typedef struct {
	ast_Kind kind;
	token_Pos Lparen;
	ast_Expr *X;
	token_Pos Rparen;
} ast_ParenExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	ast_Ident *Sel;
} ast_SelectorExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	token_Pos Lbrack;
	ast_Expr *Index;
	token_Pos Rbrack;
} ast_IndexExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	token_Pos Lbrack;
	Vec Indices; /* ast_Expr * */
	token_Pos Rbrack;
} ast_IndexListExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	token_Pos Lbrack;
	ast_Expr *Low, *High, *Max; /* or NULL */
	bool Slice3;
	token_Pos Rbrack;
} ast_SliceExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	token_Pos Lparen;
	ast_Expr *Type; /* asserted type; NULL means type switch X.(type) */
	token_Pos Rparen;
} ast_TypeAssertExpr;

struct ast_CallExpr {
	ast_Kind kind;
	ast_Expr *Fun;
	token_Pos Lparen;
	Vec Args; /* ast_Expr * */
	token_Pos Ellipsis; /* position of "..." (token_NoPos if there is no "...") */
	token_Pos Rparen;
};

typedef struct {
	ast_Kind kind;
	token_Pos Star;
	ast_Expr *X;
} ast_StarExpr;

typedef struct {
	ast_Kind kind;
	token_Pos OpPos;
	token_Token Op;
	ast_Expr *X;
} ast_UnaryExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	token_Pos OpPos;
	token_Token Op;
	ast_Expr *Y;
} ast_BinaryExpr;

typedef struct {
	ast_Kind kind;
	ast_Expr *Key;
	token_Pos Colon;
	ast_Expr *Value;
} ast_KeyValueExpr;

typedef enum {
	ast_SEND = 1 << 0,
	ast_RECV = 1 << 1,
} ast_ChanDir;

typedef struct {
	ast_Kind kind;
	token_Pos Lbrack;
	ast_Expr *Len; /* Ellipsis node for [...]T array types, NULL for slice types */
	ast_Expr *Elt;
} ast_ArrayType;

typedef struct {
	ast_Kind kind;
	token_Pos Struct;
	ast_FieldList *Fields;
	bool Incomplete;
} ast_StructType;

struct ast_FuncType {
	ast_Kind kind;
	token_Pos Func; /* position of "func" keyword (token_NoPos if there is no "func") */
	ast_FieldList *TypeParams; /* or NULL */
	ast_FieldList *Params; /* or NULL */
	ast_FieldList *Results; /* or NULL */
};

typedef struct {
	ast_Kind kind;
	token_Pos Interface;
	ast_FieldList *Methods;
	bool Incomplete;
} ast_InterfaceType;

typedef struct {
	ast_Kind kind;
	token_Pos Map;
	ast_Expr *Key;
	ast_Expr *Value;
} ast_MapType;

typedef struct {
	ast_Kind kind;
	token_Pos Begin; /* position of "chan" keyword or "<-" (whichever comes first) */
	token_Pos Arrow; /* position of "<-" (token_NoPos if there is no "<-") */
	int Dir; /* ast_ChanDir */
	ast_Expr *Value;
} ast_ChanType;

/* ---- Statements ---- */

typedef struct {
	ast_Kind kind;
	token_Pos From, To;
} ast_BadStmt;

typedef struct {
	ast_Kind kind;
	ast_Decl *Decl; /* *GenDecl with CONST, TYPE, or VAR token */
} ast_DeclStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Semicolon;
	bool Implicit; /* if set, ";" was omitted in the source */
} ast_EmptyStmt;

typedef struct {
	ast_Kind kind;
	ast_Ident *Label;
	token_Pos Colon;
	ast_Stmt *Stmt;
} ast_LabeledStmt;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
} ast_ExprStmt;

typedef struct {
	ast_Kind kind;
	ast_Expr *Chan;
	token_Pos Arrow;
	ast_Expr *Value;
} ast_SendStmt;

typedef struct {
	ast_Kind kind;
	ast_Expr *X;
	token_Pos TokPos;
	token_Token Tok; /* INC or DEC */
} ast_IncDecStmt;

typedef struct {
	ast_Kind kind;
	Vec Lhs; /* ast_Expr * */
	token_Pos TokPos;
	token_Token Tok; /* assignment token, DEFINE */
	Vec Rhs; /* ast_Expr * */
} ast_AssignStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Go;
	ast_CallExpr *Call;
} ast_GoStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Defer;
	ast_CallExpr *Call;
} ast_DeferStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Return;
	Vec Results; /* ast_Expr * */
} ast_ReturnStmt;

typedef struct {
	ast_Kind kind;
	token_Pos TokPos;
	token_Token Tok; /* keyword token (BREAK, CONTINUE, GOTO, FALLTHROUGH) */
	ast_Ident *Label; /* or NULL */
} ast_BranchStmt;

struct ast_BlockStmt {
	ast_Kind kind;
	token_Pos Lbrace;
	Vec List; /* ast_Stmt * */
	token_Pos Rbrace; /* token_NoPos if missing */
};

typedef struct {
	ast_Kind kind;
	token_Pos If;
	ast_Stmt *Init; /* or NULL */
	ast_Expr *Cond;
	ast_BlockStmt *Body;
	ast_Stmt *Else; /* else branch; or NULL */
} ast_IfStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Case; /* position of "case" or "default" keyword */
	Vec List; /* ast_Expr *: list of expressions or types; empty means default case */
	token_Pos Colon;
	Vec Body; /* ast_Stmt * */
} ast_CaseClause;

typedef struct {
	ast_Kind kind;
	token_Pos Switch;
	ast_Stmt *Init; /* or NULL */
	ast_Expr *Tag; /* or NULL */
	ast_BlockStmt *Body; /* CaseClauses only */
} ast_SwitchStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Switch;
	ast_Stmt *Init; /* or NULL */
	ast_Stmt *Assign; /* x := y.(type) or y.(type) */
	ast_BlockStmt *Body; /* CaseClauses only */
} ast_TypeSwitchStmt;

typedef struct {
	ast_Kind kind;
	token_Pos Case; /* position of "case" or "default" keyword */
	ast_Stmt *Comm; /* send or receive statement; NULL means default case */
	token_Pos Colon;
	Vec Body; /* ast_Stmt * */
} ast_CommClause;

typedef struct {
	ast_Kind kind;
	token_Pos Select;
	ast_BlockStmt *Body; /* CommClauses only */
} ast_SelectStmt;

typedef struct {
	ast_Kind kind;
	token_Pos For;
	ast_Stmt *Init; /* or NULL */
	ast_Expr *Cond; /* or NULL */
	ast_Stmt *Post; /* or NULL */
	ast_BlockStmt *Body;
} ast_ForStmt;

typedef struct {
	ast_Kind kind;
	token_Pos For;
	ast_Expr *Key, *Value; /* Key, Value may be NULL */
	token_Pos TokPos; /* position of Tok; invalid if Key == NULL */
	token_Token Tok; /* ILLEGAL if Key == NULL, ASSIGN, DEFINE */
	token_Pos Range;
	ast_Expr *X;
	ast_BlockStmt *Body;
} ast_RangeStmt;

/* ---- Specifications ---- */

typedef struct {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* or NULL */
	ast_Ident *Name; /* local package name (including "."); or NULL */
	ast_BasicLit *Path;
	ast_CommentGroup *Comment; /* or NULL */
	token_Pos EndPos; /* end of spec (overrides Path.Pos if nonzero) */
} ast_ImportSpec;

typedef struct {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* or NULL */
	Vec Names; /* ast_Ident * */
	ast_Expr *Type; /* or NULL */
	Vec Values; /* ast_Expr * */
	ast_CommentGroup *Comment; /* or NULL */
} ast_ValueSpec;

typedef struct {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* or NULL */
	ast_Ident *Name;
	ast_FieldList *TypeParams; /* or NULL */
	token_Pos Assign; /* position of '=', if any */
	ast_Expr *Type;
	ast_CommentGroup *Comment; /* or NULL */
} ast_TypeSpec;

/* ---- Declarations ---- */

typedef struct {
	ast_Kind kind;
	token_Pos From, To;
} ast_BadDecl;

typedef struct {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* or NULL */
	token_Pos TokPos;
	token_Token Tok; /* IMPORT, CONST, TYPE, or VAR */
	token_Pos Lparen; /* position of '(', if any */
	Vec Specs; /* ast_Spec * */
	token_Pos Rparen; /* position of ')', if any */
} ast_GenDecl;

typedef struct {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* or NULL */
	ast_FieldList *Recv; /* receiver (methods); or NULL (functions) */
	ast_Ident *Name;
	ast_FuncType *Type; /* function signature: type and value parameters, results, and position of "func" keyword */
	ast_BlockStmt *Body; /* or NULL for external (non-Go) function */
} ast_FuncDecl;

typedef struct {
	ast_Kind kind;
	ast_CommentGroup *Doc; /* or NULL */
	token_Pos Package; /* position of "package" keyword */
	ast_Ident *Name;
	Vec Decls; /* ast_Decl * */
	token_Pos FileStart, FileEnd; /* start and end of entire file */
	Vec Imports; /* ast_ImportSpec * */
	Vec Comments; /* ast_CommentGroup *, in lexical order */
	GoStr GoVersion; /* minimum Go version required by //go:build or // +build directives */
} ast_File;

static token_Pos ast_Node_Pos(const ast_Node *n);
static token_Pos ast_Node_End(const ast_Node *n);
#define ast_Pos(n) ast_Node_Pos((const ast_Node *)(n))
#define ast_End(n) ast_Node_End((const ast_Node *)(n))

GO_HELPER ast_Ident *ast_NewIdent(GoStr name);
GO_HELPER bool ast_IsExported(GoStr name);
GO_HELPER int ast_FieldList_NumFields(const ast_FieldList *f);
GO_HELPER GoStr ast_CommentGroup_Text(const ast_CommentGroup *g);
GO_HELPER ast_Expr *ast_Unparen(ast_Expr *e);

/* Inspect traverses an AST in depth-first order (as ast.Inspect): it calls f(ctx, node); if f
 * returns true, Inspect is invoked recursively for each non-NULL child of node, followed by a
 * call of f(ctx, NULL). */
GO_HELPER void ast_Inspect(ast_Node *node, bool (*f)(void *ctx, ast_Node *node), void *ctx);

#endif
