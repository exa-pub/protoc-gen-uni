/* Package go/ast, ported from go1.27.1 src/go/ast/ast.go (Pos/End and helpers).
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../internal.h"

static ast_Node *ast_new(ast_Kind kind, size_t size)
{
	ast_Node *n = xalloc(size);
	n->kind = kind;
	return n;
}

#define AST_LAST(vec) ((const ast_Node *)(vec).v[(vec).n - 1])
#define AST_FIRST(vec) ((const ast_Node *)(vec).v[0])

/* Pos and End of a node delegate to a child in tail position (X.Pos(), Y.End(), ...); TAIL
 * continues with the child instead of recursing, so long selector/call/binary chains (the parser
 * accepts up to 100000 levels) do not use C stack. */
#define TAIL(child)                                                                                \
	do {                                                                                       \
		n = (const ast_Node *)(child);                                                     \
		goto loop;                                                                         \
	} while (0)

static token_Pos ast_Node_Pos(const ast_Node *n)
{
loop:
	switch (n->kind) {
	case AST_Comment:
		return ((const ast_Comment *)n)->Slash;
	case AST_CommentGroup:
		TAIL(AST_FIRST(((const ast_CommentGroup *)n)->List));
	case AST_Field: {
		const ast_Field *f = (const ast_Field *)n;
		if (f->Names.n > 0)
			TAIL(AST_FIRST(f->Names));
		if (f->Type)
			TAIL(f->Type);
		return token_NoPos;
	}
	case AST_FieldList: {
		const ast_FieldList *f = (const ast_FieldList *)n;
		if (token_Pos_IsValid(f->Opening))
			return f->Opening;
		if (f->List.n > 0)
			TAIL(AST_FIRST(f->List));
		return token_NoPos;
	}
	case AST_BadExpr:
		return ((const ast_BadExpr *)n)->From;
	case AST_Ident:
		return ((const ast_Ident *)n)->NamePos;
	case AST_Ellipsis:
		return ((const ast_Ellipsis *)n)->Ellipsis;
	case AST_BasicLit:
		return ((const ast_BasicLit *)n)->ValuePos;
	case AST_FuncLit:
		TAIL(((const ast_FuncLit *)n)->Type);
	case AST_CompositeLit: {
		const ast_CompositeLit *x = (const ast_CompositeLit *)n;
		if (x->Type)
			TAIL(x->Type);
		return x->Lbrace;
	}
	case AST_ParenExpr:
		return ((const ast_ParenExpr *)n)->Lparen;
	case AST_SelectorExpr:
		TAIL(((const ast_SelectorExpr *)n)->X);
	case AST_IndexExpr:
		TAIL(((const ast_IndexExpr *)n)->X);
	case AST_IndexListExpr:
		TAIL(((const ast_IndexListExpr *)n)->X);
	case AST_SliceExpr:
		TAIL(((const ast_SliceExpr *)n)->X);
	case AST_TypeAssertExpr:
		TAIL(((const ast_TypeAssertExpr *)n)->X);
	case AST_CallExpr:
		TAIL(((const ast_CallExpr *)n)->Fun);
	case AST_StarExpr:
		return ((const ast_StarExpr *)n)->Star;
	case AST_UnaryExpr:
		return ((const ast_UnaryExpr *)n)->OpPos;
	case AST_BinaryExpr:
		TAIL(((const ast_BinaryExpr *)n)->X);
	case AST_KeyValueExpr:
		TAIL(((const ast_KeyValueExpr *)n)->Key);
	case AST_ArrayType:
		return ((const ast_ArrayType *)n)->Lbrack;
	case AST_StructType:
		return ((const ast_StructType *)n)->Struct;
	case AST_FuncType: {
		const ast_FuncType *x = (const ast_FuncType *)n;
		if (token_Pos_IsValid(x->Func) || x->Params == NULL) /* see issue 3870 */
			return x->Func;
		TAIL(x->Params); /* interface method declarations have no "func" keyword */
	}
	case AST_InterfaceType:
		return ((const ast_InterfaceType *)n)->Interface;
	case AST_MapType:
		return ((const ast_MapType *)n)->Map;
	case AST_ChanType:
		return ((const ast_ChanType *)n)->Begin;

	case AST_BadStmt:
		return ((const ast_BadStmt *)n)->From;
	case AST_DeclStmt:
		TAIL(((const ast_DeclStmt *)n)->Decl);
	case AST_EmptyStmt:
		return ((const ast_EmptyStmt *)n)->Semicolon;
	case AST_LabeledStmt:
		TAIL(((const ast_LabeledStmt *)n)->Label);
	case AST_ExprStmt:
		TAIL(((const ast_ExprStmt *)n)->X);
	case AST_SendStmt:
		TAIL(((const ast_SendStmt *)n)->Chan);
	case AST_IncDecStmt:
		TAIL(((const ast_IncDecStmt *)n)->X);
	case AST_AssignStmt:
		TAIL(AST_FIRST(((const ast_AssignStmt *)n)->Lhs));
	case AST_GoStmt:
		return ((const ast_GoStmt *)n)->Go;
	case AST_DeferStmt:
		return ((const ast_DeferStmt *)n)->Defer;
	case AST_ReturnStmt:
		return ((const ast_ReturnStmt *)n)->Return;
	case AST_BranchStmt:
		return ((const ast_BranchStmt *)n)->TokPos;
	case AST_BlockStmt:
		return ((const ast_BlockStmt *)n)->Lbrace;
	case AST_IfStmt:
		return ((const ast_IfStmt *)n)->If;
	case AST_CaseClause:
		return ((const ast_CaseClause *)n)->Case;
	case AST_SwitchStmt:
		return ((const ast_SwitchStmt *)n)->Switch;
	case AST_TypeSwitchStmt:
		return ((const ast_TypeSwitchStmt *)n)->Switch;
	case AST_CommClause:
		return ((const ast_CommClause *)n)->Case;
	case AST_SelectStmt:
		return ((const ast_SelectStmt *)n)->Select;
	case AST_ForStmt:
		return ((const ast_ForStmt *)n)->For;
	case AST_RangeStmt:
		return ((const ast_RangeStmt *)n)->For;

	case AST_ImportSpec: {
		const ast_ImportSpec *s = (const ast_ImportSpec *)n;
		if (s->Name)
			TAIL(s->Name);
		TAIL(s->Path);
	}
	case AST_ValueSpec:
		TAIL(AST_FIRST(((const ast_ValueSpec *)n)->Names));
	case AST_TypeSpec:
		TAIL(((const ast_TypeSpec *)n)->Name);

	case AST_BadDecl:
		return ((const ast_BadDecl *)n)->From;
	case AST_GenDecl:
		return ((const ast_GenDecl *)n)->TokPos;
	case AST_FuncDecl:
		TAIL(((const ast_FuncDecl *)n)->Type);

	case AST_File:
		return ((const ast_File *)n)->Package;
	default:
		break;
	}
	fail("go/ast: Pos of unexpected node kind %d", (int)n->kind);
}

