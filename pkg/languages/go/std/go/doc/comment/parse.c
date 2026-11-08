/* Package go/doc/comment, ported from go1.27.1 src/go/doc/comment/parse.go.
 * Copyright 2022 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 * The types (Doc, LinkDef, Block, Heading, List, ListItem, Paragraph, Code, Text, Plain, Italic,
 * Link, DocLink, Parser) are declared in comment.h. */
#include "../../../internal.h"

/* not ported: the block() and text() interface marker methods (kind fields in comment.h) */

/* BlankBetween reports whether a reformatting of the comment should include a blank line
 * between each pair of list items: if the list item content contains any blank lines (meaning
 * at least one item has multiple paragraphs) or ForceBlankBetween is set. */
static bool comment_List_BlankBetween(const comment_List *l)
{
	if (l->ForceBlankBetween)
		return true;
	for (int i = 0; i < l->Items.n; i++) {
		comment_ListItem *item = VEC_AT(l->Items, comment_ListItem, i);
		if (item->Content.n != 1) {
			/* Unreachable for parsed comments today, since the only way to get multiple
			 * item.Content is multiple paragraphs, which must have been separated by a
			 * blank line. */
			return true;
		}
	}
	return false;
}

/* BlankBefore reports whether a reformatting of the comment should include a blank line before
 * the list. The default rule is the same as for BlankBetween; a preceding blank line can be
 * forced by setting ForceBlankBefore. */
static bool comment_List_BlankBefore(const comment_List *l) { return l->ForceBlankBefore || comment_List_BlankBetween(l); }

static comment_Text *comment_newPlain(GoStr s)
{
	comment_Plain *t = xalloc(sizeof *t);
	t->t.kind = comment_KindPlain;
	t->s = s;
	return &t->t;
}

static comment_Text *comment_newItalic(GoStr s)
{
	comment_Italic *t = xalloc(sizeof *t);
	t->t.kind = comment_KindItalic;
	t->s = s;
	return &t->t;
}

/* lines[i:j] (a view: never append to it). */
static GoStrVec comment_lines(GoStrVec lines, int i, int j)
{
	if (i < 0 || j < i || j > lines.n)
		fail("go: slice bounds out of range [%d:%d] with length %d", i, j, lines.n);
	return (GoStrVec){lines.v + i, j - i, j - i};
}

/* parseDoc is parsing state for a single doc comment. */
typedef struct {
	comment_Parser *Parser;
	comment_Doc *Doc;
	Vec links; /* map[string]*LinkDef: the first definition of each link text */
	GoStrVec lines;
	bool (*lookupSym)(GoStr recv, GoStr name);
} comment_parseDoc;

static bool comment_isList(GoStr line);
static bool comment_isHeading(GoStr line);
static bool comment_isOldHeading(GoStr line, GoStrVec all, int off);
static Vec comment_parseDoc_parseLinkedText(comment_parseDoc *d, GoStr text);
static Vec comment_parseDoc_parseText(comment_parseDoc *d, Vec out, GoStr s, bool autoLink);
static bool comment_isScheme(GoStr s);
static bool comment_isName(GoStr s);
static bool comment_ident(GoStr s, GoStr *id);
static bool comment_isIdentASCII(unsigned char c);
static bool comment_validImportPath(GoStr path);
static bool comment_isStdPkg(GoStr path);

/* d.links[text] (nil if absent). */
static comment_LinkDef *comment_parseDoc_link(comment_parseDoc *d, GoStr text)
{
	for (int i = 0; i < d->links.n; i++) {
		comment_LinkDef *def = VEC_AT(d->links, comment_LinkDef, i);
		if (gostr_eq(def->Text, text))
			return def;
	}
	return NULL;
}

/* lookupPkg is called to look up the pkg in [pkg], [pkg.Name], and [pkg.Name.Recv]. If pkg has
 * a slash, it is assumed to be the full import path and is returned with ok = true.
 *
 * Otherwise, pkg is probably a simple package name like "rand"; d.LookupPackage provides a way
 * for the caller to resolve such names. Single-element standard library names like "math" are
 * full import paths without slashes: LookupPackage has the first chance to resolve them, and
 * otherwise we refer to a built-in list of single-element standard library package names. */
static bool comment_parseDoc_lookupPkg(comment_parseDoc *d, GoStr pkg, GoStr *importPath)
{
	if (strings_Contains(pkg, GOSTR("/"))) { /* assume a full import path */
		if (comment_validImportPath(pkg)) {
			*importPath = pkg;
			return true;
		}
		*importPath = (GoStr){0};
		return false;
	}
	if (d->Parser->LookupPackage != NULL) {
		/* Give LookupPackage a chance. */
		GoStr path = {0};
		if (d->Parser->LookupPackage(pkg, &path)) {
			*importPath = path;
			return true;
		}
	}
	return comment_DefaultLookupPackage(pkg, importPath);
}

static bool comment_isStdPkg(GoStr path)
{
	/* slices.BinarySearch(stdPkgs, path) */
	int lo = 0, hi = (int)(sizeof comment_stdPkgs / sizeof *comment_stdPkgs);
	while (lo < hi) {
		int h = (int)((unsigned)(lo + hi) >> 1);
		if (gostr_cmp(comment_stdPkgs[h], path) < 0)
			lo = h + 1;
		else
			hi = h;
	}
	return lo < (int)(sizeof comment_stdPkgs / sizeof *comment_stdPkgs) && gostr_eq(comment_stdPkgs[lo], path) &&
	       (go_std_rel->minor >= 27 || !gostr_eq(path, GOSTR("uuid")));
}

