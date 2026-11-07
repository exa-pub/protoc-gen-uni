/* Unity build of the Go 1.26.5 standard library port; see internal.h for conventions and
 * go1_26_5.h for the API. */
#include "go1_26_5.h"

#include "internal.h"

#include "strings/strings.c"
#include "unicode/utf8/utf8.c"
#include "unicode/tables.c"
#include "unicode/unicode.c"
#include "strconv/quote.c"
#include "strconv/ftoa.c"
#include "go/token/token.c"
#include "go/token/position.c"
#include "go/ast/ast.c"
#include "go/ast/walk.c"
#include "go/scanner/errors.c"
#include "go/scanner/scanner.c"
#include "go/parser/parser.c"
#include "go/parser/interface.c"
#include "text/tabwriter/tabwriter.c"
#include "go/doc/comment/std.c"
#include "go/doc/comment/parse.c"
#include "go/doc/comment/print.c"
#include "go/build/constraint/expr.c"
#include "go/printer/printer.c"
#include "go/printer/nodes.c"
#include "go/printer/comment.c"
#include "go/printer/gobuild.c"

/* ---- API ---- */

/* Ported from pkg/languages/go/protogen/protogen.go of the former Go implementation (git c9b2e00), (*GeneratedFile).Content (fork of
 * google.golang.org/protobuf/compiler/protogen v1.36.12; Copyright 2019 The Go Authors). */
const char *go1_26_5_format_content(const char *src, size_t n, const go1_26_5_import *imports, size_t nimports,
				    Buf *out)
{
	token_FileSet *fset = token_NewFileSet();
	GoStr err;
	ast_File *file = parser_ParseFile(fset, GOSTR(""), gostr(src, (int)n), parser_ParseComments, &err);
	if (err.p)
		return gostr_cstr(err);

	/* Modify the AST to include a new import block. */
	if (nimports > 0) {
		/* Insert block after package statement or possible comment attached to the end of
		 * the package statement. */
		token_Pos pos = file->Package;
		token_File *tokFile = token_FileSet_File(fset, file->Package);
		int pkgLine = token_File_Line(tokFile, file->Package);
		for (int i = 0; i < file->Comments.n; i++) {
			ast_CommentGroup *c = VEC_AT(file->Comments, ast_CommentGroup, i);
			if (token_File_Line(tokFile, ast_Pos(c)) > pkgLine)
				break;
			pos = ast_End(c);
		}

		/* Construct the import block. */
		ast_GenDecl *impDecl = AST_NEW(GenDecl);
		impDecl->Tok = token_IMPORT;
		impDecl->TokPos = pos;
		impDecl->Lparen = pos;
		impDecl->Rparen = pos;
		for (size_t i = 0; i < nimports; i++) {
			ast_ImportSpec *spec = AST_NEW(ImportSpec);
			spec->Name = AST_NEW(Ident);
			spec->Name->Name = gostr_c(imports[i].name);
			spec->Name->NamePos = pos;
			spec->Path = AST_NEW(BasicLit);
			spec->Path->Kind = token_STRING;
			spec->Path->Value = strconv_Quote(gostr_c(imports[i].path));
			spec->Path->ValuePos = pos;
			spec->EndPos = pos;
			vec_push(&impDecl->Specs, spec);
		}
		Vec decls = {0};
		vec_push(&decls, impDecl);
		for (int i = 0; i < file->Decls.n; i++)
			vec_push(&decls, file->Decls.v[i]);
		file->Decls = decls;
	}

	printer_Config cfg = {.Mode = printer_TabIndent | printer_UseSpaces, .Tabwidth = 8};
	return printer_Config_Fprint(&cfg, out, fset, (ast_Node *)file);
}

typedef struct {
	go1_26_5_ident *v;
	size_t n, cap;
} go1_26_5_identVec;

static void go1_26_5_annotate(go1_26_5_identVec *v, token_FileSet *fset, GoStr s, ast_Ident *ident)
{
	if (v->n == v->cap) {
		size_t cap = v->cap ? 2 * v->cap : 64;
		v->v = xrealloc(v->v, sizeof *v->v * v->cap, sizeof *v->v * cap);
		v->cap = cap;
	}
	v->v[v->n++] = (go1_26_5_ident){
		gostr_cstr(s),
		token_FileSet_Position(fset, ast_Pos(ident)).Offset,
		token_FileSet_Position(fset, ast_End(ident)).Offset,
	};
}

