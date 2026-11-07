/* Unit tests of the go/printer port: inputs formatted by Go 1.26.5 (go/parser ParseComments +
 * go/printer Config.Fprint), covering syntax the gofmt corpus does not (generics, statements,
 * labels, alignment heuristics, //go:build fixing, doc comments, other modes). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "internal.h"

#include "strings/strings.c"
#include "unicode/utf8/utf8.c"
#include "unicode/tables.c"
#include "unicode/unicode.c"
#include "strconv/quote.c"
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

#include "test.h"

typedef struct {
	const char *src;
	unsigned mode;
	int tabwidth;
	Buf out;
	const char *err;
} format_job;

static void run_format(void *arg)
{
	format_job *j = arg;
	token_FileSet *fset = token_NewFileSet();
	GoStr err;
	ast_File *file = parser_ParseFile(fset, GOSTR(""), gostr_c(j->src), parser_ParseComments, &err);
	if (err.p) {
		j->err = gostr_cstr(err);
		return;
	}
	printer_Config cfg = {.Mode = j->mode, .Tabwidth = j->tabwidth};
	j->err = printer_Config_Fprint(&cfg, &j->out, fset, (ast_Node *)file);
}

static void check_format(const char *src, unsigned mode, int tabwidth, const char *want)
{
	format_job j = {src, mode, tabwidth, {0}, NULL};
	const char *f = try_run(run_format, &j);
	CHECK(f == NULL);
	CHECK(j.err == NULL);
	if (f == NULL && j.err == NULL)
		CHECK_MEM(j.out.p ? j.out.p : "", j.out.n, want, strlen(want));
}

static const char src_align[] =
	"package p\n"
	"\n"
	"var m = map[string]int{\n"
	"\t\"a\": 1,\n"
	"\t\"bb\": 2,\n"
	"\t\"a_very_long_key_that_is_much_longer_than_the_others_in_this_literal_xx\": 3,\n"
	"\t\"c\": 4,\n"
	"\t\"dd\": 5,\n"
	"}\n"
	"\n"
	"var s = []struct{ A, B int }{\n"
	"\t{A: 1, B: 2}, {A: 3, B: 4},\n"
	"\t{A: 5,\n"
	"\t\tB: 6},\n"
	"}\n"
	"\n"
	"const (\n"
	"\ta = iota // first\n"
	"\tbb       // second\n"
	"\tccc int = 7 // third\n"
	")\n"
	"\n"
	"type T struct {\n"
	"\tA    int    `json:\"a\"` // a\n"
	"\tBbbb string // b\n"
	"\tC struct{ X int }\n"
	"\t*T\n"
	"}\n";

static const char src_comments[] =
	"// Copyright header.\n"
	"\n"
	"// +build linux darwin\n"
	"\n"
	"// Package p is a test.\n"
	"// It has a   doc comment with a list:\n"
	"//  - one\n"
	"//  - two\n"
	"package p\n"
	"\n"
	"import (\n"
	"\t\"fmt\" // fmt\n"
	"\n"
	"\tb \"bytes\"\n"
	")\n"
	"\n"
	"/*\n"
	"\t * Block comment\n"
	"\t * with stars.\n"
	" */\n"
	"func f(a int, // first\n"
	"\tb string, /* second */\n"
	"\tc ...int, // rest\n"
	") (int, error) {\n"
	"\tx := a /* inline */ + 1\n"
	"\t_ = fmt.Sprint(b, c)\n"
	"\treturn x, nil // done\n"
	"}\n"
	"\n"
	"// Deprecated: use g2.\n"
	"func g() {\n"
	"\t// line comment in empty body\n"
	"}\n"
	"\n"
	"var _ = b.NewBuffer\n";

static const char src_control[] =
	"package p\n"
	"\n"
	"func f(ch chan int, done <-chan struct{}) (n int, err error) {\n"
	"outer:\n"
	"\tfor i := 0; i < 10; i++ {\n"
	"\t\tswitch {\n"
	"\t\tcase i%2 == 0:\n"
	"\t\t\tcontinue outer\n"
	"\t\tcase i > 5: break outer\n"
	"\t\tdefault:\n"
	"\t\t}\n"
	"\t\tselect {\n"
	"\t\tcase v := <-ch:\n"
	"\t\t\tn += v\n"
	"\t\tcase ch <- 1:\n"
	"\t\tcase <-done:\n"
	"\t\t\tgoto end\n"
	"\t\tdefault:\n"
	"\t\t}\n"
	"\t}\n"
	"\tfor k, v := range map[string]int{\"a\": 1} { _, _ = k, v }\n"
	"\tfor range 3 {}\n"
	"\tswitch x := n; x {\n"
	"\tcase 1, 2,\n"
	"\t\t3:\n"
	"\t\tfallthrough\n"
	"\tcase 4:\n"
	"\t}\n"
	"\tgo func() { defer close(ch) }()\n"
	"end:\n"
	"\treturn n*2 + 1, nil\n"
	"}\n";

