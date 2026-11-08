/* Port of dart_style 3.1.13 lib/src/short/source_visitor.dart (without selections; visit methods
 * of AST nodes outside the parsed subset are not ported).
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

typedef struct SourceVisitor SourceVisitor;
struct SourceVisitor {
	ChunkBuilder *builder;
	const LineInfo *lineInfo;
	Token *lastToken;
	int *collectionSplits;
	int ncollectionSplits, capcollectionSplits;
	PtrMap blockRules;          /* Token -> index into blockRuleVals */
	PtrVec blockRuleVals;
	PtrMap blockPreviousChunks; /* Token -> index into blockChunkVals */
	PtrVec blockChunkVals;
	PtrMap suppressPrecedingCommentsAndNewLines;
};

typedef void (*SvFn)(SourceVisitor *v);

static void sv_visit(SourceVisitor *v, Node *node);
static void sv_visit_ba(SourceVisitor *v, Node *node, SvFn before, SvFn after);
static void sv_token(SourceVisitor *v, Token *token);
static void sv_token_ba(SourceVisitor *v, Token *token, SvFn before, SvFn after);
static bool sv_writePrecedingCommentsAndNewlines(SourceVisitor *v, Token *token);
static void sv_writeText(SourceVisitor *v, const char *text, size_t n, Token *token, bool mergeEmptySplits);
static void sv_space(SourceVisitor *v);
static void sv_newline(SourceVisitor *v);
static void sv_twoNewlines(SourceVisitor *v);
static void sv_oneOrTwoNewlines(SourceVisitor *v);
static Chunk *sv_split(SourceVisitor *v);
static Chunk *sv_zeroSplit(SourceVisitor *v);
static Rule *sv_soloSplit(SourceVisitor *v, int cost);
static void sv_soloZeroSplit(SourceVisitor *v);
static void sv_modifier(SourceVisitor *v, Token *modifier);
static void sv_visitArgumentList(SourceVisitor *v, Node *node, bool nestExpression);
static void sv_visitCollectionLiteral(SourceVisitor *v, Token *leftBracket, NodeList *elements, Token *rightBracket,
				      Token *constKeyword, Node *typeArguments, int cost, bool splitOuterCollection,
				      bool isRecord);
static void sv_visitNamedNode(SourceVisitor *v, Token *name, Token *colon, Node *node, Rule *rule);
static void sv_finishIndexExpression(SourceVisitor *v, Node *node);
static void sv_beforeBlock(SourceVisitor *v, Token *token, Rule *rule, Chunk *previousChunk);
static void sv_visitFormalParameterList(SourceVisitor *v, Node *node, bool nestExpression);

/* Splits are written by name in callbacks (`between: split`), whose results are ignored. */
static void sv_splitFn(SourceVisitor *v) { sv_split(v); }
static void sv_soloSplitFn(SourceVisitor *v) { sv_soloSplit(v, Cost_normal); }
static void sv_zeroSplitFn(SourceVisitor *v) { sv_zeroSplit(v); }

static int sv_startLine(SourceVisitor *v, const Token *t) { return line_number(v->lineInfo, t->offset); }
static int sv_endLine(SourceVisitor *v, const Token *t) { return line_number(v->lineInfo, tok_end(t)); }
static int sv_startColumn(SourceVisitor *v, const Token *t) { return column_number(v->lineInfo, t->offset); }

#include "argument_list_visitor.c"
#include "call_chain_visitor.c"

static void sv_visit_ba(SourceVisitor *v, Node *node, SvFn before, SvFn after)
{
	if (!node)
		return;
	if (before)
		before(v);
	sv_visit(v, node);
	if (after)
		after(v);
}

static void sv_visitNodes(SourceVisitor *v, NodeList *nodes, SvFn before, SvFn between, SvFn after)
{
	if (nodes->n == 0)
		return;
	if (before)
		before(v);
	sv_visit(v, nodes->v[0]);
	for (int i = 1; i < nodes->n; i++) {
		if (between)
			between(v);
		sv_visit(v, nodes->v[i]);
	}
	if (after)
		after(v);
}

static void sv_visitCommaSeparatedNodes(SourceVisitor *v, NodeList *nodes, SvFn between)
{
	if (nodes->n == 0)
		return;
	if (!between)
		between = sv_space;
	for (int i = 0; i < nodes->n; i++) {
		if (i > 0)
			between(v);
		sv_visit(v, nodes->v[i]);
		Token *next = nodes->v[i]->end->next;
		if (strcmp(next->lexeme, ",") == 0)
			sv_token(v, next);
	}
}

static void sv_visitMetadata(SourceVisitor *v, NodeList *metadata) { sv_visitNodes(v, metadata, NULL, sv_newline, sv_newline); }

static void sv_simpleStatement_begin(SourceVisitor *v) { cb_nestExpression(v->builder, -1, false); }

static void sv_simpleStatement_end(SourceVisitor *v, Token *semicolon)
{
	sv_token(v, semicolon);
	cb_unnest(v->builder, true);
}

static void sv_writeCommaAfter(SourceVisitor *v, Node *node) { sv_token(v, node_commaAfter(node)); }

/* ---- visit methods ---- */

static void sv_visitAdjacentStrings(SourceVisitor *v, Node *node)
{
	bool shouldNest = true;
	Node *parent = node->parent;
	if (parent->kind == N_ArgumentList) {
		shouldNest = false;
		for (int i = 0; i < parent->list.n; i++) {
			Node *argument = parent->list.v[i];
			if (argument == node)
				continue;
			if (is_string_literal(argument)) {
				shouldNest = true;
				break;
			}
		}
	} else if (parent->kind == N_AssertStatement || parent->kind == N_AssertInitializer) {
		shouldNest = false;
		if (parent->expr != node && is_string_literal(parent->expr))
			shouldNest = true;
		if (parent->expr2 != node && is_string_literal(parent->expr2))
			shouldNest = true;
	} else if (parent->kind == N_VariableDeclaration ||
		   (parent->kind == N_AssignmentExpression && parent->expr2 == node && parent->parent &&
		    parent->parent->kind == N_ExpressionStatement)) {
		shouldNest = false;
	} else if (parent->kind == N_NamedArgument || parent->kind == N_ExpressionFunctionBody) {
		shouldNest = false;
	}
	ChunkBuilder *b = v->builder;
	cb_startSpan(b, Cost_normal);
	cb_startRule(b, NULL);
	if (shouldNest)
		cb_nestExpression(b, -1, false);
	/* visitNodes(node.strings, between: splitOrNewline) */
	for (int i = 0; i < node->list.n; i++) {
		if (i > 0) {
			Token *previous = v->lastToken;
			Token *next = previous->next;
			if (next->precedingComments)
				next = next->precedingComments;
			if (sv_startLine(v, next) - sv_endLine(v, previous) > 0)
				cb_writeNewline(v->builder, false, false, true);
			else
				sv_split(v);
		}
		sv_visit(v, node->list.v[i]);
	}
	if (shouldNest)
		cb_unnest(v->builder, true);
	cb_endRule(v->builder);
	cb_endSpan(v->builder);
}

static void sv_visitAnnotation(SourceVisitor *v, Node *node)
{
	sv_token(v, node->op);
	sv_visit(v, node->ident);
	cb_nestExpression(v->builder, -1, false);
	sv_visit_ba(v, node->typeArgs, NULL, NULL);
	sv_token(v, node->period);
	sv_visit_ba(v, node->ident2, NULL, NULL);
	if (node->args)
		sv_visitArgumentList(v, node->args, false);
	cb_unnest(v->builder, true);
}

static void sv_visitArgumentList(SourceVisitor *v, Node *node, bool nestExpression)
{
	if (node->list.n == 0) {
		sv_token(v, node->lp);
		if (node->rp->precedingComments)
			sv_soloZeroSplit(v);
		sv_token(v, node->rp);
		return;
	}
	if (list_hasCommaAfter(&node->list)) {
		sv_visitCollectionLiteral(v, node->lp, &node->list, node->rp, NULL, NULL, Cost_normal, false, false);
		return;
	}
	if (nestExpression)
		cb_nestExpression(v->builder, -1, false);
	ArgumentListVisitor alv;
	alv_init(&alv, v, node->lp, node->rp, &node->list);
	alv_visit(&alv);
	if (nestExpression)
		cb_unnest(v->builder, true);
}

static void sv_visitAsOrIs(SourceVisitor *v, Node *node)
{
	cb_startSpan(v->builder, Cost_normal);
	cb_nestExpression(v->builder, -1, false);
	sv_visit(v, node->expr);
	sv_soloSplit(v, Cost_normal);
	sv_token(v, node->op);
	sv_token(v, node->kw2);
	sv_space(v);
	sv_visit(v, node->type);
	cb_unnest(v->builder, true);
	cb_endSpan(v->builder);
}

static void sv_visitAssignment(SourceVisitor *v, Token *equalsOperator, Node *rightHandSide, bool nest);

static void sv_visitAssignmentExpression(SourceVisitor *v, Node *node)
{
	cb_nestExpression(v->builder, -1, false);
	sv_visit(v, node->expr);
	sv_visitAssignment(v, node->op, node->expr2, false);
	cb_unnest(v->builder, true);
}

