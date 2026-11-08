/* Copyright (c) 2011-2019 Canonical Ltd
 * Copyright (c) 2006-2010 Kirill Simonov
 * Licensed under the Apache License, Version 2.0 and the MIT License (see LICENSE of
 * go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 scannerc.go (with yaml_insert_token of apic.go): the
 * scanner, which produces the tokens and collects the comments of the YAML stream. Byte slices
 * that grow are Buf; the token values and comments are Str views of them. */
#include "internal.h"

#define BUF (yaml_pbuf(parser))
#define POS (parser->buffer_pos)
#define AT(k) yaml_at(BUF, POS + (k))
#define CACHE(n) (parser->unread >= (n) || yaml_parser_update_buffer(parser, (n)))

/* ---- map[int]int ---- */

static void intmap_grow(yaml_intmap *m)
{
	int cap = m->cap ? m->cap * 2 : 16;
	int *keys = xalloc(sizeof *keys * (size_t)cap), *vals = xalloc(sizeof *vals * (size_t)cap);
	for (int i = 0; i < cap; i++)
		keys[i] = -1;
	for (int i = 0; i < m->cap; i++) {
		if (m->keys[i] < 0)
			continue;
		unsigned h = (unsigned)m->keys[i] * 2654435761u & (unsigned)(cap - 1);
		while (keys[h] >= 0)
			h = (h + 1) & (unsigned)(cap - 1);
		keys[h] = m->keys[i];
		vals[h] = m->vals[i];
	}
	m->keys = keys, m->vals = vals, m->cap = cap, m->used = m->n;
}

static int *intmap_find(const yaml_intmap *m, int key)
{
	if (!m->cap)
		return NULL;
	unsigned h = (unsigned)key * 2654435761u & (unsigned)(m->cap - 1);
	while (m->keys[h] != -1) {
		if (m->keys[h] == key)
			return &m->vals[h];
		h = (h + 1) & (unsigned)(m->cap - 1);
	}
	return NULL;
}

static void intmap_put(yaml_intmap *m, int key, int val)
{
	int *v = intmap_find(m, key);
	if (v) {
		*v = val;
		return;
	}
	if ((m->used + 1) * 2 > m->cap)
		intmap_grow(m);
	unsigned h = (unsigned)key * 2654435761u & (unsigned)(m->cap - 1);
	while (m->keys[h] >= 0)
		h = (h + 1) & (unsigned)(m->cap - 1);
	if (m->keys[h] == -1)
		m->used++;
	m->keys[h] = key;
	m->vals[h] = val;
	m->n++;
}

static void intmap_delete(yaml_intmap *m, int key)
{
	int *v = intmap_find(m, key);
	if (!v)
		return;
	m->keys[v - m->vals] = -2;
	m->n--;
}

/* ---- buffer helpers ---- */

Str yaml_str_cat(Str a, Str b)
{
	Buf r = {0};
	buf_add(&r, a.p ? a.p : "", a.n);
	buf_add(&r, b.p ? b.p : "", b.n);
	return buf_view(&r);
}

Str yaml_str_catc(Str a, char c) { return yaml_str_cat(a, (Str){&c, 1}); }

/* Advance the buffer pointer. */
static void skip(yaml_parser_t *parser)
{
	if (!is_blank(BUF, POS))
		parser->newlines = 0;
	parser->mark.index++;
	parser->mark.column++;
	parser->unread--;
	int w = yaml_width(AT(0));
	POS += (size_t)(w ? w : 1);
}

static void skip_line(yaml_parser_t *parser)
{
	if (is_crlf(BUF, POS)) {
		parser->mark.index += 2;
		parser->mark.column = 0;
		parser->mark.line++;
		parser->unread -= 2;
		POS += 2;
		parser->newlines++;
	} else if (is_break(BUF, POS)) {
		parser->mark.index++;
		parser->mark.column = 0;
		parser->mark.line++;
		parser->unread--;
		POS += (size_t)yaml_width(AT(0));
		parser->newlines++;
	}
}

/* Copy a character to a string buffer and advance pointers. */
static void read1(yaml_parser_t *parser, Buf *s)
{
	if (!is_blank(BUF, POS))
		parser->newlines = 0;
	int w = yaml_width(AT(0));
	if (w == 0)
		fail("invalid character sequence");
	for (int k = 0; k < w; k++)
		buf_addc(s, (char)AT(k));
	POS += (size_t)w;
	parser->mark.index++;
	parser->mark.column++;
	parser->unread--;
}

/* Copy a line break character to a string buffer and advance pointers. */
static void read_line(yaml_parser_t *parser, Buf *s)
{
	uint8_t c0 = AT(0), c1 = AT(1), c2 = AT(2);
	if (c0 == '\r' && c1 == '\n') {
		/* CR LF . LF */
		buf_addc(s, '\n');
		POS += 2;
		parser->mark.index++;
		parser->unread--;
	} else if (c0 == '\r' || c0 == '\n') {
		/* CR|LF . LF */
		buf_addc(s, '\n');
		POS += 1;
	} else if (c0 == 0xC2 && c1 == 0x85) {
		/* NEL . LF */
		buf_addc(s, '\n');
		POS += 2;
	} else if (c0 == 0xE2 && c1 == 0x80 && (c2 == 0xA8 || c2 == 0xA9)) {
		/* LS|PS . LS|PS */
		buf_add(s, BUF.p + POS, 3);
		POS += 3;
	} else {
		return;
	}
	parser->mark.index++;
	parser->mark.column = 0;
	parser->mark.line++;
	parser->unread--;
	parser->newlines++;
}

static Str buf_take(Buf *b) { return b->n ? buf_view(b) : (Str){NULL, 0}; }

/* ---- tokens ---- */

static void insert_token(yaml_parser_t *parser, int pos, const yaml_token_t *token)
{
	/* Check if we can move the queue at the beginning of the buffer. */
	if (parser->tokens_head > 0 && parser->n_tokens == parser->cap_tokens) {
		if (parser->tokens_head != parser->n_tokens)
			memmove(parser->tokens, parser->tokens + parser->tokens_head,
				sizeof *parser->tokens * (size_t)(parser->n_tokens - parser->tokens_head));
		parser->n_tokens -= parser->tokens_head;
		parser->tokens_head = 0;
	}
	if (parser->n_tokens == parser->cap_tokens) {
		int cap = parser->cap_tokens ? parser->cap_tokens * 2 : 16;
		parser->tokens = xrealloc(parser->tokens, sizeof *parser->tokens * (size_t)parser->cap_tokens, sizeof *parser->tokens * (size_t)cap);
		parser->cap_tokens = cap;
	}
	parser->tokens[parser->n_tokens++] = *token;
	if (pos < 0)
		return;
	int at = parser->tokens_head + pos;
	memmove(parser->tokens + at + 1, parser->tokens + at, sizeof *parser->tokens * (size_t)(parser->n_tokens - 1 - at));
	parser->tokens[at] = *token;
}

static bool set_scanner_error(yaml_parser_t *parser, const char *context, yaml_mark_t context_mark, const char *problem)
{
	parser->error = yaml_SCANNER_ERROR;
	parser->context = context;
	parser->context_mark = context_mark;
	parser->problem = problem;
	parser->problem_mark = parser->mark;
	return false;
}

static bool set_scanner_tag_error(yaml_parser_t *parser, bool directive, yaml_mark_t context_mark, const char *problem)
{
	const char *context = "while parsing a tag";
	if (directive)
		context = "while parsing a %TAG directive";
	return set_scanner_error(parser, context, context_mark, problem);
}

static bool fetch_next_token(yaml_parser_t *parser);
static bool simple_key_is_valid(yaml_parser_t *parser, yaml_simple_key_t *simple_key, bool *valid);

/* Ensure that the tokens queue contains at least one token which can be returned to the
 * Parser. */
bool yaml_parser_fetch_more_tokens(yaml_parser_t *parser)
{
	/* While we need more tokens to fetch, do it. */
	for (;;) {
		/* [Go] The comment parsing logic requires a lookahead of two tokens so that foot
		 * comments may be parsed in time of associating them with the tokens that are
		 * parsed before them, and also for line comments to be transformed into head
		 * comments in some edge cases. */
		if (parser->tokens_head < parser->n_tokens - 2) {
			/* If a potential simple key is at the head position, we need to fetch the
			 * next token to disambiguate it. */
			int *head_tok_idx = intmap_find(&parser->simple_keys_by_tok, parser->tokens_parsed);
			if (!head_tok_idx)
				break;
			bool valid;
			if (!simple_key_is_valid(parser, &parser->simple_keys[*head_tok_idx], &valid))
				return false;
			if (!valid)
				break;
		}
		/* Fetch the next token. */
		if (!fetch_next_token(parser))
			return false;
	}
	parser->token_available = true;
	return true;
}

static bool fetch_stream_start(yaml_parser_t *parser);
static bool fetch_stream_end(yaml_parser_t *parser);
static bool fetch_directive(yaml_parser_t *parser);
static bool fetch_document_indicator(yaml_parser_t *parser, yaml_token_type_t typ);
static bool fetch_flow_collection_start(yaml_parser_t *parser, yaml_token_type_t typ);
static bool fetch_flow_collection_end(yaml_parser_t *parser, yaml_token_type_t typ);
static bool fetch_flow_entry(yaml_parser_t *parser);
static bool fetch_block_entry(yaml_parser_t *parser);
static bool fetch_key(yaml_parser_t *parser);
static bool fetch_value(yaml_parser_t *parser);
static bool fetch_anchor(yaml_parser_t *parser, yaml_token_type_t typ);
static bool fetch_tag(yaml_parser_t *parser);
static bool fetch_block_scalar(yaml_parser_t *parser, bool literal);
static bool fetch_flow_scalar(yaml_parser_t *parser, bool single);
static bool fetch_plain_scalar(yaml_parser_t *parser);
static bool scan_to_next_token(yaml_parser_t *parser);
static bool unroll_indent(yaml_parser_t *parser, int column, yaml_mark_t scan_mark);
static bool scan_line_comment(yaml_parser_t *parser, yaml_mark_t token_mark);
static bool scan_comments(yaml_parser_t *parser, yaml_mark_t scan_mark);

