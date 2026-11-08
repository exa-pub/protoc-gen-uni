/* Port of dart_style 3.1.13 lib/src/short/chunk_builder.dart and source_comment.dart (without
 * selections).
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* comment_type.dart */
typedef enum { CommentType_doc, CommentType_line, CommentType_block, CommentType_inlineBlock } CommentType;

/* source_comment.dart */
typedef struct {
	const char *text;
	size_t n;
	CommentType type;
	int linesBefore;
	bool flushLeft;
} SourceComment;

typedef struct ChunkBuilder ChunkBuilder;
struct ChunkBuilder {
	const DartFormatterOpts *formatter;
	ChunkBuilder *parent;
	bool isCompilationUnit;
	PtrVec *chunks;
	int pendingNewlines;
	bool pendingSpace;
	bool pendingNested;
	bool pendingFlushLeft;
	bool afterComment;
	PtrVec rules;
	PtrVec hardSplitRules; /* insertion ordered set */
	PtrMap hardSplitRulesSet;
	PtrVec lazyRules;
	PtrVec openSpans; /* OpenSpan* */
	NestingBuilder nesting;
	PtrVec blockArgumentNesting;
	int preventSplitNesting;
};

typedef struct {
	int start;
	int cost;
} OpenSpan;

static void cb_indent(ChunkBuilder *b, int spaces) { nb_indent(&b->nesting, spaces); }
static void cb_startBlockArgumentNesting(ChunkBuilder *b) { pv_push(&b->blockArgumentNesting, nb_currentNesting(&b->nesting)); }

static ChunkBuilder *cb_new(const DartFormatterOpts *formatter, bool isCompilationUnit)
{
	ChunkBuilder *b = xalloc(sizeof *b);
	b->formatter = formatter;
	b->isCompilationUnit = isCompilationUnit;
	b->chunks = xalloc(sizeof *b->chunks);
	nb_init(&b->nesting);
	cb_indent(b, formatter->indent);
	cb_startBlockArgumentNesting(b);
	return b;
}

static ChunkBuilder *cb_new_child(ChunkBuilder *parent, PtrVec *chunks)
{
	ChunkBuilder *b = xalloc(sizeof *b);
	b->formatter = parent->formatter;
	b->parent = parent;
	b->isCompilationUnit = parent->isCompilationUnit;
	b->chunks = chunks;
	nb_init(&b->nesting);
	cb_startBlockArgumentNesting(b);
	return b;
}

static Rule *cb_rule(ChunkBuilder *b) { return pv_last(&b->rules); }

static Chunk *cb_last(ChunkBuilder *b) { return pv_last(b->chunks); }

static void cb_emitPendingWhitespace(ChunkBuilder *b, bool isDouble, bool mergeEmptySplits);
static void cb_writeText(ChunkBuilder *b, const char *text, size_t n, Chunk *chunk);
static void cb_activateRule(ChunkBuilder *b, Rule *rule);
static Chunk *cb_writeSplit(ChunkBuilder *b, bool isHard, bool isDouble, bool nest, bool space, bool mergeEmptySplits);
static void cb_handleHardSplit(ChunkBuilder *b);
static Chunk *cb_startChunk(ChunkBuilder *b, NestingLevel *nesting, bool isHard, bool isDouble, bool space);

static void cb_write(ChunkBuilder *b, const char *s, size_t n, bool mergeEmptySplits)
{
	cb_emitPendingWhitespace(b, false, mergeEmptySplits);
	cb_writeText(b, s, n, NULL);
	for (int i = 0; i < b->lazyRules.n; i++)
		cb_activateRule(b, b->lazyRules.v[i]);
	b->lazyRules.n = 0;
	nb_commitNesting(&b->nesting);
	b->afterComment = false;
}

static void cb_writeNewline(ChunkBuilder *b, bool isDouble, bool flushLeft, bool nest)
{
	b->pendingNewlines = isDouble ? 2 : 1;
	b->pendingFlushLeft = flushLeft;
	b->pendingNested = nest;
}

static void cb_writeSpace(ChunkBuilder *b) { b->pendingSpace = true; }

static Chunk *cb_split(ChunkBuilder *b, bool nest, bool space)
{
	if (b->preventSplitNesting > 0) {
		b->pendingNewlines = 0;
		b->pendingNested = false;
		if (space)
			b->pendingSpace = true;
		return chunk_dummy();
	}
	if (b->pendingNewlines > 0)
		return chunk_dummy();
	return cb_writeSplit(b, false, false, nest, space, true);
}

static bool cb_isGenericMethodComment(const SourceComment *comment)
{
	return has_prefix(comment->text, "/*<") || has_prefix(comment->text, "/*=");
}

