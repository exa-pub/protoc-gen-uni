/* Copyright 2009 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.26.5 src/go/parser/interface.go.
 *
 * This file contains the exported entry points for invoking the parser.
 * not ported: readSource (src is always given), ParseDir, ParseExprFrom, ParseExpr. */
#include "../../internal.h"

/* ParseFile parses the source code of a single Go source file and returns the corresponding
 * ast.File node.
 *
 * The mode parameter controls the amount of source text parsed and other optional parser
 * functionality. Object resolution is not ported: SkipObjectResolution is always in effect.
 *
 * Position information is recorded in the file set fset, which must not be nil.
 *
 * If syntax errors were found, the result is a partial AST (with ast.Bad* nodes representing
 * the fragments of erroneous source code). Multiple errors are returned via *err as the text of
 * a scanner.ErrorList which is sorted by source position. */
static ast_File *parser_ParseFile(token_FileSet *fset, GoStr filename, GoStr src, int mode, GoStr *err)
{
	if (fset == NULL)
		fail("parser.ParseFile: no token.FileSet provided (fset == nil)");

	token_File *file = token_FileSet_AddFile(fset, filename, -1, src.n);

	parser_parser *p = xalloc(sizeof *p);
	ast_File *volatile f = NULL;
	p->stackBase = parser_stackPos();
	if (setjmp(p->bailout) == 0) {
		/* parse source */
		parser_init(p, file, src, mode);
		f = parser_parseFile(p);
	}
	/* deferred part: a bailout (longjmp) lands here with f == NULL.
	 * not ported: bail.msg (only set when bailing out of object resolution) */

	/* set result values */
	ast_File *r = f;
	if (r == NULL) {
		/* source is not a valid Go source file - satisfy ParseFile API and return a valid
		 * (but) empty *ast.File */
		r = AST_NEW(File);
		r->Name = AST_NEW(Ident);
	}

	/* Ensure the start/end are consistent, whether parsing succeeded or not. */
	r->FileStart = token_File_Base(file);
	r->FileEnd = token_File_Base(file) + token_File_Size(file); /* file.End() */

	scanner_ErrorList_Sort(&p->errors);
	/* err = p.errors.Err() */
	*err = p->errors.list.n == 0 ? (GoStr){0} : scanner_ErrorList_Error(&p->errors);
	return r;
}
