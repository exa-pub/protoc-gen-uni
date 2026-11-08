/* Port of dart_style 3.1.13 lib/src/short/line_splitting/{rule_set,solve_state,
 * solve_state_queue,line_splitter}.dart.
 *
 * Copyright (c) 2015, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

typedef struct LineWriter LineWriter;
typedef struct {
	const char *text;
	size_t n;
	int cost;
} FormatResult;
static FormatResult *lw_formatBlock(LineWriter *w, Chunk *chunk, int column);
static int lw_pageWidth(const LineWriter *w);

/* ---- rule_set.dart ---- */

struct RuleSet {
	int *values; /* NO_VALUE: null */
	int n;
};

static RuleSet *ruleset_new(int numRules)
{
	RuleSet *rs = xalloc(sizeof *rs);
	rs->n = numRules;
	rs->values = xalloc((size_t)(numRules ? numRules : 1) * sizeof *rs->values);
	for (int i = 0; i < numRules; i++)
		rs->values[i] = NO_VALUE;
	return rs;
}

/* `_values[rule.index!]`: Dart throws on a null or out of range index. */
static int ruleset_slot(const RuleSet *rs, const Rule *rule)
{
	if (rule->index < 0 || rule->index >= rs->n)
		fail("dart_style: internal error (rule index out of range)");
	return rule->index;
}

static bool ruleset_contains(const RuleSet *rs, const Rule *rule)
{
	if (rule->isHardened)
		return true;
	return rs->values[ruleset_slot(rs, rule)] != NO_VALUE;
}

static int ruleset_getValue(const RuleSet *rs, const Rule *rule)
{
	if (rule->isHardened)
		return rule_fullySplitValue(rule);
	int value = rs->values[ruleset_slot(rs, rule)];
	if (value != NO_VALUE)
		return value;
	return Rule_unsplit;
}

static RuleSet *ruleset_clone(const RuleSet *rs)
{
	RuleSet *c = xalloc(sizeof *c);
	c->n = rs->n;
	c->values = xalloc((size_t)(rs->n ? rs->n : 1) * sizeof *c->values);
	memcpy(c->values, rs->values, (size_t)rs->n * sizeof *c->values);
	return c;
}

/* onSplitRule: appends to *mustSplitRules (created on demand). */
static bool ruleset_tryBind(RuleSet *rs, Rule *rule, int value, PtrVec **mustSplitRules)
{
	rs->values[ruleset_slot(rs, rule)] = value;
	for (int i = 0; i < rule->ncons; i++) {
		Rule *other = rule->cons[i].rule;
		int otherValue;
		if (other->isHardened)
			otherValue = rule_fullySplitValue(other);
		else
			otherValue = rs->values[ruleset_slot(rs, other)];
		int constraint = rule_constrain(rule, value, other);
		if (otherValue == NO_VALUE) {
			if (constraint == Rule_mustSplit) {
				if (rule_numValues(other) == 2) {
					if (!ruleset_tryBind(rs, other, 1, mustSplitRules))
						return false;
				} else if (mustSplitRules) {
					if (!*mustSplitRules)
						*mustSplitRules = xalloc(sizeof(PtrVec));
					pv_push(*mustSplitRules, other);
				}
			} else if (constraint != NO_VALUE) {
				if (!ruleset_tryBind(rs, other, constraint, mustSplitRules))
					return false;
			}
		} else {
			if (constraint == Rule_mustSplit) {
				if (otherValue == Rule_unsplit)
					return false;
			} else if (constraint != NO_VALUE) {
				if (otherValue != constraint)
					return false;
			}
			constraint = rule_constrain(other, otherValue, rule);
			if (constraint == Rule_mustSplit) {
				if (value == Rule_unsplit)
					return false;
			} else if (constraint != NO_VALUE) {
				if (value != constraint)
					return false;
			}
		}
	}
	return true;
}

/* SplitSet */
typedef struct {
	int *columns;
	int n;
	int cost;
} SplitSet;

static SplitSet *splitset_new(int numChunks)
{
	SplitSet *s = xalloc(sizeof *s);
	s->n = numChunks;
	s->columns = xalloc((size_t)(numChunks ? numChunks : 1) * sizeof *s->columns);
	for (int i = 0; i < numChunks; i++)
		s->columns[i] = -1;
	return s;
}

