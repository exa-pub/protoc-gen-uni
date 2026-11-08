/* Port of dart_style 3.1.13 lib/src/short/call_chain_visitor.dart (included by
 * source_visitor.c).
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

typedef enum { SEL_METHOD, SEL_PREFIXED, SEL_PROPERTY } SelectorKind;

typedef struct {
	SelectorKind kind;
	Node *node;
	NodeList postfixes;
} Selector;

typedef struct {
	SourceVisitor *visitor;
	Node *target;
	PtrVec properties; /* Selector* */
	PtrVec calls;
	PtrVec blockCalls; /* null if !hasBlockCalls */
	bool hasBlockCalls;
	Selector *hangingCall;
	bool ruleEnabled;
	bool spanEnded;
	Rule *propertyRule;
} CallChainVisitor;

static bool sel_isProperty(const Selector *s) { return s->kind != SEL_METHOD; }

static bool sel_isBlockCall(const Selector *s, SourceVisitor *visitor)
{
	if (s->kind != SEL_METHOD)
		return false;
	ArgumentListVisitor alv;
	alv_init(&alv, visitor, s->node->args->lp, s->node->args->rp, &s->node->args->list);
	return alv_hasBlockArguments(&alv);
}

static Node *ccv_unwrapTarget(Node *node, PtrVec *calls);

static Selector *sel_new(SelectorKind kind, Node *node)
{
	Selector *s = xalloc(sizeof *s);
	s->kind = kind;
	s->node = node;
	return s;
}

static Node *ccv_unwrapPostfix(Node *node, Node *target, PtrVec *calls)
{
	target = ccv_unwrapTarget(target, calls);
	if (calls->n == 0)
		return node;
	ds_push(&((Selector *)pv_last(calls))->postfixes, node);
	return target;
}

static Node *ccv_unwrapSelector(Node *target, Selector *selector, PtrVec *calls)
{
	target = ccv_unwrapTarget(target, calls);
	pv_push(calls, selector);
	return target;
}

static Node *ccv_unwrapTarget(Node *node, PtrVec *calls)
{
	if (node_looksLikeStaticCall(node))
		return node;
	if (node->kind == N_MethodInvocation && node->target)
		return ccv_unwrapSelector(node->target, sel_new(SEL_METHOD, node), calls);
	if (node->kind == N_PropertyAccess && node->target)
		return ccv_unwrapSelector(node->target, sel_new(SEL_PROPERTY, node), calls);
	if (node->kind == N_PrefixedIdentifier)
		return ccv_unwrapSelector(node->target, sel_new(SEL_PREFIXED, node), calls);
	if (node->kind == N_IndexExpression && node->target)
		return ccv_unwrapPostfix(node, node->target, calls);
	if (node->kind == N_FunctionExpressionInvocation)
		return ccv_unwrapPostfix(node, node->target, calls);
	if (node->kind == N_PostfixExpression && node->op->type == TT_BANG)
		return ccv_unwrapPostfix(node, node->expr, calls);
	return node;
}

static Node *ccv_unwrapNullAssertion(Node *expression)
{
	if (expression->kind == N_PostfixExpression && expression->op->type == TT_BANG)
		return expression->expr;
	return expression;
}

static void pv_remove(PtrVec *v, void *p)
{
	for (int i = 0; i < v->n; i++)
		if (v->v[i] == p) {
			memmove(v->v + i, v->v + i + 1, (size_t)(v->n - i - 1) * sizeof *v->v);
			v->n--;
			return;
		}
}

static void ccv_init(CallChainVisitor *c, SourceVisitor *visitor, Node *node)
{
	memset(c, 0, sizeof *c);
	c->visitor = visitor;
	PtrVec calls = {0};
	Node *target = ccv_unwrapTarget(node, &calls);
	PtrVec properties = {0};
	if (ccv_unwrapNullAssertion(target)->kind == N_SimpleIdentifier) {
		for (int i = 0; i < calls.n && sel_isProperty(calls.v[i]); i++)
			pv_push(&properties, calls.v[i]);
	}
	/* calls.removeRange(0, properties.length) */
	memmove(calls.v, calls.v + properties.n, (size_t)(calls.n - properties.n) * sizeof *calls.v);
	calls.n -= properties.n;

	PtrVec blockCalls = {0};
	bool hasBlockCalls = false;
	Selector *hangingCall = NULL;
	bool inBlockCalls = false;
	for (int i = 0; i < calls.n; i++) {
		Selector *call = calls.v[i];
		if (sel_isBlockCall(call, visitor)) {
			inBlockCalls = true;
			hasBlockCalls = true;
			pv_push(&blockCalls, call);
		} else if (inBlockCalls) {
			if (call == calls.v[calls.n - 1]) {
				hangingCall = call;
				break;
			}
			hasBlockCalls = false;
			blockCalls.n = 0;
			break;
		}
	}
	if (hasBlockCalls)
		for (int i = 0; i < blockCalls.n; i++)
			pv_remove(&calls, blockCalls.v[i]);
	if (hangingCall)
		pv_remove(&calls, hangingCall);
	c->target = target;
	c->properties = properties;
	c->calls = calls;
	c->blockCalls = blockCalls;
	c->hasBlockCalls = hasBlockCalls;
	c->hangingCall = hangingCall;
}

