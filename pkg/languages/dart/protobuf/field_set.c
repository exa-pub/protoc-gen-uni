/* GeneratedMessage.operator == of package:protobuf 6.1.0 (lib/src/protobuf/field_set.dart
 * _equals, _equalFieldValues; unknown_field_set.dart ==; utils.dart deepEquals; BSD-3-Clause,
 * Copyright the Dart project authors) over pkg/pb messages read by dpb_merge_from_buffer, and a
 * hash consistent with it (not Dart's hashCode). */
#include "dart_protobuf.h"

#include <stdlib.h>
#include <string.h>

/* The unknown fields of a message as UnknownFieldSet holds them: per field number, the varints,
 * fixed32s, fixed64s, length-delimited values and groups in wire order. Entries are sorted by
 * (number, kind, wire order), so two sets are equal when their entries are. */
typedef struct DpbUnknown DpbUnknown;
typedef struct {
	uint32_t num;
	int kind, idx;
	uint64_t v;
	Str s;
	DpbUnknown *group;
} DpbUEntry;
struct DpbUnknown {
	int n, cap;
	DpbUEntry *e;
};

/* A varint as the Dart reader reads it (validated by dpb_merge_from_buffer): 10 bytes at most,
 * modulo 2^64. */
static bool dpb_uvarint(PbReader *r, uint64_t *v)
{
	uint64_t x = 0;
	for (int i = 0; i < 10 && r->p < r->end; i++) {
		uint8_t b = *r->p++;
		x |= (uint64_t)(b & 0x7f) << (7 * i);
		if (!(b & 0x80)) {
			*v = x;
			return true;
		}
	}
	return false;
}

static int dpb_uentry_cmp(const void *a, const void *b)
{
	const DpbUEntry *x = a, *y = b;
	if (x->num != y->num)
		return x->num < y->num ? -1 : 1;
	if (x->kind != y->kind)
		return x->kind < y->kind ? -1 : 1;
	return (x->idx > y->idx) - (x->idx < y->idx);
}

/* Parses unknown fields up to the end of r, or up to the end-group tag of a group. */
static DpbUnknown *dpb_unknown_parse(PbReader *r)
{
	DpbUnknown *u = xalloc(sizeof *u);
	while (r->p < r->end) {
		uint64_t tag, v = 0;
		if (!dpb_uvarint(r, &tag))
			break;
		DpbUEntry e = {.num = (uint32_t)tag >> 3, .kind = (int)(tag & 7), .idx = u->n};
		if (e.kind == WT_EGROUP)
			break;
		if (e.kind == WT_VARINT) {
			if (!dpb_uvarint(r, &e.v))
				break;
		} else if (e.kind == WT_I64 || e.kind == WT_I32) {
			int n = e.kind == WT_I64 ? 8 : 4;
			if (r->end - r->p < n)
				break;
			for (int i = n - 1; i >= 0; i--)
				e.v = e.v << 8 | r->p[i];
			r->p += n;
		} else if (e.kind == WT_LEN) {
			if (!dpb_uvarint(r, &v) || (uint32_t)v > (uint64_t)(r->end - r->p))
				break;
			e.s = (Str){(const char *)r->p, (uint32_t)v};
			r->p += (uint32_t)v;
		} else if (e.kind == WT_SGROUP) {
			e.group = dpb_unknown_parse(r);
		} else {
			break;
		}
		if (u->n == u->cap) {
			int cap = u->cap ? u->cap * 2 : 4;
			u->e = xrealloc(u->e, sizeof *u->e * (size_t)u->cap, sizeof *u->e * (size_t)cap);
			u->cap = cap;
		}
		u->e[u->n++] = e;
	}
	if (u->n > 1)
		qsort(u->e, (size_t)u->n, sizeof *u->e, dpb_uentry_cmp);
	return u;
}

static DpbUnknown *dpb_unknown_of(const PbMsg *m)
{
	PbReader r = {(const uint8_t *)m->unknown.p, (const uint8_t *)m->unknown.p + m->unknown.n};
	return dpb_unknown_parse(&r);
}

