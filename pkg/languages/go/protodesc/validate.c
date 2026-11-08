/* Copyright 2019 The Go Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file. */

/* Ported from google.golang.org/protobuf v1.36.12 reflect/protodesc/desc_validate.go, with the
 * range and name checks of internal/filedesc/desc_list.go and strs.EnumValueName/MapEntryName.
 * Map iteration order in Go (duplicate reserved names) is replaced by declaration order. */
#include "internal.h"

#include <string.h>

#define MAX_VALID_NUMBER 536870911
#define FIRST_RESERVED 19000
#define LAST_RESERVED 19999

static bool number_is_valid(int64_t n) { return n >= 1 && n <= MAX_VALID_NUMBER && !(n >= FIRST_RESERVED && n <= LAST_RESERVED); }

/* Names.CheckValid: duplicate names */
static COLD const char *names_check(const PbMsg *owner, uint32_t num)
{
	int n = pb_count(owner, num);
	for (int i = 0; i < n; i++)
		for (int j = 0; j < i; j++)
			if (str_equal(pb_bytes_at(owner, num, i), pb_bytes_at(owner, num, j)))
				return xcat("duplicate name: ", go_quote_str(pb_bytes_at(owner, num, i)));
	return NULL;
}

static COLD bool names_has(const PbMsg *owner, uint32_t num, const char *name)
{
	for (int i = 0; i < pb_count(owner, num); i++)
		if (str_eqc(pb_bytes_at(owner, num, i), name))
			return true;
	return false;
}

typedef struct {
	int64_t start, end; /* inclusive */
} Range;

static COLD const char *range_string(Range r)
{
	if (r.start == r.end)
		return go_itoa(r.start);
	return xcat(go_itoa(r.start), " to ", go_itoa(r.end));
}

/* ranges of a message (start, end) sub-messages; field ranges have an exclusive end */
static COLD Range *ranges_of(const PbMsg *owner, uint32_t num, bool exclusive_end, int *n)
{
	*n = pb_count(owner, num);
	Range *r = xalloc(sizeof *r * (size_t)(*n ? *n : 1));
	for (int i = 0; i < *n; i++) {
		const PbMsg *m = pb_msg_at(owner, num, i);
		r[i].start = (int32_t)pb_int(m, 1, 0);
		r[i].end = (int32_t)pb_int(m, 2, 0) - (exclusive_end ? 1 : 0);
	}
	/* sorted by start */
	for (int i = 1; i < *n; i++) {
		Range x = r[i];
		int j = i - 1;
		for (; j >= 0 && r[j].start > x.start; j--)
			r[j + 1] = r[j];
		r[j + 1] = x;
	}
	return r;
}

static COLD bool ranges_has(const Range *r, int n, int64_t v)
{
	for (int i = 0; i < n; i++)
		if (v >= r[i].start && v <= r[i].end)
			return true;
	return false;
}

static bool valid_field_number(int64_t n, bool message_set) { return n >= 1 && (n <= MAX_VALID_NUMBER || message_set); }

static COLD const char *field_ranges_check(const Range *r, int n, bool message_set)
{
	for (int i = 0; i < n; i++) {
		if (!valid_field_number(r[i].start, message_set))
			return xcat("invalid field number: ", go_itoa(r[i].start));
		if (!valid_field_number(r[i].end, message_set))
			return xcat("invalid field number: ", go_itoa(r[i].end));
		if (!(r[i].start <= r[i].end))
			return xcat("invalid range: ", range_string(r[i]));
		if (i > 0 && !(r[i - 1].end < r[i].start))
			return xcat("overlapping ranges: ", range_string(r[i - 1]), " with ", range_string(r[i]));
	}
	return NULL;
}

static COLD const char *enum_ranges_check(const Range *r, int n)
{
	for (int i = 0; i < n; i++) {
		if (!(r[i].start <= r[i].end))
			return xcat("invalid range: ", range_string(r[i]));
		if (i > 0 && !(r[i - 1].end < r[i].start))
			return xcat("overlapping ranges: ", range_string(r[i - 1]), " with ", range_string(r[i]));
	}
	return NULL;
}

static COLD const char *ranges_overlap(const Range *p, int np, const Range *q, int nq)
{
	for (int pi = 0, qi = 0; pi < np && qi < nq;) {
		Range rp = p[pi], rq = q[qi];
		if (!(rp.end < rq.start || rq.end < rp.start))
			return xcat("overlapping ranges: ", range_string(rp), " with ", range_string(rq));
		if (rp.start < rq.start)
			pi++;
		else
			qi++;
	}
	return NULL;
}

