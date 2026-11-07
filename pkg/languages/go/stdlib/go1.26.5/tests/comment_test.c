/* Tests of go/doc/comment: runs the "gofmt" (Printer.Comment) and "dump" sections of Go's
 * src/go/doc/comment/testdata/ *.txt (copied verbatim into testdata/comment/; BSD-3-Clause,
 * Copyright The Go Authors) like go1.26.5 src/go/doc/comment/testdata_test.go. The html,
 * markdown and text sections are ignored (those printers are not ported). */
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stdio.h>
#include <stdlib.h>

#include "internal.h"

#include "strings/strings.c"
#include "unicode/utf8/utf8.c"
#include "unicode/tables.c"
#include "unicode/unicode.c"
#include "strconv/quote.c"
#include "go/doc/comment/std.c"
#include "go/doc/comment/parse.c"
#include "go/doc/comment/print.c"

#include "../../../../../tests/unit/test.h"

static const char *const files[] = {
	"blank.txt", "code2.txt", "code3.txt", "code4.txt", "code5.txt", "code6.txt", "code.txt",
	"crash1.txt", "doclink.txt", "escape.txt", "head2.txt", "head.txt", "hello.txt", "link2.txt",
	"link3.txt", "link4.txt", "link5.txt", "link6.txt", "link7.txt", "link.txt", "list10.txt",
	"list2.txt", "list3.txt", "list4.txt", "list5.txt", "list6.txt", "list7.txt", "list8.txt",
	"list9.txt", "list.txt", "para.txt", "quote.txt", "text9.txt", "text.txt", "words.txt",
};

static char *read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	Buf b = {0};
	char tmp[65536];
	size_t r;
	while ((r = fread(tmp, 1, sizeof tmp, f)) > 0)
		buf_add(&b, tmp, r);
	fclose(f);
	*n = b.n;
	return b.p ? b.p : xstrdup("");
}

/* A txtar archive (golang.org/x/tools/txtar, internal/txtar): files introduced by
 * "-- name --" lines. */
typedef struct {
	GoStr name, data;
} txtar_File;

typedef struct {
	GoStr comment;
	txtar_File files[16];
	int n;
} txtar_Archive;

/* isMarker: "-- name --" line at the start of data; returns name and the rest after the line. */
static bool txtar_isMarker(GoStr data, GoStr *name, GoStr *after)
{
	if (!strings_HasPrefix(data, GOSTR("-- ")))
		return false;
	GoStr line = data;
	*after = (GoStr){0};
	int i = strings_IndexByte(data, '\n');
	if (i >= 0) {
		line = gostr_sub(data, 0, i);
		*after = gostr_from(data, i + 1);
	}
	if (!(strings_HasSuffix(line, GOSTR(" --")) && line.n >= 6))
		return false;
	*name = strings_TrimSpace(gostr_sub(line, 3, line.n - 3));
	return true;
}

/* findFileMarker: the data before the next marker line, the marker name and what follows. */
static GoStr txtar_findFileMarker(GoStr data, GoStr *name, GoStr *after)
{
	int i = 0;
	for (;;) {
		if (txtar_isMarker(gostr_from(data, i), name, after))
			return gostr_sub(data, 0, i);
		int j = strings_Index(gostr_from(data, i), GOSTR("\n-- "));
		if (j < 0) {
			*name = (GoStr){0};
			*after = (GoStr){0};
			return data;
		}
		i += j + 1;
	}
}

static GoStr txtar_fixNL(GoStr data)
{
	if (data.n == 0 || data.p[data.n - 1] == '\n')
		return data;
	return gostr_cat(data, GOSTR("\n"));
}

static void txtar_Parse(GoStr data, txtar_Archive *a)
{
	GoStr name, after;
	a->n = 0;
	a->comment = txtar_fixNL(txtar_findFileMarker(data, &name, &after));
	while (name.n != 0 && a->n < 16) {
		txtar_File *f = &a->files[a->n++];
		f->name = name;
		GoStr rest = after;
		f->data = txtar_fixNL(txtar_findFileMarker(rest, &name, &after));
	}
}

/* Remove trailing $ on lines (they make lines with trailing spaces visible). */
static GoStr stripDollars(GoStr b) { return strings_ReplaceAll(b, GOSTR("$\n"), GOSTR("\n")); }

static const comment_Word testWords[] = {
	{GOSTR("italicword"), GOSTR("")},
	{GOSTR("linkedword"), GOSTR("https://example.com/linkedword")},
};

static bool testLookupPackage(GoStr name, GoStr *importPath)
{
	if (gostr_eqc(name, "comment")) {
		*importPath = GOSTR("go/doc/comment");
		return true;
	}
	return comment_DefaultLookupPackage(name, importPath);
}

