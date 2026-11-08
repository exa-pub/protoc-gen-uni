/* Port of dart_style 3.1.13 lib/src/short/argument_list_visitor.dart (included by
 * source_visitor.c).
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

typedef struct {
	NodeList *allArguments;
	NodeList positional, named;
	PtrMap blocks;     /* Argument -> index into blockTokens */
	PtrVec blockTokens;
	int leadingBlocks, trailingBlocks;
	Rule *blockRule;
	Chunk *previousSplit;
} ArgumentSublist;

typedef struct {
	SourceVisitor *visitor;
	Token *leftParenthesis, *rightParenthesis;
	NodeList *allArguments;
	ArgumentSublist arguments;
	NodeList functions; /* n == 0: null */
	bool hasFunctions;
	ArgumentSublist argumentsAfterFunctions;
} ArgumentListVisitor;

static bool alv_isBlockFunction(Node *argument);

static bool alv_isValidWrappingTarget(Node *expression)
{
	if (!expression)
		return true;
	while (expression->kind == N_PropertyAccess)
		expression = expression->target;
	if (expression->kind == N_PrefixedIdentifier)
		return true;
	if (expression->kind == N_SimpleIdentifier)
		return true;
	return false;
}

static bool alv_isBlockFunction(Node *argument)
{
	Node *expression = argument_expression(argument);
	if (expression->kind == N_MethodInvocation) {
		if (!alv_isValidWrappingTarget(expression->target))
			return false;
		if (expression->args->list.n != 1)
			return false;
		return alv_isBlockFunction(expression->args->list.v[0]);
	}
	if (expression->kind == N_InstanceCreationExpression) {
		if (expression->args->list.n != 1)
			return false;
		return alv_isBlockFunction(expression->args->list.v[0]);
	}
	if (expression->kind == N_FunctionExpressionInvocation) {
		if (expression->args->list.n > 0)
			return false;
		expression = expression->target;
	}
	while (expression->kind == N_ParenthesizedExpression)
		expression = expression->expr;
	if (expression->kind != N_FunctionExpression)
		return false;
	if (expression->body->kind != N_BlockFunctionBody)
		return false;
	Node *body = expression->body;
	return body->body->list.n > 0 || body->body->rb->precedingComments != NULL;
}

static bool alv_isAllNamed(Node **arguments, int n)
{
	for (int i = 0; i < n; i++)
		if (arguments[i]->kind != N_NamedArgument)
			return false;
	return true;
}

static bool alv_isNamedArrow(Node *argument)
{
	if (argument->kind != N_NamedArgument)
		return false;
	Node *expression = argument->expr;
	return expression->kind == N_FunctionExpression && expression->body->kind == N_ExpressionFunctionBody;
}

/* Returns false if there is no contiguous function range. */
static bool alv_contiguousFunctions(NodeList *arguments, int *start, int *end)
{
	int functionsStart = -1;
	int functionsEnd = -1;
	for (int i = 0; i < arguments->n; i++) {
		if (alv_isBlockFunction(arguments->v[i])) {
			if (functionsStart < 0)
				functionsStart = i;
			if (functionsEnd != -1 && functionsEnd != i)
				return false;
			functionsEnd = i + 1;
		}
	}
	if (functionsStart < 0)
		return false;
	if (alv_isAllNamed(arguments->v, arguments->n) && (functionsStart > 0 || functionsEnd < arguments->n))
		return false;
	if (alv_isAllNamed(arguments->v + functionsStart, functionsEnd - functionsStart)) {
		for (int i = 0; i < functionsStart; i++)
			if (alv_isNamedArrow(arguments->v[i]))
				return false;
		for (int i = functionsEnd; i < arguments->n; i++)
			if (alv_isNamedArrow(arguments->v[i]))
				return false;
	}
	*start = functionsStart;
	*end = functionsEnd;
	return true;
}

