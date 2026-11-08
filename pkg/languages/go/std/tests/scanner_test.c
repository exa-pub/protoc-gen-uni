/* Unit tests of the go/scanner port: the tests of go1.26.5 src/go/scanner/scanner_test.go (tables
 * converted mechanically; strings as C literals). Copyright 2009 The Go Authors. All rights
 * reserved. BSD-3-Clause (see LICENSE in Go). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "internal.h"

#include "strings/strings.c"
#include "unicode/utf8/utf8.c"
#include "unicode/tables.c"
#include "unicode/unicode.c"
#include "strconv/quote.c"
#include "go/token/token.c"
#include "go/token/position.c"
#include "go/scanner/errors.c"
#include "go/scanner/scanner.c"

#include "test.h"

#define LEN(a) (sizeof(a) / sizeof *(a))
#define L(s) {(s), (int)sizeof(s) - 1}

static token_FileSet *fset;

static token_FileSet *getFset(void)
{
	if (!fset)
		fset = token_NewFileSet();
	return fset;
}

/* fset.AddFile(name, fset.Base(), size) */
static token_File *addFile(const char *name, int size)
{
	return token_FileSet_AddFile(getFset(), gostr_c(name), token_FileSet_Base(getFset()), size);
}

static void fail_test(const char *what, GoStr a, GoStr b)
{
	fprintf(stderr, "%s: got %.*s, want %.*s\n", what, a.n, a.p ? a.p : "", b.n, b.p ? b.p : "");
	uni_test_failed = 1;
}

#define EXPECT_STR(what, got, want)                                                                \
	do {                                                                                       \
		GoStr g__ = (got), w__ = (want);                                                   \
		if (!gostr_eq(g__, w__))                                                           \
			fail_test(what, g__, w__);                                                 \
	} while (0)

#define EXPECT_INT(what, got, want)                                                                \
	do {                                                                                       \
		long g__ = (long)(got), w__ = (long)(want);                                        \
		if (g__ != w__) {                                                                  \
			fprintf(stderr, "%s:%d: %s: got %ld, want %ld\n", __FILE__, __LINE__, what, g__, w__); \
			uni_test_failed = 1;                                                       \
		}                                                                                  \
	} while (0)

enum { special, literal, operator, keyword };

static int tokenclass(token_Token tok)
{
	if (token_Token_IsLiteral(tok))
		return literal;
	if (token_Token_IsOperator(tok))
		return operator;
	if (token_Token_IsKeyword(tok))
		return keyword;
	return special;
}

typedef struct {
	token_Token tok;
	const char *lit;
	int class;
} elt;

static const elt tokens[] = {
	{token_COMMENT, "/* a comment */", special},
	{token_COMMENT, "// a comment \n", special},
	{token_COMMENT, "/*\r*/", special},
	{token_COMMENT, "/**\r/*/", special},
	{token_COMMENT, "/**\r\r/*/", special},
	{token_COMMENT, "//\r\n", special},
	{token_IDENT, "foobar", literal},
	{token_IDENT, "a\333\260\333\261\333\270", literal},
	{token_IDENT, "foo\340\245\254\340\245\252", literal},
	{token_IDENT, "bar\357\274\231\357\274\230\357\274\227\357\274\226", literal},
	{token_IDENT, "\305\235", literal},
	{token_IDENT, "\305\235foo", literal},
	{token_INT, "0", literal},
	{token_INT, "1", literal},
	{token_INT, "123456789012345678890", literal},
	{token_INT, "01234567", literal},
	{token_INT, "0xcafebabe", literal},
	{token_FLOAT, "0.", literal},
	{token_FLOAT, ".0", literal},
	{token_FLOAT, "3.14159265", literal},
	{token_FLOAT, "1e0", literal},
	{token_FLOAT, "1e+100", literal},
	{token_FLOAT, "1e-100", literal},
	{token_FLOAT, "2.71828e-1000", literal},
	{token_IMAG, "0i", literal},
	{token_IMAG, "1i", literal},
	{token_IMAG, "012345678901234567889i", literal},
	{token_IMAG, "123456789012345678890i", literal},
	{token_IMAG, "0.i", literal},
	{token_IMAG, ".0i", literal},
	{token_IMAG, "3.14159265i", literal},
	{token_IMAG, "1e0i", literal},
	{token_IMAG, "1e+100i", literal},
	{token_IMAG, "1e-100i", literal},
	{token_IMAG, "2.71828e-1000i", literal},
	{token_CHAR, "'a'", literal},
	{token_CHAR, "'\\000'", literal},
	{token_CHAR, "'\\xFF'", literal},
	{token_CHAR, "'\\uff16'", literal},
	{token_CHAR, "'\\U0000ff16'", literal},
	{token_STRING, "`foobar`", literal},
	{token_STRING, "`foo\n\t                        bar`", literal},
	{token_STRING, "`\r`", literal},
	{token_STRING, "`foo\r\nbar`", literal},
	{token_ADD, "+", operator},
	{token_SUB, "-", operator},
	{token_MUL, "*", operator},
	{token_QUO, "/", operator},
	{token_REM, "%", operator},
	{token_AND, "&", operator},
	{token_OR, "|", operator},
	{token_XOR, "^", operator},
	{token_SHL, "<<", operator},
	{token_SHR, ">>", operator},
	{token_AND_NOT, "&^", operator},
	{token_ADD_ASSIGN, "+=", operator},
	{token_SUB_ASSIGN, "-=", operator},
	{token_MUL_ASSIGN, "*=", operator},
	{token_QUO_ASSIGN, "/=", operator},
	{token_REM_ASSIGN, "%=", operator},
	{token_AND_ASSIGN, "&=", operator},
	{token_OR_ASSIGN, "|=", operator},
	{token_XOR_ASSIGN, "^=", operator},
	{token_SHL_ASSIGN, "<<=", operator},
	{token_SHR_ASSIGN, ">>=", operator},
	{token_AND_NOT_ASSIGN, "&^=", operator},
	{token_LAND, "&&", operator},
	{token_LOR, "||", operator},
	{token_ARROW, "<-", operator},
	{token_INC, "++", operator},
	{token_DEC, "--", operator},
	{token_EQL, "==", operator},
	{token_LSS, "<", operator},
	{token_GTR, ">", operator},
	{token_ASSIGN, "=", operator},
	{token_NOT, "!", operator},
	{token_NEQ, "!=", operator},
	{token_LEQ, "<=", operator},
	{token_GEQ, ">=", operator},
	{token_DEFINE, ":=", operator},
	{token_ELLIPSIS, "...", operator},
	{token_LPAREN, "(", operator},
	{token_LBRACK, "[", operator},
	{token_LBRACE, "{", operator},
	{token_COMMA, ",", operator},
	{token_PERIOD, ".", operator},
	{token_RPAREN, ")", operator},
	{token_RBRACK, "]", operator},
	{token_RBRACE, "}", operator},
	{token_SEMICOLON, ";", operator},
	{token_COLON, ":", operator},
	{token_TILDE, "~", operator},
	{token_BREAK, "break", keyword},
	{token_CASE, "case", keyword},
	{token_CHAN, "chan", keyword},
	{token_CONST, "const", keyword},
	{token_CONTINUE, "continue", keyword},
	{token_DEFAULT, "default", keyword},
	{token_DEFER, "defer", keyword},
	{token_ELSE, "else", keyword},
	{token_FALLTHROUGH, "fallthrough", keyword},
	{token_FOR, "for", keyword},
	{token_FUNC, "func", keyword},
	{token_GO, "go", keyword},
	{token_GOTO, "goto", keyword},
	{token_IF, "if", keyword},
	{token_IMPORT, "import", keyword},
	{token_INTERFACE, "interface", keyword},
	{token_MAP, "map", keyword},
	{token_PACKAGE, "package", keyword},
	{token_RANGE, "range", keyword},
	{token_RETURN, "return", keyword},
	{token_SELECT, "select", keyword},
	{token_STRUCT, "struct", keyword},
	{token_SWITCH, "switch", keyword},
	{token_TYPE, "type", keyword},
	{token_VAR, "var", keyword},
};