static bool splitset_shouldSplitAt(const SplitSet *s, int index) { return index < s->n && s->columns[index] != -1; }

/* ---- line_splitter.dart (state) ---- */

typedef struct SolveState SolveState;

typedef struct {
	SolveState **v;
	int length, cap;
} SolveStateQueue;

typedef struct {
	LineWriter *writer;
	Chunk **chunks;
	int nchunks;
	Rule **rules;
	int nrules;
	int blockIndentation;
	SolveStateQueue queue;
} LineSplitter;

/* ---- solve_state.dart ---- */

struct SolveState {
	LineSplitter *splitter;
	RuleSet *ruleValues;
	PtrMap liveRules;
	SplitSet *splits;
	int overflowChars;
	bool isComplete;
	/* lazily initialized */
	bool constraintsInit;
	PtrMap boundRules, unboundRules;
	PtrMap constraints; /* Rule -> int */
	bool unboundConstraintsInit;
	PtrMap unboundConstraints; /* Rule -> index into unboundSets */
	PtrVec unboundSets;        /* PtrMap* (sets of ints stored as keys value+1 via intptr) */
	bool boundRulesInUnboundLinesInit;
	PtrMap boundRulesInUnboundLines;
};

static int ss_getValue(const SolveState *s, const Rule *rule) { return ruleset_getValue(s->ruleValues, rule); }

static void ss_calculateSplits(SolveState *s);
static void ss_calculateCost(SolveState *s);

static SolveState *ss_new(LineSplitter *splitter, RuleSet *ruleValues)
{
	SolveState *s = xalloc(sizeof *s);
	s->splitter = splitter;
	s->ruleValues = ruleValues;
	s->isComplete = true;
	ss_calculateSplits(s);
	ss_calculateCost(s);
	return s;
}

static bool ss_isBetterThan(const SolveState *s, const SolveState *other)
{
	if (!s->isComplete)
		return false;
	if (s->overflowChars != other->overflowChars)
		return s->overflowChars < other->overflowChars;
	return s->splits->cost < other->splits->cost;
}

static void ss_initConstraints(SolveState *s)
{
	if (s->constraintsInit)
		return;
	s->constraintsInit = true;
	LineSplitter *sp = s->splitter;
	PtrVec bound = {0};
	for (int i = 0; i < sp->nrules; i++) {
		Rule *rule = sp->rules[i];
		if (ruleset_contains(s->ruleValues, rule)) {
			if (ps_add(&s->boundRules, rule))
				pv_push(&bound, rule);
		} else {
			ps_add(&s->unboundRules, rule);
		}
	}
	for (int i = 0; i < bound.n; i++) {
		Rule *b = bound.v[i];
		for (int j = 0; j < b->ncons; j++) {
			Rule *unbound = b->cons[j].rule;
			if (!pm_has(&s->unboundRules, unbound))
				continue;
			int value = ruleset_getValue(s->ruleValues, b);
			int constraint = rule_constrain(b, value, unbound);
			if (constraint != NO_VALUE)
				pm_put(&s->constraints, unbound, constraint);
		}
	}
}

/* The disallowed value set of an unbound rule: values stored as keys (value + 1). */
static PtrMap *ss_unboundSet(SolveState *s, Rule *rule)
{
	int idx;
	if (!pm_get(&s->unboundConstraints, rule, &idx))
		return NULL;
	return s->unboundSets.v[idx];
}