/* DefaultLookupPackage is the default package lookup function, used when Parser.LookupPackage
 * is nil. It recognizes names of the packages from the standard library with single-element
 * import paths, such as math, which would otherwise be impossible to name. */
static bool comment_DefaultLookupPackage(GoStr name, GoStr *importPath)
{
	if (comment_isStdPkg(name)) {
		*importPath = name;
		return true;
	}
	*importPath = (GoStr){0};
	return false;
}

static bool comment_lookupSymFalse(GoStr recv, GoStr name) { return false; }

/* A span represents a single span of comment lines (lines[start:end]) of an identified kind
 * (code, heading, paragraph, and so on). */
typedef enum {
	comment_spanCode = 1,
	comment_spanHeading,
	comment_spanList,
	comment_spanOldHeading,
	comment_spanPara,
} comment_spanKind;

typedef struct {
	int start;
	int end;
	comment_spanKind kind;
} comment_span;

typedef struct {
	comment_span *v;
	int n, cap;
} comment_spanVec;

static GoStrVec comment_unindent(GoStrVec lines);
static comment_spanVec comment_parseSpans(GoStrVec lines);
static comment_Block *comment_parseDoc_list(comment_parseDoc *d, GoStrVec lines, bool forceBlankBefore);
static comment_Block *comment_parseDoc_code(comment_parseDoc *d, GoStrVec lines);
static comment_Block *comment_parseDoc_oldHeading(comment_parseDoc *d, GoStr line);
static comment_Block *comment_parseDoc_heading(comment_parseDoc *d, GoStr line);
static comment_Block *comment_parseDoc_paragraph(comment_parseDoc *d, GoStrVec lines);

/* Parse parses the doc comment text and returns the *Doc form. Comment markers (/ * // and * /)
 * in the text must have already been removed. */
static comment_Doc *comment_Parser_Parse(comment_Parser *p, GoStr text)
{
	GoStrVec lines = comment_unindent(strings_Split(text, GOSTR("\n")));
	comment_parseDoc dd = {
		.Parser = p,
		.Doc = xalloc(sizeof(comment_Doc)),
		.lines = lines,
		.lookupSym = comment_lookupSymFalse,
	};
	comment_parseDoc *d = &dd;
	if (p->LookupSym != NULL)
		d->lookupSym = p->LookupSym;

	/* First pass: break into block structure and collect known links. The text is all
	 * recorded as Plain for now. */
	comment_span prev = {0};
	comment_spanVec spans = comment_parseSpans(lines);
	for (int k = 0; k < spans.n; k++) {
		comment_span s = spans.v[k];
		comment_Block *b = NULL;
		switch (s.kind) {
		default:
			fail("go/doc/comment: internal error: unknown span kind");
		case comment_spanList:
			b = comment_parseDoc_list(d, comment_lines(lines, s.start, s.end), prev.end < s.start);
			break;
		case comment_spanCode:
			b = comment_parseDoc_code(d, comment_lines(lines, s.start, s.end));
			break;
		case comment_spanOldHeading:
			b = comment_parseDoc_oldHeading(d, lines.v[s.start]);
			break;
		case comment_spanHeading:
			b = comment_parseDoc_heading(d, lines.v[s.start]);
			break;
		case comment_spanPara:
			b = comment_parseDoc_paragraph(d, comment_lines(lines, s.start, s.end));
			break;
		}
		if (b != NULL)
			vec_push(&d->Doc->Content, b);
		prev = s;
	}

	/* Second pass: interpret all the Plain text now that we know the links. */
	for (int k = 0; k < d->Doc->Content.n; k++) {
		comment_Block *b = d->Doc->Content.v[k];
		switch (b->kind) {
		case comment_KindParagraph: {
			comment_Paragraph *para = (comment_Paragraph *)b;
			para->Text = comment_parseDoc_parseLinkedText(d, VEC_AT(para->Text, comment_Plain, 0)->s);
			break;
		}
		case comment_KindList: {
			comment_List *list = (comment_List *)b;
			for (int i = 0; i < list->Items.n; i++) {
				comment_ListItem *item = VEC_AT(list->Items, comment_ListItem, i);
				for (int j = 0; j < item->Content.n; j++) {
					comment_Paragraph *para = VEC_AT(item->Content, comment_Paragraph, j);
					para->Text = comment_parseDoc_parseLinkedText(d, VEC_AT(para->Text, comment_Plain, 0)->s);
				}
			}
			break;
		}
		default:
			break;
		}
	}

	return d->Doc;
}

static void comment_spanVec_push(comment_spanVec *v, comment_span s)
{
	if (v->n == v->cap) {
		v->v = go_grow(v->v, &v->cap, 16, sizeof *v->v);
	}
	v->v[v->n++] = s;
}

static bool comment_indented(GoStr line);

