/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Wire format with protobuf-go v1.36.12 semantics, ported from:
 *   - proto/decode.go, internal/impl/decode.go (proto.Unmarshal: generated messages, extensions
 *     resolved by protoregistry types, dynamicpb messages via unmarshalMessageSlow, maps),
 *   - proto/encode.go, internal/impl/encode.go, internal/order (deterministic Marshal: generated
 *     messages write extensions first then fields by number; dynamic messages use
 *     LegacyFieldOrder and GenericKeyOrder for maps),
 *   - types/dynamicpb (isSet: implicit-presence zero values are not serialized; setting a oneof
 *     member clears the others),
 *   - cmd/protoc-gen-go/internal_gengo/reflect.go (stripSourceRetentionFieldsFromMessage).
 * Required fields are not checked (AllowPartial). */
#include "internal.h"

#include <string.h>

/* protowire.DefaultRecursionLimit */
#define GO_RECURSION_LIMIT 10000
/* Nesting is also limited by the C stack used for decoding (deviation: protobuf-go allows 10000
 * levels everywhere; this keeps decoding, encoding and the protogen passes well within a 1 MiB
 * stack, the Windows default). */
#define GO_STACK_LIMIT (256 * 1024)
static uintptr_t stack_base;

static bool stack_exhausted(void)
{
	uintptr_t p = (uintptr_t)__builtin_frame_address(0);
	return (p > stack_base ? p - stack_base : stack_base - p) > GO_STACK_LIMIT;
}

struct GoSchema {
	GoMsgDef *statics[64];
	Map exts;      /* extendee full name -> Vec of ExtEntry* */
	Map dynamics;  /* message full name -> GoMsgDef* */
};

typedef struct {
	uint32_t num;
	const GoField *xd; /* NULL: the base pb.go extension */
} ExtEntry;

typedef struct {
	PbField f;
	uint8_t flags;
	int oneof;
	const char *name;
} TmpField;

/* ---- schema construction ---- */

static GoMsgDef *build_static(GoSchema *s, int idx);
static GoMsgDef *build_dynamic(GoSchema *s, const GoMessage *m);

static const Vec *exts_of(const GoSchema *s, const char *full_name) { return map_get(&s->exts, full_name); }

static uint8_t field_flags(const GoField *fd)
{
	uint8_t fl = 0;
	const PbMsg *o = fd->d.options;
	if (pb_int(o, GENID_FieldOptions_Retention_field_number, 0) == GENID_FieldOptions_RETENTION_SOURCE_enum_value)
		fl |= GOF_SOURCE;
	/* strs.EnforceUTF8: editions fields use the feature (filedesc.Field.EnforceUTF8), proto3
	 * fields always validate. */
	if (fd->kind == PT_STRING) {
		int syn = go_field_syntax(fd);
		if (syn == GO_EDITIONS && !fd->is_extension ? fd->features.IsUTF8Validated : syn == GO_PROTO3)
			fl |= GOF_UTF8;
	}
	return fl;
}

static PbField pb_field_of(GoSchema *s, const GoField *fd)
{
	PbField f = {0};
	f.num = (uint32_t)fd->number;
	f.type = (uint8_t)fd->kind;
	f.label = fd->cardinality != GO_REPEATED ? PL_OPT : go_field_is_packed(fd) ? PL_PACKED : PL_REP;
	if ((fd->kind == PT_MESSAGE || fd->kind == PT_GROUP) && fd->message_type)
		f.msg = &build_dynamic(s, fd->message_type)->pb;
	return f;
}

static int cmp_num(const TmpField *a, const TmpField *b) { return a->f.num < b->f.num ? -1 : a->f.num > b->f.num; }