static void sv_visitAwaitExpression(SourceVisitor *v, Node *node)
{
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->expr);
}

static void sv_visitBinary(SourceVisitor *v, Node *node, int precedence, bool nest);

static void sv_visitBinaryExpression(SourceVisitor *v, Node *node)
{
	bool nest = !(node->parent && node->parent->kind == N_ExpressionFunctionBody);
	sv_visitBinary(v, node, binary_precedence(node->op->type), nest);
}

static bool sv_splitEmptyBlock(Node *node);
static void sv_visitBody(SourceVisitor *v, Token *leftBracket, NodeList *nodes, Token *rightBracket);
static void sv_beginBody(SourceVisitor *v, Token *leftBracket, bool space);
static void sv_endBody(SourceVisitor *v, Token *rightBracket, bool forceSplit);
static void sv_visitBodyContents(SourceVisitor *v, NodeList *nodes);

static void sv_visitBlock(SourceVisitor *v, Node *node)
{
	if (!list_canSplit(&node->list, node->rb)) {
		sv_token(v, node->lb);
		if (sv_splitEmptyBlock(node))
			sv_newline(v);
		sv_token(v, node->rb);
		return;
	}
	sv_visitBody(v, node->lb, &node->list, node->rb);
}

static void sv_splitOrTwoNewlines(SourceVisitor *v);

static void sv_visitBlockEnumBody(SourceVisitor *v, Node *node)
{
	sv_beginBody(v, node->lb, true);
	sv_visitCommaSeparatedNodes(v, &node->list, sv_splitOrTwoNewlines);
	Node *last = node->list.v[node->list.n - 1];
	Token *trailingComma = node_commaAfter(last);
	if (trailingComma)
		cb_forceRules(v->builder);
	Token *afterConstants = last->end->next;
	Token *semicolon = NULL;
	if (afterConstants->type == TT_SEMICOLON)
		semicolon = last->end->next;
	else if (trailingComma && trailingComma->next->type == TT_SEMICOLON)
		semicolon = afterConstants->next;
	if (semicolon) {
		if (trailingComma)
			sv_newline(v);
		sv_token(v, semicolon);
		if (node->list2.n > 0)
			sv_twoNewlines(v);
	}
	sv_visitBodyContents(v, &node->list2);
	sv_endBody(v, node->rb,
		   semicolon || trailingComma || node->list2.n > 0 || list_containsLineComments(&node->list, NULL));
}

static void sv_visitBlockFunctionBody(SourceVisitor *v, Node *node)
{
	sv_space(v);
	sv_token(v, node->kw);
	sv_token(v, node->star);
	if (node->kw)
		sv_space(v);
	sv_visit(v, node->body);
}

static void sv_visitSplitCascade(SourceVisitor *v, Node *node);

static void sv_visitCascadeExpression(SourceVisitor *v, Node *node)
{
	if (node->list.n > 1) {
		sv_visitSplitCascade(v, node);
		return;
	}
	Node *target = node->target;
	bool splitIfTargetSplits = true;
	if (node->list.n > 1) {
	} else if (node_isCollectionLiteral(target)) {
		splitIfTargetSplits = false;
	} else if (target->kind == N_MethodInvocation || target->kind == N_FunctionExpressionInvocation) {
		splitIfTargetSplits = !list_hasCommaAfter(&target->args->list);
	} else if (target->kind == N_InstanceCreationExpression) {
		splitIfTargetSplits = !list_hasCommaAfter(&target->args->list);
	}
	ChunkBuilder *b = v->builder;
	if (splitIfTargetSplits)
		cb_startLazyRule(b, cascade_allowInline(node) ? rule_new() : rule_hard());
	sv_visit(v, node->target);
	b = v->builder;
	cb_nestExpression(b, Indent_cascade, true);
	cb_startBlockArgumentNesting(b);
	if (!splitIfTargetSplits)
		cb_startRule(b, cascade_allowInline(node) ? rule_new() : rule_hard());
	sv_zeroSplit(v);
	if (!splitIfTargetSplits)
		cb_endRule(v->builder);
	sv_visitNodes(v, &node->list, NULL, sv_zeroSplitFn, NULL);
	if (splitIfTargetSplits)
		cb_endRule(v->builder);
	cb_endBlockArgumentNesting(v->builder);
	cb_unnest(v->builder, true);
}

static void sv_visitSplitCascade(SourceVisitor *v, Node *node)
{
	cb_startLazyRule(v->builder, rule_hard());
	sv_visit(v, node->target);
	cb_nestExpression(v->builder, Indent_cascade, true);
	cb_startBlockArgumentNesting(v->builder);
	Token *firstCommentToken = node->list.v[0]->begin;
	sv_writePrecedingCommentsAndNewlines(v, firstCommentToken);
	ps_add(&v->suppressPrecedingCommentsAndNewLines, firstCommentToken);
	v->builder = cb_startBlock(v->builder, NULL, false, false);
	for (int i = 0; i < node->list.n - 1; i++) {
		sv_newline(v);
		sv_visit(v, node->list.v[i]);
	}
	Token *lastCommentToken = node->list.v[node->list.n - 1]->begin;
	sv_writePrecedingCommentsAndNewlines(v, lastCommentToken);
	ps_add(&v->suppressPrecedingCommentsAndNewLines, lastCommentToken);
	v->builder = cb_endBlock(v->builder, true);
	sv_visit(v, node->list.v[node->list.n - 1]);
	cb_endRule(v->builder);
	cb_endBlockArgumentNesting(v->builder);
	cb_unnest(v->builder, true);
}

static void sv_visitClauses(SourceVisitor *v, Node *withClause, Node *implementsClause)
{
	cb_startRule(v->builder, combinator_rule_new());
	sv_visit_ba(v, withClause, NULL, NULL);
	sv_visit_ba(v, implementsClause, NULL, NULL);
	cb_endRule(v->builder);
}

static void sv_visitClassDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	cb_nestExpression(v->builder, -1, false);
	sv_modifier(v, node->m_abstract);
	sv_modifier(v, node->m_base);
	sv_modifier(v, node->m_interface);
	sv_modifier(v, node->m_final);
	sv_modifier(v, node->m_sealed);
	sv_modifier(v, node->m_mixin);
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->namePart);
	sv_visit_ba(v, node->clause1, NULL, NULL);
	sv_visitClauses(v, node->clause2, node->clause3);
	if (node->body->kind != N_EmptyClassBody)
		sv_space(v);
	cb_unnest(v->builder, true);
	sv_visit(v, node->body);
}

static void sv_visitCompilationUnit(SourceVisitor *v, Node *node)
{
	if (node->tok) {
		/* visitScriptTag: the lexeme includes the trailing newline */
		Token *t = node->tok;
		size_t n = (size_t)t->len;
		const char *s = t->lexeme;
		while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
			n--;
		sv_writeText(v, s, n, t, true);
		sv_twoNewlines(v);
	}
	int start = 0;
	if (node->list.n > 0 && node->list.v[0]->kind == N_LibraryDirective) {
		sv_visit(v, node->list.v[0]);
		sv_twoNewlines(v);
		start = 1;
	}
	for (int i = start; i < node->list.n; i++) {
		if (i > start)
			sv_oneOrTwoNewlines(v);
		sv_visit(v, node->list.v[i]);
	}
	bool needsDouble = true;
	for (int i = 0; i < node->list2.n; i++) {
		Node *declaration = node->list2.v[i];
		bool hasBody = declaration->kind == N_ClassDeclaration || declaration->kind == N_EnumDeclaration;
		if (hasBody)
			needsDouble = true;
		if (needsDouble)
			sv_twoNewlines(v);
		else
			sv_oneOrTwoNewlines(v);
		sv_visit(v, declaration);
		needsDouble = false;
		if (hasBody) {
			needsDouble = true;
		} else if (declaration->kind == N_FunctionDeclaration) {
			Node *body = declaration->func->body;
			if (body->kind == N_BlockFunctionBody)
				needsDouble = body->body->list.n > 0;
		}
	}
}

static void sv_visitConditionalExpression(SourceVisitor *v, Node *node)
{
	ChunkBuilder *b = v->builder;
	cb_nestExpression(b, -1, false);
	cb_startLazyRule(b, NULL);
	sv_visit(v, node->expr);
	b = v->builder;
	cb_nestExpression(b, Indent_block, true);
	cb_startBlockArgumentNesting(b);
	cb_unnest(b, true);
	cb_startSpan(b, Cost_normal);
	sv_split(v);
	sv_token(v, node->question);
	sv_space(v);
	cb_nestExpression(v->builder, -1, false);
	sv_visit(v, node->expr2);
	cb_unnest(v->builder, true);
	sv_split(v);
	sv_token(v, node->colon);
	sv_space(v);
	sv_visit(v, node->expr3);
	if (node->parent && node->parent->kind == N_ConditionalExpression)
		cb_forceRules(v->builder);
	cb_endRule(v->builder);
	cb_endSpan(v->builder);
	cb_endBlockArgumentNesting(v->builder);
	cb_unnest(v->builder, true);
}