static comment_spanVec comment_parseSpans(GoStrVec lines)
{
	comment_spanVec spans = {0};

	/* The loop may process a line twice: once as unindented and again forced indented. So
	 * the maximum expected number of iterations is 2*len(lines); a panic is better than a
	 * quiet infinite loop. */
	int watchdog = 2 * lines.n;

	int i = 0;
	int forceIndent = 0;
	for (;;) { /* Spans: */
		/* Skip blank lines. */
		while (i < lines.n && lines.v[i].n == 0)
			i++;
		if (i >= lines.n)
			break;
		if (--watchdog < 0)
			fail("go/doc/comment: internal error: not making progress");

		comment_spanKind kind;
		int start = i;
		int end = i;
		if (i < forceIndent || comment_indented(lines.v[i])) {
			/* Indented (or force indented). Ends before next unindented. (Blank lines are
			 * OK.) If this is an unindented list that we are heuristically treating as
			 * indented, then accept unindented list item lines up to the first blank
			 * lines. The heuristic is disabled at blank lines to contain its effect to
			 * non-gofmt'ed sections of the comment. */
			bool unindentedListOK = comment_isList(lines.v[i]) && i < forceIndent;
			i++;
			while (i < lines.n && (lines.v[i].n == 0 || i < forceIndent || comment_indented(lines.v[i]) ||
					       (unindentedListOK && comment_isList(lines.v[i])))) {
				if (lines.v[i].n == 0)
					unindentedListOK = false;
				i++;
			}

			/* Drop trailing blank lines. */
			end = i;
			while (end > start && lines.v[end - 1].n == 0)
				end--;

			/* If indented lines are followed (without a blank line) by an unindented line
			 * ending in a brace, take that one line too. This fixes the common mistake of
			 * pasting in something like
			 *
			 * func main() {
			 *	fmt.Println("hello, world")
			 * }
			 *
			 * and forgetting to indent it. The heuristic will never trigger on a gofmt'ed
			 * comment, because any gofmt'ed code block or list would be followed by a
			 * blank line or end of comment. */
			if (end < lines.n && strings_HasPrefix(lines.v[end], GOSTR("}")))
				end++;

			if (comment_isList(lines.v[start]))
				kind = comment_spanList;
			else
				kind = comment_spanCode;
		} else {
			/* Unindented. Ends at next blank or indented line. */
			i++;
			while (i < lines.n && lines.v[i].n != 0 && !comment_indented(lines.v[i]))
				i++;
			end = i;

			/* If unindented lines are followed (without a blank line) by an indented line
			 * that would start a code block, check whether the final unindented lines
			 * should be left for the indented section. This can happen for the common
			 * mistakes of unindented code or unindented lists. The heuristic will never
			 * trigger on a gofmt'ed comment, because any gofmt'ed code block would have a
			 * blank line preceding it after the unindented lines. */
			if (i < lines.n && lines.v[i].n != 0 && !comment_isList(lines.v[i])) {
				if (comment_isList(lines.v[i - 1])) {
					/* If the final unindented line looks like a list item, this may be
					 * the first indented line wrap of a mistakenly unindented list.
					 * Leave all the unindented list items. */
					forceIndent = end;
					end--;
					while (end > start && comment_isList(lines.v[end - 1]))
						end--;
				} else if (strings_HasSuffix(lines.v[i - 1], GOSTR("{")) ||
					   strings_HasSuffix(lines.v[i - 1], GOSTR("\\"))) {
					/* If the final unindented line ended in { or \ it is probably the
					 * start of a misindented code block. Give the user a single line
					 * fix. Often that's enough; if not, the user can fix the others
					 * themselves. */
					forceIndent = end;
					end--;
				}

				if (start == end && forceIndent > start) {
					i = start;
					continue; /* continue Spans */
				}
			}

			/* Span is either paragraph or heading. */
			if (end - start == 1 && comment_isHeading(lines.v[start]))
				kind = comment_spanHeading;
			else if (end - start == 1 && comment_isOldHeading(lines.v[start], lines, start))
				kind = comment_spanOldHeading;
			else
				kind = comment_spanPara;
		}

		comment_spanVec_push(&spans, (comment_span){start, end, kind});
		i = end;
	}

	return spans;
}

/* indented reports whether line is indented (starts with a leading space or tab). */
static bool comment_indented(GoStr line) { return line.n != 0 && (line.p[0] == ' ' || line.p[0] == '\t'); }

static bool comment_isBlank(GoStr s);
static GoStr comment_commonPrefix(GoStr a, GoStr b);
static GoStr comment_leadingSpace(GoStr s);

/* unindent removes any common space/tab prefix from each line in lines, returning a copy of
 * lines in which those prefixes have been trimmed from each line. It also replaces any lines
 * containing only spaces with blank lines (empty strings). */