static char to_upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
static char to_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

/* strs.EnumValueName (names are ASCII) */
static COLD const char *enum_value_name(const char *s)
{
	Buf b = {0};
	bool upper_next = true;
	for (; *s; s++) {
		if (*s == '_')
			upper_next = true;
		else if (upper_next)
			buf_addc(&b, to_upper(*s)), upper_next = false;
		else
			buf_addc(&b, to_lower(*s));
	}
	return buf_str(&b);
}

/* strs.MapEntryName */
static COLD const char *map_entry_name(const char *s)
{
	Buf b = {0};
	bool upper_next = true;
	for (; *s; s++) {
		if (*s == '_')
			upper_next = true;
		else if (upper_next)
			buf_addc(&b, to_upper(*s)), upper_next = false;
		else
			buf_addc(&b, *s);
	}
	buf_adds(&b, "Entry");
	return buf_str(&b);
}

/* strs.TrimEnumPrefix (ASCII names) */
static COLD const char *trim_enum_prefix(const char *s0, const char *prefix)
{
	const char *s = s0;
	while (*s && *prefix) {
		if (*s == '_') {
			s++;
			continue;
		}
		if (to_lower(*s) != *prefix)
			return s0;
		s++, prefix++;
	}
	if (*prefix)
		return s0;
	while (*s == '_')
		s++;
	return *s ? s : s0;
}

static COLD const char *lower_no_underscore(const char *s)
{
	Buf b = {0};
	for (; *s; s++)
		if (*s != '_')
			buf_addc(&b, to_lower(*s));
	return buf_str(&b);
}

COLD const char *go_validate_enums(GoEnum **es, int n)
{
	for (int i = 0; i < n; i++) {
		GoEnum *e = es[i];
		PbMsg *ed = NULL;
		/* the EnumDescriptorProto: the enum's parent proto list */
		const GoDesc *p = e->d.parent;
		const PbMsg *owner = p->kind == GD_FILE ? ((const GoFile *)p)->proto : ((const GoMessage *)p)->proto;
		ed = pb_msg_at(owner, p->kind == GD_FILE ? GENID_FileDescriptorProto_EnumType_field_number : GENID_DescriptorProto_EnumType_field_number,
			       e->d.index);
		const char *err = names_check(ed, GENID_EnumDescriptorProto_ReservedName_field_number);
		if (err)
			return go_err("enum %s reserved names has %s", go_quote(e->d.full_name), err);
		int nr;
		Range *rr = ranges_of(ed, GENID_EnumDescriptorProto_ReservedRange_field_number, false, &nr);
		if ((err = enum_ranges_check(rr, nr)))
			return go_err("enum %s reserved ranges has %s", go_quote(e->d.full_name), err);
		if (e->n_values == 0)
			return go_err("enum %s must contain at least one value declaration", go_quote(e->d.full_name));
		bool allow_alias = pb_int(e->d.options, GENID_EnumOptions_AllowAlias_field_number, 0) != 0;
		bool found_alias = false;
		for (int j = 0; j < e->n_values; j++) {
			GoEnumValue *v1 = e->values[j], *v2 = go_enum_value_by_number(e, v1->number);
			if (v1 != v2) {
				found_alias = true;
				if (!allow_alias)
					return go_err("enum %s has conflicting non-aliased values on number %d: %s with %s",
						      go_quote(e->d.full_name), v1->number, go_quote(v1->d.name), go_quote(v2->d.name));
			}
		}
		if (allow_alias && !found_alias)
			return go_err("enum %s allows aliases, but none were found", go_quote(e->d.full_name));
		if (!go_enum_is_closed(e)) {
			if (e->values[0]->number != 0)
				return go_err("enum %s using open semantics must have zero number for the first value",
					      go_quote(e->values[0]->d.full_name));
			const char *prefix = lower_no_underscore(e->d.name);
			Map names = {0};
			for (int j = 0; j < e->n_values; j++) {
				GoEnumValue *v1 = e->values[j];
				const char *s = enum_value_name(trim_enum_prefix(v1->d.name, prefix));
				GoEnumValue *v2 = map_get(&names, s);
				if (v2 && v1->number != v2->number)
					return go_err("enum %s using open semantics has conflict: %s with %s", go_quote(e->d.full_name),
						      go_quote(v1->d.name), go_quote(v2->d.name));
				map_put(&names, s, v1);
			}
		}
		for (int j = 0; j < e->n_values; j++) {
			GoEnumValue *v = e->values[j];
			PbMsg *vd = pb_msg_at(ed, GENID_EnumDescriptorProto_Value_field_number, j);
			if (!pb_has(vd, GENID_EnumValueDescriptorProto_Number_field_number))
				return go_err("enum value %s must have a specified number", go_quote(v->d.full_name));
			if (names_has(ed, GENID_EnumDescriptorProto_ReservedName_field_number, v->d.name))
				return go_err("enum value %s must not use reserved name", go_quote(v->d.full_name));
			if (ranges_has(rr, nr, v->number))
				return go_err("enum value %s must not use reserved number %d", go_quote(v->d.full_name), v->number);
		}
	}
	return NULL;
}

