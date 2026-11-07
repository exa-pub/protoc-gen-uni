/* Go 1.26.5 standard library port: the formatting that compiler/protogen applies to generated
 * Go code (go/parser + go/printer of Go 1.26.5, the toolchain of the buf.build remote plugin
 * protocolbuffers/go). Everything else in this directory is static (unity build go1_26_5.c),
 * so other Go versions can coexist under their own prefix. */
#ifndef GO1_26_5_H
#define GO1_26_5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util.h"

/* An import added by go1_26_5_format_content: `name "path"` (NUL-terminated strings). */
typedef struct {
	const char *name;
	const char *path;
} go1_26_5_import;

/* Formats generated Go source like protogen's (*GeneratedFile).Content: parses src (with
 * comments), inserts one import block with the given imports (in the given order; protogen sorts
 * them by path) after the package clause or a comment ending on its line, and prints the file with
 * printer.Config{Mode: TabIndent | UseSpaces, Tabwidth: 8}. The output is appended to *out.
 * Returns NULL on success or the parse error (text of go/scanner.ErrorList.Error()). */
const char *go1_26_5_format_content(const char *src, size_t n, const go1_26_5_import *imports, size_t nimports,
				    Buf *out);

/* An identifier of a top-level declaration: symbol as protogen annotates it ("Name" or
 * "Type.Name") and its byte offsets in the source. */
typedef struct {
	const char *symbol;
	int begin, end;
} go1_26_5_ident;

/* Parses src (formatted output) and lists the identifiers that protogen's generatedCodeInfo visits,
 * in its order: type names, their struct fields and interface methods, const/var names, functions
 * and methods (receiver type "T" or "*T"). Returns NULL on success or the parse error. */
const char *go1_26_5_decl_idents(const char *src, size_t n, go1_26_5_ident **out, size_t *nout);

/* strconv.Quote of s[0:n]; the result is NUL-terminated, its length is stored in *out_n if set. */
char *go1_26_5_quote(const char *s, size_t n, size_t *out_n);

/* strconv.FormatFloat(f, fmt, prec, bitSize) for fmt 'e', 'E', 'f', 'g' and 'G' (bitSize 32 or
 * 64); a NUL-terminated arena string. */
char *go1_26_5_format_float(double f, char fmt, int prec, int bit_size);

/* unicode.IsLetter, unicode.IsDigit, unicode.IsUpper */
bool go1_26_5_is_letter(int32_t r);
bool go1_26_5_is_digit(int32_t r);
bool go1_26_5_is_upper(int32_t r);
/* token.Lookup(s).IsKeyword() */
bool go1_26_5_is_keyword(const char *s, size_t n);

/* A top-level type, const or var name of a Go file: tok is "type", "const" or "var", selector
 * whether its type (type specs) or value (value specs; false without a value) is a selector
 * expression (pkg.Name). */
typedef struct {
	const char *tok;
	const char *name;
	bool selector;
} go1_26_5_forward;

/* Parses src (with comments) and lists the type and value specs of its declarations in order,
 * as protoc-gen-go's genImport walks them for public imports. Returns NULL or the parse error. */
const char *go1_26_5_forward_decls(const char *src, size_t n, go1_26_5_forward **out, size_t *nout);

#endif
