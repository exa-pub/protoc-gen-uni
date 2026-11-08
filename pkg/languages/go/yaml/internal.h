/* Copyright (c) 2011-2019 Canonical Ltd
 * Copyright (c) 2006-2010 Kirill Simonov
 * Licensed under the Apache License, Version 2.0 and the MIT License (see LICENSE of
 * go.yaml.in/yaml/v3). */

/* Internal declarations of the go.yaml.in/yaml/v3 v3.0.4 port: yamlh.go and yamlprivateh.go. */
#ifndef GO_YAML_INTERNAL_H
#define GO_YAML_INTERNAL_H

#include "yaml.h"

#include <string.h>

/* ---- yamlh.go ---- */

typedef struct {
	int8_t major, minor;
} yaml_version_directive_t;

typedef struct {
	Str handle, prefix;
} yaml_tag_directive_t;

enum { yaml_ANY_ENCODING, yaml_UTF8_ENCODING, yaml_UTF16LE_ENCODING, yaml_UTF16BE_ENCODING };
enum { yaml_ANY_BREAK, yaml_CR_BREAK, yaml_LN_BREAK, yaml_CRLN_BREAK };
enum {
	yaml_NO_ERROR, yaml_MEMORY_ERROR, yaml_READER_ERROR, yaml_SCANNER_ERROR, yaml_PARSER_ERROR,
	yaml_COMPOSER_ERROR, yaml_WRITER_ERROR, yaml_EMITTER_ERROR,
};

typedef struct {
	int index, line, column;
} yaml_mark_t;

enum {
	yaml_ANY_SCALAR_STYLE = 0,
	yaml_PLAIN_SCALAR_STYLE = 2,
	yaml_SINGLE_QUOTED_SCALAR_STYLE = 4,
	yaml_DOUBLE_QUOTED_SCALAR_STYLE = 8,
	yaml_LITERAL_SCALAR_STYLE = 16,
	yaml_FOLDED_SCALAR_STYLE = 32,
};
enum { yaml_ANY_SEQUENCE_STYLE, yaml_BLOCK_SEQUENCE_STYLE, yaml_FLOW_SEQUENCE_STYLE };
enum { yaml_ANY_MAPPING_STYLE, yaml_BLOCK_MAPPING_STYLE, yaml_FLOW_MAPPING_STYLE };

typedef enum {
	yaml_NO_TOKEN,
	yaml_STREAM_START_TOKEN,
	yaml_STREAM_END_TOKEN,
	yaml_VERSION_DIRECTIVE_TOKEN,
	yaml_TAG_DIRECTIVE_TOKEN,
	yaml_DOCUMENT_START_TOKEN,
	yaml_DOCUMENT_END_TOKEN,
	yaml_BLOCK_SEQUENCE_START_TOKEN,
	yaml_BLOCK_MAPPING_START_TOKEN,
	yaml_BLOCK_END_TOKEN,
	yaml_FLOW_SEQUENCE_START_TOKEN,
	yaml_FLOW_SEQUENCE_END_TOKEN,
	yaml_FLOW_MAPPING_START_TOKEN,
	yaml_FLOW_MAPPING_END_TOKEN,
	yaml_BLOCK_ENTRY_TOKEN,
	yaml_FLOW_ENTRY_TOKEN,
	yaml_KEY_TOKEN,
	yaml_VALUE_TOKEN,
	yaml_ALIAS_TOKEN,
	yaml_ANCHOR_TOKEN,
	yaml_TAG_TOKEN,
	yaml_SCALAR_TOKEN,
} yaml_token_type_t;

typedef struct {
	yaml_token_type_t typ;
	yaml_mark_t start_mark, end_mark;
	int encoding;
	Str value, suffix, prefix;
	int style;
	int8_t major, minor;
} yaml_token_t;

typedef enum {
	yaml_NO_EVENT,
	yaml_STREAM_START_EVENT,
	yaml_STREAM_END_EVENT,
	yaml_DOCUMENT_START_EVENT,
	yaml_DOCUMENT_END_EVENT,
	yaml_ALIAS_EVENT,
	yaml_SCALAR_EVENT,
	yaml_SEQUENCE_START_EVENT,
	yaml_SEQUENCE_END_EVENT,
	yaml_MAPPING_START_EVENT,
	yaml_MAPPING_END_EVENT,
	yaml_TAIL_COMMENT_EVENT,
} yaml_event_type_t;

