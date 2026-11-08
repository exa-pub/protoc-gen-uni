/* Copyright (c) 2011-2019 Canonical Ltd
 * Copyright (c) 2006-2010 Kirill Simonov
 * Licensed under the Apache License, Version 2.0 and the MIT License (see LICENSE of
 * go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 parserc.go: the parser, which produces the events of the
 * stream from the tokens of the scanner (an LL(1) state machine). */
#include "internal.h"

static void push_state(yaml_parser_t *parser, int state) { yaml_ints_push(&parser->states, state); }
static void pop_state(yaml_parser_t *parser) { parser->state = yaml_ints_pop(&parser->states); }

static void push_mark(yaml_parser_t *parser, yaml_mark_t m)
{
	if (parser->n_marks == parser->cap_marks) {
		int cap = parser->cap_marks ? parser->cap_marks * 2 : 16;
		parser->marks = xrealloc(parser->marks, sizeof *parser->marks * (size_t)parser->cap_marks, sizeof *parser->marks * (size_t)cap);
		parser->cap_marks = cap;
	}
	parser->marks[parser->n_marks++] = m;
}

static yaml_mark_t pop_mark(yaml_parser_t *parser) { return parser->marks[--parser->n_marks]; }

static void unfold_comments(yaml_parser_t *parser, yaml_token_t *token);

/* Peek the next token in the token queue. */
static yaml_token_t *peek_token(yaml_parser_t *parser)
{
	if (parser->token_available || yaml_parser_fetch_more_tokens(parser)) {
		yaml_token_t *token = &parser->tokens[parser->tokens_head];
		unfold_comments(parser, token);
		return token;
	}
	return NULL;
}

/* unfold_comments walks through the comments queue and joins all comments behind the position
 * of the provided token into the respective top-level comment slices in the parser. */
static void unfold_comments(yaml_parser_t *parser, yaml_token_t *token)
{
	while (parser->comments_head < parser->n_comments &&
	       token->start_mark.index >= parser->comments[parser->comments_head].token_mark.index) {
		yaml_comment_t *comment = &parser->comments[parser->comments_head];
		if (comment->head.n > 0) {
			if (token->typ == yaml_BLOCK_END_TOKEN) {
				/* No heads on ends, so keep comment.head for a follow up token. */
				break;
			}
			if (parser->head_comment.n > 0)
				parser->head_comment = yaml_str_catc(parser->head_comment, '\n');
			parser->head_comment = yaml_str_cat(parser->head_comment, comment->head);
		}
		if (comment->foot.n > 0) {
			if (parser->foot_comment.n > 0)
				parser->foot_comment = yaml_str_catc(parser->foot_comment, '\n');
			parser->foot_comment = yaml_str_cat(parser->foot_comment, comment->foot);
		}
		if (comment->line.n > 0) {
			if (parser->line_comment.n > 0)
				parser->line_comment = yaml_str_catc(parser->line_comment, '\n');
			parser->line_comment = yaml_str_cat(parser->line_comment, comment->line);
		}
		memset(comment, 0, sizeof *comment);
		parser->comments_head++;
	}
}

/* Remove the next token from the queue (must be called after peek_token). */
static void skip_token(yaml_parser_t *parser)
{
	parser->token_available = false;
	parser->tokens_parsed++;
	parser->stream_end_produced = parser->tokens[parser->tokens_head].typ == yaml_STREAM_END_TOKEN;
	parser->tokens_head++;
}

static bool state_machine(yaml_parser_t *parser, yaml_event_t *event);

/* Get the next event. */
bool yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event)
{
	/* Erase the event object. */
	memset(event, 0, sizeof *event);

	/* No events after the end of the stream or error. */
	if (parser->stream_end_produced || parser->error != yaml_NO_ERROR || parser->state == yaml_PARSE_END_STATE)
		return true;

	/* Generate the next event. */
	return state_machine(parser, event);
}

/* Set parser error. */
static bool set_parser_error(yaml_parser_t *parser, const char *problem, yaml_mark_t problem_mark)
{
	parser->error = yaml_PARSER_ERROR;
	parser->problem = problem;
	parser->problem_mark = problem_mark;
	return false;
}

static bool set_parser_error_context(yaml_parser_t *parser, const char *context, yaml_mark_t context_mark, const char *problem,
				     yaml_mark_t problem_mark)
{
	parser->error = yaml_PARSER_ERROR;
	parser->context = context;
	parser->context_mark = context_mark;
	parser->problem = problem;
	parser->problem_mark = problem_mark;
	return false;
}

