/* Copyright (c) 2011-2019 Canonical Ltd
 * Copyright (c) 2006-2010 Kirill Simonov
 * Licensed under the Apache License, Version 2.0 and the MIT License (see LICENSE of
 * go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 emitterc.go (with writerc.go and the emitter parts of
 * apic.go). The output buffer is the Buf of the encoder: flushing is a no-op. */
#include "internal.h"

void yaml_ints_push(yaml_ints *s, int x)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 16;
		s->v = xrealloc(s->v, sizeof *s->v * (size_t)s->cap, sizeof *s->v * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = x;
}

static const char *const event_strings[] = {
	"none",        "stream start", "stream end",   "document start", "document end", "alias",
	"scalar",      "sequence start", "sequence end", "mapping start", "mapping end", "tail comment",
};

const char *yaml_event_type_string(yaml_event_type_t e)
{
	if ((int)e < 0 || (size_t)e >= sizeof event_strings / sizeof *event_strings)
		return xfmt("unknown event %d", (int)e);
	return event_strings[e];
}

void yaml_emitter_initialize(yaml_emitter_t *emitter, Buf *out)
{
	memset(emitter, 0, sizeof *emitter);
	emitter->out = out;
	emitter->best_width = -1;
}

#define S(lit) STR_LIT(lit)

static bool put(yaml_emitter_t *emitter, uint8_t value)
{
	buf_addc(emitter->out, (char)value);
	emitter->column++;
	return true;
}

static bool put_break(yaml_emitter_t *emitter)
{
	switch (emitter->line_break) {
	case yaml_CR_BREAK:
		buf_addc(emitter->out, '\r');
		break;
	case yaml_LN_BREAK:
		buf_addc(emitter->out, '\n');
		break;
	case yaml_CRLN_BREAK:
		buf_add(emitter->out, "\r\n", 2);
		break;
	default:
		fail("unknown line break setting");
	}
	if (emitter->column == 0)
		emitter->space_above = true;
	emitter->column = 0;
	emitter->line++;
	emitter->indention = true;
	return true;
}

/* Copies one character from s at *i to the buffer and advances *i. */
static bool write1(yaml_emitter_t *emitter, Str s, size_t *i)
{
	int w = yaml_width(yaml_at(s, *i));
	if (w == 0 || *i + (size_t)w > s.n)
		fail("unknown character width");
	buf_add(emitter->out, s.p + *i, (size_t)w);
	emitter->column++;
	*i += (size_t)w;
	return true;
}

static bool write_all(yaml_emitter_t *emitter, Str s)
{
	for (size_t i = 0; i < s.n;)
		if (!write1(emitter, s, &i))
			return false;
	return true;
}

static bool write_break(yaml_emitter_t *emitter, Str s, size_t *i)
{
	if (yaml_at(s, *i) == '\n') {
		if (!put_break(emitter))
			return false;
		(*i)++;
	} else {
		if (!write1(emitter, s, i))
			return false;
		if (emitter->column == 0)
			emitter->space_above = true;
		emitter->column = 0;
		emitter->line++;
		emitter->indention = true;
	}
	return true;
}

static bool set_emitter_error(yaml_emitter_t *emitter, const char *problem)
{
	emitter->error = yaml_EMITTER_ERROR;
	emitter->problem = problem;
	return false;
}

static bool need_more_events(yaml_emitter_t *emitter);
static bool analyze_event(yaml_emitter_t *emitter, yaml_event_t *event);
static bool state_machine(yaml_emitter_t *emitter, yaml_event_t *event);

bool yaml_emitter_emit(yaml_emitter_t *emitter, yaml_event_t *event)
{
	if (emitter->n_events == emitter->cap_events) {
		int cap = emitter->cap_events ? emitter->cap_events * 2 : 16;
		emitter->events = xrealloc(emitter->events, sizeof *emitter->events * (size_t)emitter->cap_events,
					   sizeof *emitter->events * (size_t)cap);
		emitter->cap_events = cap;
	}
	emitter->events[emitter->n_events++] = *event;
	while (!need_more_events(emitter)) {
		yaml_event_t *ev = &emitter->events[emitter->events_head];
		if (!analyze_event(emitter, ev))
			return false;
		if (!state_machine(emitter, ev))
			return false;
		memset(ev, 0, sizeof *ev);
		emitter->events_head++;
	}
	return true;
}

/* Check if we need to accumulate more events before emitting.
 *
 * We accumulate extra
 *  - 1 event for DOCUMENT-START
 *  - 2 events for SEQUENCE-START
 *  - 3 events for MAPPING-START */
static bool need_more_events(yaml_emitter_t *emitter)
{
	if (emitter->events_head == emitter->n_events)
		return true;
	int accumulate;
	switch (emitter->events[emitter->events_head].typ) {
	case yaml_DOCUMENT_START_EVENT:
		accumulate = 1;
		break;
	case yaml_SEQUENCE_START_EVENT:
		accumulate = 2;
		break;
	case yaml_MAPPING_START_EVENT:
		accumulate = 3;
		break;
	default:
		return false;
	}
	if (emitter->n_events - emitter->events_head > accumulate)
		return false;
	int level = 0;
	for (int i = emitter->events_head; i < emitter->n_events; i++) {
		switch (emitter->events[i].typ) {
		case yaml_STREAM_START_EVENT:
		case yaml_DOCUMENT_START_EVENT:
		case yaml_SEQUENCE_START_EVENT:
		case yaml_MAPPING_START_EVENT:
			level++;
			break;
		case yaml_STREAM_END_EVENT:
		case yaml_DOCUMENT_END_EVENT:
		case yaml_SEQUENCE_END_EVENT:
		case yaml_MAPPING_END_EVENT:
			level--;
			break;
		default:
			break;
		}
		if (level == 0)
			return false;
	}
	return true;
}

static const yaml_tag_directive_t default_tag_directives[] = {
	{{"!", 1}, {"!", 1}},
	{{"!!", 2}, {"tag:yaml.org,2002:", 18}},
};

static bool append_tag_directive(yaml_emitter_t *emitter, const yaml_tag_directive_t *value, bool allow_duplicates)
{
	for (int i = 0; i < emitter->n_tag_directives; i++) {
		if (str_equal(value->handle, emitter->tag_directives[i].handle)) {
			if (allow_duplicates)
				return true;
			return set_emitter_error(emitter, "duplicate %TAG directive");
		}
	}
	if (emitter->n_tag_directives == emitter->cap_tag_directives) {
		int cap = emitter->cap_tag_directives ? emitter->cap_tag_directives * 2 : 4;
		emitter->tag_directives = xrealloc(emitter->tag_directives, sizeof *emitter->tag_directives * (size_t)emitter->cap_tag_directives,
						   sizeof *emitter->tag_directives * (size_t)cap);
		emitter->cap_tag_directives = cap;
	}
	emitter->tag_directives[emitter->n_tag_directives++] = *value;
	return true;
}

static bool increase_indent_compact(yaml_emitter_t *emitter, bool flow, bool indentless, bool compact_seq)
{
	yaml_ints_push(&emitter->indents, emitter->indent);
	if (emitter->indent < 0) {
		if (flow)
			emitter->indent = emitter->best_indent;
		else
			emitter->indent = 0;
	} else if (!indentless) {
		/* [Go] This was changed so that indentations are more regular. */
		if (emitter->states.v[emitter->states.n - 1] == yaml_EMIT_BLOCK_SEQUENCE_ITEM_STATE) {
			/* The first indent inside a sequence will just skip the "- " indicator. */
			emitter->indent += 2;
		} else {
			/* Everything else aligns to the chosen indentation. */
			emitter->indent = emitter->best_indent * ((emitter->indent + emitter->best_indent) / emitter->best_indent);
			if (compact_seq) {
				/* The value compact_seq passed in is almost always set to `false` when this
				 * function is called, except when we are dealing with sequence nodes. */
				emitter->indent = emitter->indent - 2;
			}
		}
	}
	return true;
}