static bool cb_trailingIdentifierChar(const Chunk *chunk)
{
	if (chunk->text.n == 0)
		return false;
	char c = chunk->text.p[chunk->text.n - 1];
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool cb_needsSpaceBeforeComment(const SourceComment *comment, const Chunk *chunk)
{
	if (chunk_textIsEmpty(chunk))
		return false;
	if (comment->type == CommentType_line)
		return true;
	if (cb_isGenericMethodComment(comment) && cb_trailingIdentifierChar(chunk))
		return false;
	return !chunk_textEndsWith(chunk, "(") && !chunk_textEndsWith(chunk, "[") && !chunk_textEndsWith(chunk, "{");
}

static bool cb_needsSpaceAfterComment(ChunkBuilder *b, const SourceComment *comment, const char *token)
{
	if (chunk_textIsEmpty(cb_last(b)))
		return false;
	if (b->pendingNewlines > 0)
		return false;
	if (cb_isGenericMethodComment(comment) && strcmp(token, "(") == 0)
		return false;
	return strcmp(token, ")") != 0 && strcmp(token, "]") != 0 && strcmp(token, "}") != 0 &&
	       strcmp(token, ",") != 0 && strcmp(token, ";") != 0 && strcmp(token, "") != 0;
}

static bool cb_needsBlankLineBeforeComment(ChunkBuilder *b, const SourceComment *comment)
{
	if (comment->linesBefore < 2)
		return false;
	if (b->chunks->n == 0)
		return false;
	Chunk *last = cb_last(b);
	if (chunk_textEndsWith(last, "{") || chunk_textEndsWith(last, "["))
		return false;
	return true;
}

static Chunk *cb_chunkForComment(ChunkBuilder *b, const SourceComment *comment, const char *token)
{
	if (b->chunks->n == 0)
		return NULL;
	if (comment->linesBefore != 0)
		return NULL;
	if (comment->type == CommentType_doc)
		return NULL;
	if (comment->type == CommentType_block)
		return NULL;
	Chunk *chunk = cb_last(b);
	if (chunk_textIsEmpty(chunk) && b->chunks->n > 1 &&
	    (!cb_isGenericMethodComment(comment) || strcmp(token, "(") != 0))
		chunk = b->chunks->v[b->chunks->n - 2];
	if (chunk_textEndsWith(chunk, ",") && comment->type == CommentType_inlineBlock)
		return NULL;
	if (chunk_textEndsWith(chunk, "(") || chunk_textEndsWith(chunk, "[") ||
	    (chunk_textEndsWith(chunk, "{") && !chunk_textEndsWith(chunk, "${")))
		return NULL;
	return chunk;
}

static void cb_writeComments(ChunkBuilder *b, SourceComment *comments, int ncomments, int linesBeforeToken,
			     const char *token)
{
	if (b->pendingNewlines == 2 && comments[0].linesBefore < 2) {
		if (linesBeforeToken > 1) {
			cb_writeNewline(b, false, false, false);
		} else {
			for (int i = 1; i < ncomments; i++) {
				if (comments[i].linesBefore > 1) {
					cb_writeNewline(b, false, false, false);
					break;
				}
			}
		}
	}
	if (linesBeforeToken == 0 && b->pendingNewlines > comments[0].linesBefore) {
		bool all = true;
		for (int i = 0; i < ncomments; i++)
			if (comments[i].type != CommentType_inlineBlock)
				all = false;
		if (all)
			comments[0].linesBefore = b->pendingNewlines;
	}
	for (int i = 0; i < ncomments; i++) {
		SourceComment *comment = &comments[i];
		Chunk *chunk = cb_chunkForComment(b, comment, token);
		if (chunk) {
			b->pendingSpace = cb_needsSpaceBeforeComment(comment, chunk);
			if (b->pendingSpace && chunk != cb_last(b))
				chunk_updateSplit(cb_last(b), -1, false, 1);
		} else {
			if (b->pendingNewlines == 0) {
				if (comment->linesBefore > 0 &&
				    (b->afterComment || comment->type != CommentType_inlineBlock)) {
					cb_writeNewline(b, cb_needsBlankLineBeforeComment(b, comment), comment->flushLeft, true);
				} else if (b->chunks->n > 0) {
					b->pendingSpace = cb_needsSpaceBeforeComment(comment, cb_last(b));
				}
			} else {
				b->pendingFlushLeft = comment->flushLeft;
			}
			cb_emitPendingWhitespace(b, cb_needsBlankLineBeforeComment(b, comment), true);
		}
		cb_writeText(b, comment->text, comment->n, chunk);
		int linesAfter;
		if (i < ncomments - 1) {
			linesAfter = comments[i + 1].linesBefore;
		} else {
			linesAfter = linesBeforeToken;
			if (linesAfter == 0 && memchr(comments[ncomments - 1].text, '\n', comments[ncomments - 1].n))
				linesAfter = 1;
		}
		if (linesAfter > 0)
			cb_writeNewline(b, b->pendingNewlines == 2 || linesAfter > 1, false, true);
	}
	b->pendingSpace = cb_needsSpaceAfterComment(b, &comments[ncomments - 1], token);
	b->afterComment = true;
}

static void cb_unindent(ChunkBuilder *b) { nb_unindent(&b->nesting); }

static void cb_startSpan(ChunkBuilder *b, int cost)
{
	OpenSpan *s = xalloc(sizeof *s);
	s->start = b->chunks->n;
	s->cost = cost;
	pv_push(&b->openSpans, s);
}

static void cb_endSpan(ChunkBuilder *b)
{
	OpenSpan *openSpan = pv_pop(&b->openSpans);
	int end = b->chunks->n;
	if (openSpan->start == end)
		return;
	Span *span = span_new(openSpan->cost);
	for (int i = openSpan->start; i < end; i++) {
		Chunk *chunk = b->chunks->v[i];
		if (!chunk->rule->isHardened)
			pv_push(&chunk->spans, span);
	}
}

static void cb_startRule(ChunkBuilder *b, Rule *rule)
{
	if (!rule)
		rule = rule_new();
	for (int i = 0; i < b->lazyRules.n; i++)
		cb_activateRule(b, b->lazyRules.v[i]);
	b->lazyRules.n = 0;
	cb_activateRule(b, rule);
}

static void cb_activateRule(ChunkBuilder *b, Rule *rule)
{
	for (int i = 0; i < b->rules.n; i++) {
		Rule *outer = b->rules.v[i];
		if (!rule_splitsOnInnerRules(outer))
			continue;
		rule_constrainWhenSplit(rule, outer);
	}
	pv_push(&b->rules, rule);
}

static void cb_startLazyRule(ChunkBuilder *b, Rule *rule)
{
	if (!rule)
		rule = rule_new();
	pv_push(&b->lazyRules, rule);
}

static void cb_endRule(ChunkBuilder *b)
{
	if (b->lazyRules.n > 0)
		b->lazyRules.n--;
	else
		b->rules.n--;
}

static void cb_forceRules(ChunkBuilder *b) { cb_handleHardSplit(b); }

/* nestExpression({indent, now}); indent < 0: default (Indent.expression) */
static void cb_nestExpression(ChunkBuilder *b, int indent, bool now)
{
	nb_nest(&b->nesting, indent < 0 ? Indent_expression : indent);
	if (now)
		nb_commitNesting(&b->nesting);
}

static void cb_unnest(ChunkBuilder *b, bool now)
{
	nb_unnest(&b->nesting);
	if (now)
		nb_commitNesting(&b->nesting);
}

static void cb_endBlockArgumentNesting(ChunkBuilder *b) { pv_pop(&b->blockArgumentNesting); }

static ChunkBuilder *cb_startBlock(ChunkBuilder *b, Chunk *argumentChunk, bool indent, bool space)
{
	Chunk *chunk = block_chunk_new(argumentChunk, pv_last(&b->rules), nb_indentation(&b->nesting),
				       pv_last(&b->blockArgumentNesting), space, b->pendingFlushLeft);
	pv_push(b->chunks, chunk);
	b->pendingFlushLeft = false;
	ChunkBuilder *builder = cb_new_child(b, &chunk->children);
	if (indent)
		cb_indent(builder, Indent_block);
	Rule *rule = rule_hard();
	cb_startRule(builder, rule);
	cb_split(builder, false, space);
	return builder;
}

static void cb_divideChunks(ChunkBuilder *b);

static ChunkBuilder *cb_endBlock(ChunkBuilder *b, bool forceSplit)
{
	cb_divideChunks(b);
	forceSplit |= b->pendingNested;
	if (!forceSplit) {
		int length = 0;
		for (int i = 0; i < b->chunks->n; i++) {
			Chunk *chunk = b->chunks->v[i];
			length += chunk_length(chunk) + chunk_unsplitBlockLength(chunk);
			if (length > b->formatter->pageWidth) {
				forceSplit = true;
				break;
			}
			if (chunk->rule->isHardened && chunk->rule != b->rules.v[0]) {
				forceSplit = true;
				break;
			}
		}
	}
	ChunkBuilder *parent = b->parent;
	if (forceSplit)
		cb_forceRules(parent);
	return parent;
}

static FormatResult *cb_end(ChunkBuilder *b, const char *lineEnding)
{
	cb_divideChunks(b);
	BlockCache *cache = xalloc(sizeof *cache);
	LineWriter *writer = lw_new((Chunk **)b->chunks->v, b->chunks->n, lineEnding, b->formatter->pageWidth, 0, cache);
	return lw_writeLines(writer, b->isCompilationUnit);
}

static void cb_preventSplit(ChunkBuilder *b) { b->preventSplitNesting++; }
static void cb_endPreventSplit(ChunkBuilder *b) { b->preventSplitNesting--; }

static void cb_emitPendingWhitespace(ChunkBuilder *b, bool isDouble, bool mergeEmptySplits)
{
	if (b->pendingNewlines == 0)
		return;
	if (b->pendingNewlines == 2)
		isDouble = true;
	cb_writeSplit(b, true, isDouble, b->pendingNested, false, mergeEmptySplits);
}

static Chunk *cb_writeSplit(ChunkBuilder *b, bool isHard, bool isDouble, bool nest, bool space, bool mergeEmptySplits)
{
	Chunk *chunk;
	if (mergeEmptySplits && b->chunks->n > 0 && chunk_textIsEmpty(cb_last(b))) {
		chunk = cb_last(b);
		if (isDouble) {
			if (b->chunks->n > 1 && chunk_textEndsWith(b->chunks->v[b->chunks->n - 2], "{"))
				isDouble = false;
		}
		rule_harden(chunk->rule);
		chunk_updateSplit(chunk, b->pendingFlushLeft, isDouble, -1);
	} else {
		chunk = cb_startChunk(b, nest ? b->nesting.nesting : nesting_new(), isHard, isDouble, space);
	}
	if (chunk->rule->isHardened)
		cb_handleHardSplit(b);
	b->pendingNewlines = 0;
	b->pendingNested = false;
	return chunk;
}

static void cb_writeText(ChunkBuilder *b, const char *text, size_t n, Chunk *chunk)
{
	if (!chunk) {
		if (b->chunks->n == 0)
			cb_startChunk(b, nesting_new(), true, false, false);
		chunk = cb_last(b);
	}
	if (b->pendingSpace && !chunk_textIsEmpty(chunk))
		chunk_appendText(chunk, " ", 1);
	b->pendingSpace = false;
	chunk_appendText(chunk, text, n);
}

static Chunk *cb_startChunk(ChunkBuilder *b, NestingLevel *nesting, bool isHard, bool isDouble, bool space)
{
	Rule *rule = isHard ? rule_hard() : pv_last(&b->rules);
	Chunk *chunk = chunk_new(rule, nb_indentation(&b->nesting), nesting, space, b->pendingFlushLeft, isDouble);
	pv_push(b->chunks, chunk);
	b->pendingFlushLeft = false;
	return chunk;
}

static bool cb_canDivide(ChunkBuilder *b, Chunk *chunk)
{
	if (chunk == b->chunks->v[0])
		return false;
	if (!chunk->rule->isHardened)
		return false;
	if (nesting_isNested(chunk->nesting))
		return false;
	if (chunk->isBlock)
		return false;
	return true;
}

static void cb_hardenRules(ChunkBuilder *b);

static void cb_divideChunks(ChunkBuilder *b)
{
	cb_hardenRules(b);
	for (int i = 0; i < b->chunks->n; i++) {
		Chunk *chunk = b->chunks->v[i];
		if (!cb_canDivide(b, chunk))
			chunk_preventDivide(chunk);
	}
}

static void cb_handleHardSplit(ChunkBuilder *b)
{
	if (b->rules.n == 0)
		return;
	Rule *last = pv_last(&b->rules);
	if (!rule_splitsOnInnerRules(last))
		return;
	if (ps_add(&b->hardSplitRulesSet, last))
		pv_push(&b->hardSplitRules, last);
}

static void cb_walkConstraints(Rule *rule)
{
	rule_harden(rule);
	for (int i = 0; i < rule->ncons; i++) {
		Rule *other = rule->cons[i].rule;
		if (other == rule)
			continue;
		if (!other->isHardened &&
		    rule_constrain(rule, rule_fullySplitValue(rule), other) == rule_fullySplitValue(other))
			cb_walkConstraints(other);
	}
}

static void cb_hardenRules(ChunkBuilder *b)
{
	if (b->hardSplitRules.n == 0)
		return;
	for (int i = 0; i < b->hardSplitRules.n; i++)
		cb_walkConstraints(b->hardSplitRules.v[i]);
	for (int i = 0; i < b->chunks->n; i++) {
		Chunk *chunk = b->chunks->v[i];
		if (chunk->rule->isHardened)
			chunk->spans.n = 0;
	}
}
