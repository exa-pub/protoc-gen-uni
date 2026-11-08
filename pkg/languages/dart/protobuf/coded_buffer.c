/* GeneratedMessage.writeToBuffer of package:protobuf (lib/src/protobuf/coded_buffer.dart
 * _writeToCodedBufferWriter, unknown_field_set.dart; BSD-3-Clause, Copyright the Dart project
 * authors) over pkg/pb messages. */
#include "dart_protobuf.h"

#include <string.h>

static int dpb_wire_type(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WT_I64;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return WT_I32;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return WT_LEN;
	default: return WT_VARINT;
	}
}

static void dpb_put_le(Buf *b, uint64_t v, int n)
{
	for (int i = 0; i < n; i++)
		buf_addc(b, (char)(v >> (8 * i)));
}

static void dpb_put_scalar(Buf *b, int type, uint64_t v)
{
	switch (dpb_wire_type(type)) {
	case WT_I64:
		dpb_put_le(b, v, 8);
		break;
	case WT_I32:
		dpb_put_le(b, v, 4);
		break;
	default:
		if (type == PT_SINT32) {
			uint32_t x = (uint32_t)v;
			pb_put_varint(b, (x << 1) ^ (0u - (x >> 31)));
		} else if (type == PT_SINT64) {
			pb_put_varint(b, (v << 1) ^ (0u - (v >> 63)));
		} else {
			pb_put_varint(b, v);
		}
	}
}

/* One value of an unknown field. */
typedef struct {
	int wt;
	uint64_t v;            /* varint, fixed32, fixed64 */
	const uint8_t *p;      /* length-delimited payload or group content */
	size_t n;
} DpbUnknown;

typedef struct {
	uint32_t num;
	DpbUnknown *v;
	int nv, cap;
} DpbUnknownField;

/* UnknownFieldSet.writeToCodedBufferWriter of the unknown fields in p[0:n]: fields in the order of
 * the first occurrence of their tag, per field varints, fixed32s, fixed64s, length-delimited
 * values and groups (UnknownFieldSetField.writeTo). Malformed bytes are copied as they are (they
 * cannot occur: pkg/pb kept them while parsing). */
static void dpb_write_unknown(const uint8_t *p, size_t n, Buf *out, int depth)
{
	Map index = {0};
	DpbUnknownField *fields = NULL;
	int nf = 0, capf = 0;
	PbReader r = {p, p + n};
	while (r.p < r.end) {
		const uint8_t *start = r.p;
		uint32_t num;
		int wt;
		if (!pb_read_tag(&r, &num, &wt)) {
			buf_add(out, start, (size_t)(r.end - start));
			break;
		}
		DpbUnknown u = {.wt = wt};
		const uint8_t *vstart = r.p;
		bool ok;
		if (wt == WT_VARINT) {
			ok = pb_read_varint(&r, &u.v);
		} else {
			ok = pb_skip_value(&r, num, wt, &u.p, &u.n, depth);
			if (ok && wt == WT_I64)
				for (int i = 0; i < 8; i++)
					u.v |= (uint64_t)vstart[i] << (8 * i);
			if (ok && wt == WT_I32)
				for (int i = 0; i < 4; i++)
					u.v |= (uint64_t)vstart[i] << (8 * i);
			if (ok && wt == WT_SGROUP) {
				/* the content without the end tag */
				Buf tag = {0};
				pb_put_tag(&tag, num, WT_EGROUP);
				u.p = vstart;
				u.n = (size_t)(r.p - vstart) - tag.n;
			}
		}
		if (!ok) {
			buf_add(out, start, (size_t)(r.end - start));
			break;
		}
		char key[16];
		int kn = 0;
		for (uint32_t x = num; kn == 0 || x; x /= 10)
			key[kn++] = (char)('0' + x % 10);
		key[kn] = 0;
		intptr_t idx = (intptr_t)map_get(&index, key);
		if (!idx) {
			if (nf == capf) {
				int cap = capf ? capf * 2 : 8;
				fields = xrealloc(fields, (size_t)capf * sizeof *fields, (size_t)cap * sizeof *fields);
				capf = cap;
			}
			fields[nf] = (DpbUnknownField){.num = num};
			idx = ++nf;
			map_put(&index, xstrdup(key), (void *)idx);
		}
		DpbUnknownField *f = &fields[idx - 1];
		if (f->nv == f->cap) {
			int cap = f->cap ? f->cap * 2 : 4;
			f->v = xrealloc(f->v, (size_t)f->cap * sizeof *f->v, (size_t)cap * sizeof *f->v);
			f->cap = cap;
		}
		f->v[f->nv++] = u;
	}
	static const int order[] = {WT_VARINT, WT_I32, WT_I64, WT_LEN, WT_SGROUP};
	for (int i = 0; i < nf; i++) {
		DpbUnknownField *f = &fields[i];
		for (size_t k = 0; k < sizeof order / sizeof *order; k++)
			for (int j = 0; j < f->nv; j++) {
				DpbUnknown *u = &f->v[j];
				if (u->wt != order[k])
					continue;
				pb_put_tag(out, f->num, u->wt);
				switch (u->wt) {
				case WT_VARINT: pb_put_varint(out, u->v); break;
				case WT_I32: dpb_put_le(out, u->v, 4); break;
				case WT_I64: dpb_put_le(out, u->v, 8); break;
				case WT_LEN:
					pb_put_varint(out, u->n);
					buf_add(out, u->p, u->n);
					break;
				default:
					dpb_write_unknown(u->p, u->n, out, depth + 1);
					pb_put_tag(out, f->num, WT_EGROUP);
				}
			}
	}
}

void dpb_write(const PbMsg *m, Buf *out)
{
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		if (f->label == PL_PACKED) {
			Buf tmp = {0};
			for (int j = 0; j < s->n; j++)
				dpb_put_scalar(&tmp, f->type, s->v[j].u);
			pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			continue;
		}
		for (int j = 0; j < s->n; j++) {
			const PbVal *v = &s->v[j];
			if (f->type == PT_MESSAGE) {
				Buf tmp = {0};
				dpb_write(v->m, &tmp);
				pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			} else if (f->type == PT_STRING || f->type == PT_BYTES) {
				pb_put_bytes(out, f->num, v->s);
			} else {
				pb_put_tag(out, f->num, dpb_wire_type(f->type));
				dpb_put_scalar(out, f->type, v->u);
			}
		}
	}
	if (m->unknown.n)
		dpb_write_unknown((const uint8_t *)m->unknown.p, m->unknown.n, out, 0);
}
