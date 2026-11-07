/* Package text/tabwriter, ported from go1.26.5 src/text/tabwriter/tabwriter.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO1_26_5_TABWRITER_H
#define GO1_26_5_TABWRITER_H

/* Formatting flags */
enum {
	tabwriter_FilterHTML = 1 << 0,
	tabwriter_StripEscape = 1 << 1,
	tabwriter_AlignRight = 1 << 2,
	tabwriter_DiscardEmptyColumns = 1 << 3,
	tabwriter_TabIndent = 1 << 4,
	tabwriter_Debug = 1 << 5,
};

#define tabwriter_Escape '\xff'

typedef struct tabwriter_Writer tabwriter_Writer; /* defined in tabwriter.c */

static tabwriter_Writer *tabwriter_NewWriter(io_Writer output, int minwidth, int tabwidth, int padding, char padchar,
					     unsigned flags);
static void tabwriter_Writer_Write(tabwriter_Writer *b, const char *buf, int n);
static void tabwriter_Writer_Flush(tabwriter_Writer *b);

#endif