typedef struct {
	yaml_event_type_t typ;
	yaml_mark_t start_mark, end_mark;
	int encoding;
	yaml_version_directive_t *version_directive;
	yaml_tag_directive_t *tag_directives;
	int n_tag_directives;
	Str head_comment, line_comment, foot_comment, tail_comment;
	Str anchor, tag, value;
	bool implicit, quoted_implicit;
	int style;
} yaml_event_t;

/* (yaml_event_type_t).String() */
const char *yaml_event_type_string(yaml_event_type_t e);

/* ---- the emitter ---- */

typedef enum {
	yaml_EMIT_STREAM_START_STATE,
	yaml_EMIT_FIRST_DOCUMENT_START_STATE,
	yaml_EMIT_DOCUMENT_START_STATE,
	yaml_EMIT_DOCUMENT_CONTENT_STATE,
	yaml_EMIT_DOCUMENT_END_STATE,
	yaml_EMIT_FLOW_SEQUENCE_FIRST_ITEM_STATE,
	yaml_EMIT_FLOW_SEQUENCE_TRAIL_ITEM_STATE,
	yaml_EMIT_FLOW_SEQUENCE_ITEM_STATE,
	yaml_EMIT_FLOW_MAPPING_FIRST_KEY_STATE,
	yaml_EMIT_FLOW_MAPPING_TRAIL_KEY_STATE,
	yaml_EMIT_FLOW_MAPPING_KEY_STATE,
	yaml_EMIT_FLOW_MAPPING_SIMPLE_VALUE_STATE,
	yaml_EMIT_FLOW_MAPPING_VALUE_STATE,
	yaml_EMIT_BLOCK_SEQUENCE_FIRST_ITEM_STATE,
	yaml_EMIT_BLOCK_SEQUENCE_ITEM_STATE,
	yaml_EMIT_BLOCK_MAPPING_FIRST_KEY_STATE,
	yaml_EMIT_BLOCK_MAPPING_KEY_STATE,
	yaml_EMIT_BLOCK_MAPPING_SIMPLE_VALUE_STATE,
	yaml_EMIT_BLOCK_MAPPING_VALUE_STATE,
	yaml_EMIT_END_STATE,
} yaml_emitter_state_t;

/* A growable int stack (states, indents). */
typedef struct {
	int *v;
	int n, cap;
} yaml_ints;
void yaml_ints_push(yaml_ints *s, int x);
static inline int yaml_ints_pop(yaml_ints *s) { return s->v[--s->n]; }

typedef struct {
	int error;
	const char *problem;

	Buf *out; /* the output (the Go port's output buffer, flushed into a []byte) */

	int encoding;
	bool canonical;
	int best_indent, best_width;
	bool unicode;
	int line_break;

	yaml_ints states;
	int state;

	yaml_event_t *events;
	int n_events, cap_events, events_head;

	yaml_ints indents;

	yaml_tag_directive_t *tag_directives;
	int n_tag_directives, cap_tag_directives;

	int indent;
	bool compact_sequence_indent;
	int flow_level;

	bool root_context, sequence_context, mapping_context, simple_key_context;

	int line, column;
	bool whitespace, indention, space_above;
	bool open_ended;
	int foot_indent;

	struct {
		Str anchor; /* p == NULL: nil */
		bool alias;
	} anchor_data;
	struct {
		Str handle, suffix;
	} tag_data;
	struct {
		Str value;
		bool multiline, flow_plain_allowed, block_plain_allowed, single_quoted_allowed, block_allowed;
		int style;
	} scalar_data;

	Str head_comment, line_comment, foot_comment, tail_comment, key_line_comment;
} yaml_emitter_t;

void yaml_emitter_initialize(yaml_emitter_t *emitter, Buf *out);
bool yaml_emitter_emit(yaml_emitter_t *emitter, yaml_event_t *event);

/* ---- the parser ---- */

