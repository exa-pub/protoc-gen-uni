/* Copyright 2020 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.27.1 src/go/printer/gobuild.go. */
#include "../../internal.h"

static bool printer_isNL(char b) { return b == '\n' || b == '\f'; }

static GoStr printer_lineAt(printer_printer *p, int start);
static GoStr printer_commentTextAt(printer_printer *p, int start);
static void printer_appendLines(Buf *x, GoStr y);

/* slices.Sort for []int (insertion sort: the lists are tiny) */
static void printer_sortInts(int *v, int n)
{
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && v[j] < v[j - 1]; j--) {
			int t = v[j];
			v[j] = v[j - 1];
			v[j - 1] = t;
		}
}

static void printer_fixGoBuildLines(printer_printer *p)
{
	if (p->goBuild.n + p->plusBuild.n == 0)
		return;

	const char *out = p->output.p;
	int nout = (int)p->output.n;

	/* Find latest possible placement of //go:build and // +build comments. That's just after
	 * the last blank line before we find a non-comment. (We'll add another blank line after
	 * our comment block.) When we start dropping // +build comments, we can skip over / * * /
	 * comments too. Note that we are processing tabwriter input, so every comment begins and
	 * ends with a tabwriter.Escape byte. And some newlines have turned into \f bytes. */
	int insert = 0;
	for (int pos = 0;;) {
		/* Skip leading space at beginning of line. */
		bool blank = true;
		while (pos < nout && (out[pos] == ' ' || out[pos] == '\t'))
			pos++;
		/* Skip over // comment if any. */
		if (pos + 3 < nout && out[pos] == tabwriter_Escape && out[pos + 1] == '/' && out[pos + 2] == '/') {
			blank = false;
			while (pos < nout && !printer_isNL(out[pos]))
				pos++;
		}
		/* Skip over \n at end of line. */
		if (pos >= nout || !printer_isNL(out[pos]))
			break;
		pos++;

		if (blank)
			insert = pos;
	}

	/* If there is a //go:build comment before the place we identified, use that point
	 * instead. (Earlier in the file is always fine.) */
	if (p->goBuild.n > 0 && p->goBuild.v[0] < insert)
		insert = p->goBuild.v[0];
	else if (p->plusBuild.n > 0 && p->plusBuild.v[0] < insert)
		insert = p->plusBuild.v[0];

	constraint_Expr *x = NULL;
	switch (p->goBuild.n) {
	case 0:
		/* Synthesize //go:build expression from // +build lines. */
		for (int i = 0; i < p->plusBuild.n; i++) {
			GoStr err = {0};
			constraint_Expr *y = constraint_Parse(printer_commentTextAt(p, p->plusBuild.v[i]), &err);
			if (err.n != 0 || y == NULL) {
				x = NULL;
				break;
			}
			if (x == NULL)
				x = y;
			else
				x = constraint_and(x, y);
		}
		break;
	case 1:
		/* Parse //go:build expression. */
		x = constraint_Parse(printer_commentTextAt(p, p->goBuild.v[0]), NULL);
		break;
	}

	Buf block = {0};
	if (x == NULL) {
		/* Don't have a valid //go:build expression to treat as truth. Bring all the lines
		 * together but leave them alone. Note that these are already tabwriter-escaped. */
		for (int i = 0; i < p->goBuild.n; i++)
			buf_addg(&block, printer_lineAt(p, p->goBuild.v[i]));
		for (int i = 0; i < p->plusBuild.n; i++)
			buf_addg(&block, printer_lineAt(p, p->plusBuild.v[i]));
	} else {
		buf_addc(&block, tabwriter_Escape);
		buf_adds(&block, "//go:build ");
		buf_addg(&block, constraint_Expr_String(x));
		buf_addc(&block, tabwriter_Escape);
		buf_addc(&block, '\n');
		if (p->plusBuild.n > 0) {
			GoStrVec lines;
			GoStr err;
			if (!constraint_PlusBuildLines(x, &lines, &err)) {
				lines = (GoStrVec){0};
				gostrvec_push(&lines, gostr_cat(GOSTR("// +build error: "), err));
			}
			for (int i = 0; i < lines.n; i++) {
				buf_addc(&block, tabwriter_Escape);
				buf_addg(&block, lines.v[i]);
				buf_addc(&block, tabwriter_Escape);
				buf_addc(&block, '\n');
			}
		}
	}
	buf_addc(&block, '\n');

	/* Build sorted list of lines to delete from remainder of output. */
	int ndel = p->goBuild.n + p->plusBuild.n;
	int *toDelete = xalloc(sizeof *toDelete * (size_t)ndel);
	if (p->goBuild.n > 0)
		memcpy(toDelete, p->goBuild.v, sizeof *toDelete * (size_t)p->goBuild.n);
	if (p->plusBuild.n > 0)
		memcpy(toDelete + p->goBuild.n, p->plusBuild.v, sizeof *toDelete * (size_t)p->plusBuild.n);
	printer_sortInts(toDelete, ndel);

	/* Collect output after insertion point, with lines deleted, into after. */
	Buf after = {0};
	int start = insert;
	for (int i = 0; i < ndel; i++) {
		int end = toDelete[i];
		if (end < start)
			continue;
		printer_appendLines(&after, gostr(out + start, end - start));
		start = end + printer_lineAt(p, end).n;
	}
	printer_appendLines(&after, gostr(out + start, nout - start));
	size_t n = after.n;
	if (n >= 2 && printer_isNL(after.p[n - 1]) && printer_isNL(after.p[n - 2]))
		after.n = n - 1;

	Buf result = {0};
	buf_add(&result, out, (size_t)insert);
	buf_add(&result, block.p, block.n);
	buf_add(&result, after.p, after.n);
	p->output = result;
}

/* appendLines is like append(x, y...) but it avoids creating doubled blank lines, which would
 * not be gofmt-standard output. It assumes that only whole blocks of lines are being appended,
 * not line fragments. */
static void printer_appendLines(Buf *x, GoStr y)
{
	if (y.n > 0 && printer_isNL(y.p[0]) && /* y starts in blank line */
	    (x->n == 0 || /* x is empty or ends in blank line */
	     (x->n >= 2 && printer_isNL(x->p[x->n - 1]) && printer_isNL(x->p[x->n - 2]))))
		y = gostr_from(y, 1); /* delete y's leading blank line */
	buf_addg(x, y);
}

static GoStr printer_lineAt(printer_printer *p, int start)
{
	int pos = start;
	int n = (int)p->output.n;
	while (pos < n && !printer_isNL(p->output.p[pos]))
		pos++;
	if (pos < n)
		pos++;
	return gostr(p->output.p + start, pos - start);
}

static GoStr printer_commentTextAt(printer_printer *p, int start)
{
	int n = (int)p->output.n;
	if (start < n && p->output.p[start] == tabwriter_Escape)
		start++;
	int pos = start;
	while (pos < n && p->output.p[pos] != tabwriter_Escape && !printer_isNL(p->output.p[pos]))
		pos++;
	return gostr_dup(gostr(p->output.p + start, pos - start));
}
