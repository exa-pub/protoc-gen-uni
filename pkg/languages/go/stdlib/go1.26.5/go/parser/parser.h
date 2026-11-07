/* Package go/parser, ported from go1.26.5 src/go/parser/{parser,interface}.go.
 * Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go). */
#ifndef GO1_26_5_PARSER_H
#define GO1_26_5_PARSER_H

/* Mode */
enum {
	parser_PackageClauseOnly = 1 << 0, /* stop parsing after package clause */
	parser_ImportsOnly = 1 << 1, /* stop parsing after import declarations */
	parser_ParseComments = 1 << 2, /* parse comments and add them to AST */
	parser_Trace = 1 << 3, /* print a trace of parsed productions (not ported) */
	parser_DeclarationErrors = 1 << 4, /* report declaration errors */
	parser_SpuriousErrors = 1 << 5, /* same as AllErrors, for backward-compatibility */
	parser_SkipObjectResolution = 1 << 6, /* always on: object resolution is not ported */
	parser_AllErrors = parser_SpuriousErrors, /* report all errors (not just the first 10 on different lines) */
};

/* ParseFile parses the source code of a single Go source file (src) and returns the
 * corresponding ast_File (never NULL; partial when there are syntax errors). *err receives the
 * error (scanner.ErrorList.Error() of the sorted list) or {NULL, 0} on success. */
static ast_File *parser_ParseFile(token_FileSet *fset, GoStr filename, GoStr src, int mode, GoStr *err);

#endif
