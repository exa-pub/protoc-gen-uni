/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Package text/tabwriter: ported from go1.27.1 src/text/tabwriter/tabwriter.go.
 *
 * The package implements a write filter (tabwriter.Writer) that translates tabbed columns in
 * input into properly aligned text, using the Elastic Tabstops algorithm described at
 * http://nickgravgaard.com/elastictabstops/index.html.
 *
 * not ported: dump (debugging), osError/handlePanic (writes never fail here; Go's panics on
 * impossible states become fail()). Write returns nothing (Go: always len(buf), nil). */
#include "../../internal.h"

/* ----------------------------------------------------------------------------
 * Filter implementation */

/* A cell represents a segment of text terminated by tabs or line breaks. The text itself is
 * stored in a separate buffer; cell only describes the segment's size in bytes, its width in
 * runes, and whether it's an htab ('\t') terminated cell. */
typedef struct {
	int size; /* cell size in bytes */
	int width; /* cell width in runes */
	bool htab; /* true if the cell is terminated by an htab ('\t') */
} tabwriter_cell;

/* []cell */
typedef struct {
	tabwriter_cell *v;
	int n, cap;
} tabwriter_cells;

/* A Writer is a filter that inserts padding around tab-delimited columns in its input to align
 * them in the output.
 *
 * The Writer treats incoming bytes as UTF-8-encoded text consisting of cells terminated by
 * horizontal ('\t') or vertical ('\v') tabs, and newline ('\n') or formfeed ('\f') characters;
 * both newline and formfeed act as line breaks. Tab-terminated cells in contiguous lines
 * constitute a column. The Writer inserts padding as needed to make all cells in a column have
 * the same width. Column cells must be tab-terminated, not tab-separated: non-tab terminated
 * trailing text at the end of a line forms a cell but that cell is not part of an aligned
 * column.
 *
 * If DiscardEmptyColumns is set, empty columns that are terminated entirely by vertical (or
 * "soft") tabs are discarded. If a Writer is configured to filter HTML, HTML tags and entities
 * are passed through (width zero for tags, one for entities). A segment of text may be escaped
 * by bracketing it with Escape characters; the escaped text is passed through unchanged (the
 * Escape characters are stripped with StripEscape) and its width excludes the Escape
 * characters. The formfeed character acts like a newline but it also terminates all columns in
 * the current line (effectively calling Flush). Clients must call Flush when done calling
 * Write. */
struct tabwriter_Writer {
	/* configuration */
	io_Writer output;
	int minwidth;
	int tabwidth;
	int padding;
	char padbytes[8];
	unsigned flags;

	/* current state */
	Buf buf; /* collected text excluding tabs or line breaks */
	int pos; /* buffer position up to which cell.width of incomplete cell has been computed */
	tabwriter_cell cell; /* current incomplete cell; cell.width is up to buf[pos] excluding ignored sections */
	char endChar; /* terminating char of escaped sequence (Escape for escapes, '>', ';' for HTML
			 tags/entities, or 0) */
	tabwriter_cells *lines; /* list of lines; each line is a list of cells */
	int nlines, caplines;
	IntVec widths; /* list of column widths in runes - re-used during formatting */
};

static void tabwriter_cells_push(tabwriter_cells *c, tabwriter_cell x)
{
	if (c->n == c->cap) {
		c->v = go_grow(c->v, &c->cap, 8, sizeof *c->v);
	}
	c->v[c->n++] = x;
}

/* addLine adds a new line. flushed is a hint indicating whether the underlying writer was just
 * flushed. If so, the previous line is not likely to be a good indicator of the new line's
 * cells. */