typedef enum {
	yaml_PARSE_STREAM_START_STATE,
	yaml_PARSE_IMPLICIT_DOCUMENT_START_STATE,
	yaml_PARSE_DOCUMENT_START_STATE,
	yaml_PARSE_DOCUMENT_CONTENT_STATE,
	yaml_PARSE_DOCUMENT_END_STATE,
	yaml_PARSE_BLOCK_NODE_STATE,
	yaml_PARSE_BLOCK_NODE_OR_INDENTLESS_SEQUENCE_STATE,
	yaml_PARSE_FLOW_NODE_STATE,
	yaml_PARSE_BLOCK_SEQUENCE_FIRST_ENTRY_STATE,
	yaml_PARSE_BLOCK_SEQUENCE_ENTRY_STATE,
	yaml_PARSE_INDENTLESS_SEQUENCE_ENTRY_STATE,
	yaml_PARSE_BLOCK_MAPPING_FIRST_KEY_STATE,
	yaml_PARSE_BLOCK_MAPPING_KEY_STATE,
	yaml_PARSE_BLOCK_MAPPING_VALUE_STATE,
	yaml_PARSE_FLOW_SEQUENCE_FIRST_ENTRY_STATE,
	yaml_PARSE_FLOW_SEQUENCE_ENTRY_STATE,
	yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_KEY_STATE,
	yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_VALUE_STATE,
	yaml_PARSE_FLOW_SEQUENCE_ENTRY_MAPPING_END_STATE,
	yaml_PARSE_FLOW_MAPPING_FIRST_KEY_STATE,
	yaml_PARSE_FLOW_MAPPING_KEY_STATE,
	yaml_PARSE_FLOW_MAPPING_VALUE_STATE,
	yaml_PARSE_FLOW_MAPPING_EMPTY_VALUE_STATE,
	yaml_PARSE_END_STATE,
} yaml_parser_state_t;

typedef struct {
	bool possible, required;
	int token_number;
	yaml_mark_t mark;
} yaml_simple_key_t;

typedef struct {
	yaml_mark_t scan_mark, token_mark, start_mark, end_mark;
	Str head, line, foot;
} yaml_comment_t;

/* map[int]int (simple_keys_by_tok): open addressing; key -1 is free, -2 deleted */
typedef struct {
	int *keys, *vals;
	int cap, n, used;
} yaml_intmap;

#define YAML_INPUT_RAW_BUFFER_SIZE 512

typedef struct {
	int error;
	const char *problem;
	int problem_offset, problem_value;
	yaml_mark_t problem_mark;
	const char *context;
	yaml_mark_t context_mark;

	Str input;
	size_t input_pos;
	bool eof;

	/* the working buffer: parser.buffer[:buffer_len] */
	uint8_t *buffer;
	size_t buffer_len, buffer_cap, buffer_pos;
	int unread;
	int newlines;

	uint8_t raw_buffer[YAML_INPUT_RAW_BUFFER_SIZE];
	size_t raw_len, raw_pos;

	int encoding;
	int offset;
	yaml_mark_t mark;

	Str head_comment, line_comment, foot_comment, tail_comment, stem_comment;

	yaml_comment_t *comments;
	int n_comments, cap_comments, comments_head;

	bool stream_start_produced, stream_end_produced;
	int flow_level;

	yaml_token_t *tokens;
	int n_tokens, cap_tokens, tokens_head, tokens_parsed;
	bool token_available;

	int indent;
	yaml_ints indents;

	bool simple_key_allowed;
	yaml_simple_key_t *simple_keys;
	int n_simple_keys, cap_simple_keys;
	yaml_intmap simple_keys_by_tok;

	int state;
	yaml_ints states;
	yaml_mark_t *marks;
	int n_marks, cap_marks;
	yaml_tag_directive_t *tag_directives;
	int n_tag_directives, cap_tag_directives;
} yaml_parser_t;

/* the working buffer as a Str (bounds-checked reads through the character classes) */
static inline Str yaml_pbuf(const yaml_parser_t *p) { return (Str){(const char *)p->buffer, p->buffer_len}; }

