/* Copyright (c) 2015, Gengo, Inc. All rights reserved. BSD-3-Clause (see LICENSE of
 * github.com/grpc-ecosystem/grpc-gateway). */

/* Port of grpc-gateway v2.33.0 utilities/trie.go: NewDoubleArray (HasCommonPrefix is runtime
 * code, not needed by the plugins). */
#include "grpcgateway.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	int *v;
	int n;
} ints;

typedef struct {
	int row, col;
	int left, right;
} node;

static int node_value(node n, const ints *seqs) { return seqs[n.row].v[n.col]; }

static void ensureSize(GwDoubleArray *da, int i)
{
	while (i >= da->n) {
		/* append(da.Base, make([]int, len(da.Base)+1)...) */
		int n = 2 * da->n + 1;
		da->Base = xrealloc(da->Base, sizeof(int) * (size_t)da->n, sizeof(int) * (size_t)n);
		da->Check = xrealloc(da->Check, sizeof(int) * (size_t)da->n, sizeof(int) * (size_t)n);
		da->n = n;
	}
}

/* children of n; *nout receives their number */
static node *children(node n, const ints *seqs, int *nout)
{
	node *result = xalloc(sizeof *result * (size_t)(n.right - n.left + 1));
	int k = 0;
	int lastVal = -1;
	node dummy;
	node *last = &dummy;
	for (int i = n.left; i < n.right; i++) {
		if (lastVal == seqs[i].v[n.col + 1])
			continue;
		lastVal = seqs[i].v[n.col + 1];
		last->right = i;
		result[k] = (node){i, n.col + 1, i, 0};
		last = &result[k++];
	}
	last->right = n.right;
	*nout = k;
	return result;
}

/* Recursion depth: one level per token of the longest sequence (field paths of HTTP bindings,
 * bounded by the request size); an explicit stack keeps it off the C stack. */
typedef struct {
	int pos;
	node n;
} frame;

static void addSeqs(GwDoubleArray *da, const ints *seqs, int pos0, node n0)
{
	Vec stack = {0};
	frame *f0 = xalloc(sizeof *f0);
	*f0 = (frame){pos0, n0};
	vec_push(&stack, f0);
	/* addSeqs recurses into the children in order: a stack of pending frames, pushed in
	 * reverse, visits them in the same (depth-first, in order) sequence. */
	while (stack.n) {
		frame *f = stack.v[--stack.n];
		int pos = f->pos;
		node n = f->n;
		ensureSize(da, pos);

		int nchildren;
		node *ch = children(n, seqs, &nchildren);
		int i;
		for (i = 1;; i++) {
			bool ok = true;
			for (int c = 0; c < nchildren; c++) {
				int j = i + node_value(ch[c], seqs);
				ensureSize(da, j);
				if (da->Check[j] != 0) {
					ok = false;
					break;
				}
			}
			if (ok)
				break;
		}
		da->Base[pos] = i;
		for (int c = 0; c < nchildren; c++)
			da->Check[i + node_value(ch[c], seqs)] = pos + 1;
		int terminator = da->nEncoding;
		for (int c = nchildren - 1; c >= 0; c--) {
			int code = node_value(ch[c], seqs);
			if (code == terminator)
				continue;
			frame *nf = xalloc(sizeof *nf);
			*nf = (frame){i + code, ch[c]};
			vec_push(&stack, nf);
		}
	}
}

static int byLex(const void *a, const void *b)
{
	const ints *si = a, *sj = b;
	int k;
	for (k = 0; k < si->n && k < sj->n; k++) {
		if (si->v[k] < sj->v[k])
			return -1;
		if (si->v[k] > sj->v[k])
			return 1;
	}
	return (k < si->n) - (k < sj->n);
}

/* NewDoubleArray builds a DoubleArray from a set of sequences of strings. */
GwDoubleArray *gw_utilities_NewDoubleArray(const char *const *const *seqs, const int *lens, int nseqs)
{
	GwDoubleArray *da = xalloc(sizeof *da);
	da->nil = true;
	if (nseqs == 0)
		return da;
	da->nil = false;

	/* registerTokens */
	Map enc = {0};
	int ntok = 0;
	for (int i = 0; i < nseqs; i++)
		ntok += lens[i];
	da->Encoding = xalloc(sizeof *da->Encoding * (size_t)(ntok + 1));
	ints *encoded = xalloc(sizeof *encoded * (size_t)nseqs);
	for (int i = 0; i < nseqs; i++) {
		encoded[i].v = xalloc(sizeof(int) * (size_t)(lens[i] + 1));
		for (int k = 0; k < lens[i]; k++) {
			const char *token = seqs[i][k];
			int *code = map_get(&enc, token);
			if (!code) {
				code = xalloc(sizeof *code);
				*code = da->nEncoding;
				da->Encoding[da->nEncoding++] = token;
				map_put(&enc, token, code);
			}
			encoded[i].v[encoded[i].n++] = *code;
		}
	}
	for (int i = 0; i < nseqs; i++)
		encoded[i].v[encoded[i].n++] = da->nEncoding;
	/* sort.Sort(byLex(encoded)): sequences that compare equal are equal, so any sort gives the
	 * same order. */
	qsort(encoded, (size_t)nseqs, sizeof *encoded, byLex);

	node root = {-1, -1, 0, nseqs};
	addSeqs(da, encoded, 0, root);

	for (int i = da->n; i > 0; i--) {
		if (da->Check[i - 1] != 0) {
			da->n = i;
			break;
		}
	}
	return da;
}
