/* Port of dart_style 3.1.13 lib/src/short/chunk.dart, nesting_level.dart and
 * nesting_builder.dart (selections are not ported: the formatter is never given one).
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* ---- nesting_level.dart ---- */

static NestingLevel *nesting_new(void)
{
	NestingLevel *n = xalloc(sizeof *n);
	n->totalUsedIndent = -1;
	return n;
}

static NestingLevel *nesting_nest(NestingLevel *n, int spaces)
{
	NestingLevel *r = nesting_new();
	r->parent = n;
	r->indent = spaces;
	return r;
}

static bool nesting_isNested(const NestingLevel *n) { return n->parent != NULL; }

static void nesting_clearTotalUsedIndent(NestingLevel *n)
{
	for (; n; n = n->parent)
		n->totalUsedIndent = -1;
}

static void nesting_refreshTotalUsedIndent(NestingLevel *n)
{
	if (n->totalUsedIndent >= 0)
		return;
	int totalIndent = 0;
	if (n->parent) {
		nesting_refreshTotalUsedIndent(n->parent);
		totalIndent += n->parent->totalUsedIndent;
	}
	if (n->isMarked)
		totalIndent += n->indent;
	n->totalUsedIndent = totalIndent;
}

static int nesting_totalUsedIndent(const NestingLevel *n)
{
	if (n->totalUsedIndent < 0)
		fail("dart_style: internal error (totalUsedIndent)");
	return n->totalUsedIndent;
}

/* ---- chunk.dart ---- */

static Rule *rule_dummy; /* Rule.dummy, created per format() call */

static Chunk *chunk_new(Rule *rule, int indent, NestingLevel *nesting, bool space, bool flushLeft, bool isDouble)
{
	Chunk *c = xalloc(sizeof *c);
	c->rule = rule;
	c->indent = indent;
	c->nesting = nesting;
	c->spaceWhenUnsplit = space;
	c->flushLeft = flushLeft;
	c->isDouble = isDouble;
	c->canDivide = true;
	return c;
}

static Chunk *chunk_dummy(void)
{
	Chunk *c = chunk_new(rule_dummy, 0, nesting_new(), false, false, false);
	buf_adds(&c->text, "(dummy)");
	c->len16 = 7;
	return c;
}

static Chunk *block_chunk_new(Chunk *argument, Rule *rule, int indent, NestingLevel *nesting, bool space,
			      bool flushLeft)
{
	Chunk *c = chunk_new(rule, indent, nesting, space, flushLeft, false);
	c->isBlock = true;
	c->argument = argument;
	return c;
}

static bool chunk_textIsEmpty(const Chunk *c) { return c->text.n == 0; }

static bool chunk_textEndsWith(const Chunk *c, const char *s)
{
	size_t k = strlen(s);
	return c->text.n >= k && memcmp(c->text.p + c->text.n - k, s, k) == 0;
}

/* Chunk.length */
static int chunk_length(const Chunk *c) { return (c->spaceWhenUnsplit ? 1 : 0) + c->len16; }

/* Chunk.unsplitBlockLength */
static int chunk_unsplitBlockLength(const Chunk *c)
{
	if (!c->isBlock)
		return 0;
	int length = 0;
	for (int i = 0; i < c->children.n; i++) {
		Chunk *child = c->children.v[i];
		length += chunk_length(child) + chunk_unsplitBlockLength(child);
	}
	return length;
}

static void chunk_appendText(Chunk *c, const char *text, size_t n)
{
	buf_add(&c->text, text, n);
	c->len16 += utf16_len(text, n);
}

/* Chunk.updateSplit: flushLeft -1 = null, space -1 = null */
static void chunk_updateSplit(Chunk *c, int flushLeft, bool isDouble, int space)
{
	if (flushLeft >= 0)
		c->flushLeft = flushLeft;
	if (isDouble)
		c->isDouble = true;
	if (space >= 0)
		c->spaceWhenUnsplit = space;
}

static void chunk_preventDivide(Chunk *c) { c->canDivide = false; }

typedef struct RuleSet RuleSet;
static int ruleset_getValue(const RuleSet *rs, const Rule *rule);

/* BlockChunk.indentBlock */
static bool chunk_indentBlock(Chunk *c, const RuleSet *values)
{
	if (!c->isBlock)
		return false;
	Chunk *argument = c->argument;
	if (!argument)
		return false;
	Rule *rule = argument->rule;
	if (rule == rule_dummy)
		return true;
	return rule_isSplit(rule, ruleset_getValue(values, rule), argument);
}

static Span *span_new(int cost)
{
	Span *s = xalloc(sizeof *s);
	s->cost = cost;
	return s;
}

/* ---- nesting_builder.dart ---- */

typedef struct {
	int *stack;
	int n, cap;
	NestingLevel *pendingNesting;
	NestingLevel *nesting;
} NestingBuilder;

static void nb_init(NestingBuilder *b)
{
	b->cap = 8;
	b->stack = xalloc((size_t)b->cap * sizeof *b->stack);
	b->stack[b->n++] = 0;
	b->nesting = nesting_new();
}

static int nb_indentation(const NestingBuilder *b) { return b->stack[b->n - 1]; }

static NestingLevel *nb_currentNesting(const NestingBuilder *b)
{
	return b->pendingNesting ? b->pendingNesting : b->nesting;
}

static void nb_indent(NestingBuilder *b, int spaces)
{
	if (b->n == b->cap) {
		b->stack = xrealloc(b->stack, (size_t)b->cap * sizeof *b->stack, (size_t)b->cap * 2 * sizeof *b->stack);
		b->cap *= 2;
	}
	b->stack[b->n] = b->stack[b->n - 1] + spaces;
	b->n++;
}

static void nb_unindent(NestingBuilder *b) { b->n--; }

static void nb_nest(NestingBuilder *b, int indent)
{
	if (b->pendingNesting)
		b->pendingNesting = nesting_nest(b->pendingNesting, indent);
	else
		b->pendingNesting = nesting_nest(b->nesting, indent);
}

static void nb_unnest(NestingBuilder *b)
{
	if (b->pendingNesting)
		b->pendingNesting = b->pendingNesting->parent;
	else
		b->pendingNesting = b->nesting->parent;
	/* (an assert upstream, disabled in the compiled plugin) */
}

static void nb_commitNesting(NestingBuilder *b)
{
	if (!b->pendingNesting)
		return;
	b->nesting = b->pendingNesting;
	b->pendingNesting = NULL;
}
