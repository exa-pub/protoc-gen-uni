/* Port of the parts of dart_style 3.1.13 lib/src/ast_extensions.dart that the short style uses,
 * plus the analyzer AST properties they rely on (isCascaded, isNamed, ...).
 *
 * Copyright (c) 2023, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

static DS_UNUSED bool is_expression_kind(NodeKind k)
{
	switch (k) {
	case N_SimpleIdentifier: case N_PrefixedIdentifier: case N_PropertyAccess: case N_MethodInvocation:
	case N_FunctionExpressionInvocation: case N_InstanceCreationExpression: case N_IndexExpression:
	case N_NamedArgument: case N_IntegerLiteral: case N_DoubleLiteral: case N_BooleanLiteral: case N_NullLiteral:
	case N_SimpleStringLiteral: case N_AdjacentStrings: case N_StringInterpolation: case N_ListLiteral:
	case N_SetOrMapLiteral: case N_ParenthesizedExpression: case N_BinaryExpression: case N_PrefixExpression:
	case N_PostfixExpression: case N_AssignmentExpression: case N_ConditionalExpression: case N_AsExpression:
	case N_IsExpression: case N_CascadeExpression: case N_FunctionExpression: case N_ThisExpression:
	case N_SuperExpression: case N_ThrowExpression: case N_RethrowExpression: case N_AwaitExpression:
	case N_SymbolLiteral:
		return true;
	default:
		return false;
	}
}

static bool is_string_literal(const Node *n)
{
	return n && (n->kind == N_SimpleStringLiteral || n->kind == N_StringInterpolation || n->kind == N_AdjacentStrings);
}

/* PropertyAccess.isCascaded / IndexExpression.isCascaded */
static bool node_isCascaded(const Node *n)
{
	if (n->kind == N_PropertyAccess)
		return n->op->type == TT_PERIOD_PERIOD || n->op->type == TT_QUESTION_PERIOD_PERIOD;
	if (n->kind == N_IndexExpression)
		return n->op != NULL;
	return false;
}

/* Argument.argumentExpression */
static Node *argument_expression(Node *n) { return n->kind == N_NamedArgument ? n->expr : n; }

/* AstNode.commaAfter */
static Token *node_commaAfter(const Node *n)
{
	Token *next = n->end->next;
	if (next->type == TT_COMMA)
		return next;
	if (next->type == TT_QUESTION && next->next->type == TT_COMMA)
		return next->next;
	return NULL;
}

static bool node_hasCommaAfter(const Node *n) { return node_commaAfter(n) != NULL; }

/* Iterable<AstNode>.hasCommaAfter */
static bool list_hasCommaAfter(const NodeList *l) { return l->n > 0 && node_hasCommaAfter(l->v[l->n - 1]); }

/* Iterable<AstNode>.canSplit */
static bool list_canSplit(const NodeList *l, const Token *rightBracket)
{
	return l->n > 0 || rightBracket->precedingComments != NULL;
}

/* Token.hasLineCommentBefore */
static bool token_hasLineCommentBefore(const Token *t)
{
	for (Token *c = t->precedingComments; c; c = c->next)
		if (c->type == TT_SINGLE_LINE_COMMENT)
			return true;
	return false;
}

/* Iterable<AstNode>.containsLineComments */
static bool list_containsLineComments(const NodeList *l, const Token *rightBracket)
{
	for (int i = 0; i < l->n; i++)
		if (token_hasLineCommentBefore(l->v[i]->begin))
			return true;
	return rightBracket ? token_hasLineCommentBefore(rightBracket) : false;
}

/* AstNode.hasNonEmptyBody */
static bool node_hasNonEmptyBody(const Node *n)
{
	const Node *body = NULL;
	if (n->kind == N_MethodDeclaration)
		body = n->body;
	else if (n->kind == N_FunctionDeclarationStatement)
		body = n->decl->func->body;
	else if (n->kind == N_FunctionDeclaration)
		body = n->func->body;
	return body && body->kind == N_BlockFunctionBody && body->body->list.n > 0;
}

static bool node_isCollectionLiteral(const Node *n)
{
	return n->kind == N_ListLiteral || n->kind == N_SetOrMapLiteral;
}

/* AstNode.isFunctionExpressionBody */
static bool node_isFunctionExpressionBody(const Node *n)
{
	return n->parent && n->parent->kind == N_FunctionExpression &&
	       !(n->parent->parent && n->parent->parent->kind == N_FunctionDeclaration);
}

/* Expression.isTrailingCommaArgument */
static bool node_isTrailingCommaArgument(const Node *n)
{
	Node *parent = n->parent;
	if (parent && parent->kind == N_NamedArgument)
		parent = parent->parent;
	return parent && parent->kind == N_ArgumentList && list_hasCommaAfter(&parent->list);
}

static bool looksLikeClassName(const char *name)
{
	if (!strcmp(name, "bool") || !strcmp(name, "double") || !strcmp(name, "int") || !strcmp(name, "num"))
		return true;
	size_t len = strlen(name);
	size_t start = 0;
	unsigned char firstChar = (unsigned char)name[start++];
	if (firstChar == '_') {
		if (len == 1)
			return false;
		firstChar = (unsigned char)name[start++];
	}
	if (firstChar < 'A' || firstChar > 'Z')
		return false;
	for (size_t i = start; i < len; i++) {
		unsigned char c = (unsigned char)name[i];
		if (c >= 'a' && c <= 'z')
			return true;
	}
	return false;
}

/* Expression.looksLikeStaticCall */
static bool node_looksLikeStaticCall(const Node *n)
{
	if (n->kind != N_MethodInvocation)
		return false;
	if (!n->target)
		return false;
	if (n->target->kind == N_SimpleIdentifier && looksLikeClassName(n->ident->name->lexeme))
		return true;
	const Node *target = n->target;
	if (target->kind == N_PrefixedIdentifier)
		target = target->ident;
	return target->kind == N_SimpleIdentifier && looksLikeClassName(target->name->lexeme);
}

/* CascadeExpression.allowInline */
static bool cascade_allowInline(const Node *n)
{
	if (n->list.n > 1)
		return false;
	switch (n->target->kind) {
	case N_ConditionalExpression: case N_BinaryExpression: case N_PrefixExpression: case N_AwaitExpression:
		return false;
	default:
		return true;
	}
}

/* TokenType.precedence of binary operators (0: not a binary operator). */
static int binary_precedence(TokenType t)
{
	switch (t) {
	case TT_QUESTION_QUESTION: return 4;
	case TT_BAR_BAR: return 5;
	case TT_AMPERSAND_AMPERSAND: return 6;
	case TT_EQ_EQ: case TT_BANG_EQ: return 7;
	case TT_LT: case TT_GT: case TT_LT_EQ: case TT_GT_EQ: return 8;
	case TT_BAR: return 9;
	case TT_CARET: return 10;
	case TT_AMPERSAND: return 11;
	case TT_LT_LT: case TT_GT_GT: case TT_GT_GT_GT: return 12;
	case TT_PLUS: case TT_MINUS: return 13;
	case TT_STAR: case TT_SLASH: case TT_TILDE_SLASH: case TT_PERCENT: return 14;
	default: return 0;
	}
}