static bool fetch_next_token_body(yaml_parser_t *parser);

/* The dispatcher for token fetchers. */
static bool fetch_next_token(yaml_parser_t *parser)
{
	/* Ensure that the buffer is initialized. */
	if (parser->unread < 1 && !yaml_parser_update_buffer(parser, 1))
		return false;

	/* Check if we just started scanning. Fetch STREAM-START then. */
	if (!parser->stream_start_produced)
		return fetch_stream_start(parser);

	yaml_mark_t scan_mark = parser->mark;

	/* Eat whitespaces and comments until we reach the next token. */
	if (!scan_to_next_token(parser))
		return false;

	/* [Go] While unrolling indents, transform the head comments of prior indentation levels
	 * observed after scan_start into foot comments at the respective indexes. */

	/* Remove obsolete potential simple keys. */
	if (!unroll_indent(parser, parser->mark.column, scan_mark))
		return false;

	/* Ensure that the buffer contains at least 4 characters. 4 is the length of the longest
	 * indicators ('--- ' and '... '). */
	if (parser->unread < 4 && !yaml_parser_update_buffer(parser, 4))
		return false;

	/* Is it the end of the stream? */
	if (is_z(BUF, POS))
		return fetch_stream_end(parser);

	/* Is it a directive? */
	if (parser->mark.column == 0 && AT(0) == '%')
		return fetch_directive(parser);

	/* Is it the document start indicator? */
	if (parser->mark.column == 0 && AT(0) == '-' && AT(1) == '-' && AT(2) == '-' && is_blankz(BUF, POS + 3))
		return fetch_document_indicator(parser, yaml_DOCUMENT_START_TOKEN);

	/* Is it the document end indicator? */
	if (parser->mark.column == 0 && AT(0) == '.' && AT(1) == '.' && AT(2) == '.' && is_blankz(BUF, POS + 3))
		return fetch_document_indicator(parser, yaml_DOCUMENT_END_TOKEN);

	yaml_mark_t comment_mark = parser->mark;
	if (parser->n_tokens > 0 && ((parser->flow_level == 0 && AT(0) == ':') || (parser->flow_level > 0 && AT(0) == ','))) {
		/* Associate any following comments with the prior token. */
		comment_mark = parser->tokens[parser->n_tokens - 1].start_mark;
	}

	bool ok = fetch_next_token_body(parser);
	/* the deferred function */
	if (!ok)
		return false;
	if (parser->n_tokens > 0 && parser->tokens[parser->n_tokens - 1].typ == yaml_BLOCK_ENTRY_TOKEN) {
		/* Sequence indicators alone have no line comments. It becomes a head comment for
		 * whatever follows. */
		return true;
	}
	if (!scan_line_comment(parser, comment_mark))
		return false;
	return true;
}

static bool fetch_next_token_body(yaml_parser_t *parser)
{
	uint8_t c = AT(0);
	/* Is it the flow sequence start indicator? */
	if (c == '[')
		return fetch_flow_collection_start(parser, yaml_FLOW_SEQUENCE_START_TOKEN);
	/* Is it the flow mapping start indicator? */
	if (c == '{')
		return fetch_flow_collection_start(parser, yaml_FLOW_MAPPING_START_TOKEN);
	/* Is it the flow sequence end indicator? */
	if (c == ']')
		return fetch_flow_collection_end(parser, yaml_FLOW_SEQUENCE_END_TOKEN);
	/* Is it the flow mapping end indicator? */
	if (c == '}')
		return fetch_flow_collection_end(parser, yaml_FLOW_MAPPING_END_TOKEN);
	/* Is it the flow entry indicator? */
	if (c == ',')
		return fetch_flow_entry(parser);
	/* Is it the block entry indicator? */
	if (c == '-' && is_blankz(BUF, POS + 1))
		return fetch_block_entry(parser);
	/* Is it the key indicator? */
	if (c == '?' && (parser->flow_level > 0 || is_blankz(BUF, POS + 1)))
		return fetch_key(parser);
	/* Is it the value indicator? */
	if (c == ':' && (parser->flow_level > 0 || is_blankz(BUF, POS + 1)))
		return fetch_value(parser);
	/* Is it an alias? */
	if (c == '*')
		return fetch_anchor(parser, yaml_ALIAS_TOKEN);
	/* Is it an anchor? */
	if (c == '&')
		return fetch_anchor(parser, yaml_ANCHOR_TOKEN);
	/* Is it a tag? */
	if (c == '!')
		return fetch_tag(parser);
	/* Is it a literal scalar? */
	if (c == '|' && parser->flow_level == 0)
		return fetch_block_scalar(parser, true);
	/* Is it a folded scalar? */
	if (c == '>' && parser->flow_level == 0)
		return fetch_block_scalar(parser, false);
	/* Is it a single-quoted scalar? */
	if (c == '\'')
		return fetch_flow_scalar(parser, true);
	/* Is it a double-quoted scalar? */
	if (c == '"')
		return fetch_flow_scalar(parser, false);

	/* Is it a plain scalar?
	 *
	 * A plain scalar may start with any non-blank characters except
	 *
	 *      '-', '?', ':', ',', '[', ']', '{', '}',
	 *      '#', '&', '*', '!', '|', '>', '\'', '\"',
	 *      '%', '@', '`'.
	 *
	 * In the block context (and, for the '-' indicator, in the flow context too), it may also
	 * start with the characters
	 *
	 *      '-', '?', ':'
	 *
	 * if it is followed by a non-space character.
	 *
	 * The last rule is more restrictive than the specification requires. [Go] TODO Make this
	 * logic more reasonable. */
	if (!(is_blankz(BUF, POS) || c == '-' || c == '?' || c == ':' || c == ',' || c == '[' || c == ']' || c == '{' || c == '}' ||
	      c == '#' || c == '&' || c == '*' || c == '!' || c == '|' || c == '>' || c == '\'' || c == '"' || c == '%' || c == '@' ||
	      c == '`') ||
	    (c == '-' && !is_blank(BUF, POS + 1)) ||
	    (parser->flow_level == 0 && (c == '?' || c == ':') && !is_blankz(BUF, POS + 1)))
		return fetch_plain_scalar(parser);

	/* If we don't determine the token type so far, it is an error. */
	return set_scanner_error(parser, "while scanning for the next token", parser->mark, "found character that cannot start any token");
}

static bool simple_key_is_valid(yaml_parser_t *parser, yaml_simple_key_t *simple_key, bool *valid)
{
	*valid = false;
	if (!simple_key->possible)
		return true;

	/* The 1.2 specification says:
	 *
	 *     "If the ? indicator is omitted, parsing needs to see past the implicit key to
	 *     recognize it as such. To limit the amount of lookahead required, the “:” indicator
	 *     must appear at most 1024 Unicode characters beyond the start of the key. In
	 *     addition, the key is restricted to a single line."
	 */
	if (simple_key->mark.line < parser->mark.line || simple_key->mark.index + 1024 < parser->mark.index) {
		/* Check if the potential simple key to be removed is required. */
		if (simple_key->required)
			return set_scanner_error(parser, "while scanning a simple key", simple_key->mark, "could not find expected ':'");
		simple_key->possible = false;
		return true;
	}
	*valid = true;
	return true;
}

/* Remove a potential simple key at the current flow level. */
static bool remove_simple_key(yaml_parser_t *parser)
{
	int i = parser->n_simple_keys - 1;
	if (parser->simple_keys[i].possible) {
		/* If the key is required, it is an error. */
		if (parser->simple_keys[i].required)
			return set_scanner_error(parser, "while scanning a simple key", parser->simple_keys[i].mark,
						 "could not find expected ':'");
		/* Remove the key from the stack. */
		parser->simple_keys[i].possible = false;
		intmap_delete(&parser->simple_keys_by_tok, parser->simple_keys[i].token_number);
	}
	return true;
}

/* Check if a simple key may start at the current position and add it if needed. */
static bool save_simple_key(yaml_parser_t *parser)
{
	/* A simple key is required at the current position if the scanner is in the block
	 * context and the current column coincides with the indentation level. */
	bool required = parser->flow_level == 0 && parser->indent == parser->mark.column;

	/* If the current position may start a simple key, save it. */
	if (parser->simple_key_allowed) {
		yaml_simple_key_t simple_key = {
			.possible = true,
			.required = required,
			.token_number = parser->tokens_parsed + (parser->n_tokens - parser->tokens_head),
			.mark = parser->mark,
		};
		if (!remove_simple_key(parser))
			return false;
		parser->simple_keys[parser->n_simple_keys - 1] = simple_key;
		intmap_put(&parser->simple_keys_by_tok, simple_key.token_number, parser->n_simple_keys - 1);
	}
	return true;
}

static void push_simple_key(yaml_parser_t *parser, yaml_simple_key_t k)
{
	if (parser->n_simple_keys == parser->cap_simple_keys) {
		int cap = parser->cap_simple_keys ? parser->cap_simple_keys * 2 : 16;
		parser->simple_keys = xrealloc(parser->simple_keys, sizeof *parser->simple_keys * (size_t)parser->cap_simple_keys,
					       sizeof *parser->simple_keys * (size_t)cap);
		parser->cap_simple_keys = cap;
	}
	parser->simple_keys[parser->n_simple_keys++] = k;
}