static bool ccv_forcesSplit(Node *expression)
{
	while (expression->kind == N_ParenthesizedExpression)
		expression = expression->expr;
	if (node_isCollectionLiteral(expression))
		return false;
	if (expression->kind == N_FunctionExpression) {
		if (expression->body->kind != N_BlockFunctionBody)
			return false;
		return expression->body->body->list.n == 0;
	}
	Node *argumentList = NULL;
	if (expression->kind == N_MethodInvocation || expression->kind == N_InstanceCreationExpression ||
	    expression->kind == N_FunctionExpressionInvocation)
		argumentList = expression->args;
	if (!argumentList)
		return true;
	if (argumentList->list.n == 0)
		return true;
	Node *argument = argumentList->list.v[argumentList->list.n - 1];
	if (node_hasCommaAfter(argument))
		return false;
	return ccv_forcesSplit(argument_expression(argument));
}

static void ccv_disableRule(CallChainVisitor *c)
{
	if (!c->ruleEnabled)
		return;
	cb_endRule(c->visitor->builder);
	c->ruleEnabled = false;
}

static void ccv_enableRule(CallChainVisitor *c, bool lazy)
{
	if (c->ruleEnabled)
		return;
	Rule *rule = rule_new();
	if (c->propertyRule)
		positional_addNamedArgsConstraints(c->propertyRule, rule);
	if (lazy)
		cb_startLazyRule(c->visitor->builder, rule);
	else
		cb_startRule(c->visitor->builder, rule);
	c->ruleEnabled = true;
}

static void ccv_endSpan(CallChainVisitor *c)
{
	if (c->spanEnded)
		return;
	cb_endSpan(c->visitor->builder);
	c->spanEnded = true;
}

static void ccv_beforeMethodArguments(CallChainVisitor *c, Selector *selector)
{
	if (!c->hasBlockCalls && c->calls.n > 0 && selector == pv_last(&c->calls))
		ccv_disableRule(c);
	if (c->properties.n == 0 && c->calls.n == 1 && !c->hasBlockCalls && c->target->kind == N_SimpleIdentifier)
		ccv_endSpan(c);
}

static void sel_writeSelector(Selector *s, CallChainVisitor *c)
{
	SourceVisitor *v = c->visitor;
	Node *node = s->node;
	switch (s->kind) {
	case SEL_METHOD:
		sv_token(v, node->op);
		sv_token(v, node->ident->name);
		ccv_beforeMethodArguments(c, s);
		cb_nestExpression(v->builder, -1, false);
		sv_visit_ba(v, node->typeArgs, NULL, NULL);
		sv_visitArgumentList(v, node->args, false);
		cb_unnest(v->builder, true);
		break;
	case SEL_PREFIXED:
		sv_token(v, node->period);
		sv_visit(v, node->ident);
		break;
	case SEL_PROPERTY:
		sv_token(v, node->op);
		sv_visit(v, node->ident);
		break;
	}
}

static void sel_write(Selector *s, CallChainVisitor *c)
{
	SourceVisitor *v = c->visitor;
	sel_writeSelector(s, c);
	cb_nestExpression(v->builder, -1, false);
	for (int i = 0; i < s->postfixes.n; i++) {
		Node *postfix = s->postfixes.v[i];
		if (postfix->kind == N_FunctionExpressionInvocation) {
			sv_soloZeroSplit(v);
			sv_visit_ba(v, postfix->typeArgs, NULL, NULL);
			sv_visitArgumentList(v, postfix->args, true);
		} else if (postfix->kind == N_IndexExpression) {
			sv_finishIndexExpression(v, postfix);
		} else if (postfix->kind == N_PostfixExpression) {
			sv_token(v, postfix->op);
		}
	}
	cb_unnest(v->builder, true);
}

static void ccv_visit(CallChainVisitor *c)
{
	SourceVisitor *v = c->visitor;
	cb_nestExpression(v->builder, -1, false);
	cb_startSpan(v->builder, Cost_normal);
	bool splitOnTarget = ccv_forcesSplit(c->target);
	if (splitOnTarget) {
		if (c->properties.n > 1) {
			c->propertyRule = positional_rule_new(NULL, c->properties.n, 0, 0);
			cb_startLazyRule(v->builder, c->propertyRule);
		} else {
			ccv_enableRule(c, true);
		}
	}
	sv_visit(v, c->target);
	if (c->properties.n == 1) {
		sv_soloZeroSplit(v);
		sel_write(c->properties.v[0], c);
	} else if (c->properties.n > 1) {
		if (!splitOnTarget) {
			c->propertyRule = positional_rule_new(NULL, c->properties.n, 0, 0);
			cb_startRule(v->builder, c->propertyRule);
		}
		for (int i = 0; i < c->properties.n; i++) {
			rule_beforeArgument(c->propertyRule, sv_zeroSplit(v));
			sel_write(c->properties.v[i], c);
		}
		cb_endRule(v->builder);
	}
	if (c->calls.n > 1)
		cb_startBlockArgumentNesting(v->builder);
	for (int i = 0; i < c->calls.n; i++) {
		ccv_enableRule(c, false);
		sv_zeroSplit(v);
		sel_write(c->calls.v[i], c);
	}
	if (c->calls.n > 1)
		cb_endBlockArgumentNesting(v->builder);
	if (c->hasBlockCalls) {
		ccv_enableRule(c, false);
		sv_zeroSplit(v);
		ccv_disableRule(c);
		for (int i = 0; i < c->blockCalls.n; i++)
			sel_write(c->blockCalls.v[i], c);
		if (c->hangingCall)
			sel_write(c->hangingCall, c);
	}
	ccv_disableRule(c);
	ccv_endSpan(c);
	cb_unnest(v->builder, true);
}