static void ss_initUnboundConstraints(SolveState *s)
{
	if (s->unboundConstraintsInit)
		return;
	ss_initConstraints(s);
	s->unboundConstraintsInit = true;
	LineSplitter *sp = s->splitter;
	/* iterate _unboundRules in insertion order (= splitter rule order) */
	for (int i = 0; i < sp->nrules; i++) {
		Rule *unbound = sp->rules[i];
		if (!pm_has(&s->unboundRules, unbound))
			continue;
		/* each rule appears once in _unboundRules */
		bool seen = false;
		for (int k = 0; k < i; k++)
			if (sp->rules[k] == unbound)
				seen = true;
		if (seen)
			continue;
		for (int j = 0; j < unbound->ncons; j++) {
			Rule *b = unbound->cons[j].rule;
			if (!pm_has(&s->boundRules, b))
				continue;
			int boundValue = ruleset_getValue(s->ruleValues, b);
			int nv = rule_numValues(unbound);
			for (int value = 0; value < nv; value++) {
				int constraint = rule_constrain(unbound, value, b);
				if (constraint == NO_VALUE)
					continue;
				if (constraint == boundValue)
					continue;
				if (constraint == Rule_mustSplit && boundValue != Rule_unsplit)
					continue;
				PtrMap *set = ss_unboundSet(s, unbound);
				if (!set) {
					set = xalloc(sizeof *set);
					pm_put(&s->unboundConstraints, unbound, s->unboundSets.n);
					pv_push(&s->unboundSets, set);
				}
				ps_add(set, (void *)(intptr_t)(value + 1));
			}
		}
	}
}

static void ss_initBoundRulesInUnboundLines(SolveState *s)
{
	if (s->boundRulesInUnboundLinesInit)
		return;
	s->boundRulesInUnboundLinesInit = true;
	LineSplitter *sp = s->splitter;
	PtrVec boundInLine = {0};
	PtrMap boundInLineSet = {0};
	bool hasUnbound = false;
	for (int i = 0; i < sp->nchunks; i++) {
		if (splitset_shouldSplitAt(s->splits, i)) {
			if (hasUnbound)
				for (int k = 0; k < boundInLine.n; k++)
					ps_add(&s->boundRulesInUnboundLines, boundInLine.v[k]);
			boundInLine.n = 0;
			boundInLineSet = (PtrMap){0};
			hasUnbound = false;
		}
		Rule *rule = sp->chunks[i]->rule;
		if (ruleset_contains(s->ruleValues, rule)) {
			if (ps_add(&boundInLineSet, rule))
				pv_push(&boundInLine, rule);
		} else {
			hasUnbound = true;
		}
	}
	if (hasUnbound)
		for (int k = 0; k < boundInLine.n; k++)
			ps_add(&s->boundRulesInUnboundLines, boundInLine.v[k]);
}

static bool ss_isOverlapping(SolveState *s, SolveState *other)
{
	ss_initBoundRulesInUnboundLines(s);
	ss_initBoundRulesInUnboundLines(other);
	if (s->boundRulesInUnboundLines.n != other->boundRulesInUnboundLines.n)
		return false;
	for (int i = 0; i < s->boundRulesInUnboundLines.cap; i++) {
		Rule *rule = s->boundRulesInUnboundLines.keys[i];
		if (!rule)
			continue;
		if (!pm_has(&other->boundRulesInUnboundLines, rule))
			return false;
		if (ruleset_getValue(s->ruleValues, rule) != ruleset_getValue(other->ruleValues, rule))
			return false;
	}
	ss_initConstraints(s);
	ss_initConstraints(other);
	if (s->constraints.n != other->constraints.n)
		return false;
	for (int i = 0; i < s->constraints.cap; i++) {
		Rule *rule = s->constraints.keys[i];
		if (!rule)
			continue;
		int ov;
		if (!pm_get(&other->constraints, rule, &ov) || ov != s->constraints.vals[i])
			return false;
	}
	ss_initUnboundConstraints(s);
	ss_initUnboundConstraints(other);
	if (s->unboundConstraints.n != other->unboundConstraints.n)
		return false;
	for (int i = 0; i < s->unboundConstraints.cap; i++) {
		Rule *rule = s->unboundConstraints.keys[i];
		if (!rule)
			continue;
		PtrMap *disallowed = s->unboundSets.v[s->unboundConstraints.vals[i]];
		PtrMap *otherDisallowed = ss_unboundSet(other, rule);
		if (!otherDisallowed)
			fail("dart_style: internal error (null check)");
		if (disallowed->n != otherDisallowed->n)
			return false;
		for (int k = 0; k < disallowed->cap; k++)
			if (disallowed->keys[k] && !pm_has(otherDisallowed, disallowed->keys[k]))
				return false;
	}
	return true;
}

static int int_compare(int a, int b) { return a < b ? -1 : a > b ? 1 : 0; }