static bool parse_stream_start(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_document_start(yaml_parser_t *parser, yaml_event_t *event, bool implicit);
static bool parse_document_content(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_document_end(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_node(yaml_parser_t *parser, yaml_event_t *event, bool block, bool indentless_sequence);
static bool parse_block_sequence_entry(yaml_parser_t *parser, yaml_event_t *event, bool first);
static bool parse_indentless_sequence_entry(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_block_mapping_key(yaml_parser_t *parser, yaml_event_t *event, bool first);
static bool parse_block_mapping_value(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_flow_sequence_entry(yaml_parser_t *parser, yaml_event_t *event, bool first);
static bool parse_flow_sequence_entry_mapping_key(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_flow_sequence_entry_mapping_value(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_flow_sequence_entry_mapping_end(yaml_parser_t *parser, yaml_event_t *event);
static bool parse_flow_mapping_key(yaml_parser_t *parser, yaml_event_t *event, bool first);
static bool parse_flow_mapping_value(yaml_parser_t *parser, yaml_event_t *event, bool empty);
static bool process_empty_scalar(yaml_parser_t *parser, yaml_event_t *event, yaml_mark_t mark);
static bool process_directives(yaml_parser_t *parser, yaml_version_directive_t **version_directive_ref,
			       yaml_tag_directive_t **tag_directives_ref, int *n_tag_directives_ref);

/* State dispatcher. */
static bool state_machine(yaml_parser_t *parser, yaml_event_t *event)
{
	switch (parser->state) {
	case yaml_PARSE_STREAM_START_STATE:
		return parse_stream_start(parser, event);
	case yaml_PARSE_IMPLICIT_DOCUMENT_START_STATE:
		return parse_document_start(parser, event, true);
	case yaml_PARSE_DOCUMENT_START_STATE:
		return parse_document_start(parser, event, false);
	case yaml_PARSE_DOCUMENT_CONTENT_STATE:
		return parse_document_content(parser, event);
	case yaml_PARSE_DOCUMENT_END_STATE:
		return parse_document_end(parser, event);
	case yaml_PARSE_BLOCK_NODE_STATE:
		return parse_node(parser, event, true, false);
	case yaml_PARSE_BLOCK_NODE_OR_INDENTLESS_SEQUENCE_STATE:
		return parse_node(parser, event, true, true);
	case yaml_PARSE_FLOW_NODE_STATE:
		return parse_node(parser, event, false, false);
	case yaml_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE:
		return parse_block_sequence_entry(parser, event, true);
	case yaml_PARSE_BLOCK_SEQUENCE_ENTRY_STATE:
		return parse_block_sequence_entry(parser, event, false);
	case yaml_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE:
		return parse_indentless_sequence_entry(parser, event);
	case yaml_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE:
		return parse_block_mapping_key(parser, event, true);
	case yaml_PARSE_BLOCK_MAPPING_KEY_STATE:
		return parse_block_mapping_key(parser, event, false);
	case yaml_PARSE_BLOCK_MAPPING_VALUE_STATE:
		return parse_block_mapping_value(parser, event);
	case yaml_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE:
		return parse_flow_sequence_entry(parser, event, true);
	case yaml_PARSE_FLOW_SEQUENCE_ENTRY_STATE:
		return parse_flow_sequence_entry(parser, event, false);
	case yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE:
		return parse_flow_sequence_entry_mapping_key(parser, event);
	case yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE:
		return parse_flow_sequence_entry_mapping_value(parser, event);
	case yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE:
		return parse_flow_sequence_entry_mapping_end(parser, event);
	case yaml_PARSE_FLOW_MAPPING_FIRST_KEY_STATE:
		return parse_flow_mapping_key(parser, event, true);
	case yaml_PARSE_FLOW_MAPPING_KEY_STATE:
		return parse_flow_mapping_key(parser, event, false);
	case yaml_PARSE_FLOW_MAPPING_VALUE_STATE:
		return parse_flow_mapping_value(parser, event, false);
	case yaml_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE:
		return parse_flow_mapping_value(parser, event, true);
	default:
		fail("invalid parser state");
	}
}

/* Parse the production:
 * stream   ::= STREAM-START implicit_document? explicit_document* STREAM-END
 *              ************ */
static bool parse_stream_start(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	if (token->typ != yaml_STREAM_START_TOKEN)
		return set_parser_error(parser, "did not find expected <stream-start>", token->start_mark);
	parser->state = yaml_PARSE_IMPLICIT_DOCUMENT_START_STATE;
	*event = (yaml_event_t){.typ = yaml_STREAM_START_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark,
				.encoding = token->encoding};
	skip_token(parser);
	return true;
}

/* Parse the productions:
 * implicit_document    ::= block_node DOCUMENT-END*
 *                          *
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                          ************************* */
static bool parse_document_start(yaml_parser_t *parser, yaml_event_t *event, bool implicit)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	/* Parse extra document end indicators. */
	if (!implicit) {
		while (token->typ == yaml_DOCUMENT_END_TOKEN) {
			skip_token(parser);
			token = peek_token(parser);
			if (!token)
				return false;
		}
	}

	if (implicit && token->typ != yaml_VERSION_DIRECTIVE_TOKEN && token->typ != yaml_TAG_DIRECTIVE_TOKEN &&
	    token->typ != yaml_DOCUMENT_START_TOKEN && token->typ != yaml_STREAM_END_TOKEN) {
		/* Parse an implicit document. */
		if (!process_directives(parser, NULL, NULL, NULL))
			return false;
		push_state(parser, yaml_PARSE_DOCUMENT_END_STATE);
		parser->state = yaml_PARSE_BLOCK_NODE_STATE;

		Str head_comment = {NULL, 0};
		if (parser->head_comment.n > 0) {
			/* [Go] Scan the header comment backwards, and if an empty line is found,
			 * break the header so the part before the last empty line goes into the
			 * document header, while the bottom of it goes into a follow up event. */
			Str h = parser->head_comment;
			for (size_t i = h.n - 1; i > 0; i--) {
				if (h.p[i] == '\n') {
					if (i == h.n - 1) {
						head_comment = (Str){h.p, i};
						parser->head_comment = (Str){h.p + i + 1, h.n - i - 1};
						break;
					} else if (h.p[i - 1] == '\n') {
						head_comment = (Str){h.p, i - 1};
						parser->head_comment = (Str){h.p + i + 1, h.n - i - 1};
						break;
					}
				}
			}
		}

		*event = (yaml_event_t){.typ = yaml_DOCUMENT_START_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark,
					.head_comment = head_comment};

	} else if (token->typ != yaml_STREAM_END_TOKEN) {
		/* Parse an explicit document. */
		yaml_version_directive_t *version_directive = NULL;
		yaml_tag_directive_t *tag_directives = NULL;
		int n_tag_directives = 0;
		yaml_mark_t start_mark = token->start_mark;
		if (!process_directives(parser, &version_directive, &tag_directives, &n_tag_directives))
			return false;
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ != yaml_DOCUMENT_START_TOKEN) {
			set_parser_error(parser, "did not find expected <document start>", token->start_mark);
			return false;
		}
		push_state(parser, yaml_PARSE_DOCUMENT_END_STATE);
		parser->state = yaml_PARSE_DOCUMENT_CONTENT_STATE;
		yaml_mark_t end_mark = token->end_mark;

		*event = (yaml_event_t){.typ = yaml_DOCUMENT_START_EVENT,
					.start_mark = start_mark,
					.end_mark = end_mark,
					.version_directive = version_directive,
					.tag_directives = tag_directives,
					.n_tag_directives = n_tag_directives,
					.implicit = false};
		skip_token(parser);

	} else {
		/* Parse the stream end. */
		parser->state = yaml_PARSE_END_STATE;
		*event = (yaml_event_t){.typ = yaml_STREAM_END_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark};
		skip_token(parser);
	}
	return true;
}

/* Parse the productions:
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                                                    *********** */
static bool parse_document_content(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	if (token->typ == yaml_VERSION_DIRECTIVE_TOKEN || token->typ == yaml_TAG_DIRECTIVE_TOKEN ||
	    token->typ == yaml_DOCUMENT_START_TOKEN || token->typ == yaml_DOCUMENT_END_TOKEN || token->typ == yaml_STREAM_END_TOKEN) {
		pop_state(parser);
		return process_empty_scalar(parser, event, token->start_mark);
	}
	return parse_node(parser, event, true, false);
}

static void set_event_comments(yaml_parser_t *parser, yaml_event_t *event)
{
	event->head_comment = parser->head_comment;
	event->line_comment = parser->line_comment;
	event->foot_comment = parser->foot_comment;
	parser->head_comment = (Str){NULL, 0};
	parser->line_comment = (Str){NULL, 0};
	parser->foot_comment = (Str){NULL, 0};
	parser->tail_comment = (Str){NULL, 0};
	parser->stem_comment = (Str){NULL, 0};
}

/* Parse the productions:
 * implicit_document    ::= block_node DOCUMENT-END*
 *                                     *************
 * explicit_document    ::= DIRECTIVE* DOCUMENT-START block_node? DOCUMENT-END*
 *                                                                *************/
static bool parse_document_end(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	yaml_mark_t start_mark = token->start_mark, end_mark = token->start_mark;

	bool implicit = true;
	if (token->typ == yaml_DOCUMENT_END_TOKEN) {
		end_mark = token->end_mark;
		skip_token(parser);
		implicit = false;
	}

	parser->n_tag_directives = 0;

	parser->state = yaml_PARSE_DOCUMENT_START_STATE;
	*event = (yaml_event_t){.typ = yaml_DOCUMENT_END_EVENT, .start_mark = start_mark, .end_mark = end_mark, .implicit = implicit};
	set_event_comments(parser, event);
	if (event->head_comment.n > 0 && event->foot_comment.n == 0) {
		event->foot_comment = event->head_comment;
		event->head_comment = (Str){NULL, 0};
	}
	return true;
}

/* Parse the productions:
 * block_node_or_indentless_sequence    ::=
 *                          ALIAS
 *                          *****
 *                          | properties (block_content | indentless_block_sequence)?
 *                            **********  *
 *                          | block_content | indentless_block_sequence
 *                            *
 * block_node           ::= ALIAS
 *                          *****
 *                          | properties block_content?
 *                            ********** *
 *                          | block_content
 *                            *
 * flow_node            ::= ALIAS
 *                          *****
 *                          | properties flow_content?
 *                            ********** *
 *                          | flow_content
 *                            *
 * properties           ::= TAG ANCHOR? | ANCHOR TAG?
 *                          *************************
 * block_content        ::= block_collection | flow_collection | SCALAR
 *                                                               ******
 * flow_content         ::= flow_collection | SCALAR
 *                                            ****** */
static bool parse_node(yaml_parser_t *parser, yaml_event_t *event, bool block, bool indentless_sequence)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	if (token->typ == yaml_ALIAS_TOKEN) {
		pop_state(parser);
		*event = (yaml_event_t){.typ = yaml_ALIAS_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark, .anchor = token->value};
		set_event_comments(parser, event);
		skip_token(parser);
		return true;
	}

	yaml_mark_t start_mark = token->start_mark, end_mark = token->start_mark;

	bool tag_token = false;
	Str tag_handle = {NULL, 0}, tag_suffix = {NULL, 0}, anchor = {NULL, 0};
	yaml_mark_t tag_mark = {0};
	if (token->typ == yaml_ANCHOR_TOKEN) {
		anchor = token->value;
		start_mark = token->start_mark;
		end_mark = token->end_mark;
		skip_token(parser);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ == yaml_TAG_TOKEN) {
			tag_token = true;
			tag_handle = token->value;
			tag_suffix = token->suffix;
			tag_mark = token->start_mark;
			end_mark = token->end_mark;
			skip_token(parser);
			token = peek_token(parser);
			if (!token)
				return false;
		}
	} else if (token->typ == yaml_TAG_TOKEN) {
		tag_token = true;
		tag_handle = token->value;
		tag_suffix = token->suffix;
		start_mark = token->start_mark;
		tag_mark = token->start_mark;
		end_mark = token->end_mark;
		skip_token(parser);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ == yaml_ANCHOR_TOKEN) {
			anchor = token->value;
			end_mark = token->end_mark;
			skip_token(parser);
			token = peek_token(parser);
			if (!token)
				return false;
		}
	}

	Str tag = {NULL, 0};
	if (tag_token) {
		if (tag_handle.n == 0) {
			tag = tag_suffix;
		} else {
			for (int i = 0; i < parser->n_tag_directives; i++) {
				if (str_equal(parser->tag_directives[i].handle, tag_handle)) {
					tag = yaml_str_cat(parser->tag_directives[i].prefix, tag_suffix);
					break;
				}
			}
			if (tag.n == 0) {
				set_parser_error_context(parser, "while parsing a node", start_mark, "found undefined tag handle", tag_mark);
				return false;
			}
		}
	}

	bool implicit = tag.n == 0;
	if (indentless_sequence && token->typ == yaml_BLOCK_ENTRY_TOKEN) {
		end_mark = token->end_mark;
		parser->state = yaml_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE;
		*event = (yaml_event_t){.typ = yaml_SEQUENCE_START_EVENT, .start_mark = start_mark, .end_mark = end_mark, .anchor = anchor,
					.tag = tag, .implicit = implicit, .style = yaml_BLOCK_SEQUENCE_STYLE};
		return true;
	}
	if (token->typ == yaml_SCALAR_TOKEN) {
		bool plain_implicit = false, quoted_implicit = false;
		end_mark = token->end_mark;
		if ((tag.n == 0 && token->style == yaml_PLAIN_SCALAR_STYLE) || (tag.n == 1 && tag.p[0] == '!'))
			plain_implicit = true;
		else if (tag.n == 0)
			quoted_implicit = true;
		pop_state(parser);

		*event = (yaml_event_t){.typ = yaml_SCALAR_EVENT,
					.start_mark = start_mark,
					.end_mark = end_mark,
					.anchor = anchor,
					.tag = tag,
					.value = token->value,
					.implicit = plain_implicit,
					.quoted_implicit = quoted_implicit,
					.style = token->style};
		set_event_comments(parser, event);
		skip_token(parser);
		return true;
	}
	if (token->typ == yaml_FLOW_SEQUENCE_START_TOKEN) {
		/* [Go] Some of the events below can be merged as they differ only on style. */
		end_mark = token->end_mark;
		parser->state = yaml_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE;
		*event = (yaml_event_t){.typ = yaml_SEQUENCE_START_EVENT, .start_mark = start_mark, .end_mark = end_mark, .anchor = anchor,
					.tag = tag, .implicit = implicit, .style = yaml_FLOW_SEQUENCE_STYLE};
		set_event_comments(parser, event);
		return true;
	}
	if (token->typ == yaml_FLOW_MAPPING_START_TOKEN) {
		end_mark = token->end_mark;
		parser->state = yaml_PARSE_FLOW_MAPPING_FIRST_KEY_STATE;
		*event = (yaml_event_t){.typ = yaml_MAPPING_START_EVENT, .start_mark = start_mark, .end_mark = end_mark, .anchor = anchor,
					.tag = tag, .implicit = implicit, .style = yaml_FLOW_MAPPING_STYLE};
		set_event_comments(parser, event);
		return true;
	}
	if (block && token->typ == yaml_BLOCK_SEQUENCE_START_TOKEN) {
		end_mark = token->end_mark;
		parser->state = yaml_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE;
		*event = (yaml_event_t){.typ = yaml_SEQUENCE_START_EVENT, .start_mark = start_mark, .end_mark = end_mark, .anchor = anchor,
					.tag = tag, .implicit = implicit, .style = yaml_BLOCK_SEQUENCE_STYLE};
		if (parser->stem_comment.n > 0) {
			event->head_comment = parser->stem_comment;
			parser->stem_comment = (Str){NULL, 0};
		}
		return true;
	}
	if (block && token->typ == yaml_BLOCK_MAPPING_START_TOKEN) {
		end_mark = token->end_mark;
		parser->state = yaml_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE;
		*event = (yaml_event_t){.typ = yaml_MAPPING_START_EVENT, .start_mark = start_mark, .end_mark = end_mark, .anchor = anchor,
					.tag = tag, .implicit = implicit, .style = yaml_BLOCK_MAPPING_STYLE};
		if (parser->stem_comment.n > 0) {
			event->head_comment = parser->stem_comment;
			parser->stem_comment = (Str){NULL, 0};
		}
		return true;
	}
	if (anchor.n > 0 || tag.n > 0) {
		pop_state(parser);
		*event = (yaml_event_t){.typ = yaml_SCALAR_EVENT,
					.start_mark = start_mark,
					.end_mark = end_mark,
					.anchor = anchor,
					.tag = tag,
					.implicit = implicit,
					.quoted_implicit = false,
					.style = yaml_PLAIN_SCALAR_STYLE};
		return true;
	}

	const char *context = "while parsing a flow node";
	if (block)
		context = "while parsing a block node";
	set_parser_error_context(parser, context, start_mark, "did not find expected node content", token->start_mark);
	return false;
}

static void split_stem_comment(yaml_parser_t *parser, size_t stem_len);

/* Parse the productions:
 * block_sequence ::= BLOCK-SEQUENCE-START (BLOCK-ENTRY block_node?)* BLOCK-END
 *                    ********************  *********** *             ********* */
static bool parse_block_sequence_entry(yaml_parser_t *parser, yaml_event_t *event, bool first)
{
	if (first) {
		yaml_token_t *token = peek_token(parser);
		if (!token)
			return false;
		push_mark(parser, token->start_mark);
		skip_token(parser);
	}

	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	if (token->typ == yaml_BLOCK_ENTRY_TOKEN) {
		yaml_mark_t mark = token->end_mark;
		size_t prior_head_len = parser->head_comment.n;
		skip_token(parser);
		split_stem_comment(parser, prior_head_len);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ != yaml_BLOCK_ENTRY_TOKEN && token->typ != yaml_BLOCK_END_TOKEN) {
			push_state(parser, yaml_PARSE_BLOCK_SEQUENCE_ENTRY_STATE);
			return parse_node(parser, event, true, false);
		} else {
			parser->state = yaml_PARSE_BLOCK_SEQUENCE_ENTRY_STATE;
			return process_empty_scalar(parser, event, mark);
		}
	}
	if (token->typ == yaml_BLOCK_END_TOKEN) {
		pop_state(parser);
		pop_mark(parser);

		*event = (yaml_event_t){.typ = yaml_SEQUENCE_END_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark};

		skip_token(parser);
		return true;
	}

	yaml_mark_t context_mark = pop_mark(parser);
	return set_parser_error_context(parser, "while parsing a block collection", context_mark, "did not find expected '-' indicator",
					token->start_mark);
}

/* Parse the productions:
 * indentless_sequence  ::= (BLOCK-ENTRY block_node?)+
 *                           *********** * */
static bool parse_indentless_sequence_entry(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	if (token->typ == yaml_BLOCK_ENTRY_TOKEN) {
		yaml_mark_t mark = token->end_mark;
		size_t prior_head_len = parser->head_comment.n;
		skip_token(parser);
		split_stem_comment(parser, prior_head_len);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ != yaml_BLOCK_ENTRY_TOKEN && token->typ != yaml_KEY_TOKEN && token->typ != yaml_VALUE_TOKEN &&
		    token->typ != yaml_BLOCK_END_TOKEN) {
			push_state(parser, yaml_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE);
			return parse_node(parser, event, true, false);
		}
		parser->state = yaml_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE;
		return process_empty_scalar(parser, event, mark);
	}
	pop_state(parser);

	*event = (yaml_event_t){.typ = yaml_SEQUENCE_END_EVENT, .start_mark = token->start_mark,
				.end_mark = token->start_mark}; /* [Go] Shouldn't this be token.end_mark? */
	return true;
}