static const char whitespace[] = "  \t  \n\n\n"; /* to separate tokens */

static int newlineCount(GoStr s)
{
	int n = 0;
	for (int i = 0; i < s.n; i++)
		if (s.p[i] == '\n')
			n++;
	return n;
}

static void checkPos(GoStr lit, token_Pos p, token_Position expected)
{
	token_Position pos = token_FileSet_Position(getFset(), p);
	if (!gostr_eq(pos.Filename, expected.Filename) &&
	    !gostr_eq(scanner_filepathClean(pos.Filename), scanner_filepathClean(expected.Filename)))
		fail_test("bad filename", pos.Filename, expected.Filename);
	if (pos.Offset != expected.Offset || pos.Line != expected.Line || pos.Column != expected.Column) {
		fprintf(stderr, "bad position for %.*s: got %d:%d:%d, expected %d:%d:%d\n", lit.n, lit.p ? lit.p : "",
			pos.Offset, pos.Line, pos.Column, expected.Offset, expected.Line, expected.Column);
		uni_test_failed = 1;
	}
}

static void eh_fail(void *ctx, token_Position pos, GoStr msg)
{
	fprintf(stderr, "error handler called (msg = %.*s)\n", msg.n, msg.p);
	uni_test_failed = 1;
}

/* Verify that calling Scan() provides the correct results. */
static void test_scan(void)
{
	int whitespace_linecount = newlineCount(GOSTR(whitespace));
	Buf src = {0};
	for (size_t i = 0; i < LEN(tokens); i++) {
		buf_adds(&src, tokens[i].lit);
		buf_adds(&src, whitespace);
	}
	GoStr source = gostr_buf(&src);

	/* verify scan */
	scanner_Scanner s = {0};
	scanner_Scanner_Init(&s, addFile("", source.n),
			     source, eh_fail, NULL, scanner_ScanComments | scanner_dontInsertSemis);

	/* set up expected position */
	token_Position epos = {GOSTR(""), 0, 1, 1};

	size_t index = 0;
	for (;;) {
		token_Pos pos;
		GoStr lit;
		token_Token tok = scanner_Scanner_Scan(&s, &pos, &lit);

		/* check position */
		if (tok == token_EOF) {
			/* correction for EOF */
			epos.Line = newlineCount(source);
			epos.Column = 2;
		}
		checkPos(lit, pos, epos);

		/* check token */
		elt e = {token_EOF, "", special};
		if (index < LEN(tokens))
			e = tokens[index++];
		EXPECT_INT("token", tok, e.tok);

		/* check token class */
		EXPECT_INT("class", tokenclass(tok), e.class);

		/* check literal */
		GoStr elit = GOSTR("");
		GoStr el = gostr_c(e.lit);
		switch (e.tok) {
		case token_COMMENT:
			/* no CRs in comments */
			elit = scanner_stripCR(el, el.p[1] == '*');
			/* //-style comment literal doesn't contain newline */
			if (elit.p[1] == '/')
				elit.n--;
			break;
		case token_IDENT:
			elit = el;
			break;
		case token_SEMICOLON:
			elit = GOSTR(";");
			break;
		default:
			if (token_Token_IsLiteral(e.tok)) {
				/* no CRs in raw string literals */
				elit = el;
				if (elit.p[0] == '`')
					elit = scanner_stripCR(elit, false);
			} else if (token_Token_IsKeyword(e.tok)) {
				elit = el;
			}
		}
		EXPECT_STR("literal", lit, elit);

		/* the parser backdoor: end of STRING tokens */
		if (tok == token_STRING)
			EXPECT_INT("stringEnd", s.stringEnd, pos + (int)strlen(e.lit));

		if (tok == token_EOF)
			break;

		/* update position */
		epos.Offset += el.n + (int)strlen(whitespace);
		epos.Line += newlineCount(el) + whitespace_linecount;
	}

	EXPECT_INT("ErrorCount", s.ErrorCount, 0);
}

static void test_strip_cr(void)
{
	static const struct {
		const char *have, *want;
	} tests[] = {
		{"//\n", "//\n"},
		{"//\r\n", "//\n"},
		{"//\r\r\r\n", "//\n"},
		{"//\r*\r/\r\n", "//*/\n"},
		{"/**/", "/**/"},
		{"/*\r/*/", "/*/*/"},
		{"/*\r*/", "/**/"},
		{"/**\r/*/", "/**\r/*/"},
		{"/*\r/\r*\r/*/", "/*/*\r/*/"},
		{"/*\r\r\r\r*/", "/**/"},
	};
	for (size_t i = 0; i < LEN(tests); i++) {
		GoStr have = gostr_c(tests[i].have);
		EXPECT_STR("stripCR", scanner_stripCR(have, have.n >= 2 && have.p[1] == '*'), gostr_c(tests[i].want));
	}
}