static const char src_exprs[] =
	"package p\n"
	"\n"
	"func f(a, b, c, d int, s []int, x interface{}) {\n"
	"\t_ = a*b + c*d\n"
	"\t_ = a + b*c\n"
	"\t_ = (a + b) * c\n"
	"\t_ = a / *(&b)\n"
	"\t_ = a - -b\n"
	"\t_ = a & ^b\n"
	"\t_ = s[a+1 : b-1]\n"
	"\t_ = s[a:b:c]\n"
	"\t_ = s[:len(s)-1]\n"
	"\t_ = x.(fmt.Stringer).String()\n"
	"\t_ = func(x int) int { return x * 2 }(a)\n"
	"\t_ = []int{1, 2, 3}[a]\n"
	"\t_ = 0X1F + 0o17 + 1E3\n"
	"\tif v, ok := x.(int); ok && v > 0 || a<b {\n"
	"\t}\n"
	"\ta, b = b, a\n"
	"\ta++\n"
	"\tb <<= 2\n"
	"\t_ = struct{}{}\n"
	"\t_ = map[string][]*int{}\n"
	"\t_ = chan<- int(nil)\n"
	"\t_ = (<-chan int)(nil)\n"
	"}\n";

static const char src_generics[] =
	"package p\n"
	"\n"
	"type Number interface {\n"
	"\t~int | ~int64 |\n"
	"\t\t~float64\n"
	"}\n"
	"\n"
	"type List[T any] struct { next *List[T]; val T }\n"
	"\n"
	"type Ptr[P *T,] struct{}\n"
	"type PtrIface[P interface{ *T }] struct{}\n"
	"\n"
	"func Map[S ~[]E, E any, R any](s S, f func(E) R) []R {\n"
	"\tr := make([]R, 0, len(s))\n"
	"\tfor _, v := range s { r = append(r, f(v)) }\n"
	"\treturn r\n"
	"}\n"
	"\n"
	"func (l *List[T]) Push(v T) { l.next = &List[T]{val: v} }\n"
	"\n"
	"var _ = Map[[]int, int, string]\n";

static const char src_gobuild[] =
	"// +build linux,amd64 !windows\n"
	"\n"
	"package p\n";

static const char src_tie[] =
	"package p\n"
	"\n"
	"var m1 = map[string]int{\n"
	"\t\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": 1,\n"
	"\t\"bbbbbbbbbbbbbbbbbb\": 2,\n"
	"}\n"
	"\n"
	"var m2 = map[string]int{\n"
	"\t\"ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\": 1,\n"
	"\t\"dddddddddddddddddddddddddddddddddddd\": 2,\n"
	"}\n";

/* type parameters, constraints, [P *T,] trailing comma, one-line method body */
static void test_generics(void)
{
	check_format(src_generics, printer_TabIndent | printer_UseSpaces, 8,
		"package p\n"
		"\n"
		"type Number interface {\n"
		"\t~int | ~int64 |\n"
		"\t\t~float64\n"
		"}\n"
		"\n"
		"type List[T any] struct {\n"
		"\tnext *List[T]\n"
		"\tval  T\n"
		"}\n"
		"\n"
		"type Ptr[P *T,] struct{}\n"
		"type PtrIface[P interface{ *T }] struct{}\n"
		"\n"
		"func Map[S ~[]E, E any, R any](s S, f func(E) R) []R {\n"
		"\tr := make([]R, 0, len(s))\n"
		"\tfor _, v := range s {\n"
		"\t\tr = append(r, f(v))\n"
		"\t}\n"
		"\treturn r\n"
		"}\n"
		"\n"
		"func (l *List[T]) Push(v T) { l.next = &List[T]{val: v} }\n"
		"\n"
		"var _ = Map[[]int, int, string]\n");
}

