// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The wire format of messages whose schema comes from a descriptor pool, with the semantics of
 * the C++ runtime for generated messages (the generated code of descriptor.proto and plugin.proto
 * linked into protoc_lib) and DynamicMessage (src/google/protobuf/wire_format.cc,
 * dynamic_message.cc, parse_context.h):
 *
 *   - parsing merges: the last scalar wins, singular messages and groups are merged, repeated
 *     scalars accept packed and unpacked encodings, a oneof member clears the others, implicit
 *     presence fields that receive their zero value are cleared, unknown values of closed enums
 *     and fields with an unexpected wire type are kept as unknown fields, strings with
 *     utf8_validation = VERIFY must be valid UTF-8, nesting is limited to 100 levels;
 *   - serialization writes the set fields (extensions included) by number, then the unknown
 *     fields in parse order; the fields of a map entry are always written, an unset one with its
 *     default value (wire_format.cc: "Map entry fields always need to be serialized").
 *
 * Values are stored in pkg/pb PbMsg slots (the pb.h accessors work on them); the definitions are
 * LpMsgDef, built on demand from a pool and cached per pool. Map fields are kept as repeated
 * entries in parse order, duplicate keys included: upstream parses them through reflection
 * (Reflection::AddMessage), which makes the repeated field authoritative, so the map is never
 * built and WireFormat serializes the entries as they were parsed. */

/* Per-field flags of an LpMsgDef. */
enum {
	LPF_IMPLICIT = 1, /* no presence: the zero value clears the field */
	LPF_UTF8 = 2,     /* string with utf8_validation = VERIFY */
	LPF_REQUIRED = 4, /* checked by lp_is_initialized */
	LPF_ALWAYS = 8,   /* field of a map entry: always serialized */
};

typedef struct LpMsgDef LpMsgDef;
struct LpMsgDef {
	PbMsgDef pb;                /* fields sorted by number, extensions included */
	const LpMessage *desc;
	const LpField **fields;     /* per pb.f[i] */
	uint8_t *flags;             /* per pb.f[i]: LPF_* */
	int *oneof;                 /* per pb.f[i]: index of the containing oneof + 1, or 0 */
};

#define LP_MAX_DEPTH 100

static const LpMsgDef *lp_def(const PbMsg *m) { return (const LpMsgDef *)m->def; }

/* ---- reading (parse_context.h) ---- */

/* VarintParse: up to 10 bytes; bits beyond 64 are dropped like upstream. */
static bool lp_read_varint(PbReader *r, uint64_t *v)
{
	uint64_t x = 0;
	for (int i = 0; i < 10; i++) {
		if (r->p >= r->end)
			return false;
		uint8_t b = *r->p++;
		x |= (uint64_t)(b & 0x7f) << (7 * i);
		if (!(b & 0x80)) {
			*v = x;
			return true;
		}
	}
	return false;
}

/* ReadTag: a 32-bit varint; 0 ends the message (an error for the callers here). */
static bool lp_read_tag(PbReader *r, uint32_t *tag)
{
	uint64_t x = 0;
	for (int i = 0; i < 5; i++) {
		if (r->p >= r->end)
			return false;
		uint8_t b = *r->p++;
		x |= (uint64_t)(b & 0x7f) << (7 * i);
		if (!(b & 0x80)) {
			if (x > 0xffffffffu)
				return false;
			*tag = (uint32_t)x;
			return true;
		}
	}
	return false;
}

/* ReadSize: at most INT_MAX - 16 bytes and within the buffer. */
static bool lp_read_len(PbReader *r, const uint8_t **val, size_t *n)
{
	uint64_t v;
	if (!lp_read_varint(r, &v) || v > 0x7fffffefu || v > (uint64_t)(r->end - r->p))
		return false;
	*val = r->p;
	*n = (size_t)v;
	r->p += v;
	return true;
}

/* Skips the value of an unknown field (groups up to the depth limit). */
static bool lp_skip_value(PbReader *r, uint32_t tag, int depth)
{
	uint64_t v;
	const uint8_t *val;
	size_t n;
	switch (tag & 7) {
	case WT_VARINT:
		return lp_read_varint(r, &v);
	case WT_I64:
		if (r->end - r->p < 8)
			return false;
		r->p += 8;
		return true;
	case WT_I32:
		if (r->end - r->p < 4)
			return false;
		r->p += 4;
		return true;
	case WT_LEN:
		return lp_read_len(r, &val, &n);
	case WT_SGROUP:
		if (depth + 1 > LP_MAX_DEPTH)
			return false;
		for (;;) {
			uint32_t t;
			if (!lp_read_tag(r, &t) || t == 0)
				return false;
			if ((t & 7) == WT_EGROUP)
				return (t >> 3) == (tag >> 3);
			if (!lp_skip_value(r, t, depth + 1))
				return false;
		}
	default:
		return false;
	}
}