static void sv_visitFunctionBody(SourceVisitor *v, Node *typeParameters, Node *parameters, Node *body,
				 void (*beforeBody)(SourceVisitor *, Node *), Node *ctx);

static void sv_visitConstructorInitializers(SourceVisitor *v, Node *node)
{
	bool hasTrailingComma = list_hasCommaAfter(&node->params->list);
	ChunkBuilder *b = v->builder;
	if (hasTrailingComma) {
		sv_space(v);
		if (node->list.n > 1) {
			const char *padding = "  ";
			Node *last = node->params->list.v[node->params->list.n - 1];
			if (last->flags & (NF_NAMED | NF_OPTIONAL_POSITIONAL))
				padding = " ";
			sv_writeText(v, padding, strlen(padding), node->separator, true);
		}
		sv_token(v, node->separator);
		sv_space(v);
		cb_indent(v->builder, 6);
	} else {
		cb_indent(b, Indent_constructorInitializer);
		sv_split(v);
		sv_token(v, node->separator);
		sv_space(v);
		cb_indent(v->builder, 2);
	}
	for (int i = 0; i < node->list.n; i++) {
		if (i > 0) {
			sv_token(v, node->list.v[i]->begin->previous);
			sv_newline(v);
		}
		sv_visit(v, node->list.v[i]);
	}
	cb_unindent(v->builder);
	if (!hasTrailingComma)
		cb_unindent(v->builder);
}

static void sv_constructorBeforeBody(SourceVisitor *v, Node *node)
{
	if (node->redirect) {
		/* _visitConstructorRedirects */
		sv_token_ba(v, node->separator, sv_space, NULL);
		sv_soloSplit(v, Cost_normal);
		sv_visitCommaSeparatedNodes(v, &node->list, NULL);
		sv_visit(v, node->redirect);
		cb_unnest(v->builder, true);
	} else if (node->list.n > 0) {
		sv_visitConstructorInitializers(v, node);
		cb_endRule(v->builder);
	}
}

static void sv_visitConstructorDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	sv_modifier(v, node->m_external);
	sv_modifier(v, node->m_const);
	sv_modifier(v, node->m_new);
	sv_modifier(v, node->m_factory);
	sv_visit(v, node->ident);
	sv_token(v, node->period);
	sv_token(v, node->name);
	if (node->list.n > 0)
		cb_startRule(v->builder, NULL);
	if (node->redirect)
		cb_nestExpression(v->builder, -1, false);
	sv_visitFunctionBody(v, NULL, node->params, node->body, sv_constructorBeforeBody, node);
}

static void sv_visitConstructorName(SourceVisitor *v, Node *node)
{
	sv_visit(v, node->type);
	sv_token(v, node->period);
	sv_visit_ba(v, node->ident, NULL, NULL);
}

static void sv_visitEnumConstantDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	sv_token(v, node->name);
}

static void sv_visitEnumDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	cb_nestExpression(v->builder, -1, false);
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->namePart);
	sv_visitClauses(v, node->clause2, node->clause3);
	sv_space(v);
	cb_unnest(v->builder, true);
	sv_visit(v, node->body);
}

static void sv_visitCombinators(SourceVisitor *v, NodeList *combinators)
{
	cb_startRule(v->builder, combinator_rule_new());
	sv_visitNodes(v, combinators, NULL, NULL, NULL);
	cb_endRule(v->builder);
}

static void sv_visitDirectiveMetadata(SourceVisitor *v, Node *directive)
{
	Node *unit = directive->parent;
	bool isFirst = unit->list.n > 0 && directive == unit->list.v[0];
	sv_visitNodes(v, &directive->meta, NULL, sv_newline, isFirst ? sv_oneOrTwoNewlines : sv_newline);
}

static void sv_visitExportDirective(SourceVisitor *v, Node *node)
{
	sv_visitDirectiveMetadata(v, node);
	sv_simpleStatement_begin(v);
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->expr);
	sv_visitCombinators(v, &node->list);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_visitExpressionFunctionBody(SourceVisitor *v, Node *node)
{
	sv_space(v);
	sv_token(v, node->kw);
	sv_token(v, node->star);
	if (node->kw || node->star)
		sv_space(v);
	bool fe = node_isFunctionExpressionBody(node);
	if (fe)
		cb_startSpan(v->builder, Cost_normal);
	sv_token(v, node->op);
	sv_split(v);
	if (node->expr->kind != N_BinaryExpression)
		cb_endRule(v->builder);
	if (fe)
		cb_endSpan(v->builder);
	bool isArgWithTrailingComma = false;
	Node *parent = node->parent;
	if (parent && parent->kind == N_FunctionExpression)
		isArgWithTrailingComma = node_isTrailingCommaArgument(parent);
	if (!isArgWithTrailingComma)
		cb_startBlockArgumentNesting(v->builder);
	cb_startSpan(v->builder, Cost_normal);
	sv_visit(v, node->expr);
	cb_endSpan(v->builder);
	if (!isArgWithTrailingComma)
		cb_endBlockArgumentNesting(v->builder);
	if (node->expr->kind == N_BinaryExpression)
		cb_endRule(v->builder);
	sv_token(v, node->semi);
}

static void sv_visitExpressionStatement(SourceVisitor *v, Node *node)
{
	sv_simpleStatement_begin(v);
	sv_visit(v, node->expr);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_visitExtendsClause(SourceVisitor *v, Node *node)
{
	sv_soloSplit(v, Cost_normal);
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->type);
}

static void sv_visitFieldDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	sv_simpleStatement_begin(v);
	sv_modifier(v, node->m_external);
	sv_modifier(v, node->m_static);
	sv_modifier(v, node->m_abstract);
	sv_modifier(v, node->m_covariant);
	sv_visit(v, node->vars);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_beginFormalParameter(SourceVisitor *v, Node *node)
{
	cb_startLazyRule(v->builder, rule_new_cost(Cost_parameterType));
	cb_nestExpression(v->builder, -1, false);
	sv_modifier(v, node->m_required);
	sv_modifier(v, node->m_covariant);
}

static void sv_endFormalParameter(SourceVisitor *v)
{
	cb_unnest(v->builder, true);
	cb_endRule(v->builder);
}

static void sv_separatorBetweenTypeAndVariable(SourceVisitor *v, Node *type, bool isSolo)
{
	if (!type)
		return;
	bool isBlockType = false;
	if (type->kind == N_GenericFunctionType)
		isBlockType = type->params->list.n > 0 && list_hasCommaAfter(&type->params->list);
	if (isBlockType)
		sv_space(v);
	else if (isSolo)
		sv_soloSplit(v, Cost_normal);
	else
		sv_split(v);
}

static void sv_visitParameterSignature(SourceVisitor *v, Node *typeParameters, Node *parameters);

/* visitParameterMetadata(metadata, visitParameter) */
static void sv_parameterMetadata_begin(SourceVisitor *v, NodeList *metadata)
{
	if (metadata->n == 0)
		return;
	cb_startLazyRule(v->builder, NULL);
	sv_visitNodes(v, metadata, NULL, sv_splitFn, sv_splitFn);
}

static void sv_parameterMetadata_end(SourceVisitor *v, NodeList *metadata)
{
	if (metadata->n == 0)
		return;
	cb_endRule(v->builder);
}

static void sv_visitFieldOrSuperFormalParameter(SourceVisitor *v, Node *node)
{
	sv_parameterMetadata_begin(v, &node->meta);
	sv_beginFormalParameter(v, node);
	sv_token_ba(v, node->kw, NULL, sv_space);
	if (node->kind == N_FieldFormalParameter) {
		sv_visit_ba(v, node->type, NULL, NULL);
		sv_separatorBetweenTypeAndVariable(v, node->type, false);
	} else {
		sv_visit_ba(v, node->type, NULL, sv_splitFn);
	}
	sv_token(v, node->kw2);
	sv_token(v, node->period);
	sv_token(v, node->name);
	if (node->suffix) {
		sv_visit_ba(v, node->suffix->typeParams, NULL, NULL);
		sv_visit_ba(v, node->suffix->params, NULL, NULL);
		sv_token(v, node->suffix->question);
	}
	sv_endFormalParameter(v);
	sv_visit_ba(v, node->defaultClause, NULL, NULL);
	sv_parameterMetadata_end(v, &node->meta);
}

static int sv_assignmentCost(Node *rightHandSide)
{
	if (rightHandSide->kind == N_ListLiteral)
		return Cost_assignBlock;
	if (rightHandSide->kind == N_SetOrMapLiteral)
		return Cost_assignBlock;
	if (rightHandSide->kind == N_CascadeExpression)
		return Cost_assignBlock;
	return Cost_assign;
}

static void sv_visitFormalParameterDefaultClause(SourceVisitor *v, Node *node)
{
	cb_startSpan(v->builder, Cost_normal);
	cb_nestExpression(v->builder, -1, false);
	if (node->op->type == TT_EQ)
		sv_space(v);
	sv_token(v, node->op);
	sv_soloSplit(v, sv_assignmentCost(node->expr));
	sv_visit(v, node->expr);
	cb_unnest(v->builder, true);
	cb_endSpan(v->builder);
}

