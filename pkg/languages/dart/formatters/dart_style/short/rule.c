/* Port of dart_style 3.1.13 lib/src/short/rule/{rule,argument,combinator,type_argument}.dart
 * and lib/src/constants.dart.
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* constants.dart: Cost */
enum {
	Cost_arrow = 0,
	Cost_normal = 1,
	Cost_assign = 1,
	Cost_assignBlock = 2,
	Cost_firstBlockArgument = 2,
	Cost_positionalArguments = 2,
	Cost_singleElementList = 2,
	Cost_splitBlocks = 2,
	Cost_constructorName = 4,
	Cost_index = 4,
	Cost_typeArgument = 4,
	Cost_parameterType = 4,
};

/* constants.dart: Indent */
enum {
	Indent_none = 0,
	Indent_block = 2,
	Indent_cascade = 2,
	Indent_expression = 4,
	Indent_constructorInitializer = 4,
	Indent_initializer = 2,
	Indent_initializerWithOptionalParameter = 3,
};

/* Rule.unsplit, Rule.mustSplit, Rule._fullSplitConstraint */
enum { Rule_unsplit = 0, Rule_mustSplit = -1, Rule_fullSplitConstraint = -2 };

/* "null" for int? results */
#define NO_VALUE INT32_MIN

static Rule *rule_new_cost(int cost)
{
	Rule *r = xalloc(sizeof *r);
	r->kind = RK_RULE;
	r->cost = cost;
	r->index = -1;
	return r;
}

static Rule *rule_new(void) { return rule_new_cost(Cost_normal); }

static void rule_harden(Rule *r) { r->isHardened = true; }

/* Rule.hard() */
static Rule *rule_hard(void)
{
	Rule *r = rule_new_cost(0);
	rule_harden(r);
	return r;
}

static int rule_numValues(const Rule *r)
{
	switch (r->kind) {
	case RK_POSITIONAL: {
		int result = r->args.n + 1;
		if (r->args.n > 1)
			result++;
		if (r->leadingCollections > 0 || r->trailingCollections > 0)
			result++;
		return result;
	}
	case RK_NAMED:
		return 3;
	case RK_COMBINATOR: {
		int count = 2;
		if (r->names.n == 2)
			count += 3;
		else
			count++;
		return count;
	}
	case RK_TYPE_ARGUMENT:
		return r->args.n == 1 ? 2 : r->args.n + 2;
	default:
		return 2;
	}
}

static int rule_fullySplitValue(const Rule *r) { return rule_numValues(r) - 1; }

static int rule_cost(const Rule *r) { return r->kind == RK_TYPE_ARGUMENT ? Cost_typeArgument : r->cost; }

static bool rule_splitsOnInnerRules(const Rule *r) { return r->splitContaining ? r->trackInnerRules : true; }

static bool combinator_isCombinatorSplit(const Rule *r, int combinator, const Chunk *chunk)
{
	return pv_contains(&r->combinators, chunk) || pv_contains((PtrVec *)r->names.v[combinator], chunk);
}

static bool rule_isSplitAtValue(const Rule *r, int value, const Chunk *chunk)
{
	switch (r->kind) {
	case RK_POSITIONAL: {
		if (value == 1)
			return chunk == r->args.v[0];
		if (value <= r->args.n) {
			int argument = r->args.n - value + 1;
			return chunk == r->args.v[argument];
		}
		if (value == r->args.n + 1) {
			for (int i = 0; i < r->leadingCollections; i++)
				if (chunk == r->args.v[i])
					return false;
			for (int i = r->args.n - r->trailingCollections; i < r->args.n; i++)
				if (chunk == r->args.v[i])
					return false;
			return true;
		}
		return true;
	}
	case RK_NAMED:
		if (value == 1)
			return chunk == r->args.v[0];
		return true;
	case RK_COMBINATOR:
		switch (value) {
		case 1:
			return pv_contains(&r->combinators, chunk);
		case 2:
			return combinator_isCombinatorSplit(r, 0, chunk);
		case 3:
			if (r->names.n == 2)
				return combinator_isCombinatorSplit(r, 1, chunk);
			return true;
		default:
			return true;
		}
	default:
		return true;
	}
}

/* Rule.isSplit (TypeArgumentRule overrides it completely). */
static bool rule_isSplit(const Rule *r, int value, const Chunk *chunk)
{
	if (r->kind == RK_TYPE_ARGUMENT) {
		if (value == Rule_unsplit)
			return false;
		if (value == rule_numValues(r) - 1)
			return true;
		return chunk == r->args.v[r->args.n - value];
	}
	if (r->isHardened)
		return true;
	if (value == Rule_unsplit)
		return false;
	return rule_isSplitAtValue(r, value, chunk);
}

static ConstraintEntry *rule_findConstraints(const Rule *r, const Rule *other)
{
	for (int i = 0; i < r->ncons; i++)
		if (r->cons[i].rule == other)
			return &r->cons[i];
	return NULL;
}

/* Rule.constrain: NO_VALUE for null. */
static int rule_constrain(const Rule *r, int value, const Rule *other)
{
	if (value == Rule_unsplit)
		return NO_VALUE;
	ConstraintEntry *e = rule_findConstraints(r, other);
	if (!e)
		return NO_VALUE;
	for (int i = 0; i < e->n; i++) {
		Constraint *c = &e->v[i];
		if (value >= c->min && value <= c->max) {
			if (c->otherValue == Rule_fullSplitConstraint)
				return rule_fullySplitValue(other);
			return c->otherValue;
		}
	}
	return NO_VALUE;
}