/* ArgumentSublist._blockToken */
static Token *as_blockToken(Node *argument)
{
	Node *expression = argument_expression(argument);
	if (expression->kind == N_ListLiteral)
		return expression->lb;
	if (expression->kind == N_SetOrMapLiteral)
		return expression->lb;
	if ((expression->kind == N_SimpleStringLiteral || expression->kind == N_StringInterpolation) &&
	    (expression->flags & NF_MULTILINE))
		return expression->begin;
	return NULL;
}

static void as_init(ArgumentSublist *s, NodeList *allArguments, Node **arguments, int n)
{
	memset(s, 0, sizeof *s);
	s->allArguments = allArguments;
	/* _splitArgumentLists */
	bool inNamed = false, interleaved = false;
	for (int i = 0; i < n; i++) {
		if (arguments[i]->kind == N_NamedArgument)
			inNamed = true;
		else if (inNamed)
			interleaved = true;
	}
	inNamed = false;
	for (int i = 0; i < n; i++) {
		Node *argument = arguments[i];
		if (interleaved) {
			ds_push(&s->named, argument);
			continue;
		}
		if (argument->kind == N_NamedArgument)
			inNamed = true;
		if (inNamed)
			ds_push(&s->named, argument);
		else
			ds_push(&s->positional, argument);
	}
	int nblocks = 0;
	for (int i = 0; i < n; i++) {
		Token *bracket = as_blockToken(arguments[i]);
		if (bracket) {
			pm_put(&s->blocks, arguments[i], s->blockTokens.n);
			pv_push(&s->blockTokens, bracket);
			nblocks++;
		}
	}
	int leadingBlocks = 0;
	for (int i = 0; i < n; i++) {
		if (!pm_has(&s->blocks, arguments[i]))
			break;
		leadingBlocks++;
	}
	int trailingBlocks = 0;
	if (leadingBlocks != n) {
		for (int i = n - 1; i >= 0; i--) {
			if (!pm_has(&s->blocks, arguments[i]))
				break;
			trailingBlocks++;
		}
	}
	if (leadingBlocks != nblocks)
		leadingBlocks = 0;
	if (trailingBlocks != nblocks)
		trailingBlocks = 0;
	if (leadingBlocks == 0 && trailingBlocks == 0) {
		s->blocks = (PtrMap){0};
		s->blockTokens.n = 0;
	}
	s->leadingBlocks = leadingBlocks;
	s->trailingBlocks = trailingBlocks;
}

static Token *as_blockFor(ArgumentSublist *s, Node *argument)
{
	int idx;
	return pm_get(&s->blocks, argument, &idx) ? s->blockTokens.v[idx] : NULL;
}

static void as_visitArgument(ArgumentSublist *s, SourceVisitor *visitor, Rule *rule, Node *argument)
{
	Token *argumentBlock = as_blockFor(s, argument);
	bool many = s->allArguments->n > 1;
	if (argumentBlock) {
		rule_disableSplitOnInnerRules(rule);
		sv_beforeBlock(visitor, argumentBlock, s->blockRule, s->previousSplit);
	} else if (many) {
		cb_startBlockArgumentNesting(visitor->builder);
	} else if (argument->kind != N_NamedArgument) {
		rule_disableSplitOnInnerRules(rule);
	}
	if (argument->kind == N_NamedArgument)
		sv_visitNamedNode(visitor, argument->name, argument->colon, argument->expr, rule);
	else
		sv_visit(visitor, argument);
	if (argumentBlock)
		rule_enableSplitOnInnerRules(rule);
	else if (many)
		cb_endBlockArgumentNesting(visitor->builder);
	else if (argument->kind != N_NamedArgument)
		rule_enableSplitOnInnerRules(rule);
	if (node_hasCommaAfter(argument))
		sv_token(visitor, argument->end->next);
}

