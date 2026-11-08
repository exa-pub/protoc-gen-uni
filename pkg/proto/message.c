// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Generic operations on messages of any PbMsgDef, with the semantics of the protobuf runtimes
 * (Message::MergeFrom, CopyFrom, Clear, the generated setters): what the model needs for options
 * and features messages. Parsing and serialization stay with the library (each runtime has its
 * own rules for unknown enum values, wire type mismatches and field order). */
#include "proto.h"

int proto_msg_field_index(const PbMsgDef *def, uint32_t num)
{
	int lo = 0, hi = def->n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (def->f[mid].num == num)
			return mid;
		if (def->f[mid].num < num)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

void proto_slot_push(PbSlot *s, PbVal v)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 2;
		s->v = xrealloc(s->v, sizeof(PbVal) * (size_t)s->cap, sizeof(PbVal) * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = v;
}

bool proto_msg_is_empty(const PbMsg *m)
{
	if (!m)
		return true;
	if (m->unknown.n)
		return false;
	for (int i = 0; i < m->def->n; i++)
		if (m->slots[i].n)
			return false;
	return true;
}

PbMsg *proto_msg_clone(const PbMsg *m)
{
	if (!m)
		return NULL;
	PbMsg *c = pb_new(m->def);
	for (int i = 0; i < m->def->n; i++) {
		const PbSlot *s = &m->slots[i];
		for (int j = 0; j < s->n; j++) {
			PbVal v = s->v[j];
			int t = m->def->f[i].type;
			if (t == PT_MESSAGE || t == PT_GROUP)
				v.m = proto_msg_clone(v.m);
			proto_slot_push(&c->slots[i], v);
		}
	}
	buf_add(&c->unknown, m->unknown.p, m->unknown.n);
	return c;
}

void proto_msg_merge(PbMsg *dst, const PbMsg *src)
{
	if (!src)
		return;
	for (int i = 0; i < src->def->n; i++) {
		const PbField *f = &src->def->f[i];
		const PbSlot *s = &src->slots[i];
		PbSlot *d = &dst->slots[i];
		bool msg = f->type == PT_MESSAGE || f->type == PT_GROUP;
		if (!s->n)
			continue;
		if (f->label != PL_OPT) {
			for (int j = 0; j < s->n; j++) {
				PbVal v = s->v[j];
				if (msg)
					v.m = proto_msg_clone(v.m);
				proto_slot_push(d, v);
			}
		} else if (msg) {
			if (d->n)
				proto_msg_merge(d->v[0].m, s->v[0].m);
			else
				proto_slot_push(d, (PbVal){.m = proto_msg_clone(s->v[0].m)});
		} else {
			if (d->n)
				d->v[0] = s->v[0];
			else
				proto_slot_push(d, s->v[0]);
		}
	}
	buf_add(&dst->unknown, src->unknown.p, src->unknown.n);
}

void proto_msg_clear(PbMsg *m, uint32_t num)
{
	int i = proto_msg_field_index(m->def, num);
	if (i >= 0)
		m->slots[i].n = 0;
}

static PbSlot *slot(PbMsg *m, uint32_t num, const PbField **f)
{
	int i = proto_msg_field_index(m->def, num);
	if (i < 0)
		fail("proto: no field %d in message", (int)num);
	if (f)
		*f = &m->def->f[i];
	return &m->slots[i];
}

void proto_msg_set_u(PbMsg *m, uint32_t num, uint64_t v)
{
	PbSlot *s = slot(m, num, NULL);
	s->n = 0;
	proto_slot_push(s, (PbVal){.u = v});
}

void proto_msg_add_u(PbMsg *m, uint32_t num, uint64_t v) { proto_slot_push(slot(m, num, NULL), (PbVal){.u = v}); }

void proto_msg_set_s(PbMsg *m, uint32_t num, Str v)
{
	PbSlot *s = slot(m, num, NULL);
	s->n = 0;
	proto_slot_push(s, (PbVal){.s = v});
}

void proto_msg_set_cs(PbMsg *m, uint32_t num, const char *v) { proto_msg_set_s(m, num, str_c(v)); }

void proto_msg_add_s(PbMsg *m, uint32_t num, Str v) { proto_slot_push(slot(m, num, NULL), (PbVal){.s = v}); }

void proto_msg_set_msg(PbMsg *m, uint32_t num, PbMsg *v)
{
	PbSlot *s = slot(m, num, NULL);
	s->n = 0;
	proto_slot_push(s, (PbVal){.m = v});
}

PbMsg *proto_msg_mutable(PbMsg *m, uint32_t num)
{
	const PbField *f;
	PbSlot *s = slot(m, num, &f);
	if (!s->n)
		proto_slot_push(s, (PbVal){.m = pb_new(f->msg)});
	return s->v[0].m;
}

PbMsg *proto_msg_add(PbMsg *m, uint32_t num)
{
	const PbField *f;
	PbSlot *s = slot(m, num, &f);
	PbMsg *c = pb_new(f->msg);
	proto_slot_push(s, (PbVal){.m = c});
	return c;
}