static void tabwriter_Writer_addLine(tabwriter_Writer *b, bool flushed)
{
	/* Grow slice instead of appending, as that gives us an opportunity to re-use an existing
	 * []cell. */
	int n = b->nlines + 1;
	if (n <= b->caplines) {
		b->nlines = n;
		b->lines[n - 1].n = 0;
	} else {
		b->lines = go_grow(b->lines, &b->caplines, 16, sizeof *b->lines);
		b->nlines = n;
		b->lines[n - 1] = (tabwriter_cells){0};
	}
	if (!flushed) {
		/* The previous line is probably a good indicator of how many cells the current line
		 * will have. If the current line's capacity is smaller than that, abandon it and
		 * make a new one. */
		if (n >= 2) {
			int prev = b->lines[n - 2].n;
			if (prev > b->lines[n - 1].cap) {
				b->lines[n - 1].v = xalloc(sizeof(tabwriter_cell) * (size_t)prev);
				b->lines[n - 1].n = 0;
				b->lines[n - 1].cap = prev;
			}
		}
	}
}

/* Reset the current state. */
static void tabwriter_Writer_reset(tabwriter_Writer *b)
{
	b->buf.n = 0;
	b->pos = 0;
	b->cell = (tabwriter_cell){0};
	b->endChar = 0;
	b->nlines = 0;
	b->widths.n = 0;
	tabwriter_Writer_addLine(b, true);
}

/* Internal representation (current state):
 *
 * - all text written is appended to buf; tabs and line breaks are stripped away
 * - at any given time there is a (possibly empty) incomplete cell at the end (the cell starts
 *   after a tab or line break)
 * - cell.size is the number of bytes belonging to the cell so far
 * - cell.width is text width in runes of that cell from the start of the cell to position pos;
 *   html tags and entities are excluded from this width if html filtering is enabled
 * - the sizes and widths of processed text are kept in the lines list which contains a list of
 *   cells for each line
 * - the widths list is a temporary list with current widths used during formatting; it is
 *   kept in Writer because it's re-used */

/* A Writer must be initialized with a call to Init. The first parameter (output) specifies the
 * filter output. The remaining parameters control the formatting:
 *
 *	minwidth	minimal cell width including any padding
 *	tabwidth	width of tab characters (equivalent number of spaces)
 *	padding		padding added to a cell before computing its width
 *	padchar		ASCII char used for padding; if padchar == '\t', the Writer will assume
 *			that the width of a '\t' in the formatted output is tabwidth, and cells
 *			are left-aligned independent of align_left
 *	flags		formatting control */
static tabwriter_Writer *tabwriter_Writer_Init(tabwriter_Writer *b, io_Writer output, int minwidth, int tabwidth,
					       int padding, char padchar, unsigned flags)
{
	if (minwidth < 0 || tabwidth < 0 || padding < 0)
		fail("negative minwidth, tabwidth, or padding");
	b->output = output;
	b->minwidth = minwidth;
	b->tabwidth = tabwidth;
	b->padding = padding;
	for (int i = 0; i < (int)sizeof b->padbytes; i++)
		b->padbytes[i] = padchar;
	if (padchar == '\t') {
		/* tab padding enforces left-alignment */
		flags &= ~(unsigned)tabwriter_AlignRight;
	}
	b->flags = flags;
	tabwriter_Writer_reset(b);
	return b;
}

static void tabwriter_Writer_write0(tabwriter_Writer *b, const char *buf, int n)
{
	b->output.Write(b->output.self, buf, n);
}

static void tabwriter_Writer_writeN(tabwriter_Writer *b, const char *src, int nsrc, int n)
{
	while (n > nsrc) {
		tabwriter_Writer_write0(b, src, nsrc);
		n -= nsrc;
	}
	tabwriter_Writer_write0(b, src, n);
}

static const char tabwriter_newline[] = "\n";
static const char tabwriter_tabs[] = "\t\t\t\t\t\t\t\t";

