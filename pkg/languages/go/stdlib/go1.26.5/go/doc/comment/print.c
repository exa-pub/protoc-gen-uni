/* Package go/doc/comment, ported from go1.26.5 src/go/doc/comment/print.go (Printer.Comment).
 * Copyright 2022 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#include "../../../internal.h"

/* not ported: Printer.headingLevel, Printer.headingID, Printer.docLinkURL, DocLink.DefaultURL,
 * Heading.DefaultID (used only by the HTML, Markdown and Text printers, which are not ported
 * either: go/printer only calls Comment). */

typedef struct {
	comment_Printer *Printer;
} comment_commentPrinter;

static bool comment_blankBefore(comment_Block *x);
static void comment_commentPrinter_block(comment_commentPrinter *p, Buf *out, comment_Block *x);
static void comment_commentPrinter_text(comment_commentPrinter *p, Buf *out, GoStr indent, Vec x);
static void comment_commentPrinter_indent(comment_commentPrinter *p, Buf *out, GoStr indent, GoStr s);

/* Comment returns the standard Go formatting of the Doc, without any comment markers. */
static GoStr comment_Printer_Comment(comment_Printer *p, comment_Doc *d)
{
	comment_commentPrinter cp = {.Printer = p};
	Buf out = {0};
	for (int i = 0; i < d->Content.n; i++) {
		comment_Block *x = d->Content.v[i];
		if (i > 0 && comment_blankBefore(x))
			buf_adds(&out, "\n");
		comment_commentPrinter_block(&cp, &out, x);
	}

	/* Print one block containing all the link definitions that were used, and then a second
	 * block containing all the unused ones. This makes it easy to clean up the unused ones:
	 * gofmt and delete the final block. */
	for (int i = 0; i < 2; i++) {
		bool used = i == 0;
		bool first = true;
		for (int j = 0; j < d->Links.n; j++) {
			comment_LinkDef *def = d->Links.v[j];
			if (def->Used == used) {
				if (first) {
					buf_adds(&out, "\n");
					first = false;
				}
				buf_adds(&out, "[");
				buf_addg(&out, def->Text);
				buf_adds(&out, "]: ");
				buf_addg(&out, def->URL);
				buf_adds(&out, "\n");
			}
		}
	}

	return out.n ? gostr_buf(&out) : GOSTR(""); /* never a NULL pointer */
}

/* blankBefore reports whether the block x requires a blank line before it. All blocks do,
 * except for Lists that return false from x.BlankBefore(). */
static bool comment_blankBefore(comment_Block *x)
{
	if (x->kind == comment_KindList)
		return comment_List_BlankBefore((comment_List *)x);
	return true;
}

/* block prints the block x to out. */
static void comment_commentPrinter_block(comment_commentPrinter *p, Buf *out, comment_Block *x)
{
	switch (x->kind) {
	default:
		fail("go/doc/comment: unknown block kind %d", (int)x->kind); /* Go: fmt.Fprintf(out, "?%T", x) */

	case comment_KindParagraph:
		comment_commentPrinter_text(p, out, GOSTR(""), ((comment_Paragraph *)x)->Text);
		buf_adds(out, "\n");
		break;

	case comment_KindHeading:
		buf_adds(out, "# ");
		comment_commentPrinter_text(p, out, GOSTR(""), ((comment_Heading *)x)->Text);
		buf_adds(out, "\n");
		break;

	case comment_KindCode: {
		GoStr md = ((comment_Code *)x)->Text;
		while (md.n != 0) {
			GoStr line;
			strings_Cut(md, GOSTR("\n"), &line, &md);
			if (line.n != 0) {
				buf_adds(out, "\t");
				buf_addg(out, line);
			}
			buf_adds(out, "\n");
		}
		break;
	}

	case comment_KindList: {
		comment_List *list = (comment_List *)x;
		bool loose = comment_List_BlankBetween(list);
		for (int i = 0; i < list->Items.n; i++) {
			comment_ListItem *item = VEC_AT(list->Items, comment_ListItem, i);
			if (i > 0 && loose)
				buf_adds(out, "\n");
			buf_adds(out, " ");
			if (item->Number.n == 0) {
				buf_adds(out, " - ");
			} else {
				buf_addg(out, item->Number);
				buf_adds(out, ". ");
			}
			for (int j = 0; j < item->Content.n; j++) {
				comment_Block *blk = item->Content.v[j];
				static const GoStr fourSpace = GOSTR("    ");
				if (j > 0) {
					buf_adds(out, "\n");
					buf_addg(out, fourSpace);
				}
				if (blk->kind != comment_KindParagraph)
					fail("interface conversion: comment.Block is not *comment.Paragraph");
				comment_commentPrinter_text(p, out, fourSpace, ((comment_Paragraph *)blk)->Text);
				buf_adds(out, "\n");
			}
		}
		break;
	}
	}
}

/* text prints the text sequence x to out. */
static void comment_commentPrinter_text(comment_commentPrinter *p, Buf *out, GoStr indent, Vec x)
{
	for (int i = 0; i < x.n; i++) {
		comment_Text *t = x.v[i];
		switch (t->kind) {
		case comment_KindPlain:
			comment_commentPrinter_indent(p, out, indent, ((comment_Plain *)t)->s);
			break;
		case comment_KindItalic:
			comment_commentPrinter_indent(p, out, indent, ((comment_Italic *)t)->s);
			break;
		case comment_KindLink: {
			comment_Link *l = (comment_Link *)t;
			if (l->Auto) {
				comment_commentPrinter_text(p, out, indent, l->Text);
			} else {
				buf_adds(out, "[");
				comment_commentPrinter_text(p, out, indent, l->Text);
				buf_adds(out, "]");
			}
			break;
		}
		case comment_KindDocLink:
			buf_adds(out, "[");
			comment_commentPrinter_text(p, out, indent, ((comment_DocLink *)t)->Text);
			buf_adds(out, "]");
			break;
		}
	}
}

/* indent prints s to out, indenting with the indent string after each newline in s. */
static void comment_commentPrinter_indent(comment_commentPrinter *p, Buf *out, GoStr indent, GoStr s)
{
	while (s.n != 0) {
		GoStr line, rest;
		bool ok = strings_Cut(s, GOSTR("\n"), &line, &rest);
		buf_addg(out, line);
		if (ok) {
			buf_adds(out, "\n");
			buf_addg(out, indent);
		}
		s = rest;
	}
}