static void as_visitArguments(ArgumentSublist *s, SourceVisitor *visitor, NodeList *arguments, Rule *rule)
{
	cb_startRule(visitor->builder, rule);
	s->previousSplit = cb_split(visitor->builder, true, arguments->v[0] != s->allArguments->v[0]);
	rule_beforeArgument(rule, s->previousSplit);
	bool isPositional = arguments == &s->positional;
	if (isPositional)
		cb_startSpan(visitor->builder, Cost_positionalArguments);
	for (int i = 0; i < arguments->n; i++) {
		as_visitArgument(s, visitor, rule, arguments->v[i]);
		if (i != arguments->n - 1) {
			s->previousSplit = sv_split(visitor);
			rule_beforeArgument(rule, s->previousSplit);
		}
	}
	if (isPositional)
		cb_endSpan(visitor->builder);
	cb_endRule(visitor->builder);
}

static void as_visit(ArgumentSublist *s, SourceVisitor *visitor)
{
	if (s->blockTokens.n > 0)
		s->blockRule = rule_new_cost(Cost_splitBlocks);
	Rule *rule = NULL;
	if (s->positional.n > 0) {
		int leadingBlocks = s->leadingBlocks < s->positional.n ? s->leadingBlocks : s->positional.n;
		int trailingBlocks = s->trailingBlocks - s->named.n > 0 ? s->trailingBlocks - s->named.n : 0;
		rule = positional_rule_new(s->blockRule, s->positional.n, leadingBlocks, trailingBlocks);
		as_visitArguments(s, visitor, &s->positional, rule);
	}
	if (s->named.n > 0) {
		int leadingBlocks = s->leadingBlocks - s->positional.n > 0 ? s->leadingBlocks - s->positional.n : 0;
		int trailingBlocks = s->trailingBlocks < s->named.n ? s->trailingBlocks : s->named.n;
		Rule *namedRule = named_rule_new(s->blockRule, leadingBlocks, trailingBlocks);
		if (rule)
			positional_addNamedArgsConstraints(rule, namedRule);
		as_visitArguments(s, visitor, &s->named, namedRule);
	}
}

static void alv_init(ArgumentListVisitor *a, SourceVisitor *visitor, Token *leftParenthesis, Token *rightParenthesis,
		     NodeList *arguments)
{
	memset(a, 0, sizeof *a);
	a->visitor = visitor;
	a->leftParenthesis = leftParenthesis;
	a->rightParenthesis = rightParenthesis;
	a->allArguments = arguments;
	int start, end;
	if (!alv_contiguousFunctions(arguments, &start, &end)) {
		as_init(&a->arguments, arguments, arguments->v, arguments->n);
		return;
	}
	as_init(&a->arguments, arguments, arguments->v, start);
	a->hasFunctions = true;
	for (int i = start; i < end; i++)
		ds_push(&a->functions, arguments->v[i]);
	as_init(&a->argumentsAfterFunctions, arguments, arguments->v + end, arguments->n - end);
}

static bool alv_isSingle(ArgumentListVisitor *a)
{
	return a->allArguments->n == 1 && a->allArguments->v[0]->kind != N_NamedArgument;
}

static bool alv_hasBlockArguments(ArgumentListVisitor *a) { return a->arguments.blockTokens.n > 0 || a->hasFunctions; }

static void alv_visit(ArgumentListVisitor *a)
{
	SourceVisitor *v = a->visitor;
	bool single = alv_isSingle(a);
	if (single)
		cb_startSpan(v->builder, Cost_normal);
	cb_startSpan(v->builder, Cost_normal);
	sv_token(v, a->leftParenthesis);
	as_visit(&a->arguments, v);
	cb_endSpan(v->builder);
	if (a->hasFunctions) {
		if (a->functions.v[0] == a->allArguments->v[0])
			sv_soloZeroSplit(v);
		else
			sv_soloSplit(v, Cost_normal);
		for (int i = 0; i < a->functions.n; i++) {
			Node *argument = a->functions.v[i];
			if (i > 0)
				sv_space(v);
			sv_visit(v, argument);
			if (node_hasCommaAfter(argument))
				sv_token(v, argument->end->next);
		}
		cb_startSpan(v->builder, Cost_normal);
		as_visit(&a->argumentsAfterFunctions, v);
		cb_endSpan(v->builder);
	}
	sv_token(v, a->rightParenthesis);
	if (single)
		cb_endSpan(v->builder);
}