/* Fills def from tmp (n fields, any order); dynamic messages order oneof members last. */
static void finish_def(GoMsgDef *def, TmpField *tmp, int n)
{
	/* insertion sort by number (stable) */
	for (int i = 1; i < n; i++) {
		TmpField x = tmp[i];
		int j = i - 1;
		for (; j >= 0 && cmp_num(&tmp[j], &x) > 0; j--)
			tmp[j + 1] = tmp[j];
		tmp[j + 1] = x;
	}
	PbField *f = xalloc(sizeof *f * (size_t)(n ? n : 1));
	uint8_t *flags = xalloc((size_t)(n ? n : 1));
	int *oneof = xalloc(sizeof *oneof * (size_t)(n ? n : 1));
	const char **names = xalloc(sizeof *names * (size_t)(n ? n : 1));
	int *order = xalloc(sizeof *order * (size_t)(n ? n : 1));
	for (int i = 0; i < n; i++) {
		f[i] = tmp[i].f;
		flags[i] = tmp[i].flags;
		oneof[i] = tmp[i].oneof;
		names[i] = tmp[i].name;
	}
	/* Extensions first, then (LegacyFieldOrder for dynamic messages) fields outside of oneofs,
	 * then the members of each oneof by declaration index; by number within each group. */
	int k = 0;
	for (int i = 0; i < n; i++)
		if (flags[i] & GOF_EXT)
			order[k++] = i;
	for (int i = 0; i < n; i++)
		if (!(flags[i] & GOF_EXT) && !oneof[i])
			order[k++] = i;
	int max_oneof = 0;
	for (int i = 0; i < n; i++)
		if (!(flags[i] & GOF_EXT) && oneof[i] > max_oneof)
			max_oneof = oneof[i];
	for (int o = 1; o <= max_oneof; o++)
		for (int i = 0; i < n; i++)
			if (!(flags[i] & GOF_EXT) && oneof[i] == o)
				order[k++] = i;
	def->pb.n = n;
	def->pb.f = f;
	def->flags = flags;
	def->oneof = oneof;
	def->names = names;
	def->order = order;
}

static bool has_num(const TmpField *tmp, int n, uint32_t num)
{
	for (int i = 0; i < n; i++)
		if (tmp[i].f.num == num)
			return true;
	return false;
}

/* Appends the extension fields of full_name to tmp (fields win over extensions). */
static int add_exts(GoSchema *s, const char *full_name, TmpField **tmp, int n, int *cap)
{
	const Vec *xs = exts_of(s, full_name);
	for (int i = 0; xs && i < xs->n; i++) {
		const ExtEntry *e = xs->v[i];
		if (has_num(*tmp, n, e->num))
			continue;
		if (n == *cap) {
			int nc = *cap * 2 + 4;
			*tmp = xrealloc(*tmp, sizeof **tmp * (size_t)*cap, sizeof **tmp * (size_t)nc);
			*cap = nc;
		}
		TmpField t = {0};
		if (e->xd) {
			t.f = pb_field_of(s, e->xd);
			t.flags = (uint8_t)(GOF_EXT | field_flags(e->xd));
			t.name = e->xd->d.full_name;
		} else {
			t.f = (PbField){go_spec_base_ext.num, PT_MESSAGE, PL_OPT, &build_static(s, go_spec_base_ext.msg)->pb, NULL, 0};
			t.flags = GOF_EXT;
			t.name = go_spec_base_ext.name;
		}
		(*tmp)[n++] = t;
	}
	return n;
}

static GoMsgDef *build_static(GoSchema *s, int idx)
{
	if (s->statics[idx])
		return s->statics[idx];
	const GoSpecMsg *sm = &go_spec_msgs[idx];
	GoMsgDef *def = xalloc(sizeof *def);
	s->statics[idx] = def;
	def->full_name = sm->name;
	def->has_ext = sm->has_ext;
	int cap = sm->n + 4, n = 0;
	TmpField *tmp = xalloc(sizeof *tmp * (size_t)cap);
	for (int i = 0; i < sm->n; i++) {
		const GoSpecField *sf = &sm->f[i];
		TmpField t = {0};
		t.f = (PbField){sf->num, sf->type, sf->label, sf->msg >= 0 ? &build_static(s, sf->msg)->pb : NULL, NULL, 0};
		t.flags = sf->flags;
		tmp[n++] = t;
	}
	if (sm->has_ext)
		n = add_exts(s, sm->name, &tmp, n, &cap);
	finish_def(def, tmp, n);
	return def;
}