/* Split stem comment from head comment.
 *
 * When a sequence or map is found under a sequence entry, the former head comment is assigned
 * to the underlying sequence or map as a whole, not the individual sequence or map entry as
 * would be expected otherwise. To handle this case the previous head comment is moved aside as
 * the stem comment. */
static void split_stem_comment(yaml_parser_t *parser, size_t stem_len)
{
	if (stem_len == 0)
		return;

	yaml_token_t *token = peek_token(parser);
	if (!token || (token->typ != yaml_BLOCK_SEQUENCE_START_TOKEN && token->typ != yaml_BLOCK_MAPPING_START_TOKEN))
		return;

	parser->stem_comment = (Str){parser->head_comment.p, stem_len};
	if (parser->head_comment.n == stem_len) {
		parser->head_comment = (Str){NULL, 0};
	} else {
		/* Copy suffix to prevent very strange bugs if someone ever appends further to the
		 * stem comment. */
		parser->head_comment = yaml_str_cat((Str){NULL, 0}, (Str){parser->head_comment.p + stem_len + 1, parser->head_comment.n - stem_len - 1});
	}
}

/* Parse the productions:
 * block_mapping        ::= BLOCK-MAPPING_START
 *                          *******************
 *                          ((KEY block_node_or_indentless_sequence?)?
 *                            *** *
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *
 *                          BLOCK-END
 *                          ********* */