static bool increase_indent(yaml_emitter_t *emitter, bool flow, bool indentless)
{
	return increase_indent_compact(emitter, flow, indentless, false);
}

static bool emit_stream_start(yaml_emitter_t *emitter, yaml_event_t *event);
static bool emit_document_start(yaml_emitter_t *emitter, yaml_event_t *event, bool first);
static bool emit_document_content(yaml_emitter_t *emitter, yaml_event_t *event);
static bool emit_document_end(yaml_emitter_t *emitter, yaml_event_t *event);
static bool emit_flow_sequence_item(yaml_emitter_t *emitter, yaml_event_t *event, bool first, bool trail);
static bool emit_flow_mapping_key(yaml_emitter_t *emitter, yaml_event_t *event, bool first, bool trail);
static bool emit_flow_mapping_value(yaml_emitter_t *emitter, yaml_event_t *event, bool simple);
static bool emit_block_sequence_item(yaml_emitter_t *emitter, yaml_event_t *event, bool first);
static bool emit_block_mapping_key(yaml_emitter_t *emitter, yaml_event_t *event, bool first);
static bool emit_block_mapping_value(yaml_emitter_t *emitter, yaml_event_t *event, bool simple);
static bool emit_node(yaml_emitter_t *emitter, yaml_event_t *event, bool root, bool sequence, bool mapping, bool simple_key);
static bool write_indent(yaml_emitter_t *emitter);
static bool write_indicator(yaml_emitter_t *emitter, Str indicator, bool need_whitespace, bool is_whitespace, bool is_indention);
static bool write_anchor(yaml_emitter_t *emitter, Str value);
static bool write_tag_handle(yaml_emitter_t *emitter, Str value);
static bool write_tag_content(yaml_emitter_t *emitter, Str value, bool need_whitespace);
static bool write_plain_scalar(yaml_emitter_t *emitter, Str value, bool allow_breaks);
static bool write_single_quoted_scalar(yaml_emitter_t *emitter, Str value, bool allow_breaks);
static bool write_double_quoted_scalar(yaml_emitter_t *emitter, Str value, bool allow_breaks);
static bool write_literal_scalar(yaml_emitter_t *emitter, Str value);
static bool write_folded_scalar(yaml_emitter_t *emitter, Str value);
static bool write_comment(yaml_emitter_t *emitter, Str comment);
static bool process_head_comment(yaml_emitter_t *emitter);
static bool process_line_comment_linebreak(yaml_emitter_t *emitter, bool linebreak);
static bool process_foot_comment(yaml_emitter_t *emitter);
static bool check_simple_key(yaml_emitter_t *emitter);

static bool process_line_comment(yaml_emitter_t *emitter) { return process_line_comment_linebreak(emitter, false); }

static void pop_state(yaml_emitter_t *emitter) { emitter->state = yaml_ints_pop(&emitter->states); }

/* State dispatcher. */
static bool state_machine(yaml_emitter_t *emitter, yaml_event_t *event)
{
	switch (emitter->state) {
	case yaml_EMIT_STREAM_START_STATE:
		return emit_stream_start(emitter, event);
	case yaml_EMIT_FIRST_DOCUMENT_START_STATE:
		return emit_document_start(emitter, event, true);
	case yaml_EMIT_DOCUMENT_START_STATE:
		return emit_document_start(emitter, event, false);
	case yaml_EMIT_DOCUMENT_CONTENT_STATE:
		return emit_document_content(emitter, event);
	case yaml_EMIT_DOCUMENT_END_STATE:
		return emit_document_end(emitter, event);
	case yaml_EMIT_FLOW_SEQUENCE_FIRST_ITEM_STATE:
		return emit_flow_sequence_item(emitter, event, true, false);
	case yaml_EMIT_FLOW_SEQUENCE_TRAIL_ITEM_STATE:
		return emit_flow_sequence_item(emitter, event, false, true);
	case yaml_EMIT_FLOW_SEQUENCE_ITEM_STATE:
		return emit_flow_sequence_item(emitter, event, false, false);
	case yaml_EMIT_FLOW_MAPPING_FIRST_KEY_STATE:
		return emit_flow_mapping_key(emitter, event, true, false);
	case yaml_EMIT_FLOW_MAPPING_TRAIL_KEY_STATE:
		return emit_flow_mapping_key(emitter, event, false, true);
	case yaml_EMIT_FLOW_MAPPING_KEY_STATE:
		return emit_flow_mapping_key(emitter, event, false, false);
	case yaml_EMIT_FLOW_MAPPING_SIMPLE_VALUE_STATE:
		return emit_flow_mapping_value(emitter, event, true);
	case yaml_EMIT_FLOW_MAPPING_VALUE_STATE:
		return emit_flow_mapping_value(emitter, event, false);
	case yaml_EMIT_BLOCK_SEQUENCE_FIRST_ITEM_STATE:
		return emit_block_sequence_item(emitter, event, true);
	case yaml_EMIT_BLOCK_SEQUENCE_ITEM_STATE:
		return emit_block_sequence_item(emitter, event, false);
	case yaml_EMIT_BLOCK_MAPPING_FIRST_KEY_STATE:
		return emit_block_mapping_key(emitter, event, true);
	case yaml_EMIT_BLOCK_MAPPING_KEY_STATE:
		return emit_block_mapping_key(emitter, event, false);
	case yaml_EMIT_BLOCK_MAPPING_SIMPLE_VALUE_STATE:
		return emit_block_mapping_value(emitter, event, true);
	case yaml_EMIT_BLOCK_MAPPING_VALUE_STATE:
		return emit_block_mapping_value(emitter, event, false);
	case yaml_EMIT_END_STATE:
		return set_emitter_error(emitter, "expected nothing after STREAM-END");
	}
	fail("invalid emitter state");
}

/* Expect STREAM-START. */
static bool emit_stream_start(yaml_emitter_t *emitter, yaml_event_t *event)
{
	if (event->typ != yaml_STREAM_START_EVENT)
		return set_emitter_error(emitter, "expected STREAM-START");
	if (emitter->encoding == yaml_ANY_ENCODING) {
		emitter->encoding = event->encoding;
		if (emitter->encoding == yaml_ANY_ENCODING)
			emitter->encoding = yaml_UTF8_ENCODING;
	}
	if (emitter->best_indent < 2 || emitter->best_indent > 9)
		emitter->best_indent = 2;
	if (emitter->best_width >= 0 && emitter->best_width <= emitter->best_indent * 2)
		emitter->best_width = 80;
	if (emitter->best_width < 0)
		emitter->best_width = 2147483647;
	if (emitter->line_break == yaml_ANY_BREAK)
		emitter->line_break = yaml_LN_BREAK;

	emitter->indent = -1;
	emitter->line = 0;
	emitter->column = 0;
	emitter->whitespace = true;
	emitter->indention = true;
	emitter->space_above = true;
	emitter->foot_indent = -1;

	if (emitter->encoding != yaml_UTF8_ENCODING)
		buf_add(emitter->out, "\xEF\xBB\xBF", 3); /* yaml_emitter_write_bom */
	emitter->state = yaml_EMIT_FIRST_DOCUMENT_START_STATE;
	return true;
}

