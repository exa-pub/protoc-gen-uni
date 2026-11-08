/* Copyright (c) 2011-2019 Canonical Ltd
 * Licensed under the Apache License, Version 2.0 (see LICENSE of go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 decode.go (the parser that builds the Node tree) and
 * yaml.go (Unmarshal into a *Node: the first document of the stream, copied into the target). */
#include "internal.h"

typedef struct {
	yaml_parser_t parser;
	yaml_event_t event;
	YamlNode *doc;
	Map anchors;
	bool doneInit;
	bool textless;
	int depth;
} parser;

/* Nesting deeper than this is not supported (the Go parser recurses without a limit; the
 * scanner limits flow levels and indents to 10000). */
#define MAX_DEPTH 1000

static NORETURN COLD void failf(const char *msg) { fail("yaml: %s", msg); }

static NORETURN COLD void p_fail(parser *p)
{
	const char *where = "";
	int line = 0;
	if (p->parser.context_mark.line != 0) {
		line = p->parser.context_mark.line;
		/* Scanner errors don't iterate line before returning error */
		if (p->parser.error == yaml_SCANNER_ERROR)
			line++;
	} else if (p->parser.problem_mark.line != 0) {
		line = p->parser.problem_mark.line;
		/* Scanner errors don't iterate line before returning error */
		if (p->parser.error == yaml_SCANNER_ERROR)
			line++;
	}
	if (line != 0)
		where = xfmt("line %d: ", line);
	const char *msg = p->parser.problem && *p->parser.problem ? p->parser.problem : "unknown problem parsing YAML content";
	failf(xcat(where, msg));
}

static void expect(parser *p, yaml_event_type_t e)
{
	if (p->event.typ == yaml_NO_EVENT) {
		if (!yaml_parser_parse(&p->parser, &p->event))
			p_fail(p);
	}
	if (p->event.typ == yaml_STREAM_END_EVENT)
		failf("attempted to go past the end of stream; corrupted value?");
	if (p->event.typ != e) {
		p->parser.problem = xcat("expected ", yaml_event_type_string(e), " event but got ", yaml_event_type_string(p->event.typ));
		p_fail(p);
	}
	memset(&p->event, 0, sizeof p->event);
	p->event.typ = yaml_NO_EVENT;
}

/* peek peeks at the next event in the event stream, puts the results into p.event and returns
 * the event kind. */
static yaml_event_type_t peek(parser *p)
{
	if (p->event.typ != yaml_NO_EVENT)
		return p->event.typ;
	/* It's curious choice from the underlying API to generally return a positive result on
	 * success, but on this case return true in an error scenario. This was the source of bugs
	 * in the past (issue #666). */
	if (!yaml_parser_parse(&p->parser, &p->event) || p->parser.error != yaml_NO_ERROR)
		p_fail(p);
	return p->event.typ;
}

static void init(parser *p)
{
	if (p->doneInit)
		return;
	p->anchors = (Map){0};
	expect(p, yaml_STREAM_START_EVENT);
	p->doneInit = true;
}

static void anchor(parser *p, YamlNode *n, Str a)
{
	if (a.p) {
		n->anchor = a;
		/* anchors may contain NUL only if the input does, and the reader rejects it */
		map_put(&p->anchors, xstrndup(a.p, a.n), n);
	}
}

static YamlNode *parse(parser *p);

static YamlNode *node(parser *p, int kind, Str defaultTag, Str tag, Str value)
{
	int style = 0;
	if (tag.n != 0 && !(tag.n == 1 && tag.p[0] == '!')) {
		tag = yaml_short_tag(tag);
		style = YAML_TAGGED_STYLE;
	} else if (defaultTag.n != 0) {
		tag = defaultTag;
	} else if (kind == YAML_SCALAR_NODE) {
		tag = str_c(yaml_resolve_tag(value));
	}
	YamlNode *n = yaml_node_new(kind);
	n->tag = tag;
	n->value = value;
	n->style = style;
	if (!p->textless) {
		n->line = p->event.start_mark.line + 1;
		n->column = p->event.start_mark.column + 1;
		n->head_comment = p->event.head_comment;
		n->line_comment = p->event.line_comment;
		n->foot_comment = p->event.foot_comment;
	}
	return n;
}

static YamlNode *parse_child(parser *p, YamlNode *parent)
{
	YamlNode *child = parse(p);
	yaml_node_append(parent, child);
	return child;
}

static YamlNode *document(parser *p)
{
	YamlNode *n = node(p, YAML_DOCUMENT_NODE, (Str){0}, (Str){0}, (Str){0});
	p->doc = n;
	expect(p, yaml_DOCUMENT_START_EVENT);
	parse_child(p, n);
	if (peek(p) == yaml_DOCUMENT_END_EVENT)
		n->foot_comment = p->event.foot_comment;
	expect(p, yaml_DOCUMENT_END_EVENT);
	return n;
}

static YamlNode *alias(parser *p)
{
	YamlNode *n = node(p, YAML_ALIAS_NODE, (Str){0}, (Str){0}, p->event.anchor);
	n->alias = memchr(n->value.p ? n->value.p : "", 0, n->value.n) ? NULL : map_get(&p->anchors, xstrndup(n->value.p, n->value.n));
	if (!n->alias)
		failf(xcat("unknown anchor '", xstrndup(n->value.p, n->value.n), "' referenced"));
	expect(p, yaml_ALIAS_EVENT);
	return n;
}