static GoMsgDef *build_dynamic(GoSchema *s, const GoMessage *m)
{
	GoMsgDef *def = map_get(&s->dynamics, m->d.full_name);
	if (def)
		return def;
	def = xalloc(sizeof *def);
	map_put(&s->dynamics, m->d.full_name, def);
	def->full_name = m->d.full_name;
	def->dynamic = true;
	def->has_ext = m->n_ext_ranges > 0;
	def->desc = m;
	int cap = m->n_fields + 4, n = 0;
	TmpField *tmp = xalloc(sizeof *tmp * (size_t)cap);
	for (int i = 0; i < m->n_fields; i++) {
		const GoField *fd = m->fields[i];
		if (has_num(tmp, n, (uint32_t)fd->number))
			continue; /* duplicate numbers are rejected by validation */
		TmpField t = {0};
		t.f = pb_field_of(s, fd);
		t.flags = field_flags(fd);
		if (fd->cardinality != GO_REPEATED && !go_field_has_presence(fd))
			t.flags |= GOF_IMPLICIT;
		if (go_field_is_map(fd))
			t.flags |= GOF_MAP;
		if (fd->oneof && !go_oneof_is_synthetic(fd->oneof))
			t.oneof = fd->oneof->d.index + 1;
		t.name = fd->d.full_name;
		tmp[n++] = t;
	}
	if (def->has_ext)
		n = add_exts(s, m->d.full_name, &tmp, n, &cap);
	finish_def(def, tmp, n);
	return def;
}

static void add_ext(GoSchema *s, const char *extendee, uint32_t num, const GoField *xd)
{
	Vec *v = map_get(&s->exts, extendee);
	if (!v) {
		v = xalloc(sizeof *v);
		map_put(&s->exts, extendee, v);
	}
	/* FindExtensionByNumber: local types first, then the base registry. */
	for (int i = 0; i < v->n; i++) {
		ExtEntry *e = v->v[i];
		if (e->num == num) {
			if (!e->xd && xd)
				e->xd = xd;
			return;
		}
	}
	ExtEntry *e = xalloc(sizeof *e);
	e->num = num;
	e->xd = xd;
	vec_push(v, e);
}

static GoSchema *schema_new(void)
{
	GoSchema *s = xalloc(sizeof *s);
	add_ext(s, go_spec_msgs[go_spec_base_ext.extendee].name, go_spec_base_ext.num, NULL);
	return s;
}

static void schema_build(GoSchema *s)
{
	for (int i = 0; i < go_spec_n_msgs; i++)
		build_static(s, i);
}

GoSchema *gopb_schema_base(void)
{
	/* not cached: everything lives in the arena, which may be reset between requests */
	GoSchema *base = schema_new();
	schema_build(base);
	return base;
}

GoSchema *gopb_schema_with_extensions(GoField *const *xs, int n)
{
	GoSchema *s = schema_new();
	for (int i = 0; i < n; i++)
		if (xs[i]->containing)
			add_ext(s, xs[i]->containing->d.full_name, (uint32_t)xs[i]->number, xs[i]);
	schema_build(s);
	return s;
}

const GoMsgDef *gopb_schema_msg(const GoSchema *s, const char *full_name)
{
	for (int i = 0; i < go_spec_n_msgs; i++)
		if (str_eq(go_spec_msgs[i].name, full_name))
			return s->statics[i];
	return NULL;
}

/* ---- decoding ---- */

#define DEF(m) ((const GoMsgDef *)(m)->def)
#define ERR_DECODE "proto: cannot parse invalid wire-format data"
#define ERR_DEPTH "proto: exceeded maximum recursion depth"

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
	uint64_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | r->p[i];
	r->p += n;
	*out = v;
	return true;
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

static int field_index(const PbMsgDef *def, uint32_t num)
{
	int lo = 0, hi = def->n;
	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (def->f[mid].num < num)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < def->n && def->f[lo].num == num ? lo : -1;
}

static bool in_ext_ranges(const GoMessage *m, uint32_t num)
{
	for (int i = 0; i < m->n_ext_ranges; i++)
		if ((int64_t)num >= m->ext_ranges[i][0] && (int64_t)num < m->ext_ranges[i][1])
			return true;
	return false;
}