/* Expect DOCUMENT-START or STREAM-END. */
static bool emit_document_start(yaml_emitter_t *emitter, yaml_event_t *event, bool first)
{
	if (event->typ == yaml_DOCUMENT_START_EVENT) {
		if (event->version_directive) {
			if (event->version_directive->major != 1 || event->version_directive->minor != 1)
				return set_emitter_error(emitter, "incompatible %YAML directive");
		}
		for (int i = 0; i < event->n_tag_directives; i++) {
			yaml_tag_directive_t *td = &event->tag_directives[i];
			/* yaml_emitter_analyze_tag_directive */
			Str handle = td->handle, prefix = td->prefix;
			if (handle.n == 0)
				return set_emitter_error(emitter, "tag handle must not be empty");
			if (handle.p[0] != '!')
				return set_emitter_error(emitter, "tag handle must start with '!'");
			if (handle.p[handle.n - 1] != '!')
				return set_emitter_error(emitter, "tag handle must end with '!'");
			for (size_t k = 1; k < handle.n - 1;) {
				if (!is_alpha(handle, k))
					return set_emitter_error(emitter, "tag handle must contain alphanumerical characters only");
				int w = yaml_width(yaml_at(handle, k));
				k += w ? (size_t)w : 1;
			}
			if (prefix.n == 0)
				return set_emitter_error(emitter, "tag prefix must not be empty");
			if (!append_tag_directive(emitter, td, false))
				return false;
		}
		for (size_t i = 0; i < sizeof default_tag_directives / sizeof *default_tag_directives; i++)
			if (!append_tag_directive(emitter, &default_tag_directives[i], true))
				return false;

		bool implicit = event->implicit;
		if (!first || emitter->canonical)
			implicit = false;

		if (emitter->open_ended && (event->version_directive || event->n_tag_directives > 0)) {
			if (!write_indicator(emitter, S("..."), true, false, false))
				return false;
			if (!write_indent(emitter))
				return false;
		}

		if (event->version_directive) {
			implicit = false;
			if (!write_indicator(emitter, S("%YAML"), true, false, false))
				return false;
			if (!write_indicator(emitter, S("1.1"), true, false, false))
				return false;
			if (!write_indent(emitter))
				return false;
		}

		if (event->n_tag_directives > 0) {
			implicit = false;
			for (int i = 0; i < event->n_tag_directives; i++) {
				yaml_tag_directive_t *td = &event->tag_directives[i];
				if (!write_indicator(emitter, S("%TAG"), true, false, false))
					return false;
				if (!write_tag_handle(emitter, td->handle))
					return false;
				if (!write_tag_content(emitter, td->prefix, true))
					return false;
				if (!write_indent(emitter))
					return false;
			}
		}

		/* yaml_emitter_check_empty_document: always false */

		if (!implicit) {
			if (!write_indent(emitter))
				return false;
			if (!write_indicator(emitter, S("---"), true, false, false))
				return false;
			if (!write_indent(emitter)) /* emitter.canonical || true */
				return false;
		}

		if (emitter->head_comment.n > 0) {
			if (!process_head_comment(emitter))
				return false;
			if (!put_break(emitter))
				return false;
		}

		emitter->state = yaml_EMIT_DOCUMENT_CONTENT_STATE;
		return true;
	}

	if (event->typ == yaml_STREAM_END_EVENT) {
		if (emitter->open_ended) {
			if (!write_indicator(emitter, S("..."), true, false, false))
				return false;
			if (!write_indent(emitter))
				return false;
		}
		emitter->state = yaml_EMIT_END_STATE;
		return true;
	}

	return set_emitter_error(emitter, "expected DOCUMENT-START or STREAM-END");
}

/* Expect the root node. */
static bool emit_document_content(yaml_emitter_t *emitter, yaml_event_t *event)
{
	yaml_ints_push(&emitter->states, yaml_EMIT_DOCUMENT_END_STATE);
	if (!process_head_comment(emitter))
		return false;
	if (!emit_node(emitter, event, true, false, false, false))
		return false;
	if (!process_line_comment(emitter))
		return false;
	if (!process_foot_comment(emitter))
		return false;
	return true;
}

/* Expect DOCUMENT-END. */
static bool emit_document_end(yaml_emitter_t *emitter, yaml_event_t *event)
{
	if (event->typ != yaml_DOCUMENT_END_EVENT)
		return set_emitter_error(emitter, "expected DOCUMENT-END");
	/* [Go] Force document foot separation. */
	emitter->foot_indent = 0;
	if (!process_foot_comment(emitter))
		return false;
	emitter->foot_indent = -1;
	if (!write_indent(emitter))
		return false;
	if (!event->implicit) {
		/* [Go] Allocate the slice elsewhere. */
		if (!write_indicator(emitter, S("..."), true, false, false))
			return false;
		if (!write_indent(emitter))
			return false;
	}
	emitter->state = yaml_EMIT_DOCUMENT_START_STATE;
	emitter->n_tag_directives = 0;
	return true;
}

static bool comments_pending(const yaml_emitter_t *emitter)
{
	return emitter->line_comment.n + emitter->foot_comment.n + emitter->tail_comment.n > 0;
}

/* Expect a flow item node. */
static bool emit_flow_sequence_item(yaml_emitter_t *emitter, yaml_event_t *event, bool first, bool trail)
{
	if (first) {
		if (!write_indicator(emitter, S("["), true, true, false))
			return false;
		if (!increase_indent(emitter, true, false))
			return false;
		emitter->flow_level++;
	}

	if (event->typ == yaml_SEQUENCE_END_EVENT) {
		if (emitter->canonical && !first && !trail) {
			if (!write_indicator(emitter, S(","), false, false, false))
				return false;
		}
		emitter->flow_level--;
		emitter->indent = yaml_ints_pop(&emitter->indents);
		if (emitter->column == 0 || (emitter->canonical && !first)) {
			if (!write_indent(emitter))
				return false;
		}
		if (!write_indicator(emitter, S("]"), false, false, false))
			return false;
		if (!process_line_comment(emitter))
			return false;
		if (!process_foot_comment(emitter))
			return false;
		pop_state(emitter);
		return true;
	}

	if (!first && !trail) {
		if (!write_indicator(emitter, S(","), false, false, false))
			return false;
	}

	if (!process_head_comment(emitter))
		return false;
	if (emitter->column == 0) {
		if (!write_indent(emitter))
			return false;
	}

	if (emitter->canonical || emitter->column > emitter->best_width) {
		if (!write_indent(emitter))
			return false;
	}
	if (comments_pending(emitter))
		yaml_ints_push(&emitter->states, yaml_EMIT_FLOW_SEQUENCE_TRAIL_ITEM_STATE);
	else
		yaml_ints_push(&emitter->states, yaml_EMIT_FLOW_SEQUENCE_ITEM_STATE);
	if (!emit_node(emitter, event, false, true, false, false))
		return false;
	if (comments_pending(emitter)) {
		if (!write_indicator(emitter, S(","), false, false, false))
			return false;
	}
	if (!process_line_comment(emitter))
		return false;
	if (!process_foot_comment(emitter))
		return false;
	return true;
}

/* Expect a flow key node. */
static bool emit_flow_mapping_key(yaml_emitter_t *emitter, yaml_event_t *event, bool first, bool trail)
{
	if (first) {
		if (!write_indicator(emitter, S("{"), true, true, false))
			return false;
		if (!increase_indent(emitter, true, false))
			return false;
		emitter->flow_level++;
	}

	if (event->typ == yaml_MAPPING_END_EVENT) {
		if ((emitter->canonical || emitter->head_comment.n + emitter->foot_comment.n + emitter->tail_comment.n > 0) && !first &&
		    !trail) {
			if (!write_indicator(emitter, S(","), false, false, false))
				return false;
		}
		if (!process_head_comment(emitter))
			return false;
		emitter->flow_level--;
		emitter->indent = yaml_ints_pop(&emitter->indents);
		if (emitter->canonical && !first) {
			if (!write_indent(emitter))
				return false;
		}
		if (!write_indicator(emitter, S("}"), false, false, false))
			return false;
		if (!process_line_comment(emitter))
			return false;
		if (!process_foot_comment(emitter))
			return false;
		pop_state(emitter);
		return true;
	}

	if (!first && !trail) {
		if (!write_indicator(emitter, S(","), false, false, false))
			return false;
	}

	if (!process_head_comment(emitter))
		return false;

	if (emitter->column == 0) {
		if (!write_indent(emitter))
			return false;
	}

	if (emitter->canonical || emitter->column > emitter->best_width) {
		if (!write_indent(emitter))
			return false;
	}

	if (!emitter->canonical && check_simple_key(emitter)) {
		yaml_ints_push(&emitter->states, yaml_EMIT_FLOW_MAPPING_SIMPLE_VALUE_STATE);
		return emit_node(emitter, event, false, false, true, true);
	}
	if (!write_indicator(emitter, S("?"), true, false, false))
		return false;
	yaml_ints_push(&emitter->states, yaml_EMIT_FLOW_MAPPING_VALUE_STATE);
	return emit_node(emitter, event, false, false, true, false);
}