static int ss_compareOverlap(SolveState *s, SolveState *other)
{
	if (!ss_isOverlapping(s, other))
		return 0;
	LineSplitter *sp = s->splitter;
	for (int i = 0; i < sp->nrules; i++) {
		Rule *rule = sp->rules[i];
		int value = ruleset_getValue(s->ruleValues, rule);
		int otherValue = ruleset_getValue(other->ruleValues, rule);
		if (value != otherValue)
			return int_compare(value, otherValue);
	}
	fail("dart_style: internal error (unreachable)");
}

static void ls_enqueue(LineSplitter *sp, SolveState *state);

static void ss_expand(SolveState *s)
{
	LineSplitter *sp = s->splitter;
	RuleSet *unsplitRules = ruleset_clone(s->ruleValues);
	int triedRules = 0;
	for (int i = 0; i < sp->nrules; i++) {
		Rule *rule = sp->rules[i];
		if (pm_has(&s->liveRules, rule)) {
			int nv = rule_numValues(rule);
			for (int value = 1; value < nv; value++) {
				RuleSet *boundRules = ruleset_clone(unsplitRules);
				PtrVec *mustSplitRules = NULL;
				bool valid = ruleset_tryBind(boundRules, rule, value, &mustSplitRules);
				if (!valid)
					continue;
				SolveState *state = ss_new(sp, boundRules);
				if (mustSplitRules) {
					state->isComplete = false;
					for (int k = 0; k < mustSplitRules->n; k++)
						ps_add(&state->liveRules, mustSplitRules->v[k]);
				}
				ls_enqueue(sp, state);
			}
			if (++triedRules == s->liveRules.n)
				break;
		}
		if (!ruleset_contains(s->ruleValues, rule)) {
			if (!ruleset_tryBind(unsplitRules, rule, 0, NULL))
				break;
		}
	}
}

static void ss_calculateSplits(SolveState *s)
{
	LineSplitter *sp = s->splitter;
	PtrVec usedNestingLevels = {0};
	for (int i = 0; i < sp->nchunks; i++) {
		Chunk *chunk = sp->chunks[i];
		if (rule_isSplit(chunk->rule, ss_getValue(s, chunk->rule), chunk)) {
			NestingLevel *nesting = chunk->nesting;
			if (!nesting->isMarked) {
				nesting->isMarked = true;
				pv_push(&usedNestingLevels, nesting);
				nesting_clearTotalUsedIndent(nesting);
			}
		}
	}
	for (int i = 0; i < usedNestingLevels.n; i++)
		nesting_refreshTotalUsedIndent(usedNestingLevels.v[i]);
	for (int i = 0; i < usedNestingLevels.n; i++)
		((NestingLevel *)usedNestingLevels.v[i])->isMarked = false;

	s->splits = splitset_new(sp->nchunks);
	for (int i = 0; i < sp->nchunks; i++) {
		Chunk *chunk = sp->chunks[i];
		if (rule_isSplit(chunk->rule, ss_getValue(s, chunk->rule), chunk)) {
			int indent = 0;
			if (!chunk->flushLeft) {
				indent = sp->blockIndentation + chunk->indent;
				indent += nesting_totalUsedIndent(chunk->nesting);
				if (chunk_indentBlock(sp->chunks[i], s->ruleValues))
					indent += Indent_expression;
			}
			s->splits->columns[i] = indent;
		}
	}
}

/* Adds rule and the rules it constrains to the live rules; true if any were added. */
static bool ss_addLiveRules(SolveState *s, Rule *rule)
{
	if (!rule)
		return false;
	bool added = false;
	PtrVec *all = rule_allConstrainedRules(rule);
	for (int i = 0; i < all->n; i++) {
		Rule *constrained = all->v[i];
		if (ruleset_contains(s->ruleValues, constrained))
			continue;
		ps_add(&s->liveRules, constrained);
		added = true;
	}
	return added;
}

