/* blib2to3/pytree.py of black 24.3.0: the parts of Base, Node and Leaf that black uses, and the
 * builder API.
 *
 * Copyright 2006 Google, Inc. All Rights Reserved.
 * Licensed to PSF under a Contributor Agreement.
 * (blib2to3 is under the Python Software Foundation License, src/blib2to3/LICENSE.) */

static BkNode *new_leaf(int type, const char *value)
{
	BkNode *l = xalloc(sizeof *l);
	l->type = type;
	l->value = value ? value : "";
	l->prefix = "";
	return l;
}

static void grow_kids(BkNode *p, int extra)
{
	if (p->n + extra <= p->cap)
		return;
	int cap = p->cap ? p->cap * 2 : 4;
	while (cap < p->n + extra)
		cap *= 2;
	p->kids = xrealloc(p->kids, sizeof *p->kids * (size_t)p->cap, sizeof *p->kids * (size_t)cap);
	p->cap = cap;
}

static void renumber(BkNode *p, int from)
{
	for (int i = from; i < p->n; i++)
		p->kids[i]->idx = i;
}

static BkNode *new_node(int type, int n, BkNode *const *kids)
{
	BkNode *p = xalloc(sizeof *p);
	p->type = type;
	grow_kids(p, n);
	for (int i = 0; i < n; i++) {
		p->kids[i] = kids[i];
		kids[i]->parent = p;
		kids[i]->idx = i;
	}
	p->n = n;
	return p;
}

BkNode *bk_leaf(BkType type, const char *value) { return new_leaf((int)type, value); }

BkNode *bk_node(BkType type, int n, BkNode *const *kids)
{
	if (n == 1)
		return kids[0];
	return new_node((int)type, n, kids);
}

BkNode *bk_nodev(BkType type, ...)
{
	BkNode *kids[64];
	int n = 0;
	va_list ap;
	va_start(ap, type);
	for (BkNode *k; (k = va_arg(ap, BkNode *));) {
		if (n == 64)
			fail("bk_nodev: too many children");
		kids[n++] = k;
	}
	va_end(ap);
	return bk_node(type, n, kids);
}

static BkNode *first_leaf(BkNode *n)
{
	while (n && !IS_LEAF(n))
		n = n->n ? n->kids[0] : NULL;
	return n;
}

static BkNode *last_leaf(BkNode *n)
{
	while (n && !IS_LEAF(n))
		n = n->n ? n->kids[n->n - 1] : NULL;
	return n;
}

void bk_set_prefix(BkNode *n, const char *prefix)
{
	BkNode *l = first_leaf(n);
	if (l)
		l->prefix = prefix;
}

static const char *node_prefix(const BkNode *n)
{
	BkNode *l = first_leaf((BkNode *)n);
	return l ? l->prefix : "";
}

static BkNode *prev_sibling(const BkNode *n)
{
	if (!n->parent || n->idx == 0)
		return NULL;
	return n->parent->kids[n->idx - 1];
}

static BkNode *next_sibling(const BkNode *n)
{
	if (!n->parent || n->idx + 1 >= n->parent->n)
		return NULL;
	return n->parent->kids[n->idx + 1];
}

static void insert_child(BkNode *p, int i, BkNode *child)
{
	grow_kids(p, 1);
	memmove(p->kids + i + 1, p->kids + i, sizeof *p->kids * (size_t)(p->n - i));
	p->kids[i] = child;
	p->n++;
	child->parent = p;
	renumber(p, i);
}

static void append_child(BkNode *p, BkNode *child) { insert_child(p, p->n, child); }

static int remove_node(BkNode *n)
{
	BkNode *p = n->parent;
	if (!p)
		return -1;
	int i = n->idx;
	memmove(p->kids + i, p->kids + i + 1, sizeof *p->kids * (size_t)(p->n - i - 1));
	p->n--;
	renumber(p, i);
	n->parent = NULL;
	return i;
}

/* Node.replace(new) with a single replacement. */
static void replace_node(BkNode *old, BkNode *new_)
{
	BkNode *p = old->parent;
	if (new_->parent)
		remove_node(new_);
	p->kids[old->idx] = new_;
	new_->parent = p;
	new_->idx = old->idx;
	old->parent = NULL;
}

static void collect_leaves(BkNode *n, Buf *b)
{
	if (IS_LEAF(n)) {
		buf_add(b, &n, sizeof n);
		return;
	}
	for (int i = 0; i < n->n; i++)
		collect_leaves(n->kids[i], b);
}

static int leaves_of(BkNode *n, BkNode ***out)
{
	Buf b = {0};
	collect_leaves(n, &b);
	*out = (BkNode **)b.p;
	return (int)(b.n / sizeof(BkNode *));
}