/* labels, for/range/switch/select/goto/go/defer */
static void test_control(void)
{
	check_format(src_control, printer_TabIndent | printer_UseSpaces, 8,
		"package p\n"
		"\n"
		"func f(ch chan int, done <-chan struct{}) (n int, err error) {\n"
		"outer:\n"
		"\tfor i := 0; i < 10; i++ {\n"
		"\t\tswitch {\n"
		"\t\tcase i%2 == 0:\n"
		"\t\t\tcontinue outer\n"
		"\t\tcase i > 5:\n"
		"\t\t\tbreak outer\n"
		"\t\tdefault:\n"
		"\t\t}\n"
		"\t\tselect {\n"
		"\t\tcase v := <-ch:\n"
		"\t\t\tn += v\n"
		"\t\tcase ch <- 1:\n"
		"\t\tcase <-done:\n"
		"\t\t\tgoto end\n"
		"\t\tdefault:\n"
		"\t\t}\n"
		"\t}\n"
		"\tfor k, v := range map[string]int{\"a\": 1} {\n"
		"\t\t_, _ = k, v\n"
		"\t}\n"
		"\tfor range 3 {\n"
		"\t}\n"
		"\tswitch x := n; x {\n"
		"\tcase 1, 2,\n"
		"\t\t3:\n"
		"\t\tfallthrough\n"
		"\tcase 4:\n"
		"\t}\n"
		"\tgo func() { defer close(ch) }()\n"
		"end:\n"
		"\treturn n*2 + 1, nil\n"
		"}\n");
}

/* key/value alignment broken by a long key (geomean heuristic), const/struct columns */
static void test_align(void)
{
	check_format(src_align, printer_TabIndent | printer_UseSpaces, 8,
		"package p\n"
		"\n"
		"var m = map[string]int{\n"
		"\t\"a\":  1,\n"
		"\t\"bb\": 2,\n"
		"\t\"a_very_long_key_that_is_much_longer_than_the_others_in_this_literal_xx\": 3,\n"
		"\t\"c\":  4,\n"
		"\t\"dd\": 5,\n"
		"}\n"
		"\n"
		"var s = []struct{ A, B int }{\n"
		"\t{A: 1, B: 2}, {A: 3, B: 4},\n"
		"\t{A: 5,\n"
		"\t\tB: 6},\n"
		"}\n"
		"\n"
		"const (\n"
		"\ta   = iota     // first\n"
		"\tbb             // second\n"
		"\tccc int    = 7 // third\n"
		")\n"
		"\n"
		"type T struct {\n"
		"\tA    int    `json:\"a\"` // a\n"
		"\tBbbb string // b\n"
		"\tC    struct{ X int }\n"
		"\t*T\n"
		"}\n");
}

/* the same with tabs for alignment (Mode 0, Tabwidth 4) */
static void test_align_tabs(void)
{
	check_format(src_align, 0, 4,
		"package p\n"
		"\n"
		"var m = map[string]int{\n"
		"\t\"a\":\t1,\n"
		"\t\"bb\":\t2,\n"
		"\t\"a_very_long_key_that_is_much_longer_than_the_others_in_this_literal_xx\":\t3,\n"
		"\t\"c\":\t4,\n"
		"\t\"dd\":\t5,\n"
		"}\n"
		"\n"
		"var s = []struct{ A, B int }{\n"
		"\t{A: 1, B: 2}, {A: 3, B: 4},\n"
		"\t{A: 5,\n"
		"\t\tB:\t6},\n"
		"}\n"
		"\n"
		"const (\n"
		"\ta\t= iota\t\t// first\n"
		"\tbb\t\t\t\t// second\n"
		"\tccc\tint\t\t= 7\t// third\n"
		")\n"
		"\n"
		"type T struct {\n"
		"\tA\t\tint\t\t`json:\"a\"`\t// a\n"
		"\tBbbb\tstring\t// b\n"
		"\tC\t\tstruct{ X int }\n"
		"\t*T\n"
		"}\n");
}