/* Expect a flow value node. */
static bool emit_flow_mapping_value(yaml_emitter_t *emitter, yaml_event_t *event, bool simple)
{
	if (simple) {
		if (!write_indicator(emitter, S(":"), false, false, false))
			return false;
	} else {
		if (emitter->canonical || emitter->column > emitter->best_width) {
			if (!write_indent(emitter))
				return false;
		}
		if (!write_indicator(emitter, S(":"), true, false, false))
			return false;
	}
	if (comments_pending(emitter))
		yaml_ints_push(&emitter->states, yaml_EMIT_FLOW_MAPPING_TRAIL_KEY_STATE);
	else
		yaml_ints_push(&emitter->states, yaml_EMIT_FLOW_MAPPING_KEY_STATE);
	if (!emit_node(emitter, event, false, false, true, false))
		return false;
	if (comments_pending(emitter)) {
		if (!write_indicator(emitter, S(","), false, false, false))
			return false;
	}
	if (!process_line_comment(emitter))
		return false;
	if (!process_foot_comment(emitter))
		return false;
	return true;
}

/* Expect a block item node. */
static bool emit_block_sequence_item(yaml_emitter_t *emitter, yaml_event_t *event, bool first)
{
	if (first) {
		/* emitter.mapping context tells us if we are really in a mapping context.
		 * emitter.column tells us which column we are in the yaml output. 0 is the first
		 * char of the column. emitter.indentation tells us if the last character was an
		 * indentation character. emitter.compact_sequence_indent tells us if '- ' is
		 * considered part of the indentation for sequence elements. So, `seq` means that we
		 * are in a mapping context, and we are either at the first char of the column or
		 * the last character was not an indentation character, and we consider '- ' part
		 * of the indentation for sequence elements. */
		bool seq = emitter->mapping_context && (emitter->column == 0 || !emitter->indention) &&
			   emitter->compact_sequence_indent;
		if (!increase_indent_compact(emitter, false, false, seq))
			return false;
	}
	if (event->typ == yaml_SEQUENCE_END_EVENT) {
		emitter->indent = yaml_ints_pop(&emitter->indents);
		pop_state(emitter);
		return true;
	}
	if (!process_head_comment(emitter))
		return false;
	if (!write_indent(emitter))
		return false;
	if (!write_indicator(emitter, S("-"), true, false, true))
		return false;
	yaml_ints_push(&emitter->states, yaml_EMIT_BLOCK_SEQUENCE_ITEM_STATE);
	if (!emit_node(emitter, event, false, true, false, false))
		return false;
	if (!process_line_comment(emitter))
		return false;
	if (!process_foot_comment(emitter))
		return false;
	return true;
}

/* Expect a block key node. */
static bool emit_block_mapping_key(yaml_emitter_t *emitter, yaml_event_t *event, bool first)
{
	if (first) {
		if (!increase_indent(emitter, false, false))
			return false;
	}
	if (!process_head_comment(emitter))
		return false;
	if (event->typ == yaml_MAPPING_END_EVENT) {
		emitter->indent = yaml_ints_pop(&emitter->indents);
		pop_state(emitter);
		return true;
	}
	if (!write_indent(emitter))
		return false;
	if (emitter->line_comment.n > 0) {
		/* [Go] A line comment was provided for the key. That's unusual as the scanner
		 * associates line comments with the value. Either way, save the line comment and
		 * render it appropriately later. */
		emitter->key_line_comment = emitter->line_comment;
		emitter->line_comment = (Str){0};
	}
	if (check_simple_key(emitter)) {
		yaml_ints_push(&emitter->states, yaml_EMIT_BLOCK_MAPPING_SIMPLE_VALUE_STATE);
		return emit_node(emitter, event, false, false, true, true);
	}
	if (!write_indicator(emitter, S("?"), true, false, true))
		return false;
	yaml_ints_push(&emitter->states, yaml_EMIT_BLOCK_MAPPING_VALUE_STATE);
	return emit_node(emitter, event, false, false, true, false);
}

/* Expect a block value node. */
static bool emit_block_mapping_value(yaml_emitter_t *emitter, yaml_event_t *event, bool simple)
{
	if (simple) {
		if (!write_indicator(emitter, S(":"), false, false, false))
			return false;
	} else {
		if (!write_indent(emitter))
			return false;
		if (!write_indicator(emitter, S(":"), true, false, true))
			return false;
	}
	if (emitter->key_line_comment.n > 0) {
		/* [Go] Line comments are generally associated with the value, but when there's no
		 * value on the same line as a mapping key they end up attached to the key itself. */
		if (event->typ == yaml_SCALAR_EVENT) {
			if (emitter->line_comment.n == 0) {
				/* A scalar is coming and it has no line comments by itself yet, so just
				 * let it handle the line comment as usual. If it has a line comment, we
				 * can't have both so the one from the key is lost. */
				emitter->line_comment = emitter->key_line_comment;
				emitter->key_line_comment = (Str){0};
			}
		} else if (event->style != yaml_FLOW_SEQUENCE_STYLE &&
			   (event->typ == yaml_MAPPING_START_EVENT || event->typ == yaml_SEQUENCE_START_EVENT)) {
			/* An indented block follows, so write the comment right now. */
			Str t = emitter->line_comment;
			emitter->line_comment = emitter->key_line_comment;
			emitter->key_line_comment = t;
			if (!process_line_comment(emitter))
				return false;
			t = emitter->line_comment;
			emitter->line_comment = emitter->key_line_comment;
			emitter->key_line_comment = t;
		}
	}
	yaml_ints_push(&emitter->states, yaml_EMIT_BLOCK_MAPPING_KEY_STATE);
	if (!emit_node(emitter, event, false, false, true, false))
		return false;
	if (!process_line_comment(emitter))
		return false;
	if (!process_foot_comment(emitter))
		return false;
	return true;
}

static bool emit_alias(yaml_emitter_t *emitter, yaml_event_t *event);
static bool emit_scalar(yaml_emitter_t *emitter, yaml_event_t *event);
static bool emit_sequence_start(yaml_emitter_t *emitter, yaml_event_t *event);
static bool emit_mapping_start(yaml_emitter_t *emitter, yaml_event_t *event);

/* Expect a node. */
static bool emit_node(yaml_emitter_t *emitter, yaml_event_t *event, bool root, bool sequence, bool mapping, bool simple_key)
{
	emitter->root_context = root;
	emitter->sequence_context = sequence;
	emitter->mapping_context = mapping;
	emitter->simple_key_context = simple_key;

	switch (event->typ) {
	case yaml_ALIAS_EVENT:
		return emit_alias(emitter, event);
	case yaml_SCALAR_EVENT:
		return emit_scalar(emitter, event);
	case yaml_SEQUENCE_START_EVENT:
		return emit_sequence_start(emitter, event);
	case yaml_MAPPING_START_EVENT:
		return emit_mapping_start(emitter, event);
	default:
		return set_emitter_error(
			emitter, xcat("expected SCALAR, SEQUENCE-START, MAPPING-START, or ALIAS, but got ", yaml_event_type_string(event->typ)));
	}
}