static COLD bool is_packable(const GoField *f)
{
	switch (f->kind) {
	case PT_STRING: case PT_BYTES: case PT_MESSAGE: case PT_GROUP: return false;
	}
	return go_field_is_list(f);
}

static COLD const char *check_valid_group(const GoFile *file, const GoField *f)
{
	const GoMessage *md = f->message_type;
	if (f->kind != PT_GROUP)
		return NULL;
	if (file->edition == GENID_Edition_EDITION_PROTO3)
		return "invalid under proto3 semantics";
	if (!md)
		return "message must be resolvable";
	if (file->edition < GENID_Edition_EDITION_2023) {
		const char *fp = f->d.full_name, *mp = md->d.full_name;
		const char *fdot = strrchr(fp, '.'), *mdot = strrchr(mp, '.');
		size_t fl = fdot ? (size_t)(fdot - fp) : 0, ml = mdot ? (size_t)(mdot - mp) : 0;
		if (fl != ml || memcmp(fp, mp, fl) != 0)
			return "message and field must be declared in the same scope";
		if (!(md->d.name[0] >= 'A' && md->d.name[0] <= 'Z'))
			return "message name must start with an uppercase";
		Buf b = {0};
		for (const char *s = md->d.name; *s; s++)
			buf_addc(&b, to_lower(*s));
		if (!str_eq(f->d.name, buf_str(&b)))
			return "field name must be lowercased form of the message name";
	}
	return NULL;
}

static COLD const char *check_valid_map(const GoField *f)
{
	const GoMessage *md = f->message_type;
	if (!md || !md->is_map_entry)
		return NULL;
	const char *fp = f->d.full_name, *mp = md->d.full_name;
	const char *fdot = strrchr(fp, '.'), *mdot = strrchr(mp, '.');
	size_t fl = fdot ? (size_t)(fdot - fp) : 0, ml = mdot ? (size_t)(mdot - mp) : 0;
	if (fl != ml || memcmp(fp, mp, fl) != 0)
		return "message and field must be declared in the same scope";
	if (!str_eq(md->d.name, map_entry_name(f->d.name)))
		return "incorrect implicit map entry name";
	if (f->cardinality != GO_REPEATED)
		return "field must be repeated";
	if (md->n_fields != 2)
		return "message must have exactly two fields";
	if (md->n_ext_ranges > 0)
		return "message must not have any extension ranges";
	if (md->n_enums + md->n_messages + md->n_extensions > 0)
		return "message must not have any nested declarations";
	const GoField *kf = md->fields[0], *vf = md->fields[1];
	if (!str_eq(kf->d.name, "key") || kf->number != 1 || kf->cardinality != GO_OPTIONAL || kf->oneof || kf->has_default)
		return "invalid key field";
	if (!str_eq(vf->d.name, "value") || vf->number != 2 || vf->cardinality != GO_OPTIONAL || vf->oneof || vf->has_default)
		return "invalid value field";
	switch (kf->kind) {
	case PT_BOOL: case PT_INT32: case PT_SINT32: case PT_SFIXED32: case PT_INT64: case PT_SINT64: case PT_SFIXED64:
	case PT_UINT32: case PT_FIXED32: case PT_UINT64: case PT_FIXED64: case PT_STRING:
		break;
	default:
		return xcat("invalid key kind: ", go_kind_string(kf->kind));
	}
	if (vf->enum_type && vf->enum_type->n_values > 0 && vf->enum_type->values[0]->number != 0)
		return "map enum value must have zero number for the first value";
	return NULL;
}

