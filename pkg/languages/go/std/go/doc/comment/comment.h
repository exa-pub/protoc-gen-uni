/* Package go/doc/comment, ported from go1.27.1 src/go/doc/comment (parse.go, print.go, std.go:
 * what go/printer needs to reformat doc comments).
 * Copyright 2022 The Go Authors. All rights reserved. BSD-3-Clause (see LICENSE in Go).
 *
 * Not ported: html.go, markdown.go, text.go (Printer.HTML, Printer.Markdown, Printer.Text) and
 * the helpers only they use (Printer.headingLevel/headingID/docLinkURL, DocLink.DefaultURL,
 * Heading.DefaultID): go/printer only calls Parser.Parse and Printer.Comment.
 *
 * Representation: the Block and Text interfaces are tagged structs: every concrete block starts
 * with `comment_Block b` and every concrete text item with `comment_Text t` (Go type switch ->
 * switch on ->kind, then cast). Slices ([]Block, []Text, []*ListItem, []*LinkDef) are Vec of
 * pointers; Plain and Italic (string types in Go) are allocated like the other items. */
#ifndef GO_STD_COMMENT_H
#define GO_STD_COMMENT_H

/* A Doc is a parsed Go doc comment. */
typedef struct comment_Doc {
	Vec Content; /* []Block: the sequence of content blocks in the comment */
	Vec Links;   /* []*LinkDef: the link definitions in the comment */
} comment_Doc;

/* A LinkDef is a single link definition. */
typedef struct comment_LinkDef {
	GoStr Text; /* the link text */
	GoStr URL;  /* the link URL */
	bool Used;  /* whether the comment uses the definition */
} comment_LinkDef;

/* A Block is block-level content in a doc comment, one of *Code, *Heading, *List or
 * *Paragraph. */
typedef enum {
	comment_KindCode = 1,
	comment_KindHeading,
	comment_KindList,
	comment_KindParagraph,
} comment_BlockKind;

typedef struct comment_Block {
	comment_BlockKind kind;
} comment_Block;

/* A Heading is a doc comment heading. */
typedef struct comment_Heading {
	comment_Block b;
	Vec Text; /* []Text: the heading text */
} comment_Heading;

/* A List is a numbered or bullet list. Lists are always non-empty: len(Items) > 0. In a
 * numbered list, every Items[i].Number is a non-empty string; in a bullet list, every
 * Items[i].Number is an empty string. */
typedef struct comment_List {
	comment_Block b;
	Vec Items; /* []*ListItem */
	/* ForceBlankBefore: the list must be preceded by a blank line when reformatting (the
	 * parser sets it for any list preceded by a blank line). */
	bool ForceBlankBefore;
	/* ForceBlankBetween: list items must be separated by blank lines (the parser sets it for
	 * any list that has a blank line between any two of its items). */
	bool ForceBlankBetween;
} comment_List;

/* A ListItem is a single item in a numbered or bullet list. */
typedef struct comment_ListItem {
	GoStr Number; /* "1", "2", ...; "" for bullet list */
	Vec Content;  /* []Block; currently every element is a *Paragraph */
} comment_ListItem;

/* A Paragraph is a paragraph of text. */
typedef struct comment_Paragraph {
	comment_Block b;
	Vec Text; /* []Text */
} comment_Paragraph;

/* A Code is a preformatted code block. Text ends with a newline character; it is never empty,
 * nor does it start or end with a blank line. */
typedef struct comment_Code {
	comment_Block b;
	GoStr Text;
} comment_Code;

/* A Text is text-level content in a doc comment, one of Plain, Italic, *Link or *DocLink. */
typedef enum {
	comment_KindPlain = 1,
	comment_KindItalic,
	comment_KindLink,
	comment_KindDocLink,
} comment_TextKind;

typedef struct comment_Text {
	comment_TextKind kind;
} comment_Text;

/* A Plain is a string rendered as plain text (not italicized). */
typedef struct comment_Plain {
	comment_Text t;
	GoStr s;
} comment_Plain;

/* An Italic is a string rendered as italicized text. */
typedef struct comment_Italic {
	comment_Text t;
	GoStr s;
} comment_Italic;

/* A Link is a link to a specific URL. */
typedef struct comment_Link {
	comment_Text t;
	bool Auto; /* is this an automatic (implicit) link of a literal URL? */
	Vec Text;  /* []Text: text of link */
	GoStr URL; /* target URL of link */
} comment_Link;

/* A DocLink is a link to documentation for a Go package or symbol. */
typedef struct comment_DocLink {
	comment_Text t;
	Vec Text; /* []Text: text of link */
	/* ImportPath, Recv and Name identify the Go package or symbol that is the link target. */
	GoStr ImportPath; /* import path */
	GoStr Recv;       /* receiver type, without any pointer star, for methods */
	GoStr Name;       /* const, func, type, var, or method name */
} comment_DocLink;

/* An entry of Parser.Words (Go: map[string]string). */
typedef struct comment_Word {
	GoStr Word;
	GoStr URL;
} comment_Word;

/* A Parser is a doc comment parser. A zero comment_Parser is ready to use. */
typedef struct comment_Parser {
	/* Words is a map of Go identifier words that should be italicized and potentially linked.
	 * If Words[w] is the empty string, then the word w is only italicized; otherwise it is
	 * linked, using Words[w] as the link target. (nWords entries; nil map = 0.) */
	const comment_Word *Words;
	int nWords;
	/* LookupPackage resolves a package name to an import path (returns ok; sets
	 * *importPath). NULL is equivalent to a function that always returns "", false. */
	bool (*LookupPackage)(GoStr name, GoStr *importPath);
	/* LookupSym reports whether a symbol name or method name exists in the current package.
	 * NULL is equivalent to a function that always returns false. */
	bool (*LookupSym)(GoStr recv, GoStr name);
} comment_Parser;

/* A Printer is a doc comment printer. A zero comment_Printer is ready to use. Comment does not
 * read any of the fields; they are kept for the shape of the Go type (used by the not ported
 * HTML/Markdown/Text printers). */
typedef struct comment_Printer {
	int HeadingLevel;
	GoStr (*HeadingID)(comment_Heading *h);
	GoStr (*DocLinkURL)(comment_DocLink *link);
	GoStr DocLinkBaseURL;
	GoStr TextPrefix;
	GoStr TextCodePrefix;
	int TextWidth;
} comment_Printer;

/* DefaultLookupPackage is the default package lookup function, used when
 * Parser.LookupPackage is nil: it recognizes the single-element standard library import
 * paths, such as math. */
static bool comment_DefaultLookupPackage(GoStr name, GoStr *importPath);
/* Parse parses the doc comment text (comment markers already removed). */
static comment_Doc *comment_Parser_Parse(comment_Parser *p, GoStr text);
/* Comment returns the standard Go formatting of the Doc, without any comment markers. */
static GoStr comment_Printer_Comment(comment_Printer *p, comment_Doc *d);

#endif