static bool testLookupSym(GoStr recv, GoStr name)
{
	return (gostr_eqc(recv, "Parser") && gostr_eqc(name, "Parse")) ||
	       (gostr_eqc(recv, "") && gostr_eqc(name, "Doc")) || (gostr_eqc(recv, "") && gostr_eqc(name, "NoURL"));
}

/* ---- dump (testdata_test.go) ---- */

static void dumpNL(Buf *out, int n)
{
	buf_addc(out, '\n');
	for (int i = 0; i < n; i++)
		buf_addc(out, '\t');
}

static void dumpString(Buf *out, int indent, GoStr x)
{
	/* strings.SplitAfter(x, "\n") */
	while (x.n > 0) {
		int i = strings_IndexByte(x, '\n');
		GoStr line = i < 0 ? x : gostr_sub(x, 0, i + 1);
		x = gostr_from(x, line.n);
		dumpNL(out, indent);
		buf_addg(out, strconv_Quote(line));
	}
}

static void dumpTexts(Buf *out, int indent, Vec x);
static void dumpBlocks(Buf *out, int indent, Vec x);

static void dumpPlain(Buf *out, int indent, const char *what, GoStr s)
{
	if (!strings_Contains(s, GOSTR("\n"))) {
		buf_fmt(out, "%s ", what);
		buf_addg(out, strconv_Quote(s));
	} else {
		buf_adds(out, what);
		dumpString(out, indent + 1, s);
	}
}

static void dumpTexts(Buf *out, int indent, Vec x)
{
	for (int i = 0; i < x.n; i++) {
		comment_Text *t = x.v[i];
		dumpNL(out, indent);
		switch (t->kind) {
		case comment_KindPlain:
			dumpPlain(out, indent, "Plain", ((comment_Plain *)t)->s);
			break;
		case comment_KindItalic:
			dumpPlain(out, indent, "Italic", ((comment_Italic *)t)->s);
			break;
		case comment_KindLink:
			buf_adds(out, "Link ");
			buf_addg(out, strconv_Quote(((comment_Link *)t)->URL));
			dumpTexts(out, indent + 1, ((comment_Link *)t)->Text);
			break;
		case comment_KindDocLink: {
			comment_DocLink *l = (comment_DocLink *)t;
			buf_adds(out, "DocLink pkg:");
			buf_addg(out, strconv_Quote(l->ImportPath));
			buf_adds(out, ", recv:");
			buf_addg(out, strconv_Quote(l->Recv));
			buf_adds(out, ", name:");
			buf_addg(out, strconv_Quote(l->Name));
			dumpTexts(out, indent + 1, l->Text);
			break;
		}
		}
	}
}

static void dumpBlocks(Buf *out, int indent, Vec x)
{
	for (int i = 0; i < x.n; i++) {
		comment_Block *b = x.v[i];
		dumpNL(out, indent);
		switch (b->kind) {
		case comment_KindHeading:
			buf_adds(out, "Heading");
			dumpTexts(out, indent + 1, ((comment_Heading *)b)->Text);
			break;
		case comment_KindList: {
			comment_List *l = (comment_List *)b;
			buf_fmt(out, "List ForceBlankBefore=%s ForceBlankBetween=%s", l->ForceBlankBefore ? "true" : "false",
				l->ForceBlankBetween ? "true" : "false");
			for (int j = 0; j < l->Items.n; j++) {
				comment_ListItem *item = l->Items.v[j];
				dumpNL(out, indent + 1);
				buf_adds(out, "Item Number=");
				buf_addg(out, strconv_Quote(item->Number));
				dumpBlocks(out, indent + 2, item->Content);
			}
			break;
		}
		case comment_KindParagraph:
			buf_adds(out, "Paragraph");
			dumpTexts(out, indent + 1, ((comment_Paragraph *)b)->Text);
			break;
		case comment_KindCode:
			buf_adds(out, "Code");
			dumpString(out, indent + 1, ((comment_Code *)b)->Text);
			break;
		}
	}
}

static GoStr dump(comment_Doc *d)
{
	Buf out = {0};
	buf_adds(&out, "Doc");
	dumpBlocks(&out, 1, d->Content);
	if (d->Links.n > 0) {
		dumpNL(&out, 1);
		buf_adds(&out, "Links");
		for (int i = 0; i < d->Links.n; i++) {
			comment_LinkDef *def = d->Links.v[i];
			dumpNL(&out, 2);
			buf_fmt(&out, "LinkDef Used:%s Text:", def->Used ? "true" : "false");
			buf_addg(&out, strconv_Quote(def->Text));
			buf_adds(&out, " URL:");
			buf_addg(&out, def->URL);
		}
	}
	buf_adds(&out, "\n");
	return gostr_buf(&out);
}

/* ---- tests ---- */