/* Skips a group whose start tag was read; content and clen receive its payload (without the end
 * tag). protowire.ConsumeGroup. */
static bool skip_group(PbReader *r, uint32_t num, const uint8_t **content, size_t *clen, int depth)
{
	const uint8_t *start = r->p;
	if (depth > GO_RECURSION_LIMIT || stack_exhausted())
		return false;
	for (;;) {
		const uint8_t *before = r->p;
		uint32_t n2;
		int wt2;
		if (!pb_read_tag(r, &n2, &wt2))
			return false;
		if (wt2 == WT_EGROUP) {
			if (n2 != num)
				return false;
			*content = start;
			*clen = (size_t)(before - start);
			return true;
		}
		const uint8_t *c;
		size_t cl;
		if (wt2 == WT_SGROUP) {
			if (!skip_group(r, n2, &c, &cl, depth + 1))
				return false;
		} else if (!pb_skip_value(r, n2, wt2, &c, &cl, 0)) {
			return false;
		}
	}
}

/* protowire.ConsumeFieldValue */
static bool skip_value(PbReader *r, uint32_t num, int wt, int depth)
{
	const uint8_t *c;
	size_t cl;
	if (wt == WT_SGROUP)
		return skip_group(r, num, &c, &cl, depth + 1);
	if (wt == WT_EGROUP)
		return false;
	return pb_skip_value(r, num, wt, &c, &cl, 0);
}

static bool utf8_valid(const char *p, size_t n)
{
	for (size_t i = 0; i < n;) {
		uint32_t r;
		int w = utf8_decode(p + i, n - i, &r);
		if (r == 0xfffd && w == 1)
			return false;
		i += (size_t)w;
	}
	return true;
}

static const char *decode(PbMsg *m, const uint8_t *p, size_t n, int depth);

static void clear_other_oneof_fields(PbMsg *m, int fi)
{
	const GoMsgDef *def = DEF(m);
	if (!def->oneof[fi])
		return;
	for (int i = 0; i < def->pb.n; i++)
		if (i != fi && def->oneof[i] == def->oneof[fi])
			m->slots[i].n = 0;
}

/* A map entry (unmarshalMap): the entry message gets the key and value as fields 1 and 2;
 * missing ones are filled in when encoding, unknown fields are dropped. */
static const char *decode_map_entry(PbMsg *e, const uint8_t *p, size_t n, int depth)
{
	const PbMsgDef *def = e->def;
	PbReader r = {p, p + n};
	while (r.p < r.end) {
		uint32_t num;
		int wt;
		if (!pb_read_tag(&r, &num, &wt))
			return ERR_DECODE;
		int fi = field_index(def, num);
		const PbField *f = fi >= 0 && (num == 1 || num == 2) ? &def->f[fi] : NULL;
		if (!f || wt != wire_type_of(f->type)) {
			if (!skip_value(&r, num, wt, depth))
				return ERR_DECODE;
			continue;
		}
		PbSlot *s = &e->slots[fi];
		if (f->type == PT_MESSAGE || f->type == PT_GROUP) {
			const uint8_t *v;
			size_t vn;
			if (f->type == PT_GROUP ? !skip_group(&r, num, &v, &vn, depth + 1) : !pb_skip_value(&r, num, wt, &v, &vn, 0))
				return ERR_DECODE;
			if (!s->n)
				slot_push(s, (PbVal){.m = pb_new(f->msg)});
			const char *err = decode(s->v[0].m, v, vn, depth + 1);
			if (err)
				return err;
		} else if (f->type == PT_STRING || f->type == PT_BYTES) {
			const uint8_t *v;
			size_t vn;
			if (!pb_skip_value(&r, num, wt, &v, &vn, 0))
				return ERR_DECODE;
			if (f->type == PT_STRING && (DEF(e)->flags[fi] & GOF_UTF8) && !utf8_valid((const char *)v, vn))
				return go_err("field %s contains invalid UTF-8", DEF(e)->names[fi] ? DEF(e)->names[fi] : "");
			PbVal x = {.s = {xstrndup((const char *)v, vn), vn}};
			s->n = 0;
			slot_push(s, x);
		} else {
			PbVal x;
			if (!read_scalar(&r, f->type, &x.u))
				return ERR_DECODE;
			s->n = 0;
			slot_push(s, x);
		}
	}
	return NULL;
}