static GoStrVec comment_unindent(GoStrVec lines)
{
	/* Trim leading and trailing blank lines. */
	while (lines.n > 0 && comment_isBlank(lines.v[0]))
		lines = comment_lines(lines, 1, lines.n);
	while (lines.n > 0 && comment_isBlank(lines.v[lines.n - 1]))
		lines = comment_lines(lines, 0, lines.n - 1);
	if (lines.n == 0)
		return (GoStrVec){0};

	/* Compute and remove common indentation. */
	GoStr prefix = comment_leadingSpace(lines.v[0]);
	for (int i = 1; i < lines.n; i++) {
		if (!comment_isBlank(lines.v[i]))
			prefix = comment_commonPrefix(prefix, comment_leadingSpace(lines.v[i]));
	}

	GoStrVec out = {xalloc(sizeof(GoStr) * (size_t)lines.n), lines.n, lines.n};
	for (int i = 0; i < lines.n; i++) {
		GoStr line = strings_TrimPrefix(lines.v[i], prefix);
		if (strings_TrimSpace(line).n == 0)
			line = (GoStr){0};
		out.v[i] = line;
	}
	while (out.n > 0 && out.v[0].n == 0)
		out = comment_lines(out, 1, out.n);
	while (out.n > 0 && out.v[out.n - 1].n == 0)
		out = comment_lines(out, 0, out.n - 1);
	return out;
}

/* isBlank reports whether s is a blank line. */
static bool comment_isBlank(GoStr s) { return s.n == 0 || (s.n == 1 && s.p[0] == '\n'); }

/* commonPrefix returns the longest common prefix of a and b. */
static GoStr comment_commonPrefix(GoStr a, GoStr b)
{
	int i = 0;
	while (i < a.n && i < b.n && a.p[i] == b.p[i])
		i++;
	return gostr_sub(a, 0, i);
}

/* leadingSpace returns the longest prefix of s consisting of spaces and tabs. */
static GoStr comment_leadingSpace(GoStr s)
{
	int i = 0;
	while (i < s.n && (s.p[i] == ' ' || s.p[i] == '\t'))
		i++;
	return gostr_sub(s, 0, i);
}

/* isOldHeading reports whether line is an old-style section heading. line is all[off]. */
static bool comment_isOldHeading(GoStr line, GoStrVec all, int off)
{
	if (off <= 0 || all.v[off - 1].n != 0 || off + 2 >= all.n || all.v[off + 1].n != 0 ||
	    comment_leadingSpace(all.v[off + 2]).n != 0)
		return false;

	line = strings_TrimSpace(line);

	/* a heading must start with an uppercase letter */
	rune r = utf8_DecodeRuneInString(line, NULL);
	if (!unicode_IsLetter(r) || !unicode_IsUpper(r))
		return false;

	/* it must end in a letter or digit: */
	r = utf8_DecodeLastRuneInString(line, NULL);
	if (!unicode_IsLetter(r) && !unicode_IsDigit(r))
		return false;

	/* exclude lines with illegal characters. we allow "()," */
	if (strings_ContainsAny(line, GOSTR(";:!?+*/=[]{}_^°&§~%#@<\">\\")))
		return false;

	/* allow "'" for possessive "'s" only */
	for (GoStr b = line;;) {
		if (!strings_Cut(b, GOSTR("'"), NULL, &b))
			break;
		if (!gostr_eqc(b, "s") && !strings_HasPrefix(b, GOSTR("s ")))
			return false; /* ' not followed by s and then end-of-word */
	}

	/* allow "." when followed by non-space */
	for (GoStr b = line;;) {
		if (!strings_Cut(b, GOSTR("."), NULL, &b))
			break;
		if (b.n == 0 || strings_HasPrefix(b, GOSTR(" ")))
			return false; /* not followed by non-space */
	}

	return true;
}

static comment_Block *comment_newHeading(GoStr text)
{
	comment_Heading *h = xalloc(sizeof *h);
	h->b.kind = comment_KindHeading;
	vec_push(&h->Text, comment_newPlain(text));
	return &h->b;
}

/* oldHeading returns the *Heading for the given old-style section heading line. */
static comment_Block *comment_parseDoc_oldHeading(comment_parseDoc *d, GoStr line)
{
	return comment_newHeading(strings_TrimSpace(line));
}

/* isHeading reports whether line is a new-style section heading. */
static bool comment_isHeading(GoStr line)
{
	return line.n >= 2 && line.p[0] == '#' && (line.p[1] == ' ' || line.p[1] == '\t') &&
	       !gostr_eqc(strings_TrimSpace(line), "#");
}

/* heading returns the *Heading for the given new-style section heading line. */
static comment_Block *comment_parseDoc_heading(comment_parseDoc *d, GoStr line)
{
	return comment_newHeading(strings_TrimSpace(gostr_from(line, 1)));
}

/* code returns a code block built from the lines. */
static comment_Block *comment_parseDoc_code(comment_parseDoc *d, GoStrVec lines)
{
	GoStrVec body = comment_unindent(lines);
	/* body = append(body, ""): to get final \n from Join */
	GoStr *v = xalloc(sizeof(GoStr) * ((size_t)body.n + 1));
	if (body.n > 0)
		memcpy(v, body.v, sizeof(GoStr) * (size_t)body.n);
	comment_Code *c = xalloc(sizeof *c);
	c->b.kind = comment_KindCode;
	c->Text = strings_Join(v, body.n + 1, GOSTR("\n"));
	return &c->b;
}

static bool comment_parseLink(GoStr line, comment_LinkDef **def);

/* paragraph returns a paragraph block built from the lines. If the lines are link
 * definitions, paragraph adds them to d and returns nil. */
