/* Internal header of the Go 1.26.5 standard library port (unity build, see go1_26_5.c).
 *
 * Everything below is `static` and visible only inside the single translation unit go1_26_5.c,
 * so another Go version (a copied directory, e.g. go1.27.0) can live in the same binary. The
 * public API is go1_26_5.h.
 *
 * Conventions (keep them so upstream diffs can be ported file by file):
 *   - files mirror Go's src/ layout: go/scanner/scanner.go -> go/scanner/scanner.c;
 *   - a Go identifier X of package pkg becomes pkg_X (types too: ast.Ident -> ast_Ident,
 *     token.Pos -> token_Pos); methods become pkg_Type_Method or pkg_method with the receiver as
 *     the first argument; struct fields keep their Go names;
 *   - Go strings are GoStr (pointer + length, not NUL-terminated); []byte buffers are Buf
 *     (pkg/core/util.h); slices of pointers are Vec; memory comes from the arena (xalloc) and
 *     is never freed. */
#ifndef GO1_26_5_INTERNAL_H
#define GO1_26_5_INTERNAL_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "util.h"

/* Helper libraries (strings, utf8, unicode) may have functions unused by a given build. */
#if defined(__GNUC__)
#define GO_UNUSED __attribute__((unused))
#else
#define GO_UNUSED
#endif
#define GO_HELPER static GO_UNUSED

/* Go rune (int32). */
typedef int32_t rune;

/* io.Writer; writes never fail (output goes to memory). */
typedef struct {
	void (*Write)(void *self, const char *p, int n);
	void *self;
} io_Writer;

#include "strings/strings.h"
#include "unicode/utf8/utf8.h"
#include "unicode/unicode.h"
#include "strconv/quote.h"
#include "go/token/token.h"
#include "go/ast/ast.h"
#include "go/scanner/scanner.h"
#include "go/parser/parser.h"
#include "text/tabwriter/tabwriter.h"
#include "go/doc/comment/comment.h"
#include "go/build/constraint/constraint.h"
#include "go/printer/printer.h"

#endif