static const char *decode(PbMsg *m, const uint8_t *p, size_t n, int depth)
{
	if (depth > GO_RECURSION_LIMIT || stack_exhausted())
		return ERR_DEPTH;
	const GoMsgDef *def = DEF(m);
	PbReader r = {p, p + n};
	while (r.p < r.end) {
		const uint8_t *start = r.p;
		uint32_t num;
		int wt;
		if (!pb_read_tag(&r, &num, &wt))
			return ERR_DECODE;
		int fi = field_index(&def->pb, num);
		/* dynamic messages resolve extensions only within the extension ranges */
		if (fi >= 0 && (def->flags[fi] & GOF_EXT) && def->dynamic && !in_ext_ranges(def->desc, num))
			fi = -1;
		const PbField *f = fi >= 0 ? &def->pb.f[fi] : NULL;
		int fwt = f ? wire_type_of(f->type) : -1;
		bool packed = f && f->label != PL_OPT && wt == WT_LEN && fwt != WT_LEN && fwt != WT_SGROUP;
		if (!f || (wt != fwt && !packed)) {
			if (!skip_value(&r, num, wt, depth))
				return ERR_DECODE;
			buf_add(&m->unknown, start, (size_t)(r.p - start));
			continue;
		}
		PbSlot *s = &m->slots[fi];
		uint8_t fl = def->flags[fi];
		if (packed) {
			const uint8_t *v;
			size_t vn;
			if (!pb_skip_value(&r, num, wt, &v, &vn, 0))
				return ERR_DECODE;
			PbReader pr = {v, v + vn};
			while (pr.p < pr.end) {
				PbVal x;
				if (!read_scalar(&pr, f->type, &x.u))
					return ERR_DECODE;
				slot_push(s, x);
			}
			continue;
		}
		if (fl & GOF_MAP) {
			const uint8_t *v;
			size_t vn;
			if (!pb_skip_value(&r, num, wt, &v, &vn, 0))
				return ERR_DECODE;
			PbMsg *e = pb_new(f->msg);
			const char *err = decode_map_entry(e, v, vn, depth + 1);
			if (err)
				return err;
			slot_push(s, (PbVal){.m = e});
			continue;
		}
		if (fwt == WT_LEN || fwt == WT_SGROUP) {
			const uint8_t *v;
			size_t vn;
			if (fwt == WT_SGROUP ? !skip_group(&r, num, &v, &vn, depth + 1) : !pb_skip_value(&r, num, wt, &v, &vn, 0))
				return ERR_DECODE;
			if (f->type == PT_MESSAGE || f->type == PT_GROUP) {
				PbMsg *sub;
				if (f->label == PL_OPT && s->n > 0) {
					sub = s->v[0].m; /* merge */
				} else {
					if (f->label == PL_OPT)
						clear_other_oneof_fields(m, fi);
					sub = pb_new(f->msg);
					slot_push(s, (PbVal){.m = sub});
				}
				const char *err = decode(sub, v, vn, depth + 1);
				if (err)
					return err;
			} else {
				if (f->type == PT_STRING && (fl & GOF_UTF8) && !utf8_valid((const char *)v, vn))
					return go_err("field %s contains invalid UTF-8", def->names[fi] ? def->names[fi] : "");
				PbVal x = {.s = {xstrndup((const char *)v, vn), vn}};
				if (f->label == PL_OPT) {
					clear_other_oneof_fields(m, fi);
					s->n = 0;
				}
				slot_push(s, x);
			}
			continue;
		}
		PbVal x;
		if (!read_scalar(&r, f->type, &x.u))
			return ERR_DECODE;
		if (f->label == PL_OPT) {
			clear_other_oneof_fields(m, fi);
			s->n = 0;
		}
		slot_push(s, x);
	}
	return NULL;
}