static comment_Block *comment_parseDoc_paragraph(comment_parseDoc *d, GoStrVec lines)
{
	/* Is this a block of known links? Handle. */
	Vec defs = {0};
	for (int i = 0; i < lines.n; i++) {
		comment_LinkDef *def;
		if (!comment_parseLink(lines.v[i], &def))
			goto NoDefs;
		vec_push(&defs, def);
	}
	for (int i = 0; i < defs.n; i++) {
		comment_LinkDef *def = defs.v[i];
		vec_push(&d->Doc->Links, def);
		if (comment_parseDoc_link(d, def->Text) == NULL)
			vec_push(&d->links, def);
	}
	return NULL;
NoDefs:;

	comment_Paragraph *para = xalloc(sizeof *para);
	para->b.kind = comment_KindParagraph;
	vec_push(&para->Text, comment_newPlain(strings_Join(lines.v, lines.n, GOSTR("\n"))));
	return &para->b;
}

/* parseLink parses a single link definition line:
 *
 *	[text]: url
 *
 * It returns the link definition and whether the line was well formed. */
static bool comment_parseLink(GoStr line, comment_LinkDef **def)
{
	*def = NULL;
	if (line.n == 0 || line.p[0] != '[')
		return false;
	int i = strings_Index(line, GOSTR("]:"));
	if (i < 0 || i + 3 >= line.n || (line.p[i + 2] != ' ' && line.p[i + 2] != '\t'))
		return false;

	GoStr text = gostr_sub(line, 1, i);
	GoStr url = strings_TrimSpace(gostr_from(line, i + 3));
	int j = strings_Index(url, GOSTR("://"));
	if (j < 0 || !comment_isScheme(gostr_sub(url, 0, j)))
		return false;

	/* Line has right form and has valid scheme://. That's good enough for us - we are not as
	 * picky about the characters beyond the :// as we are when extracting inline URLs from
	 * text. */
	comment_LinkDef *ld = xalloc(sizeof *ld);
	ld->Text = text;
	ld->URL = url;
	*def = ld;
	return true;
}

static bool comment_listMarker(GoStr line, GoStr *num, GoStr *rest);

/* flush closure of list */
static void comment_list_flush(comment_parseDoc *d, comment_ListItem *item, GoStrVec *text)
{
	if (item != NULL) {
		comment_Block *para = comment_parseDoc_paragraph(d, *text);
		if (para != NULL)
			vec_push(&item->Content, para);
	}
	*text = (GoStrVec){0};
}

/* list returns a list built from the indented lines, using forceBlankBefore as the value of
 * the List's ForceBlankBefore field. */
static comment_Block *comment_parseDoc_list(comment_parseDoc *d, GoStrVec lines, bool forceBlankBefore)
{
	GoStr num;
	comment_listMarker(lines.v[0], &num, NULL);
	comment_List *list = xalloc(sizeof *list);
	list->b.kind = comment_KindList;
	list->ForceBlankBefore = forceBlankBefore;
	comment_ListItem *item = NULL;
	GoStrVec text = {0};

	for (int i = 0; i < lines.n; i++) {
		GoStr line = lines.v[i];
		GoStr n, after;
		if (comment_listMarker(line, &n, &after) && (n.n != 0) == (num.n != 0)) {
			/* start new list item */
			comment_list_flush(d, item, &text);

			item = xalloc(sizeof *item);
			item->Number = n;
			vec_push(&list->Items, item);
			line = after;
		}
		line = strings_TrimSpace(line);
		if (line.n == 0) {
			list->ForceBlankBetween = true;
			comment_list_flush(d, item, &text);
			continue;
		}
		gostrvec_push(&text, strings_TrimSpace(line));
	}
	comment_list_flush(d, item, &text);
	return &list->b;
}

/* listMarker parses the line as beginning with a list marker. If it can do that, it returns
 * the numeric marker ("" for a bullet list), the rest of the line, and ok == true. Otherwise,
 * it returns "", "", false. rest may be NULL. */
static bool comment_listMarker(GoStr line, GoStr *num, GoStr *rest)
{
	GoStr n_, r_;
	*num = (GoStr){0};
	if (rest == NULL)
		rest = &r_;
	*rest = (GoStr){0};
	line = strings_TrimSpace(line);
	if (line.n == 0)
		return false;

	/* Can we find a marker? */
	int n;
	rune r = utf8_DecodeRuneInString(line, &n);
	if (r == 0x2022 /* '•' */ || r == '*' || r == '+' || r == '-') {
		n_ = (GoStr){0};
		r_ = gostr_from(line, n);
	} else if ('0' <= line.p[0] && line.p[0] <= '9') {
		n = 1;
		while (n < line.n && '0' <= line.p[n] && line.p[n] <= '9')
			n++;
		if (n >= line.n || (line.p[n] != '.' && line.p[n] != ')'))
			return false;
		n_ = gostr_sub(line, 0, n);
		r_ = gostr_from(line, n + 1);
	} else {
		return false;
	}

	if (!comment_indented(r_) || strings_TrimSpace(r_).n == 0)
		return false;

	*num = n_;
	*rest = r_;
	return true;
}

/* isList reports whether the line is the first line of a list, meaning starts with a list
 * marker after any indentation. (The caller is responsible for checking the line is indented,
 * as appropriate.) */
static bool comment_isList(GoStr line)
{
	GoStr num;
	return comment_listMarker(line, &num, NULL);
}

static bool comment_parseDoc_docLink(comment_parseDoc *d, GoStr text, GoStr before, GoStr after,
				     comment_DocLink **link);

