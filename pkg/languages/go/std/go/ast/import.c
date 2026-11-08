/* Package go/ast, ported from go1.27.1 src/go/ast/import.go: SortImports (used by go/format.Source).
 * Copyright 2011 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../internal.h"

/* token.File.MergeLine (go/token/position.go) */
static void token_File_MergeLine(token_File *f, int line)
{
	if (line < 1)
		fail("invalid line number %d (should be >= 1)", line);
	if (line >= f->lines.n)
		fail("invalid line number %d (should be < %d)", line, f->lines.n);
	memmove(f->lines.v + line, f->lines.v + line + 1, sizeof *f->lines.v * (size_t)(f->lines.n - line - 1));
	f->lines.n--;
}

static int ast_lineAt(token_FileSet *fset, token_Pos pos) { return token_FileSet_PositionFor(fset, pos, false).Line; }

static GoStr ast_importPath(const ast_Spec *s)
{
	GoStr t;
	if (strconv_Unquote(((const ast_ImportSpec *)s)->Path->Value, &t))
		return t;
	return (GoStr){"", 0};
}

static GoStr ast_importName(const ast_Spec *s)
{
	const ast_Ident *n = ((const ast_ImportSpec *)s)->Name;
	if (!n)
		return (GoStr){"", 0};
	return n->Name;
}

static GoStr ast_importComment(const ast_Spec *s)
{
	const ast_CommentGroup *c = ((const ast_ImportSpec *)s)->Comment;
	if (!c)
		return (GoStr){"", 0};
	return ast_CommentGroup_Text(c);
}

/* collapse indicates whether prev may be removed, leaving only next. */
static bool ast_collapse(const ast_Spec *prev, const ast_Spec *next)
{
	if (!gostr_eq(ast_importPath(next), ast_importPath(prev)) || !gostr_eq(ast_importName(next), ast_importName(prev)))
		return false;
	return ((const ast_ImportSpec *)prev)->Comment == NULL;
}

typedef struct {
	token_Pos Start, End;
} ast_posSpan;

typedef struct {
	bool left; /* true if comment is to the left of the spec, false otherwise. */
	ast_CommentGroup *cg;
} ast_cgPos;

/* A spec and its entry of the importComments map. */
typedef struct {
	ast_Spec *spec;
	ast_cgPos *cg;
	int ncg, capcg;
} ast_specComments;

static bool ast_specLess(const void *a, const void *b, void *ctx)
{
	(void)ctx;
	const ast_Spec *x = ((const ast_specComments *)a)->spec, *y = ((const ast_specComments *)b)->spec;
	int r = gostr_cmp(ast_importPath(x), ast_importPath(y));
	if (r != 0)
		return r < 0;
	r = gostr_cmp(ast_importName(x), ast_importName(y));
	if (r != 0)
		return r < 0;
	return gostr_cmp(ast_importComment(x), ast_importComment(y)) < 0;
}

static bool ast_commentLess(const void *a, const void *b, void *ctx)
{
	(void)ctx;
	return ast_Pos(a) < ast_Pos(b);
}