static YamlNode *scalar(parser *p)
{
	int parsedStyle = p->event.style;
	int nodeStyle = 0;
	if (parsedStyle & yaml_DOUBLE_QUOTED_SCALAR_STYLE)
		nodeStyle = YAML_DOUBLE_QUOTED_STYLE;
	else if (parsedStyle & yaml_SINGLE_QUOTED_SCALAR_STYLE)
		nodeStyle = YAML_SINGLE_QUOTED_STYLE;
	else if (parsedStyle & yaml_LITERAL_SCALAR_STYLE)
		nodeStyle = YAML_LITERAL_STYLE;
	else if (parsedStyle & yaml_FOLDED_SCALAR_STYLE)
		nodeStyle = YAML_FOLDED_STYLE;
	Str nodeValue = p->event.value, nodeTag = p->event.tag, defaultTag = {0};
	if (nodeStyle == 0) {
		if (str_eqc(nodeValue, "<<"))
			defaultTag = STR_LIT("!!merge");
	} else {
		defaultTag = STR_LIT("!!str");
	}
	YamlNode *n = node(p, YAML_SCALAR_NODE, defaultTag, nodeTag, nodeValue);
	n->style |= nodeStyle;
	anchor(p, n, p->event.anchor);
	expect(p, yaml_SCALAR_EVENT);
	return n;
}

static YamlNode *sequence(parser *p)
{
	YamlNode *n = node(p, YAML_SEQUENCE_NODE, STR_LIT("!!seq"), p->event.tag, (Str){0});
	if (p->event.style & yaml_FLOW_SEQUENCE_STYLE)
		n->style |= YAML_FLOW_STYLE;
	anchor(p, n, p->event.anchor);
	expect(p, yaml_SEQUENCE_START_EVENT);
	while (peek(p) != yaml_SEQUENCE_END_EVENT)
		parse_child(p, n);
	n->line_comment = p->event.line_comment;
	n->foot_comment = p->event.foot_comment;
	expect(p, yaml_SEQUENCE_END_EVENT);
	return n;
}

static YamlNode *mapping(parser *p)
{
	YamlNode *n = node(p, YAML_MAPPING_NODE, STR_LIT("!!map"), p->event.tag, (Str){0});
	bool block = true;
	if (p->event.style & yaml_FLOW_MAPPING_STYLE) {
		block = false;
		n->style |= YAML_FLOW_STYLE;
	}
	anchor(p, n, p->event.anchor);
	expect(p, yaml_MAPPING_START_EVENT);
	while (peek(p) != yaml_MAPPING_END_EVENT) {
		YamlNode *k = parse_child(p, n);
		if (block && k->foot_comment.n) {
			/* Must be a foot comment for the prior value when being dedented. */
			if (n->n_content > 2) {
				n->content[n->n_content - 3]->foot_comment = k->foot_comment;
				k->foot_comment = (Str){0};
			}
		}
		YamlNode *v = parse_child(p, n);
		if (!k->foot_comment.n && v->foot_comment.n) {
			k->foot_comment = v->foot_comment;
			v->foot_comment = (Str){0};
		}
		if (peek(p) == yaml_TAIL_COMMENT_EVENT) {
			if (!k->foot_comment.n)
				k->foot_comment = p->event.foot_comment;
			expect(p, yaml_TAIL_COMMENT_EVENT);
		}
	}
	n->line_comment = p->event.line_comment;
	n->foot_comment = p->event.foot_comment;
	if ((n->style & YAML_FLOW_STYLE) == 0 && n->foot_comment.n && n->n_content > 1) {
		n->content[n->n_content - 2]->foot_comment = n->foot_comment;
		n->foot_comment = (Str){0};
	}
	expect(p, yaml_MAPPING_END_EVENT);
	return n;
}

static YamlNode *parse(parser *p)
{
	init(p);
	if (++p->depth > MAX_DEPTH)
		fail("unsupported by protoc-gen-uni: YAML nested deeper than %d levels", MAX_DEPTH);
	YamlNode *n;
	switch (peek(p)) {
	case yaml_SCALAR_EVENT:
		n = scalar(p);
		break;
	case yaml_ALIAS_EVENT:
		n = alias(p);
		break;
	case yaml_MAPPING_START_EVENT:
		n = mapping(p);
		break;
	case yaml_SEQUENCE_START_EVENT:
		n = sequence(p);
		break;
	case yaml_DOCUMENT_START_EVENT:
		n = document(p);
		break;
	case yaml_STREAM_END_EVENT:
		/* Happens when attempting to decode an empty buffer. */
		n = NULL;
		break;
	case yaml_TAIL_COMMENT_EVENT:
		fail("internal error: unexpected tail comment event (please report)");
	default:
		fail("internal error: attempted to parse unknown event (please report): %s", yaml_event_type_string(p->event.typ));
	}
	p->depth--;
	return n;
}

typedef struct {
	Str in;
	YamlNode *out;
} UnmarshalCtx;

static void unmarshal(void *arg)
{
	UnmarshalCtx *c = arg;
	parser *p = xalloc(sizeof *p);
	Str in = c->in;
	if (in.n == 0)
		in = STR_LIT("\n");
	p->parser.input = in;
	YamlNode *n = parse(p);
	if (n) {
		/* d.unmarshal(node, v) for v of type yaml.Node: out.Set(reflect.ValueOf(n).Elem()) */
		*c->out = *n;
	}
}

const char *yaml_unmarshal(Str in, YamlNode *out)
{
	UnmarshalCtx c = {in, out};
	return try_run(unmarshal, &c);
}