/* max_flow_level limits the flow_level */
#define max_flow_level 10000

/* Increase the flow level and resize the simple key list if needed. */
static bool increase_flow_level(yaml_parser_t *parser)
{
	/* Reset the simple key on the next level. */
	push_simple_key(parser, (yaml_simple_key_t){
					.possible = false,
					.required = false,
					.token_number = parser->tokens_parsed + (parser->n_tokens - parser->tokens_head),
					.mark = parser->mark,
				});
	/* Increase the flow level. */
	parser->flow_level++;
	if (parser->flow_level > max_flow_level)
		return set_scanner_error(parser, "while increasing flow level", parser->simple_keys[parser->n_simple_keys - 1].mark,
					 "exceeded max depth of 10000");
	return true;
}

/* Decrease the flow level. */
static bool decrease_flow_level(yaml_parser_t *parser)
{
	if (parser->flow_level > 0) {
		parser->flow_level--;
		int last = parser->n_simple_keys - 1;
		intmap_delete(&parser->simple_keys_by_tok, parser->simple_keys[last].token_number);
		parser->n_simple_keys = last;
	}
	return true;
}

/* max_indents limits the indents stack size */
#define max_indents 10000

/* Push the current indentation level to the stack and set the new level the current column is
 * greater than the indentation level. In this case, append or insert the specified token into
 * the token queue. */
static bool roll_indent(yaml_parser_t *parser, int column, int number, yaml_token_type_t typ, yaml_mark_t mark)
{
	/* In the flow context, do nothing. */
	if (parser->flow_level > 0)
		return true;

	if (parser->indent < column) {
		/* Push the current indentation level to the stack and set the new indentation
		 * level. */
		yaml_ints_push(&parser->indents, parser->indent);
		parser->indent = column;
		if (parser->indents.n > max_indents)
			return set_scanner_error(parser, "while increasing indent level", parser->simple_keys[parser->n_simple_keys - 1].mark,
						 "exceeded max depth of 10000");

		/* Create a token and insert it into the queue. */
		yaml_token_t token = {.typ = typ, .start_mark = mark, .end_mark = mark};
		if (number > -1)
			number -= parser->tokens_parsed;
		insert_token(parser, number, &token);
	}
	return true;
}

/* Pop indentation levels from the indents stack until the current level becomes less or equal
 * to the column. For each indentation level, append the BLOCK-END token. */
static bool unroll_indent(yaml_parser_t *parser, int column, yaml_mark_t scan_mark)
{
	/* In the flow context, do nothing. */
	if (parser->flow_level > 0)
		return true;

	yaml_mark_t block_mark = scan_mark;
	block_mark.index--;

	/* Loop through the indentation levels in the stack. */
	while (parser->indent > column) {
		/* [Go] Reposition the end token before potential following foot comments of parent
		 * blocks. For that, search backwards for recent comments that were at the same
		 * indent as the block that is ending now. */
		int stop_index = block_mark.index;
		for (int i = parser->n_comments - 1; i >= 0; i--) {
			yaml_comment_t *comment = &parser->comments[i];
			if (comment->end_mark.index < stop_index) {
				/* Don't go back beyond the start of the comment/whitespace scan, unless
				 * column < 0. If requested indent column is < 0, then the document is
				 * over and everything else is a foot anyway. */
				break;
			}
			if (comment->start_mark.column == parser->indent + 1) {
				/* This is a good match. But maybe there's a former comment at that same
				 * indent level, so keep searching. */
				block_mark = comment->start_mark;
			}
			/* While the end of the former comment matches with the start of the following
			 * one, we know there's nothing in between and scanning is still safe. */
			stop_index = comment->scan_mark.index;
		}

		/* Create a token and append it to the queue. */
		yaml_token_t token = {.typ = yaml_BLOCK_END_TOKEN, .start_mark = block_mark, .end_mark = block_mark};
		insert_token(parser, -1, &token);

		/* Pop the indentation level. */
		parser->indent = yaml_ints_pop(&parser->indents);
	}
	return true;
}