/* flush closure of parseLinkedText */
static void comment_parseLinkedText_flush(comment_parseDoc *d, Vec *out, GoStr text, int *wrote, int i)
{
	if (*wrote < i) {
		*out = comment_parseDoc_parseText(d, *out, gostr_sub(text, *wrote, i), true);
		*wrote = i;
	}
}

/* parseLinkedText parses text that is allowed to contain explicit links, such as [math.Sin]
 * or [Go home page], into a slice of Text items.
 *
 * A "pkg" is only assumed to be a full import path if it starts with a domain name (a path
 * element with a dot) or is one of the packages from the standard library ("[os]",
 * "[encoding/json]", and so on). To avoid problems with maps, generics, and array types, doc
 * links must be both preceded and followed by punctuation, spaces, tabs, or the start or end
 * of a line. An example problem would be treating map[ast.Expr]TypeAndValue as containing a
 * link. */
static Vec comment_parseDoc_parseLinkedText(comment_parseDoc *d, GoStr text)
{
	Vec out = {0};
	int wrote = 0;

	int start = -1;
	Buf buf = {0};
	for (int i = 0; i < text.n; i++) {
		char c = text.p[i];
		if (c == '\n' || c == '\t')
			c = ' ';
		switch (c) {
		case '[':
			start = i;
			break;
		case ']':
			if (start >= 0) {
				comment_LinkDef *def = comment_parseDoc_link(d, gostr_buf(&buf));
				comment_DocLink *link;
				if (def != NULL) {
					def->Used = true;
					comment_parseLinkedText_flush(d, &out, text, &wrote, start);
					comment_Link *l = xalloc(sizeof *l);
					l->t.kind = comment_KindLink;
					l->Text = comment_parseDoc_parseText(d, (Vec){0}, gostr_sub(text, start + 1, i), false);
					l->URL = def->URL;
					vec_push(&out, l);
					wrote = i + 1;
				} else if (comment_parseDoc_docLink(d, gostr_sub(text, start + 1, i), gostr_sub(text, 0, start),
								    gostr_from(text, i + 1), &link)) {
					comment_parseLinkedText_flush(d, &out, text, &wrote, start);
					link->Text = comment_parseDoc_parseText(d, (Vec){0}, gostr_sub(text, start + 1, i), false);
					vec_push(&out, link);
					wrote = i + 1;
				}
			}
			start = -1;
			buf.n = 0;
			break;
		default:
			break;
		}
		if (start >= 0 && i != start)
			buf_addc(&buf, c);
	}

	comment_parseLinkedText_flush(d, &out, text, &wrote, text.n);
	return out;
}

static bool comment_splitDocName(GoStr text, GoStr *before, GoStr *name);

/* docLink parses text, which was found inside [ ] brackets, as a doc link if possible,
 * returning the DocLink and ok == true or else nil, false. The before and after strings are
 * the text before the [ and after the ] on the same line. Doc links must be preceded and
 * followed by punctuation, spaces, tabs, or the start or end of a line. */
static bool comment_parseDoc_docLink(comment_parseDoc *d, GoStr text, GoStr before, GoStr after,
				     comment_DocLink **link)
{
	*link = NULL;
	if (before.n != 0) {
		rune r = utf8_DecodeLastRuneInString(before, NULL);
		if (!unicode_IsPunct(r) && r != ' ' && r != '\t' && r != '\n')
			return false;
	}
	if (after.n != 0) {
		rune r = utf8_DecodeRuneInString(after, NULL);
		if (!unicode_IsPunct(r) && r != ' ' && r != '\t' && r != '\n')
			return false;
	}
	text = strings_TrimPrefix(text, GOSTR("*"));
	GoStr pkg, name;
	bool ok = comment_splitDocName(text, &pkg, &name);
	GoStr recv = {0};
	if (ok) {
		GoStr p = pkg;
		comment_splitDocName(p, &pkg, &recv);
	}
	if (pkg.n != 0) {
		if (!(ok = comment_parseDoc_lookupPkg(d, pkg, &pkg)))
			return false;
	} else {
		if (!(ok = d->lookupSym(recv, name)))
			return false;
	}
	comment_DocLink *l = xalloc(sizeof *l);
	l->t.kind = comment_KindDocLink;
	l->ImportPath = pkg;
	l->Recv = recv;
	l->Name = name;
	*link = l;
	return true;
}

/* If text is of the form before.Name, where Name is a capitalized Go identifier, then
 * splitDocName returns before, name, true. Otherwise it returns text, "", false. */
static bool comment_splitDocName(GoStr text, GoStr *before, GoStr *name)
{
	int i = strings_LastIndex(text, GOSTR("."));
	*name = gostr_from(text, i + 1);
	if (!comment_isName(*name)) {
		*before = text;
		*name = (GoStr){0};
		return false;
	}
	*before = (GoStr){0};
	if (i >= 0)
		*before = gostr_sub(text, 0, i);
	return true;
}

static bool comment_autoURL(GoStr s, GoStr *url);

/* writeUntil and flush closures of parseText */
static void comment_parseText_writeUntil(Buf *w, GoStr s, int *wrote, int i)
{
	buf_addg(w, gostr_sub(s, *wrote, i));
	*wrote = i;
}