static void ss_calculateCost(SolveState *s)
{
	LineSplitter *sp = s->splitter;
	int pageWidth = lw_pageWidth(sp->writer);
	int cost = 0;
	int length = 0;
	bool foundOverflowRules = false;
	int start = 0;
	PtrVec splitSpans = {0};
	NestingLevel *previousNesting = NULL;

#define END_LINE(end)                                                                                              \
	do {                                                                                                       \
		int end_ = (end);                                                                                  \
		if (length > pageWidth) {                                                                          \
			s->overflowChars += length - pageWidth;                                                    \
			if (!foundOverflowRules) {                                                                 \
				for (int k_ = start; k_ < end_; k_++)                                              \
					if (ss_addLiveRules(s, sp->chunks[k_]->rule))                              \
						foundOverflowRules = true;                                         \
			}                                                                                          \
		}                                                                                                  \
		start = end_;                                                                                      \
	} while (0)

	for (int i = 0; i < sp->nchunks; i++) {
		Chunk *chunk = sp->chunks[i];
		if (splitset_shouldSplitAt(s->splits, i)) {
			END_LINE(i);
			for (int k = 0; k < chunk->spans.n; k++) {
				Span *span = chunk->spans.v[k];
				if (!span->isMarked) {
					span->isMarked = true;
					pv_push(&splitSpans, span);
					cost += span->cost;
				}
			}
			int totalIndent = nesting_totalUsedIndent(chunk->nesting);
			if (previousNesting && totalIndent != 0 &&
			    totalIndent == nesting_totalUsedIndent(previousNesting) && chunk->nesting != previousNesting)
				s->overflowChars += 10000;
			previousNesting = chunk->nesting;
			length = s->splits->columns[i];
		} else {
			if (chunk->spaceWhenUnsplit)
				length++;
		}
		if (chunk->isBlock) {
			if (splitset_shouldSplitAt(s->splits, i))
				cost += lw_formatBlock(sp->writer, chunk, s->splits->columns[i])->cost;
			else
				length += chunk_unsplitBlockLength(chunk);
		}
		length += chunk->len16;
	}
	for (int i = 0; i < sp->nrules; i++) {
		int value = s->ruleValues->values[i];
		if (value != NO_VALUE && value != Rule_unsplit)
			cost += rule_cost(sp->rules[i]);
	}
	for (int i = 0; i < splitSpans.n; i++)
		((Span *)splitSpans.v[i])->isMarked = false;
	END_LINE(sp->nchunks);
#undef END_LINE
	s->splits->cost = cost;
}

/* ---- solve_state_queue.dart ---- */

static int ssq_compareScore(const SolveState *a, const SolveState *b)
{
	int aCost = a->splits->cost, bCost = b->splits->cost;
	if (aCost != bCost)
		return aCost < bCost ? -1 : 1;
	return int_compare(a->overflowChars, b->overflowChars);
}

static int ssq_compareRules(const LineSplitter *sp, const SolveState *a, const SolveState *b)
{
	for (int i = 0; i < sp->nrules; i++) {
		Rule *rule = sp->rules[i];
		int aValue = ss_getValue(a, rule), bValue = ss_getValue(b, rule);
		if (aValue != bValue)
			return int_compare(aValue, bValue);
	}
	fail("dart_style: internal error (unreachable)");
}

static int ssq_compare(const LineSplitter *sp, const SolveState *a, const SolveState *b)
{
	int comparison = ssq_compareScore(a, b);
	if (comparison != 0)
		return comparison;
	return ssq_compareRules(sp, a, b);
}

static bool ssq_tryOverlap(LineSplitter *sp, SolveState *state)
{
	SolveStateQueue *q = &sp->queue;
	if (q->length == 0)
		return false;
	int position = 1;
	do {
		int index = position - 1;
		SolveState *enqueued = q->v[index];
		int comparison = ssq_compareScore(enqueued, state);
		if (comparison == 0) {
			int overlap = ss_compareOverlap(enqueued, state);
			if (overlap < 0) {
				return true;
			} else if (overlap > 0) {
				q->v[index] = state;
				return true;
			} else {
				comparison = ssq_compareRules(sp, enqueued, state);
			}
		}
		if (comparison < 0) {
			int leftChildPosition = position * 2;
			if (leftChildPosition <= q->length) {
				position = leftChildPosition;
				continue;
			}
		}
		do {
			while (position & 1)
				position >>= 1;
			position += 1;
		} while (position > q->length);
	} while (position != 1);
	return false;
}

static void ssq_bubbleUp(LineSplitter *sp, SolveState *element, int index)
{
	SolveStateQueue *q = &sp->queue;
	while (index > 0) {
		int parentIndex = (index - 1) / 2;
		SolveState *parent = q->v[parentIndex];
		if (ssq_compare(sp, element, parent) > 0)
			break;
		q->v[index] = parent;
		index = parentIndex;
	}
	q->v[index] = element;
}

