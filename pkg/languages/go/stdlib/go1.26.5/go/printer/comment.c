/* Copyright 2022 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * Ported from go1.26.5 src/go/printer/comment.go. */
#include "../../internal.h"

static bool printer_isDirective(GoStr c);
static bool printer_allStars(GoStr text);

static ast_Comment *printer_newComment(token_Pos slash, GoStr text)
{
	ast_Comment *c = AST_NEW(Comment);
	c->Slash = slash;
	c->Text = text;
	return c;
}

/* formatDocComment reformats the doc comment list (Vec of ast_Comment *), returning the
 * canonical formatting. */
static Vec printer_formatDocComment(Vec list)
{
	/* Extract comment text (removing comment markers). */
	GoStr kind, text;
	Vec directives = {0};
	ast_Comment *first = list.v[0];
	if (list.n == 1 && strings_HasPrefix(first->Text, GOSTR("/*"))) {
		kind = GOSTR("/*");
		text = first->Text;
		if (!strings_Contains(text, GOSTR("\n")) || printer_allStars(text)) {
			/* Single-line / * .. * / comment in doc comment position, or multiline old-style
			 * comment like
			 *	/ *
			 *	 * Comment
			 *	 * text here.
			 *	 * /
			 * Should not happen, since it will not work well as a doc comment, but if it
			 * does, just ignore: reformatting it will only make the situation worse. */
			return list;
		}
		text = gostr_sub(text, 2, text.n - 2); /* cut / * and * / */
	} else if (strings_HasPrefix(first->Text, GOSTR("//"))) {
		kind = GOSTR("//");
		Buf b = {0};
		for (int i = 0; i < list.n; i++) {
			ast_Comment *c = list.v[i];
			GoStr after;
			if (!strings_CutPrefix(c->Text, GOSTR("//"), &after))
				return list;
			/* Accumulate //go:build etc lines separately. */
			if (printer_isDirective(after)) {
				vec_push(&directives, c);
				continue;
			}
			buf_addg(&b, strings_TrimPrefix(after, GOSTR(" ")));
			buf_adds(&b, "\n");
		}
		text = gostr_buf(&b);
	} else {
		/* Not sure what this is, so leave alone. */
		return list;
	}

	if (text.n == 0)
		return list;

	/* Parse comment and reformat as text. */
	comment_Parser p = {0};
	comment_Doc *d = comment_Parser_Parse(&p, text);

	comment_Printer pr = {0};
	text = comment_Printer_Comment(&pr, d);

	/* For / * * / comment, return one big comment with text inside. */
	token_Pos slash = first->Slash;
	Vec out = {0};
	if (gostr_eqc(kind, "/*")) {
		vec_push(&out, printer_newComment(slash, gostr_cat(gostr_cat(GOSTR("/*\n"), text), GOSTR("*/"))));
		return out;
	}

	/* For // comment, return sequence of // lines. */
	while (text.n != 0) {
		GoStr line;
		if (!strings_Cut(text, GOSTR("\n"), &line, &text)) {
			line = text;
			text = GOSTR("");
		}
		if (line.n == 0)
			line = GOSTR("//");
		else if (strings_HasPrefix(line, GOSTR("\t")))
			line = gostr_cat(GOSTR("//"), line);
		else
			line = gostr_cat(GOSTR("// "), line);
		vec_push(&out, printer_newComment(slash, line));
	}
	if (directives.n > 0) {
		vec_push(&out, printer_newComment(slash, GOSTR("//")));
		for (int i = 0; i < directives.n; i++)
			vec_push(&out, printer_newComment(slash, VEC_AT(directives, ast_Comment, i)->Text));
	}
	return out;
}

/* isDirective reports whether c is a comment directive. See go.dev/issue/37974. This code is
 * also in go/ast. */
static bool printer_isDirective(GoStr c)
{
	/* "//line " is a line directive. "//extern " is for gccgo. "//export " is for cgo. (The //
	 * has been removed.) */
	if (strings_HasPrefix(c, GOSTR("line ")) || strings_HasPrefix(c, GOSTR("extern ")) ||
	    strings_HasPrefix(c, GOSTR("export ")))
		return true;

	/* "//[a-z0-9]+:[a-z0-9]" (The // has been removed.) */
	int colon = strings_Index(c, GOSTR(":"));
	if (colon <= 0 || colon + 1 >= c.n)
		return false;
	for (int i = 0; i <= colon + 1; i++) {
		if (i == colon)
			continue;
		char b = c.p[i];
		if (!(('a' <= b && b <= 'z') || ('0' <= b && b <= '9')))
			return false;
	}
	return true;
}

/* allStars reports whether text is the interior of an old-style / * * / comment with a star at
 * the start of each line. */
static bool printer_allStars(GoStr text)
{
	for (int i = 0; i < text.n; i++) {
		if (text.p[i] == '\n') {
			int j = i + 1;
			while (j < text.n && (text.p[j] == ' ' || text.p[j] == '\t'))
				j++;
			if (j < text.n && text.p[j] != '*')
				return false;
		}
	}
	return true;
}