static void tabwriter_Writer_writePadding(tabwriter_Writer *b, int textw, int cellw, bool useTabs)
{
	if (b->padbytes[0] == '\t' || useTabs) {
		/* padding is done with tabs */
		if (b->tabwidth == 0)
			return; /* tabs have no width - can't do any padding */
		/* make cellw the smallest multiple of b.tabwidth */
		cellw = (cellw + b->tabwidth - 1) / b->tabwidth * b->tabwidth;
		int n = cellw - textw; /* amount of padding */
		if (n < 0)
			fail("internal error");
		tabwriter_Writer_writeN(b, tabwriter_tabs, 8, (n + b->tabwidth - 1) / b->tabwidth);
		return;
	}

	/* padding is done with non-tab characters */
	tabwriter_Writer_writeN(b, b->padbytes, 8, cellw - textw);
}

static const char tabwriter_vbar[] = "|";

/* &b.buf[pos] (b.buf.p is NULL while nothing was written). */
static const char *tabwriter_Writer_bufAt(tabwriter_Writer *b, int pos) { return b->buf.p ? b->buf.p + pos : ""; }

static int tabwriter_Writer_writeLines(tabwriter_Writer *b, int pos0, int line0, int line1)
{
	int pos = pos0;
	for (int i = line0; i < line1; i++) {
		tabwriter_cells line = b->lines[i];

		/* if TabIndent is set, use tabs to pad leading empty cells */
		bool useTabs = (b->flags & tabwriter_TabIndent) != 0;

		for (int j = 0; j < line.n; j++) {
			tabwriter_cell c = line.v[j];
			if (j > 0 && (b->flags & tabwriter_Debug) != 0) {
				/* indicate column break */
				tabwriter_Writer_write0(b, tabwriter_vbar, 1);
			}

			if (c.size == 0) {
				/* empty cell */
				if (j < b->widths.n)
					tabwriter_Writer_writePadding(b, c.width, b->widths.v[j], useTabs);
			} else {
				/* non-empty cell */
				useTabs = false;
				if ((b->flags & tabwriter_AlignRight) == 0) { /* align left */
					tabwriter_Writer_write0(b, tabwriter_Writer_bufAt(b, pos), c.size);
					pos += c.size;
					if (j < b->widths.n)
						tabwriter_Writer_writePadding(b, c.width, b->widths.v[j], false);
				} else { /* align right */
					if (j < b->widths.n)
						tabwriter_Writer_writePadding(b, c.width, b->widths.v[j], false);
					tabwriter_Writer_write0(b, tabwriter_Writer_bufAt(b, pos), c.size);
					pos += c.size;
				}
			}
		}

		if (i + 1 == b->nlines) {
			/* last buffered line - we don't have a newline, so just write any outstanding
			 * buffered data */
			tabwriter_Writer_write0(b, tabwriter_Writer_bufAt(b, pos), b->cell.size);
			pos += b->cell.size;
		} else {
			/* not the last line - write newline */
			tabwriter_Writer_write0(b, tabwriter_newline, 1);
		}
	}
	return pos;
}

/* Maximum nesting of format (one level per column). Go's stack grows; here a pathological input
 * with this many columns in one block fails instead of overflowing the C stack. */
#define tabwriter_maxColumns 10000

/* Format the text between line0 and line1 (excluding line1); pos is the buffer position
 * corresponding to the beginning of line0. Returns the buffer position corresponding to the
 * beginning of line1. */
