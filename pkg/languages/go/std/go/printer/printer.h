/* Package go/printer, ported from go1.27.1 src/go/printer (as vendored by the former Go
 * implementation of this project, git c9b2e00, at go1.26.5; go1.26.5 differences: go_std_release).
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO_STD_PRINTER_H
#define GO_STD_PRINTER_H

/* Mode */
enum {
	printer_RawFormat = 1 << 0, /* do not use a tabwriter; if set, UseSpaces is ignored */
	printer_TabIndent = 1 << 1, /* use tabs for indentation independent of UseSpaces */
	printer_UseSpaces = 1 << 2, /* use spaces instead of tabs for alignment */
	printer_SourcePos = 1 << 3, /* emit //line directives to preserve original source positions */
	printer_normalizeNumbers = 1 << 30,
};

typedef struct {
	unsigned Mode; /* default: 0 */
	int Tabwidth; /* default: 8 */
	int Indent; /* default: 0 (all code is indented at least by this much) */
} printer_Config;

/* Fprint "pretty-prints" an AST node (ast_File, or any expression, statement, declaration or
 * spec) to out. Returns NULL or an error message. */
static const char *printer_Config_Fprint(const printer_Config *cfg, Buf *out, token_FileSet *fset, ast_Node *node);

#endif