const char *gopb_decode(PbMsg *m, const uint8_t *p, size_t n)
{
	stack_base = (uintptr_t)__builtin_frame_address(0);
	return decode(m, p, n, 1);
}

/* ---- encoding ---- */

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
	case WT_I32: {
		int n = wire_type_of(type) == WT_I64 ? 8 : 4;
		uint8_t t[8];
		for (int i = 0; i < n; i++)
			t[i] = (uint8_t)(v >> (8 * i));
		buf_add(b, t, (size_t)n);
		break;
	}
	}
}

/* dynamicpb isSet for implicit-presence scalars */
static bool is_zero(int type, const PbVal *v)
{
	switch (type) {
	case PT_STRING:
	case PT_BYTES:
		return v->s.n == 0;
	case PT_DOUBLE:
		return v->u == 0; /* -0.0 is set (math.Signbit) */
	case PT_FLOAT:
		return (uint32_t)v->u == 0;
	default:
		return v->u == 0;
	}
}

static void encode_value(Buf *out, const PbField *f, const PbVal *v)
{
	if (f->type == PT_MESSAGE) {
		Buf tmp = {0};
		gopb_encode(v->m, &tmp);
		pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
	} else if (f->type == PT_GROUP) {
		pb_put_tag(out, f->num, WT_SGROUP);
		gopb_encode(v->m, out);
		pb_put_tag(out, f->num, WT_EGROUP);
	} else if (f->type == PT_STRING || f->type == PT_BYTES) {
		pb_put_bytes(out, f->num, v->s);
	} else {
		pb_put_tag(out, f->num, wire_type_of(f->type));
		put_scalar(out, f->type, v->u);
	}
}

typedef struct {
	PbMsg *e;
	const PbVal *key;
	int seq;
} MapEnt;

static int key_cmp(int type, const PbVal *a, const PbVal *b)
{
	switch (type) {
	case PT_STRING: {
		size_t n = a->s.n < b->s.n ? a->s.n : b->s.n;
		int c = memcmp(a->s.p, b->s.p, n);
		return c ? c : (a->s.n < b->s.n ? -1 : a->s.n > b->s.n);
	}
	case PT_INT32: case PT_INT64: case PT_SINT32: case PT_SINT64: case PT_SFIXED32: case PT_SFIXED64: {
		int64_t x = (int64_t)a->u, y = (int64_t)b->u;
		if (type == PT_SFIXED32)
			x = (int32_t)(uint32_t)a->u, y = (int32_t)(uint32_t)b->u;
		return x < y ? -1 : x > y;
	}
	default:
		return a->u < b->u ? -1 : a->u > b->u;
	}
}

/* The default of a map entry field (filedesc defaultValue.get): zero, or the first enum value. */
static PbVal map_default(const GoMsgDef *ed, int num, int type)
{
	PbVal v = {0};
	if (type == PT_STRING || type == PT_BYTES) {
		v.s = (Str){"", 0};
		return v;
	}
	for (int i = 0; ed->desc && i < ed->desc->n_fields; i++) {
		const GoField *fd = ed->desc->fields[i];
		if (fd->number == num && fd->kind == PT_ENUM && fd->enum_type && fd->enum_type->n_values > 0)
			v.u = (uint64_t)(int64_t)fd->enum_type->values[0]->number;
	}
	return v;
}