static char testdata_dir[4096];

static void init_testdata_dir(void)
{
	/* testdata/comment next to this source file. __FILE__ is relative to the source root in
	 * builds with -ffile-prefix-map (ctest runs this test there), absolute otherwise. */
	const char *self = __FILE__;
	const char *slash = strrchr(self, '/');
	int n = slash ? (int)(slash - self) + 1 : 0;
	snprintf(testdata_dir, sizeof testdata_dir, "%.*stestdata/comment/", n, self);
}

static int ncompared;

static void run_file(const char *file)
{
	char path[8192];
	snprintf(path, sizeof path, "%s%s", testdata_dir, file);
	size_t n;
	char *data = read_file(path, &n);
	if (!data) {
		fprintf(stderr, "%s: cannot read\n", path);
		uni_test_failed = 1;
		return;
	}
	txtar_Archive a;
	txtar_Parse(gostr(data, (int)n), &a);
	if (a.n < 1 || !gostr_eqc(a.files[0].name, "input")) {
		fprintf(stderr, "%s: first file is not \"input\"\n", file);
		uni_test_failed = 1;
		return;
	}
	comment_Parser p = {0};
	p.Words = testWords;
	p.nWords = (int)(sizeof testWords / sizeof *testWords);
	p.LookupPackage = testLookupPackage;
	p.LookupSym = testLookupSym;
	/* not ported: the JSON Printer config before "-- input --" (only used by html/markdown/text) */
	comment_Printer pr = {0};
	comment_Doc *d = comment_Parser_Parse(&p, stripDollars(a.files[0].data));
	for (int i = 1; i < a.n; i++) {
		GoStr want = stripDollars(a.files[i].data);
		while (want.n >= 2 && want.p[want.n - 1] == '\n' && want.p[want.n - 2] == '\n')
			want.n--;
		GoStr out;
		if (gostr_eqc(a.files[i].name, "gofmt"))
			out = comment_Printer_Comment(&pr, d);
		else if (gostr_eqc(a.files[i].name, "dump"))
			out = dump(d);
		else
			continue;
		ncompared++;
		if (!gostr_eq(out, want)) {
			fprintf(stderr, "%s: %.*s mismatch\n", file, GOSTR_FMT(a.files[i].name));
			CHECK_MEM(out.p, (size_t)out.n, want.p, (size_t)want.n);
		}
	}
}

static void test_testdata(void)
{
	init_testdata_dir();
	for (size_t i = 0; i < sizeof files / sizeof *files; i++)
		run_file(files[i]);
	CHECK(ncompared == 37); /* 35 gofmt + 2 dump sections */
}

/* Zero Parser (as go/printer uses it): no Words, DefaultLookupPackage, no LookupSym. Expected
 * outputs recorded from Go 1.26.5. */
static void test_zero_parser(void)
{
	static const struct {
		const char *in, *want;
	} cases[] = {
		{"", ""},
		{"\n\n", ""},
		{"Hello.\n", "Hello.\n"},
		{" Indented\n code\n", "Indented\ncode\n"},
		{"Text.\n\n\tcode()\n\nMore.", "Text.\n\n\tcode()\n\nMore.\n"},
		{"See [io.Reader] and [Doc] and [math].\n", "See [io.Reader] and [Doc] and [math].\n"},
		{"Text\n- a\n- b\n", "Text\n- a\n- b\n"},
		{"Use ``x'' here.\n", "Use “x” here.\n"},
		{"Unused.\n\n[x]: https://x.org\n", "Unused.\n\n[x]: https://x.org\n"},
		{"Heading\n\nOld Heading\n\nText.\n", "Heading\n\n# Old Heading\n\nText.\n"},
		{"func f() {\n\tx\n}\n", "\tfunc f() {\n\t\tx\n\t}\n"},
		{"Text\n - a\n - b\n", "Text\n  - a\n  - b\n"},
		{"Doc\n\n- a\n  b\n- c\n", "Doc\n\n  - a\n    b\n  - c\n"},
		{"Link [x] here.\n\n[x]: https://x.org\n[y]: http://y\n", "Link [x] here.\n\n[x]: https://x.org\n\n[y]: http://y\n"},
		{"See https://go.dev/doc. Done\n", "See https://go.dev/doc. Done\n"},
		{"# Title\n\n\tcode\nnot code\n", "# Title\n\n\tcode\n\nnot code\n"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		comment_Parser p = {0};
		comment_Printer pr = {0};
		GoStr out = comment_Printer_Comment(&pr, comment_Parser_Parse(&p, gostr_c(cases[i].in)));
		CHECK_MEM(out.p, (size_t)out.n, cases[i].want, strlen(cases[i].want));
	}
}

TESTS(TEST(test_testdata), TEST(test_zero_parser))