/* // +build -> //go:build, doc comment reformatting, / * * / prefix, comments in parameter lists */
static void test_comments(void)
{
	check_format(src_comments, printer_TabIndent | printer_UseSpaces, 8,
		"// Copyright header.\n"
		"\n"
		"//go:build linux || darwin\n"
		"// +build linux darwin\n"
		"\n"
		"// Package p is a test.\n"
		"// It has a   doc comment with a list:\n"
		"//   - one\n"
		"//   - two\n"
		"package p\n"
		"\n"
		"import (\n"
		"\t\"fmt\" // fmt\n"
		"\n"
		"\tb \"bytes\"\n"
		")\n"
		"\n"
		"/*\n"
		" * Block comment\n"
		" * with stars.\n"
		" */\n"
		"func f(a int, // first\n"
		"\tb string, /* second */\n"
		"\tc ...int, // rest\n"
		") (int, error) {\n"
		"\tx := a /* inline */ + 1\n"
		"\t_ = fmt.Sprint(b, c)\n"
		"\treturn x, nil // done\n"
		"}\n"
		"\n"
		"// Deprecated: use g2.\n"
		"func g() {\n"
		"\t// line comment in empty body\n"
		"}\n"
		"\n"
		"var _ = b.NewBuffer\n");
}

/* the same in RawFormat (no tabwriter) */
static void test_comments_raw(void)
{
	check_format(src_comments, printer_RawFormat, 8,
		"// Copyright header.\n"
		"\n"
		"//go:build linux || darwin\n"
		"// +build linux darwin\n"
		"\n"
		"// Package p is a test.\n"
		"// It has a   doc comment with a list:\n"
		"//   - one\n"
		"//   - two\n"
		"package p\n"
		"\n"
		"import (\n"
		"\t\"fmt\"\t// fmt\n"
		"\n"
		"\tb \"bytes\"\n"
		")\n"
		"\n"
		"/*\n"
		" * Block comment\n"
		" * with stars.\n"
		" */\n"
		"func f(a int,\t// first\n"
		"\tb string,\t/* second */\n"
		"\tc ...int,\t// rest\n"
		") (int, error) {\n"
		"\tx := a /* inline */ + 1\n"
		"\t_ = fmt.Sprint(b, c)\n"
		"\treturn x, nil\t// done\n"
		"}\n"
		"\n"
		"// Deprecated: use g2.\n"
		"func g() {\n"
		"\t// line comment in empty body\n"
		"}\n"
		"\n"
		"var _ = b.NewBuffer\n");
}

/* binary expression spacing (cutoff/depth), slices, conversions */
static void test_exprs(void)
{
	check_format(src_exprs, printer_TabIndent | printer_UseSpaces, 8,
		"package p\n"
		"\n"
		"func f(a, b, c, d int, s []int, x interface{}) {\n"
		"\t_ = a*b + c*d\n"
		"\t_ = a + b*c\n"
		"\t_ = (a + b) * c\n"
		"\t_ = a / *(&b)\n"
		"\t_ = a - -b\n"
		"\t_ = a & ^b\n"
		"\t_ = s[a+1 : b-1]\n"
		"\t_ = s[a:b:c]\n"
		"\t_ = s[:len(s)-1]\n"
		"\t_ = x.(fmt.Stringer).String()\n"
		"\t_ = func(x int) int { return x * 2 }(a)\n"
		"\t_ = []int{1, 2, 3}[a]\n"
		"\t_ = 0X1F + 0o17 + 1E3\n"
		"\tif v, ok := x.(int); ok && v > 0 || a < b {\n"
		"\t}\n"
		"\ta, b = b, a\n"
		"\ta++\n"
		"\tb <<= 2\n"
		"\t_ = struct{}{}\n"
		"\t_ = map[string][]*int{}\n"
		"\t_ = chan<- int(nil)\n"
		"\t_ = (<-chan int)(nil)\n"
		"}\n");
}

/* normalizeNumbers */
static void test_exprs_norm(void)
{
	check_format(src_exprs, printer_TabIndent | printer_UseSpaces | printer_normalizeNumbers, 8,
		"package p\n"
		"\n"
		"func f(a, b, c, d int, s []int, x interface{}) {\n"
		"\t_ = a*b + c*d\n"
		"\t_ = a + b*c\n"
		"\t_ = (a + b) * c\n"
		"\t_ = a / *(&b)\n"
		"\t_ = a - -b\n"
		"\t_ = a & ^b\n"
		"\t_ = s[a+1 : b-1]\n"
		"\t_ = s[a:b:c]\n"
		"\t_ = s[:len(s)-1]\n"
		"\t_ = x.(fmt.Stringer).String()\n"
		"\t_ = func(x int) int { return x * 2 }(a)\n"
		"\t_ = []int{1, 2, 3}[a]\n"
		"\t_ = 0x1F + 0o17 + 1e3\n"
		"\tif v, ok := x.(int); ok && v > 0 || a < b {\n"
		"\t}\n"
		"\ta, b = b, a\n"
		"\ta++\n"
		"\tb <<= 2\n"
		"\t_ = struct{}{}\n"
		"\t_ = map[string][]*int{}\n"
		"\t_ = chan<- int(nil)\n"
		"\t_ = (<-chan int)(nil)\n"
		"}\n");
}