static void comment_parseText_flush(Buf *w, Vec *out, GoStr s, int *wrote, int i)
{
	comment_parseText_writeUntil(w, s, wrote, i);
	if (w->n > 0) {
		vec_push(out, comment_newPlain(gostr_dup(gostr_buf(w))));
		w->n = 0;
	}
}

/* d.Words[id] */
static bool comment_parseDoc_word(comment_parseDoc *d, GoStr id, GoStr *url)
{
	for (int i = 0; i < d->Parser->nWords; i++) {
		if (gostr_eq(d->Parser->Words[i].Word, id)) {
			*url = d->Parser->Words[i].URL;
			return true;
		}
	}
	*url = (GoStr){0};
	return false;
}

/* parseText parses s as text and returns the result of appending those parsed Text elements
 * to out. parseText does not handle explicit links like [math.Sin] or [Go home page]: those
 * are handled by parseLinkedText. If autoLink is true, then parseText recognizes URLs and words
 * from d.Words and converts those to links as appropriate. */
static Vec comment_parseDoc_parseText(comment_parseDoc *d, Vec out, GoStr s, bool autoLink)
{
	Buf w = {0};
	int wrote = 0;
	for (int i = 0; i < s.n;) {
		GoStr t = gostr_from(s, i);
		if (autoLink) {
			GoStr url, id;
			if (comment_autoURL(t, &url)) {
				comment_parseText_flush(&w, &out, s, &wrote, i);
				/* Note: The old comment parser would look up the URL in words and
				 * replace the target with words[URL] if it was non-empty. We're not
				 * doing that lookup here. */
				comment_Link *l = xalloc(sizeof *l);
				l->t.kind = comment_KindLink;
				l->Auto = true;
				vec_push(&l->Text, comment_newPlain(url));
				l->URL = url;
				vec_push(&out, l);
				i += url.n;
				wrote = i;
				continue;
			}
			if (comment_ident(t, &id)) {
				bool italics = comment_parseDoc_word(d, id, &url);
				if (!italics) {
					i += id.n;
					continue;
				}
				comment_parseText_flush(&w, &out, s, &wrote, i);
				if (url.n == 0) {
					vec_push(&out, comment_newItalic(id));
				} else {
					comment_Link *l = xalloc(sizeof *l);
					l->t.kind = comment_KindLink;
					l->Auto = true;
					vec_push(&l->Text, comment_newItalic(id));
					l->URL = url;
					vec_push(&out, l);
				}
				i += id.n;
				wrote = i;
				continue;
			}
		}
		if (strings_HasPrefix(t, GOSTR("``"))) {
			if (t.n >= 3 && t.p[2] == '`') {
				/* Do not convert `` inside ```, in case people are mistakenly writing
				 * Markdown. */
				i += 3;
				while (i < t.n && t.p[i] == '`') /* sic: t is s[i:] of the loop start */
					i++;
				continue;
			}
			comment_parseText_writeUntil(&w, s, &wrote, i);
			utf8_AppendRune(&w, 0x201C /* '“' */);
			i += 2;
			wrote = i;
		} else if (strings_HasPrefix(t, GOSTR("''"))) {
			comment_parseText_writeUntil(&w, s, &wrote, i);
			utf8_AppendRune(&w, 0x201D /* '”' */);
			i += 2;
			wrote = i;
		} else {
			i++;
		}
	}
	comment_parseText_flush(&w, &out, s, &wrote, s.n);
	return out;
}

static bool comment_isHost(unsigned char c);
static bool comment_isPunct(unsigned char c);
static bool comment_isPath(unsigned char c);

/* autoURL checks whether s begins with a URL that should be hyperlinked. If so, it returns
 * the URL, which is a prefix of s, and ok == true. Otherwise it returns "", false. The caller
 * should skip over the first len(url) bytes of s before further processing. */
static bool comment_autoURL(GoStr s, GoStr *url)
{
	*url = (GoStr){0};
	/* Find the ://. Fast path to pick off non-URL, since we call this at every position in
	 * the string. The shortest possible URL is ftp://x, 7 bytes. */
	int i;
	if (s.n < 7)
		return false;
	else if (s.p[3] == ':')
		i = 3;
	else if (s.p[4] == ':')
		i = 4;
	else if (s.p[5] == ':')
		i = 5;
	else if (s.p[6] == ':')
		i = 6;
	else
		return false;
	if (i + 3 > s.n || !gostr_eqc(gostr_sub(s, i, i + 3), "://"))
		return false;

	/* Check valid scheme. */
	if (!comment_isScheme(gostr_sub(s, 0, i)))
		return false;

	/* Scan host part. Must have at least one byte, and must start and end in
	 * non-punctuation. */
	i += 3;
	if (i >= s.n || !comment_isHost((unsigned char)s.p[i]) || comment_isPunct((unsigned char)s.p[i]))
		return false;
	i++;
	int end = i;
	while (i < s.n && comment_isHost((unsigned char)s.p[i])) {
		if (!comment_isPunct((unsigned char)s.p[i]))
			end = i + 1;
		i++;
	}
	i = end;

	/* At this point we are definitely returning a URL (scheme://host). We just have to find
	 * the longest path we can add to it. Heuristics abound. We allow parens, braces, and
	 * brackets, but only if they match (#5043, #22285). We allow .,:;?! in the path but not
	 * at the end, to avoid end-of-sentence punctuation (#18139, #16565). */
	Buf stk = {0};
	end = i;
	for (; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		if (comment_isPunct(c))
			continue;
		if (!comment_isPath(c))
			break;
		switch (c) {
		case '(':
			buf_addc(&stk, ')');
			break;
		case '{':
			buf_addc(&stk, '}');
			break;
		case '[':
			buf_addc(&stk, ']');
			break;
		case ')':
		case '}':
		case ']':
			if (stk.n == 0 || stk.p[stk.n - 1] != (char)c)
				goto Path; /* break Path */
			stk.n--;
			break;
		default:
			break;
		}
		if (stk.n == 0)
			end = i + 1;
	}
Path:
	*url = gostr_sub(s, 0, end);
	return true;
}