/* utf8_range::IsStructurallyValid */
static bool lp_utf8_valid(const uint8_t *p, size_t n)
{
	size_t i = 0;
	while (i < n) {
		uint8_t c = p[i];
		if (c < 0x80) {
			i++;
			continue;
		}
		int k;
		uint32_t r;
		if (c >= 0xc2 && c <= 0xdf)
			k = 1, r = c & 0x1f;
		else if (c >= 0xe0 && c <= 0xef)
			k = 2, r = c & 0x0f;
		else if (c >= 0xf0 && c <= 0xf4)
			k = 3, r = c & 0x07;
		else
			return false;
		if (i + (size_t)k >= n)
			return false;
		for (int j = 1; j <= k; j++) {
			if ((p[i + (size_t)j] & 0xc0) != 0x80)
				return false;
			r = r << 6 | (p[i + (size_t)j] & 0x3f);
		}
		if ((k == 2 && (r < 0x800 || (r >= 0xd800 && r <= 0xdfff))) || (k == 3 && (r < 0x10000 || r > 0x10ffff)))
			return false;
		i += (size_t)k + 1;
	}
	return true;
}

/* ---- slots (proto_msg_field_index, proto_slot_push: pkg/proto) ---- */

static int lp_wire_type(int type)
{
	switch (type) {
	case PT_DOUBLE: case PT_FIXED64: case PT_SFIXED64: return WT_I64;
	case PT_FLOAT: case PT_FIXED32: case PT_SFIXED32: return WT_I32;
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: return WT_LEN;
	case PT_GROUP: return WT_SGROUP;
	default: return WT_VARINT;
	}
}

static uint64_t lp_canon_varint(int type, uint64_t v)
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

static bool lp_read_scalar(PbReader *r, int type, uint64_t *out)
{
	int wt = lp_wire_type(type);
	if (wt == WT_VARINT) {
		uint64_t v;
		if (!lp_read_varint(r, &v))
			return false;
		*out = lp_canon_varint(type, v);
		return true;
	}
	int n = wt == WT_I64 ? 8 : 4;
	if (r->end - r->p < n)
		return false;
	uint64_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | r->p[i];
	r->p += n;
	*out = v;
	return true;
}

static bool lp_enum_known(const PbField *f, uint64_t v)
{
	if (!f->enum_vals)
		return true;
	for (int i = 0; i < f->n_enum_vals; i++)
		if ((int64_t)f->enum_vals[i] == (int64_t)v)
			return true;
	return false;
}

/* Clears the other members of the oneof of field i. */
static void lp_clear_oneof_others(PbMsg *m, int i)
{
	const LpMsgDef *d = lp_def(m);
	if (!d->oneof[i])
		return;
	for (int j = 0; j < d->pb.n; j++)
		if (j != i && d->oneof[j] == d->oneof[i])
			m->slots[j].n = 0;
}

static bool lp_is_zero(int type, PbVal v)
{
	if (type == PT_STRING || type == PT_BYTES)
		return v.s.n == 0;
	return v.u == 0;
}

/* Stores a singular or repeated non-message value. */
static void lp_store(PbMsg *m, int i, PbVal v)
{
	const LpMsgDef *d = lp_def(m);
	const PbField *f = &d->pb.f[i];
	PbSlot *s = &m->slots[i];
	if (f->label != PL_OPT) {
		proto_slot_push(s, v);
		return;
	}
	lp_clear_oneof_others(m, i);
	if ((d->flags[i] & LPF_IMPLICIT) && lp_is_zero(f->type, v)) {
		s->n = 0;
		return;
	}
	if (s->n)
		s->v[0] = v;
	else
		proto_slot_push(s, v);
}

static void lp_add_unknown_varint(PbMsg *m, uint32_t num, uint64_t v)
{
	pb_put_tag(&m->unknown, num, WT_VARINT);
	pb_put_varint(&m->unknown, v);
}

static bool lp_decode_impl(PbMsg *m, PbReader *r, int depth, uint32_t end_group);