static bool process_anchor(yaml_emitter_t *emitter);
static bool process_tag(yaml_emitter_t *emitter);
static bool select_scalar_style(yaml_emitter_t *emitter, yaml_event_t *event);
static bool process_scalar(yaml_emitter_t *emitter);

/* Expect ALIAS. */
static bool emit_alias(yaml_emitter_t *emitter, yaml_event_t *event)
{
	(void)event;
	if (!process_anchor(emitter))
		return false;
	pop_state(emitter);
	return true;
}

/* Expect SCALAR. */
static bool emit_scalar(yaml_emitter_t *emitter, yaml_event_t *event)
{
	if (!select_scalar_style(emitter, event))
		return false;
	if (!process_anchor(emitter))
		return false;
	if (!process_tag(emitter))
		return false;
	if (!increase_indent(emitter, true, false))
		return false;
	if (!process_scalar(emitter))
		return false;
	emitter->indent = yaml_ints_pop(&emitter->indents);
	pop_state(emitter);
	return true;
}

static bool check_empty_sequence(yaml_emitter_t *emitter);
static bool check_empty_mapping(yaml_emitter_t *emitter);

/* Expect SEQUENCE-START. */
static bool emit_sequence_start(yaml_emitter_t *emitter, yaml_event_t *event)
{
	if (!process_anchor(emitter))
		return false;
	if (!process_tag(emitter))
		return false;
	if (emitter->flow_level > 0 || emitter->canonical || event->style == yaml_FLOW_SEQUENCE_STYLE || check_empty_sequence(emitter))
		emitter->state = yaml_EMIT_FLOW_SEQUENCE_FIRST_ITEM_STATE;
	else
		emitter->state = yaml_EMIT_BLOCK_SEQUENCE_FIRST_ITEM_STATE;
	return true;
}

/* Expect MAPPING-START. */
static bool emit_mapping_start(yaml_emitter_t *emitter, yaml_event_t *event)
{
	if (!process_anchor(emitter))
		return false;
	if (!process_tag(emitter))
		return false;
	if (emitter->flow_level > 0 || emitter->canonical || event->style == yaml_FLOW_MAPPING_STYLE || check_empty_mapping(emitter))
		emitter->state = yaml_EMIT_FLOW_MAPPING_FIRST_KEY_STATE;
	else
		emitter->state = yaml_EMIT_BLOCK_MAPPING_FIRST_KEY_STATE;
	return true;
}

/* Check if the next events represent an empty sequence. */
static bool check_empty_sequence(yaml_emitter_t *emitter)
{
	if (emitter->n_events - emitter->events_head < 2)
		return false;
	return emitter->events[emitter->events_head].typ == yaml_SEQUENCE_START_EVENT &&
	       emitter->events[emitter->events_head + 1].typ == yaml_SEQUENCE_END_EVENT;
}

/* Check if the next events represent an empty mapping. */
static bool check_empty_mapping(yaml_emitter_t *emitter)
{
	if (emitter->n_events - emitter->events_head < 2)
		return false;
	return emitter->events[emitter->events_head].typ == yaml_MAPPING_START_EVENT &&
	       emitter->events[emitter->events_head + 1].typ == yaml_MAPPING_END_EVENT;
}

/* Check if the next node can be expressed as a simple key. */
static bool check_simple_key(yaml_emitter_t *emitter)
{
	size_t length = 0;
	switch (emitter->events[emitter->events_head].typ) {
	case yaml_ALIAS_EVENT:
		length += emitter->anchor_data.anchor.n;
		break;
	case yaml_SCALAR_EVENT:
		if (emitter->scalar_data.multiline)
			return false;
		length += emitter->anchor_data.anchor.n + emitter->tag_data.handle.n + emitter->tag_data.suffix.n +
			  emitter->scalar_data.value.n;
		break;
	case yaml_SEQUENCE_START_EVENT:
		if (!check_empty_sequence(emitter))
			return false;
		length += emitter->anchor_data.anchor.n + emitter->tag_data.handle.n + emitter->tag_data.suffix.n;
		break;
	case yaml_MAPPING_START_EVENT:
		if (!check_empty_mapping(emitter))
			return false;
		length += emitter->anchor_data.anchor.n + emitter->tag_data.handle.n + emitter->tag_data.suffix.n;
		break;
	default:
		return false;
	}
	return length <= 128;
}

/* Determine an acceptable scalar style. */
static bool select_scalar_style(yaml_emitter_t *emitter, yaml_event_t *event)
{
	bool no_tag = emitter->tag_data.handle.n == 0 && emitter->tag_data.suffix.n == 0;
	if (no_tag && !event->implicit && !event->quoted_implicit)
		return set_emitter_error(emitter, "neither tag nor implicit flags are specified");

	int style = event->style;
	if (style == yaml_ANY_SCALAR_STYLE)
		style = yaml_PLAIN_SCALAR_STYLE;
	if (emitter->canonical)
		style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;
	if (emitter->simple_key_context && emitter->scalar_data.multiline)
		style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;

	if (style == yaml_PLAIN_SCALAR_STYLE) {
		if ((emitter->flow_level > 0 && !emitter->scalar_data.flow_plain_allowed) ||
		    (emitter->flow_level == 0 && !emitter->scalar_data.block_plain_allowed))
			style = yaml_SINGLE_QUOTED_SCALAR_STYLE;
		if (emitter->scalar_data.value.n == 0 && (emitter->flow_level > 0 || emitter->simple_key_context))
			style = yaml_SINGLE_QUOTED_SCALAR_STYLE;
		if (no_tag && !event->implicit)
			style = yaml_SINGLE_QUOTED_SCALAR_STYLE;
	}
	if (style == yaml_SINGLE_QUOTED_SCALAR_STYLE) {
		if (!emitter->scalar_data.single_quoted_allowed)
			style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;
	}
	if (style == yaml_LITERAL_SCALAR_STYLE || style == yaml_FOLDED_SCALAR_STYLE) {
		if (!emitter->scalar_data.block_allowed || emitter->flow_level > 0 || emitter->simple_key_context)
			style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;
	}

	if (no_tag && !event->quoted_implicit && style != yaml_PLAIN_SCALAR_STYLE)
		emitter->tag_data.handle = S("!");
	emitter->scalar_data.style = style;
	return true;
}

/* Write an anchor. */
static bool process_anchor(yaml_emitter_t *emitter)
{
	if (!emitter->anchor_data.anchor.p)
		return true;
	Str c = S("&");
	if (emitter->anchor_data.alias)
		c = S("*");
	if (!write_indicator(emitter, c, true, false, false))
		return false;
	return write_anchor(emitter, emitter->anchor_data.anchor);
}

/* Write a tag. */
static bool process_tag(yaml_emitter_t *emitter)
{
	if (emitter->tag_data.handle.n == 0 && emitter->tag_data.suffix.n == 0)
		return true;
	if (emitter->tag_data.handle.n > 0) {
		if (!write_tag_handle(emitter, emitter->tag_data.handle))
			return false;
		if (emitter->tag_data.suffix.n > 0) {
			if (!write_tag_content(emitter, emitter->tag_data.suffix, false))
				return false;
		}
	} else {
		/* [Go] Allocate these slices elsewhere. */
		if (!write_indicator(emitter, S("!<"), true, false, false))
			return false;
		if (!write_tag_content(emitter, emitter->tag_data.suffix, false))
			return false;
		if (!write_indicator(emitter, S(">"), false, false, false))
			return false;
	}
	return true;
}

