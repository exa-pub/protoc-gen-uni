/* Port of dart_style 3.1.13 lib/src/short/line_writer.dart (without selections).
 *
 * Copyright (c) 2014, the Dart project authors. Please see the AUTHORS file for details. All
 * rights reserved. Use of this source code is governed by a BSD-style license that can be found
 * in the LICENSE file. */

/* _blockCache: (chunk, column) -> FormatResult */
typedef struct {
	Chunk **chunks;
	int *columns;
	FormatResult **results;
	int cap, n;
} BlockCache;

struct LineWriter {
	Buf buffer;
	Chunk **chunks;
	int nchunks;
	const char *lineEnding;
	int pageWidth;
	int blockIndentation;
	BlockCache *blockCache;
};

static int lw_pageWidth(const LineWriter *w) { return w->pageWidth; }

static uint32_t bc_hash(const Chunk *c, int column)
{
	uintptr_t x = (uintptr_t)c ^ ((uintptr_t)(unsigned)column * 0x9E3779B1u);
	x ^= x >> 15;
	x *= 0x2c1b3c6dU;
	x ^= x >> 12;
	return (uint32_t)x;
}

static FormatResult *bc_get(BlockCache *bc, Chunk *c, int column)
{
	if (!bc->cap)
		return NULL;
	uint32_t mask = (uint32_t)bc->cap - 1;
	for (uint32_t i = bc_hash(c, column) & mask;; i = (i + 1) & mask) {
		if (!bc->chunks[i])
			return NULL;
		if (bc->chunks[i] == c && bc->columns[i] == column)
			return bc->results[i];
	}
}

static void bc_put(BlockCache *bc, Chunk *c, int column, FormatResult *r)
{
	if ((bc->n + 1) * 4 > bc->cap * 3) {
		BlockCache old = *bc;
		int cap = old.cap ? old.cap * 2 : 16;
		bc->chunks = xalloc((size_t)cap * sizeof *bc->chunks);
		bc->columns = xalloc((size_t)cap * sizeof *bc->columns);
		bc->results = xalloc((size_t)cap * sizeof *bc->results);
		bc->cap = cap;
		bc->n = 0;
		for (int i = 0; i < old.cap; i++)
			if (old.chunks[i])
				bc_put(bc, old.chunks[i], old.columns[i], old.results[i]);
	}
	uint32_t mask = (uint32_t)bc->cap - 1;
	for (uint32_t i = bc_hash(c, column) & mask;; i = (i + 1) & mask) {
		if (!bc->chunks[i]) {
			bc->chunks[i] = c;
			bc->columns[i] = column;
			bc->results[i] = r;
			bc->n++;
			return;
		}
		if (bc->chunks[i] == c && bc->columns[i] == column) {
			bc->results[i] = r;
			return;
		}
	}
}

static LineWriter *lw_new(Chunk **chunks, int nchunks, const char *lineEnding, int pageWidth, int blockIndentation,
			  BlockCache *blockCache)
{
	LineWriter *w = xalloc(sizeof *w);
	w->chunks = chunks;
	w->nchunks = nchunks;
	w->lineEnding = lineEnding;
	w->pageWidth = pageWidth;
	w->blockIndentation = blockIndentation;
	w->blockCache = blockCache;
	return w;
}

static FormatResult *lw_writeLines(LineWriter *w, bool isCompilationUnit);

static FormatResult *lw_formatBlock(LineWriter *w, Chunk *chunk, int column)
{
	FormatResult *cached = bc_get(w->blockCache, chunk, column);
	if (cached)
		return cached;
	LineWriter *writer = lw_new((Chunk **)chunk->children.v, chunk->children.n, w->lineEnding, w->pageWidth, column,
				    w->blockCache);
	FormatResult *r = lw_writeLines(writer, false);
	bc_put(w->blockCache, chunk, column, r);
	return r;
}

static void lw_writeChunk(LineWriter *w, Chunk *chunk) { buf_add(&w->buffer, chunk->text.p, chunk->text.n); }

static void lw_writeChunksUnsplit(LineWriter *w, Chunk *block)
{
	for (int i = 0; i < block->children.n; i++) {
		Chunk *chunk = block->children.v[i];
		if (chunk->spaceWhenUnsplit)
			buf_addc(&w->buffer, ' ');
		if (chunk->isBlock)
			lw_writeChunksUnsplit(w, chunk);
		lw_writeChunk(w, chunk);
	}
}

static int lw_completeLine(LineWriter *w, int start, int end)
{
	Chunk **chunks = w->chunks + start;
	int n = end - start;
	LineSplitter sp;
	ls_init(&sp, w, chunks, n, w->blockIndentation);
	SplitSet *splits = ls_apply(&sp);
	for (int i = 0; i < n; i++) {
		Chunk *chunk = chunks[i];
		if (chunk->isBlock) {
			if (!splitset_shouldSplitAt(splits, i)) {
				lw_writeChunksUnsplit(w, chunk);
			} else {
				buf_adds(&w->buffer, w->lineEnding);
				FormatResult *block = lw_formatBlock(w, chunk, splits->columns[i]);
				buf_add(&w->buffer, block->text, block->n);
			}
		}
		if (splitset_shouldSplitAt(splits, i)) {
			if (w->buffer.n > 0) {
				buf_adds(&w->buffer, w->lineEnding);
				if (chunk->isDouble)
					buf_adds(&w->buffer, w->lineEnding);
			}
			for (int k = 0; k < splits->columns[i]; k++)
				buf_addc(&w->buffer, ' ');
		} else {
			if (chunk->spaceWhenUnsplit)
				buf_addc(&w->buffer, ' ');
		}
		lw_writeChunk(w, chunk);
	}
	return splits->cost;
}

static FormatResult *lw_writeLines(LineWriter *w, bool isCompilationUnit)
{
	int totalCost = 0;
	int start = 0;
	for (int i = 0; i < w->nchunks; i++) {
		Chunk *chunk = w->chunks[i];
		if (!chunk->canDivide)
			continue;
		totalCost += lw_completeLine(w, start, i);
		start = i;
	}
	if (start < w->nchunks)
		totalCost += lw_completeLine(w, start, w->nchunks);
	if (isCompilationUnit)
		buf_adds(&w->buffer, w->lineEnding);
	FormatResult *r = xalloc(sizeof *r);
	r->text = buf_str(&w->buffer);
	r->n = w->buffer.n;
	r->cost = totalCost;
	return r;
}
