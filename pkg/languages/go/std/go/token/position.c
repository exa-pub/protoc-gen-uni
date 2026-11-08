/* Package go/token, ported from go1.27.1 src/go/token/position.go. The FileSet keeps its files
 * in a sorted vector instead of a tree; there is no locking.
 * Copyright 2010 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../internal.h"

GO_HELPER GoStr token_Position_String(token_Position pos)
{
	Buf b = {0};
	buf_addg(&b, pos.Filename);
	if (token_Position_IsValid(&pos)) {
		if (pos.Filename.n)
			buf_addc(&b, ':');
		buf_fmt(&b, "%d", pos.Line);
		if (pos.Column != 0)
			buf_fmt(&b, ":%d", pos.Column);
	}
	if (b.n == 0)
		buf_addc(&b, '-');
	return gostr_buf(&b);
}

GO_HELPER int token_File_Base(token_File *f) { return f->base; }
GO_HELPER int token_File_Size(token_File *f) { return f->size; }
GO_HELPER int token_File_LineCount(token_File *f) { return f->lines.n; }

GO_HELPER void token_File_AddLine(token_File *f, int offset)
{
	int i = f->lines.n;
	if ((i == 0 || f->lines.v[i - 1] < offset) && offset < f->size)
		intvec_push(&f->lines, offset);
}

GO_HELPER void token_File_AddLineColumnInfo(token_File *f, int offset, GoStr filename, int line, int column)
{
	int i = f->ninfos;
	if ((i == 0 || f->infos[i - 1].Offset < offset) && offset < f->size) {
		if (f->ninfos == f->capinfos) {
			f->infos = go_grow(f->infos, &f->capinfos, 4, sizeof *f->infos);
		}
		f->infos[f->ninfos++] = (token_lineInfo){offset, filename, line, column};
	}
}

/* fixOffset fixes an out-of-bounds offset such that 0 <= offset <= f.size. */
static int token_File_fixOffset(token_File *f, int offset)
{
	if (offset < 0)
		return 0;
	if (offset > f->size)
		return f->size;
	return offset;
}

GO_HELPER token_Pos token_File_Pos(token_File *f, int offset) { return f->base + token_File_fixOffset(f, offset); }
GO_HELPER int token_File_Offset(token_File *f, token_Pos p) { return token_File_fixOffset(f, p - f->base); }

/* LineStart returns the Pos value of the start of the given line (1-based). */
GO_HELPER token_Pos token_File_LineStart(token_File *f, int line)
{
	if (line < 1)
		fail("go/token: invalid line number %d (should be >= 1)", line);
	if (line > f->lines.n)
		fail("go/token: invalid line number %d (should be < %d)", line, f->lines.n + 1);
	return f->base + f->lines.v[line - 1];
}

static int token_searchInts(const int *a, int n, int x)
{
	int i = 0, j = n;
	while (i < j) {
		int h = (int)((unsigned)(i + j) >> 1);
		if (a[h] <= x)
			i = h + 1;
		else
			j = h;
	}
	return i - 1;
}

static int token_searchLineInfos(const token_lineInfo *a, int n, int x)
{
	/* index of the last entry with Offset <= x, or -1 */
	int i = 0, j = n;
	while (i < j) {
		int h = (int)((unsigned)(i + j) >> 1);
		if (a[h].Offset <= x)
			i = h + 1;
		else
			j = h;
	}
	return i - 1;
}

static void token_File_unpack(token_File *f, int offset, bool adjusted, GoStr *filename, int *line, int *column)
{
	*filename = f->name;
	*line = *column = 0;
	int i = token_searchInts(f->lines.v, f->lines.n, offset);
	if (i >= 0) {
		*line = i + 1;
		*column = offset - f->lines.v[i] + 1;
	}
	if (adjusted && f->ninfos > 0) {
		int k = token_searchLineInfos(f->infos, f->ninfos, offset);
		if (k >= 0) {
			token_lineInfo *alt = &f->infos[k];
			*filename = alt->Filename;
			int j = token_searchInts(f->lines.v, f->lines.n, alt->Offset);
			if (j >= 0) {
				int d = *line - (j + 1);
				*line = alt->Line + d;
				if (alt->Column == 0)
					*column = 0;
				else if (d == 0)
					*column = alt->Column + (offset - alt->Offset);
			}
		}
	}
}

static token_Position token_File_position(token_File *f, token_Pos p, bool adjusted)
{
	token_Position pos = {0};
	int offset = token_File_fixOffset(f, p - f->base);
	pos.Offset = offset;
	token_File_unpack(f, offset, adjusted, &pos.Filename, &pos.Line, &pos.Column);
	return pos;
}

GO_HELPER token_Position token_File_PositionFor(token_File *f, token_Pos p, bool adjusted)
{
	token_Position pos = {0};
	if (p != token_NoPos)
		pos = token_File_position(f, p, adjusted);
	return pos;
}

GO_HELPER token_Position token_File_Position(token_File *f, token_Pos p) { return token_File_PositionFor(f, p, true); }
GO_HELPER int token_File_Line(token_File *f, token_Pos p) { return token_File_Position(f, p).Line; }

GO_HELPER token_FileSet *token_NewFileSet(void)
{
	token_FileSet *s = xalloc(sizeof *s);
	s->base = 1; /* 0 == NoPos */
	return s;
}

GO_HELPER int token_FileSet_Base(token_FileSet *s) { return s->base; }

GO_HELPER token_File *token_FileSet_AddFile(token_FileSet *s, GoStr filename, int base, int size)
{
	token_File *f = xalloc(sizeof *f);
	f->name = filename;
	f->size = size;
	intvec_push(&f->lines, 0);
	if (base < 0)
		base = s->base;
	if (base < s->base)
		fail("go/token: invalid base %d (should be >= %d)", base, s->base);
	f->base = base;
	if (size < 0)
		fail("go/token: invalid size %d (should be >= 0)", size);
	base += size + 1; /* +1 because EOF also has a position */
	if (base < 0)
		fail("go/token: token.Pos offset overflow (> 2G of source code in file set)");
	s->base = base;
	vec_push(&s->files, f);
	s->last = f;
	return f;
}

static token_File *token_FileSet_file(token_FileSet *s, token_Pos p)
{
	token_File *f = s->last;
	if (f && f->base <= p && p <= f->base + f->size)
		return f;
	int i = 0, j = s->files.n; /* files are sorted by base and do not overlap */
	while (i < j) {
		int h = (int)((unsigned)(i + j) >> 1);
		f = VEC_AT(s->files, token_File, h);
		if (p < f->base)
			j = h;
		else if (p > f->base + f->size)
			i = h + 1;
		else {
			s->last = f;
			return f;
		}
	}
	return NULL;
}

GO_HELPER token_File *token_FileSet_File(token_FileSet *s, token_Pos p)
{
	return p != token_NoPos ? token_FileSet_file(s, p) : NULL;
}

GO_HELPER token_Position token_FileSet_PositionFor(token_FileSet *s, token_Pos p, bool adjusted)
{
	token_Position pos = {0};
	if (p != token_NoPos) {
		token_File *f = token_FileSet_file(s, p);
		if (f)
			return token_File_position(f, p, adjusted);
	}
	return pos;
}

GO_HELPER token_Position token_FileSet_Position(token_FileSet *s, token_Pos p) { return token_FileSet_PositionFor(s, p, true); }