static token_Pos ast_Node_End(const ast_Node *n)
{
loop:
	switch (n->kind) {
	case AST_Comment: {
		const ast_Comment *c = (const ast_Comment *)n;
		return c->Slash + c->Text.n;
	}
	case AST_CommentGroup:
		TAIL(AST_LAST(((const ast_CommentGroup *)n)->List));
	case AST_Field: {
		const ast_Field *f = (const ast_Field *)n;
		if (f->Tag)
			TAIL(f->Tag);
		if (f->Type)
			TAIL(f->Type);
		if (f->Names.n > 0)
			TAIL(AST_LAST(f->Names));
		return token_NoPos;
	}
	case AST_FieldList: {
		const ast_FieldList *f = (const ast_FieldList *)n;
		if (token_Pos_IsValid(f->Closing))
			return f->Closing + 1;
		if (f->List.n > 0)
			TAIL(AST_LAST(f->List));
		return token_NoPos;
	}
	case AST_BadExpr:
		return ((const ast_BadExpr *)n)->To;
	case AST_Ident: {
		const ast_Ident *x = (const ast_Ident *)n;
		return x->NamePos + x->Name.n;
	}
	case AST_Ellipsis: {
		const ast_Ellipsis *x = (const ast_Ellipsis *)n;
		if (x->Elt)
			TAIL(x->Elt);
		return x->Ellipsis + 3; /* len("...") */
	}
	case AST_BasicLit: {
		const ast_BasicLit *x = (const ast_BasicLit *)n;
		if (!token_Pos_IsValid(x->ValueEnd)) {
			/* Not from parser; use a heuristic. (Incorrect for `...` containing \r\n; see
			 * https://go.dev/issue/76031.) */
			return x->ValuePos + x->Value.n;
		}
		return x->ValueEnd;
	}
	case AST_FuncLit:
		TAIL(((const ast_FuncLit *)n)->Body);
	case AST_CompositeLit:
		return ((const ast_CompositeLit *)n)->Rbrace + 1;
	case AST_ParenExpr:
		return ((const ast_ParenExpr *)n)->Rparen + 1;
	case AST_SelectorExpr:
		TAIL(((const ast_SelectorExpr *)n)->Sel);
	case AST_IndexExpr:
		return ((const ast_IndexExpr *)n)->Rbrack + 1;
	case AST_IndexListExpr:
		return ((const ast_IndexListExpr *)n)->Rbrack + 1;
	case AST_SliceExpr:
		return ((const ast_SliceExpr *)n)->Rbrack + 1;
	case AST_TypeAssertExpr:
		return ((const ast_TypeAssertExpr *)n)->Rparen + 1;
	case AST_CallExpr:
		return ((const ast_CallExpr *)n)->Rparen + 1;
	case AST_StarExpr:
		TAIL(((const ast_StarExpr *)n)->X);
	case AST_UnaryExpr:
		TAIL(((const ast_UnaryExpr *)n)->X);
	case AST_BinaryExpr:
		TAIL(((const ast_BinaryExpr *)n)->Y);
	case AST_KeyValueExpr:
		TAIL(((const ast_KeyValueExpr *)n)->Value);
	case AST_ArrayType:
		TAIL(((const ast_ArrayType *)n)->Elt);
	case AST_StructType:
		TAIL(((const ast_StructType *)n)->Fields);
	case AST_FuncType: {
		const ast_FuncType *x = (const ast_FuncType *)n;
		if (x->Results)
			TAIL(x->Results);
		TAIL(x->Params);
	}
	case AST_InterfaceType:
		TAIL(((const ast_InterfaceType *)n)->Methods);
	case AST_MapType:
		TAIL(((const ast_MapType *)n)->Value);
	case AST_ChanType:
		TAIL(((const ast_ChanType *)n)->Value);

	case AST_BadStmt:
		return ((const ast_BadStmt *)n)->To;
	case AST_DeclStmt:
		TAIL(((const ast_DeclStmt *)n)->Decl);
	case AST_EmptyStmt: {
		const ast_EmptyStmt *s = (const ast_EmptyStmt *)n;
		if (s->Implicit)
			return s->Semicolon;
		return s->Semicolon + 1; /* len(";") */
	}
	case AST_LabeledStmt:
		TAIL(((const ast_LabeledStmt *)n)->Stmt);
	case AST_ExprStmt:
		TAIL(((const ast_ExprStmt *)n)->X);
	case AST_SendStmt:
		TAIL(((const ast_SendStmt *)n)->Value);
	case AST_IncDecStmt:
		return ((const ast_IncDecStmt *)n)->TokPos + 2; /* len("++") */
	case AST_AssignStmt:
		TAIL(AST_LAST(((const ast_AssignStmt *)n)->Rhs));
	case AST_GoStmt:
		TAIL(((const ast_GoStmt *)n)->Call);
	case AST_DeferStmt:
		TAIL(((const ast_DeferStmt *)n)->Call);
	case AST_ReturnStmt: {
		const ast_ReturnStmt *s = (const ast_ReturnStmt *)n;
		if (s->Results.n > 0)
			TAIL(AST_LAST(s->Results));
		return s->Return + 6; /* len("return") */
	}
	case AST_BranchStmt: {
		const ast_BranchStmt *s = (const ast_BranchStmt *)n;
		if (s->Label)
			TAIL(s->Label);
		return s->TokPos + token_Token_String(s->Tok).n;
	}
	case AST_BlockStmt: {
		const ast_BlockStmt *s = (const ast_BlockStmt *)n;
		if (token_Pos_IsValid(s->Rbrace))
			return s->Rbrace + 1;
		if (s->List.n > 0)
			TAIL(AST_LAST(s->List));
		return s->Lbrace + 1;
	}
	case AST_IfStmt: {
		const ast_IfStmt *s = (const ast_IfStmt *)n;
		if (s->Else)
			TAIL(s->Else);
		TAIL(s->Body);
	}
	case AST_CaseClause: {
		const ast_CaseClause *s = (const ast_CaseClause *)n;
		if (s->Body.n > 0)
			TAIL(AST_LAST(s->Body));
		return s->Colon + 1;
	}
	case AST_SwitchStmt:
		TAIL(((const ast_SwitchStmt *)n)->Body);
	case AST_TypeSwitchStmt:
		TAIL(((const ast_TypeSwitchStmt *)n)->Body);
	case AST_CommClause: {
		const ast_CommClause *s = (const ast_CommClause *)n;
		if (s->Body.n > 0)
			TAIL(AST_LAST(s->Body));
		return s->Colon + 1;
	}
	case AST_SelectStmt:
		TAIL(((const ast_SelectStmt *)n)->Body);
	case AST_ForStmt:
		TAIL(((const ast_ForStmt *)n)->Body);
	case AST_RangeStmt:
		TAIL(((const ast_RangeStmt *)n)->Body);

	case AST_ImportSpec: {
		const ast_ImportSpec *s = (const ast_ImportSpec *)n;
		if (s->EndPos != 0)
			return s->EndPos;
		TAIL(s->Path);
	}
	case AST_ValueSpec: {
		const ast_ValueSpec *s = (const ast_ValueSpec *)n;
		if (s->Values.n > 0)
			TAIL(AST_LAST(s->Values));
		if (s->Type)
			TAIL(s->Type);
		TAIL(AST_LAST(s->Names));
	}
	case AST_TypeSpec:
		TAIL(((const ast_TypeSpec *)n)->Type);

	case AST_BadDecl:
		return ((const ast_BadDecl *)n)->To;
	case AST_GenDecl: {
		const ast_GenDecl *d = (const ast_GenDecl *)n;
		if (token_Pos_IsValid(d->Rparen))
			return d->Rparen + 1;
		TAIL(AST_FIRST(d->Specs));
	}
	case AST_FuncDecl: {
		const ast_FuncDecl *d = (const ast_FuncDecl *)n;
		if (d->Body)
			TAIL(d->Body);
		TAIL(d->Type);
	}

	case AST_File: {
		const ast_File *f = (const ast_File *)n;
		if (f->Decls.n > 0)
			TAIL(AST_LAST(f->Decls));
		TAIL(f->Name);
	}
	default:
		break;
	}
	fail("go/ast: End of unexpected node kind %d", (int)n->kind);
}