static void checkSemi(GoStr input, GoStr want, int mode)
{
	if ((mode & scanner_ScanComments) == 0) {
		want = strings_ReplaceAll(want, GOSTR("COMMENT "), GOSTR(""));
		want = strings_ReplaceAll(want, GOSTR(" COMMENT"), GOSTR("")); /* if at end */
		want = strings_ReplaceAll(want, GOSTR("COMMENT"), GOSTR("")); /* if sole token */
	}

	token_File *file = addFile("TestSemis", input.n);
	scanner_Scanner scan = {0};
	scanner_Scanner_Init(&scan, file, input, NULL, NULL, mode);
	Buf got = {0};
	for (;;) {
		token_Pos pos;
		GoStr lit;
		token_Token tok = scanner_Scanner_Scan(&scan, &pos, &lit);
		if (tok == token_EOF)
			break;
		if (tok == token_SEMICOLON && !gostr_eqc(lit, ";")) {
			/* Artificial semicolon: assert that position is EOF or that of a newline. */
			int off = token_File_Offset(file, pos);
			if (off != input.n && input.p[off] != '\n') {
				fprintf(stderr, "scanning <<%.*s>>, got SEMICOLON at offset %d, want newline or EOF\n",
					input.n, input.p, off);
				uni_test_failed = 1;
			}
		}
		if (got.n)
			buf_addc(&got, ' ');
		buf_addg(&got, token_Token_String(tok)); /* "\n" => ";" */
	}
	if (!gostr_eq(gostr_buf(&got), want)) {
		fprintf(stderr, "scanning <<%.*s>>, got [%.*s], want [%.*s]\n", input.n, input.p, (int)got.n,
			got.p ? got.p : "", want.n, want.p);
		uni_test_failed = 1;
	}
}

static const struct {
	const char *input, *want;
} semicolonTests[] = {
	{"", ""},
	{"\357\273\277;", ";"},
	{";", ";"},
	{"foo\n", "IDENT ;"},
	{"123\n", "INT ;"},
	{"1.2\n", "FLOAT ;"},
	{"'x'\n", "CHAR ;"},
	{"\"x\"\n", "STRING ;"},
	{"`x`\n", "STRING ;"},
	{"+\n", "+"},
	{"-\n", "-"},
	{"*\n", "*"},
	{"/\n", "/"},
	{"%\n", "%"},
	{"&\n", "&"},
	{"|\n", "|"},
	{"^\n", "^"},
	{"<<\n", "<<"},
	{">>\n", ">>"},
	{"&^\n", "&^"},
	{"+=\n", "+="},
	{"-=\n", "-="},
	{"*=\n", "*="},
	{"/=\n", "/="},
	{"%=\n", "%="},
	{"&=\n", "&="},
	{"|=\n", "|="},
	{"^=\n", "^="},
	{"<<=\n", "<<="},
	{">>=\n", ">>="},
	{"&^=\n", "&^="},
	{"&&\n", "&&"},
	{"||\n", "||"},
	{"<-\n", "<-"},
	{"++\n", "++ ;"},
	{"--\n", "-- ;"},
	{"==\n", "=="},
	{"<\n", "<"},
	{">\n", ">"},
	{"=\n", "="},
	{"!\n", "!"},
	{"!=\n", "!="},
	{"<=\n", "<="},
	{">=\n", ">="},
	{":=\n", ":="},
	{"...\n", "..."},
	{"(\n", "("},
	{"[\n", "["},
	{"{\n", "{"},
	{",\n", ","},
	{".\n", "."},
	{")\n", ") ;"},
	{"]\n", "] ;"},
	{"}\n", "} ;"},
	{";\n", ";"},
	{":\n", ":"},
	{"break\n", "break ;"},
	{"case\n", "case"},
	{"chan\n", "chan"},
	{"const\n", "const"},
	{"continue\n", "continue ;"},
	{"default\n", "default"},
	{"defer\n", "defer"},
	{"else\n", "else"},
	{"fallthrough\n", "fallthrough ;"},
	{"for\n", "for"},
	{"func\n", "func"},
	{"go\n", "go"},
	{"goto\n", "goto"},
	{"if\n", "if"},
	{"import\n", "import"},
	{"interface\n", "interface"},
	{"map\n", "map"},
	{"package\n", "package"},
	{"range\n", "range"},
	{"return\n", "return ;"},
	{"select\n", "select"},
	{"struct\n", "struct"},
	{"switch\n", "switch"},
	{"type\n", "type"},
	{"var\n", "var"},
	{"foo//comment\n", "IDENT COMMENT ;"},
	{"foo//comment", "IDENT COMMENT ;"},
	{"foo/*comment*/\n", "IDENT COMMENT ;"},
	{"foo/*\n*/", "IDENT COMMENT ;"},
	{"foo/*comment*/    \n", "IDENT COMMENT ;"},
	{"foo/*\n*/    ", "IDENT COMMENT ;"},
	{"foo    // comment\n", "IDENT COMMENT ;"},
	{"foo    // comment", "IDENT COMMENT ;"},
	{"foo    /*comment*/\n", "IDENT COMMENT ;"},
	{"foo    /*\n*/", "IDENT COMMENT ;"},
	{"foo    /*  */ /* \n */ bar/**/\n", "IDENT COMMENT COMMENT ; IDENT COMMENT ;"},
	{"foo    /*0*/ /*1*/ /*2*/\n", "IDENT COMMENT COMMENT COMMENT ;"},
	{"foo    /*comment*/    \n", "IDENT COMMENT ;"},
	{"foo    /*0*/ /*1*/ /*2*/    \n", "IDENT COMMENT COMMENT COMMENT ;"},
	{"foo\t/**/ /*-------------*/       /*----\n"
		"*/bar       /*  \n"
		"*/baa\n",
	 "IDENT COMMENT COMMENT COMMENT ; IDENT COMMENT ; IDENT ;"},
	{"foo    /* an EOF terminates a line */", "IDENT COMMENT ;"},
	{"foo    /* an EOF terminates a line */ /*", "IDENT COMMENT COMMENT ;"},
	{"foo    /* an EOF terminates a line */ //", "IDENT COMMENT COMMENT ;"},
	{"package main\n"
		"\n"
		"func main() {\n"
		"\tif {\n"
		"\t\treturn /* */ }\n"
		"}\n",
	 "package IDENT ; func IDENT ( ) { if { return COMMENT } ; } ;"},
	{"package main", "package IDENT ;"},
};

