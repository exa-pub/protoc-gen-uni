/* Protocol Buffers - Google's data interchange format
 * Copyright 2008 Google Inc.  All rights reserved.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://developers.google.com/open-source/licenses/bsd */

/* The serialization of messages by protobuf-java 3.25.8: GeneratedMessageV3.writeTo (known fields
 * and registered extensions by number, then the UnknownFieldSet, which is a TreeMap by field
 * number: per number varints, fixed32, fixed64, length-delimited values, then groups, each group
 * sorted again; UnknownFieldSet.java), and Builder.mergeFrom. */
#include "protobuf_java.h"

#include <stdlib.h>
#include <string.h>

/* ---- serialization (GeneratedMessageV3.writeTo, UnknownFieldSet.writeTo) ---- */

typedef struct {
	uint32_t num;
	int wt, rank; /* rank: varint, fixed32, fixed64, length-delimited, group */
	uint64_t v;
	const uint8_t *val; /* payload of length-delimited values and groups, raw bytes of fixed ones */
	size_t vlen;
	int pos;
} JUnknown;

static int junknown_cmp(const JUnknown *a, const JUnknown *b)
{
	if (a->num != b->num)
		return a->num < b->num ? -1 : 1;
	if (a->rank != b->rank)
		return a->rank < b->rank ? -1 : 1;
	return a->pos < b->pos ? -1 : a->pos > b->pos;
}

static int junknown_qcmp(const void *a, const void *b)
{
	return junknown_cmp(a, b);
}

/* Sorts and writes the fields of an unknown field set given as wire bytes (already validated by
 * the parser). */
static void jpb_write_unknown(const uint8_t *p, size_t n, Buf *out, int depth)
{
	PbReader r = {p, p + n};
	JUnknown *fs = NULL;
	int nf = 0, cap = 0;
	while (r.p < r.end) {
		const uint8_t *start = r.p;
		uint32_t num;
		int wt;
		if (!pb_read_tag(&r, &num, &wt))
			break;
		const uint8_t *after_tag = r.p;
		JUnknown u = {num, wt, 0, 0, NULL, 0, nf};
		if (wt == WT_VARINT) {
			if (!pb_read_varint(&r, &u.v))
				break;
		} else {
			const uint8_t *val = NULL;
			size_t vlen = 0;
			if (!pb_skip_value(&r, num, wt, &val, &vlen, depth))
				break;
			if (wt == WT_LEN) {
				u.val = val, u.vlen = vlen;
			} else if (wt == WT_SGROUP) {
				/* content: between the start tag and the end tag */
				Buf endtag = {0};
				pb_put_tag(&endtag, num, WT_EGROUP);
				u.val = after_tag;
				u.vlen = (size_t)(r.p - after_tag) - endtag.n;
			} else {
				u.val = after_tag;
				u.vlen = (size_t)(r.p - after_tag);
			}
		}
		(void)start;
		u.rank = wt == WT_VARINT ? 0 : wt == WT_I32 ? 1 : wt == WT_I64 ? 2 : wt == WT_LEN ? 3 : 4;
		if (nf == cap) {
			int nc = cap ? cap * 2 : 8;
			fs = xrealloc(fs, sizeof *fs * (size_t)cap, sizeof *fs * (size_t)nc);
			cap = nc;
		}
		fs[nf++] = u;
	}
	/* UnknownFieldSet keeps fields in a TreeMap by number; pos makes the order total (stable) */
	if (nf > 1)
		qsort(fs, (size_t)nf, sizeof *fs, junknown_qcmp);
	for (int i = 0; i < nf; i++) {
		JUnknown *u = &fs[i];
		switch (u->wt) {
		case WT_VARINT:
			pb_put_tag(out, u->num, WT_VARINT);
			pb_put_varint(out, u->v);
			break;
		case WT_LEN:
			pb_put_bytes(out, u->num, (Str){(const char *)u->val, u->vlen});
			break;
		case WT_SGROUP:
			pb_put_tag(out, u->num, WT_SGROUP);
			jpb_write_unknown(u->val, u->vlen, out, depth + 1);
			pb_put_tag(out, u->num, WT_EGROUP);
			break;
		default:
			pb_put_tag(out, u->num, u->wt);
			buf_add(out, u->val, u->vlen);
			break;
		}
	}
}