static bool parse_block_mapping_key(yaml_parser_t *parser, yaml_event_t *event, bool first)
{
	if (first) {
		yaml_token_t *token = peek_token(parser);
		if (!token)
			return false;
		push_mark(parser, token->start_mark);
		skip_token(parser);
	}

	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	/* [Go] A tail comment was left from the prior mapping value processed. Emit an event as it
	 * needs to be processed with that value and not the following key. */
	if (parser->tail_comment.n > 0) {
		*event = (yaml_event_t){.typ = yaml_TAIL_COMMENT_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark,
					.foot_comment = parser->tail_comment};
		parser->tail_comment = (Str){NULL, 0};
		return true;
	}

	if (token->typ == yaml_KEY_TOKEN) {
		yaml_mark_t mark = token->end_mark;
		skip_token(parser);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ != yaml_KEY_TOKEN && token->typ != yaml_VALUE_TOKEN && token->typ != yaml_BLOCK_END_TOKEN) {
			push_state(parser, yaml_PARSE_BLOCK_MAPPING_VALUE_STATE);
			return parse_node(parser, event, true, true);
		} else {
			parser->state = yaml_PARSE_BLOCK_MAPPING_VALUE_STATE;
			return process_empty_scalar(parser, event, mark);
		}
	} else if (token->typ == yaml_BLOCK_END_TOKEN) {
		pop_state(parser);
		pop_mark(parser);
		*event = (yaml_event_t){.typ = yaml_MAPPING_END_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark};
		set_event_comments(parser, event);
		skip_token(parser);
		return true;
	}

	yaml_mark_t context_mark = pop_mark(parser);
	return set_parser_error_context(parser, "while parsing a block mapping", context_mark, "did not find expected key", token->start_mark);
}