/* isScheme reports whether s is a recognized URL scheme. Note that if strings of new length
 * (beyond 3-7) are added here, the fast path at the top of autoURL will need updating. */
static bool comment_isScheme(GoStr s)
{
	static const char *const schemes[] = {"file", "ftp", "gopher", "http", "https", "mailto", "nntp"};
	for (size_t i = 0; i < sizeof schemes / sizeof *schemes; i++) {
		if (gostr_eqc(s, schemes[i]))
			return true;
	}
	return false;
}

/* In Go, isHost, isPunct, isPath, isIdentASCII and importPathOK test c against a 128-bit
 * bitmap (bytes >= 128 are never in it); here the same sets are spelled out. */
static bool comment_isASCIIAlnum(unsigned char c)
{
	return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || ('0' <= c && c <= '9');
}

/* isHost reports whether c is a byte that can appear in a URL host, like www.example.com or
 * user@[::1]:8080 */
static bool comment_isHost(unsigned char c)
{
	return comment_isASCIIAlnum(c) || (c != 0 && strchr("_@-.[]:", c) != NULL);
}

/* isPunct reports whether c is a punctuation byte that can appear inside a path but not at
 * the end. */
static bool comment_isPunct(unsigned char c) { return c != 0 && strchr(".,:;?!", c) != NULL; }

/* isPath reports whether c is a (non-punctuation) path byte. */
static bool comment_isPath(unsigned char c)
{
	return comment_isASCIIAlnum(c) || (c != 0 && strchr("$'()*+&#=@~_/-[]{}%", c) != NULL);
}

/* isName reports whether s is a capitalized Go identifier (like Name). */
static bool comment_isName(GoStr s)
{
	GoStr t;
	bool ok = comment_ident(s, &t);
	if (!ok || !gostr_eq(t, s))
		return false;
	rune r = utf8_DecodeRuneInString(s, NULL);
	return unicode_IsUpper(r);
}

/* ident checks whether s begins with a Go identifier. If so, it returns the identifier, which
 * is a prefix of s, and ok == true. Otherwise it returns "", false. The caller should skip
 * over the first len(id) bytes of s before further processing. */
static bool comment_ident(GoStr s, GoStr *id)
{
	/* Scan [\pL_][\pL_0-9]* */
	int n = 0;
	while (n < s.n) {
		unsigned char c = (unsigned char)s.p[n];
		if (c < utf8_RuneSelf) {
			if (comment_isIdentASCII(c) && (n > 0 || c < '0' || c > '9')) {
				n++;
				continue;
			}
			break;
		}
		int nr;
		rune r = utf8_DecodeRuneInString(gostr_from(s, n), &nr);
		if (unicode_IsLetter(r)) {
			n += nr;
			continue;
		}
		break;
	}
	*id = gostr_sub(s, 0, n);
	return n > 0;
}

/* isIdentASCII reports whether c is an ASCII identifier byte. */
static bool comment_isIdentASCII(unsigned char c) { return comment_isASCIIAlnum(c) || c == '_'; }

static bool comment_validImportPathElem(GoStr elem);

/* validImportPath reports whether path is a valid import path. It is a lightly edited copy of
 * golang.org/x/mod/module.CheckImportPath. */
static bool comment_validImportPath(GoStr path)
{
	if (!utf8_ValidString(path))
		return false;
	if (path.n == 0)
		return false;
	if (path.p[0] == '-')
		return false;
	if (strings_Contains(path, GOSTR("//")))
		return false;
	if (path.p[path.n - 1] == '/')
		return false;
	int elemStart = 0;
	for (int i = 0; i < path.n; i++) { /* for i, r := range path: '/' is ASCII, path is valid UTF-8 */
		if (path.p[i] == '/') {
			if (!comment_validImportPathElem(gostr_sub(path, elemStart, i)))
				return false;
			elemStart = i + 1;
		}
	}
	return comment_validImportPathElem(gostr_from(path, elemStart));
}

static bool comment_importPathOK(unsigned char c);

static bool comment_validImportPathElem(GoStr elem)
{
	if (elem.n == 0 || elem.p[0] == '.' || elem.p[elem.n - 1] == '.')
		return false;
	for (int i = 0; i < elem.n; i++) {
		if (!comment_importPathOK((unsigned char)elem.p[i]))
			return false;
	}
	return true;
}

static bool comment_importPathOK(unsigned char c)
{
	return comment_isASCIIAlnum(c) || (c != 0 && strchr("-.~_+", c) != NULL);
}
