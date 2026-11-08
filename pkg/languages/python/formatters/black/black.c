/* black 24.3.0 port: unity build (black.h). Copyright (c) 2018 Łukasz Langa, MIT. Ports
 * black/{linegen,lines,nodes,brackets,comments,strings}.py and the format_str /
 * _format_str_once driver of black/__init__.py; blib2to3 (tokenizer, parser) is not ported:
 * the generator builds the tree. tree.c ports blib2to3/pytree.py (Copyright 2006 Google, Inc.
 * All Rights Reserved. Licensed to PSF under a Contributor Agreement; PSF License). Only the
 * functions of black.h are exported. */
#include "internal.h"

#include "tree.c"
#include "nodes.c"
#include "brackets.c"
#include "strings.c"
#include "comments.c"
#include "lines.c"
#include "linegen.c"
#include "build.c"

/* One run of _format_str_once. */
typedef struct {
	EmptyLineTracker elt;
	LinesBlock **blocks;
	int n, cap;
	Lines out; /* every output line, for the second pass */
} Pass;

static void pass_emit(void *ctx, Line *line)
{
	Pass *p = ctx;
	LinesBlock *block = maybe_empty_lines(&p->elt, line);
	if (p->n == p->cap) {
		int nc = p->cap ? p->cap * 2 : 64;
		p->blocks = xrealloc(p->blocks, sizeof *p->blocks * (size_t)p->cap, sizeof *p->blocks * (size_t)nc);
		p->cap = nc;
	}
	p->blocks[p->n++] = block;
	Lines ls = {0};
	transform_line(line, F_TRAILING_COMMA_IN_CALL | F_TRAILING_COMMA_IN_DEF, &ls);
	for (int i = 0; i < ls.n; i++) {
		line_str_to(ls.v[i], &block->content);
		lines_push(&p->out, ls.v[i]);
	}
}

static char *pass_run(BkNode *root, Pass *p)
{
	memset(p, 0, sizeof *p);
	linegen_run(root, pass_emit, p);
	if (!p->n)
		return "\n"; /* the source always has a newline */
	p->blocks[p->n - 1]->after = 0;
	Buf b = {0};
	for (int i = 0; i < p->n; i++) {
		LinesBlock *bl = p->blocks[i];
		if (bl->form_feed) {
			for (int k = 0; k < bl->before - 1; k++)
				buf_addc(&b, '\n');
			buf_adds(&b, "\f\n");
		} else {
			for (int k = 0; k < bl->before; k++)
				buf_addc(&b, '\n');
		}
		buf_add(&b, bl->content.p, bl->content.n);
		for (int k = 0; k < bl->after; k++)
			buf_addc(&b, '\n');
	}
	return (char *)buf_str(&b);
}

static BkNode *current(BkNode *leaf)
{
	while (leaf->replaced_by)
		leaf = leaf->replaced_by;
	return leaf;
}

static bool is_list_node(int t)
{
	return IN(t, BK_arglist, BK_typedargslist, BK_subscriptlist, BK_import_as_names, BK_testlist_gexp, BK_listmaker,
		  BK_dictsetmaker, BK_exprlist, BK_testlist, BK_varargslist);
}

/* Puts a trailing comma that the first pass added after `prev` into the tree, where blib2to3
 * would put it when it parses the output: at the end of the list inside the brackets that close
 * after prev, or in a new list node around a single element. */
static void insert_trailing_comma(BkNode *prev)
{
	BkNode *n = current(prev);
	for (;;) {
		if (!n->parent)
			fail("black port: cannot place a trailing comma");
		BkNode *s = next_sibling(n);
		if (s) {
			if (IS_LEAF(s) && is_closing(s->type))
				break;
			fail("black port: trailing comma not before a closing bracket");
		}
		n = n->parent;
	}
	BkNode *comma = new_leaf(BK_COMMA, ",");
	if (is_list_node(n->type)) {
		append_child(n, comma);
		return;
	}
	BkNode *owner = n->parent, *open = prev_sibling(n);
	int kind = -1;
	if (open && owner->type == BK_trailer)
		kind = open->type == BK_LSQB ? BK_subscriptlist : BK_arglist;
	else if (owner->type == BK_parameters)
		kind = BK_typedargslist;
	else if (owner->type == BK_import_from)
		kind = BK_import_as_names;
	else if (owner->type == BK_classdef)
		kind = BK_arglist;
	else if (open && owner->type == BK_atom)
		kind = open->type == BK_LPAR ? BK_testlist_gexp : open->type == BK_LSQB ? BK_listmaker : BK_dictsetmaker;
	if (kind < 0)
		fail("black port: trailing comma in an unsupported node");
	int idx = remove_node(n);
	BkNode *kids[2] = {n, comma};
	insert_child(owner, idx, new_node(kind, 2, kids));
}

static void reset_prefixes(BkNode *n)
{
	if (IS_LEAF(n)) {
		n->prefix = "";
		return;
	}
	for (int i = 0; i < n->n; i++)
		reset_prefixes(n->kids[i]);
}

static void newlines(Buf *b, int k)
{
	for (int i = 0; i < k; i++)
		buf_addc(b, '\n');
}

/* Makes the tree the one blib2to3 builds from the output of the first pass: the trailing
 * commas it added become leaves, and the prefixes of the first leaves of the lines hold the
 * blank lines and comments of the output (invisible parentheses that were made visible are
 * made invisible again by the second pass, docstrings keep their normalized value). */
static void prepare_second_pass(BkNode *root, Pass *p)
{
	for (int i = 0; i < p->out.n; i++) {
		Line *l = p->out.v[i];
		for (int j = 1; j < l->n; j++)
			if (l->leaves[j]->added_comma)
				insert_trailing_comma(l->leaves[j - 1]);
	}
	reset_prefixes(root);
	Buf pending = {0};
	BkNode *pending_src = NULL;
	for (int i = 0; i < p->n; i++) {
		LinesBlock *bl = p->blocks[i];
		int blank = bl->before + (i ? p->blocks[i - 1]->after : 0);
		Line *l = bl->original_line;
		if (line_is_comment(l) && l->leaves[0]->src) {
			BkNode *src = current(l->leaves[0]->src);
			if (pending_src && pending_src != src) {
				pending_src->prefix = buf_str(&pending);
				pending = (Buf){0};
			}
			pending_src = src;
			newlines(&pending, blank);
			buf_adds(&pending, l->leaves[0]->value);
			buf_addc(&pending, '\n');
			continue;
		}
		BkNode *first = current(l->leaves[0]);
		if (pending_src && pending_src != first) {
			pending_src->prefix = buf_str(&pending);
			pending = (Buf){0};
			pending_src = NULL;
		}
		newlines(&pending, blank);
		first->prefix = buf_str(&pending);
		pending = (Buf){0};
		pending_src = NULL;
	}
	if (pending_src)
		pending_src->prefix = buf_str(&pending);
}

char *bk_format_str(BkNode *root)
{
	Pass p1, p2;
	pass_run(root, &p1);
	/* format_str runs the second pass only when the output differs from the source; when it
	 * does not, the second pass gives the same output again, so it always runs here. */
	prepare_second_pass(root, &p1);
	return pass_run(root, &p2);
}
