// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/scc.h: SCCAnalyzer<DepsGenerator> (Tarjan's strongly
 * connected components over the messages a DepsGenerator returns). The recursive DFS is
 * iterative here (input graphs are untrusted); the components, their order of descriptors (sorted
 * by full name) and their children are the same. */

typedef struct {
	LpScc pub;
	int seen; /* AddChildren's seen set: the generation that saw it */
} SccImpl;

typedef struct {
	SccImpl *scc; /* NULL while on the stack */
	int index, lowlink;
} SccNode;

struct LpSccAnalyzer {
	LpSccDeps deps;
	void *arg;
	Map nodes; /* full name -> SccNode* (cache_) */
	Vec stack; /* const LpMessage* (stack_) */
	int index; /* index_ */
	int generation;
};

LpSccAnalyzer *lp_scc_analyzer_new(LpSccDeps deps, void *arg)
{
	LpSccAnalyzer *a = xalloc(sizeof *a);
	a->deps = deps;
	a->arg = arg;
	return a;
}

static int scc_compare(const void *x, const void *y)
{
	return strcmp((*(const LpMessage *const *)x)->full_name, (*(const LpMessage *const *)y)->full_name);
}

static Vec scc_deps(LpSccAnalyzer *a, const LpMessage *desc)
{
	Vec deps = {0};
	a->deps(desc, &deps, a->arg);
	for (int i = 0; i < deps.n; i++)
		if (!deps.v[i])
			fail("Check failed: dep");
	return deps;
}

/* AddChildren */
static void scc_add_children(LpSccAnalyzer *a, SccImpl *scc)
{
	Vec children = {0};
	int gen = ++a->generation;
	for (int i = 0; i < scc->pub.n; i++) {
		Vec deps = scc_deps(a, scc->pub.descriptors[i]);
		for (int j = 0; j < deps.n; j++) {
			SccImpl *child = (SccImpl *)lp_scc_get(a, deps.v[j]);
			if (child == scc || child->seen == gen)
				continue;
			child->seen = gen;
			vec_push(&children, child);
		}
	}
	scc->pub.children = (const LpScc **)children.v;
	scc->pub.n_children = children.n;
}

typedef struct {
	const LpMessage *desc;
	SccNode *node;
	Vec deps;
	int next;
} SccFrame;

/* DFS (Tarjan) */
static void scc_dfs(LpSccAnalyzer *a, const LpMessage *root)
{
	SccFrame *frames = NULL;
	int n = 0, cap = 0;
	const LpMessage *start = root;
	for (;;) {
		if (start) {
			SccNode *node = xalloc(sizeof *node);
			map_put(&a->nodes, start->full_name, node);
			node->index = node->lowlink = a->index++;
			vec_push(&a->stack, (void *)start);
			if (n == cap) {
				int c = cap ? 2 * cap : 16;
				frames = xrealloc(frames, sizeof *frames * (size_t)cap, sizeof *frames * (size_t)c);
				cap = c;
			}
			frames[n++] = (SccFrame){start, node, scc_deps(a, start), 0};
			start = NULL;
		}
		SccFrame *f = &frames[n - 1];
		if (f->next < f->deps.n) {
			const LpMessage *dep = f->deps.v[f->next++];
			SccNode *child = map_get(&a->nodes, dep->full_name);
			if (!child)
				start = dep; /* unexplored node */
			else if (!child->scc && child->index < f->node->lowlink)
				f->node->lowlink = child->index; /* a back edge */
			continue;
		}
		SccNode *node = f->node;
		if (node->index == node->lowlink) {
			/* the root of a strongly connected component */
			SccImpl *scc = xalloc(sizeof *scc);
			Vec descs = {0};
			for (;;) {
				const LpMessage *d = a->stack.v[--a->stack.n];
				vec_push(&descs, (void *)d);
				((SccNode *)map_get(&a->nodes, d->full_name))->scc = scc;
				if (d == f->desc)
					break;
			}
			qsort(descs.v, (size_t)descs.n, sizeof *descs.v, scc_compare);
			scc->pub.descriptors = (const LpMessage **)descs.v;
			scc->pub.n = descs.n;
			scc_add_children(a, scc);
		}
		n--;
		if (!n)
			return;
		if (node->lowlink < frames[n - 1].node->lowlink)
			frames[n - 1].node->lowlink = node->lowlink;
	}
}

const LpScc *lp_scc_get(LpSccAnalyzer *a, const LpMessage *desc)
{
	SccNode *node = map_get(&a->nodes, desc->full_name);
	if (!node) {
		scc_dfs(a, desc);
		node = map_get(&a->nodes, desc->full_name);
	}
	if (!node->scc)
		fail("Check failed: SCC of %s", desc->full_name);
	return &node->scc->pub;
}