static int jpb_wire_type(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WT_I64;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return WT_I32;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return WT_LEN;
	default: return WT_VARINT;
	}
}

static void jpb_put_scalar(Buf *b, int type, uint64_t v)
{
	switch (jpb_wire_type(type)) {
	case WT_I64:
		for (int i = 0; i < 8; i++)
			buf_addc(b, (char)(v >> (8 * i)));
		break;
	case WT_I32:
		for (int i = 0; i < 4; i++)
			buf_addc(b, (char)(v >> (8 * i)));
		break;
	default:
		pb_put_varint(b, v); /* the descriptor.proto schemas use int32, int64, uint64, bool and enums only */
		break;
	}
}

/* Serializes m like protobuf-java. */
void jpb_encode(const PbMsg *m, Buf *out)
{
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		if (f->label == PL_PACKED) {
			Buf tmp = {0};
			for (int j = 0; j < s->n; j++)
				jpb_put_scalar(&tmp, f->type, s->v[j].u);
			pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			continue;
		}
		for (int j = 0; j < s->n; j++) {
			const PbVal *v = &s->v[j];
			if (f->type == PT_MESSAGE) {
				Buf tmp = {0};
				jpb_encode(v->m, &tmp);
				pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			} else if (f->type == PT_STRING || f->type == PT_BYTES) {
				pb_put_bytes(out, f->num, v->s);
			} else {
				pb_put_tag(out, f->num, jpb_wire_type(f->type));
				jpb_put_scalar(out, f->type, v->u);
			}
		}
	}
	if (m->unknown.n)
		jpb_write_unknown((const uint8_t *)m->unknown.p, m->unknown.n, out, 0);
}

/* ---- helpers for the generic messages ---- */

/* A shallow copy of m (slots copied, values shared): for clearing or replacing fields. */
PbMsg *jpb_copy(const PbMsg *m)
{
	PbMsg *c = pb_new(m->def);
	for (int i = 0; i < m->def->n; i++) {
		const PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		c->slots[i].v = xalloc(sizeof(PbVal) * (size_t)s->n);
		memcpy(c->slots[i].v, s->v, sizeof(PbVal) * (size_t)s->n);
		c->slots[i].n = c->slots[i].cap = s->n;
	}
	buf_add(&c->unknown, m->unknown.p, m->unknown.n);
	return c;
}

/* Replaces the values of a repeated message field. */
void jpb_set_msgs(PbMsg *m, uint32_t num, PbMsg **msgs, int n)
{
	int i = 0;
	while (i < m->def->n && m->def->f[i].num != num)
		i++;
	if (i == m->def->n)
		return;
	PbSlot *slot = &m->slots[i];
	slot->v = n ? xalloc(sizeof(PbVal) * (size_t)n) : NULL;
	for (int j = 0; j < n; j++)
		slot->v[j].m = msgs[j];
	slot->n = slot->cap = n;
}

void jpb_clear(PbMsg *m, uint32_t num)
{
	for (int i = 0; i < m->def->n; i++)
		if (m->def->f[i].num == num)
			m->slots[i].n = 0;
}

/* A message parsed from the concatenation of the encodings of a and b: Java's
 * a.toBuilder().mergeFrom(b) (either may be NULL). */
PbMsg *jpb_merge(const PbMsgDef *def, const PbMsg *a, const PbMsg *b)
{
	Buf tmp = {0};
	if (a)
		jpb_encode(a, &tmp);
	if (b)
		jpb_encode(b, &tmp);
	PbMsg *m = pb_new(def);
	pb_decode(m, (const uint8_t *)tmp.p, tmp.n);
	return m;
}