static void test_semicolons(void)
{
	for (size_t k = 0; k < LEN(semicolonTests); k++) {
		GoStr input = gostr_c(semicolonTests[k].input), want = gostr_c(semicolonTests[k].want);
		checkSemi(input, want, 0);
		checkSemi(input, want, scanner_ScanComments);

		/* if the input ended in newlines, the input must tokenize the same with or without
		 * those newlines */
		for (int i = input.n - 1; i >= 0 && input.p[i] == '\n'; i--) {
			checkSemi(gostr_sub(input, 0, i), want, 0);
			checkSemi(gostr_sub(input, 0, i), want, scanner_ScanComments);
		}
	}
}

typedef struct {
	const char *srcline; /* a line of source text */
	const char *filename; /* filename for current token; error message for invalid line directives */
	int line, column; /* line and column for current token; error position for invalid line directives */
} segment;

static const segment segments[] = {
	{"  line1", "TestLineDirectives", 1, 3},
	{"\nline2", "TestLineDirectives", 2, 1},
	{"\nline3  //line File1.go:100", "TestLineDirectives", 3, 1},
	{"\nline4", "TestLineDirectives", 4, 1},
	{"\n//line File1.go:100\n  line100", "File1.go", 100, 0},
	{"\n//line  \t :42\n  line1", " \t ", 42, 0},
	{"\n//line File2.go:200\n  line200", "File2.go", 200, 0},
	{"\n//line foo\t:42\n  line42", "foo\t", 42, 0},
	{"\n //line foo:42\n  line43", "foo\t", 44, 0},
	{"\n//line foo 42\n  line44", "foo\t", 46, 0},
	{"\n//line /bar:42\n  line45", "/bar", 42, 0},
	{"\n//line ./foo:42\n  line46", "foo", 42, 0},
	{"\n//line a/b/c/File1.go:100\n  line100", "a/b/c/File1.go", 100, 0},
	{"\n//line c:\\bar:42\n  line200", "c:\\bar", 42, 0},
	{"\n//line c:\\dir\\File1.go:100\n  line201", "c:\\dir\\File1.go", 100, 0},
	{"\n//line :100\na1", "", 100, 0},
	{"\n//line bar:100\nb1", "bar", 100, 0},
	{"\n//line :100:10\nc1", "bar", 100, 10},
	{"\n//line foo:100:10\nd1", "foo", 100, 10},
	{"\n/*line :100*/a2", "", 100, 0},
	{"\n/*line bar:100*/b2", "bar", 100, 0},
	{"\n/*line :100:10*/c2", "bar", 100, 10},
	{"\n/*line foo:100:10*/d2", "foo", 100, 10},
	{"\n/*line foo:100:10*/    e2", "foo", 100, 14},
	{"\n/*line foo:100:10*/\n\nf2", "foo", 102, 1},
};

static const segment dirsegments[] = {
	{"  line1", "TestLineDir/TestLineDirectives", 1, 3},
	{"\n//line File1.go:100\n  line100", "TestLineDir/File1.go", 100, 0},
};

static const segment dirUnixSegments[] = {
	{"\n//line /bar:42\n  line42", "/bar", 42, 0},
};

static void eh_count(void *ctx, token_Position pos, GoStr msg)
{
	fprintf(stderr, "%.*s\n", GOSTR_FMT(scanner_Error_Error(&(scanner_Error){pos, msg})));
	uni_test_failed = 1;
}

static void testSegments(const segment *segs, size_t n, const char *filename)
{
	Buf src = {0};
	for (size_t i = 0; i < n; i++)
		buf_adds(&src, segs[i].srcline);

	/* verify scan */
	scanner_Scanner S = {0};
	token_File *file =
		token_FileSet_AddFile(getFset(), gostr_c(filename), token_FileSet_Base(getFset()), (int)src.n);
	scanner_Scanner_Init(&S, file, gostr_buf(&src), eh_count, NULL, scanner_dontInsertSemis);
	for (size_t i = 0; i < n; i++) {
		token_Pos p;
		GoStr lit;
		scanner_Scanner_Scan(&S, &p, &lit);
		token_Position pos = token_File_Position(file, p);
		checkPos(lit, p, (token_Position){gostr_c(segs[i].filename), pos.Offset, segs[i].line, segs[i].column});
	}

	EXPECT_INT("ErrorCount", S.ErrorCount, 0);
}

/* Verify that line directives are interpreted correctly. */
static void test_line_directives(void)
{
	testSegments(segments, LEN(segments), "TestLineDirectives");
	testSegments(dirsegments, LEN(dirsegments), "TestLineDir/TestLineDirectives");
	testSegments(dirUnixSegments, LEN(dirUnixSegments), "TestLineDir/TestLineDirectives");
}

/* The filename is used for the error message in these test cases. The first line directive is
 * valid and used to control the expected error line. */
static const segment invalidSegments[] = {
	{"\n//line :1:1\n//line foo:42 extra text\ndummy", "invalid line number: 42 extra text", 1, 12},
	{"\n//line :2:1\n//line foobar:\ndummy", "invalid line number: ", 2, 15},
	{"\n//line :5:1\n//line :0\ndummy", "invalid line number: 0", 5, 9},
	{"\n//line :10:1\n//line :1:0\ndummy", "invalid column number: 0", 10, 11},
	{"\n//line :1:1\n//line :foo:0\ndummy", "invalid line number: 0", 1, 13},
};

static const segment *curSegment;

static void eh_invalid(void *ctx, token_Position pos, GoStr msg)
{
	EXPECT_STR("error", msg, gostr_c(curSegment->filename));
	EXPECT_INT("line", pos.Line, curSegment->line);
	EXPECT_INT("column", pos.Column, curSegment->column);
}

/* Verify that invalid line directives get the correct error message. */
static void test_invalid_line_directives(void)
{
	Buf src = {0};
	for (size_t i = 0; i < LEN(invalidSegments); i++)
		buf_adds(&src, invalidSegments[i].srcline);

	scanner_Scanner S = {0};
	token_File *file = token_FileSet_AddFile(getFset(), GOSTR("dir/TestInvalidLineDirectives"),
						 token_FileSet_Base(getFset()), (int)src.n);
	scanner_Scanner_Init(&S, file, gostr_buf(&src), eh_invalid, NULL, scanner_dontInsertSemis);
	for (size_t i = 0; i < LEN(invalidSegments); i++) {
		curSegment = &invalidSegments[i];
		token_Pos p;
		GoStr lit;
		scanner_Scanner_Scan(&S, &p, &lit);
	}

	EXPECT_INT("ErrorCount", S.ErrorCount, (int)LEN(invalidSegments));
}