COLD const char *go_validate_messages(GoFile *file, GoMessage **ms, int n)
{
	bool is_proto3 = file->edition == GENID_Edition_EDITION_PROTO3;
	for (int i = 0; i < n; i++) {
		GoMessage *m = ms[i];
		PbMsg *md = m->proto;
		bool is_message_set = m->is_message_set;
		const char *err = names_check(md, GENID_DescriptorProto_ReservedName_field_number);
		if (err)
			return go_err("message %s reserved names has %s", go_quote(m->d.full_name), err);
		int nr, nx;
		Range *rr = ranges_of(md, GENID_DescriptorProto_ReservedRange_field_number, true, &nr);
		Range *xr = ranges_of(md, GENID_DescriptorProto_ExtensionRange_field_number, true, &nx);
		if ((err = field_ranges_check(rr, nr, is_message_set)))
			return go_err("message %s reserved ranges has %s", go_quote(m->d.full_name), err);
		if ((err = field_ranges_check(xr, nx, is_message_set)))
			return go_err("message %s extension ranges has %s", go_quote(m->d.full_name), err);
		if ((err = ranges_overlap(rr, nr, xr, nx)))
			return go_err("message %s reserved and extension ranges has %s", go_quote(m->d.full_name), err);
		for (int j = 0; j < m->n_fields; j++) {
			GoField *f1 = m->fields[j];
			for (int k = 0; k < j; k++)
				if (m->fields[k]->number == f1->number)
					return go_err("message %s has conflicting fields: %s with %s", go_quote(m->d.full_name),
						      go_quote(f1->d.name), go_quote(m->fields[k]->d.name));
		}
		if (is_message_set)
			return go_err("message %s is a MessageSet, which is a legacy proto1 feature that is no longer supported",
				      go_quote(m->d.full_name));
		if (is_proto3 && m->n_ext_ranges > 0)
			return go_err("message %s using proto3 semantics cannot have extension ranges", go_quote(m->d.full_name));
		for (int j = 0; j < m->n_fields; j++) {
			GoField *f = m->fields[j];
			const char *q = go_quote(f->d.full_name);
			if (names_has(md, GENID_DescriptorProto_ReservedName_field_number, f->d.name))
				return go_err("message field %s must not use reserved name", q);
			if (!number_is_valid(f->number))
				return go_err("message field %s has an invalid number: %d", q, f->number);
			if (f->cardinality < 1 || f->cardinality > 3)
				return go_err("message field %s has an invalid cardinality: %d", q, f->cardinality);
			if (ranges_has(rr, nr, f->number))
				return go_err("message field %s must not use reserved number %d", q, f->number);
			if (ranges_has(xr, nx, f->number))
				return go_err("message field %s with number %d in extension range", q, f->number);
			if (pb_has(f->proto, GENID_FieldDescriptorProto_Extendee_field_number))
				return go_err("message field %s may not have extendee: %s", q,
					      go_quote_str(pb_bytes(f->proto, GENID_FieldDescriptorProto_Extendee_field_number)));
			if (f->is_proto3_optional) {
				if (!is_proto3)
					return go_err("message field %s under proto3 optional semantics must be specified in the proto3 syntax", q);
				if (f->cardinality != GO_OPTIONAL)
					return go_err("message field %s under proto3 optional semantics must have optional cardinality", q);
				if (f->oneof && f->oneof->n_fields != 1)
					return go_err("message field %s under proto3 optional semantics must be within a single element oneof", q);
			}
			if (go_field_is_packed(f) && !is_packable(f))
				return go_err("message field %s is not packable", q);
			if ((err = check_valid_group(file, f)))
				return go_err("message field %s is an invalid group: %s", q, err);
			if ((err = check_valid_map(f)))
				return go_err("message field %s is an invalid map: %s", q, err);
			if (is_proto3) {
				if (f->cardinality == GO_REQUIRED)
					return go_err("message field %s using proto3 semantics cannot be required", q);
				if (f->enum_type && go_enum_is_closed(f->enum_type))
					return go_err("message field %s using proto3 semantics may only depend on open enums", q);
			}
			if (f->cardinality == GO_OPTIONAL && !go_field_has_presence(f) && f->enum_type && go_enum_is_closed(f->enum_type))
				return go_err("message field %s with implicit presence may only use open enums", q);
		}
		bool seen_synthetic = false;
		for (int j = 0; j < m->n_oneofs; j++) {
			GoOneof *o = m->oneofs[j];
			if (o->n_fields == 0)
				return go_err("message oneof %s must contain at least one field declaration", go_quote(o->d.full_name));
			if (o->n_fields - 1 != o->fields[o->n_fields - 1]->d.index - o->fields[0]->d.index)
				return go_err("message oneof %s must have consecutively declared fields", go_quote(o->d.full_name));
			if (go_oneof_is_synthetic(o)) {
				seen_synthetic = true;
				continue;
			}
			if (seen_synthetic)
				return go_err("message oneof %s must be declared before synthetic oneofs", go_quote(o->d.full_name));
			for (int k = 0; k < o->n_fields; k++)
				if (o->fields[k]->cardinality != GO_OPTIONAL)
					return go_err("message field %s belongs in a oneof and must be optional", go_quote(o->fields[k]->d.full_name));
		}
		if ((err = go_validate_enums(m->enums, m->n_enums)))
			return err;
		if ((err = go_validate_messages(file, m->messages, m->n_messages)))
			return err;
		if ((err = go_validate_extensions(file, m->extensions, m->n_extensions)))
			return err;
	}
	return NULL;
}