static bool dpb_unknown_equals(const DpbUnknown *a, const DpbUnknown *b)
{
	if (a->n != b->n)
		return false;
	for (int i = 0; i < a->n; i++) {
		const DpbUEntry *x = &a->e[i], *y = &b->e[i];
		if (x->num != y->num || x->kind != y->kind || x->v != y->v || !str_equal(x->s, y->s))
			return false;
		if (x->group && !dpb_unknown_equals(x->group, y->group))
			return false;
	}
	return true;
}

static double dpb_double(int type, uint64_t bits)
{
	if (type == PT_FLOAT) {
		uint32_t b = (uint32_t)bits;
		float f;
		memcpy(&f, &b, sizeof f);
		return f;
	}
	double d;
	memcpy(&d, &bits, sizeof d);
	return d;
}

static bool dpb_value_equals(const PbField *f, PbVal a, PbVal b)
{
	switch (f->type) {
	case PT_MESSAGE: case PT_GROUP: return dpb_equals(a.m, b.m);
	case PT_STRING: case PT_BYTES: return str_equal(a.s, b.s);
	case PT_DOUBLE: case PT_FLOAT: return dpb_double(f->type, a.u) == dpb_double(f->type, b.u);
	default: return a.u == b.u;
	}
}

bool dpb_equals(const PbMsg *a, const PbMsg *b)
{
	if (a == b)
		return true;
	for (int i = 0; i < a->def->n; i++) {
		const PbSlot *x = &a->slots[i], *y = &b->slots[i];
		if (x->n != y->n)
			return false; /* an unset field differs from a set one, an empty list is unset */
		for (int j = 0; j < x->n; j++)
			if (!dpb_value_equals(&a->def->f[i], x->v[j], y->v[j]))
				return false;
	}
	if (!a->unknown.n || !b->unknown.n)
		return !a->unknown.n && !b->unknown.n;
	return dpb_unknown_equals(dpb_unknown_of(a), dpb_unknown_of(b));
}

static uint64_t dpb_mix(uint64_t h, uint64_t v)
{
	h ^= v + 0x9e3779b97f4a7c15u + (h << 6) + (h >> 2);
	h ^= h >> 31;
	h *= 0xbf58476d1ce4e5b9u;
	return h ^ (h >> 29);
}

static uint64_t dpb_hash_bytes(Str s)
{
	uint64_t h = 0xcbf29ce484222325u;
	for (size_t i = 0; i < s.n; i++)
		h = (h ^ (uint8_t)s.p[i]) * 0x100000001b3u;
	return dpb_mix(h, s.n);
}

static uint64_t dpb_unknown_hash(const DpbUnknown *u)
{
	uint64_t h = (uint64_t)u->n;
	for (int i = 0; i < u->n; i++) {
		const DpbUEntry *e = &u->e[i];
		h = dpb_mix(h, (uint64_t)e->num << 3 | (uint64_t)e->kind);
		h = dpb_mix(h, e->group ? dpb_unknown_hash(e->group) : e->kind == WT_LEN ? dpb_hash_bytes(e->s) : e->v);
	}
	return h;
}

uint64_t dpb_hash(const PbMsg *m, bool *self_only)
{
	uint64_t h = 0;
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		h = dpb_mix(h, (uint64_t)i << 32 | (uint32_t)s->n);
		for (int j = 0; j < s->n; j++) {
			uint64_t v = s->v[j].u;
			if (f->type == PT_MESSAGE || f->type == PT_GROUP) {
				v = dpb_hash(s->v[j].m, self_only);
			} else if (f->type == PT_STRING || f->type == PT_BYTES) {
				v = dpb_hash_bytes(s->v[j].s);
			} else if (f->type == PT_DOUBLE || f->type == PT_FLOAT) {
				double d = dpb_double(f->type, v);
				if (d != d)
					*self_only = true; /* NaN != NaN: equal only to itself */
				v = d == 0 ? 0 : v;
			}
			h = dpb_mix(h, v);
		}
	}
	if (m->unknown.n)
		h = dpb_mix(h, dpb_unknown_hash(dpb_unknown_of(m)));
	return h;
}