/* Verify that initializing the same scanner more than once works correctly. */
static void test_init(void)
{
	scanner_Scanner s = {0};
	token_Pos pos;
	GoStr lit;

	/* 1st init */
	GoStr src1 = GOSTR("if true { }");
	token_File *f1 = addFile("src1", src1.n);
	scanner_Scanner_Init(&s, f1, src1, NULL, NULL, scanner_dontInsertSemis);
	EXPECT_INT("size", token_File_Size(f1), src1.n);
	scanner_Scanner_Scan(&s, &pos, &lit); /* if */
	scanner_Scanner_Scan(&s, &pos, &lit); /* true */
	EXPECT_INT("tok", scanner_Scanner_Scan(&s, &pos, &lit), token_LBRACE);

	/* 2nd init */
	GoStr src2 = GOSTR("go true { ]");
	token_File *f2 = addFile("src2", src2.n);
	scanner_Scanner_Init(&s, f2, src2, NULL, NULL, scanner_dontInsertSemis);
	EXPECT_INT("size", token_File_Size(f2), src2.n);
	EXPECT_INT("tok", scanner_Scanner_Scan(&s, &pos, &lit), token_GO);

	EXPECT_INT("ErrorCount", s.ErrorCount, 0);
}

static void eh_list(void *ctx, token_Position pos, GoStr msg) { scanner_ErrorList_Add(ctx, pos, msg); }

static void test_std_error_handler(void)
{
	static const char src[] = "@\n" /* illegal character, cause an error */
				  "@ @\n" /* two errors on the same line */
				  "//line File2:20\n"
				  "@\n" /* different file, but same line */
				  "//line File2:1\n"
				  "@ @\n" /* same file, decreasing line number */
				  "//line File1:1\n"
				  "@ @ @"; /* original file, line 1 again */

	scanner_ErrorList list = {0};
	scanner_Scanner s = {0};
	scanner_Scanner_Init(&s, addFile("File1", (int)sizeof src - 1),
			     GOSTR(src), eh_list, &list, scanner_dontInsertSemis);
	for (;;) {
		token_Pos pos;
		GoStr lit;
		if (scanner_Scanner_Scan(&s, &pos, &lit) == token_EOF)
			break;
	}

	EXPECT_INT("errors", list.list.n, s.ErrorCount);
	EXPECT_INT("raw errors", list.list.n, 9);

	scanner_ErrorList_Sort(&list);
	EXPECT_INT("sorted errors", list.list.n, 9);
	/* sorted by filename, line, column (Column 0 after a //line file:line directive); expected
	 * values from Go 1.26.5 */
	EXPECT_STR("first", scanner_Error_Error(list.list.v[0]), GOSTR("File1:1: illegal character U+0040 '@'"));
	EXPECT_STR("last", scanner_Error_Error(list.list.v[8]), GOSTR("File2:20: illegal character U+0040 '@'"));

	scanner_ErrorList_RemoveMultiples(&list);
	EXPECT_INT("one-per-line errors", list.list.n, 4);
	EXPECT_STR("Error", scanner_ErrorList_Error(&list),
		   GOSTR("File1:1: illegal character U+0040 '@' (and 3 more errors)"));

	scanner_ErrorList empty = {0};
	EXPECT_STR("Error", scanner_ErrorList_Error(&empty), GOSTR("no errors"));
	scanner_ErrorList_Add(&empty, (token_Position){0}, GOSTR("msg"));
	EXPECT_STR("Error", scanner_ErrorList_Error(&empty), GOSTR("msg"));
}

typedef struct {
	int cnt; /* number of errors encountered */
	GoStr msg; /* last error message encountered */
	token_Position pos; /* last error position encountered */
} errorCollector;

static void eh_collect(void *ctx, token_Position pos, GoStr msg)
{
	errorCollector *h = ctx;
	h->cnt++;
	h->msg = msg;
	h->pos = pos;
}

static void checkError(GoStr src, token_Token tok, int pos, GoStr lit, GoStr err)
{
	scanner_Scanner s = {0};
	errorCollector h = {0};
	scanner_Scanner_Init(&s, addFile("", src.n), src,
			     eh_collect, &h, scanner_ScanComments | scanner_dontInsertSemis);
	token_Pos pos0;
	GoStr lit0;
	token_Token tok0 = scanner_Scanner_Scan(&s, &pos0, &lit0);
	EXPECT_INT("tok", tok0, tok);
	if (tok0 != token_ILLEGAL)
		EXPECT_STR("literal", lit0, lit);
	EXPECT_INT("cnt", h.cnt, err.n != 0 ? 1 : 0);
	EXPECT_STR("msg", h.msg, err);
	EXPECT_INT("offset", h.pos.Offset, pos);
}