/* Write a scalar. */
static bool process_scalar(yaml_emitter_t *emitter)
{
	switch (emitter->scalar_data.style) {
	case yaml_PLAIN_SCALAR_STYLE:
		return write_plain_scalar(emitter, emitter->scalar_data.value, !emitter->simple_key_context);
	case yaml_SINGLE_QUOTED_SCALAR_STYLE:
		return write_single_quoted_scalar(emitter, emitter->scalar_data.value, !emitter->simple_key_context);
	case yaml_DOUBLE_QUOTED_SCALAR_STYLE:
		return write_double_quoted_scalar(emitter, emitter->scalar_data.value, !emitter->simple_key_context);
	case yaml_LITERAL_SCALAR_STYLE:
		return write_literal_scalar(emitter, emitter->scalar_data.value);
	case yaml_FOLDED_SCALAR_STYLE:
		return write_folded_scalar(emitter, emitter->scalar_data.value);
	}
	fail("unknown scalar style");
}

/* Write a head comment. */
static bool process_head_comment(yaml_emitter_t *emitter)
{
	if (emitter->tail_comment.n > 0) {
		if (!write_indent(emitter))
			return false;
		if (!write_comment(emitter, emitter->tail_comment))
			return false;
		emitter->tail_comment.n = 0;
		emitter->foot_indent = emitter->indent;
		if (emitter->foot_indent < 0)
			emitter->foot_indent = 0;
	}

	if (emitter->head_comment.n == 0)
		return true;
	if (!write_indent(emitter))
		return false;
	if (!write_comment(emitter, emitter->head_comment))
		return false;
	emitter->head_comment.n = 0;
	return true;
}

/* Write an line comment. */
static bool process_line_comment_linebreak(yaml_emitter_t *emitter, bool linebreak)
{
	if (emitter->line_comment.n == 0) {
		/* The next 3 lines are needed to resolve an issue with leading newlines. See
		 * https://github.com/go-yaml/yaml/issues/755. When linebreak is set to true,
		 * put_break will be called and will add the needed newline. */
		if (linebreak && !put_break(emitter))
			return false;
		return true;
	}
	if (!emitter->whitespace) {
		if (!put(emitter, ' '))
			return false;
	}
	if (!write_comment(emitter, emitter->line_comment))
		return false;
	emitter->line_comment.n = 0;
	return true;
}

/* Write a foot comment. */
static bool process_foot_comment(yaml_emitter_t *emitter)
{
	if (emitter->foot_comment.n == 0)
		return true;
	if (!write_indent(emitter))
		return false;
	if (!write_comment(emitter, emitter->foot_comment))
		return false;
	emitter->foot_comment.n = 0;
	emitter->foot_indent = emitter->indent;
	if (emitter->foot_indent < 0)
		emitter->foot_indent = 0;
	return true;
}

/* Check if an anchor is valid. */
static bool analyze_anchor(yaml_emitter_t *emitter, Str anchor, bool alias)
{
	if (anchor.n == 0) {
		const char *problem = "anchor value must not be empty";
		if (alias)
			problem = "alias value must not be empty";
		return set_emitter_error(emitter, problem);
	}
	for (size_t i = 0; i < anchor.n;) {
		if (!is_alpha(anchor, i)) {
			const char *problem = "anchor value must contain alphanumerical characters only";
			if (alias)
				problem = "alias value must contain alphanumerical characters only";
			return set_emitter_error(emitter, problem);
		}
		int w = yaml_width(yaml_at(anchor, i));
		i += w ? (size_t)w : 1;
	}
	emitter->anchor_data.anchor = anchor;
	emitter->anchor_data.alias = alias;
	return true;
}

/* Check if a tag is valid. */
static bool analyze_tag(yaml_emitter_t *emitter, Str tag)
{
	if (tag.n == 0)
		return set_emitter_error(emitter, "tag value must not be empty");
	for (int i = 0; i < emitter->n_tag_directives; i++) {
		yaml_tag_directive_t *td = &emitter->tag_directives[i];
		if (str_has_prefix(tag, td->prefix)) {
			emitter->tag_data.handle = td->handle;
			emitter->tag_data.suffix = (Str){tag.p + td->prefix.n, tag.n - td->prefix.n};
			return true;
		}
	}
	emitter->tag_data.suffix = tag;
	return true;
}

/* Check if a scalar is valid. */
static bool analyze_scalar(yaml_emitter_t *emitter, Str value)
{
	bool block_indicators = false, flow_indicators = false, line_breaks = false, special_characters = false,
	     tab_characters = false, leading_space = false, leading_break = false, trailing_space = false,
	     trailing_break = false, break_space = false, space_break = false, preceded_by_whitespace = false,
	     followed_by_whitespace = false, previous_space = false, previous_break = false;

	emitter->scalar_data.value = value;

	if (value.n == 0) {
		emitter->scalar_data.multiline = false;
		emitter->scalar_data.flow_plain_allowed = false;
		emitter->scalar_data.block_plain_allowed = true;
		emitter->scalar_data.single_quoted_allowed = true;
		emitter->scalar_data.block_allowed = false;
		return true;
	}

	if (value.n >= 3 && ((value.p[0] == '-' && value.p[1] == '-' && value.p[2] == '-') ||
			     (value.p[0] == '.' && value.p[1] == '.' && value.p[2] == '.'))) {
		block_indicators = true;
		flow_indicators = true;
	}

	preceded_by_whitespace = true;
	for (size_t i = 0, w = 0; i < value.n; i += w) {
		w = (size_t)yaml_width(yaml_at(value, i));
		if (w == 0)
			w = 1;
		followed_by_whitespace = i + w >= value.n || is_blank(value, i + w);

		uint8_t c = yaml_at(value, i);
		if (i == 0) {
			switch (c) {
			case '#': case ',': case '[': case ']': case '{': case '}': case '&': case '*': case '!': case '|':
			case '>': case '\'': case '"': case '%': case '@': case '`':
				flow_indicators = true;
				block_indicators = true;
				break;
			case '?': case ':':
				flow_indicators = true;
				if (followed_by_whitespace)
					block_indicators = true;
				break;
			case '-':
				if (followed_by_whitespace) {
					flow_indicators = true;
					block_indicators = true;
				}
				break;
			}
		} else {
			switch (c) {
			case ',': case '?': case '[': case ']': case '{': case '}':
				flow_indicators = true;
				break;
			case ':':
				flow_indicators = true;
				if (followed_by_whitespace)
					block_indicators = true;
				break;
			case '#':
				if (preceded_by_whitespace) {
					flow_indicators = true;
					block_indicators = true;
				}
				break;
			}
		}

		if (c == '\t')
			tab_characters = true;
		else if (!is_printable(value, i) || (!is_ascii(value, i) && !emitter->unicode))
			special_characters = true;
		if (is_space(value, i)) {
			if (i == 0)
				leading_space = true;
			if (i + (size_t)yaml_width(c) == value.n)
				trailing_space = true;
			if (previous_break)
				break_space = true;
			previous_space = true;
			previous_break = false;
		} else if (is_break(value, i)) {
			line_breaks = true;
			if (i == 0)
				leading_break = true;
			if (i + (size_t)yaml_width(c) == value.n)
				trailing_break = true;
			if (previous_space)
				space_break = true;
			previous_space = false;
			previous_break = true;
		} else {
			previous_space = false;
			previous_break = false;
		}

		/* [Go]: Why 'z'? Couldn't be the end of the string as that's the loop condition. */
		preceded_by_whitespace = is_blankz(value, i);
	}

	emitter->scalar_data.multiline = line_breaks;
	emitter->scalar_data.flow_plain_allowed = true;
	emitter->scalar_data.block_plain_allowed = true;
	emitter->scalar_data.single_quoted_allowed = true;
	emitter->scalar_data.block_allowed = true;

	if (leading_space || leading_break || trailing_space || trailing_break) {
		emitter->scalar_data.flow_plain_allowed = false;
		emitter->scalar_data.block_plain_allowed = false;
	}
	if (trailing_space)
		emitter->scalar_data.block_allowed = false;
	if (break_space) {
		emitter->scalar_data.flow_plain_allowed = false;
		emitter->scalar_data.block_plain_allowed = false;
		emitter->scalar_data.single_quoted_allowed = false;
	}
	if (space_break || tab_characters || special_characters) {
		emitter->scalar_data.flow_plain_allowed = false;
		emitter->scalar_data.block_plain_allowed = false;
		emitter->scalar_data.single_quoted_allowed = false;
	}
	if (space_break || special_characters)
		emitter->scalar_data.block_allowed = false;
	if (line_breaks) {
		emitter->scalar_data.flow_plain_allowed = false;
		emitter->scalar_data.block_plain_allowed = false;
	}
	if (flow_indicators)
		emitter->scalar_data.flow_plain_allowed = false;
	if (block_indicators)
		emitter->scalar_data.block_plain_allowed = false;
	return true;
}