/* Parse the productions:
 * block_mapping        ::= BLOCK-MAPPING_START
 *
 *                          ((KEY block_node_or_indentless_sequence?)?
 *
 *                          (VALUE block_node_or_indentless_sequence?)?)*
 *                           ***** *
 *                          BLOCK-END */
static bool parse_block_mapping_value(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	if (token->typ == yaml_VALUE_TOKEN) {
		yaml_mark_t mark = token->end_mark;
		skip_token(parser);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ != yaml_KEY_TOKEN && token->typ != yaml_VALUE_TOKEN && token->typ != yaml_BLOCK_END_TOKEN) {
			push_state(parser, yaml_PARSE_BLOCK_MAPPING_KEY_STATE);
			return parse_node(parser, event, true, true);
		}
		parser->state = yaml_PARSE_BLOCK_MAPPING_KEY_STATE;
		return process_empty_scalar(parser, event, mark);
	}
	parser->state = yaml_PARSE_BLOCK_MAPPING_KEY_STATE;
	return process_empty_scalar(parser, event, token->start_mark);
}

/* Parse the productions:
 * flow_sequence        ::= FLOW-SEQUENCE-START
 *                          *******************
 *                          (flow_sequence_entry FLOW-ENTRY)*
 *                           *                   **********
 *                          flow_sequence_entry?
 *                          *
 *                          FLOW-SEQUENCE-END
 *                          *****************
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                          * */
static bool parse_flow_sequence_entry(yaml_parser_t *parser, yaml_event_t *event, bool first)
{
	if (first) {
		yaml_token_t *token = peek_token(parser);
		if (!token)
			return false;
		push_mark(parser, token->start_mark);
		skip_token(parser);
	}
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	if (token->typ != yaml_FLOW_SEQUENCE_END_TOKEN) {
		if (!first) {
			if (token->typ == yaml_FLOW_ENTRY_TOKEN) {
				skip_token(parser);
				token = peek_token(parser);
				if (!token)
					return false;
			} else {
				yaml_mark_t context_mark = pop_mark(parser);
				return set_parser_error_context(parser, "while parsing a flow sequence", context_mark,
								"did not find expected ',' or ']'", token->start_mark);
			}
		}

		if (token->typ == yaml_KEY_TOKEN) {
			parser->state = yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE;
			*event = (yaml_event_t){.typ = yaml_MAPPING_START_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark,
						.implicit = true, .style = yaml_FLOW_MAPPING_STYLE};
			skip_token(parser);
			return true;
		} else if (token->typ != yaml_FLOW_SEQUENCE_END_TOKEN) {
			push_state(parser, yaml_PARSE_FLOW_SEQUENCE_ENTRY_STATE);
			return parse_node(parser, event, false, false);
		}
	}

	pop_state(parser);
	pop_mark(parser);

	*event = (yaml_event_t){.typ = yaml_SEQUENCE_END_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark};
	set_event_comments(parser, event);

	skip_token(parser);
	return true;
}

/* Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                      *** * */
static bool parse_flow_sequence_entry_mapping_key(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	if (token->typ != yaml_VALUE_TOKEN && token->typ != yaml_FLOW_ENTRY_TOKEN && token->typ != yaml_FLOW_SEQUENCE_END_TOKEN) {
		push_state(parser, yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE);
		return parse_node(parser, event, false, false);
	}
	yaml_mark_t mark = token->end_mark;
	skip_token(parser);
	parser->state = yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE;
	return process_empty_scalar(parser, event, mark);
}

/* Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                                      ***** * */
static bool parse_flow_sequence_entry_mapping_value(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	if (token->typ == yaml_VALUE_TOKEN) {
		skip_token(parser);
		yaml_token_t *t2 = peek_token(parser);
		if (!t2)
			return false;
		if (t2->typ != yaml_FLOW_ENTRY_TOKEN && t2->typ != yaml_FLOW_SEQUENCE_END_TOKEN) {
			push_state(parser, yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE);
			return parse_node(parser, event, false, false);
		}
	}
	parser->state = yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE;
	/* token: the VALUE token (Go's shadowed variable) or the current one */
	return process_empty_scalar(parser, event, token->start_mark);
}

/* Parse the productions:
 * flow_sequence_entry  ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                                                      * */
static bool parse_flow_sequence_entry_mapping_end(yaml_parser_t *parser, yaml_event_t *event)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	parser->state = yaml_PARSE_FLOW_SEQUENCE_ENTRY_STATE;
	*event = (yaml_event_t){.typ = yaml_MAPPING_END_EVENT, .start_mark = token->start_mark,
				.end_mark = token->start_mark}; /* [Go] Shouldn't this be end_mark? */
	return true;
}