#undef TAIL

GO_HELPER ast_Ident *ast_NewIdent(GoStr name)
{
	ast_Ident *id = AST_NEW(Ident);
	id->Name = name;
	return id;
}

GO_HELPER bool ast_IsExported(GoStr name) { return token_IsExported(name); }

GO_HELPER int ast_FieldList_NumFields(const ast_FieldList *f)
{
	int n = 0;
	if (f) {
		for (int i = 0; i < f->List.n; i++) {
			int m = VEC_AT(f->List, ast_Field, i)->Names.n;
			if (m == 0)
				m = 1;
			n += m;
		}
	}
	return n;
}

static bool ast_isWhitespace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }

static GoStr ast_stripTrailingWhitespace(GoStr s)
{
	int i = s.n;
	while (i > 0 && ast_isWhitespace(s.p[i - 1]))
		i--;
	return gostr_sub(s, 0, i);
}

/* isDirective reports whether c is a comment directive. This code is also in go/printer. */
static bool ast_isDirective(GoStr c)
{
	if (strings_HasPrefix(c, GOSTR("line ")) || strings_HasPrefix(c, GOSTR("extern ")) ||
	    strings_HasPrefix(c, GOSTR("export ")))
		return true;
	int colon = strings_Index(c, GOSTR(":"));
	if (colon <= 0 || colon + 1 >= c.n)
		return false;
	for (int i = 0; i <= colon + 1; i++) {
		if (i == colon)
			continue;
		char b = c.p[i];
		if (!(('a' <= b && b <= 'z') || ('0' <= b && b <= '9')))
			return false;
	}
	return true;
}

