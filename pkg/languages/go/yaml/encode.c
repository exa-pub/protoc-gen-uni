/* Copyright (c) 2011-2019 Canonical Ltd
 * Licensed under the Apache License, Version 2.0 (see LICENSE of go.yaml.in/yaml/v3). */

/* Port of go.yaml.in/yaml/v3 v3.0.4 yaml.go (Marshal of a *Node, Node) and encode.go (the
 * encoder of Node values: (*encoder).node). */
#include "internal.h"

#include "go_std.h"

#include <stdlib.h>

YamlNode *yaml_node_new(int kind)
{
	YamlNode *n = xalloc(sizeof *n);
	n->kind = kind;
	return n;
}

void yaml_node_append(YamlNode *n, YamlNode *child)
{
	if (n->n_content == n->cap_content) {
		int cap = n->cap_content ? n->cap_content * 2 : 4;
		n->content = xrealloc(n->content, sizeof *n->content * (size_t)n->cap_content, sizeof *n->content * (size_t)cap);
		n->cap_content = cap;
	}
	n->content[n->n_content++] = child;
	n->content_set = true;
}

typedef struct {
	yaml_emitter_t emitter;
	yaml_event_t event;
	int depth;
} encoder;

/* failf: the panic that Marshal recovers into its error. */
static NORETURN COLD void failf(const char *msg) { fail("yaml: %s", msg); }

static void emit(encoder *e)
{
	/* This will internally delete the e.event value. */
	if (!yaml_emitter_emit(&e->emitter, &e->event)) {
		const char *msg = e->emitter.problem;
		if (!msg || !*msg)
			msg = "unknown problem generating YAML content";
		failf(msg);
	}
	memset(&e->event, 0, sizeof e->event);
}

