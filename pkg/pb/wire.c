/* Wire format primitives. */
#include "pb.h"

bool pb_read_varint(PbReader *r, uint64_t *v)
{
	uint64_t x = 0;
	for (int i = 0; i < 10; i++) {
		if (r->p >= r->end)
			return false;
		uint8_t b = *r->p++;
		if (i == 9 && b > 1)
			return false; /* overflow, like protowire.ConsumeVarint */
		x |= (uint64_t)(b & 0x7f) << (7 * i);
		if (!(b & 0x80)) {
			*v = x;
			return true;
		}
	}
	return false;
}

bool pb_read_tag(PbReader *r, uint32_t *num, int *wt)
{
	uint64_t v;
	if (!pb_read_varint(r, &v))
		return false;
	uint64_t n = v >> 3;
	if (n == 0 || n > 0x1fffffff)
		return false;
	*num = (uint32_t)n;
	*wt = (int)(v & 7);
	return true;
}

static bool skip_n(PbReader *r, size_t n)
{
	if ((size_t)(r->end - r->p) < n)
		return false;
	r->p += n;
	return true;
}

bool pb_skip_value(PbReader *r, uint32_t num, int wt, const uint8_t **val, size_t *vlen, int depth)
{
	uint64_t v;
	switch (wt) {
	case WT_VARINT:
		return pb_read_varint(r, &v);
	case WT_I64:
		return skip_n(r, 8);
	case WT_I32:
		return skip_n(r, 4);
	case WT_LEN:
		if (!pb_read_varint(r, &v) || v > (uint64_t)(r->end - r->p))
			return false;
		if (val)
			*val = r->p, *vlen = (size_t)v;
		r->p += v;
		return true;
	case WT_SGROUP:
		if (depth >= PB_MAX_DEPTH)
			return false;
		for (;;) {
			uint32_t n2;
			int wt2;
			if (!pb_read_tag(r, &n2, &wt2))
				return false;
			if (wt2 == WT_EGROUP)
				return n2 == num;
			if (!pb_skip_value(r, n2, wt2, NULL, NULL, depth + 1))
				return false;
		}
	default:
		return false;
	}
}

bool pb_next_field(PbReader *r, PbRawField *f)
{
	if (r->p >= r->end || !pb_read_tag(r, &f->num, &f->wt))
		return false;
	f->val = NULL, f->vlen = 0, f->v = 0;
	if (f->wt == WT_VARINT)
		return pb_read_varint(r, &f->v);
	return pb_skip_value(r, f->num, f->wt, &f->val, &f->vlen, 0);
}

PbReader pb_unknown_fields(const PbMsg *m)
{
	if (!m || !m->unknown.n)
		return (PbReader){NULL, NULL};
	return (PbReader){(const uint8_t *)m->unknown.p, (const uint8_t *)m->unknown.p + m->unknown.n};
}

void pb_put_varint(Buf *b, uint64_t v)
{
	uint8_t t[10];
	int n = 0;
	while (v >= 0x80) {
		t[n++] = (uint8_t)(v | 0x80);
		v >>= 7;
	}
	t[n++] = (uint8_t)v;
	buf_add(b, t, (size_t)n);
}

void pb_put_tag(Buf *b, uint32_t num, int wt) { pb_put_varint(b, (uint64_t)num << 3 | (uint64_t)wt); }

void pb_put_bytes(Buf *b, uint32_t num, Str s)
{
	pb_put_tag(b, num, WT_LEN);
	pb_put_varint(b, s.n);
	buf_add(b, s.p, s.n);
}