/* Check if the event data is valid. */
static bool analyze_event(yaml_emitter_t *emitter, yaml_event_t *event)
{
	emitter->anchor_data.anchor = (Str){0};
	emitter->tag_data.handle = (Str){0};
	emitter->tag_data.suffix = (Str){0};
	emitter->scalar_data.value = (Str){0};

	if (event->head_comment.n > 0)
		emitter->head_comment = event->head_comment;
	if (event->line_comment.n > 0)
		emitter->line_comment = event->line_comment;
	if (event->foot_comment.n > 0)
		emitter->foot_comment = event->foot_comment;
	if (event->tail_comment.n > 0)
		emitter->tail_comment = event->tail_comment;

	switch (event->typ) {
	case yaml_ALIAS_EVENT:
		if (!analyze_anchor(emitter, event->anchor, true))
			return false;
		break;
	case yaml_SCALAR_EVENT:
		if (event->anchor.n > 0) {
			if (!analyze_anchor(emitter, event->anchor, false))
				return false;
		}
		if (event->tag.n > 0 && (emitter->canonical || (!event->implicit && !event->quoted_implicit))) {
			if (!analyze_tag(emitter, event->tag))
				return false;
		}
		if (!analyze_scalar(emitter, event->value))
			return false;
		break;
	case yaml_SEQUENCE_START_EVENT:
	case yaml_MAPPING_START_EVENT:
		if (event->anchor.n > 0) {
			if (!analyze_anchor(emitter, event->anchor, false))
				return false;
		}
		if (event->tag.n > 0 && (emitter->canonical || !event->implicit)) {
			if (!analyze_tag(emitter, event->tag))
				return false;
		}
		break;
	default:
		break;
	}
	return true;
}

static bool write_indent(yaml_emitter_t *emitter)
{
	int indent = emitter->indent;
	if (indent < 0)
		indent = 0;
	if (!emitter->indention || emitter->column > indent || (emitter->column == indent && !emitter->whitespace)) {
		if (!put_break(emitter))
			return false;
	}
	if (emitter->foot_indent == indent) {
		if (!put_break(emitter))
			return false;
	}
	while (emitter->column < indent) {
		if (!put(emitter, ' '))
			return false;
	}
	emitter->whitespace = true;
	emitter->space_above = false;
	emitter->foot_indent = -1;
	return true;
}

static bool write_indicator(yaml_emitter_t *emitter, Str indicator, bool need_whitespace, bool is_whitespace, bool is_indention)
{
	if (need_whitespace && !emitter->whitespace) {
		if (!put(emitter, ' '))
			return false;
	}
	if (!write_all(emitter, indicator))
		return false;
	emitter->whitespace = is_whitespace;
	emitter->indention = emitter->indention && is_indention;
	emitter->open_ended = false;
	return true;
}

static bool write_anchor(yaml_emitter_t *emitter, Str value)
{
	if (!write_all(emitter, value))
		return false;
	emitter->whitespace = false;
	emitter->indention = false;
	return true;
}

static bool write_tag_handle(yaml_emitter_t *emitter, Str value)
{
	if (!emitter->whitespace) {
		if (!put(emitter, ' '))
			return false;
	}
	if (!write_all(emitter, value))
		return false;
	emitter->whitespace = false;
	emitter->indention = false;
	return true;
}

static bool write_tag_content(yaml_emitter_t *emitter, Str value, bool need_whitespace)
{
	if (need_whitespace && !emitter->whitespace) {
		if (!put(emitter, ' '))
			return false;
	}
	for (size_t i = 0; i < value.n;) {
		bool must_write;
		switch (yaml_at(value, i)) {
		case ';': case '/': case '?': case ':': case '@': case '&': case '=': case '+': case '$': case ',': case '_':
		case '.': case '~': case '*': case '\'': case '(': case ')': case '[': case ']':
			must_write = true;
			break;
		default:
			must_write = is_alpha(value, i);
		}
		if (must_write) {
			if (!write1(emitter, value, &i))
				return false;
		} else {
			int w = yaml_width(yaml_at(value, i));
			if (w == 0)
				w = 1;
			for (int k = 0; k < w && i < value.n; k++) {
				uint8_t octet = (uint8_t)value.p[i];
				i++;
				if (!put(emitter, '%'))
					return false;
				uint8_t c = octet >> 4;
				c = c < 10 ? (uint8_t)(c + '0') : (uint8_t)(c + 'A' - 10);
				if (!put(emitter, c))
					return false;
				c = octet & 0x0f;
				c = c < 10 ? (uint8_t)(c + '0') : (uint8_t)(c + 'A' - 10);
				if (!put(emitter, c))
					return false;
			}
		}
	}
	emitter->whitespace = false;
	emitter->indention = false;
	return true;
}

static bool write_plain_scalar(yaml_emitter_t *emitter, Str value, bool allow_breaks)
{
	if (value.n > 0 && !emitter->whitespace) {
		if (!put(emitter, ' '))
			return false;
	}

	bool spaces = false, breaks = false;
	for (size_t i = 0; i < value.n;) {
		if (is_space(value, i)) {
			if (allow_breaks && !spaces && emitter->column > emitter->best_width && !is_space(value, i + 1)) {
				if (!write_indent(emitter))
					return false;
				i += (size_t)yaml_width(yaml_at(value, i));
			} else {
				if (!write1(emitter, value, &i))
					return false;
			}
			spaces = true;
		} else if (is_break(value, i)) {
			if (!breaks && yaml_at(value, i) == '\n') {
				if (!put_break(emitter))
					return false;
			}
			if (!write_break(emitter, value, &i))
				return false;
			breaks = true;
		} else {
			if (breaks) {
				if (!write_indent(emitter))
					return false;
			}
			if (!write1(emitter, value, &i))
				return false;
			emitter->indention = false;
			spaces = false;
			breaks = false;
		}
	}

	if (value.n > 0)
		emitter->whitespace = false;
	emitter->indention = false;
	if (emitter->root_context)
		emitter->open_ended = true;
	return true;
}

static bool write_single_quoted_scalar(yaml_emitter_t *emitter, Str value, bool allow_breaks)
{
	if (!write_indicator(emitter, S("'"), true, false, false))
		return false;

	bool spaces = false, breaks = false;
	for (size_t i = 0; i < value.n;) {
		if (is_space(value, i)) {
			if (allow_breaks && !spaces && emitter->column > emitter->best_width && i > 0 && i < value.n - 1 &&
			    !is_space(value, i + 1)) {
				if (!write_indent(emitter))
					return false;
				i += (size_t)yaml_width(yaml_at(value, i));
			} else {
				if (!write1(emitter, value, &i))
					return false;
			}
			spaces = true;
		} else if (is_break(value, i)) {
			if (!breaks && yaml_at(value, i) == '\n') {
				if (!put_break(emitter))
					return false;
			}
			if (!write_break(emitter, value, &i))
				return false;
			breaks = true;
		} else {
			if (breaks) {
				if (!write_indent(emitter))
					return false;
			}
			if (yaml_at(value, i) == '\'') {
				if (!put(emitter, '\''))
					return false;
			}
			if (!write1(emitter, value, &i))
				return false;
			emitter->indention = false;
			spaces = false;
			breaks = false;
		}
	}
	if (!write_indicator(emitter, S("'"), false, false, false))
		return false;
	emitter->whitespace = false;
	emitter->indention = false;
	return true;
}