static bool utf8_valid_str(Str s)
{
	for (size_t i = 0; i < s.n;) {
		uint32_t r;
		int w = utf8_decode(s.p + i, s.n - i, &r);
		if (r == 0xFFFD && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

/* base64.StdEncoding with lines of 70 characters (encodeBase64). */
static Str encode_base64(Str s)
{
	static const char enc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	Buf in = {0};
	for (size_t i = 0; i < s.n; i += 3) {
		uint32_t v = (uint32_t)(uint8_t)s.p[i] << 16;
		size_t rem = s.n - i;
		if (rem > 1)
			v |= (uint32_t)(uint8_t)s.p[i + 1] << 8;
		if (rem > 2)
			v |= (uint8_t)s.p[i + 2];
		buf_addc(&in, enc[(v >> 18) & 63]);
		buf_addc(&in, enc[(v >> 12) & 63]);
		buf_addc(&in, rem > 1 ? enc[(v >> 6) & 63] : '=');
		buf_addc(&in, rem > 2 ? enc[v & 63] : '=');
	}
	const size_t line_len = 70;
	size_t lines = in.n / line_len + 1;
	Buf out = {0};
	for (size_t i = 0; i < in.n; i += line_len) {
		size_t j = i + line_len;
		if (j > in.n)
			j = in.n;
		buf_add(&out, in.p + i, j - i);
		if (lines > 1)
			buf_addc(&out, '\n');
	}
	return buf_view(&out);
}

static void emit_scalar(encoder *e, Str value, Str anchor, Str tag, int style, Str head, Str line, Str foot, Str tail)
{
	/* TODO Kill this function. Replace all initialize calls by their underlining Go literals. */
	bool implicit = tag.n == 0;
	if (!implicit)
		tag = yaml_long_tag(tag);
	e->event = (yaml_event_t){.typ = yaml_SCALAR_EVENT, .anchor = anchor, .tag = tag, .value = value,
				  .implicit = implicit, .quoted_implicit = implicit, .style = style};
	e->event.head_comment = head;
	e->event.line_comment = line;
	e->event.foot_comment = foot;
	e->event.tail_comment = tail;
	emit(e);
}

static void nilv(encoder *e)
{
	Str z = {0};
	emit_scalar(e, STR_LIT("null"), z, z, yaml_PLAIN_SCALAR_STYLE, z, z, z, z);
}

static bool node_is_zero(const YamlNode *n)
{
	return n->kind == 0 && n->style == 0 && n->tag.n == 0 && n->value.n == 0 && n->anchor.n == 0 && !n->alias && !n->content_set &&
	       n->head_comment.n == 0 && n->line_comment.n == 0 && n->foot_comment.n == 0 && n->line == 0 && n->column == 0;
}

/* Nesting deeper than this is not supported (the Go encoder recurses without a limit). */
#define MAX_DEPTH 2000

static void node(encoder *e, const YamlNode *n, Str tail)
{
	/* Zero nodes behave as nil. */
	if (n->kind == 0 && node_is_zero(n)) {
		nilv(e);
		return;
	}
	if (++e->depth > MAX_DEPTH)
		fail("unsupported by protoc-gen-uni: YAML nodes nested deeper than %d levels", MAX_DEPTH);

	/* If the tag was not explicitly requested, and dropping it won't change the implicit tag
	 * of the value, don't include it in the presentation. */
	Str tag = n->tag;
	Str stag = yaml_short_tag(tag);
	bool force_quoting = false;
	if (tag.n != 0 && (n->style & YAML_TAGGED_STYLE) == 0) {
		if (n->kind == YAML_SCALAR_NODE) {
			if (str_eqc(stag, "!!str") &&
			    (n->style & (YAML_SINGLE_QUOTED_STYLE | YAML_DOUBLE_QUOTED_STYLE | YAML_LITERAL_STYLE | YAML_FOLDED_STYLE)) != 0) {
				tag = (Str){0};
			} else {
				const char *rtag = yaml_resolve_tag(n->value);
				if (str_eqc(stag, rtag)) {
					tag = (Str){0};
				} else if (str_eqc(stag, "!!str")) {
					tag = (Str){0};
					force_quoting = true;
				}
			}
		} else {
			const char *rtag = NULL;
			switch (n->kind) {
			case YAML_MAPPING_NODE:
				rtag = "!!map";
				break;
			case YAML_SEQUENCE_NODE:
				rtag = "!!seq";
				break;
			}
			if (rtag && str_eqc(stag, rtag))
				tag = (Str){0};
		}
	}

	Str z = {0};
	switch (n->kind) {
	case YAML_DOCUMENT_NODE:
		e->event = (yaml_event_t){.typ = yaml_DOCUMENT_START_EVENT, .implicit = true};
		e->event.head_comment = n->head_comment;
		emit(e);
		for (int i = 0; i < n->n_content; i++)
			node(e, n->content[i], z);
		e->event = (yaml_event_t){.typ = yaml_DOCUMENT_END_EVENT, .implicit = true};
		e->event.foot_comment = n->foot_comment;
		emit(e);
		break;

	case YAML_SEQUENCE_NODE: {
		int style = yaml_BLOCK_SEQUENCE_STYLE;
		if (n->style & YAML_FLOW_STYLE)
			style = yaml_FLOW_SEQUENCE_STYLE;
		e->event = (yaml_event_t){.typ = yaml_SEQUENCE_START_EVENT, .anchor = n->anchor, .tag = yaml_long_tag(tag),
					  .implicit = tag.n == 0, .style = style};
		e->event.head_comment = n->head_comment;
		emit(e);
		for (int i = 0; i < n->n_content; i++)
			node(e, n->content[i], z);
		e->event = (yaml_event_t){.typ = yaml_SEQUENCE_END_EVENT};
		e->event.line_comment = n->line_comment;
		e->event.foot_comment = n->foot_comment;
		emit(e);
		break;
	}

	case YAML_MAPPING_NODE: {
		int style = yaml_BLOCK_MAPPING_STYLE;
		if (n->style & YAML_FLOW_STYLE)
			style = yaml_FLOW_MAPPING_STYLE;
		e->event = (yaml_event_t){.typ = yaml_MAPPING_START_EVENT, .anchor = n->anchor, .tag = yaml_long_tag(tag),
					  .implicit = tag.n == 0, .style = style};
		e->event.tail_comment = tail;
		e->event.head_comment = n->head_comment;
		emit(e);

		/* The tail logic below moves the foot comment of prior keys to the following key,
		 * since the value for each key may be a nested structure and the foot needs to be
		 * processed only the entirety of the value is streamed. The last tail is processed
		 * with the mapping end event. */
		Str ktail = {0};
		for (int i = 0; i + 1 < n->n_content; i += 2) {
			const YamlNode *k = n->content[i];
			Str foot = k->foot_comment;
			if (foot.n != 0) {
				YamlNode *kopy = xalloc(sizeof *kopy);
				*kopy = *k;
				kopy->foot_comment = (Str){0};
				k = kopy;
			}
			node(e, k, ktail);
			ktail = foot;

			node(e, n->content[i + 1], z);
		}

		e->event = (yaml_event_t){.typ = yaml_MAPPING_END_EVENT};
		e->event.tail_comment = ktail;
		e->event.line_comment = n->line_comment;
		e->event.foot_comment = n->foot_comment;
		emit(e);
		break;
	}

	case YAML_ALIAS_NODE:
		e->event = (yaml_event_t){.typ = yaml_ALIAS_EVENT, .anchor = n->value};
		e->event.head_comment = n->head_comment;
		e->event.line_comment = n->line_comment;
		e->event.foot_comment = n->foot_comment;
		emit(e);
		break;

	case YAML_SCALAR_NODE: {
		Str value = n->value;
		if (!utf8_valid_str(value)) {
			if (str_eqc(stag, "!!binary"))
				failf("explicitly tagged !!binary data must be base64-encoded");
			if (stag.n != 0)
				failf(xcat("cannot marshal invalid UTF-8 data as ", xstrndup(stag.p, stag.n)));
			/* It can't be encoded directly as YAML so use a binary tag and encode it as
			 * base64. */
			tag = STR_LIT("!!binary");
			value = encode_base64(value);
		}

		int style = yaml_PLAIN_SCALAR_STYLE;
		if (n->style & YAML_DOUBLE_QUOTED_STYLE)
			style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;
		else if (n->style & YAML_SINGLE_QUOTED_STYLE)
			style = yaml_SINGLE_QUOTED_SCALAR_STYLE;
		else if (n->style & YAML_LITERAL_STYLE)
			style = yaml_LITERAL_SCALAR_STYLE;
		else if (n->style & YAML_FOLDED_STYLE)
			style = yaml_FOLDED_SCALAR_STYLE;
		else if (memchr(value.p ? value.p : "", '\n', value.n))
			style = yaml_LITERAL_SCALAR_STYLE;
		else if (force_quoting)
			style = yaml_DOUBLE_QUOTED_SCALAR_STYLE;

		emit_scalar(e, value, n->anchor, tag, style, n->head_comment, n->line_comment, n->foot_comment, tail);
		break;
	}
	default:
		failf(xfmt("cannot encode node with unknown kind %d", n->kind));
	}
	e->depth--;
}

typedef struct {
	const YamlNode *in;
	Buf *out;
} MarshalCtx;

static void marshal(void *arg)
{
	MarshalCtx *c = arg;
	encoder e;
	memset(&e, 0, sizeof e);
	yaml_emitter_initialize(&e.emitter, c->out);
	e.emitter.unicode = true;
	/* init */
	e.emitter.best_indent = 4;
	e.event = (yaml_event_t){.typ = yaml_STREAM_START_EVENT, .encoding = yaml_UTF8_ENCODING};
	emit(&e);
	/* marshalDoc */
	if (c->in->kind == YAML_DOCUMENT_NODE) {
		node(&e, c->in, (Str){0});
	} else {
		e.event = (yaml_event_t){.typ = yaml_DOCUMENT_START_EVENT, .implicit = true};
		emit(&e);
		node(&e, c->in, (Str){0});
		e.event = (yaml_event_t){.typ = yaml_DOCUMENT_END_EVENT, .implicit = true};
		emit(&e);
	}
	/* finish */
	e.emitter.open_ended = false;
	e.event = (yaml_event_t){.typ = yaml_STREAM_END_EVENT};
	emit(&e);
}

const char *yaml_marshal(const YamlNode *n, Buf *out)
{
	MarshalCtx c = {n, out};
	size_t start = out->n;
	const char *err = try_run(marshal, &c);
	if (err)
		out->n = start;
	return err;
}

/* ---- Encoder (yaml.go) and the parts of encode.go and sorter.go for Go values ---- */

typedef struct {
	const YamlNode *in;
	int indent;
	Buf *out;
} EncodeCtx;

/* NewEncoder, SetIndent(indent) and one Encode (init, marshalDoc); not Close: no STREAM-END. */
static void encode_doc(void *arg)
{
	EncodeCtx *c = arg;
	encoder e;
	memset(&e, 0, sizeof e);
	yaml_emitter_initialize(&e.emitter, c->out);
	e.emitter.unicode = true;
	e.emitter.best_indent = c->indent ? c->indent : 4;
	e.event = (yaml_event_t){.typ = yaml_STREAM_START_EVENT, .encoding = yaml_UTF8_ENCODING};
	emit(&e);
	if (c->in->kind == YAML_DOCUMENT_NODE) {
		node(&e, c->in, (Str){0});
	} else {
		e.event = (yaml_event_t){.typ = yaml_DOCUMENT_START_EVENT, .implicit = true};
		emit(&e);
		node(&e, c->in, (Str){0});
		e.event = (yaml_event_t){.typ = yaml_DOCUMENT_END_EVENT, .implicit = true};
		emit(&e);
	}
}

const char *yaml_encode(const YamlNode *n, int indent, Buf *out)
{
	EncodeCtx c = {n, indent, out};
	size_t start = out->n;
	const char *err = try_run(encode_doc, &c);
	if (err)
		out->n = start;
	return err;
}

/* isBase60Float: ^[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+(?:\.[0-9_]*)?$ */
static bool is_base60_float(Str s)
{
	size_t i = 0;
	if (s.n == 0 || !(s.p[0] == '+' || s.p[0] == '-' || (s.p[0] >= '0' && s.p[0] <= '9')) || !memchr(s.p, ':', s.n))
		return false;
	if (s.p[i] == '+' || s.p[i] == '-')
		i++;
	if (i >= s.n || s.p[i] < '0' || s.p[i] > '9')
		return false;
	for (i++; i < s.n && ((s.p[i] >= '0' && s.p[i] <= '9') || s.p[i] == '_'); i++)
		;
	int groups = 0;
	while (i < s.n && s.p[i] == ':') {
		/* [0-5]?[0-9]: one or two digits, the first of two in 0-5 */
		size_t d = i + 1;
		if (d < s.n && s.p[d] >= '0' && s.p[d] <= '5' && d + 1 < s.n && s.p[d + 1] >= '0' && s.p[d + 1] <= '9')
			i = d + 2;
		else if (d < s.n && s.p[d] >= '0' && s.p[d] <= '9')
			i = d + 1;
		else
			return false;
		groups++;
	}
	if (!groups)
		return false;
	if (i < s.n && s.p[i] == '.')
		for (i++; i < s.n && ((s.p[i] >= '0' && s.p[i] <= '9') || s.p[i] == '_'); i++)
			;
	return i == s.n;
}

static bool is_old_bool(Str s)
{
	static const char *const v[] = {"y", "Y", "yes", "Yes", "YES", "on", "On", "ON",
					"n", "N", "no", "No", "NO", "off", "Off", "OFF"};
	for (size_t i = 0; i < sizeof v / sizeof *v; i++)
		if (str_eqc(s, v[i]))
			return true;
	return false;
}

int yaml_string_style(Str s)
{
	bool can_use_plain = true;
	if (!utf8_valid_str(s))
		return 0; /* !!binary: node() encodes it */
	can_use_plain = str_eqc(str_c(yaml_resolve_tag(s)), "!!str") && !(is_base60_float(s) || is_old_bool(s));
	if (memchr(s.p, '\n', s.n))
		return YAML_LITERAL_STYLE;
	return can_use_plain ? 0 : YAML_DOUBLE_QUOTED_STYLE;
}

YamlNode *yaml_node_string(Str s)
{
	YamlNode *n = yaml_node_new(YAML_SCALAR_NODE);
	n->value = s;
	n->style = yaml_string_style(s);
	return n;
}

/* keyList.Less of two string keys as rune slices ([]rune(s): invalid UTF-8 bytes are U+FFFD). */
static bool key_less(const uint32_t *ar, size_t na, const uint32_t *br, size_t nb)
{
	bool digits = false;
	for (size_t i = 0; i < na && i < nb; i++) {
		if (ar[i] == br[i]) {
			digits = go_std_is_digit((int32_t)ar[i]);
			continue;
		}
		bool al = go_std_is_letter((int32_t)ar[i]), bl = go_std_is_letter((int32_t)br[i]);
		if (al && bl)
			return ar[i] < br[i];
		if (al || bl)
			return digits ? al : bl;
		int64_t an = 0, bn = 0;
		size_t ai, bi;
		if (ar[i] == '0' || br[i] == '0') {
			for (size_t j = i; j-- > 0 && go_std_is_digit((int32_t)ar[j]);) {
				if (ar[j] != '0') {
					an = 1;
					bn = 1;
					break;
				}
			}
		}
		/* digits beyond '0'-'9' (other Unicode digits) add rune - '0', as in Go */
		for (ai = i; ai < na && go_std_is_digit((int32_t)ar[ai]); ai++)
			an = (int64_t)((uint64_t)an * 10 + (uint64_t)((int64_t)ar[ai] - '0'));
		for (bi = i; bi < nb && go_std_is_digit((int32_t)br[bi]); bi++)
			bn = (int64_t)((uint64_t)bn * 10 + (uint64_t)((int64_t)br[bi] - '0'));
		if (an != bn)
			return an < bn;
		if (ai != bi)
			return ai < bi;
		return ar[i] < br[i];
	}
	return na < nb;
}

typedef struct {
	uint32_t *r;
	size_t n;
	int index;
} SortKey;

static int cmp_key(const void *x, const void *y)
{
	const SortKey *a = x, *b = y;
	if (key_less(a->r, a->n, b->r, b->n))
		return -1;
	if (key_less(b->r, b->n, a->r, a->n))
		return 1;
	return (a->index > b->index) - (a->index < b->index);
}

void yaml_sort_keys(int *order, const Str *keys, int n)
{
	SortKey *v = xalloc(sizeof *v * (size_t)(n + 1));
	for (int k = 0; k < n; k++) {
		Str s = keys[k];
		v[k].r = xalloc(sizeof *v[k].r * (s.n + 1));
		v[k].index = k;
		for (size_t i = 0; i < s.n;)
			i += (size_t)utf8_decode(s.p + i, s.n - i, &v[k].r[v[k].n++]);
	}
	qsort(v, (size_t)n, sizeof *v, cmp_key);
	for (int k = 0; k < n; k++)
		order[k] = v[k].index;
}
