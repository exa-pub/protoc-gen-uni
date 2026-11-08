/* Package go/build/constraint (subset used by go/printer), ported from go1.27.1
 * src/go/build/constraint/expr.go. Copyright 2020 The Go Authors. All rights reserved.
 * BSD-3-Clause (see LICENSE in Go). */
#ifndef GO_STD_CONSTRAINT_H
#define GO_STD_CONSTRAINT_H

/* Expr kinds: *TagExpr, *NotExpr, *AndExpr, *OrExpr. */
typedef enum {
	constraint_TagExprKind = 1,
	constraint_NotExprKind,
	constraint_AndExprKind,
	constraint_OrExprKind,
} constraint_ExprKind;

/* An Expr is a build tag constraint expression. */
typedef struct constraint_Expr {
	constraint_ExprKind kind;
	GoStr Tag; /* TagExpr: for example, "linux" or "cgo" */
	struct constraint_Expr *X, *Y; /* NotExpr: X; AndExpr, OrExpr: X, Y */
} constraint_Expr;

/* String returns the string form of the expression, using the boolean syntax used in
 * //go:build lines. */
static GoStr constraint_Expr_String(const constraint_Expr *x);
static constraint_Expr *constraint_and(constraint_Expr *x, constraint_Expr *y);

/* Parse parses a single build constraint line of the form "//go:build ..." or "// +build ..."
 * and returns the corresponding boolean expression; NULL and *err (if err != NULL) on error. */
static constraint_Expr *constraint_Parse(GoStr line, GoStr *err);
static bool constraint_IsGoBuild(GoStr line);
static bool constraint_IsPlusBuild(GoStr line);
/* PlusBuildLines returns a sequence of "// +build" lines that evaluate to the build
 * expression x; false and *err if x is too complex. */
static bool constraint_PlusBuildLines(constraint_Expr *x, GoStrVec *lines, GoStr *err);

#endif