/* Ported from protogen.go, (*GeneratedFile).generatedCodeInfo (the traversal only). */
const char *go1_26_5_decl_idents(const char *src, size_t n, go1_26_5_ident **out, size_t *nout)
{
	token_FileSet *fset = token_NewFileSet();
	GoStr err;
	ast_File *astFile = parser_ParseFile(fset, GOSTR(""), gostr(src, (int)n), 0, &err);
	if (err.p)
		return gostr_cstr(err);
	go1_26_5_identVec v = {0};
	for (int i = 0; i < astFile->Decls.n; i++) {
		ast_Decl *decl = astFile->Decls.v[i];
		if (decl->kind == AST_GenDecl) {
			ast_GenDecl *d = (ast_GenDecl *)decl;
			for (int j = 0; j < d->Specs.n; j++) {
				ast_Spec *spec = d->Specs.v[j];
				if (spec->kind == AST_TypeSpec) {
					ast_TypeSpec *ts = (ast_TypeSpec *)spec;
					go1_26_5_annotate(&v, fset, ts->Name->Name, ts->Name);
					ast_FieldList *fields = NULL;
					if (ts->Type->kind == AST_StructType)
						fields = ((ast_StructType *)ts->Type)->Fields;
					else if (ts->Type->kind == AST_InterfaceType)
						fields = ((ast_InterfaceType *)ts->Type)->Methods;
					for (int k = 0; fields && k < fields->List.n; k++) {
						ast_Field *field = VEC_AT(fields->List, ast_Field, k);
						for (int m = 0; m < field->Names.n; m++) {
							ast_Ident *name = VEC_AT(field->Names, ast_Ident, m);
							GoStr sym = gostr_cat(gostr_cat(ts->Name->Name, GOSTR(".")), name->Name);
							go1_26_5_annotate(&v, fset, sym, name);
						}
					}
				} else if (spec->kind == AST_ValueSpec) {
					ast_ValueSpec *vs = (ast_ValueSpec *)spec;
					for (int k = 0; k < vs->Names.n; k++) {
						ast_Ident *name = VEC_AT(vs->Names, ast_Ident, k);
						go1_26_5_annotate(&v, fset, name->Name, name);
					}
				}
			}
		} else if (decl->kind == AST_FuncDecl) {
			ast_FuncDecl *d = (ast_FuncDecl *)decl;
			if (d->Recv == NULL) {
				go1_26_5_annotate(&v, fset, d->Name->Name, d->Name);
			} else {
				if (d->Recv->List.n == 0) /* Go would panic; cannot happen for parsed methods */
					continue;
				ast_Expr *recv = VEC_AT(d->Recv->List, ast_Field, 0)->Type;
				if (recv->kind == AST_StarExpr)
					recv = ((ast_StarExpr *)recv)->X;
				if (recv->kind == AST_Ident) {
					ast_Ident *id = (ast_Ident *)recv;
					GoStr sym = gostr_cat(gostr_cat(id->Name, GOSTR(".")), d->Name->Name);
					go1_26_5_annotate(&v, fset, sym, d->Name);
				}
			}
		}
	}
	*out = v.v;
	*nout = v.n;
	return NULL;
}

char *go1_26_5_quote(const char *s, size_t n, size_t *out_n)
{
	GoStr q = strconv_Quote(gostr(s, (int)n));
	if (out_n)
		*out_n = (size_t)q.n;
	return gostr_cstr(q);
}

char *go1_26_5_format_float(double f, char fmt, int prec, int bit_size)
{
	Buf b = {0};
	strconv_AppendFloat(&b, f, fmt, prec, bit_size);
	return (char *)buf_str(&b);
}

bool go1_26_5_is_letter(int32_t r) { return unicode_IsLetter(r); }
bool go1_26_5_is_digit(int32_t r) { return unicode_IsDigit(r); }
bool go1_26_5_is_upper(int32_t r) { return unicode_IsUpper(r); }
bool go1_26_5_is_keyword(const char *s, size_t n) { return token_Token_IsKeyword(token_Lookup(gostr(s, (int)n))); }

/* Ported from protoc-gen-go's genImport (cmd/protoc-gen-go/internal_gengo/main.go): the walk
 * over the declarations of a parsed file. */
const char *go1_26_5_forward_decls(const char *src, size_t n, go1_26_5_forward **out, size_t *nout)
{
	token_FileSet *fset = token_NewFileSet();
	GoStr err;
	ast_File *astFile = parser_ParseFile(fset, GOSTR(""), gostr(src, (int)n), parser_ParseComments, &err);
	if (err.p)
		return gostr_cstr(err);
	size_t cap = 16, k = 0;
	go1_26_5_forward *v = xalloc(sizeof *v * cap);
	for (int i = 0; i < astFile->Decls.n; i++) {
		ast_Decl *decl = astFile->Decls.v[i];
		if (decl->kind != AST_GenDecl)
			continue;
		ast_GenDecl *d = (ast_GenDecl *)decl;
		const char *tok = gostr_cstr(token_Token_String(d->Tok));
		for (int j = 0; j < d->Specs.n; j++) {
			ast_Spec *spec = d->Specs.v[j];
			if (spec->kind == AST_TypeSpec) {
				ast_TypeSpec *ts = (ast_TypeSpec *)spec;
				if (k == cap)
					v = xrealloc(v, sizeof *v * cap, sizeof *v * cap * 2), cap *= 2;
				v[k++] = (go1_26_5_forward){tok, gostr_cstr(ts->Name->Name), ts->Type && ts->Type->kind == AST_SelectorExpr};
			} else if (spec->kind == AST_ValueSpec) {
				ast_ValueSpec *vs = (ast_ValueSpec *)spec;
				for (int m = 0; m < vs->Names.n; m++) {
					ast_Expr *e = m < vs->Values.n ? VEC_AT(vs->Values, ast_Expr, m) : NULL;
					if (k == cap)
						v = xrealloc(v, sizeof *v * cap, sizeof *v * cap * 2), cap *= 2;
					v[k++] = (go1_26_5_forward){tok, gostr_cstr(VEC_AT(vs->Names, ast_Ident, m)->Name),
								    e && e->kind == AST_SelectorExpr};
				}
			}
		}
	}
	*out = v;
	*nout = k;
	return NULL;
}