/* The message to parse into for field i: the existing one (merge) or a new one. */
static PbMsg *lp_sub_for_parse(PbMsg *m, int i)
{
	const PbField *f = &m->def->f[i];
	PbSlot *s = &m->slots[i];
	if (f->label == PL_OPT) {
		if (s->n)
			return s->v[0].m;
		lp_clear_oneof_others(m, i);
	}
	PbVal x = {.m = pb_new(f->msg)};
	proto_slot_push(s, x);
	return x.m;
}

/* depth: the nesting level of m (0 for the top-level message); end_group: the tag that ends m if
 * it is a group, else 0. */
static bool lp_decode_impl(PbMsg *m, PbReader *r, int depth, uint32_t end_group)
{
	const LpMsgDef *d = lp_def(m);
	while (r->p < r->end) {
		const uint8_t *start = r->p;
		uint32_t tag;
		if (!lp_read_tag(r, &tag) || tag == 0)
			return false;
		uint32_t num = tag >> 3;
		int wt = (int)(tag & 7);
		if (wt == WT_EGROUP)
			return end_group && tag == end_group;
		int i = proto_msg_field_index(&d->pb, num);
		const PbField *f = i >= 0 ? &d->pb.f[i] : NULL;
		int fwt = f ? lp_wire_type(f->type) : -1;
		bool packable = f && f->label != PL_OPT && fwt != WT_LEN && fwt != WT_SGROUP;
		bool packed = packable && wt == WT_LEN;
		if (!f || (wt != fwt && !packed)) {
			if (!lp_skip_value(r, tag, depth))
				return false;
			buf_add(&m->unknown, start, (size_t)(r->p - start));
			continue;
		}
		if (packed) {
			const uint8_t *v;
			size_t vn;
			if (!lp_read_len(r, &v, &vn))
				return false;
			PbReader pr = {v, v + vn};
			while (pr.p < pr.end) {
				PbVal x;
				if (!lp_read_scalar(&pr, f->type, &x.u))
					return false;
				if (f->type == PT_ENUM && !lp_enum_known(f, x.u))
					lp_add_unknown_varint(m, num, x.u);
				else
					lp_store(m, i, x);
			}
			continue;
		}
		switch (fwt) {
		case WT_LEN: {
			const uint8_t *v;
			size_t vn;
			if (!lp_read_len(r, &v, &vn))
				return false;
			if (f->type == PT_MESSAGE) {
				if (depth + 1 > LP_MAX_DEPTH)
					return false;
				PbMsg *sub = lp_sub_for_parse(m, i);
				PbReader sr = {v, v + vn};
				if (!lp_decode_impl(sub, &sr, depth + 1, 0))
					return false;
			} else {
				if ((d->flags[i] & LPF_UTF8) && !lp_utf8_valid(v, vn))
					return false;
				PbVal x;
				x.s.p = xstrndup((const char *)v, vn);
				x.s.n = vn;
				lp_store(m, i, x);
			}
			break;
		}
		case WT_SGROUP: {
			if (depth + 1 > LP_MAX_DEPTH)
				return false;
			PbMsg *sub = lp_sub_for_parse(m, i);
			if (!lp_decode_impl(sub, r, depth + 1, (num << 3) | WT_EGROUP))
				return false;
			break;
		}
		default: {
			PbVal x;
			if (!lp_read_scalar(r, f->type, &x.u))
				return false;
			if (f->type == PT_ENUM && !lp_enum_known(f, x.u))
				lp_add_unknown_varint(m, num, x.u);
			else
				lp_store(m, i, x);
		}
		}
	}
	return end_group == 0;
}

/* MergeFromString / ParsePartialFromString (no required-field check). */
static bool lp_decode(PbMsg *m, const uint8_t *p, size_t n)
{
	PbReader r = {p, p + n};
	return lp_decode_impl(m, &r, 0, 0);
}

/* Message::IsInitialized */
static bool lp_is_initialized(const PbMsg *m)
{
	const LpMsgDef *d = lp_def(m);
	for (int i = 0; i < d->pb.n; i++) {
		const PbSlot *s = &m->slots[i];
		if ((d->flags[i] & LPF_REQUIRED) && !s->n)
			return false;
		if (d->pb.f[i].type == PT_MESSAGE || d->pb.f[i].type == PT_GROUP)
			for (int j = 0; j < s->n; j++)
				if (!lp_is_initialized(s->v[j].m))
					return false;
	}
	return true;
}

/* ---- writing ---- */