static void rule_addRangeConstraint(Rule *r, int min, int max, Rule *other, int otherValue)
{
	ConstraintEntry *e = rule_findConstraints(r, other);
	if (!e) {
		if (r->ncons == r->capcons) {
			int cap = r->capcons ? r->capcons * 2 : 4;
			r->cons = xrealloc(r->cons, (size_t)r->capcons * sizeof *r->cons, (size_t)cap * sizeof *r->cons);
			r->capcons = cap;
		}
		e = &r->cons[r->ncons++];
		e->rule = other;
	}
	if (e->n == e->cap) {
		int cap = e->cap ? e->cap * 2 : 2;
		e->v = xrealloc(e->v, (size_t)e->cap * sizeof *e->v, (size_t)cap * sizeof *e->v);
		e->cap = cap;
	}
	e->v[e->n++] = (Constraint){min, max, otherValue};
}

static void rule_addConstraint(Rule *r, int value, Rule *other, int otherValue)
{
	rule_addRangeConstraint(r, value, value, other, otherValue);
}

static void rule_constrainWhenSplit(Rule *r, Rule *other)
{
	rule_addRangeConstraint(r, 1, 100000, other, Rule_fullSplitConstraint);
}

static void rule_constrainWhenFullySplit(Rule *r, Rule *other)
{
	rule_addConstraint(r, rule_fullySplitValue(r), other, Rule_fullSplitConstraint);
}

static void rule_forgetUnusedRules(Rule *r)
{
	int k = 0;
	for (int i = 0; i < r->ncons; i++)
		if (r->cons[i].rule->index >= 0)
			r->cons[k++] = r->cons[i];
	r->ncons = k;
	r->allConstrained = NULL;
}

static void rule_traverseConstraints(PtrVec *rules, PtrMap *set, Rule *rule)
{
	if (!ps_add(set, rule))
		return;
	pv_push(rules, rule);
	for (int i = 0; i < rule->ncons; i++)
		rule_traverseConstraints(rules, set, rule->cons[i].rule);
}

/* Rule.allConstrainedRules (insertion ordered). */
static PtrVec *rule_allConstrainedRules(Rule *r)
{
	if (r->allConstrained)
		return r->allConstrained;
	PtrVec *rules = xalloc(sizeof *rules);
	PtrMap set = {0};
	rule_traverseConstraints(rules, &set, r);
	r->allConstrained = rules;
	return rules;
}

/* SplitContainingRule */
static void rule_disableSplitOnInnerRules(Rule *r) { r->trackInnerRules = false; }
static void rule_enableSplitOnInnerRules(Rule *r) { r->trackInnerRules = true; }

/* ArgumentRule.beforeArgument / TypeArgumentRule.beforeArgument */
static void rule_beforeArgument(Rule *r, Chunk *chunk) { pv_push(&r->args, chunk); }

/* PositionalRule(collectionRule, argumentCount:, leadingCollections:, trailingCollections:) */
static Rule *positional_rule_new(Rule *collectionRule, int argumentCount, int leadingCollections,
				 int trailingCollections)
{
	Rule *r = rule_new();
	r->kind = RK_POSITIONAL;
	r->splitContaining = true;
	r->trackInnerRules = true;
	r->leadingCollections = leadingCollections;
	r->trailingCollections = trailingCollections;
	if (leadingCollections > 0)
		rule_addConstraint(r, 1, collectionRule, Rule_unsplit);
	if (leadingCollections > 0 || trailingCollections > 0)
		rule_addConstraint(r, argumentCount + 1, collectionRule, 1);
	for (int argument = 0; argument < leadingCollections; argument++) {
		int value = argumentCount - argument + 1;
		rule_addConstraint(r, value, collectionRule, Rule_unsplit);
	}
	for (int argument = argumentCount - trailingCollections; argument < argumentCount; argument++) {
		int value = argumentCount - argument + 1;
		rule_addConstraint(r, value, collectionRule, Rule_unsplit);
	}
	return r;
}

/* PositionalRule.addNamedArgsConstraints */
static void positional_addNamedArgsConstraints(Rule *r, Rule *rule)
{
	rule_constrainWhenFullySplit(r, rule);
	rule_addRangeConstraint(r, 1, rule_fullySplitValue(r), rule, Rule_mustSplit);
}

static Rule *named_rule_new(Rule *collectionRule, int leadingCollections, int trailingCollections)
{
	Rule *r = rule_new();
	r->kind = RK_NAMED;
	r->splitContaining = true;
	r->trackInnerRules = true;
	if (leadingCollections > 0 || trailingCollections > 0)
		rule_addConstraint(r, 1, collectionRule, Rule_unsplit);
	return r;
}

static Rule *combinator_rule_new(void)
{
	Rule *r = rule_new();
	r->kind = RK_COMBINATOR;
	return r;
}

static void combinator_addCombinator(Rule *r, Chunk *chunk)
{
	if (!pv_contains(&r->combinators, chunk))
		pv_push(&r->combinators, chunk);
	pv_push(&r->names, xalloc(sizeof(PtrVec)));
}

static void combinator_addName(Rule *r, Chunk *chunk)
{
	PtrVec *names = pv_last(&r->names);
	if (!pv_contains(names, chunk))
		pv_push(names, chunk);
}

static Rule *type_argument_rule_new(void)
{
	Rule *r = rule_new();
	r->kind = RK_TYPE_ARGUMENT;
	return r;
}
