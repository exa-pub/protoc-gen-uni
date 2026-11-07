/* Package go/ast, ported from go1.26.5 src/go/ast/walk.go (Inspect only).
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../internal.h"

typedef struct {
	bool (*f)(void *ctx, ast_Node *node);
	void *ctx;
} ast_inspector;

static void ast_walk(ast_inspector *v, ast_Node *node);

static void ast_walkList(ast_inspector *v, Vec list)
{
	for (int i = 0; i < list.n; i++)
		ast_walk(v, list.v[i]);
}

#define W(x) ast_walk(v, (ast_Node *)(x))

/* Unlike Go, NULL children are skipped (Go would visit a typed nil and crash on bad ASTs). */
static void ast_walk(ast_inspector *v, ast_Node *node)
{
	if (!node)
		return;
	if (!v->f(v->ctx, node))
		return;
	switch (node->kind) {
	case AST_Comment:
		break;
	case AST_CommentGroup:
		ast_walkList(v, ((ast_CommentGroup *)node)->List);
		break;
	case AST_Field: {
		ast_Field *n = (ast_Field *)node;
		W(n->Doc);
		ast_walkList(v, n->Names);
		W(n->Type);
		W(n->Tag);
		W(n->Comment);
		break;
	}
	case AST_FieldList:
		ast_walkList(v, ((ast_FieldList *)node)->List);
		break;
	case AST_BadExpr:
	case AST_Ident:
	case AST_BasicLit:
		break;
	case AST_Ellipsis:
		W(((ast_Ellipsis *)node)->Elt);
		break;
	case AST_FuncLit:
		W(((ast_FuncLit *)node)->Type);
		W(((ast_FuncLit *)node)->Body);
		break;
	case AST_CompositeLit:
		W(((ast_CompositeLit *)node)->Type);
		ast_walkList(v, ((ast_CompositeLit *)node)->Elts);
		break;
	case AST_ParenExpr:
		W(((ast_ParenExpr *)node)->X);
		break;
	case AST_SelectorExpr:
		W(((ast_SelectorExpr *)node)->X);
		W(((ast_SelectorExpr *)node)->Sel);
		break;
	case AST_IndexExpr:
		W(((ast_IndexExpr *)node)->X);
		W(((ast_IndexExpr *)node)->Index);
		break;
	case AST_IndexListExpr:
		W(((ast_IndexListExpr *)node)->X);
		ast_walkList(v, ((ast_IndexListExpr *)node)->Indices);
		break;
	case AST_SliceExpr: {
		ast_SliceExpr *n = (ast_SliceExpr *)node;
		W(n->X);
		W(n->Low);
		W(n->High);
		W(n->Max);
		break;
	}
	case AST_TypeAssertExpr:
		W(((ast_TypeAssertExpr *)node)->X);
		W(((ast_TypeAssertExpr *)node)->Type);
		break;
	case AST_CallExpr:
		W(((ast_CallExpr *)node)->Fun);
		ast_walkList(v, ((ast_CallExpr *)node)->Args);
		break;
	case AST_StarExpr:
		W(((ast_StarExpr *)node)->X);
		break;
	case AST_UnaryExpr:
		W(((ast_UnaryExpr *)node)->X);
		break;
	case AST_BinaryExpr:
		W(((ast_BinaryExpr *)node)->X);
		W(((ast_BinaryExpr *)node)->Y);
		break;
	case AST_KeyValueExpr:
		W(((ast_KeyValueExpr *)node)->Key);
		W(((ast_KeyValueExpr *)node)->Value);
		break;
	case AST_ArrayType:
		W(((ast_ArrayType *)node)->Len);
		W(((ast_ArrayType *)node)->Elt);
		break;
	case AST_StructType:
		W(((ast_StructType *)node)->Fields);
		break;
	case AST_FuncType:
		W(((ast_FuncType *)node)->TypeParams);
		W(((ast_FuncType *)node)->Params);
		W(((ast_FuncType *)node)->Results);
		break;
	case AST_InterfaceType:
		W(((ast_InterfaceType *)node)->Methods);
		break;
	case AST_MapType:
		W(((ast_MapType *)node)->Key);
		W(((ast_MapType *)node)->Value);
		break;
	case AST_ChanType:
		W(((ast_ChanType *)node)->Value);
		break;
	case AST_BadStmt:
		break;
	case AST_DeclStmt:
		W(((ast_DeclStmt *)node)->Decl);
		break;
	case AST_EmptyStmt:
		break;
	case AST_LabeledStmt:
		W(((ast_LabeledStmt *)node)->Label);
		W(((ast_LabeledStmt *)node)->Stmt);
		break;
	case AST_ExprStmt:
		W(((ast_ExprStmt *)node)->X);
		break;
	case AST_SendStmt:
		W(((ast_SendStmt *)node)->Chan);
		W(((ast_SendStmt *)node)->Value);
		break;
	case AST_IncDecStmt:
		W(((ast_IncDecStmt *)node)->X);
		break;
	case AST_AssignStmt:
		ast_walkList(v, ((ast_AssignStmt *)node)->Lhs);
		ast_walkList(v, ((ast_AssignStmt *)node)->Rhs);
		break;
	case AST_GoStmt:
		W(((ast_GoStmt *)node)->Call);
		break;
	case AST_DeferStmt:
		W(((ast_DeferStmt *)node)->Call);
		break;
	case AST_ReturnStmt:
		ast_walkList(v, ((ast_ReturnStmt *)node)->Results);
		break;
	case AST_BranchStmt:
		W(((ast_BranchStmt *)node)->Label);
		break;
	case AST_BlockStmt:
		ast_walkList(v, ((ast_BlockStmt *)node)->List);
		break;
	case AST_IfStmt: {
		ast_IfStmt *n = (ast_IfStmt *)node;
		W(n->Init);
		W(n->Cond);
		W(n->Body);
		W(n->Else);
		break;
	}
	case AST_CaseClause:
		ast_walkList(v, ((ast_CaseClause *)node)->List);
		ast_walkList(v, ((ast_CaseClause *)node)->Body);
		break;
	case AST_SwitchStmt:
		W(((ast_SwitchStmt *)node)->Init);
		W(((ast_SwitchStmt *)node)->Tag);
		W(((ast_SwitchStmt *)node)->Body);
		break;
	case AST_TypeSwitchStmt:
		W(((ast_TypeSwitchStmt *)node)->Init);
		W(((ast_TypeSwitchStmt *)node)->Assign);
		W(((ast_TypeSwitchStmt *)node)->Body);
		break;
	case AST_CommClause:
		W(((ast_CommClause *)node)->Comm);
		ast_walkList(v, ((ast_CommClause *)node)->Body);
		break;
	case AST_SelectStmt:
		W(((ast_SelectStmt *)node)->Body);
		break;
	case AST_ForStmt: {
		ast_ForStmt *n = (ast_ForStmt *)node;
		W(n->Init);
		W(n->Cond);
		W(n->Post);
		W(n->Body);
		break;
	}
	case AST_RangeStmt: {
		ast_RangeStmt *n = (ast_RangeStmt *)node;
		W(n->Key);
		W(n->Value);
		W(n->X);
		W(n->Body);
		break;
	}
	case AST_ImportSpec: {
		ast_ImportSpec *n = (ast_ImportSpec *)node;
		W(n->Doc);
		W(n->Name);
		W(n->Path);
		W(n->Comment);
		break;
	}
	case AST_ValueSpec: {
		ast_ValueSpec *n = (ast_ValueSpec *)node;
		W(n->Doc);
		ast_walkList(v, n->Names);
		W(n->Type);
		ast_walkList(v, n->Values);
		W(n->Comment);
		break;
	}
	case AST_TypeSpec: {
		ast_TypeSpec *n = (ast_TypeSpec *)node;
		W(n->Doc);
		W(n->Name);
		W(n->TypeParams);
		W(n->Type);
		W(n->Comment);
		break;
	}
	case AST_BadDecl:
		break;
	case AST_GenDecl:
		W(((ast_GenDecl *)node)->Doc);
		ast_walkList(v, ((ast_GenDecl *)node)->Specs);
		break;
	case AST_FuncDecl: {
		ast_FuncDecl *n = (ast_FuncDecl *)node;
		W(n->Doc);
		W(n->Recv);
		W(n->Name);
		W(n->Type);
		W(n->Body);
		break;
	}
	case AST_File: {
		ast_File *n = (ast_File *)node;
		W(n->Doc);
		W(n->Name);
		ast_walkList(v, n->Decls);
		break;
	}
	default:
		fail("go/ast: Walk: unexpected node kind %d", (int)node->kind);
	}
	v->f(v->ctx, NULL);
}

#undef W

GO_HELPER void ast_Inspect(ast_Node *node, bool (*f)(void *ctx, ast_Node *node), void *ctx)
{
	ast_inspector v = {f, ctx};
	ast_walk(&v, node);
}