static void lp_put_scalar(Buf *b, int type, uint64_t v)
{
	switch (lp_wire_type(type)) {
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
	case WT_I32: {
		int n = lp_wire_type(type) == WT_I64 ? 8 : 4;
		uint8_t t[8];
		for (int i = 0; i < n; i++)
			t[i] = (uint8_t)(v >> (8 * i));
		buf_add(b, t, (size_t)n);
		break;
	}
	}
}

/* The default value of an unset field of a map entry (Reflection::Get* of an unset field). */
static PbSlot lp_default_slot(const PbField *f, const LpField *field)
{
	PbVal *v = xalloc(sizeof *v);
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32: v->u = (uint64_t)(int64_t)(int32_t)field->default_value.i; break;
	case LP_CPPTYPE_INT64: v->u = (uint64_t)field->default_value.i; break;
	case LP_CPPTYPE_UINT32: case LP_CPPTYPE_UINT64: v->u = field->default_value.u; break;
	case LP_CPPTYPE_BOOL: v->u = field->default_value.b; break;
	case LP_CPPTYPE_FLOAT: {
		uint32_t bits;
		memcpy(&bits, &field->default_value.f, sizeof bits);
		v->u = bits;
		break;
	}
	case LP_CPPTYPE_DOUBLE: memcpy(&v->u, &field->default_value.d, sizeof v->u); break;
	case LP_CPPTYPE_ENUM:
		v->u = field->default_value_enum ? (uint64_t)(int64_t)field->default_value_enum->number : 0;
		break;
	case LP_CPPTYPE_STRING: v->s = field->default_value_string; break;
	case LP_CPPTYPE_MESSAGE:
		if (f->type == PT_MESSAGE || f->type == PT_GROUP)
			v->m = pb_new(f->msg);
		break;
	}
	if (f->type == PT_STRING || f->type == PT_BYTES)
		v->s = field->default_value_string; /* includes unresolved message types: empty */
	return (PbSlot){1, 1, v};
}

/* SerializeToString: set fields by number, then the unknown fields. d is m->def as an LpMsgDef,
 * or NULL if m has a plain pkg/pb definition (the messages of the embedded descriptors, which
 * have no map fields). */
static void lp_serialize_impl(const PbMsg *m, const LpMsgDef *d, Buf *out)
{
	for (int i = 0; i < m->def->n; i++) {
		const PbField *f = &m->def->f[i];
		const PbSlot *s = &m->slots[i];
		PbSlot dflt;
		if (!s->n && d && (d->flags[i] & LPF_ALWAYS)) {
			dflt = lp_default_slot(f, d->fields[i]);
			s = &dflt;
		}
		if (!s->n)
			continue;
		if (f->label == PL_PACKED) {
			Buf tmp = {0};
			for (int j = 0; j < s->n; j++)
				lp_put_scalar(&tmp, f->type, s->v[j].u);
			pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			continue;
		}
		for (int j = 0; j < s->n; j++) {
			const PbVal *v = &s->v[j];
			/* the definitions of the fields of an LpMsgDef are LpMsgDefs (msgdef_fill) */
			const LpMsgDef *sub = d && (f->type == PT_MESSAGE || f->type == PT_GROUP) && v->m->def == f->msg
						      ? (const LpMsgDef *)f->msg
						      : NULL;
			if (f->type == PT_MESSAGE) {
				Buf tmp = {0};
				lp_serialize_impl(v->m, sub, &tmp);
				pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			} else if (f->type == PT_GROUP) {
				pb_put_tag(out, f->num, WT_SGROUP);
				lp_serialize_impl(v->m, sub, out);
				pb_put_tag(out, f->num, WT_EGROUP);
			} else if (f->type == PT_STRING || f->type == PT_BYTES) {
				pb_put_bytes(out, f->num, v->s);
			} else {
				pb_put_tag(out, f->num, lp_wire_type(f->type));
				lp_put_scalar(out, f->type, v->u);
			}
		}
	}
	buf_add(out, m->unknown.p, m->unknown.n);
}

void lp_msg_serialize(const PbMsg *m, Buf *out) { lp_serialize_impl(m, (const LpMsgDef *)m->def, out); }

static Str lp_serialize_str(const PbMsg *m)
{
	Buf b = {0};
	lp_msg_serialize(m, &b);
	return (Str){b.p ? b.p : "", b.n};
}

/* The generic operations (MergeFrom, copies, setters, ByteSizeLong() == 0 as proto_msg_is_empty)
 * are pkg/proto's proto_msg_* (proto.h). */