static const struct {
	GoStr src;
	token_Token tok;
	int pos;
	GoStr lit, err;
} errors[] = {
	{L("\007"), token_ILLEGAL, 0, L(""), L("illegal character U+0007")},
	{L("#"), token_ILLEGAL, 0, L(""), L("illegal character U+0023 '#'")},
	{L("\342\200\246"), token_ILLEGAL, 0, L(""), L("illegal character U+2026 '\342\200\246'")},
	{L(".."), token_PERIOD, 0, L(""), L("")},
	{L("' '"), token_CHAR, 0, L("' '"), L("")},
	{L("''"), token_CHAR, 0, L("''"), L("illegal rune literal")},
	{L("'12'"), token_CHAR, 0, L("'12'"), L("illegal rune literal")},
	{L("'123'"), token_CHAR, 0, L("'123'"), L("illegal rune literal")},
	{L("'\\0'"), token_CHAR, 3, L("'\\0'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\07'"), token_CHAR, 4, L("'\\07'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\8'"), token_CHAR, 2, L("'\\8'"), L("unknown escape sequence")},
	{L("'\\08'"), token_CHAR, 3, L("'\\08'"), L("illegal character U+0038 '8' in escape sequence")},
	{L("'\\x'"), token_CHAR, 3, L("'\\x'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\x0'"), token_CHAR, 4, L("'\\x0'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\x0g'"), token_CHAR, 4, L("'\\x0g'"), L("illegal character U+0067 'g' in escape sequence")},
	{L("'\\u'"), token_CHAR, 3, L("'\\u'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\u0'"), token_CHAR, 4, L("'\\u0'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\u00'"), token_CHAR, 5, L("'\\u00'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\u000'"), token_CHAR, 6, L("'\\u000'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\u000"), token_CHAR, 6, L("'\\u000"), L("escape sequence not terminated")},
	{L("'\\u0000'"), token_CHAR, 0, L("'\\u0000'"), L("")},
	{L("'\\U'"), token_CHAR, 3, L("'\\U'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U0'"), token_CHAR, 4, L("'\\U0'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U00'"), token_CHAR, 5, L("'\\U00'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U000'"), token_CHAR, 6, L("'\\U000'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U0000'"), token_CHAR, 7, L("'\\U0000'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U00000'"), token_CHAR, 8, L("'\\U00000'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U000000'"), token_CHAR, 9, L("'\\U000000'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U0000000'"), token_CHAR, 10, L("'\\U0000000'"), L("illegal character U+0027 ''' in escape sequence")},
	{L("'\\U0000000"), token_CHAR, 10, L("'\\U0000000"), L("escape sequence not terminated")},
	{L("'\\U00000000'"), token_CHAR, 0, L("'\\U00000000'"), L("")},
	{L("'\\Uffffffff'"), token_CHAR, 2, L("'\\Uffffffff'"), L("escape sequence is invalid Unicode code point")},
	{L("'"), token_CHAR, 0, L("'"), L("rune literal not terminated")},
	{L("'\\"), token_CHAR, 2, L("'\\"), L("escape sequence not terminated")},
	{L("'\n"), token_CHAR, 0, L("'"), L("rune literal not terminated")},
	{L("'\n   "), token_CHAR, 0, L("'"), L("rune literal not terminated")},
	{L("\"\""), token_STRING, 0, L("\"\""), L("")},
	{L("\"abc"), token_STRING, 0, L("\"abc"), L("string literal not terminated")},
	{L("\"abc\n"), token_STRING, 0, L("\"abc"), L("string literal not terminated")},
	{L("\"abc\n   "), token_STRING, 0, L("\"abc"), L("string literal not terminated")},
	{L("``"), token_STRING, 0, L("``"), L("")},
	{L("`"), token_STRING, 0, L("`"), L("raw string literal not terminated")},
	{L("/**/"), token_COMMENT, 0, L("/**/"), L("")},
	{L("/*"), token_COMMENT, 0, L("/*"), L("comment not terminated")},
	{L("077"), token_INT, 0, L("077"), L("")},
	{L("078."), token_FLOAT, 0, L("078."), L("")},
	{L("07801234567."), token_FLOAT, 0, L("07801234567."), L("")},
	{L("078e0"), token_FLOAT, 0, L("078e0"), L("")},
	{L("0E"), token_FLOAT, 2, L("0E"), L("exponent has no digits")},
	{L("078"), token_INT, 2, L("078"), L("invalid digit '8' in octal literal")},
	{L("07090000008"), token_INT, 3, L("07090000008"), L("invalid digit '9' in octal literal")},
	{L("0x"), token_INT, 2, L("0x"), L("hexadecimal literal has no digits")},
	{L("\"abc\000def\""), token_STRING, 4, L("\"abc\000def\""), L("illegal character NUL")},
	{L("\"abc\200def\""), token_STRING, 4, L("\"abc\200def\""), L("illegal UTF-8 encoding")},
	{L("\357\273\277\357\273\277"), token_ILLEGAL, 3, L("\357\273\277\357\273\277"), L("illegal byte order mark")},
	{L("//\357\273\277"), token_COMMENT, 2, L("//\357\273\277"), L("illegal byte order mark")},
	{L("'\357\273\277'"), token_CHAR, 1, L("'\357\273\277'"), L("illegal byte order mark")},
	{L("\"abc\357\273\277def\""), token_STRING, 4, L("\"abc\357\273\277def\""), L("illegal byte order mark")},
	{L("abc\000def"), token_IDENT, 3, L("abc"), L("illegal character NUL")},
	{L("abc\000"), token_IDENT, 3, L("abc"), L("illegal character NUL")},
	{L("\342\200\234abc\342\200\235"), token_ILLEGAL, 0, L("abc"),
	 L("curly quotation mark '\342\200\234' (use neutral '\"')")},
};

static void test_scan_errors(void)
{
	for (size_t i = 0; i < LEN(errors); i++)
		checkError(errors[i].src, errors[i].tok, errors[i].pos, errors[i].lit, errors[i].err);
}

static void eh_utf16(void *ctx, token_Position posn, GoStr msg)
{
	Buf *b = ctx;
	buf_fmt(b, "#%d: ", posn.Offset);
	buf_addg(b, msg);
	buf_addc(b, '\n');
}

static void test_utf16(void)
{
	static const GoStr srcs[] = {
		/* BOM + "package p" encoded as UTF-16 BE, LE */
		L("\xfe\xff\x00p\x00" "a\x00" "c\x00k\x00" "a\x00g\x00" "e\x00 \x00p"),
		L("\xff\xfep\x00" "a\x00" "c\x00k\x00" "a\x00g\x00" "e\x00 \x00p\x00"),
	};
	for (size_t i = 0; i < LEN(srcs); i++) {
		Buf got = {0};
		scanner_Scanner sc = {0};
		scanner_Scanner_Init(&sc, addFile("", srcs[i].n),
				     srcs[i], eh_utf16, &got, 0);
		token_Pos pos;
		GoStr lit;
		scanner_Scanner_Scan(&sc, &pos, &lit);
		/* We expect two errors: one from the decoder, one from the scanner. */
		EXPECT_STR("errors", gostr_buf(&got),
			   GOSTR("#0: illegal UTF-8 encoding (got UTF-16)\n"
				 "#0: illegal character U+FFFD '\xef\xbf\xbd'\n"));
	}
}

/* Verify that no comments show up as literal values when skipping comments. */
static void test_issue10213(void)
{
	static const char src[] = "\n\t\tvar (\n\t\t\tA = 1 // foo\n\t\t)\n\n"
				  "\t\tvar (\n\t\t\tB = 2\n\t\t\t// foo\n\t\t)\n\n"
				  "\t\tvar C = 3 // foo\n\n"
				  "\t\tvar D = 4\n\t\t// foo\n\n"
				  "\t\tfunc anycode() {\n\t\t// foo\n\t\t}\n\t";
	scanner_Scanner s = {0};
	scanner_Scanner_Init(&s, addFile("", (int)sizeof src - 1),
			     GOSTR(src), NULL, NULL, 0);
	for (;;) {
		token_Pos pos;
		GoStr lit;
		token_Token tok = scanner_Scanner_Scan(&s, &pos, &lit);
		int class = tokenclass(tok);
		if (lit.n != 0 && class != keyword && class != literal && tok != token_SEMICOLON)
			fail_test("tok with literal", token_Token_String(tok), lit);
		if (tok <= token_EOF)
			break;
	}
}