/* // +build with AND/OR/NOT */
static void test_gobuild(void)
{
	check_format(src_gobuild, printer_TabIndent | printer_UseSpaces, 8,
		"//go:build (linux && amd64) || !windows\n"
		"// +build linux,amd64 !windows\n"
		"\n"
		"package p\n");
}

/* key size ratios 50/20 and 95/38 (exactly 2.5): alignment depends on Go's amd64 math.Exp/Log bit
 * for bit (a libm port decides both the other way) */
static void test_tie(void)
{
	check_format(src_tie, printer_TabIndent | printer_UseSpaces, 8,
		"package p\n"
		"\n"
		"var m1 = map[string]int{\n"
		"\t\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\": 1,\n"
		"\t\"bbbbbbbbbbbbbbbbbb\": 2,\n"
		"}\n"
		"\n"
		"var m2 = map[string]int{\n"
		"\t\"ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\": 1,\n"
		"\t\"dddddddddddddddddddddddddddddddddddd\":                                                          2,\n"
		"}\n");
}

/* constraint subset used by fixGoBuildLines */
static void test_constraint(void)
{
	GoStr err;
	constraint_Expr *x = constraint_Parse(GOSTR("//go:build !(linux && arm) || (darwin && !cgo)"), &err);
	CHECK(x != NULL);
	if (x != NULL) {
		CHECK(gostr_eqc(constraint_Expr_String(x), "!(linux && arm) || (darwin && !cgo)"));
		GoStrVec lines;
		CHECK(constraint_PlusBuildLines(x, &lines, &err));
		CHECK(lines.n == 1 && gostr_eqc(lines.v[0], "// +build !linux !arm darwin,!cgo"));
	}
	CHECK(constraint_Parse(GOSTR("//go:build (a"), &err) == NULL && gostr_eqc(err, "missing close paren"));
	CHECK(constraint_Parse(GOSTR("//go:build a &&"), &err) == NULL && gostr_eqc(err, "unexpected end of expression"));
	CHECK(constraint_Parse(GOSTR("//go:build !!a"), &err) == NULL && gostr_eqc(err, "double negation not allowed"));
	CHECK(constraint_Parse(GOSTR("// +build"), &err) != NULL);
	CHECK(constraint_IsGoBuild(GOSTR("//go:build")) && !constraint_IsGoBuild(GOSTR("//go:buildx")));
	CHECK(constraint_IsPlusBuild(GOSTR("//+build x")) && !constraint_IsPlusBuild(GOSTR("// +buildx")));
	x = constraint_Parse(GOSTR("//go:build (a || b) && (c || d)"), NULL);
	CHECK(x != NULL);
	if (x != NULL) {
		GoStrVec lines;
		CHECK(constraint_PlusBuildLines(x, &lines, &err));
		CHECK(lines.n == 2 && gostr_eqc(lines.v[0], "// +build a b") && gostr_eqc(lines.v[1], "// +build c d"));
	}
	x = constraint_Parse(GOSTR("//go:build (a && b) || (c && (d || e))"), NULL);
	CHECK(x != NULL && !constraint_PlusBuildLines(x, &(GoStrVec){0}, &err) &&
	      gostr_eqc(err, "expression too complex for // +build lines"));
}

/* exprList alignment uses math.Exp/math.Log as Go computes them on amd64 */
static void test_math(void)
{
	for (int i = 1; i <= 1000; i++) {
		double x = printer_exp(printer_log((double)i));
		CHECK(x > (double)i * (1 - 1e-15) && x < (double)i * (1 + 1e-15));
	}
	CHECK(printer_fma(0x1.fb2f20d951adep+1, -0x1.1f569b7f17725p+3, 0x1.1ca2b5dcffa62p+5) == 0x1.2f104af3dfafap-46);
	CHECK(printer_fma(0.1, 10, -1) == 0x1p-54);
}


TESTS(TEST(test_generics), TEST(test_control), TEST(test_align), TEST(test_align_tabs), TEST(test_comments),
      TEST(test_comments_raw), TEST(test_exprs), TEST(test_exprs_norm), TEST(test_gobuild), TEST(test_tie),
      TEST(test_constraint), TEST(test_math))