static void sv_visitTrailingCommaParameterList(SourceVisitor *v, Node *parameters);

static void sv_visitFormalParameterList(SourceVisitor *v, Node *node, bool nestExpression)
{
	if (node->list.n == 0) {
		sv_token(v, node->lp);
		if (node->rp->precedingComments)
			sv_soloZeroSplit(v);
		sv_token(v, node->rp);
		return;
	}
	if (list_hasCommaAfter(&node->list)) {
		sv_visitTrailingCommaParameterList(v, node);
		return;
	}
	NodeList requiredParams = {0}, optionalParams = {0};
	for (int i = 0; i < node->list.n; i++) {
		Node *param = node->list.v[i];
		if (param->flags & (NF_NAMED | NF_OPTIONAL_POSITIONAL))
			ds_push(&optionalParams, param);
		else
			ds_push(&requiredParams, param);
	}
	if (nestExpression)
		cb_nestExpression(v->builder, -1, false);
	sv_token(v, node->lp);
	Rule *rule = NULL;
	if (requiredParams.n > 0) {
		rule = positional_rule_new(NULL, requiredParams.n, 0, 0);
		cb_startRule(v->builder, rule);
		if (node_isFunctionExpressionBody(node))
			rule_beforeArgument(rule, NULL);
		else
			rule_beforeArgument(rule, sv_zeroSplit(v));
		cb_startBlockArgumentNesting(v->builder);
		cb_startSpan(v->builder, Cost_normal);
		for (int i = 0; i < requiredParams.n; i++) {
			Node *param = requiredParams.v[i];
			sv_visit(v, param);
			sv_writeCommaAfter(v, param);
			if (i != requiredParams.n - 1)
				rule_beforeArgument(rule, sv_split(v));
		}
		cb_endBlockArgumentNesting(v->builder);
		cb_endSpan(v->builder);
		cb_endRule(v->builder);
	}
	if (optionalParams.n > 0) {
		Rule *namedRule = named_rule_new(NULL, 0, 0);
		if (rule)
			positional_addNamedArgsConstraints(rule, namedRule);
		cb_startRule(v->builder, namedRule);
		cb_startBlockArgumentNesting(v->builder);
		rule_beforeArgument(namedRule, cb_split(v->builder, true, requiredParams.n > 0));
		sv_token(v, node->lb);
		for (int i = 0; i < optionalParams.n; i++) {
			Node *param = optionalParams.v[i];
			sv_visit(v, param);
			sv_writeCommaAfter(v, param);
			if (i != optionalParams.n - 1)
				rule_beforeArgument(namedRule, sv_split(v));
		}
		cb_endBlockArgumentNesting(v->builder);
		cb_endRule(v->builder);
		sv_token(v, node->rb);
	}
	sv_token(v, node->rp);
	if (nestExpression)
		cb_unnest(v->builder, true);
}

static void sv_visitIfCondition(SourceVisitor *v, Token *ifKeyword, Token *leftParenthesis, Node *condition,
				Token *rightParenthesis)
{
	cb_nestExpression(v->builder, -1, false);
	sv_token(v, ifKeyword);
	sv_space(v);
	sv_token(v, leftParenthesis);
	sv_visit(v, condition);
	sv_token(v, rightParenthesis);
	cb_unnest(v->builder, true);
}


static void sv_visitFunctionOrMethodDeclaration(SourceVisitor *v, NodeList *metadata, Token *externalKeyword,
						Token *propertyKeyword, Token *modifierKeyword, Token *operatorKeyword,
						Token *name, Node *returnType, Node *typeParameters,
						Node *formalParameters, Node *body);

static void sv_visitFunctionExpression(SourceVisitor *v, Node *node)
{
	sv_visitFunctionBody(v, node->typeParams, node->params, node->body, NULL, NULL);
}

static void sv_visitGenericFunctionType(SourceVisitor *v, Node *node)
{
	cb_startLazyRule(v->builder, NULL);
	cb_nestExpression(v->builder, -1, false);
	sv_visit_ba(v, node->type, NULL, sv_splitFn);
	sv_token(v, node->kw);
	cb_unnest(v->builder, true);
	cb_endRule(v->builder);
	sv_visitParameterSignature(v, node->typeParams, node->params);
	sv_token(v, node->question);
}

static void sv_visitCombinator(SourceVisitor *v, Token *keyword, NodeList *nodes);

static void sv_visitIfStatementClause(SourceVisitor *v, Node *node, Node *clause)
{
	if (clause->kind == N_Block || clause->kind == N_IfStatement) {
		sv_space(v);
		sv_visit(v, clause);
	} else {
		cb_indent(v->builder, Indent_block);
		cb_startRule(v->builder, NULL);
		if (node->stmt2)
			cb_writeNewline(v->builder, false, false, false);
		else
			cb_split(v->builder, false, true);
		sv_visit(v, clause);
		cb_endRule(v->builder);
		cb_unindent(v->builder);
	}
}

static void sv_visitIfStatement(SourceVisitor *v, Node *node)
{
	sv_visitIfCondition(v, node->kw, node->lp, node->expr, node->rp);
	sv_visitIfStatementClause(v, node, node->stmt);
	if (node->stmt2) {
		if (node->stmt->kind == N_Block)
			sv_space(v);
		else
			sv_newline(v);
		sv_token(v, node->kw2);
		sv_visitIfStatementClause(v, node, node->stmt2);
	}
}

static void sv_visitImportDirective(SourceVisitor *v, Node *node)
{
	sv_visitDirectiveMetadata(v, node);
	sv_simpleStatement_begin(v);
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->expr);
	if (node->op) {
		sv_soloSplit(v, Cost_normal);
		sv_token_ba(v, node->kw2, NULL, sv_space);
		sv_token(v, node->op);
		sv_space(v);
		sv_visit(v, node->ident);
	}
	sv_visitCombinators(v, &node->list);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_visitIndexExpression(SourceVisitor *v, Node *node)
{
	cb_nestExpression(v->builder, -1, false);
	if (node_isCascaded(node))
		sv_token(v, node->op);
	else
		sv_visit(v, node->target);
	sv_finishIndexExpression(v, node);
	cb_unnest(v->builder, true);
}

static void sv_finishIndexExpression(SourceVisitor *v, Node *node)
{
	if (node->target && node->target->kind == N_IndexExpression)
		sv_soloZeroSplit(v);
	cb_startSpan(v->builder, Cost_index);
	sv_token(v, node->question);
	sv_token(v, node->lb);
	sv_soloZeroSplit(v);
	sv_visit(v, node->expr);
	sv_token(v, node->rb);
	cb_endSpan(v->builder);
}

static void sv_visitInstanceCreationExpression(SourceVisitor *v, Node *node)
{
	cb_startSpan(v->builder, Cost_normal);
	sv_token_ba(v, node->kw, NULL, sv_space);
	cb_startSpan(v->builder, Cost_constructorName);
	cb_nestExpression(v->builder, -1, false);
	sv_visit(v, node->ctorName);
	cb_endSpan(v->builder);
	sv_visitArgumentList(v, node->args, false);
	cb_endSpan(v->builder);
	cb_unnest(v->builder, true);
}

static void sv_visitInterpolationExpression(SourceVisitor *v, Node *node)
{
	cb_preventSplit(v->builder);
	sv_token(v, node->lb);
	cb_startSpan(v->builder, Cost_normal);
	sv_visit(v, node->expr);
	cb_endSpan(v->builder);
	sv_token(v, node->rb);
	cb_endPreventSplit(v->builder);
}

static void sv_writeStringLiteral(SourceVisitor *v, Token *string);

static void sv_visitListLiteral(SourceVisitor *v, Node *node)
{
	int cost = node->list.n <= 1 ? Cost_singleElementList : Cost_normal;
	sv_visitCollectionLiteral(v, node->lb, &node->list, node->rb, node->kw, node->typeArgs, cost, true, false);
}

static void sv_visitMapLiteralEntry(SourceVisitor *v, Node *node)
{
	cb_nestExpression(v->builder, -1, false);
	sv_visit(v, node->expr);
	sv_token(v, node->op);
	sv_soloSplit(v, Cost_normal);
	sv_visit(v, node->expr2);
	cb_unnest(v->builder, true);
}

static void sv_visitMethodDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitFunctionOrMethodDeclaration(v, &node->meta, node->m_external, node->m_property, node->m_static,
					    node->m_operator, node->name, node->type, node->typeParams, node->params,
					    node->body);
}

static void sv_visitMethodInvocation(SourceVisitor *v, Node *node)
{
	if (!node->target || node_looksLikeStaticCall(node)) {
		cb_nestExpression(v->builder, -1, false);
		cb_startSpan(v->builder, Cost_normal);
		if (node->target) {
			cb_startSpan(v->builder, Cost_constructorName);
			sv_visit(v, node->target);
			sv_soloZeroSplit(v);
		}
		sv_token(v, node->op);
		sv_visit(v, node->ident);
		if (node->target)
			cb_endSpan(v->builder);
		cb_nestExpression(v->builder, -1, false);
		sv_visit_ba(v, node->typeArgs, NULL, NULL);
		sv_visitArgumentList(v, node->args, false);
		cb_unnest(v->builder, true);
		cb_endSpan(v->builder);
		cb_unnest(v->builder, true);
		return;
	}
	CallChainVisitor ccv;
	ccv_init(&ccv, v, node);
	ccv_visit(&ccv);
}