/* Parse the productions:
 * flow_mapping         ::= FLOW-MAPPING-START
 *                          ******************
 *                          (flow_mapping_entry FLOW-ENTRY)*
 *                           *                  **********
 *                          flow_mapping_entry?
 *                          ******************
 *                          FLOW-MAPPING-END
 *                          ****************
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                          *           *** * */
static bool parse_flow_mapping_key(yaml_parser_t *parser, yaml_event_t *event, bool first)
{
	if (first) {
		yaml_token_t *token = peek_token(parser);
		if (!token)
			fail("yaml: nil token"); /* a nil pointer dereference in Go (never reached) */
		push_mark(parser, token->start_mark);
		skip_token(parser);
	}

	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	if (token->typ != yaml_FLOW_MAPPING_END_TOKEN) {
		if (!first) {
			if (token->typ == yaml_FLOW_ENTRY_TOKEN) {
				skip_token(parser);
				token = peek_token(parser);
				if (!token)
					return false;
			} else {
				yaml_mark_t context_mark = pop_mark(parser);
				return set_parser_error_context(parser, "while parsing a flow mapping", context_mark,
								"did not find expected ',' or '}'", token->start_mark);
			}
		}

		if (token->typ == yaml_KEY_TOKEN) {
			skip_token(parser);
			token = peek_token(parser);
			if (!token)
				return false;
			if (token->typ != yaml_VALUE_TOKEN && token->typ != yaml_FLOW_ENTRY_TOKEN && token->typ != yaml_FLOW_MAPPING_END_TOKEN) {
				push_state(parser, yaml_PARSE_FLOW_MAPPING_VALUE_STATE);
				return parse_node(parser, event, false, false);
			} else {
				parser->state = yaml_PARSE_FLOW_MAPPING_VALUE_STATE;
				return process_empty_scalar(parser, event, token->start_mark);
			}
		} else if (token->typ != yaml_FLOW_MAPPING_END_TOKEN) {
			push_state(parser, yaml_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE);
			return parse_node(parser, event, false, false);
		}
	}

	pop_state(parser);
	pop_mark(parser);
	*event = (yaml_event_t){.typ = yaml_MAPPING_END_EVENT, .start_mark = token->start_mark, .end_mark = token->end_mark};
	set_event_comments(parser, event);
	skip_token(parser);
	return true;
}