static void test_issue28112(void)
{
	static const char src[] = "... .. 0.. .."; /* stand-alone ".." immediately before EOF */
	static const token_Token want[] = {token_ELLIPSIS, token_PERIOD, token_PERIOD, token_FLOAT,
					   token_PERIOD,   token_PERIOD, token_PERIOD, token_EOF};
	scanner_Scanner s = {0};
	scanner_Scanner_Init(&s, addFile("", (int)sizeof src - 1),
			     GOSTR(src), NULL, NULL, 0);
	for (size_t i = 0; i < LEN(want); i++) {
		token_Pos pos;
		GoStr lit;
		token_Token got = scanner_Scanner_Scan(&s, &pos, &lit);
		EXPECT_INT("tok", got, want[i]);
		if (tokenclass(got) == literal)
			CHECK(lit.n != 0);
	}
}

static const struct {
	token_Token tok;
	const char *src, *tokens, *err;
} numbers[] = {
	{token_INT, "0b0", "0b0", ""},
	{token_INT, "0b1010", "0b1010", ""},
	{token_INT, "0B1110", "0B1110", ""},
	{token_INT, "0b", "0b", "binary literal has no digits"},
	{token_INT, "0b0190", "0b0190", "invalid digit '9' in binary literal"},
	{token_INT, "0b01a0", "0b01 a0", ""},
	{token_FLOAT, "0b.", "0b.", "invalid radix point in binary literal"},
	{token_FLOAT, "0b.1", "0b.1", "invalid radix point in binary literal"},
	{token_FLOAT, "0b1.0", "0b1.0", "invalid radix point in binary literal"},
	{token_FLOAT, "0b1e10", "0b1e10", "'e' exponent requires decimal mantissa"},
	{token_FLOAT, "0b1P-1", "0b1P-1", "'P' exponent requires hexadecimal mantissa"},
	{token_IMAG, "0b10i", "0b10i", ""},
	{token_IMAG, "0b10.0i", "0b10.0i", "invalid radix point in binary literal"},
	{token_INT, "0o0", "0o0", ""},
	{token_INT, "0o1234", "0o1234", ""},
	{token_INT, "0O1234", "0O1234", ""},
	{token_INT, "0o", "0o", "octal literal has no digits"},
	{token_INT, "0o8123", "0o8123", "invalid digit '8' in octal literal"},
	{token_INT, "0o1293", "0o1293", "invalid digit '9' in octal literal"},
	{token_INT, "0o12a3", "0o12 a3", ""},
	{token_FLOAT, "0o.", "0o.", "invalid radix point in octal literal"},
	{token_FLOAT, "0o.2", "0o.2", "invalid radix point in octal literal"},
	{token_FLOAT, "0o1.2", "0o1.2", "invalid radix point in octal literal"},
	{token_FLOAT, "0o1E+2", "0o1E+2", "'E' exponent requires decimal mantissa"},
	{token_FLOAT, "0o1p10", "0o1p10", "'p' exponent requires hexadecimal mantissa"},
	{token_IMAG, "0o10i", "0o10i", ""},
	{token_IMAG, "0o10e0i", "0o10e0i", "'e' exponent requires decimal mantissa"},
	{token_INT, "0", "0", ""},
	{token_INT, "0123", "0123", ""},
	{token_INT, "08123", "08123", "invalid digit '8' in octal literal"},
	{token_INT, "01293", "01293", "invalid digit '9' in octal literal"},
	{token_INT, "0F.", "0 F .", ""},
	{token_INT, "0123F.", "0123 F .", ""},
	{token_INT, "0123456x", "0123456 x", ""},
	{token_INT, "1", "1", ""},
	{token_INT, "1234", "1234", ""},
	{token_INT, "1f", "1 f", ""},
	{token_IMAG, "0i", "0i", ""},
	{token_IMAG, "0678i", "0678i", ""},
	{token_FLOAT, "0.", "0.", ""},
	{token_FLOAT, "123.", "123.", ""},
	{token_FLOAT, "0123.", "0123.", ""},
	{token_FLOAT, ".0", ".0", ""},
	{token_FLOAT, ".123", ".123", ""},
	{token_FLOAT, ".0123", ".0123", ""},
	{token_FLOAT, "0.0", "0.0", ""},
	{token_FLOAT, "123.123", "123.123", ""},
	{token_FLOAT, "0123.0123", "0123.0123", ""},
	{token_FLOAT, "0e0", "0e0", ""},
	{token_FLOAT, "123e+0", "123e+0", ""},
	{token_FLOAT, "0123E-1", "0123E-1", ""},
	{token_FLOAT, "0.e+1", "0.e+1", ""},
	{token_FLOAT, "123.E-10", "123.E-10", ""},
	{token_FLOAT, "0123.e123", "0123.e123", ""},
	{token_FLOAT, ".0e-1", ".0e-1", ""},
	{token_FLOAT, ".123E+10", ".123E+10", ""},
	{token_FLOAT, ".0123E123", ".0123E123", ""},
	{token_FLOAT, "0.0e1", "0.0e1", ""},
	{token_FLOAT, "123.123E-10", "123.123E-10", ""},
	{token_FLOAT, "0123.0123e+456", "0123.0123e+456", ""},
	{token_FLOAT, "0e", "0e", "exponent has no digits"},
	{token_FLOAT, "0E+", "0E+", "exponent has no digits"},
	{token_FLOAT, "1e+f", "1e+ f", "exponent has no digits"},
	{token_FLOAT, "0p0", "0p0", "'p' exponent requires hexadecimal mantissa"},
	{token_FLOAT, "1.0P-1", "1.0P-1", "'P' exponent requires hexadecimal mantissa"},
	{token_IMAG, "0.i", "0.i", ""},
	{token_IMAG, ".123i", ".123i", ""},
	{token_IMAG, "123.123i", "123.123i", ""},
	{token_IMAG, "123e+0i", "123e+0i", ""},
	{token_IMAG, "123.E-10i", "123.E-10i", ""},
	{token_IMAG, ".123E+10i", ".123E+10i", ""},
	{token_INT, "0x0", "0x0", ""},
	{token_INT, "0x1234", "0x1234", ""},
	{token_INT, "0xcafef00d", "0xcafef00d", ""},
	{token_INT, "0XCAFEF00D", "0XCAFEF00D", ""},
	{token_INT, "0x", "0x", "hexadecimal literal has no digits"},
	{token_INT, "0x1g", "0x1 g", ""},
	{token_IMAG, "0xf00i", "0xf00i", ""},
	{token_FLOAT, "0x0p0", "0x0p0", ""},
	{token_FLOAT, "0x12efp-123", "0x12efp-123", ""},
	{token_FLOAT, "0xABCD.p+0", "0xABCD.p+0", ""},
	{token_FLOAT, "0x.0189P-0", "0x.0189P-0", ""},
	{token_FLOAT, "0x1.ffffp+1023", "0x1.ffffp+1023", ""},
	{token_FLOAT, "0x.", "0x.", "hexadecimal literal has no digits"},
	{token_FLOAT, "0x0.", "0x0.", "hexadecimal mantissa requires a 'p' exponent"},
	{token_FLOAT, "0x.0", "0x.0", "hexadecimal mantissa requires a 'p' exponent"},
	{token_FLOAT, "0x1.1", "0x1.1", "hexadecimal mantissa requires a 'p' exponent"},
	{token_FLOAT, "0x1.1e0", "0x1.1e0", "hexadecimal mantissa requires a 'p' exponent"},
	{token_FLOAT, "0x1.2gp1a", "0x1.2 gp1a", "hexadecimal mantissa requires a 'p' exponent"},
	{token_FLOAT, "0x0p", "0x0p", "exponent has no digits"},
	{token_FLOAT, "0xeP-", "0xeP-", "exponent has no digits"},
	{token_FLOAT, "0x1234PAB", "0x1234P AB", "exponent has no digits"},
	{token_FLOAT, "0x1.2p1a", "0x1.2p1 a", ""},
	{token_IMAG, "0xf00.bap+12i", "0xf00.bap+12i", ""},
	{token_INT, "0b_1000_0001", "0b_1000_0001", ""},
	{token_INT, "0o_600", "0o_600", ""},
	{token_INT, "0_466", "0_466", ""},
	{token_INT, "1_000", "1_000", ""},
	{token_FLOAT, "1_000.000_1", "1_000.000_1", ""},
	{token_IMAG, "10e+1_2_3i", "10e+1_2_3i", ""},
	{token_INT, "0x_f00d", "0x_f00d", ""},
	{token_FLOAT, "0x_f00d.0p1_2", "0x_f00d.0p1_2", ""},
	{token_INT, "0b__1000", "0b__1000", "'_' must separate successive digits"},
	{token_INT, "0o60___0", "0o60___0", "'_' must separate successive digits"},
	{token_INT, "0466_", "0466_", "'_' must separate successive digits"},
	{token_FLOAT, "1_.", "1_.", "'_' must separate successive digits"},
	{token_FLOAT, "0._1", "0._1", "'_' must separate successive digits"},
	{token_FLOAT, "2.7_e0", "2.7_e0", "'_' must separate successive digits"},
	{token_IMAG, "10e+12_i", "10e+12_i", "'_' must separate successive digits"},
	{token_INT, "0x___0", "0x___0", "'_' must separate successive digits"},
	{token_FLOAT, "0x1.0_p0", "0x1.0_p0", "'_' must separate successive digits"},
};