/* readerc.go */
bool yaml_parser_update_buffer(yaml_parser_t *parser, int length);
/* scannerc.go */
bool yaml_parser_fetch_more_tokens(yaml_parser_t *parser);
/* parserc.go */
bool yaml_parser_parse(yaml_parser_t *parser, yaml_event_t *event);
/* Str concatenation for comments (a new array, as Go's append on these slices behaves) */
Str yaml_str_cat(Str a, Str b);
Str yaml_str_catc(Str a, char c);

/* ---- resolve.go ---- */

/* resolve("", in): the implicit short tag ("!!str", "!!int", ...) of a plain scalar. */
const char *yaml_resolve_tag(Str in);
Str yaml_short_tag(Str tag);
Str yaml_long_tag(Str tag);

/* ---- yamlprivateh.go: character classes (bounds-checked: bytes past the end read as 0) ---- */

static inline uint8_t yaml_at(Str b, size_t i) { return i < b.n ? (uint8_t)b.p[i] : 0; }
static inline bool is_alpha(Str b, size_t i)
{
	uint8_t c = yaml_at(b, i);
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-';
}
static inline bool is_digit(Str b, size_t i) { return yaml_at(b, i) >= '0' && yaml_at(b, i) <= '9'; }
static inline int as_digit(Str b, size_t i) { return (int)yaml_at(b, i) - '0'; }
static inline bool is_hex(Str b, size_t i)
{
	uint8_t c = yaml_at(b, i);
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
static inline int as_hex(Str b, size_t i)
{
	uint8_t c = yaml_at(b, i);
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return c - '0';
}
static inline bool is_ascii(Str b, size_t i) { return yaml_at(b, i) <= 0x7F; }
static inline bool is_printable(Str b, size_t i)
{
	uint8_t c = yaml_at(b, i), c1 = yaml_at(b, i + 1), c2 = yaml_at(b, i + 2);
	return c == 0x0A || (c >= 0x20 && c <= 0x7E) || (c == 0xC2 && c1 >= 0xA0) || (c > 0xC2 && c < 0xED) ||
	       (c == 0xED && c1 < 0xA0) || c == 0xEE ||
	       (c == 0xEF && !(c1 == 0xBB && c2 == 0xBF) && !(c1 == 0xBF && (c2 == 0xBE || c2 == 0xBF)));
}
static inline bool is_z(Str b, size_t i) { return yaml_at(b, i) == 0; }
/* is_bom ignores i (as upstream does) */
static inline bool is_bom(Str b, size_t i)
{
	(void)i;
	return yaml_at(b, 0) == 0xEF && yaml_at(b, 1) == 0xBB && yaml_at(b, 2) == 0xBF;
}
static inline bool is_space(Str b, size_t i) { return yaml_at(b, i) == ' '; }
static inline bool is_tab(Str b, size_t i) { return yaml_at(b, i) == '\t'; }
static inline bool is_blank(Str b, size_t i) { return yaml_at(b, i) == ' ' || yaml_at(b, i) == '\t'; }
static inline bool is_break(Str b, size_t i)
{
	uint8_t c = yaml_at(b, i);
	return c == '\r' || c == '\n' || (c == 0xC2 && yaml_at(b, i + 1) == 0x85) ||
	       (c == 0xE2 && yaml_at(b, i + 1) == 0x80 && yaml_at(b, i + 2) == 0xA8) ||
	       (c == 0xE2 && yaml_at(b, i + 1) == 0x80 && yaml_at(b, i + 2) == 0xA9);
}
static inline bool is_crlf(Str b, size_t i) { return yaml_at(b, i) == '\r' && yaml_at(b, i + 1) == '\n'; }
static inline bool is_breakz(Str b, size_t i) { return is_break(b, i) || yaml_at(b, i) == 0; }
static inline bool is_spacez(Str b, size_t i) { return yaml_at(b, i) == ' ' || is_breakz(b, i); }
static inline bool is_blankz(Str b, size_t i) { return is_blank(b, i) || is_breakz(b, i); }
static inline int yaml_width(uint8_t b)
{
	if ((b & 0x80) == 0x00)
		return 1;
	if ((b & 0xE0) == 0xC0)
		return 2;
	if ((b & 0xF0) == 0xE0)
		return 3;
	if ((b & 0xF8) == 0xF0)
		return 4;
	return 0;
}

#endif
