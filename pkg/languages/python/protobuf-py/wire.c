/* Copyright (c) 2025-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Binary wire format of protobuf-py v0.7.0: _wire/_binary_reader.py, _wire/_binary_writer.py,
 * _from_binary.py, _to_binary.py and the required-field check of _validate.py, over pkg/pb
 * messages with the schemas of schema.c.
 *
 * protobuf-py reads with one reader over the whole input: a length-delimited message ends when
 * the reader reaches its end, so a field that overruns it is read from the enclosing data. Unknown
 * fields are stored per number as the tag (re-encoded) and the raw value; m->unknown holds them
 * as records [number:4][length:4][bytes]. */
#include "protobuf_py.h"

#include <string.h>

typedef struct {
	const uint8_t *p;
	size_t n, off;
} Reader;

static const char EOF_VARINT[] = "EOFError: unexpected end of buffer while reading varint";

/* BinaryReader.varint(limit, last_byte_limit) */
static uint64_t rd_varint(Reader *r, int limit, unsigned last_byte_limit)
{
	uint64_t v = 0;
	for (int i = 0; i < limit; i++) {
		if (r->off >= r->n)
			fail("%s", EOF_VARINT);
		uint8_t b = r->p[r->off++];
		if (i < 10)
			v |= (uint64_t)(b & 0x7f) << (7 * i);
		if (b < 0x80) {
			if (i == limit - 1 && b > last_byte_limit)
				fail("ValueError: invalid varint");
			return v;
		}
	}
	fail("ValueError: invalid varint");
}

static uint64_t rd_varint64(Reader *r) { return rd_varint(r, 10, 1); }

/* BinaryReader.read */
static const uint8_t *rd_read(Reader *r, uint64_t n)
{
	if (n > r->n - r->off)
		fail("EOFError: unexpected end of buffer");
	const uint8_t *p = r->p + r->off;
	r->off += (size_t)n;
	return p;
}

/* BinaryReader.tag (Tag.__init__) */
static void rd_tag(Reader *r, uint32_t *num, int *wt)
{
	uint64_t key = rd_varint(r, 5, 0x0f);
	*wt = (int)(key & 7);
	if (*wt > 5)
		fail("ValueError: %d is not a valid WireType", *wt);
	*num = (uint32_t)(key >> 3);
	if (*num == 0)
		fail("ValueError: invalid tag with field number 0");
}

/* BinaryReader.skip: returns the skipped bytes. */
static Str rd_skip(Reader *r, int wt, int depth, uint32_t num)
{
	if (depth > PBPY_DEPTH_LIMIT)
		fail("RecursionError: exceeded maximum recursion depth %d while skipping field %lld", PBPY_DEPTH_LIMIT, (long long)num);
	size_t start = r->off;
	switch (wt) {
	case WT_VARINT:
		rd_varint64(r);
		break;
	case WT_I64:
		rd_read(r, 8);
		break;
	case WT_LEN:
		rd_read(r, rd_varint64(r));
		break;
	case WT_I32:
		rd_read(r, 4);
		break;
	case WT_SGROUP:
		for (;;) {
			uint32_t inum;
			int iwt;
			rd_tag(r, &inum, &iwt);
			if (iwt == WT_EGROUP) {
				if (inum != num)
					fail("ValueError: mismatched group tag: expected %lld, got %lld", (long long)num, (long long)inum);
				break;
			}
			rd_skip(r, iwt, iwt == WT_SGROUP ? depth + 1 : depth, inum);
		}
		break;
	case WT_EGROUP:
		fail("ValueError: unexpected end group tag outside of group");
	}
	return (Str){(const char *)r->p + start, r->off - start};
}

static void put_varint(Buf *b, uint64_t v)
{
	uint8_t t[10];
	int n = 0;
	while (v > 0x7f) {
		t[n++] = (uint8_t)(v | 0x80);
		v >>= 7;
	}
	t[n++] = (uint8_t)v;
	buf_add(b, t, (size_t)n);
}

static void put_u32(Buf *b, uint32_t v) { buf_add(b, &v, 4); }

static uint32_t get_u32(const char *p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}

/* message._get_or_init_unknown_fields().setdefault(num, []).append(tag + raw) */
static void add_unknown(PbMsg *m, uint32_t num, int wt, Str raw)
{
	Buf rec = {0};
	put_varint(&rec, (uint64_t)num << 3 | (unsigned)wt);
	buf_add(&rec, raw.p, raw.n);
	put_u32(&m->unknown, num);
	put_u32(&m->unknown, (uint32_t)rec.n);
	buf_add(&m->unknown, rec.p, rec.n);
}

/* _write_unknown_enum_field: the value as an int32 varint. */
static void add_unknown_enum(PbMsg *m, uint32_t num, int64_t v)
{
	Buf val = {0};
	put_varint(&val, (uint64_t)v);
	add_unknown(m, num, WT_VARINT, buf_view(&val));
}