/* Text returns the text of the comment (see ast.CommentGroup.Text). */
GO_HELPER GoStr ast_CommentGroup_Text(const ast_CommentGroup *g)
{
	if (!g)
		return GOSTR("");
	GoStrVec lines = {0};
	for (int i = 0; i < g->List.n; i++) {
		GoStr c = VEC_AT(g->List, ast_Comment, i)->Text;
		switch (c.p[1]) {
		case '/':
			c = gostr_from(c, 2);
			if (c.n == 0)
				break;
			if (c.p[0] == ' ') {
				c = gostr_from(c, 1);
				break;
			}
			if (ast_isDirective(c))
				continue;
			break;
		case '*':
			c = gostr_sub(c, 2, c.n - 2);
			break;
		}
		GoStrVec cl = strings_Split(c, GOSTR("\n"));
		for (int j = 0; j < cl.n; j++)
			gostrvec_push(&lines, ast_stripTrailingWhitespace(cl.v[j]));
	}
	int n = 0;
	for (int i = 0; i < lines.n; i++) {
		GoStr line = lines.v[i];
		if (line.n != 0 || (n > 0 && lines.v[n - 1].n != 0))
			lines.v[n++] = line;
	}
	lines.n = n;
	if (n > 0 && lines.v[n - 1].n != 0)
		gostrvec_push(&lines, GOSTR(""));
	return strings_Join(lines.v, lines.n, GOSTR("\n"));
}

GO_HELPER ast_Expr *ast_Unparen(ast_Expr *e)
{
	while (e->kind == AST_ParenExpr)
		e = ((ast_ParenExpr *)e)->X;
	return e;
}