/* Initialize the scanner and produce the STREAM-START token. */
static bool fetch_stream_start(yaml_parser_t *parser)
{
	/* Set the initial indentation. */
	parser->indent = -1;

	/* Initialize the simple key stack. */
	push_simple_key(parser, (yaml_simple_key_t){0});
	parser->simple_keys_by_tok = (yaml_intmap){0};

	/* A simple key is allowed at the beginning of the stream. */
	parser->simple_key_allowed = true;

	/* We have started. */
	parser->stream_start_produced = true;

	/* Create the STREAM-START token and append it to the queue. */
	yaml_token_t token = {.typ = yaml_STREAM_START_TOKEN, .start_mark = parser->mark, .end_mark = parser->mark, .encoding = parser->encoding};
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the STREAM-END token and shut down the scanner. */
static bool fetch_stream_end(yaml_parser_t *parser)
{
	/* Force new line. */
	if (parser->mark.column != 0) {
		parser->mark.column = 0;
		parser->mark.line++;
	}

	/* Reset the indentation level. */
	if (!unroll_indent(parser, -1, parser->mark))
		return false;

	/* Reset simple keys. */
	if (!remove_simple_key(parser))
		return false;

	parser->simple_key_allowed = false;

	/* Create the STREAM-END token and append it to the queue. */
	yaml_token_t token = {.typ = yaml_STREAM_END_TOKEN, .start_mark = parser->mark, .end_mark = parser->mark};
	insert_token(parser, -1, &token);
	return true;
}

static bool scan_directive(yaml_parser_t *parser, yaml_token_t *token);

/* Produce a VERSION-DIRECTIVE or TAG-DIRECTIVE token. */
static bool fetch_directive(yaml_parser_t *parser)
{
	/* Reset the indentation level. */
	if (!unroll_indent(parser, -1, parser->mark))
		return false;

	/* Reset simple keys. */
	if (!remove_simple_key(parser))
		return false;

	parser->simple_key_allowed = false;

	/* Create the YAML-DIRECTIVE or TAG-DIRECTIVE token. */
	yaml_token_t token = {0};
	if (!scan_directive(parser, &token))
		return false;
	/* Append the token to the queue. */
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the DOCUMENT-START or DOCUMENT-END token. */
static bool fetch_document_indicator(yaml_parser_t *parser, yaml_token_type_t typ)
{
	/* Reset the indentation level. */
	if (!unroll_indent(parser, -1, parser->mark))
		return false;

	/* Reset simple keys. */
	if (!remove_simple_key(parser))
		return false;

	parser->simple_key_allowed = false;

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;

	skip(parser);
	skip(parser);
	skip(parser);

	yaml_mark_t end_mark = parser->mark;

	/* Create the DOCUMENT-START or DOCUMENT-END token. */
	yaml_token_t token = {.typ = typ, .start_mark = start_mark, .end_mark = end_mark};
	/* Append the token to the queue. */
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the FLOW-SEQUENCE-START or FLOW-MAPPING-START token. */
static bool fetch_flow_collection_start(yaml_parser_t *parser, yaml_token_type_t typ)
{
	/* The indicators '[' and '{' may start a simple key. */
	if (!save_simple_key(parser))
		return false;

	/* Increase the flow level. */
	if (!increase_flow_level(parser))
		return false;

	/* A simple key may follow the indicators '[' and '{'. */
	parser->simple_key_allowed = true;

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create the FLOW-SEQUENCE-START of FLOW-MAPPING-START token. */
	yaml_token_t token = {.typ = typ, .start_mark = start_mark, .end_mark = end_mark};
	/* Append the token to the queue. */
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the FLOW-SEQUENCE-END or FLOW-MAPPING-END token. */
static bool fetch_flow_collection_end(yaml_parser_t *parser, yaml_token_type_t typ)
{
	/* Reset any potential simple key on the current flow level. */
	if (!remove_simple_key(parser))
		return false;

	/* Decrease the flow level. */
	if (!decrease_flow_level(parser))
		return false;

	/* No simple keys after the indicators ']' and '}'. */
	parser->simple_key_allowed = false;

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create the FLOW-SEQUENCE-END of FLOW-MAPPING-END token. */
	yaml_token_t token = {.typ = typ, .start_mark = start_mark, .end_mark = end_mark};
	/* Append the token to the queue. */
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the FLOW-ENTRY token. */
static bool fetch_flow_entry(yaml_parser_t *parser)
{
	/* Reset any potential simple keys on the current flow level. */
	if (!remove_simple_key(parser))
		return false;

	/* Simple keys are allowed after ','. */
	parser->simple_key_allowed = true;

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create the FLOW-ENTRY token and append it to the queue. */
	yaml_token_t token = {.typ = yaml_FLOW_ENTRY_TOKEN, .start_mark = start_mark, .end_mark = end_mark};
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the BLOCK-ENTRY token. */
static bool fetch_block_entry(yaml_parser_t *parser)
{
	/* Check if the scanner is in the block context. */
	if (parser->flow_level == 0) {
		/* Check if we are allowed to start a new entry. */
		if (!parser->simple_key_allowed)
			return set_scanner_error(parser, "", parser->mark, "block sequence entries are not allowed in this context");
		/* Add the BLOCK-SEQUENCE-START token if needed. */
		if (!roll_indent(parser, parser->mark.column, -1, yaml_BLOCK_SEQUENCE_START_TOKEN, parser->mark))
			return false;
	} else {
		/* It is an error for the '-' indicator to occur in the flow context, but we let
		 * the Parser detect and report about it because the Parser is able to point to
		 * the context. */
	}

	/* Reset any potential simple keys on the current flow level. */
	if (!remove_simple_key(parser))
		return false;

	/* Simple keys are allowed after '-'. */
	parser->simple_key_allowed = true;

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create the BLOCK-ENTRY token and append it to the queue. */
	yaml_token_t token = {.typ = yaml_BLOCK_ENTRY_TOKEN, .start_mark = start_mark, .end_mark = end_mark};
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the KEY token. */
static bool fetch_key(yaml_parser_t *parser)
{
	/* In the block context, additional checks are required. */
	if (parser->flow_level == 0) {
		/* Check if we are allowed to start a new key (not necessary simple). */
		if (!parser->simple_key_allowed)
			return set_scanner_error(parser, "", parser->mark, "mapping keys are not allowed in this context");
		/* Add the BLOCK-MAPPING-START token if needed. */
		if (!roll_indent(parser, parser->mark.column, -1, yaml_BLOCK_MAPPING_START_TOKEN, parser->mark))
			return false;
	}

	/* Reset any potential simple keys on the current flow level. */
	if (!remove_simple_key(parser))
		return false;

	/* Simple keys are allowed after '?' in the block context. */
	parser->simple_key_allowed = parser->flow_level == 0;

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create the KEY token and append it to the queue. */
	yaml_token_t token = {.typ = yaml_KEY_TOKEN, .start_mark = start_mark, .end_mark = end_mark};
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the VALUE token. */
static bool fetch_value(yaml_parser_t *parser)
{
	yaml_simple_key_t *simple_key = &parser->simple_keys[parser->n_simple_keys - 1];

	/* Have we found a simple key? */
	bool valid;
	if (!simple_key_is_valid(parser, simple_key, &valid))
		return false;
	if (valid) {
		/* Create the KEY token and insert it into the queue. */
		yaml_token_t token = {.typ = yaml_KEY_TOKEN, .start_mark = simple_key->mark, .end_mark = simple_key->mark};
		insert_token(parser, simple_key->token_number - parser->tokens_parsed, &token);

		/* In the block context, we may need to add the BLOCK-MAPPING-START token. */
		if (!roll_indent(parser, simple_key->mark.column, simple_key->token_number, yaml_BLOCK_MAPPING_START_TOKEN, simple_key->mark))
			return false;

		/* Remove the simple key. */
		simple_key = &parser->simple_keys[parser->n_simple_keys - 1];
		simple_key->possible = false;
		intmap_delete(&parser->simple_keys_by_tok, simple_key->token_number);

		/* A simple key cannot follow another simple key. */
		parser->simple_key_allowed = false;
	} else {
		/* The ':' indicator follows a complex key. */

		/* In the block context, extra checks are required. */
		if (parser->flow_level == 0) {
			/* Check if we are allowed to start a complex value. */
			if (!parser->simple_key_allowed)
				return set_scanner_error(parser, "", parser->mark, "mapping values are not allowed in this context");
			/* Add the BLOCK-MAPPING-START token if needed. */
			if (!roll_indent(parser, parser->mark.column, -1, yaml_BLOCK_MAPPING_START_TOKEN, parser->mark))
				return false;
		}

		/* Simple keys after ':' are allowed in the block context. */
		parser->simple_key_allowed = parser->flow_level == 0;
	}

	/* Consume the token. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create the VALUE token and append it to the queue. */
	yaml_token_t token = {.typ = yaml_VALUE_TOKEN, .start_mark = start_mark, .end_mark = end_mark};
	insert_token(parser, -1, &token);
	return true;
}

static bool scan_anchor(yaml_parser_t *parser, yaml_token_t *token, yaml_token_type_t typ);
static bool scan_tag(yaml_parser_t *parser, yaml_token_t *token);
static bool scan_block_scalar(yaml_parser_t *parser, yaml_token_t *token, bool literal);
static bool scan_flow_scalar(yaml_parser_t *parser, yaml_token_t *token, bool single);
static bool scan_plain_scalar(yaml_parser_t *parser, yaml_token_t *token);

/* Produce the ALIAS or ANCHOR token. */
static bool fetch_anchor(yaml_parser_t *parser, yaml_token_type_t typ)
{
	/* An anchor or an alias could be a simple key. */
	if (!save_simple_key(parser))
		return false;
	/* A simple key cannot follow an anchor or an alias. */
	parser->simple_key_allowed = false;
	/* Create the ALIAS or ANCHOR token and append it to the queue. */
	yaml_token_t token = {0};
	if (!scan_anchor(parser, &token, typ))
		return false;
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the TAG token. */
static bool fetch_tag(yaml_parser_t *parser)
{
	/* A tag could be a simple key. */
	if (!save_simple_key(parser))
		return false;
	/* A simple key cannot follow a tag. */
	parser->simple_key_allowed = false;
	/* Create the TAG token and append it to the queue. */
	yaml_token_t token = {0};
	if (!scan_tag(parser, &token))
		return false;
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the SCALAR(...,literal) or SCALAR(...,folded) tokens. */
static bool fetch_block_scalar(yaml_parser_t *parser, bool literal)
{
	/* Remove any potential simple keys. */
	if (!remove_simple_key(parser))
		return false;
	/* A simple key may follow a block scalar. */
	parser->simple_key_allowed = true;
	/* Create the SCALAR token and append it to the queue. */
	yaml_token_t token = {0};
	if (!scan_block_scalar(parser, &token, literal))
		return false;
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the SCALAR(...,single-quoted) or SCALAR(...,double-quoted) tokens. */
static bool fetch_flow_scalar(yaml_parser_t *parser, bool single)
{
	/* A plain scalar could be a simple key. */
	if (!save_simple_key(parser))
		return false;
	/* A simple key cannot follow a flow scalar. */
	parser->simple_key_allowed = false;
	/* Create the SCALAR token and append it to the queue. */
	yaml_token_t token = {0};
	if (!scan_flow_scalar(parser, &token, single))
		return false;
	insert_token(parser, -1, &token);
	return true;
}

/* Produce the SCALAR(...,plain) token. */
static bool fetch_plain_scalar(yaml_parser_t *parser)
{
	/* A plain scalar could be a simple key. */
	if (!save_simple_key(parser))
		return false;
	/* A simple key cannot follow a flow scalar. */
	parser->simple_key_allowed = false;
	/* Create the SCALAR token and append it to the queue. */
	yaml_token_t token = {0};
	if (!scan_plain_scalar(parser, &token))
		return false;
	insert_token(parser, -1, &token);
	return true;
}

/* Eat whitespaces and comments until the next token is found. */
static bool scan_to_next_token(yaml_parser_t *parser)
{
	yaml_mark_t scan_mark = parser->mark;

	/* Until the next token is not found. */
	for (;;) {
		/* Allow the BOM. */
		if (!CACHE(1))
			return false;
		if (parser->mark.column == 0 && is_bom(BUF, POS))
			skip(parser);

		/* Eat whitespaces. Tabs are allowed:
		 *  - in the flow context
		 *  - in the block context, but not at the beginning of the line or after '-', '?',
		 *    or ':' (complex value). */
		if (!CACHE(1))
			return false;

		while (AT(0) == ' ' || ((parser->flow_level > 0 || !parser->simple_key_allowed) && AT(0) == '\t')) {
			skip(parser);
			if (!CACHE(1))
				return false;
		}

		/* Check if we just had a line comment under a sequence entry that looks more like
		 * a header to the following content. Similar to this:
		 *
		 * - # The comment
		 *   - Some data
		 *
		 * If so, transform the line comment to a head comment and reposition. */
		if (parser->n_comments > 0 && parser->n_tokens > 1) {
			yaml_token_t *tokenA = &parser->tokens[parser->n_tokens - 2];
			yaml_token_t *tokenB = &parser->tokens[parser->n_tokens - 1];
			yaml_comment_t *comment = &parser->comments[parser->n_comments - 1];
			if (tokenA->typ == yaml_BLOCK_SEQUENCE_START_TOKEN && tokenB->typ == yaml_BLOCK_ENTRY_TOKEN && comment->line.n > 0 &&
			    !is_break(BUF, POS)) {
				/* If it was in the prior line, reposition so it becomes a header of the
				 * follow up token. Otherwise, keep it in place so it becomes a header of
				 * the former. */
				comment->head = comment->line;
				comment->line = (Str){NULL, 0};
				if (comment->start_mark.line == parser->mark.line - 1)
					comment->token_mark = parser->mark;
			}
		}

		/* Eat a comment until a line break. */
		if (AT(0) == '#') {
			if (!scan_comments(parser, scan_mark))
				return false;
		}

		/* If it is a line break, eat it. */
		if (is_break(BUF, POS)) {
			if (!CACHE(2))
				return false;
			skip_line(parser);

			/* In the block context, a new line may start a simple key. */
			if (parser->flow_level == 0)
				parser->simple_key_allowed = true;
		} else {
			break; /* We have found a token. */
		}
	}
	return true;
}

static bool scan_directive_name(yaml_parser_t *parser, yaml_mark_t start_mark, Str *name);
static bool scan_version_directive_value(yaml_parser_t *parser, yaml_mark_t start_mark, int8_t *major, int8_t *minor);
static bool scan_tag_directive_value(yaml_parser_t *parser, yaml_mark_t start_mark, Str *handle, Str *prefix);
static bool scan_tag_handle(yaml_parser_t *parser, bool directive, yaml_mark_t start_mark, Str *handle);
static bool scan_tag_uri(yaml_parser_t *parser, bool directive, Str head, yaml_mark_t start_mark, Str *uri);

/* Scan a YAML-DIRECTIVE or TAG-DIRECTIVE token.
 *
 * Scope:
 *      %YAML    1.1    # a comment \n
 *      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
static bool scan_directive(yaml_parser_t *parser, yaml_token_t *token)
{
	/* Eat '%'. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);

	/* Scan the directive name. */
	Str name;
	if (!scan_directive_name(parser, start_mark, &name))
		return false;

	/* Is it a YAML directive? */
	if (str_eqc(name, "YAML")) {
		/* Scan the VERSION directive value. */
		int8_t major, minor;
		if (!scan_version_directive_value(parser, start_mark, &major, &minor))
			return false;
		yaml_mark_t end_mark = parser->mark;

		/* Create a VERSION-DIRECTIVE token. */
		*token = (yaml_token_t){.typ = yaml_VERSION_DIRECTIVE_TOKEN, .start_mark = start_mark, .end_mark = end_mark, .major = major, .minor = minor};

		/* Is it a TAG directive? */
	} else if (str_eqc(name, "TAG")) {
		/* Scan the TAG directive value. */
		Str handle, prefix;
		if (!scan_tag_directive_value(parser, start_mark, &handle, &prefix))
			return false;
		yaml_mark_t end_mark = parser->mark;

		/* Create a TAG-DIRECTIVE token. */
		*token = (yaml_token_t){.typ = yaml_TAG_DIRECTIVE_TOKEN, .start_mark = start_mark, .end_mark = end_mark, .value = handle, .prefix = prefix};

		/* Unknown directive. */
	} else {
		set_scanner_error(parser, "while scanning a directive", start_mark, "found unknown directive name");
		return false;
	}

	/* Eat the rest of the line including any comments. */
	if (!CACHE(1))
		return false;

	while (is_blank(BUF, POS)) {
		skip(parser);
		if (!CACHE(1))
			return false;
	}

	if (AT(0) == '#') {
		/* [Go] Discard this inline comment for the time being. */
		while (!is_breakz(BUF, POS)) {
			skip(parser);
			if (!CACHE(1))
				return false;
		}
	}

	/* Check if we are at the end of the line. */
	if (!is_breakz(BUF, POS)) {
		set_scanner_error(parser, "while scanning a directive", start_mark, "did not find expected comment or line break");
		return false;
	}

	/* Eat a line break. */
	if (is_break(BUF, POS)) {
		if (!CACHE(2))
			return false;
		skip_line(parser);
	}

	return true;
}

/* Scan the directive name.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *       ^^^^
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *       ^^^ */
static bool scan_directive_name(yaml_parser_t *parser, yaml_mark_t start_mark, Str *name)
{
	/* Consume the directive name. */
	if (!CACHE(1))
		return false;

	Buf s = {0};
	while (is_alpha(BUF, POS)) {
		read1(parser, &s);
		if (!CACHE(1))
			return false;
	}

	/* Check if the name is empty. */
	if (s.n == 0) {
		set_scanner_error(parser, "while scanning a directive", start_mark, "could not find expected directive name");
		return false;
	}

	/* Check for an blank character after the name. */
	if (!is_blankz(BUF, POS)) {
		set_scanner_error(parser, "while scanning a directive", start_mark, "found unexpected non-alphabetical character");
		return false;
	}
	*name = buf_take(&s);
	return true;
}

static bool scan_version_directive_number(yaml_parser_t *parser, yaml_mark_t start_mark, int8_t *number);

/* Scan the value of VERSION-DIRECTIVE.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *           ^^^^^^ */
static bool scan_version_directive_value(yaml_parser_t *parser, yaml_mark_t start_mark, int8_t *major, int8_t *minor)
{
	/* Eat whitespaces. */
	if (!CACHE(1))
		return false;
	while (is_blank(BUF, POS)) {
		skip(parser);
		if (!CACHE(1))
			return false;
	}

	/* Consume the major version number. */
	if (!scan_version_directive_number(parser, start_mark, major))
		return false;

	/* Eat '.'. */
	if (AT(0) != '.')
		return set_scanner_error(parser, "while scanning a %YAML directive", start_mark, "did not find expected digit or '.' character");

	skip(parser);

	/* Consume the minor version number. */
	if (!scan_version_directive_number(parser, start_mark, minor))
		return false;
	return true;
}

#define max_number_length 2

/* Scan the version number of VERSION-DIRECTIVE.
 *
 * Scope:
 *      %YAML   1.1     # a comment \n
 *              ^
 *      %YAML   1.1     # a comment \n
 *                ^ */
static bool scan_version_directive_number(yaml_parser_t *parser, yaml_mark_t start_mark, int8_t *number)
{
	/* Repeat while the next character is digit. */
	if (!CACHE(1))
		return false;
	int8_t value = 0, length = 0;
	while (is_digit(BUF, POS)) {
		/* Check if the number is too long. */
		length++;
		if (length > max_number_length)
			return set_scanner_error(parser, "while scanning a %YAML directive", start_mark, "found extremely long version number");
		value = (int8_t)(value * 10 + as_digit(BUF, POS));
		skip(parser);
		if (!CACHE(1))
			return false;
	}

	/* Check if the number was present. */
	if (length == 0)
		return set_scanner_error(parser, "while scanning a %YAML directive", start_mark, "did not find expected version number");
	*number = value;
	return true;
}

/* Scan the value of a TAG-DIRECTIVE token.
 *
 * Scope:
 *      %TAG    !yaml!  tag:yaml.org,2002:  \n
 *          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
static bool scan_tag_directive_value(yaml_parser_t *parser, yaml_mark_t start_mark, Str *handle, Str *prefix)
{
	Str handle_value, prefix_value;

	/* Eat whitespaces. */
	if (!CACHE(1))
		return false;

	while (is_blank(BUF, POS)) {
		skip(parser);
		if (!CACHE(1))
			return false;
	}

	/* Scan a handle. */
	if (!scan_tag_handle(parser, true, start_mark, &handle_value))
		return false;

	/* Expect a whitespace. */
	if (!CACHE(1))
		return false;
	if (!is_blank(BUF, POS)) {
		set_scanner_error(parser, "while scanning a %TAG directive", start_mark, "did not find expected whitespace");
		return false;
	}

	/* Eat whitespaces. */
	while (is_blank(BUF, POS)) {
		skip(parser);
		if (!CACHE(1))
			return false;
	}

	/* Scan a prefix. */
	if (!scan_tag_uri(parser, true, (Str){NULL, 0}, start_mark, &prefix_value))
		return false;

	/* Expect a whitespace or line break. */
	if (!CACHE(1))
		return false;
	if (!is_blankz(BUF, POS)) {
		set_scanner_error(parser, "while scanning a %TAG directive", start_mark, "did not find expected whitespace or line break");
		return false;
	}

	*handle = handle_value;
	*prefix = prefix_value;
	return true;
}

static bool scan_anchor(yaml_parser_t *parser, yaml_token_t *token, yaml_token_type_t typ)
{
	Buf s = {0};

	/* Eat the indicator character. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);

	/* Consume the value. */
	if (!CACHE(1))
		return false;

	while (is_alpha(BUF, POS)) {
		read1(parser, &s);
		if (!CACHE(1))
			return false;
	}

	yaml_mark_t end_mark = parser->mark;

	/* Check if length of the anchor is greater than 0 and it is followed by a whitespace
	 * character or one of the indicators:
	 *
	 *      '?', ':', ',', ']', '}', '%', '@', '`'. */
	uint8_t c = AT(0);
	if (s.n == 0 ||
	    !(is_blankz(BUF, POS) || c == '?' || c == ':' || c == ',' || c == ']' || c == '}' || c == '%' || c == '@' || c == '`')) {
		const char *context = "while scanning an alias";
		if (typ == yaml_ANCHOR_TOKEN)
			context = "while scanning an anchor";
		set_scanner_error(parser, context, start_mark, "did not find expected alphabetic or numeric character");
		return false;
	}

	/* Create a token. */
	*token = (yaml_token_t){.typ = typ, .start_mark = start_mark, .end_mark = end_mark, .value = buf_take(&s)};
	return true;
}

/* Scan a TAG token. */
static bool scan_tag(yaml_parser_t *parser, yaml_token_t *token)
{
	Str handle = {NULL, 0}, suffix = {NULL, 0};

	yaml_mark_t start_mark = parser->mark;

	/* Check if the tag is in the canonical form. */
	if (!CACHE(2))
		return false;

	if (AT(1) == '<') {
		/* Keep the handle as '' */

		/* Eat '!<' */
		skip(parser);
		skip(parser);

		/* Consume the tag value. */
		if (!scan_tag_uri(parser, false, (Str){NULL, 0}, start_mark, &suffix))
			return false;

		/* Check for '>' and eat it. */
		if (AT(0) != '>') {
			set_scanner_error(parser, "while scanning a tag", start_mark, "did not find the expected '>'");
			return false;
		}

		skip(parser);
	} else {
		/* The tag has either the '!suffix' or the '!handle!suffix' form. */

		/* First, try to scan a handle. */
		if (!scan_tag_handle(parser, false, start_mark, &handle))
			return false;

		/* Check if it is, indeed, handle. */
		if (handle.p[0] == '!' && handle.n > 1 && handle.p[handle.n - 1] == '!') {
			/* Scan the suffix now. */
			if (!scan_tag_uri(parser, false, (Str){NULL, 0}, start_mark, &suffix))
				return false;
		} else {
			/* It wasn't a handle after all. Scan the rest of the tag. */
			if (!scan_tag_uri(parser, false, handle, start_mark, &suffix))
				return false;

			/* Set the handle to '!'. */
			handle = STR_LIT("!");

			/* A special case: the '!' tag. Set the handle to '' and the suffix to '!'. */
			if (suffix.n == 0) {
				Str t = handle;
				handle = suffix;
				suffix = t;
			}
		}
	}

	/* Check the character which ends the tag. */
	if (!CACHE(1))
		return false;
	if (!is_blankz(BUF, POS)) {
		set_scanner_error(parser, "while scanning a tag", start_mark, "did not find expected whitespace or line break");
		return false;
	}

	yaml_mark_t end_mark = parser->mark;

	/* Create a token. */
	*token = (yaml_token_t){.typ = yaml_TAG_TOKEN, .start_mark = start_mark, .end_mark = end_mark, .value = handle, .suffix = suffix};
	return true;
}

/* Scan a tag handle. */
static bool scan_tag_handle(yaml_parser_t *parser, bool directive, yaml_mark_t start_mark, Str *handle)
{
	/* Check the initial '!' character. */
	if (!CACHE(1))
		return false;
	if (AT(0) != '!') {
		set_scanner_tag_error(parser, directive, start_mark, "did not find expected '!'");
		return false;
	}

	Buf s = {0};

	/* Copy the '!' character. */
	read1(parser, &s);

	/* Copy all subsequent alphabetical and numerical characters. */
	if (!CACHE(1))
		return false;
	while (is_alpha(BUF, POS)) {
		read1(parser, &s);
		if (!CACHE(1))
			return false;
	}

	/* Check if the trailing character is '!' and copy it. */
	if (AT(0) == '!') {
		read1(parser, &s);
	} else {
		/* It's either the '!' tag or not really a tag handle. If it's a %TAG directive,
		 * it's an error. If it's a tag token, it must be a part of URI. */
		if (directive && !(s.n == 1 && s.p[0] == '!')) {
			set_scanner_tag_error(parser, directive, start_mark, "did not find expected '!'");
			return false;
		}
	}

	*handle = buf_take(&s);
	return true;
}

static bool scan_uri_escapes(yaml_parser_t *parser, bool directive, yaml_mark_t start_mark, Buf *s);

/* Scan a tag. */
static bool scan_tag_uri(yaml_parser_t *parser, bool directive, Str head, yaml_mark_t start_mark, Str *uri)
{
	Buf s = {0};
	bool hasTag = head.n > 0;

	/* Copy the head if needed.
	 *
	 * Note that we don't copy the leading '!' character. */
	if (head.n > 1)
		buf_add(&s, head.p + 1, head.n - 1);

	/* Scan the tag. */
	if (!CACHE(1))
		return false;

	/* The set of characters that may appear in URI is as follows:
	 *
	 *      '0'-'9', 'A'-'Z', 'a'-'z', '_', '-', ';', '/', '?', ':', '@', '&', '=', '+', '$',
	 *      '.', '!', '~', '*', '\'', '(', ')', '[', ']', '%'.
	 *
	 * [Go] TODO Convert this into more reasonable logic. */
	for (;;) {
		uint8_t c = AT(0);
		if (!(is_alpha(BUF, POS) || c == ';' || c == '/' || c == '?' || c == ':' || c == '@' || c == '&' || c == '=' || c == '+' ||
		      c == '$' || c == ',' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')' || c == '[' ||
		      c == ']' || c == '%'))
			break;
		/* Check if it is a URI-escape sequence. */
		if (c == '%') {
			if (!scan_uri_escapes(parser, directive, start_mark, &s))
				return false;
		} else {
			read1(parser, &s);
		}
		if (!CACHE(1))
			return false;
		hasTag = true;
	}

	if (!hasTag) {
		set_scanner_tag_error(parser, directive, start_mark, "did not find expected tag URI");
		return false;
	}
	*uri = buf_take(&s);
	return true;
}

/* Decode an URI-escape sequence corresponding to a single UTF-8 character. */
static bool scan_uri_escapes(yaml_parser_t *parser, bool directive, yaml_mark_t start_mark, Buf *s)
{
	/* Decode the required number of characters. */
	int w = 1024;
	while (w > 0) {
		/* Check for a URI-escaped octet. */
		if (!CACHE(3))
			return false;

		if (!(AT(0) == '%' && is_hex(BUF, POS + 1) && is_hex(BUF, POS + 2)))
			return set_scanner_tag_error(parser, directive, start_mark, "did not find URI escaped octet");

		/* Get the octet. */
		uint8_t octet = (uint8_t)((as_hex(BUF, POS + 1) << 4) + as_hex(BUF, POS + 2));

		/* If it is the leading octet, determine the length of the UTF-8 sequence. */
		if (w == 1024) {
			w = yaml_width(octet);
			if (w == 0)
				return set_scanner_tag_error(parser, directive, start_mark, "found an incorrect leading UTF-8 octet");
		} else {
			/* Check if the trailing octet is correct. */
			if ((octet & 0xC0) != 0x80)
				return set_scanner_tag_error(parser, directive, start_mark, "found an incorrect trailing UTF-8 octet");
		}

		/* Copy the octet and move the pointers. */
		buf_addc(s, (char)octet);
		skip(parser);
		skip(parser);
		skip(parser);
		w--;
	}
	return true;
}

static bool scan_block_scalar_breaks(yaml_parser_t *parser, int *indent, Buf *breaks, yaml_mark_t start_mark, yaml_mark_t *end_mark);

/* Scan a block scalar. */
static bool scan_block_scalar(yaml_parser_t *parser, yaml_token_t *token, bool literal)
{
	/* Eat the indicator '|' or '>'. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);

	/* Scan the additional block scalar indicators. */
	if (!CACHE(1))
		return false;

	/* Check for a chomping indicator. */
	int chomping = 0, increment = 0;
	if (AT(0) == '+' || AT(0) == '-') {
		/* Set the chomping method and eat the indicator. */
		chomping = AT(0) == '+' ? +1 : -1;
		skip(parser);

		/* Check for an indentation indicator. */
		if (!CACHE(1))
			return false;
		if (is_digit(BUF, POS)) {
			/* Check that the indentation is greater than 0. */
			if (AT(0) == '0') {
				set_scanner_error(parser, "while scanning a block scalar", start_mark, "found an indentation indicator equal to 0");
				return false;
			}

			/* Get the indentation level and eat the indicator. */
			increment = as_digit(BUF, POS);
			skip(parser);
		}

	} else if (is_digit(BUF, POS)) {
		/* Do the same as above, but in the opposite order. */

		if (AT(0) == '0') {
			set_scanner_error(parser, "while scanning a block scalar", start_mark, "found an indentation indicator equal to 0");
			return false;
		}
		increment = as_digit(BUF, POS);
		skip(parser);

		if (!CACHE(1))
			return false;
		if (AT(0) == '+' || AT(0) == '-') {
			chomping = AT(0) == '+' ? +1 : -1;
			skip(parser);
		}
	}

	/* Eat whitespaces and comments to the end of the line. */
	if (!CACHE(1))
		return false;
	while (is_blank(BUF, POS)) {
		skip(parser);
		if (!CACHE(1))
			return false;
	}
	if (AT(0) == '#') {
		if (!scan_line_comment(parser, start_mark))
			return false;
		while (!is_breakz(BUF, POS)) {
			skip(parser);
			if (!CACHE(1))
				return false;
		}
	}

	/* Check if we are at the end of the line. */
	if (!is_breakz(BUF, POS)) {
		set_scanner_error(parser, "while scanning a block scalar", start_mark, "did not find expected comment or line break");
		return false;
	}

	/* Eat a line break. */
	if (is_break(BUF, POS)) {
		if (!CACHE(2))
			return false;
		skip_line(parser);
	}

	yaml_mark_t end_mark = parser->mark;

	/* Set the indentation level if it was specified. */
	int indent = 0;
	if (increment > 0) {
		if (parser->indent >= 0)
			indent = parser->indent + increment;
		else
			indent = increment;
	}

	/* Scan the leading line breaks and determine the indentation level if needed. */
	Buf s = {0}, leading_break = {0}, trailing_breaks = {0};
	if (!scan_block_scalar_breaks(parser, &indent, &trailing_breaks, start_mark, &end_mark))
		return false;

	/* Scan the block scalar content. */
	if (!CACHE(1))
		return false;
	bool leading_blank = false, trailing_blank = false;
	while (parser->mark.column == indent && !is_z(BUF, POS)) {
		/* We are at the beginning of a non-empty line. */

		/* Is it a trailing whitespace? */
		trailing_blank = is_blank(BUF, POS);

		/* Check if we need to fold the leading line break. */
		if (!literal && !leading_blank && !trailing_blank && leading_break.n > 0 && leading_break.p[0] == '\n') {
			/* Do we need to join the lines by space? */
			if (trailing_breaks.n == 0)
				buf_addc(&s, ' ');
		} else {
			buf_add(&s, leading_break.p, leading_break.n);
		}
		leading_break.n = 0;

		/* Append the remaining line breaks. */
		buf_add(&s, trailing_breaks.p, trailing_breaks.n);
		trailing_breaks.n = 0;

		/* Is it a leading whitespace? */
		leading_blank = is_blank(BUF, POS);

		/* Consume the current line. */
		while (!is_breakz(BUF, POS)) {
			read1(parser, &s);
			if (!CACHE(1))
				return false;
		}

		/* Consume the line break. */
		if (!CACHE(2))
			return false;

		read_line(parser, &leading_break);

		/* Eat the following indentation spaces and line breaks. */
		if (!scan_block_scalar_breaks(parser, &indent, &trailing_breaks, start_mark, &end_mark))
			return false;
	}

	/* Chomp the tail. */
	if (chomping != -1)
		buf_add(&s, leading_break.p, leading_break.n);
	if (chomping == 1)
		buf_add(&s, trailing_breaks.p, trailing_breaks.n);

	/* Create a token. */
	*token = (yaml_token_t){.typ = yaml_SCALAR_TOKEN, .start_mark = start_mark, .end_mark = end_mark, .value = buf_take(&s),
				.style = yaml_LITERAL_SCALAR_STYLE};
	if (!literal)
		token->style = yaml_FOLDED_SCALAR_STYLE;
	return true;
}

/* Scan indentation spaces and line breaks for a block scalar. Determine the indentation level
 * if needed. */
static bool scan_block_scalar_breaks(yaml_parser_t *parser, int *indent, Buf *breaks, yaml_mark_t start_mark, yaml_mark_t *end_mark)
{
	*end_mark = parser->mark;

	/* Eat the indentation spaces and line breaks. */
	int max_indent = 0;
	for (;;) {
		/* Eat the indentation spaces. */
		if (!CACHE(1))
			return false;
		while ((*indent == 0 || parser->mark.column < *indent) && is_space(BUF, POS)) {
			skip(parser);
			if (!CACHE(1))
				return false;
		}
		if (parser->mark.column > max_indent)
			max_indent = parser->mark.column;

		/* Check for a tab character messing the indentation. */
		if ((*indent == 0 || parser->mark.column < *indent) && is_tab(BUF, POS))
			return set_scanner_error(parser, "while scanning a block scalar", start_mark,
						 "found a tab character where an indentation space is expected");

		/* Have we found a non-empty line? */
		if (!is_break(BUF, POS))
			break;

		/* Consume the line break. */
		if (!CACHE(2))
			return false;
		/* [Go] Should really be returning breaks instead. */
		read_line(parser, breaks);
		*end_mark = parser->mark;
	}

	/* Determine the indentation level if needed. */
	if (*indent == 0) {
		*indent = max_indent;
		if (*indent < parser->indent + 1)
			*indent = parser->indent + 1;
		if (*indent < 1)
			*indent = 1;
	}
	return true;
}

/* Scan a quoted scalar. */
static bool scan_flow_scalar(yaml_parser_t *parser, yaml_token_t *token, bool single)
{
	/* Eat the left quote. */
	yaml_mark_t start_mark = parser->mark;
	skip(parser);

	/* Consume the content of the quoted scalar. */
	Buf s = {0}, leading_break = {0}, trailing_breaks = {0}, whitespaces = {0};
	for (;;) {
		/* Check that there are no document indicators at the beginning of the line. */
		if (!CACHE(4))
			return false;

		if (parser->mark.column == 0 &&
		    ((AT(0) == '-' && AT(1) == '-' && AT(2) == '-') || (AT(0) == '.' && AT(1) == '.' && AT(2) == '.')) &&
		    is_blankz(BUF, POS + 3)) {
			set_scanner_error(parser, "while scanning a quoted scalar", start_mark, "found unexpected document indicator");
			return false;
		}

		/* Check for EOF. */
		if (is_z(BUF, POS)) {
			set_scanner_error(parser, "while scanning a quoted scalar", start_mark, "found unexpected end of stream");
			return false;
		}

		/* Consume non-blank characters. */
		bool leading_blanks = false;
		while (!is_blankz(BUF, POS)) {
			if (single && AT(0) == '\'' && AT(1) == '\'') {
				/* Is is an escaped single quote. */
				buf_addc(&s, '\'');
				skip(parser);
				skip(parser);

			} else if (single && AT(0) == '\'') {
				/* It is a right single quote. */
				break;
			} else if (!single && AT(0) == '"') {
				/* It is a right double quote. */
				break;

			} else if (!single && AT(0) == '\\' && is_break(BUF, POS + 1)) {
				/* It is an escaped line break. */
				if (!CACHE(3))
					return false;
				skip(parser);
				skip_line(parser);
				leading_blanks = true;
				break;

			} else if (!single && AT(0) == '\\') {
				/* It is an escape sequence. */
				int code_length = 0;

				/* Check the escape character. */
				switch (AT(1)) {
				case '0': buf_addc(&s, 0); break;
				case 'a': buf_addc(&s, '\x07'); break;
				case 'b': buf_addc(&s, '\x08'); break;
				case 't': case '\t': buf_addc(&s, '\x09'); break;
				case 'n': buf_addc(&s, '\x0A'); break;
				case 'v': buf_addc(&s, '\x0B'); break;
				case 'f': buf_addc(&s, '\x0C'); break;
				case 'r': buf_addc(&s, '\x0D'); break;
				case 'e': buf_addc(&s, '\x1B'); break;
				case ' ': buf_addc(&s, '\x20'); break;
				case '"': buf_addc(&s, '"'); break;
				case '\'': buf_addc(&s, '\''); break;
				case '\\': buf_addc(&s, '\\'); break;
				case 'N': buf_add(&s, "\xC2\x85", 2); break; /* NEL (#x85) */
				case '_': buf_add(&s, "\xC2\xA0", 2); break; /* #xA0 */
				case 'L': buf_add(&s, "\xE2\x80\xA8", 3); break; /* LS (#x2028) */
				case 'P': buf_add(&s, "\xE2\x80\xA9", 3); break; /* PS (#x2029) */
				case 'x': code_length = 2; break;
				case 'u': code_length = 4; break;
				case 'U': code_length = 8; break;
				default:
					set_scanner_error(parser, "while parsing a quoted scalar", start_mark, "found unknown escape character");
					return false;
				}

				skip(parser);
				skip(parser);

				/* Consume an arbitrary escape code. */
				if (code_length > 0) {
					long value = 0;

					/* Scan the character value. */
					if (!CACHE(code_length))
						return false;
					for (int k = 0; k < code_length; k++) {
						if (!is_hex(BUF, POS + (size_t)k)) {
							set_scanner_error(parser, "while parsing a quoted scalar", start_mark,
									  "did not find expected hexdecimal number");
							return false;
						}
						value = (value << 4) + as_hex(BUF, POS + (size_t)k);
					}

					/* Check the value and write the character. */
					if ((value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
						set_scanner_error(parser, "while parsing a quoted scalar", start_mark,
								  "found invalid Unicode character escape code");
						return false;
					}
					if (value <= 0x7F) {
						buf_addc(&s, (char)value);
					} else if (value <= 0x7FF) {
						buf_addc(&s, (char)(0xC0 + (value >> 6)));
						buf_addc(&s, (char)(0x80 + (value & 0x3F)));
					} else if (value <= 0xFFFF) {
						buf_addc(&s, (char)(0xE0 + (value >> 12)));
						buf_addc(&s, (char)(0x80 + ((value >> 6) & 0x3F)));
						buf_addc(&s, (char)(0x80 + (value & 0x3F)));
					} else {
						buf_addc(&s, (char)(0xF0 + (value >> 18)));
						buf_addc(&s, (char)(0x80 + ((value >> 12) & 0x3F)));
						buf_addc(&s, (char)(0x80 + ((value >> 6) & 0x3F)));
						buf_addc(&s, (char)(0x80 + (value & 0x3F)));
					}

					/* Advance the pointer. */
					for (int k = 0; k < code_length; k++)
						skip(parser);
				}
			} else {
				/* It is a non-escaped non-blank character. */
				read1(parser, &s);
			}
			if (!CACHE(2))
				return false;
		}

		if (!CACHE(1))
			return false;

		/* Check if we are at the end of the scalar. */
		if (single) {
			if (AT(0) == '\'')
				break;
		} else {
			if (AT(0) == '"')
				break;
		}

		/* Consume blank characters. */
		while (is_blank(BUF, POS) || is_break(BUF, POS)) {
			if (is_blank(BUF, POS)) {
				/* Consume a space or a tab character. */
				if (!leading_blanks)
					read1(parser, &whitespaces);
				else
					skip(parser);
			} else {
				if (!CACHE(2))
					return false;

				/* Check if it is a first line break. */
				if (!leading_blanks) {
					whitespaces.n = 0;
					read_line(parser, &leading_break);
					leading_blanks = true;
				} else {
					read_line(parser, &trailing_breaks);
				}
			}
			if (!CACHE(1))
				return false;
		}

		/* Join the whitespaces or fold line breaks. */
		if (leading_blanks) {
			/* Do we need to fold line breaks? */
			if (leading_break.n > 0 && leading_break.p[0] == '\n') {
				if (trailing_breaks.n == 0)
					buf_addc(&s, ' ');
				else
					buf_add(&s, trailing_breaks.p, trailing_breaks.n);
			} else {
				buf_add(&s, leading_break.p, leading_break.n);
				buf_add(&s, trailing_breaks.p, trailing_breaks.n);
			}
			trailing_breaks.n = 0;
			leading_break.n = 0;
		} else {
			buf_add(&s, whitespaces.p, whitespaces.n);
			whitespaces.n = 0;
		}
	}

	/* Eat the right quote. */
	skip(parser);
	yaml_mark_t end_mark = parser->mark;

	/* Create a token. */
	*token = (yaml_token_t){.typ = yaml_SCALAR_TOKEN, .start_mark = start_mark, .end_mark = end_mark, .value = buf_take(&s),
				.style = yaml_SINGLE_QUOTED_SCALAR_STYLE};
	if (!single)
		token->style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;
	return true;
}

/* Scan a plain scalar. */
static bool scan_plain_scalar(yaml_parser_t *parser, yaml_token_t *token)
{
	Buf s = {0}, leading_break = {0}, trailing_breaks = {0}, whitespaces = {0};
	bool leading_blanks = false;
	int indent = parser->indent + 1;

	yaml_mark_t start_mark = parser->mark;
	yaml_mark_t end_mark = parser->mark;

	/* Consume the content of the plain scalar. */
	for (;;) {
		/* Check for a document indicator. */
		if (!CACHE(4))
			return false;
		if (parser->mark.column == 0 &&
		    ((AT(0) == '-' && AT(1) == '-' && AT(2) == '-') || (AT(0) == '.' && AT(1) == '.' && AT(2) == '.')) &&
		    is_blankz(BUF, POS + 3))
			break;

		/* Check for a comment. */
		if (AT(0) == '#')
			break;

		/* Consume non-blank characters. */
		while (!is_blankz(BUF, POS)) {
			/* Check for indicators that may end a plain scalar. */
			uint8_t c = AT(0);
			if ((c == ':' && is_blankz(BUF, POS + 1)) ||
			    (parser->flow_level > 0 && (c == ',' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}')))
				break;

			/* Check if we need to join whitespaces and breaks. */
			if (leading_blanks || whitespaces.n > 0) {
				if (leading_blanks) {
					/* Do we need to fold line breaks? */
					if (leading_break.p[0] == '\n') {
						if (trailing_breaks.n == 0)
							buf_addc(&s, ' ');
						else
							buf_add(&s, trailing_breaks.p, trailing_breaks.n);
					} else {
						buf_add(&s, leading_break.p, leading_break.n);
						buf_add(&s, trailing_breaks.p, trailing_breaks.n);
					}
					trailing_breaks.n = 0;
					leading_break.n = 0;
					leading_blanks = false;
				} else {
					buf_add(&s, whitespaces.p, whitespaces.n);
					whitespaces.n = 0;
				}
			}

			/* Copy the character. */
			read1(parser, &s);

			end_mark = parser->mark;
			if (!CACHE(2))
				return false;
		}

		/* Is it the end? */
		if (!(is_blank(BUF, POS) || is_break(BUF, POS)))
			break;

		/* Consume blank characters. */
		if (!CACHE(1))
			return false;

		while (is_blank(BUF, POS) || is_break(BUF, POS)) {
			if (is_blank(BUF, POS)) {
				/* Check for tab characters that abuse indentation. */
				if (leading_blanks && parser->mark.column < indent && is_tab(BUF, POS)) {
					set_scanner_error(parser, "while scanning a plain scalar", start_mark, "found a tab character that violates indentation");
					return false;
				}

				/* Consume a space or a tab character. */
				if (!leading_blanks)
					read1(parser, &whitespaces);
				else
					skip(parser);
			} else {
				if (!CACHE(2))
					return false;

				/* Check if it is a first line break. */
				if (!leading_blanks) {
					whitespaces.n = 0;
					read_line(parser, &leading_break);
					leading_blanks = true;
				} else {
					read_line(parser, &trailing_breaks);
				}
			}
			if (!CACHE(1))
				return false;
		}

		/* Check indentation level. */
		if (parser->flow_level == 0 && parser->mark.column < indent)
			break;
	}

	/* Create a token. */
	*token = (yaml_token_t){.typ = yaml_SCALAR_TOKEN, .start_mark = start_mark, .end_mark = end_mark, .value = buf_take(&s),
				.style = yaml_PLAIN_SCALAR_STYLE};

	/* Note that we change the 'simple_key_allowed' flag. */
	if (leading_blanks)
		parser->simple_key_allowed = true;
	return true;
}

static void push_comment(yaml_parser_t *parser, yaml_comment_t c)
{
	if (parser->n_comments == parser->cap_comments) {
		int cap = parser->cap_comments ? parser->cap_comments * 2 : 8;
		parser->comments = xrealloc(parser->comments, sizeof *parser->comments * (size_t)parser->cap_comments, sizeof *parser->comments * (size_t)cap);
		parser->cap_comments = cap;
	}
	parser->comments[parser->n_comments++] = c;
}

static bool scan_line_comment(yaml_parser_t *parser, yaml_mark_t token_mark)
{
	if (parser->newlines > 0)
		return true;

	yaml_mark_t start_mark = {0};
	Buf text = {0};

	for (int peek = 0; peek < 512; peek++) {
		if (parser->unread < peek + 1 && !yaml_parser_update_buffer(parser, peek + 1))
			break;
		if (is_blank(BUF, POS + (size_t)peek))
			continue;
		if (AT(peek) == '#') {
			int seen = parser->mark.index + peek;
			for (;;) {
				if (!CACHE(1))
					return false;
				if (is_breakz(BUF, POS)) {
					if (parser->mark.index >= seen)
						break;
					if (!CACHE(2))
						return false;
					skip_line(parser);
				} else if (parser->mark.index >= seen) {
					if (text.n == 0)
						start_mark = parser->mark;
					read1(parser, &text);
				} else {
					skip(parser);
				}
			}
		}
		break;
	}
	if (text.n > 0)
		push_comment(parser, (yaml_comment_t){.token_mark = token_mark, .start_mark = start_mark, .line = buf_view(&text)});
	return true;
}

static bool scan_comments(yaml_parser_t *parser, yaml_mark_t scan_mark)
{
	yaml_token_t token = parser->tokens[parser->n_tokens - 1];

	if (token.typ == yaml_FLOW_ENTRY_TOKEN && parser->n_tokens > 1)
		token = parser->tokens[parser->n_tokens - 2];

	yaml_mark_t token_mark = token.start_mark;
	yaml_mark_t start_mark = {0};
	int next_indent = parser->indent;
	if (next_indent < 0)
		next_indent = 0;

	bool recent_empty = false;
	bool first_empty = parser->newlines <= 1;

	int line = parser->mark.line;
	int column = parser->mark.column;

	Buf text = {0};

	/* The foot line is the place where a comment must start to still be considered as a foot
	 * of the prior content. If there's some content in the currently parsed line, then the
	 * foot is the line below it. */
	int foot_line = -1;
	if (scan_mark.line > 0) {
		foot_line = parser->mark.line - parser->newlines + 1;
		if (parser->newlines == 0 && parser->mark.column > 1)
			foot_line++;
	}

	int peek = 0;
	for (; peek < 512; peek++) {
		if (parser->unread < peek + 1 && !yaml_parser_update_buffer(parser, peek + 1))
			break;
		column++;
		if (is_blank(BUF, POS + (size_t)peek))
			continue;
		uint8_t c = AT(peek);
		bool close_flow = parser->flow_level > 0 && (c == ']' || c == '}');
		if (close_flow || is_breakz(BUF, POS + (size_t)peek)) {
			/* Got line break or terminator. */
			if (close_flow || !recent_empty) {
				if (close_flow || (first_empty && ((start_mark.line == foot_line && token.typ != yaml_VALUE_TOKEN) ||
								    start_mark.column - 1 < next_indent))) {
					/* This is the first empty line and there were no empty lines
					 * before, so this initial part of the comment is a foot of the prior
					 * token instead of being a head for the following one. Split it up.
					 * Alternatively, this might also be the last comment inside a flow
					 * scope, so it must be a footer. */
					if (text.n > 0) {
						if (start_mark.column - 1 < next_indent) {
							/* If dedented it's unrelated to the prior token. */
							token_mark = start_mark;
						}
						push_comment(parser, (yaml_comment_t){.scan_mark = scan_mark,
										      .token_mark = token_mark,
										      .start_mark = start_mark,
										      .end_mark = {parser->mark.index + peek, line, column},
										      .foot = buf_view(&text)});
						scan_mark = (yaml_mark_t){parser->mark.index + peek, line, column};
						token_mark = scan_mark;
						text = (Buf){0};
					}
				} else {
					if (text.n > 0 && AT(peek) != 0)
						buf_addc(&text, '\n');
				}
			}
			if (!is_break(BUF, POS + (size_t)peek))
				break;
			first_empty = false;
			recent_empty = true;
			column = 0;
			line++;
			continue;
		}

		if (text.n > 0 && (close_flow || (column - 1 < next_indent && column != start_mark.column))) {
			/* The comment at the different indentation is a foot of the preceding data
			 * rather than a head of the upcoming one. */
			push_comment(parser, (yaml_comment_t){.scan_mark = scan_mark,
							      .token_mark = token_mark,
							      .start_mark = start_mark,
							      .end_mark = {parser->mark.index + peek, line, column},
							      .foot = buf_view(&text)});
			scan_mark = (yaml_mark_t){parser->mark.index + peek, line, column};
			token_mark = scan_mark;
			text = (Buf){0};
		}

		if (AT(peek) != '#')
			break;

		if (text.n == 0)
			start_mark = (yaml_mark_t){parser->mark.index + peek, line, column};
		else
			buf_addc(&text, '\n');

		recent_empty = false;

		/* Consume until after the consumed comment line. */
		int seen = parser->mark.index + peek;
		for (;;) {
			if (!CACHE(1))
				return false;
			if (is_breakz(BUF, POS)) {
				if (parser->mark.index >= seen)
					break;
				if (!CACHE(2))
					return false;
				skip_line(parser);
			} else if (parser->mark.index >= seen) {
				read1(parser, &text);
			} else {
				skip(parser);
			}
		}

		peek = 0;
		column = 0;
		line = parser->mark.line;
		next_indent = parser->indent;
		if (next_indent < 0)
			next_indent = 0;
	}

	if (text.n > 0) {
		push_comment(parser, (yaml_comment_t){.scan_mark = scan_mark,
						      .token_mark = start_mark,
						      .start_mark = start_mark,
						      .end_mark = {parser->mark.index + peek - 1, line, column},
						      .head = buf_view(&text)});
	}
	return true;
}