/* Parse the productions:
 * flow_mapping_entry   ::= flow_node | KEY flow_node? (VALUE flow_node?)?
 *                                   *                  ***** * */
static bool parse_flow_mapping_value(yaml_parser_t *parser, yaml_event_t *event, bool empty)
{
	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;
	if (empty) {
		parser->state = yaml_PARSE_FLOW_MAPPING_KEY_STATE;
		return process_empty_scalar(parser, event, token->start_mark);
	}
	if (token->typ == yaml_VALUE_TOKEN) {
		skip_token(parser);
		token = peek_token(parser);
		if (!token)
			return false;
		if (token->typ != yaml_FLOW_ENTRY_TOKEN && token->typ != yaml_FLOW_MAPPING_END_TOKEN) {
			push_state(parser, yaml_PARSE_FLOW_MAPPING_KEY_STATE);
			return parse_node(parser, event, false, false);
		}
	}
	parser->state = yaml_PARSE_FLOW_MAPPING_KEY_STATE;
	return process_empty_scalar(parser, event, token->start_mark);
}

/* Generate an empty scalar event. */
static bool process_empty_scalar(yaml_parser_t *parser, yaml_event_t *event, yaml_mark_t mark)
{
	(void)parser;
	*event = (yaml_event_t){.typ = yaml_SCALAR_EVENT, .start_mark = mark, .end_mark = mark, .implicit = true, .style = yaml_PLAIN_SCALAR_STYLE};
	return true;
}

static const yaml_tag_directive_t parser_default_tag_directives[] = {
	{{"!", 1}, {"!", 1}},
	{{"!!", 2}, {"tag:yaml.org,2002:", 18}},
};

static bool append_tag_directive(yaml_parser_t *parser, yaml_tag_directive_t value, bool allow_duplicates, yaml_mark_t mark)
{
	for (int i = 0; i < parser->n_tag_directives; i++) {
		if (str_equal(value.handle, parser->tag_directives[i].handle)) {
			if (allow_duplicates)
				return true;
			return set_parser_error(parser, "found duplicate %TAG directive", mark);
		}
	}
	/* [Go] I suspect the copy is unnecessary. This was likely done because there was no way
	 * to track ownership of the data. */
	if (parser->n_tag_directives == parser->cap_tag_directives) {
		int cap = parser->cap_tag_directives ? parser->cap_tag_directives * 2 : 4;
		parser->tag_directives = xrealloc(parser->tag_directives, sizeof *parser->tag_directives * (size_t)parser->cap_tag_directives,
						  sizeof *parser->tag_directives * (size_t)cap);
		parser->cap_tag_directives = cap;
	}
	parser->tag_directives[parser->n_tag_directives++] = value;
	return true;
}

/* Parse directives. */
static bool process_directives(yaml_parser_t *parser, yaml_version_directive_t **version_directive_ref,
			       yaml_tag_directive_t **tag_directives_ref, int *n_tag_directives_ref)
{
	yaml_version_directive_t *version_directive = NULL;
	yaml_tag_directive_t *tag_directives = NULL;
	int n_tag_directives = 0;

	yaml_token_t *token = peek_token(parser);
	if (!token)
		return false;

	while (token->typ == yaml_VERSION_DIRECTIVE_TOKEN || token->typ == yaml_TAG_DIRECTIVE_TOKEN) {
		if (token->typ == yaml_VERSION_DIRECTIVE_TOKEN) {
			if (version_directive) {
				set_parser_error(parser, "found duplicate %YAML directive", token->start_mark);
				return false;
			}
			if (token->major != 1 || token->minor != 1) {
				set_parser_error(parser, "found incompatible YAML document", token->start_mark);
				return false;
			}
			version_directive = xalloc(sizeof *version_directive);
			version_directive->major = token->major;
			version_directive->minor = token->minor;
		} else if (token->typ == yaml_TAG_DIRECTIVE_TOKEN) {
			yaml_tag_directive_t value = {token->value, token->prefix};
			if (!append_tag_directive(parser, value, false, token->start_mark))
				return false;
			tag_directives = xrealloc(tag_directives, sizeof *tag_directives * (size_t)n_tag_directives,
						  sizeof *tag_directives * (size_t)(n_tag_directives + 1));
			tag_directives[n_tag_directives++] = value;
		}

		skip_token(parser);
		token = peek_token(parser);
		if (!token)
			return false;
	}

	for (size_t i = 0; i < sizeof parser_default_tag_directives / sizeof *parser_default_tag_directives; i++)
		if (!append_tag_directive(parser, parser_default_tag_directives[i], true, token->start_mark))
			return false;

	if (version_directive_ref)
		*version_directive_ref = version_directive;
	if (tag_directives_ref) {
		*tag_directives_ref = tag_directives;
		*n_tag_directives_ref = n_tag_directives;
	}
	return true;
}