COLD const char *go_validate_extensions(GoFile *file, GoField **xs, int n)
{
	static const char *const options[] = {
		"google.protobuf.FileOptions", "google.protobuf.EnumOptions", "google.protobuf.EnumValueOptions",
		"google.protobuf.MessageOptions", "google.protobuf.FieldOptions", "google.protobuf.OneofOptions",
		"google.protobuf.ExtensionRangeOptions", "google.protobuf.ServiceOptions", "google.protobuf.MethodOptions",
	};
	for (int i = 0; i < n; i++) {
		GoField *x = xs[i];
		const char *q = go_quote(x->d.full_name);
		if (x->number < 0 || (x->number >= FIRST_RESERVED && x->number <= LAST_RESERVED))
			return go_err("extension field %s has an invalid number: %d", q, x->number);
		if (x->cardinality < 1 || x->cardinality > 3 || x->cardinality == GO_REQUIRED)
			return go_err("extension field %s has an invalid cardinality: %d", q, x->cardinality);
		if (pb_has(x->proto, GENID_FieldDescriptorProto_JsonName_field_number)) {
			Str jn = pb_bytes(x->proto, GENID_FieldDescriptorProto_JsonName_field_number);
			Buf b = {0};
			bool was_underscore = false;
			for (const char *s = x->d.name; *s; s++) {
				char ch = *s;
				if (ch != '_') {
					if (was_underscore && ch >= 'a' && ch <= 'z')
						ch = (char)(ch - 32);
					buf_addc(&b, ch);
				}
				was_underscore = ch == '_';
			}
			if (!str_equal(jn, buf_view(&b)) && !(jn.n == 0 && b.n == 0))
				return go_err("extension field %s may not have an explicitly set JSON name: %s", q, go_quote_str(jn));
		}
		if (pb_has(x->proto, GENID_FieldDescriptorProto_OneofIndex_field_number))
			return go_err("extension field %s may not be part of a oneof", q);
		const GoMessage *md = x->containing;
		if (md) {
			bool in = false;
			for (int k = 0; k < md->n_ext_ranges; k++)
				if (x->number >= md->ext_ranges[k][0] && x->number < md->ext_ranges[k][1])
					in = true;
			if (!in)
				return go_err("extension field %s extends %s with non-extension field number: %d", q, go_quote(md->d.full_name),
					      x->number);
			bool is_message_set = pb_int(md->d.options, GENID_MessageOptions_MessageSetWireFormat_field_number, 0) != 0;
			if (is_message_set && !((x->kind == 0 || x->kind == PT_MESSAGE) && x->cardinality == GO_OPTIONAL))
				return go_err("extension field %s extends MessageSet and must be an optional message", q);
			if (!is_message_set && !number_is_valid(x->number))
				return go_err("extension field %s has an invalid number: %d", q, x->number);
		}
		if (go_field_is_packed(x) && !is_packable(x))
			return go_err("extension field %s is not packable", q);
		const char *err = check_valid_group(file, x);
		if (err)
			return go_err("extension field %s is an invalid group: %s", q, err);
		if (x->message_type && x->message_type->is_map_entry)
			return go_err("extension field %s cannot be a map entry", q);
		if (file->edition == GENID_Edition_EDITION_PROTO3) {
			bool ok = false;
			for (int k = 0; k < (int)(sizeof options / sizeof *options); k++)
				if (md && str_eq(md->d.full_name, options[k]))
					ok = true;
			if (!ok)
				return go_err("extension field %s cannot be declared in proto3 unless extended descriptor options", q);
		}
	}
	return NULL;
}