static int tabwriter_Writer_format(tabwriter_Writer *b, int pos0, int line0, int line1)
{
	int pos = pos0;
	int column = b->widths.n;
	if (column > tabwriter_maxColumns)
		fail("tabwriter: too many columns (%d)", column);
	for (int this = line0; this < line1; this++) {
		tabwriter_cells *line = &b->lines[this];

		if (column >= line->n - 1)
			continue;
		/* cell exists in this column => this line has more cells than the previous line (the
		 * last cell per line is ignored because cells are tab-terminated; the last cell per
		 * line describes the text before the newline/formfeed and does not belong to a
		 * column) */

		/* print unprinted lines until beginning of block */
		pos = tabwriter_Writer_writeLines(b, pos, line0, this);
		line0 = this;

		/* column block begin */
		int width = b->minwidth; /* minimal column width */
		bool discardable = true; /* true if all cells in this column are empty and "soft" */
		for (; this < line1; this++) {
			line = &b->lines[this];
			if (column >= line->n - 1)
				break;
			/* cell exists in this column */
			tabwriter_cell c = line->v[column];
			/* update width */
			int w = c.width + b->padding;
			if (w > width)
				width = w;
			/* update discardable */
			if (c.width > 0 || c.htab)
				discardable = false;
		}
		/* column block end */

		/* discard empty columns if necessary */
		if (discardable && (b->flags & tabwriter_DiscardEmptyColumns) != 0)
			width = 0;

		/* format and print all columns to the right of this column (we know the widths of
		 * this column and all columns to the left) */
		intvec_push(&b->widths, width); /* push width */
		pos = tabwriter_Writer_format(b, pos, line0, this);
		b->widths.n--; /* pop width */
		line0 = this;
	}

	/* print unprinted lines until end */
	return tabwriter_Writer_writeLines(b, pos, line0, line1);
}

/* Append text to current cell. */
static void tabwriter_Writer_append(tabwriter_Writer *b, const char *text, int n)
{
	buf_add(&b->buf, text, (size_t)n);
	b->cell.size += n;
}

/* Update the cell width. */
static void tabwriter_Writer_updateWidth(tabwriter_Writer *b)
{
	b->cell.width += utf8_RuneCount(tabwriter_Writer_bufAt(b, b->pos), (int)b->buf.n - b->pos);
	b->pos = (int)b->buf.n;
}

/* To escape a text segment, bracket it with Escape characters. For instance, the tab in this
 * string "Ignore this tab: \xff\t\xff" does not terminate a cell and constitutes a single
 * character of width one for formatting purposes. The value 0xff was chosen because it cannot
 * appear in a valid UTF-8 sequence. (tabwriter_Escape is defined in tabwriter.h.) */

/* Start escaped mode. */
static void tabwriter_Writer_startEscape(tabwriter_Writer *b, char ch)
{
	switch (ch) {
	case tabwriter_Escape:
		b->endChar = tabwriter_Escape;
		break;
	case '<':
		b->endChar = '>';
		break;
	case '&':
		b->endChar = ';';
		break;
	}
}

/* Terminate escaped mode. If the escaped text was an HTML tag, its width is assumed to be zero
 * for formatting purposes; if it was an HTML entity, its width is assumed to be one. In all
 * other cases, the width is the unicode width of the text. */
static void tabwriter_Writer_endEscape(tabwriter_Writer *b)
{
	switch (b->endChar) {
	case tabwriter_Escape:
		tabwriter_Writer_updateWidth(b);
		if ((b->flags & tabwriter_StripEscape) == 0)
			b->cell.width -= 2; /* don't count the Escape chars */
		break;
	case '>': /* tag of zero width */
		break;
	case ';':
		b->cell.width++; /* entity, count as one rune */
		break;
	}
	b->pos = (int)b->buf.n;
	b->endChar = 0;
}

/* Terminate the current cell by adding it to the list of cells of the current line. Returns the
 * number of cells in that line. */
static int tabwriter_Writer_terminateCell(tabwriter_Writer *b, bool htab)
{
	b->cell.htab = htab;
	tabwriter_cells *line = &b->lines[b->nlines - 1];
	tabwriter_cells_push(line, b->cell);
	b->cell = (tabwriter_cell){0};
	return line->n;
}

static void tabwriter_Writer_flushNoDefers(tabwriter_Writer *b);

/* Flush should be called after the last call to Write to ensure that any data buffered in the
 * Writer is written to output. Any incomplete escape sequence at the end is considered
 * complete for formatting purposes. */