/* sortSpecs sorts specs[0:n] in place and returns the new length (duplicates removed). */
static int ast_sortSpecs(token_FileSet *fset, ast_File *f, ast_GenDecl *d, ast_Spec **specs, int n)
{
	/* Can't short-circuit here even if specs are already sorted, since they might yet need
	 * deduplication. A lone import, however, may be safely ignored. */
	if (n <= 1)
		return n;

	/* Record positions for specs. */
	ast_posSpan *pos = xalloc(sizeof *pos * (size_t)n);
	for (int i = 0; i < n; i++)
		pos[i] = (ast_posSpan){ast_Pos(specs[i]), ast_End(specs[i])};

	/* Identify comments in this range. */
	token_Pos begSpecs = pos[0].Start;
	token_Pos endSpecs = pos[n - 1].End;
	token_Pos beg = token_File_LineStart(token_FileSet_File(fset, begSpecs), ast_lineAt(fset, begSpecs));
	int endLine = ast_lineAt(fset, endSpecs);
	token_File *endFile = token_FileSet_File(fset, endSpecs);
	token_Pos end;
	if (endLine == token_File_LineCount(endFile))
		end = endSpecs;
	else
		end = token_File_LineStart(endFile, endLine + 1); /* beginning of next line */
	int first = f->Comments.n, last = -1;
	for (int i = 0; i < f->Comments.n; i++) {
		ast_CommentGroup *g = f->Comments.v[i];
		if (ast_End(g) >= end)
			break;
		if (beg <= ast_Pos(g)) {
			/* comment is within the range [beg, end[ of import declarations */
			if (i < first)
				first = i;
			if (i > last)
				last = i;
		}
	}

	void **comments = NULL;
	int ncomments = 0;
	if (last >= 0) {
		comments = f->Comments.v + first;
		ncomments = last + 1 - first;
	}

	/* Assign each comment to the import spec on the same line. */
	ast_specComments *sc = xalloc(sizeof *sc * (size_t)n);
	void **order = xalloc(sizeof *order * (size_t)n);
	for (int i = 0; i < n; i++) {
		sc[i].spec = specs[i];
		order[i] = &sc[i];
	}
	int specIndex = 0;
	for (int k = 0; k < ncomments; k++) {
		ast_CommentGroup *g = comments[k];
		while (specIndex + 1 < n && pos[specIndex + 1].Start <= ast_Pos(g))
			specIndex++;
		bool left = false;
		/* A block comment can appear before the first import spec. */
		if (specIndex == 0 && pos[specIndex].Start > ast_Pos(g)) {
			left = true;
		} else if (specIndex + 1 < n && /* Or it can appear on the left of an import spec. */
			   ast_lineAt(fset, pos[specIndex].Start) + 1 == ast_lineAt(fset, ast_Pos(g))) {
			specIndex++;
			left = true;
		}
		ast_specComments *s = &sc[specIndex];
		if (s->ncg == s->capcg)
			s->cg = go_grow(s->cg, &s->capcg, 2, sizeof *s->cg);
		s->cg[s->ncg++] = (ast_cgPos){left, g};
	}

	/* Sort the import specs by import path. Remove duplicates, when possible without data loss.
	 * Reassign the import paths to have the same position sequence. Reassign each comment to the
	 * spec on the same line. Sort the comments by new position. */
	go_std_sort_slice(order, (size_t)n, ast_specLess, NULL);

	/* Dedup. Thanks to our sorting, we can just consider adjacent pairs of imports. */
	int ndeduped = 0;
	for (int i = 0; i < n; i++) {
		ast_specComments *s = order[i];
		if (i == n - 1 || !ast_collapse(s->spec, ((ast_specComments *)order[i + 1])->spec)) {
			order[ndeduped++] = s;
		} else {
			token_Pos p = ast_Pos(s->spec);
			/* This function is exited early when len(specs) <= 1, so d.Rparen must be
			 * populated (d.Rparen.IsValid() == true). */
			int l = ast_lineAt(fset, p);
			if (l != ast_lineAt(fset, d->Rparen))
				token_File_MergeLine(token_FileSet_File(fset, p), l);
		}
	}

	/* Fix up comment positions */
	for (int i = 0; i < ndeduped; i++) {
		ast_specComments *e = order[i];
		ast_ImportSpec *s = (ast_ImportSpec *)e->spec;
		specs[i] = e->spec;
		if (s->Name)
			s->Name->NamePos = pos[i].Start;
		/* updateBasicLitPos: lit.Pos is updated, lit.End displaced by the same amount
		 * (go.dev/issue/76395). */
		token_Pos len = ast_End(s->Path) - ast_Pos(s->Path);
		s->Path->ValuePos = pos[i].Start;
		if (token_Pos_IsValid(s->Path->ValueEnd))
			s->Path->ValueEnd = pos[i].Start + len;
		s->EndPos = pos[i].End;
		for (int k = 0; k < e->ncg; k++) {
			ast_cgPos g = e->cg[k];
			for (int j = 0; j < g.cg->List.n; j++) {
				ast_Comment *c = g.cg->List.v[j];
				if (g.left) {
					c->Slash = pos[i].Start - 1;
				} else {
					/* An import spec can have both block comment and a line comment to its
					 * right. In that case, both of them will have the same pos. But while
					 * formatting the AST, the line comment gets moved to after the block
					 * comment. */
					c->Slash = pos[i].End;
				}
			}
		}
	}

	if (ncomments > 0)
		go_std_sort_slice(comments, (size_t)ncomments, ast_commentLess, NULL);

	return ndeduped;
}

/* SortImports sorts runs of consecutive import lines in import blocks in f. It also removes
 * duplicate imports when it is possible to do so without data loss. */
GO_HELPER void ast_SortImports(token_FileSet *fset, ast_File *f)
{
	for (int k = 0; k < f->Decls.n; k++) {
		ast_Decl *decl = f->Decls.v[k];
		if (decl->kind != AST_GenDecl || ((ast_GenDecl *)decl)->Tok != token_IMPORT)
			/* Not an import declaration, so we're done. Imports are always first. */
			break;
		ast_GenDecl *d = (ast_GenDecl *)decl;

		if (!token_Pos_IsValid(d->Lparen))
			/* Not a block: sorted by default. */
			continue;

		/* Identify and sort runs of specs on successive lines. */
		ast_Spec **all = (ast_Spec **)d->Specs.v;
		int nall = d->Specs.n;
		ast_Spec **orig = xalloc(sizeof *orig * (size_t)(nall ? nall : 1));
		if (nall)
			memcpy(orig, all, sizeof *orig * (size_t)nall);
		int nspecs = 0, i = 0;
		for (int j = 0; j < nall; j++) {
			if (j > i && ast_lineAt(fset, ast_Pos(orig[j])) > 1 + ast_lineAt(fset, ast_End(orig[j - 1]))) {
				/* j begins a new run. End this one. */
				int m = ast_sortSpecs(fset, f, d, orig + i, j - i);
				if (m)
					memmove(all + nspecs, orig + i, sizeof *all * (size_t)m);
				nspecs += m;
				i = j;
			}
		}
		int m = ast_sortSpecs(fset, f, d, orig + i, nall - i);
		if (m)
			memmove(all + nspecs, orig + i, sizeof *all * (size_t)m);
		nspecs += m;
		d->Specs.n = nspecs;

		/* Deduping can leave a blank line before the rparen; clean that up. */
		if (d->Specs.n > 0) {
			ast_Spec *lastSpec = d->Specs.v[d->Specs.n - 1];
			int lastLine = ast_lineAt(fset, ast_Pos(lastSpec));
			int rParenLine = ast_lineAt(fset, d->Rparen);
			while (rParenLine > lastLine + 1) {
				rParenLine--;
				token_File_MergeLine(token_FileSet_File(fset, d->Rparen), rParenLine);
			}
		}
	}

	/* Make File.Imports order consistent. */
	f->Imports.n = 0;
	for (int k = 0; k < f->Decls.n; k++) {
		ast_Decl *decl = f->Decls.v[k];
		if (decl->kind == AST_GenDecl && ((ast_GenDecl *)decl)->Tok == token_IMPORT) {
			ast_GenDecl *d = (ast_GenDecl *)decl;
			for (int j = 0; j < d->Specs.n; j++)
				vec_push(&f->Imports, d->Specs.v[j]);
		}
	}
}
