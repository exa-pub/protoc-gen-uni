/* Generic messages: decoding, encoding and accessors driven by the schema tables. */
#include "pb.h"

#include <string.h>

PbMsg *pb_new(const PbMsgDef *def)
{
	PbMsg *m = xalloc(sizeof *m);
	m->def = def;
	m->slots = xalloc(sizeof(PbSlot) * (size_t)(def->n ? def->n : 1));
	return m;
}

static int wire_type_of(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WT_I64;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return WT_I32;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return WT_LEN;
	case PT_GROUP: return WT_SGROUP;
	default: return WT_VARINT;
	}
}

static int field_index(const PbMsgDef *def, uint32_t num)
{
	for (int i = 0; i < def->n; i++)
		if (def->f[i].num == num)
			return i;
	return -1;
}

static void slot_push(PbSlot *s, PbVal v)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 2;
		s->v = xrealloc(s->v, sizeof(PbVal) * (size_t)s->cap, sizeof(PbVal) * (size_t)cap);
		s->cap = cap;
	}
	s->v[s->n++] = v;
}

static void slot_set(PbSlot *s, const PbField *f, PbVal v)
{
	if (f->label == PL_OPT && s->n > 0)
		s->v[0] = v; /* last one wins */
	else
		slot_push(s, v);
}

/* Canonicalizes a varint for the field type, like the Go and ES runtimes do on parse. */
static uint64_t canon_varint(int type, uint64_t v)
{
	switch (type) {
	case PT_INT32: case PT_ENUM: return (uint64_t)(int64_t)(int32_t)(uint32_t)v;
	case PT_UINT32: return (uint32_t)v;
	case PT_BOOL: return v != 0;
	case PT_SINT32: {
		uint32_t x = (uint32_t)v;
		return (uint64_t)(int64_t)(int32_t)((x >> 1) ^ (0u - (x & 1)));
	}
	case PT_SINT64: return (v >> 1) ^ (0u - (v & 1));
	default: return v;
	}
}

static bool enum_known(const PbField *f, int64_t v)
{
	if (!f->enum_vals)
		return true;
	for (int i = 0; i < f->n_enum_vals; i++)
		if (f->enum_vals[i] == v)
			return true;
	return false;
}

static uint64_t get_le(const uint8_t *p, int n)
{
	uint64_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

static void put_le(Buf *b, uint64_t v, int n)
{
	uint8_t t[8];
	for (int i = 0; i < n; i++)
		t[i] = (uint8_t)(v >> (8 * i));
	buf_add(b, t, (size_t)n);
}

/* Reads one scalar of a non-length-delimited type from r. */
static bool read_scalar(PbReader *r, int type, uint64_t *out)
{
	int wt = wire_type_of(type);
	if (wt == WT_VARINT) {
		uint64_t v;
		if (!pb_read_varint(r, &v))
			return false;
		*out = canon_varint(type, v);
		return true;
	}
	int n = wt == WT_I64 ? 8 : 4;
	if (r->end - r->p < n)
		return false;
	*out = get_le(r->p, n);
	r->p += n;
	return true;
}

/* An unknown value of a closed enum: kept as an unknown varint field, like protobuf-es. */
static void add_unknown_enum(PbMsg *m, uint32_t num, uint64_t v)
{
	pb_put_tag(&m->unknown, num, WT_VARINT);
	pb_put_varint(&m->unknown, v);
}

/* depth: nesting level of m, 1 for the top-level message (protobuf-es BinaryReadContext). */
static bool decode(PbMsg *m, const uint8_t *p, size_t n, int depth)
{
	if (depth > PB_MAX_DEPTH)
		return false;
	PbReader r = {p, p + n};
	while (r.p < r.end) {
		const uint8_t *start = r.p;
		uint32_t num;
		int wt;
		if (!pb_read_tag(&r, &num, &wt))
			return false;
		int fi = field_index(m->def, num);
		const PbField *f = fi >= 0 ? &m->def->f[fi] : NULL;
		int fwt = f ? wire_type_of(f->type) : -1;
		bool packed = f && f->label != PL_OPT && wt == WT_LEN && fwt != WT_LEN;
		if (!f || (wt != fwt && !packed)) {
			/* unknown field, or known field with a mismatching wire type */
			if (wt == WT_EGROUP || !pb_skip_value(&r, num, wt, NULL, NULL, depth))
				return false;
			buf_add(&m->unknown, start, (size_t)(r.p - start));
			continue;
		}
		PbSlot *s = &m->slots[fi];
		if (packed) {
			const uint8_t *v;
			size_t vn;
			if (!pb_skip_value(&r, num, wt, &v, &vn, depth))
				return false;
			PbReader pr = {v, v + vn};
			while (pr.p < pr.end) {
				PbVal x;
				if (!read_scalar(&pr, f->type, &x.u))
					return false;
				if (f->type == PT_ENUM && !enum_known(f, (int64_t)x.u))
					add_unknown_enum(m, num, x.u);
				else
					slot_push(s, x);
			}
			continue;
		}
		if (fwt != WT_LEN) {
			PbVal x;
			if (!read_scalar(&r, f->type, &x.u))
				return false;
			if (f->type == PT_ENUM && !enum_known(f, (int64_t)x.u))
				add_unknown_enum(m, num, x.u);
			else
				slot_set(s, f, x);
			continue;
		}
		const uint8_t *v;
		size_t vn;
		if (!pb_skip_value(&r, num, wt, &v, &vn, depth))
			return false;
		if (f->type == PT_MESSAGE) {
			if (f->label == PL_OPT && s->n > 0) {
				if (!decode(s->v[0].m, v, vn, depth + 1)) /* merge */
					return false;
			} else {
				PbVal x = {.m = pb_new(f->msg)};
				if (!decode(x.m, v, vn, depth + 1))
					return false;
				slot_push(s, x);
			}
		} else {
			PbVal x;
			x.s.p = xstrndup((const char *)v, vn);
			x.s.n = vn;
			slot_set(s, f, x);
		}
	}
	return true;
}

bool pb_decode(PbMsg *m, const uint8_t *p, size_t n) { return decode(m, p, n, 1); }

static void put_scalar(Buf *b, int type, uint64_t v)
{
	switch (wire_type_of(type)) {
	case WT_VARINT:
		if (type == PT_SINT32) {
			uint32_t x = (uint32_t)v;
			pb_put_varint(b, (x << 1) ^ (0u - (x >> 31)));
		} else if (type == PT_SINT64) {
			pb_put_varint(b, (v << 1) ^ (0u - (v >> 63)));
		} else {
			pb_put_varint(b, v);
		}
		break;
	case WT_I64:
		put_le(b, v, 8);
		break;
	case WT_I32:
		put_le(b, v, 4);
		break;
	}
}

void pb_encode(const PbMsg *m, Buf *out, PbSkipFn skip, void *ctx)
{
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		if (!s->n || (skip && skip(ctx, m, f->num)))
			continue;
		if (f->label == PL_PACKED) {
			Buf tmp = {0};
			for (int j = 0; j < s->n; j++)
				put_scalar(&tmp, f->type, s->v[j].u);
			pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			continue;
		}
		for (int j = 0; j < s->n; j++) {
			const PbVal *v = &s->v[j];
			if (f->type == PT_MESSAGE) {
				Buf tmp = {0};
				pb_encode(v->m, &tmp, skip, ctx);
				pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			} else if (f->type == PT_STRING || f->type == PT_BYTES) {
				pb_put_bytes(out, f->num, v->s);
			} else {
				pb_put_tag(out, f->num, wire_type_of(f->type));
				put_scalar(out, f->type, v->u);
			}
		}
	}
	buf_add(out, m->unknown.p, m->unknown.n);
}