static void sv_visitNameWithTypeParameters(SourceVisitor *v, Node *node)
{
	sv_token(v, node->name);
	sv_visit_ba(v, node->typeParams, NULL, NULL);
}

static void sv_visitNamedType(SourceVisitor *v, Node *node)
{
	if (node->prefixName) {
		cb_startSpan(v->builder, Cost_normal);
		sv_token(v, node->prefixName);
		sv_soloZeroSplit(v);
		sv_token(v, node->prefixPeriod);
		sv_token(v, node->name);
		cb_endSpan(v->builder);
	} else {
		sv_token(v, node->name);
	}
	sv_visit_ba(v, node->typeArgs, NULL, NULL);
	sv_token(v, node->question);
}

static void sv_visitParenthesizedExpression(SourceVisitor *v, Node *node)
{
	cb_nestExpression(v->builder, -1, false);
	sv_token(v, node->lp);
	sv_visit(v, node->expr);
	cb_unnest(v->builder, true);
	sv_token(v, node->rp);
}

static void sv_visitPrefixExpression(SourceVisitor *v, Node *node)
{
	sv_token(v, node->op);
	Node *operand = node->expr;
	if (operand->kind == N_PrefixExpression &&
	    (strcmp(operand->op->lexeme, "-") == 0 || strcmp(operand->op->lexeme, "--") == 0))
		sv_space(v);
	sv_visit(v, node->expr);
}

static void sv_visitPropertyAccess(SourceVisitor *v, Node *node)
{
	if (node_isCascaded(node)) {
		sv_token(v, node->op);
		sv_visit(v, node->ident);
		return;
	}
	CallChainVisitor ccv;
	ccv_init(&ccv, v, node);
	ccv_visit(&ccv);
}