static void ssq_bubbleDown(LineSplitter *sp, SolveState *element, int index)
{
	SolveStateQueue *q = &sp->queue;
	int rightChildIndex = index * 2 + 2;
	while (rightChildIndex < q->length) {
		int leftChildIndex = rightChildIndex - 1;
		SolveState *leftChild = q->v[leftChildIndex];
		SolveState *rightChild = q->v[rightChildIndex];
		int comparison = ssq_compare(sp, leftChild, rightChild);
		int minChildIndex;
		SolveState *minChild;
		if (comparison < 0) {
			minChild = leftChild;
			minChildIndex = leftChildIndex;
		} else {
			minChild = rightChild;
			minChildIndex = rightChildIndex;
		}
		comparison = ssq_compare(sp, element, minChild);
		if (comparison <= 0) {
			q->v[index] = element;
			return;
		}
		q->v[index] = minChild;
		index = minChildIndex;
		rightChildIndex = index * 2 + 2;
	}
	int leftChildIndex = rightChildIndex - 1;
	if (leftChildIndex < q->length) {
		SolveState *child = q->v[leftChildIndex];
		int comparison = ssq_compare(sp, element, child);
		if (comparison > 0) {
			q->v[index] = child;
			index = leftChildIndex;
		}
	}
	q->v[index] = element;
}

static void ssq_add(LineSplitter *sp, SolveState *state)
{
	SolveStateQueue *q = &sp->queue;
	if (ssq_tryOverlap(sp, state))
		return;
	if (q->length == q->cap) {
		int newCapacity = q->cap * 2 + 1;
		if (newCapacity < 7)
			newCapacity = 7;
		q->v = xrealloc(q->v, (size_t)q->cap * sizeof *q->v, (size_t)newCapacity * sizeof *q->v);
		q->cap = newCapacity;
	}
	ssq_bubbleUp(sp, state, q->length++);
}

static SolveState *ssq_removeFirst(LineSplitter *sp)
{
	SolveStateQueue *q = &sp->queue;
	SolveState *result = q->v[0];
	q->length--;
	if (q->length > 0) {
		SolveState *last = q->v[q->length];
		q->v[q->length] = NULL;
		ssq_bubbleDown(sp, last, 0);
	}
	return result;
}

/* ---- line_splitter.dart ---- */

enum { maxAttempts = 5000 };

static void ls_init(LineSplitter *sp, LineWriter *writer, Chunk **chunks, int nchunks, int blockIndentation)
{
	memset(sp, 0, sizeof *sp);
	sp->writer = writer;
	sp->chunks = chunks;
	sp->nchunks = nchunks;
	sp->blockIndentation = blockIndentation;
	PtrMap seen = {0};
	sp->rules = xalloc((size_t)(nchunks ? nchunks : 1) * sizeof *sp->rules);
	for (int i = 0; i < nchunks; i++)
		if (ps_add(&seen, chunks[i]->rule))
			sp->rules[sp->nrules++] = chunks[i]->rule;
	for (int i = 0; i < sp->nrules; i++)
		sp->rules[i]->index = i;
	for (int i = 0; i < sp->nrules; i++)
		rule_forgetUnusedRules(sp->rules[i]);
	/* the queue starts with capacity 7 */
	sp->queue.cap = 7;
	sp->queue.v = xalloc(7 * sizeof *sp->queue.v);
}

static void ls_enqueue(LineSplitter *sp, SolveState *state) { ssq_add(sp, state); }

static SplitSet *ls_apply(LineSplitter *sp)
{
	SolveState *bestSolution = ss_new(sp, ruleset_new(sp->nrules));
	ssq_add(sp, bestSolution);
	int attempts = 0;
	while (sp->queue.length != 0) {
		SolveState *state = ssq_removeFirst(sp);
		if (ss_isBetterThan(state, bestSolution)) {
			bestSolution = state;
			if (bestSolution->overflowChars == 0)
				break;
		}
		if (attempts++ > maxAttempts)
			break;
		ss_expand(state);
	}
	return bestSolution->splits;
}