static void encode_map(Buf *out, const PbField *f, const PbSlot *s)
{
	const GoMsgDef *ed = (const GoMsgDef *)f->msg;
	int ki = field_index(&ed->pb, 1), vi = field_index(&ed->pb, 2);
	if (ki < 0 || vi < 0)
		return;
	const PbField *kf = &ed->pb.f[ki], *vf = &ed->pb.f[vi];
	PbVal zero_key = map_default(ed, 1, kf->type);
	/* last entry wins per key, then GenericKeyOrder */
	MapEnt *v = xalloc(sizeof *v * (size_t)s->n);
	int n = 0;
	for (int i = 0; i < s->n; i++) {
		PbMsg *e = s->v[i].m;
		const PbVal *key = e->slots[ki].n ? &e->slots[ki].v[0] : &zero_key;
		int j = 0;
		for (; j < n; j++)
			if (!key_cmp(kf->type, v[j].key, key))
				break;
		v[j] = (MapEnt){e, key, i};
		if (j == n)
			n++;
	}
	for (int i = 1; i < n; i++) {
		MapEnt x = v[i];
		int j = i - 1;
		for (; j >= 0 && key_cmp(kf->type, &v[j].key[0], x.key) > 0; j--)
			v[j + 1] = v[j];
		v[j + 1] = x;
	}
	for (int i = 0; i < n; i++) {
		PbMsg *e = v[i].e;
		Buf entry = {0};
		encode_value(&entry, kf, v[i].key);
		if (e->slots[vi].n) {
			encode_value(&entry, vf, &e->slots[vi].v[0]);
		} else if (vf->type == PT_MESSAGE || vf->type == PT_GROUP) {
			PbVal empty = {.m = pb_new(vf->msg)};
			encode_value(&entry, vf, &empty);
		} else {
			PbVal d = map_default(ed, 2, vf->type);
			encode_value(&entry, vf, &d);
		}
		pb_put_bytes(out, f->num, (Str){entry.p, entry.n});
	}
}

void gopb_encode(const PbMsg *m, Buf *out)
{
	const GoMsgDef *def = DEF(m);
	for (int k = 0; k < def->pb.n; k++) {
		int i = def->order[k];
		const PbField *f = &def->pb.f[i];
		const PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		uint8_t fl = def->flags[i];
		if (fl & GOF_MAP) {
			encode_map(out, f, s);
			continue;
		}
		if (f->label == PL_OPT && (fl & GOF_IMPLICIT) && is_zero(f->type, &s->v[0]))
			continue;
		if (f->label == PL_PACKED) {
			Buf tmp = {0};
			for (int j = 0; j < s->n; j++)
				put_scalar(&tmp, f->type, s->v[j].u);
			pb_put_bytes(out, f->num, (Str){tmp.p, tmp.n});
			continue;
		}
		for (int j = 0; j < s->n; j++)
			encode_value(out, f, &s->v[j]);
	}
	buf_add(out, m->unknown.p, m->unknown.n);
}

/* ---- clone, clear, strip ---- */

PbMsg *gopb_clone(const PbMsg *m)
{
	PbMsg *c = pb_new(m->def);
	for (int i = 0; i < m->def->n; i++) {
		const PbSlot *s = &m->slots[i];
		const PbField *f = &m->def->f[i];
		for (int j = 0; j < s->n; j++) {
			PbVal v = s->v[j];
			if (f->type == PT_MESSAGE || f->type == PT_GROUP)
				v.m = gopb_clone(v.m);
			slot_push(&c->slots[i], v);
		}
	}
	buf_add(&c->unknown, m->unknown.p, m->unknown.n);
	return c;
}

void gopb_clear(PbMsg *m, uint32_t num)
{
	int i = field_index(m->def, num);
	if (i >= 0)
		m->slots[i].n = 0;
}

void gopb_strip_source_retention(PbMsg *m)
{
	const GoMsgDef *def = DEF(m);
	for (int i = 0; i < def->pb.n; i++) {
		PbSlot *s = &m->slots[i];
		if (!s->n)
			continue;
		if (def->flags[i] & GOF_SOURCE) {
			s->n = 0;
			continue;
		}
		const PbField *f = &def->pb.f[i];
		if (f->type != PT_MESSAGE && f->type != PT_GROUP)
			continue;
		for (int j = 0; j < s->n; j++) {
			if (def->flags[i] & GOF_MAP) {
				/* protorange visits the map values */
				PbMsg *e = s->v[j].m;
				int vi = field_index(e->def, 2);
				const PbField *vf = vi >= 0 ? &e->def->f[vi] : NULL;
				if (vf && (vf->type == PT_MESSAGE || vf->type == PT_GROUP) && e->slots[vi].n)
					gopb_strip_source_retention(e->slots[vi].v[0].m);
			} else {
				gopb_strip_source_retention(s->v[j].m);
			}
		}
	}
}