/* The records of field num, counted first and allocated once (linear in the input). */
int pbpy_unknown_fields(const PbMsg *m, uint32_t num, Str **out)
{
	size_t n = 0;
	for (size_t i = 0; m && i + 8 <= m->unknown.n; i += 8 + get_u32(m->unknown.p + i + 4))
		n += get_u32(m->unknown.p + i) == num;
	Str *v = n ? xalloc(sizeof *v * n) : NULL;
	size_t k = 0;
	for (size_t i = 0; k < n; i += 8 + get_u32(m->unknown.p + i + 4))
		if (get_u32(m->unknown.p + i) == num)
			v[k++] = (Str){m->unknown.p + i + 8, get_u32(m->unknown.p + i + 4)};
	*out = v;
	return (int)n;
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

static bool packable(const PbField *f)
{
	return f->label != PL_OPT && f->label != PBPY_REQUIRED && wire_type_of(f->type) != WT_LEN &&
		f->type != PT_GROUP;
}

static bool enum_known(const PbField *f, int64_t v)
{
	for (int i = 0; i < f->n_enum_vals; i++)
		if (f->enum_vals[i] == v)
			return true;
	return false;
}

/* read_scalar: the value as pkg/pb stores it (varints canonical, fixed types raw bits, strings and
 * bytes copied). Returns false for an enum value that a closed enum does not know (*v holds it). */
static bool read_scalar(Reader *r, const PbField *f, PbVal *v)
{
	switch (f->type) {
	case PT_STRING: case PT_BYTES: {
		uint64_t n = rd_varint64(r);
		const uint8_t *p = rd_read(r, n);
		if (f->type == PT_STRING) {
			/* str(view, "utf-8") */
			char *err = py_utf8_decode_error((Str){(const char *)p, (size_t)n});
			if (err)
				fail("%s", err);
		}
		v->s.p = xstrndup((const char *)p, (size_t)n);
		v->s.n = (size_t)n;
		return true;
	}
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: {
		int n = wire_type_of(f->type) == WT_I64 ? 8 : 4;
		const uint8_t *p = rd_read(r, (uint64_t)n);
		uint64_t x = 0;
		for (int i = n - 1; i >= 0; i--)
			x = x << 8 | p[i];
		v->u = x;
		return true;
	}
	}
	uint64_t x = rd_varint64(r);
	switch (f->type) {
	case PT_INT32: case PT_ENUM: x = (uint64_t)(int64_t)(int32_t)(uint32_t)x; break;
	case PT_UINT32: x = (uint32_t)x; break;
	case PT_BOOL: x = x != 0; break;
	case PT_SINT32: {
		uint32_t y = (uint32_t)x;
		x = (uint64_t)(int64_t)(int32_t)((y >> 1) ^ (0u - (y & 1)));
		break;
	}
	case PT_SINT64: x = (x >> 1) ^ (0u - (x & 1)); break;
	}
	v->u = x;
	return f->type != PT_ENUM || !f->enum_vals || enum_known(f, (int64_t)x);
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

static void slot_set(PbSlot *s, PbVal v)
{
	if (s->n)
		s->v[0] = v;
	else
		slot_push(s, v);
}

static void read_message(PbMsg *m, Reader *r, int depth, uint64_t length);

/* reader.offset + length (past the input: never reached) */
static uint64_t msg_end(const Reader *r, uint64_t length)
{
	return length > r->n - r->off ? UINT64_MAX : (uint64_t)r->off + length;
}

/* desc_message._fields_by_tag.get(tag.raw): the field index, or -1. */
static int field_by_tag(const PbMsgDef *def, uint32_t num, int wt)
{
	for (int i = 0; i < def->n; i++) {
		const PbField *f = &def->f[i];
		if (f->num != num)
			continue;
		if (wt == wire_type_of(f->type) || (wt == WT_LEN && packable(f)))
			return i;
	}
	return -1;
}

static void read_message(PbMsg *m, Reader *r, int depth, uint64_t length)
{
	if (depth > PBPY_DEPTH_LIMIT)
		fail("RecursionError: exceeded maximum recursion depth %d while parsing message", PBPY_DEPTH_LIMIT);
	uint64_t end = msg_end(r, length);
	while (r->off < end) {
		uint32_t num;
		int wt;
		rd_tag(r, &num, &wt);
		int fi = field_by_tag(m->def, num, wt);
		if (fi < 0) {
			Str raw = rd_skip(r, wt, depth + 1, num);
			add_unknown(m, num, wt, raw);
			continue;
		}
		const PbField *f = &m->def->f[fi];
		PbSlot *s = &m->slots[fi];
		if (f->label == PL_OPT || f->label == PBPY_REQUIRED) {
			if (f->type == PT_MESSAGE) {
				if (!s->n)
					slot_push(s, (PbVal){.m = pb_new(f->msg)});
				read_message(s->v[0].m, r, depth + 1, rd_varint64(r));
			} else {
				PbVal v;
				if (read_scalar(r, f, &v))
					slot_set(s, v);
				else
					add_unknown_enum(m, num, (int64_t)v.u);
			}
			continue;
		}
		/* read_list */
		if (wt == WT_LEN && packable(f)) {
			uint64_t pend = msg_end(r, rd_varint64(r));
			while (r->off < pend) {
				PbVal v;
				if (read_scalar(r, f, &v))
					slot_push(s, v);
				else
					add_unknown_enum(m, num, (int64_t)v.u);
			}
			continue;
		}
		if (f->type == PT_MESSAGE) {
			PbVal v = {.m = pb_new(f->msg)};
			read_message(v.m, r, depth + 1, rd_varint64(r));
			slot_push(s, v);
		} else {
			PbVal v;
			if (read_scalar(r, f, &v))
				slot_push(s, v);
			else
				add_unknown_enum(m, num, (int64_t)v.u);
		}
	}
}

PbMsg *pbpy_from_binary(const PbMsgDef *def, Str data)
{
	PbMsg *m = pb_new(def);
	Reader r = {(const uint8_t *)data.p, data.n, 0};
	read_message(m, &r, 0, data.n);
	return m;
}

/* ---- to_binary ---- */

static void put_tag(Buf *b, uint32_t num, int wt) { put_varint(b, (uint64_t)num << 3 | (unsigned)wt); }

static void put_scalar(Buf *b, int type, PbVal v)
{
	switch (type) {
	case PT_STRING: case PT_BYTES:
		put_varint(b, v.s.n);
		buf_add(b, v.s.p, v.s.n);
		return;
	case PT_SINT32: {
		uint32_t x = (uint32_t)v.u;
		put_varint(b, (x << 1) ^ (0u - (x >> 31)));
		return;
	}
	case PT_SINT64:
		put_varint(b, (v.u << 1) ^ (0u - (v.u >> 63)));
		return;
	}
	int wt = wire_type_of(type);
	if (wt == WT_VARINT) {
		put_varint(b, v.u);
	} else {
		uint8_t t[8];
		int n = wt == WT_I64 ? 8 : 4;
		for (int i = 0; i < n; i++)
			t[i] = (uint8_t)(v.u >> (8 * i));
		buf_add(b, t, (size_t)n);
	}
}

/* Required fields exist only in UninterpretedOption.NamePart. */
static const char *required_field_name(const PbField *f)
{
	return f->num == 1 ? "google.protobuf.UninterpretedOption.NamePart.name_part"
			   : "google.protobuf.UninterpretedOption.NamePart.is_extension";
}

static void encode(const PbMsg *m, uint32_t skip_num, Buf *out, int depth)
{
	if (depth > PBPY_DEPTH_LIMIT)
		fail("RecursionError: exceeded maximum recursion depth %d while serializing message", PBPY_DEPTH_LIMIT);
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		if (f->num == skip_num)
			continue;
		if (!s->n) {
			if (f->label == PBPY_REQUIRED)
				fail("ValueError: cannot encode field %s: required field not set", required_field_name(f));
			continue;
		}
		if (f->label == PL_PACKED) {
			Buf tmp = {0};
			for (int j = 0; j < s->n; j++)
				put_scalar(&tmp, f->type, s->v[j]);
			put_tag(out, f->num, WT_LEN);
			put_varint(out, tmp.n);
			buf_add(out, tmp.p, tmp.n);
			continue;
		}
		for (int j = 0; j < s->n; j++) {
			if (f->type == PT_MESSAGE) {
				Buf tmp = {0};
				encode(s->v[j].m, 0, &tmp, depth + 1);
				put_tag(out, f->num, WT_LEN);
				put_varint(out, tmp.n);
				buf_add(out, tmp.p, tmp.n);
			} else {
				put_tag(out, f->num, wire_type_of(f->type));
				put_scalar(out, f->type, s->v[j]);
			}
		}
	}
	/* the unknown fields: dict by number, in the order of the first occurrence */
	if (!m->unknown.n)
		return;
	Map seen = {0};
	Vec order = {0};
	for (size_t i = 0; i + 8 <= m->unknown.n;) {
		uint32_t num = get_u32(m->unknown.p + i), len = get_u32(m->unknown.p + i + 4);
		char *key = xfmt("%lld", (long long)num);
		Buf *recs = map_get(&seen, key);
		if (!recs) {
			recs = xalloc(sizeof *recs);
			map_put(&seen, key, recs);
			vec_push(&order, recs);
		}
		buf_add(recs, m->unknown.p + i + 8, len);
		i += 8 + len;
	}
	for (int i = 0; i < order.n; i++) {
		Buf *recs = order.v[i];
		buf_add(out, recs->p, recs->n);
	}
}

void pbpy_to_binary(const PbMsg *m, uint32_t skip_num, Buf *out) { encode(m, skip_num, out, 0); }

void pbpy_merge_unknown_message(PbMsg *m, Str record)
{
	Reader r = {(const uint8_t *)record.p, record.n, 0};
	uint32_t num;
	int wt;
	rd_tag(&r, &num, &wt);
	read_message(m, &r, 0, rd_varint64(&r));
}