static void eh_first(void *ctx, token_Position pos, GoStr msg)
{
	GoStr *err = ctx;
	if (err->n == 0)
		*err = msg;
}

static void test_numbers(void)
{
	for (size_t k = 0; k < LEN(numbers); k++) {
		GoStr src = gostr_c(numbers[k].src);
		scanner_Scanner s = {0};
		GoStr err = {0};
		scanner_Scanner_Init(&s, addFile("", src.n), src,
				     eh_first, &err, 0);
		GoStrVec wants = strings_Split(gostr_c(numbers[k].tokens), GOSTR(" "));
		for (int i = 0; i < wants.n; i++) {
			err = (GoStr){0};
			token_Pos pos;
			GoStr lit;
			token_Token tok = scanner_Scanner_Scan(&s, &pos, &lit);

			/* compute lit where for tokens where lit is not defined */
			switch (tok) {
			case token_PERIOD:
				lit = GOSTR(".");
				break;
			case token_ADD:
				lit = GOSTR("+");
				break;
			case token_SUB:
				lit = GOSTR("-");
				break;
			default:
				break;
			}

			if (i == 0) {
				EXPECT_INT("tok", tok, numbers[k].tok);
				EXPECT_STR("err", err, gostr_c(numbers[k].err));
			}
			EXPECT_STR("literal", lit, wants.v[i]);
		}

		/* make sure we read all */
		token_Pos pos;
		GoStr lit;
		token_Token tok = scanner_Scanner_Scan(&s, &pos, &lit);
		if (tok == token_SEMICOLON)
			tok = scanner_Scanner_Scan(&s, &pos, &lit);
		EXPECT_INT("EOF", tok, token_EOF);
	}
}

/* filepath helpers used for //line directives (Unix semantics). */
static void test_filepath(void)
{
	static const char *const clean[][2] = {
		{"", "."},           {"abc", "abc"},         {"a/b/c", "a/b/c"},       {"/", "/"},
		{"abc/", "abc"},     {"a//b", "a/b"},        {"./a", "a"},             {"a/./b", "a/b"},
		{"a/..", "."},       {"a/../..", ".."},      {"../../a/b/../c", "../../a/c"},
		{"/../a", "/a"},     {"/a/b/../../..", "/"}, {"a/b/../../../c/./d/", "../c/d"},
	};
	for (size_t i = 0; i < LEN(clean); i++)
		EXPECT_STR("Clean", scanner_filepathClean(gostr_c(clean[i][0])), gostr_c(clean[i][1]));
	EXPECT_STR("Join", scanner_filepathJoin(GOSTR("dir/"), GOSTR("../x.go")), GOSTR("x.go"));
	EXPECT_STR("Join", scanner_filepathJoin(GOSTR(""), GOSTR("./x.go")), GOSTR("x.go"));
	EXPECT_STR("Dir", scanner_filepathDir(GOSTR("a/b/c.go")), GOSTR("a/b/"));
	EXPECT_STR("Dir", scanner_filepathDir(GOSTR("c.go")), GOSTR(""));
}

TESTS(TEST(test_scan), TEST(test_strip_cr), TEST(test_semicolons), TEST(test_line_directives),
      TEST(test_invalid_line_directives), TEST(test_init), TEST(test_std_error_handler), TEST(test_scan_errors),
      TEST(test_utf16), TEST(test_issue10213), TEST(test_issue28112), TEST(test_numbers), TEST(test_filepath))