static bool write_double_quoted_scalar(yaml_emitter_t *emitter, Str value, bool allow_breaks)
{
	bool spaces = false;
	if (!write_indicator(emitter, S("\""), true, false, false))
		return false;

	for (size_t i = 0; i < value.n;) {
		uint8_t c = yaml_at(value, i);
		if (!is_printable(value, i) || (!emitter->unicode && !is_ascii(value, i)) || is_bom(value, i) || is_break(value, i) ||
		    c == '"' || c == '\\') {
			uint8_t octet = c;
			int w = 0;
			uint32_t v = 0;
			if ((octet & 0x80) == 0x00)
				w = 1, v = octet & 0x7F;
			else if ((octet & 0xE0) == 0xC0)
				w = 2, v = octet & 0x1F;
			else if ((octet & 0xF0) == 0xE0)
				w = 3, v = octet & 0x0F;
			else if ((octet & 0xF8) == 0xF0)
				w = 4, v = octet & 0x07;
			for (int k = 1; k < w; k++) {
				octet = yaml_at(value, i + (size_t)k);
				v = (v << 6) + (octet & 0x3F);
			}
			i += (size_t)(w ? w : 1);

			if (!put(emitter, '\\'))
				return false;

			bool ok;
			switch (v) {
			case 0x00: ok = put(emitter, '0'); break;
			case 0x07: ok = put(emitter, 'a'); break;
			case 0x08: ok = put(emitter, 'b'); break;
			case 0x09: ok = put(emitter, 't'); break;
			case 0x0A: ok = put(emitter, 'n'); break;
			case 0x0b: ok = put(emitter, 'v'); break;
			case 0x0c: ok = put(emitter, 'f'); break;
			case 0x0d: ok = put(emitter, 'r'); break;
			case 0x1b: ok = put(emitter, 'e'); break;
			case 0x22: ok = put(emitter, '"'); break;
			case 0x5c: ok = put(emitter, '\\'); break;
			case 0x85: ok = put(emitter, 'N'); break;
			case 0xA0: ok = put(emitter, '_'); break;
			case 0x2028: ok = put(emitter, 'L'); break;
			case 0x2029: ok = put(emitter, 'P'); break;
			default:
				if (v <= 0xFF) {
					ok = put(emitter, 'x');
					w = 2;
				} else if (v <= 0xFFFF) {
					ok = put(emitter, 'u');
					w = 4;
				} else {
					ok = put(emitter, 'U');
					w = 8;
				}
				for (int k = (w - 1) * 4; ok && k >= 0; k -= 4) {
					uint8_t digit = (uint8_t)((v >> k) & 0x0F);
					if (digit < 10)
						ok = put(emitter, (uint8_t)(digit + '0'));
					else
						ok = put(emitter, (uint8_t)(digit + 'A' - 10));
				}
			}
			if (!ok)
				return false;
			spaces = false;
		} else if (is_space(value, i)) {
			if (allow_breaks && !spaces && emitter->column > emitter->best_width && i > 0 && i < value.n - 1) {
				if (!write_indent(emitter))
					return false;
				if (is_space(value, i + 1)) {
					if (!put(emitter, '\\'))
						return false;
				}
				i += (size_t)yaml_width(yaml_at(value, i));
			} else if (!write1(emitter, value, &i)) {
				return false;
			}
			spaces = true;
		} else {
			if (!write1(emitter, value, &i))
				return false;
			spaces = false;
		}
	}
	if (!write_indicator(emitter, S("\""), false, false, false))
		return false;
	emitter->whitespace = false;
	emitter->indention = false;
	return true;
}

static bool write_block_scalar_hints(yaml_emitter_t *emitter, Str value)
{
	if (is_space(value, 0) || is_break(value, 0)) {
		char indent_hint = (char)('0' + emitter->best_indent);
		if (!write_indicator(emitter, (Str){&indent_hint, 1}, false, false, false))
			return false;
	}

	emitter->open_ended = false;

	char chomp_hint = 0;
	if (value.n == 0) {
		chomp_hint = '-';
	} else {
		size_t i = value.n - 1;
		while (i > 0 && ((uint8_t)value.p[i] & 0xC0) == 0x80)
			i--;
		if (!is_break(value, i)) {
			chomp_hint = '-';
		} else if (i == 0) {
			chomp_hint = '+';
			emitter->open_ended = true;
		} else {
			i--;
			while (i > 0 && ((uint8_t)value.p[i] & 0xC0) == 0x80)
				i--;
			if (is_break(value, i)) {
				chomp_hint = '+';
				emitter->open_ended = true;
			}
		}
	}
	if (chomp_hint != 0) {
		if (!write_indicator(emitter, (Str){&chomp_hint, 1}, false, false, false))
			return false;
	}
	return true;
}

static bool write_literal_scalar(yaml_emitter_t *emitter, Str value)
{
	if (!write_indicator(emitter, S("|"), true, false, false))
		return false;
	if (!write_block_scalar_hints(emitter, value))
		return false;
	if (!process_line_comment_linebreak(emitter, true))
		return false;
	emitter->whitespace = true;
	bool breaks = true;
	for (size_t i = 0; i < value.n;) {
		if (is_break(value, i)) {
			if (!write_break(emitter, value, &i))
				return false;
			breaks = true;
		} else {
			if (breaks) {
				if (!write_indent(emitter))
					return false;
			}
			if (!write1(emitter, value, &i))
				return false;
			emitter->indention = false;
			breaks = false;
		}
	}
	return true;
}

static bool write_folded_scalar(yaml_emitter_t *emitter, Str value)
{
	if (!write_indicator(emitter, S(">"), true, false, false))
		return false;
	if (!write_block_scalar_hints(emitter, value))
		return false;
	if (!process_line_comment_linebreak(emitter, true))
		return false;

	emitter->whitespace = true;

	bool breaks = true, leading_spaces = true;
	for (size_t i = 0; i < value.n;) {
		if (is_break(value, i)) {
			if (!breaks && !leading_spaces && yaml_at(value, i) == '\n') {
				size_t k = 0;
				while (is_break(value, k))
					k += (size_t)yaml_width(yaml_at(value, k));
				if (!is_blankz(value, k)) {
					if (!put_break(emitter))
						return false;
				}
			}
			if (!write_break(emitter, value, &i))
				return false;
			breaks = true;
		} else {
			if (breaks) {
				if (!write_indent(emitter))
					return false;
				leading_spaces = is_blank(value, i);
			}
			if (!breaks && is_space(value, i) && !is_space(value, i + 1) && emitter->column > emitter->best_width) {
				if (!write_indent(emitter))
					return false;
				i += (size_t)yaml_width(yaml_at(value, i));
			} else {
				if (!write1(emitter, value, &i))
					return false;
			}
			emitter->indention = false;
			breaks = false;
		}
	}
	return true;
}

static bool write_comment(yaml_emitter_t *emitter, Str comment)
{
	bool breaks = false, pound = false;
	for (size_t i = 0; i < comment.n;) {
		if (is_break(comment, i)) {
			if (!write_break(emitter, comment, &i))
				return false;
			breaks = true;
			pound = false;
		} else {
			if (breaks && !write_indent(emitter))
				return false;
			if (!pound) {
				if (yaml_at(comment, i) != '#' && (!put(emitter, '#') || !put(emitter, ' ')))
					return false;
				pound = true;
			}
			if (!write1(emitter, comment, &i))
				return false;
			emitter->indention = false;
			breaks = false;
		}
	}
	if (!breaks && !put_break(emitter))
		return false;

	emitter->whitespace = true;
	return true;
}