/* ---- accessors ---- */

const PbSlot *pb_slot(const PbMsg *m, uint32_t num)
{
	if (!m)
		return NULL;
	int i = field_index(m->def, num);
	return i >= 0 ? &m->slots[i] : NULL;
}

bool pb_has(const PbMsg *m, uint32_t num)
{
	const PbSlot *s = pb_slot(m, num);
	return s && s->n > 0;
}

int pb_count(const PbMsg *m, uint32_t num)
{
	const PbSlot *s = pb_slot(m, num);
	return s ? s->n : 0;
}

int64_t pb_int(const PbMsg *m, uint32_t num, int64_t def)
{
	const PbSlot *s = pb_slot(m, num);
	return s && s->n ? (int64_t)s->v[0].u : def;
}

int64_t pb_int_at(const PbMsg *m, uint32_t num, int i)
{
	const PbSlot *s = pb_slot(m, num);
	return s && i >= 0 && i < s->n ? (int64_t)s->v[i].u : 0;
}

Str pb_bytes_at(const PbMsg *m, uint32_t num, int i)
{
	const PbSlot *s = pb_slot(m, num);
	return s && i >= 0 && i < s->n ? s->v[i].s : (Str){"", 0};
}

Str pb_bytes(const PbMsg *m, uint32_t num) { return pb_bytes_at(m, num, 0); }
const char *pb_str(const PbMsg *m, uint32_t num) { return pb_bytes_at(m, num, 0).p; }
const char *pb_str_at(const PbMsg *m, uint32_t num, int i) { return pb_bytes_at(m, num, i).p; }

PbMsg *pb_msg_at(const PbMsg *m, uint32_t num, int i)
{
	const PbSlot *s = pb_slot(m, num);
	return s && i >= 0 && i < s->n ? s->v[i].m : NULL;
}

PbMsg *pb_msg(const PbMsg *m, uint32_t num) { return pb_msg_at(m, num, 0); }

void pb_set_bytes(PbMsg *m, uint32_t num, Str s)
{
	int i = field_index(m->def, num);
	if (i < 0)
		return;
	PbSlot *slot = &m->slots[i];
	slot->n = 0;
	if (s.p) {
		PbVal v;
		v.s.p = xstrndup(s.p, s.n);
		v.s.n = s.n;
		slot_push(slot, v);
	}
}

void pb_set_str(PbMsg *m, uint32_t num, const char *s) { pb_set_bytes(m, num, s ? str_c(s) : (Str){NULL, 0}); }