static void sv_visitReturnStatement(SourceVisitor *v, Node *node)
{
	sv_simpleStatement_begin(v);
	sv_token(v, node->kw);
	sv_visit_ba(v, node->expr, sv_space, NULL);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_visitSetOrMapLiteral(SourceVisitor *v, Node *node)
{
	sv_visitCollectionLiteral(v, node->lb, &node->list, node->rb, node->kw, node->typeArgs, Cost_normal, true, false);
}

static void sv_visitRegularFormalParameter(SourceVisitor *v, Node *node)
{
	sv_parameterMetadata_begin(v, &node->meta);
	if (node->suffix) {
		sv_modifier(v, node->m_required);
		sv_modifier(v, node->m_covariant);
		sv_visit_ba(v, node->type, NULL, sv_space);
		cb_startSpan(v->builder, Cost_normal);
		sv_token(v, node->name);
		sv_visitParameterSignature(v, node->suffix->typeParams, node->suffix->params);
		sv_token(v, node->suffix->question);
		cb_endSpan(v->builder);
		sv_visit_ba(v, node->defaultClause, NULL, NULL);
	} else {
		sv_beginFormalParameter(v, node);
		sv_modifier(v, node->kw);
		sv_visit_ba(v, node->type, NULL, NULL);
		if (node->name)
			sv_separatorBetweenTypeAndVariable(v, node->type, false);
		sv_token(v, node->name);
		sv_endFormalParameter(v);
		sv_visit_ba(v, node->defaultClause, NULL, NULL);
	}
	sv_parameterMetadata_end(v, &node->meta);
}

static void sv_visitSuperFormalParameter(SourceVisitor *v, Node *node) { sv_visitFieldOrSuperFormalParameter(v, node); }

static void sv_visitSwitchValue(SourceVisitor *v, Token *switchKeyword, Token *leftParenthesis, Node *value,
				Token *rightParenthesis)
{
	cb_nestExpression(v->builder, -1, false);
	sv_token(v, switchKeyword);
	sv_space(v);
	sv_token(v, leftParenthesis);
	sv_soloZeroSplit(v);
	sv_visit(v, value);
	sv_token(v, rightParenthesis);
	sv_space(v);
	cb_unnest(v->builder, true);
}

static void sv_visitSwitchStatement(SourceVisitor *v, Node *node)
{
	sv_visitSwitchValue(v, node->kw, node->lp, node->expr, node->rp);
	sv_beginBody(v, node->lb, false);
	for (int i = 0; i < node->list.n; i++) {
		Node *member = node->list.v[i];
		sv_visitNodes(v, &member->meta, NULL, sv_newline, sv_newline);
		sv_token(v, member->kw);
		if (member->kind == N_SwitchPatternCase) {
			sv_space(v);
			cb_indent(v->builder, Indent_block);
			sv_visit(v, member->expr);
			cb_unindent(v->builder);
		}
		sv_token(v, member->colon);
		if (member->list.n > 0) {
			cb_indent(v->builder, Indent_block);
			sv_newline(v);
			sv_visitNodes(v, &member->list, NULL, sv_oneOrTwoNewlines, NULL);
			cb_unindent(v->builder);
			sv_oneOrTwoNewlines(v);
		} else {
			cb_writeNewline(v->builder, false, false, false);
		}
	}
	if (node->list.n > 0)
		sv_newline(v);
	sv_endBody(v, node->rb, node->list.n > 0);
}

static void sv_visitConstantPattern(SourceVisitor *v, Node *node)
{
	sv_token_ba(v, node->kw, NULL, sv_space);
	sv_visit(v, node->expr);
}

static void sv_visitThrowExpression(SourceVisitor *v, Node *node)
{
	sv_token(v, node->kw);
	sv_space(v);
	sv_visit(v, node->expr);
}

static void sv_visitTopLevelVariableDeclaration(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	sv_simpleStatement_begin(v);
	sv_modifier(v, node->m_external);
	sv_visit(v, node->vars);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_visitGenericList(SourceVisitor *v, Token *leftBracket, Token *rightBracket, NodeList *nodes)
{
	Rule *rule = type_argument_rule_new();
	cb_startLazyRule(v->builder, rule);
	cb_startSpan(v->builder, Cost_normal);
	cb_nestExpression(v->builder, -1, false);
	sv_token(v, leftBracket);
	rule_beforeArgument(rule, sv_zeroSplit(v));
	cb_startBlockArgumentNesting(v->builder);
	for (int i = 0; i < nodes->n; i++) {
		Node *node = nodes->v[i];
		sv_visit(v, node);
		if (i != nodes->n - 1) {
			Token *comma = node->end->next;
			if (comma && strcmp(comma->lexeme, "?") == 0)
				comma = comma->next;
			sv_token(v, comma);
			rule_beforeArgument(rule, sv_split(v));
		}
	}
	sv_token(v, rightBracket);
	cb_endBlockArgumentNesting(v->builder);
	cb_unnest(v->builder, true);
	cb_endSpan(v->builder);
	cb_endRule(v->builder);
}

static void sv_visitVariableDeclaration(SourceVisitor *v, Node *node)
{
	sv_token(v, node->name);
	if (!node->expr)
		return;
	bool hasMultipleVariables = node->parent->list.n > 1;
	sv_visitAssignment(v, node->op, node->expr, hasMultipleVariables);
}

static void sv_visitVariableDeclarationList(SourceVisitor *v, Node *node)
{
	sv_visitMetadata(v, &node->meta);
	cb_startSpan(v->builder, Cost_normal);
	sv_modifier(v, node->m_late);
	sv_modifier(v, node->kw);
	sv_visit_ba(v, node->type, NULL, NULL);
	sv_separatorBetweenTypeAndVariable(v, node->type, true);
	cb_endSpan(v->builder);
	cb_startRule(v->builder, NULL);
	if (node->list.n > 1)
		cb_startBlockArgumentNesting(v->builder);
	sv_visitCommaSeparatedNodes(v, &node->list, sv_splitFn);
	if (node->list.n > 1)
		cb_endBlockArgumentNesting(v->builder);
	cb_endRule(v->builder);
}

static void sv_visitVariableDeclarationStatement(SourceVisitor *v, Node *node)
{
	sv_simpleStatement_begin(v);
	sv_visit(v, node->vars);
	sv_simpleStatement_end(v, node->semi);
}

static void sv_visitYieldStatement(SourceVisitor *v, Node *node)
{
	sv_simpleStatement_begin(v);
	sv_token(v, node->kw);
	sv_token(v, node->star);
	sv_space(v);
	sv_visit(v, node->expr);
	sv_simpleStatement_end(v, node->semi);
}

/* ---- helpers ---- */

static void sv_visitNamedNode(SourceVisitor *v, Token *name, Token *colon, Node *node, Rule *rule)
{
	cb_nestExpression(v->builder, -1, false);
	cb_startSpan(v->builder, Cost_normal);
	sv_token(v, name);
	sv_token(v, colon);
	if (node->kind == N_ListLiteral || node->kind == N_SetOrMapLiteral) {
		sv_space(v);
	} else {
		Rule *split = sv_soloSplit(v, Cost_normal);
		if (rule)
			rule_constrainWhenSplit(split, rule);
	}
	sv_visit(v, node);
	cb_endSpan(v->builder);
	cb_unnest(v->builder, true);
}

static void sv_visitAssignment(SourceVisitor *v, Token *equalsOperator, Node *rightHandSide, bool nest)
{
	sv_space(v);
	sv_token(v, equalsOperator);
	if (nest)
		cb_nestExpression(v->builder, -1, true);
	sv_soloSplit(v, sv_assignmentCost(rightHandSide));
	cb_startSpan(v->builder, Cost_normal);
	sv_visit(v, rightHandSide);
	cb_endSpan(v->builder);
	if (nest)
		cb_unnest(v->builder, true);
}

static void sv_binaryTraverse(SourceVisitor *v, Node *e, int precedence)
{
	if (e->kind != N_BinaryExpression) {
		sv_visit(v, e);
		return;
	}
	if (precedence >= 0 && binary_precedence(e->op->type) != precedence) {
		sv_visit(v, e);
		return;
	}
	sv_binaryTraverse(v, e->expr, precedence);
	sv_space(v);
	sv_token(v, e->op);
	sv_split(v);
	sv_binaryTraverse(v, e->expr2, precedence);
}

static void sv_visitBinary(SourceVisitor *v, Node *node, int precedence, bool nest)
{
	cb_startSpan(v->builder, Cost_normal);
	if (nest)
		cb_nestExpression(v->builder, -1, false);
	cb_startLazyRule(v->builder, NULL);
	cb_startBlockArgumentNesting(v->builder);
	sv_binaryTraverse(v, node, precedence);
	cb_endBlockArgumentNesting(v->builder);
	if (nest)
		cb_unnest(v->builder, true);
	cb_endSpan(v->builder);
	cb_endRule(v->builder);
}

static void sv_visitBodyContents(SourceVisitor *v, NodeList *nodes)
{
	for (int i = 0; i < nodes->n; i++) {
		Node *node = nodes->v[i];
		sv_visit(v, node);
		if (i != nodes->n - 1) {
			if (node_hasNonEmptyBody(node))
				sv_twoNewlines(v);
			else
				sv_oneOrTwoNewlines(v);
		}
	}
}

static void sv_visitFunctionOrMethodDeclaration(SourceVisitor *v, NodeList *metadata, Token *externalKeyword,
						Token *propertyKeyword, Token *modifierKeyword, Token *operatorKeyword,
						Token *name, Node *returnType, Node *typeParameters,
						Node *formalParameters, Node *body)
{
	sv_visitMetadata(v, metadata);
	cb_nestExpression(v->builder, -1, false);
	cb_startSpan(v->builder, Cost_normal);
	sv_modifier(v, externalKeyword);
	sv_modifier(v, modifierKeyword);
	sv_visit_ba(v, returnType, NULL, sv_soloSplitFn);
	sv_modifier(v, propertyKeyword);
	sv_modifier(v, operatorKeyword);
	sv_token(v, name);
	cb_endSpan(v->builder);
	sv_visitFunctionBody(v, typeParameters, formalParameters, body, NULL, body);
	if (body->kind == N_ExpressionFunctionBody)
		cb_unnest(v->builder, true);
}

/* beforeBody of _visitFunctionOrMethodDeclaration */
static void sv_methodBeforeBody(SourceVisitor *v, Node *body)
{
	if (body->kind != N_ExpressionFunctionBody)
		cb_unnest(v->builder, true);
}

static void sv_visitFunctionBody(SourceVisitor *v, Node *typeParameters, Node *parameters, Node *body,
				 void (*beforeBody)(SourceVisitor *, Node *), Node *ctx)
{
	if (body->kind == N_ExpressionFunctionBody) {
		cb_nestExpression(v->builder, -1, false);
		cb_startLazyRule(v->builder, rule_new_cost(Cost_arrow));
	}
	sv_visitParameterSignature(v, typeParameters, parameters);
	if (beforeBody)
		beforeBody(v, ctx);
	else if (ctx == body)
		sv_methodBeforeBody(v, body);
	sv_visit(v, body);
	if (body->kind == N_ExpressionFunctionBody)
		cb_unnest(v->builder, true);
}

static void sv_visitParameterSignature(SourceVisitor *v, Node *typeParameters, Node *parameters)
{
	cb_nestExpression(v->builder, -1, false);
	sv_visit_ba(v, typeParameters, NULL, NULL);
	if (parameters)
		sv_visitFormalParameterList(v, parameters, false);
	cb_unnest(v->builder, true);
}

static int sv_linesBeforeNextToken(SourceVisitor *v);

static void sv_visitCollectionLiteral(SourceVisitor *v, Token *leftBracket, NodeList *elements, Token *rightBracket,
				      Token *constKeyword, Node *typeArguments, int cost, bool splitOuterCollection,
				      bool isRecord)
{
	(void)cost;
	sv_modifier(v, constKeyword);
	if (typeArguments) {
		cb_startSpan(v->builder, Cost_normal);
		cb_nestExpression(v->builder, -1, false);
		sv_token(v, typeArguments->lb);
		cb_startRule(v->builder, rule_new_cost(Cost_typeArgument));
		for (int i = 0; i < typeArguments->list.n; i++) {
			Node *typeArgument = typeArguments->list.v[i];
			sv_visit(v, typeArgument);
			if (i != typeArguments->list.n - 1) {
				Token *comma = typeArgument->end->next;
				if (comma && strcmp(comma->lexeme, "?") == 0)
					comma = comma->next;
				sv_token(v, comma);
				sv_split(v);
			}
		}
		sv_token(v, typeArguments->rb);
		cb_endRule(v->builder);
		cb_unnest(v->builder, true);
		cb_endSpan(v->builder);
	}
	if (elements->n == 0) {
		sv_visitBody(v, leftBracket, elements, rightBracket);
		return;
	}
	if (splitOuterCollection) {
		for (int i = 0; i < v->ncollectionSplits; i++)
			v->collectionSplits[i] = 1;
		if (v->ncollectionSplits == v->capcollectionSplits) {
			int cap = v->capcollectionSplits ? v->capcollectionSplits * 2 : 8;
			v->collectionSplits = xrealloc(v->collectionSplits, (size_t)v->capcollectionSplits * sizeof(int),
						       (size_t)cap * sizeof(int));
			v->capcollectionSplits = cap;
		}
		v->collectionSplits[v->ncollectionSplits++] = 0;
	}
	sv_beginBody(v, leftBracket, false);
	if (list_containsLineComments(elements, rightBracket)) {
		Rule *lineRule = type_argument_rule_new();
		cb_startLazyRule(v->builder, lineRule);
		for (int i = 0; i < elements->n; i++) {
			Node *element = elements->v[i];
			if (sv_endLine(v, element->begin->previous) != sv_startLine(v, element->begin)) {
				sv_oneOrTwoNewlines(v);
				cb_endRule(v->builder);
				lineRule = type_argument_rule_new();
				cb_startLazyRule(v->builder, lineRule);
			} else {
				rule_beforeArgument(lineRule, sv_split(v));
			}
			sv_visit(v, element);
			sv_writeCommaAfter(v, element);
		}
		cb_endRule(v->builder);
	} else {
		for (int i = 0; i < elements->n; i++) {
			Node *element = elements->v[i];
			cb_split(v->builder, false, i != 0);
			sv_visit(v, element);
			sv_writeCommaAfter(v, element);
		}
	}
	bool force = false;
	if (splitOuterCollection)
		force = v->collectionSplits[--v->ncollectionSplits];
	bool isSingleElementRecord = isRecord && elements->n == 1;
	if (list_hasCommaAfter(elements) && !isSingleElementRecord)
		force = true;
	sv_endBody(v, rightBracket, force);
}

static void sv_visitTrailingCommaParameterList(SourceVisitor *v, Node *parameters)
{
	cb_startRule(v->builder, rule_hard());
	sv_token(v, parameters->lp);
	Node *lastRequired = NULL;
	for (int i = 0; i < parameters->list.n; i++) {
		if (parameters->list.v[i]->flags & (NF_NAMED | NF_OPTIONAL_POSITIONAL)) {
			if (i > 0)
				lastRequired = parameters->list.v[i - 1];
			break;
		}
	}
	if (parameters->list.v[0]->flags & (NF_NAMED | NF_OPTIONAL_POSITIONAL))
		sv_token(v, parameters->lb);
	v->builder = cb_startBlock(v->builder, NULL, true, false);
	for (int i = 0; i < parameters->list.n; i++) {
		Node *parameter = parameters->list.v[i];
		cb_writeNewline(v->builder, false, false, false);
		sv_visit(v, parameter);
		sv_writeCommaAfter(v, parameter);
		if (parameter == lastRequired) {
			sv_space(v);
			sv_token(v, parameters->lb);
			lastRequired = NULL;
		}
	}
	Token *firstDelimiter = parameters->rb ? parameters->rb : parameters->rp;
	if (firstDelimiter->precedingComments) {
		cb_writeNewline(v->builder, false, false, false);
		sv_writePrecedingCommentsAndNewlines(v, firstDelimiter);
	}
	v->builder = cb_endBlock(v->builder, true);
	cb_endRule(v->builder);
	sv_writeText(v, firstDelimiter->lexeme, (size_t)firstDelimiter->len, firstDelimiter, true);
	if (firstDelimiter != parameters->rp)
		sv_token(v, parameters->rp);
}

static bool sv_splitEmptyBlock(Node *node)
{
	Node *parent = node->parent;
	if (parent && parent->kind == N_IfStatement)
		return parent->stmt2 != NULL && parent->stmt == node;
	if (parent && parent->kind == N_CatchClause && parent->parent && parent->parent->kind == N_TryStatement) {
		Node *tryStatement = parent->parent;
		return tryStatement->body2 != NULL || node != tryStatement->list.v[tryStatement->list.n - 1]->body;
	}
	return false;
}

static Rule *sv_lookupBlockRule(SourceVisitor *v, Token *t)
{
	int idx;
	return pm_get(&v->blockRules, t, &idx) ? v->blockRuleVals.v[idx] : NULL;
}

static Chunk *sv_lookupBlockChunk(SourceVisitor *v, Token *t)
{
	int idx;
	return pm_get(&v->blockPreviousChunks, t, &idx) ? v->blockChunkVals.v[idx] : NULL;
}

static void sv_beginBody(SourceVisitor *v, Token *leftBracket, bool space)
{
	sv_token(v, leftBracket);
	cb_startRule(v->builder, sv_lookupBlockRule(v, leftBracket));
	v->builder = cb_startBlock(v->builder, sv_lookupBlockChunk(v, leftBracket), true, space);
}

static void sv_endBody(SourceVisitor *v, Token *rightBracket, bool forceSplit)
{
	bool hasLeadingNewline = sv_writePrecedingCommentsAndNewlines(v, rightBracket);
	v->builder = cb_endBlock(v->builder, hasLeadingNewline || forceSplit);
	cb_endRule(v->builder);
	sv_writeText(v, rightBracket->lexeme, (size_t)rightBracket->len, rightBracket, true);
}

static void sv_visitCombinator(SourceVisitor *v, Token *keyword, NodeList *nodes)
{
	Rule *rule = cb_rule(v->builder);
	combinator_addCombinator(rule, sv_split(v));
	cb_nestExpression(v->builder, -1, false);
	sv_token(v, keyword);
	combinator_addName(rule, sv_split(v));
	/* visitCommaSeparatedNodes(nodes, between: () => rule.addName(split())) */
	for (int i = 0; i < nodes->n; i++) {
		if (i > 0)
			combinator_addName(rule, sv_split(v));
		sv_visit(v, nodes->v[i]);
		Token *next = nodes->v[i]->end->next;
		if (strcmp(next->lexeme, ",") == 0)
			sv_token(v, next);
	}
	cb_unnest(v->builder, true);
}

static void sv_beforeBlock(SourceVisitor *v, Token *token, Rule *rule, Chunk *previousChunk)
{
	int idx;
	if (pm_get(&v->blockRules, token, &idx)) {
		v->blockRuleVals.v[idx] = rule;
	} else {
		pm_put(&v->blockRules, token, v->blockRuleVals.n);
		pv_push(&v->blockRuleVals, rule);
	}
	if (previousChunk) {
		if (pm_get(&v->blockPreviousChunks, token, &idx)) {
			v->blockChunkVals.v[idx] = previousChunk;
		} else {
			pm_put(&v->blockPreviousChunks, token, v->blockChunkVals.n);
			pv_push(&v->blockChunkVals, previousChunk);
		}
	}
}

static void sv_visitBody(SourceVisitor *v, Token *leftBracket, NodeList *nodes, Token *rightBracket)
{
	if (!list_canSplit(nodes, rightBracket)) {
		sv_token(v, leftBracket);
		sv_token(v, rightBracket);
		return;
	}
	sv_beginBody(v, leftBracket, false);
	sv_visitBodyContents(v, nodes);
	sv_endBody(v, rightBracket, nodes->n > 0);
}

static void sv_writeStringFirstLine(SourceVisitor *v, const char *line, size_t n, Token *string)
{
	size_t quoteStart = n > 0 && line[0] == 'r' ? 1 : 0;
	size_t quoteEnd = quoteStart + 3;
	int backslashCount = 0;
	if (n > quoteEnd && (strncmp(line + quoteStart, "'''", 3) == 0 || strncmp(line + quoteStart, "\"\"\"", 3) == 0)) {
		size_t cursor = quoteEnd;
		do {
			char ch = line[cursor];
			if (ch == '\\') {
				cursor += 1;
				backslashCount++;
				if (cursor >= n)
					break;
				ch = line[cursor];
			}
			if (ch != ' ' && ch != '\t')
				break;
			cursor++;
		} while (cursor < n);
		if (cursor == n) {
			Buf firstLineText = {0};
			buf_add(&firstLineText, line, quoteEnd);
			if (backslashCount > 0) {
				buf_addc(&firstLineText, '\\');
				while (--backslashCount > 0)
					buf_adds(&firstLineText, " \\");
			}
			sv_writeText(v, buf_str(&firstLineText), firstLineText.n, string, true);
			return;
		}
	}
	sv_writeText(v, line, n, string, true);
}

static void sv_writeStringLiteral(SourceVisitor *v, Token *string)
{
	sv_writePrecedingCommentsAndNewlines(v, string);
	const char *s = string->lexeme;
	size_t n = (size_t)string->len;
	/* split at \r\n, \r or \n */
	size_t lineStart = 0;
	bool first = true;
	bool multi = false;
	for (size_t i = 0; i < n; i++)
		if (s[i] == '\n' || s[i] == '\r')
			multi = true;
	for (size_t i = 0; i <= n; i++) {
		if (i < n && s[i] != '\n' && s[i] != '\r')
			continue;
		const char *line = s + lineStart;
		size_t len = i - lineStart;
		if (first) {
			if (multi)
				sv_writeStringFirstLine(v, line, len, string);
			else
				sv_writeText(v, line, len, string, true);
			first = false;
		} else {
			cb_writeNewline(v->builder, false, true, true);
			sv_writeText(v, line, len, string, false);
		}
		if (i < n && s[i] == '\r' && i + 1 < n && s[i + 1] == '\n')
			i++;
		lineStart = i + 1;
	}
}

static void sv_modifier(SourceVisitor *v, Token *modifier) { sv_token_ba(v, modifier, NULL, sv_space); }

static void sv_space(SourceVisitor *v) { cb_writeSpace(v->builder); }
static void sv_newline(SourceVisitor *v) { cb_writeNewline(v->builder, false, false, false); }
static void sv_twoNewlines(SourceVisitor *v) { cb_writeNewline(v->builder, true, false, false); }

static void sv_splitOrTwoNewlines(SourceVisitor *v)
{
	if (sv_linesBeforeNextToken(v) > 1)
		sv_twoNewlines(v);
	else
		sv_split(v);
}

static void sv_oneOrTwoNewlines(SourceVisitor *v) { cb_writeNewline(v->builder, sv_linesBeforeNextToken(v) > 1, false, false); }

static int sv_linesBeforeNextToken(SourceVisitor *v)
{
	Token *previous = v->lastToken;
	Token *next = previous->next;
	if (next->precedingComments)
		next = next->precedingComments;
	return sv_startLine(v, next) - sv_endLine(v, previous);
}

static Chunk *sv_split(SourceVisitor *v) { return cb_split(v->builder, true, true); }
static Chunk *sv_zeroSplit(SourceVisitor *v) { return cb_split(v->builder, true, false); }

static Rule *sv_soloSplit(SourceVisitor *v, int cost)
{
	Rule *rule = rule_new_cost(cost);
	cb_startRule(v->builder, rule);
	sv_split(v);
	cb_endRule(v->builder);
	return rule;
}

static void sv_soloZeroSplit(SourceVisitor *v)
{
	cb_startRule(v->builder, NULL);
	cb_split(v->builder, true, false);
	cb_endRule(v->builder);
}

static void sv_token_ba(SourceVisitor *v, Token *token, SvFn before, SvFn after)
{
	if (!token)
		return;
	sv_writePrecedingCommentsAndNewlines(v, token);
	if (before)
		before(v);
	sv_writeText(v, token->lexeme, (size_t)token->len, token, true);
	if (after)
		after(v);
}

static void sv_token(SourceVisitor *v, Token *token) { sv_token_ba(v, token, NULL, NULL); }

/* Dart's String.trim() whitespace. */
static size_t dart_ws_len(const char *p, size_t n, bool atEnd)
{
	/* returns the byte length of one whitespace code point at the start (or end) of p[0:n], or 0 */
	uint32_t r;
	int w;
	if (!atEnd) {
		w = utf8_decode(p, n, &r);
	} else {
		size_t i = n;
		do
			i--;
		while (i > 0 && ((unsigned char)p[i] & 0xC0) == 0x80);
		w = utf8_decode(p + i, n - i, &r);
		if ((size_t)w != n - i)
			return 0;
	}
	bool ws = (r >= 0x09 && r <= 0x0D) || r == 0x20 || r == 0x85 || r == 0xA0 || r == 0x1680 ||
		  (r >= 0x2000 && r <= 0x200A) || r == 0x2028 || r == 0x2029 || r == 0x202F || r == 0x205F ||
		  r == 0x3000 || r == 0xFEFF;
	return ws ? (size_t)w : 0;
}

static bool sv_writePrecedingCommentsAndNewlines(SourceVisitor *v, Token *token)
{
	Token *comment = token->precedingComments;
	if (!comment)
		return false;
	if (pm_has(&v->suppressPrecedingCommentsAndNewLines, token))
		return false;
	int previousLine = sv_endLine(v, token->previous);
	int tokenLine = sv_startLine(v, token);
	if (token->previous->type == TT_SCRIPT_TAG)
		previousLine = tokenLine;
	int ncomments = 0, cap = 4;
	SourceComment *comments = xalloc((size_t)cap * sizeof *comments);
	while (comment) {
		int commentLine = sv_startLine(v, comment);
		if (comment == token->precedingComments && token->previous->type == TT_EOF)
			previousLine = commentLine;
		const char *text = comment->lexeme;
		size_t n = (size_t)comment->len, w;
		while (n > 0 && (w = dart_ws_len(text, n, false)) > 0)
			text += w, n -= w;
		while (n > 0 && (w = dart_ws_len(text, n, true)) > 0)
			n -= w;
		text = xstrndup(text, n);
		int linesBefore = commentLine - previousLine;
		bool flushLeft = sv_startColumn(v, comment) == 1;
		if (has_prefix(text, "///") && !has_prefix(text, "////")) {
			flushLeft = false;
			if (comment == token->precedingComments)
				linesBefore = 2;
		}
		CommentType type = CommentType_block;
		if ((has_prefix(text, "///") && !has_prefix(text, "////")) || (has_prefix(text, "/**") && strcmp(text, "/**/") != 0))
			type = CommentType_doc;
		else if (comment->type == TT_SINGLE_LINE_COMMENT)
			type = CommentType_line;
		else if (commentLine == previousLine || commentLine == tokenLine)
			type = CommentType_inlineBlock;
		if (ncomments == cap) {
			comments = xrealloc(comments, (size_t)cap * sizeof *comments, (size_t)cap * 2 * sizeof *comments);
			cap *= 2;
		}
		comments[ncomments++] = (SourceComment){text, n, type, linesBefore, flushLeft};
		previousLine = sv_endLine(v, comment);
		comment = comment->next;
	}
	cb_writeComments(v->builder, comments, ncomments, tokenLine - previousLine, token->lexeme);
	return comments[0].linesBefore > 0;
}

static void sv_writeText(SourceVisitor *v, const char *text, size_t n, Token *token, bool mergeEmptySplits)
{
	cb_write(v->builder, text, n, mergeEmptySplits);
	v->lastToken = token;
}

/* ---- dispatch ---- */

static void sv_visit(SourceVisitor *v, Node *node)
{
	if (!node)
		return;
	switch (node->kind) {
	case N_SimpleIdentifier: sv_token(v, node->name); break;
	case N_PrefixedIdentifier: {
		CallChainVisitor ccv;
		ccv_init(&ccv, v, node);
		ccv_visit(&ccv);
		break;
	}
	case N_PropertyAccess: sv_visitPropertyAccess(v, node); break;
	case N_MethodInvocation: sv_visitMethodInvocation(v, node); break;
	case N_InstanceCreationExpression: sv_visitInstanceCreationExpression(v, node); break;
	case N_IndexExpression: sv_visitIndexExpression(v, node); break;
	case N_ArgumentList: sv_visitArgumentList(v, node, true); break;
	case N_NamedArgument: sv_visitNamedNode(v, node->name, node->colon, node->expr, NULL); break;
	case N_IntegerLiteral: case N_DoubleLiteral: case N_BooleanLiteral: case N_NullLiteral:
		sv_token(v, node->tok);
		break;
	case N_SimpleStringLiteral: sv_writeStringLiteral(v, node->tok); break;
	case N_InterpolationString: sv_writeStringLiteral(v, node->tok); break;
	case N_AdjacentStrings: sv_visitAdjacentStrings(v, node); break;
	case N_StringInterpolation:
		for (int i = 0; i < node->list.n; i++)
			sv_visit(v, node->list.v[i]);
		break;
	case N_InterpolationExpression: sv_visitInterpolationExpression(v, node); break;
	case N_ListLiteral: sv_visitListLiteral(v, node); break;
	case N_SetOrMapLiteral: sv_visitSetOrMapLiteral(v, node); break;
	case N_MapLiteralEntry: sv_visitMapLiteralEntry(v, node); break;
	case N_ParenthesizedExpression: sv_visitParenthesizedExpression(v, node); break;
	case N_BinaryExpression: sv_visitBinaryExpression(v, node); break;
	case N_PrefixExpression: sv_visitPrefixExpression(v, node); break;
	case N_PostfixExpression:
		sv_visit(v, node->expr);
		sv_token(v, node->op);
		break;
	case N_AssignmentExpression: sv_visitAssignmentExpression(v, node); break;
	case N_ConditionalExpression: sv_visitConditionalExpression(v, node); break;
	case N_AsExpression: sv_visitAsOrIs(v, node); break;
	case N_CascadeExpression: sv_visitCascadeExpression(v, node); break;
	case N_FunctionExpression: sv_visitFunctionExpression(v, node); break;
	case N_ThisExpression: case N_SuperExpression: sv_token(v, node->kw); break;
	case N_ThrowExpression: sv_visitThrowExpression(v, node); break;
	case N_AwaitExpression: sv_visitAwaitExpression(v, node); break;
	case N_NamedType: sv_visitNamedType(v, node); break;
	case N_GenericFunctionType: sv_visitGenericFunctionType(v, node); break;
	case N_TypeArgumentList: sv_visitGenericList(v, node->lb, node->rb, &node->list); break;
	case N_FormalParameterList: sv_visitFormalParameterList(v, node, true); break;
	case N_RegularFormalParameter: sv_visitRegularFormalParameter(v, node); break;
	case N_FieldFormalParameter: sv_visitFieldOrSuperFormalParameter(v, node); break;
	case N_SuperFormalParameter: sv_visitSuperFormalParameter(v, node); break;
	case N_FormalParameterDefaultClause: sv_visitFormalParameterDefaultClause(v, node); break;
	case N_Block: sv_visitBlock(v, node); break;
	case N_ExpressionStatement: sv_visitExpressionStatement(v, node); break;
	case N_VariableDeclarationStatement: sv_visitVariableDeclarationStatement(v, node); break;
	case N_VariableDeclarationList: sv_visitVariableDeclarationList(v, node); break;
	case N_VariableDeclaration: sv_visitVariableDeclaration(v, node); break;
	case N_ReturnStatement: sv_visitReturnStatement(v, node); break;
	case N_IfStatement: sv_visitIfStatement(v, node); break;
	case N_SwitchStatement: sv_visitSwitchStatement(v, node); break;
	case N_ConstantPattern: sv_visitConstantPattern(v, node); break;
	case N_YieldStatement: sv_visitYieldStatement(v, node); break;
	case N_EmptyFunctionBody: sv_token(v, node->semi); break;
	case N_CompilationUnit: sv_visitCompilationUnit(v, node); break;
	case N_ImportDirective: sv_visitImportDirective(v, node); break;
	case N_ExportDirective: sv_visitExportDirective(v, node); break;
	case N_ShowCombinator: sv_visitCombinator(v, node->kw, &node->list); break;
	case N_ClassDeclaration: sv_visitClassDeclaration(v, node); break;
	case N_NameWithTypeParameters: sv_visitNameWithTypeParameters(v, node); break;
	case N_ExtendsClause: sv_visitExtendsClause(v, node); break;
	case N_WithClause: sv_visitCombinator(v, node->kw, &node->list); break;
	case N_BlockClassBody: sv_visitBody(v, node->lb, &node->list, node->rb); break;
	case N_EnumDeclaration: sv_visitEnumDeclaration(v, node); break;
	case N_BlockEnumBody: sv_visitBlockEnumBody(v, node); break;
	case N_EnumConstantDeclaration: sv_visitEnumConstantDeclaration(v, node); break;
	case N_FieldDeclaration: sv_visitFieldDeclaration(v, node); break;
	case N_TopLevelVariableDeclaration: sv_visitTopLevelVariableDeclaration(v, node); break;
	case N_MethodDeclaration: sv_visitMethodDeclaration(v, node); break;
	case N_ConstructorDeclaration: sv_visitConstructorDeclaration(v, node); break;
	case N_ConstructorName: sv_visitConstructorName(v, node); break;
	case N_Annotation: sv_visitAnnotation(v, node); break;
	case N_BlockFunctionBody: sv_visitBlockFunctionBody(v, node); break;
	case N_ExpressionFunctionBody: sv_visitExpressionFunctionBody(v, node); break;
	default:
		fail("dart_style: unsupported AST node %d", (int)node->kind);
	}
}

/* SourceVisitor.run */
static FormatResult *sv_run(SourceVisitor *v, Node *node, const char *lineEnding)
{
	sv_visit(v, node);
	sv_writePrecedingCommentsAndNewlines(v, node->end->next);
	return cb_end(v->builder, lineEnding);
}