static void tabwriter_Writer_Flush(tabwriter_Writer *b) { tabwriter_Writer_flushNoDefers(b); }

/* flushNoDefers is like flush, but without a deferred handlePanic call. */
static void tabwriter_Writer_flushNoDefers(tabwriter_Writer *b)
{
	/* add current cell if not empty */
	if (b->cell.size > 0) {
		if (b->endChar != 0) {
			/* inside escape - terminate it even if incomplete */
			tabwriter_Writer_endEscape(b);
		}
		tabwriter_Writer_terminateCell(b, false);
	}

	/* format contents of buffer */
	tabwriter_Writer_format(b, 0, 0, b->nlines);
	tabwriter_Writer_reset(b);
}

static const char tabwriter_hbar[] = "---\n";

/* Write writes buf to the writer b. */
static void tabwriter_Writer_Write(tabwriter_Writer *b, const char *buf, int nbuf)
{
	if (!buf)
		buf = "";
	/* split text into cells */
	int n = 0;
	for (int i = 0; i < nbuf; i++) {
		char ch = buf[i];
		if (b->endChar == 0) {
			/* outside escape */
			switch (ch) {
			case '\t':
			case '\v':
			case '\n':
			case '\f': {
				/* end of cell */
				tabwriter_Writer_append(b, buf + n, i - n);
				tabwriter_Writer_updateWidth(b);
				n = i + 1; /* ch consumed */
				int ncells = tabwriter_Writer_terminateCell(b, ch == '\t');
				if (ch == '\n' || ch == '\f') {
					/* terminate line */
					tabwriter_Writer_addLine(b, ch == '\f');
					if (ch == '\f' || ncells == 1) {
						/* A '\f' always forces a flush. Otherwise, if the previous
						 * line has only one cell which does not have an impact on
						 * the formatting of the following lines (the last cell per
						 * line is ignored by format()), thus we can flush the Writer
						 * contents. */
						tabwriter_Writer_flushNoDefers(b);
						if (ch == '\f' && (b->flags & tabwriter_Debug) != 0) {
							/* indicate section break */
							tabwriter_Writer_write0(b, tabwriter_hbar, 4);
						}
					}
				}
				break;
			}
			case tabwriter_Escape:
				/* start of escaped sequence */
				tabwriter_Writer_append(b, buf + n, i - n);
				tabwriter_Writer_updateWidth(b);
				n = i;
				if (b->flags & tabwriter_StripEscape)
					n++; /* strip Escape */
				tabwriter_Writer_startEscape(b, tabwriter_Escape);
				break;
			case '<':
			case '&':
				/* possibly an html tag/entity */
				if (b->flags & tabwriter_FilterHTML) {
					/* begin of tag/entity */
					tabwriter_Writer_append(b, buf + n, i - n);
					tabwriter_Writer_updateWidth(b);
					n = i;
					tabwriter_Writer_startEscape(b, ch);
				}
				break;
			}
		} else {
			/* inside escape */
			if (ch == b->endChar) {
				/* end of tag/entity */
				int j = i + 1;
				if (ch == tabwriter_Escape && (b->flags & tabwriter_StripEscape) != 0)
					j = i; /* strip Escape */
				tabwriter_Writer_append(b, buf + n, j - n);
				n = i + 1; /* ch consumed */
				tabwriter_Writer_endEscape(b);
			}
		}
	}

	/* append leftover text */
	tabwriter_Writer_append(b, buf + n, nbuf - n);
}

/* NewWriter allocates and initializes a new Writer. The parameters are the same as for the
 * Init function. */
static tabwriter_Writer *tabwriter_NewWriter(io_Writer output, int minwidth, int tabwidth, int padding, char padchar,
					     unsigned flags)
{
	return tabwriter_Writer_Init(xalloc(sizeof(tabwriter_Writer)), output, minwidth, tabwidth, padding, padchar,
				     flags);
}
