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

/* The descriptor model of protobuf-py v0.7.0: _file_registry.py (add_file and the feature
 * resolution), _registry.py, _bootstrap.py (edition defaults), _descriptors.py and _names.py.
 *
 * Fields of descriptor.proto read with their proto2 defaults as protobuf-py gives them: an unset
 * FieldDescriptorProto.label is LABEL_OPTIONAL and an unset type TYPE_DOUBLE (the first enum
 * values). */
#include "protobuf_py.h"

#include <string.h>

/* descriptor.proto field numbers beyond pb.h */
enum {
	SDP_OPTIONS = 3,
	/* options: deprecated and features */
	FILEOPT_DEPRECATED = 23, FILEOPT_FEATURES = 50,
	MSGOPT_DEPRECATED = 3, MSGOPT_MAP_ENTRY = 7, MSGOPT_FEATURES = 12,
	FIELDOPT_PACKED = 2, FIELDOPT_DEPRECATED = 3, FIELDOPT_FEATURES = 21,
	ENUMOPT_DEPRECATED = 3, ENUMOPT_FEATURES = 7,
	EVOPT_DEPRECATED = 1,
	SVCOPT_DEPRECATED = 33, METHODOPT_DEPRECATED = 33,
};

/* FeatureSet fields (_FeatureKey) */
enum { FEATURE_FIELD_PRESENCE = 1, FEATURE_ENUM_TYPE = 2, FEATURE_REPEATED_FIELD_ENCODING = 3, FEATURE_MESSAGE_ENCODING = 5 };
enum { ENUM_TYPE_OPEN = 1, REPEATED_FIELD_ENCODING_PACKED = 1, MESSAGE_ENCODING_DELIMITED = 2 };

/* _bootstrap._feature_defaults of the features the model resolves: field_presence, enum_type,
 * repeated_field_encoding, message_encoding (by FeatureSet field number - 1; 0 unused). */
static const struct {
	int edition;
	uint8_t v[5];
} feature_defaults[] = {
	{900, {1, 2, 2, 0, 1}},  /* LEGACY */
	{999, {2, 1, 1, 0, 1}},  /* PROTO3 */
	{1000, {1, 1, 1, 0, 1}}, /* EDITION_2023 */
	{1001, {1, 1, 1, 0, 1}}, /* EDITION_2024 */
	{1002, {1, 1, 1, 0, 1}}, /* EDITION_2026 */
};

/* A name read from a descriptor: names with NUL are not supported. */
static const char *name_of(const PbMsg *m, uint32_t num)
{
	Str s = pb_bytes(m, num);
	if (memchr(s.p, 0, s.n))
		fail("unsupported by protoc-gen-uni: a name with a NUL character");
	return s.p;
}

static bool opt_bool(const PbMsg *proto, uint32_t opt_num, uint32_t num)
{
	return pb_int(pb_msg(proto, opt_num), num, 0) != 0;
}

static const char *strip_dot(const char *s) { return *s == '.' ? s + 1 : s; }

/* ---- registry ---- */

PbpyDescFile *pbpy_registry_file(const PbpyRegistry *r, const char *name) { return map_get(&r->files, name); }

void *pbpy_registry_type(const PbpyRegistry *r, const char *type_name, PbpyKind kind)
{
	PbpyDesc *d = map_get(&r->types, type_name);
	return d && d->kind == kind ? d : NULL;
}

static void reg_add_type(PbpyRegistry *r, const char *type_name, void *desc) { map_put(&r->types, type_name, desc); }

/* ---- small helpers ---- */

const char *pbpy_desc_name(const void *desc)
{
	switch (((const PbpyDesc *)desc)->kind) {
	case PBPY_FILE: return ((const PbpyDescFile *)desc)->name;
	case PBPY_MESSAGE: return ((const PbpyDescMessage *)desc)->name;
	case PBPY_FIELD: return ((const PbpyDescField *)desc)->name;
	case PBPY_ONEOF: return ((const PbpyDescOneof *)desc)->name;
	case PBPY_ENUM: return ((const PbpyDescEnum *)desc)->name;
	case PBPY_ENUM_VALUE: return ((const PbpyDescEnumValue *)desc)->name;
	case PBPY_EXTENSION: return ((const PbpyDescExtension *)desc)->name;
	case PBPY_SERVICE: return ((const PbpyDescService *)desc)->name;
	case PBPY_METHOD: return ((const PbpyDescMethod *)desc)->name;
	}
	return "";
}

const PbpyFieldValue *pbpy_field_value(const void *field)
{
	if (((const PbpyDesc *)field)->kind == PBPY_FIELD)
		return &((const PbpyDescField *)field)->value;
	return &((const PbpyDescExtension *)field)->value;
}

const char *pbpy_scalar_name(int scalar)
{
	static const char *const names[] = {
		"", "double", "float", "int64", "uint64", "int32", "fixed64", "fixed32", "bool", "string",
		"group", "message", "bytes", "uint32", "enum", "sfixed32", "sfixed64", "sint32", "sint64",
	};
	return scalar > 0 && scalar <= 18 ? names[scalar] : "";
}

/* _names.proto_camel_case */
COLD char *pbpy_proto_camel_case(const char *snake)
{
	Buf b = {0};
	bool first = true;
	for (const char *p = snake;;) {
		const char *u = strchr(p, '_');
		size_t n = u ? (size_t)(u - p) : strlen(p);
		if (!first && n) {
			unsigned char c = (unsigned char)*p;
			if (c >= 0x80)
				fail("unsupported by protoc-gen-uni: str.upper() of a non-ASCII character in a field name");
			buf_addc(&b, c >= 'a' && c <= 'z' ? (char)(c - 32) : (char)c);
			buf_add(&b, p + 1, n - 1);
		} else {
			buf_add(&b, p, n);
		}
		first = false;
		if (!u)
			break;
		p = u + 1;
	}
	return xstrdup(buf_str(&b));
}

/* ---- features ---- */

/* proto.options.features.<num> if set and not 0 */
static int own_feature(const PbMsg *proto, uint32_t opt_num, uint32_t features_num, int num)
{
	return (int)pb_int(pb_msg(pb_msg(proto, opt_num), features_num), (uint32_t)num, 0);
}

static int file_feature(const PbpyDescFile *f, int num)
{
	int v = own_feature(f->proto, FDP_OPTIONS, FILEOPT_FEATURES, num);
	if (v)
		return v;
	/* the closest entry at or before the edition (bisect_right) */
	int idx = -1;
	for (int i = 0; i < (int)(sizeof feature_defaults / sizeof *feature_defaults); i++)
		if (feature_defaults[i].edition <= f->edition)
			idx = i;
	if (idx < 0)
		fail("ValueError: no feature defaults for edition %d", f->edition);
	return feature_defaults[idx].v[num - 1];
}

static int message_feature(const PbpyDescMessage *m, int num)
{
	for (; m; m = m->parent) {
		int v = own_feature(m->proto, DP_OPTIONS, MSGOPT_FEATURES, num);
		if (v)
			return v;
		if (!m->parent)
			return file_feature(m->file, num);
	}
	return 0;
}

/* _resolve_feature(name, (proto, parent)) with parent a message, or the file if msg is NULL */
static int resolve_feature(const PbMsg *proto, uint32_t opt_num, uint32_t features_num, const PbpyDescMessage *msg,
	const PbpyDescFile *file, int num)
{
	int v = own_feature(proto, opt_num, features_num, num);
	if (v)
		return v;
	return msg ? message_feature(msg, num) : file_feature(file, num);
}

static int field_feature(const PbMsg *proto, const PbpyDescMessage *msg, const PbpyDescFile *file, int num)
{
	return resolve_feature(proto, FD_OPTIONS, FIELDOPT_FEATURES, msg, file, num);
}

COLD int pbpy_file_edition(const PbMsg *proto)
{
	const char *syntax = pb_str(proto, FDP_SYNTAX);
	if (!*syntax || str_eq(syntax, "proto2"))
		return PBPY_EDITION_PROTO2;
	if (str_eq(syntax, "proto3"))
		return PBPY_EDITION_PROTO3;
	if (str_eq(syntax, "editions")) {
		int e = (int)pb_int(proto, FDP_EDITION, 0);
		if (e == PBPY_EDITION_UNSTABLE)
			return PBPY_MAXIMUM_EDITION;
		if (e > PBPY_MAXIMUM_EDITION)
			fail("ValueError: %s: unsupported edition: %d", pb_str(proto, FDP_NAME), e);
		return e;
	}
	fail("ValueError: %s: unsupported syntax", pb_str(proto, FDP_NAME));
}

/* ---- building ---- */

typedef struct {
	Map map;   /* type name -> message */
	Vec order; /* type names, in insertion order */
	Map kv;    /* entry address -> PbpyFieldValue with its key and value (cache) */
} MapEntries;

static const char *type_name_of(const char *name, const PbpyDescMessage *parent, const PbpyDescFile *file)
{
	if (parent)
		return xcat(parent->type_name, ".", name);
	const char *pkg = pb_str(file->proto, FDP_PACKAGE);
	return *pkg ? xcat(pkg, ".", name) : xstrdup(name);
}

/* _pascal_to_upper_snake_case: re.sub("([^_])([A-Z][a-z]+)", r"\1_\2"), then
 * re.sub("([a-z])([A-Z])", r"\1_\2"), then upper(). */
static char *pascal_to_upper_snake_case(const char *text)
{
	for (const char *p = text; *p; p++)
		if ((unsigned char)*p >= 0x80)
			fail("unsupported by protoc-gen-uni: str.upper() of a non-ASCII enum name");
	Buf s1 = {0};
	size_t n = strlen(text);
	for (size_t i = 0; i < n;) {
		if (text[i] != '_' && i + 2 < n && text[i + 1] >= 'A' && text[i + 1] <= 'Z' && text[i + 2] >= 'a' &&
			text[i + 2] <= 'z') {
			size_t j = i + 2;
			while (j < n && text[j] >= 'a' && text[j] <= 'z')
				j++;
			buf_addc(&s1, text[i]);
			buf_addc(&s1, '_');
			buf_add(&s1, text + i + 1, j - i - 1);
			i = j;
		} else {
			buf_addc(&s1, text[i++]);
		}
	}
	const char *t = buf_str(&s1);
	n = s1.n;
	Buf s2 = {0};
	for (size_t i = 0; i < n;) {
		if (t[i] >= 'a' && t[i] <= 'z' && i + 1 < n && t[i + 1] >= 'A' && t[i + 1] <= 'Z') {
			buf_addc(&s2, t[i]);
			buf_addc(&s2, '_');
			buf_addc(&s2, t[i + 1]);
			i += 2;
		} else {
			buf_addc(&s2, t[i++]);
		}
	}
	char *out = xstrdup(buf_str(&s2));
	for (char *p = out; *p; p++)
		if (*p >= 'a' && *p <= 'z')
			*p = (char)(*p - 32);
	return out;
}

/* JsonEnumValueOptions (google/protobuf/json_enumvalue_options.proto): string = 1 */
static const PbField f_json_enumvalue_options[] = {{1, PT_STRING, PL_OPT, NULL, NULL, 0}};
static const PbMsgDef json_enumvalue_options = {1, f_json_enumvalue_options};

/* _enum_value_json_name: options[ext_json] (extension 998) parses the unknown fields; only its
 * errors matter here. */
static void check_enum_value_json_name(const PbMsg *value_proto)
{
	const PbMsg *options = pb_msg(value_proto, EVDP_OPTIONS);
	Str *recs;
	int n = pbpy_unknown_fields(options, 998, &recs);
	if (!n)
		return;
	PbMsg *m = pb_new(&json_enumvalue_options);
	for (int i = 0; i < n; i++)
		pbpy_merge_unknown_message(m, recs[i]);
}

static PbpyDescEnum *add_enum(PbpyRegistry *reg, PbMsg *proto, PbpyDescFile *file, PbpyDescMessage *parent, int index)
{
	PbpyDescEnum *e = xalloc(sizeof *e);
	e->kind = PBPY_ENUM;
	e->index = index;
	e->name = name_of(proto, EDP_NAME);
	e->type_name = type_name_of(e->name, parent, file);
	e->file = file;
	e->parent = parent;
	e->proto = proto;
	e->local_name = pbpy_escape_class_name(e->name);
	e->local_qualname = parent ? xcat(parent->local_qualname, ".", e->local_name) : e->local_name;
	e->open = resolve_feature(proto, EDP_OPTIONS, ENUMOPT_FEATURES, parent, file, FEATURE_ENUM_TYPE) == ENUM_TYPE_OPEN;
	e->deprecated = opt_bool(proto, EDP_OPTIONS, ENUMOPT_DEPRECATED);

	char *prefix = xcat(pascal_to_upper_snake_case(e->name), "_");
	size_t plen = strlen(prefix);
	Map unstripped = {0};
	int nv = pb_count(proto, EDP_VALUE);
	for (int i = 0; i < nv; i++)
		map_put(&unstripped, name_of(pb_msg_at(proto, EDP_VALUE, i), EVDP_NAME), (void *)1);
	for (int i = 0; i < nv; i++) {
		PbMsg *vp = pb_msg_at(proto, EDP_VALUE, i);
		PbpyDescEnumValue *v = xalloc(sizeof *v);
		v->kind = PBPY_ENUM_VALUE;
		v->index = i;
		v->name = name_of(vp, EVDP_NAME);
		const char *local = v->name;
		if (has_prefix(local, prefix) && strlen(local) > plen) {
			const char *stripped = local + plen;
			if ((unsigned char)*stripped >= 0x80)
				fail("unsupported by protoc-gen-uni: str.isdigit() of a non-ASCII character in an enum value name");
			if (!map_has(&unstripped, stripped) && !(*stripped >= '0' && *stripped <= '9'))
				local = stripped;
		}
		v->local_name = pbpy_escape_enum_attr(local);
		v->parent = e;
		v->number = (int32_t)pb_int(vp, EVDP_NUMBER, 0);
		check_enum_value_json_name(vp);
		v->deprecated = opt_bool(vp, EVDP_OPTIONS, EVOPT_DEPRECATED);
		v->proto = vp;
		vec_push(&e->values, v);
		map_put(&e->values_by_number, xfmt("%d", v->number), v);
		if (!map_has(&e->values_by_name, v->name))
			map_put(&e->values_by_name, v->name, v);
	}
	reg_add_type(reg, e->type_name, e);
	return e;
}

static void map_entries_add(MapEntries *me, PbpyDescMessage *m)
{
	if (!map_has(&me->map, m->type_name))
		vec_push(&me->order, (void *)m->type_name);
	map_put(&me->map, m->type_name, m);
}

static bool is_map_entry(const PbMsg *proto) { return opt_bool(proto, DP_OPTIONS, MSGOPT_MAP_ENTRY); }

static PbpyDescMessage *add_message(PbpyRegistry *reg, PbMsg *proto, PbpyDescFile *file, PbpyDescMessage *parent,
	MapEntries *me, int index)
{
	PbpyDescMessage *m = xalloc(sizeof *m);
	m->kind = PBPY_MESSAGE;
	m->index = index;
	m->name = name_of(proto, DP_NAME);
	m->type_name = type_name_of(m->name, parent, file);
	m->file = file;
	m->parent = parent;
	m->deprecated = opt_bool(proto, DP_OPTIONS, MSGOPT_DEPRECATED);
	m->proto = proto;
	m->local_name = pbpy_escape_class_name(m->name);
	m->local_qualname = parent ? xcat(parent->local_qualname, ".", m->local_name) : m->local_name;
	for (int i = 0; i < pb_count(proto, DP_ENUM_TYPE); i++)
		vec_push(&m->nested_enums, add_enum(reg, pb_msg_at(proto, DP_ENUM_TYPE, i), file, m, i));
	for (int i = 0; i < pb_count(proto, DP_NESTED_TYPE); i++) {
		PbMsg *np = pb_msg_at(proto, DP_NESTED_TYPE, i);
		PbpyDescMessage *n = add_message(reg, np, file, m, me, i);
		if (is_map_entry(np))
			map_entries_add(me, n);
		else
			vec_push(&m->nested_messages, n);
	}
	reg_add_type(reg, m->type_name, m);
	return m;
}

/* parse_text_format_scalar_value */
static void parse_scalar_default(PbpyFieldValue *v, Str value)
{
	v->has_default = true;
	switch (v->scalar) {
	case PT_BOOL:
		v->default_bool = str_eqc(value, "true");
		return;
	case PT_STRING:
		v->default_str = value;
		return;
	case PT_BYTES:
		v->default_str = pbpy_c_unescape(value);
		return;
	case PT_FLOAT: case PT_DOUBLE: {
		double d;
		PyNum st = py_parse_float(value, &d);
		if (st == PY_NUM_UNSUPPORTED)
			fail("unsupported by protoc-gen-uni: float() of non-ASCII digits");
		if (st != PY_NUM_OK)
			fail("ValueError: could not convert string to float: %s", py_repr_str(value));
		v->default_repr = py_repr_float(d);
		return;
	}
	}
	char *repr;
	PyNum st = py_parse_int(value, &repr);
	if (st == PY_NUM_UNSUPPORTED)
		fail("unsupported by protoc-gen-uni: int() of non-ASCII digits");
	if (st != PY_NUM_OK)
		fail("ValueError: invalid literal for int() with base 10: %s", py_repr_str(value));
	v->default_repr = repr;
}

static bool is_packable_type(int type) { return type != PT_STRING && type != PT_BYTES && type != PT_GROUP && type != PT_MESSAGE; }

/* _is_packed_field */
static bool is_packed_field(const PbMsg *proto, const PbpyDescMessage *msg, const PbpyDescFile *file)
{
	if (pb_int(proto, FD_LABEL, 1) != LABEL_REPEATED || !is_packable_type((int)pb_int(proto, FD_TYPE, 1)))
		return false;
	const PbMsg *options = pb_msg(proto, FD_OPTIONS);
	if (pb_has(options, FIELDOPT_PACKED))
		return pb_int(options, FIELDOPT_PACKED, 0) != 0;
	return field_feature(proto, msg, file, FEATURE_REPEATED_FIELD_ENCODING) == REPEATED_FIELD_ENCODING_PACKED;
}

/* _is_delimited_encoding */
static bool is_delimited_encoding(const PbMsg *proto, const PbpyDescMessage *msg, const PbpyDescFile *file)
{
	if (pb_int(proto, FD_TYPE, 1) == PT_GROUP)
		return true;
	return field_feature(proto, msg, file, FEATURE_MESSAGE_ENCODING) == MESSAGE_ENCODING_DELIMITED;
}

static PbpyDescMessage *find_message(const PbpyRegistry *reg, const char *type_name)
{
	PbpyDescMessage *m = pbpy_registry_type(reg, strip_dot(type_name), PBPY_MESSAGE);
	if (!m)
		fail("ValueError: could not find message: %s", type_name);
	return m;
}

static PbpyDescEnum *find_enum(const PbpyRegistry *reg, const char *type_name)
{
	PbpyDescEnum *e = pbpy_registry_type(reg, strip_dot(type_name), PBPY_ENUM);
	if (!e)
		fail("ValueError: could not find enum: %s", type_name);
	return e;
}

static PbpyDescField *find_field_number(const PbpyDescMessage *m, int32_t number)
{
	for (int i = 0; i < m->fields.n; i++) {
		PbpyDescField *f = m->fields.v[i];
		if (f->number == number)
			return f;
	}
	fail("StopIteration");
}

/* _find_map_entry_fields */
static void find_map_entry_fields(const PbpyDescMessage *entry, PbpyFieldValue *v)
{
	PbpyDescField *key = find_field_number(entry, 1), *value = find_field_number(entry, 2);
	if (key->value.kind != PBPY_VALUE_SCALAR || key->value.scalar == PT_BYTES || key->value.scalar == PT_FLOAT ||
		key->value.scalar == PT_DOUBLE)
		fail("ValueError: invalid map key type");
	v->scalar = key->value.scalar;
	switch (value->value.kind) {
	case PBPY_VALUE_SCALAR: v->element.scalar = value->value.scalar; break;
	case PBPY_VALUE_MESSAGE: v->element.message = value->value.message; break;
	case PBPY_VALUE_ENUM: v->element.enm = value->value.enm; break;
	default: fail("TypeError: unexpected map value type");
	}
}

/* _build_field_value; msg is NULL for extensions (their parent is the file) */
static void build_field_value(PbpyRegistry *reg, const PbMsg *proto, const PbpyDescMessage *msg, const PbpyDescFile *file,
	PbpyDescOneof *oneof, MapEntries *me, PbpyFieldValue *v)
{
	int label = (int)pb_int(proto, FD_LABEL, 1), type = (int)pb_int(proto, FD_TYPE, 1);
	const char *type_name = name_of(proto, FD_TYPE_NAME);
	if (label == LABEL_REPEATED) {
		PbpyDescMessage *entry = type == PT_MESSAGE && me ? map_get(&me->map, strip_dot(type_name)) : NULL;
		if (entry) {
			char *key = xfmt("%llx", (unsigned long long)(uintptr_t)entry);
			PbpyFieldValue *kv = map_get(&me->kv, key);
			if (!kv) {
				kv = xalloc(sizeof *kv);
				find_map_entry_fields(entry, kv);
				map_put(&me->kv, key, kv);
			}
			v->kind = PBPY_VALUE_MAP;
			v->scalar = kv->scalar;
			v->element = kv->element;
			return;
		}
		v->kind = PBPY_VALUE_LIST;
		if (type == PT_MESSAGE || type == PT_GROUP) {
			v->element.message = find_message(reg, type_name);
			v->packed = is_packed_field(proto, msg, file);
			v->delimited_encoding = is_delimited_encoding(proto, msg, file);
		} else {
			if (type == PT_ENUM)
				v->element.enm = find_enum(reg, type_name);
			else
				v->element.scalar = type;
			v->packed = is_packed_field(proto, msg, file);
		}
		v->packable = v->element.enm || (v->element.scalar && v->element.scalar != PT_BYTES && v->element.scalar != PT_STRING);
		return;
	}
	v->oneof = oneof;
	if (type == PT_MESSAGE || type == PT_GROUP) {
		v->kind = PBPY_VALUE_MESSAGE;
		v->message = find_message(reg, type_name);
		v->delimited_encoding = is_delimited_encoding(proto, msg, file);
		return;
	}
	if (type == PT_ENUM) {
		v->kind = PBPY_VALUE_ENUM;
		v->enm = find_enum(reg, type_name);
		if (pb_has(proto, FD_DEFAULT_VALUE)) {
			/* parse_text_format_enum_value */
			Str value = pb_bytes(proto, FD_DEFAULT_VALUE);
			v->has_default = true;
			PbpyDescEnumValue *ev = memchr(value.p, 0, value.n) ? NULL : map_get(&v->enm->values_by_name, value.p);
			if (!ev)
				fail("ValueError: cannot parse enum %s default value: %s", v->enm->type_name, value.p);
			v->default_enum = ev->number;
		}
		return;
	}
	v->kind = PBPY_VALUE_SCALAR;
	v->scalar = type;
	if (pb_has(proto, FD_DEFAULT_VALUE))
		parse_scalar_default(v, pb_bytes(proto, FD_DEFAULT_VALUE));
}

/* _get_field_presence; msg is NULL for extensions */
static int field_presence(const PbMsg *proto, const PbpyDescMessage *msg, const PbpyDescFile *file, bool in_oneof, bool is_ext)
{
	int label = (int)pb_int(proto, FD_LABEL, 1), type = (int)pb_int(proto, FD_TYPE, 1);
	if (label == LABEL_REQUIRED)
		return PBPY_LEGACY_REQUIRED;
	if (label == LABEL_REPEATED)
		return PBPY_IMPLICIT;
	if (in_oneof || pb_int(proto, FD_PROTO3_OPTIONAL, 0) || is_ext)
		return PBPY_EXPLICIT;
	int resolved = field_feature(proto, msg, file, FEATURE_FIELD_PRESENCE);
	if (resolved == PBPY_IMPLICIT && (type == PT_GROUP || type == PT_MESSAGE))
		return PBPY_EXPLICIT;
	if (resolved < 1 || resolved > 3)
		fail("ValueError: %d is not a valid SupportedFieldPresence", resolved);
	return resolved;
}

static PbpyDescField *build_field(PbpyRegistry *reg, PbMsg *proto, PbpyDescMessage *msg, PbpyDescOneof *oneof, MapEntries *me,
	int index)
{
	PbpyDescField *f = xalloc(sizeof *f);
	f->kind = PBPY_FIELD;
	f->index = index;
	build_field_value(reg, proto, msg, msg->file, oneof, me, &f->value);
	bool in_oneof = pb_has(proto, FD_ONEOF_INDEX);
	f->name = name_of(proto, FD_NAME);
	f->parent = msg;
	f->local_name = !in_oneof || pb_int(proto, FD_PROTO3_OPTIONAL, 0) ? pbpy_escape_message_attr(f->name) : f->name;
	f->number = (int32_t)pb_int(proto, FD_NUMBER, 0);
	f->json_name = pb_bytes(proto, FD_JSON_NAME);
	f->deprecated = opt_bool(proto, FD_OPTIONS, FIELDOPT_DEPRECATED);
	f->presence = field_presence(proto, msg, msg->file, in_oneof, false);
	f->proto = proto;
	return f;
}

/* _build_fields */
static void build_fields(PbpyRegistry *reg, PbpyDescMessage *msg, MapEntries *me)
{
	int no = pb_count(msg->proto, DP_ONEOF_DECL);
	PbpyDescOneof **oneofs = xalloc(sizeof *oneofs * (size_t)(no ? no : 1));
	for (int i = 0; i < no; i++) {
		PbMsg *op = pb_msg_at(msg->proto, DP_ONEOF_DECL, i);
		PbpyDescOneof *o = xalloc(sizeof *o);
		o->kind = PBPY_ONEOF;
		o->index = i;
		o->name = name_of(op, ODP_NAME);
		o->local_name = pbpy_escape_message_attr(o->name);
		o->parent = msg;
		o->proto = op;
		oneofs[i] = o;
	}
	for (int i = 0; i < pb_count(msg->proto, DP_FIELD); i++) {
		PbMsg *fp = pb_msg_at(msg->proto, DP_FIELD, i);
		bool in_oneof = !pb_int(fp, FD_PROTO3_OPTIONAL, 0) && pb_has(fp, FD_ONEOF_INDEX);
		PbpyDescOneof *oneof = NULL;
		if (in_oneof) {
			int64_t idx = pb_int(fp, FD_ONEOF_INDEX, 0);
			if (idx < 0)
				idx += no; /* Python list indexing */
			if (idx < 0 || idx >= no)
				fail("IndexError: list index out of range");
			oneof = oneofs[idx];
		}
		PbpyDescField *f = build_field(reg, fp, msg, oneof, me, i);
		vec_push(&msg->fields, f);
		if (oneof && f->value.kind != PBPY_VALUE_SCALAR && f->value.kind != PBPY_VALUE_MESSAGE &&
			f->value.kind != PBPY_VALUE_ENUM)
			fail("AssertionError");
		if (oneof) {
			vec_push(&oneof->fields, f);
			if (oneof->fields.n == 1)
				vec_push(&msg->members, oneof);
		} else {
			vec_push(&msg->members, f);
		}
	}
	for (int i = 0; i < no; i++)
		if (oneofs[i]->fields.n)
			vec_push(&msg->oneofs, oneofs[i]);
	for (int i = 0; i < msg->nested_messages.n; i++)
		build_fields(reg, msg->nested_messages.v[i], me);
	/* _finish_init: default_value() of the members; an enum default without a value set is the
	 * first value of the enum */
	for (int i = 0; i < msg->members.n; i++) {
		PbpyDescField *f = msg->members.v[i];
		if (f->kind == PBPY_FIELD && f->value.kind == PBPY_VALUE_ENUM && !f->value.has_default && !f->value.enm->values.n)
			fail("IndexError: list index out of range");
	}
}

/* _add_extension */
static PbpyDescExtension *add_extension(PbpyRegistry *reg, PbMsg *proto, PbpyDescFile *file, PbpyDescMessage *parent, int index)
{
	PbpyDescExtension *x = xalloc(sizeof *x);
	x->kind = PBPY_EXTENSION;
	x->index = index;
	x->name = name_of(proto, FD_NAME);
	x->type_name = type_name_of(x->name, parent, file);
	const char *extendee = name_of(proto, FD_EXTENDEE);
	x->extendee = pbpy_registry_type(reg, strip_dot(extendee), PBPY_MESSAGE);
	if (!x->extendee)
		fail("ValueError: could not find extendee message: %s", extendee);
	build_field_value(reg, proto, NULL, file, NULL, NULL, &x->value);
	x->file = file;
	x->parent = parent;
	x->number = (int32_t)pb_int(proto, FD_NUMBER, 0);
	x->json_name = xcat("[", x->type_name, "]");
	x->deprecated = opt_bool(proto, FD_OPTIONS, FIELDOPT_DEPRECATED);
	x->presence = field_presence(proto, NULL, file, false, true);
	x->proto = proto;
	reg_add_type(reg, x->type_name, x);
	return x;
}

static void add_message_extensions(PbpyRegistry *reg, PbpyDescMessage *msg)
{
	for (int i = 0; i < pb_count(msg->proto, DP_EXTENSION); i++)
		vec_push(&msg->nested_extensions, add_extension(reg, pb_msg_at(msg->proto, DP_EXTENSION, i), msg->file, msg, i));
	for (int i = 0; i < msg->nested_messages.n; i++)
		add_message_extensions(reg, msg->nested_messages.v[i]);
}

static PbpyDescService *add_service(PbpyRegistry *reg, PbMsg *proto, PbpyDescFile *file, int index)
{
	PbpyDescService *s = xalloc(sizeof *s);
	s->kind = PBPY_SERVICE;
	s->index = index;
	s->name = name_of(proto, SDP_NAME);
	s->type_name = type_name_of(s->name, NULL, file);
	s->file = file;
	s->deprecated = opt_bool(proto, SDP_OPTIONS, SVCOPT_DEPRECATED);
	s->proto = proto;
	for (int i = 0; i < pb_count(proto, SDP_METHOD); i++) {
		PbMsg *mp = pb_msg_at(proto, SDP_METHOD, i);
		PbpyDescMethod *m = xalloc(sizeof *m);
		m->kind = PBPY_METHOD;
		m->index = i;
		m->name = name_of(mp, MDP_NAME);
		m->parent = s;
		m->client_streaming = pb_int(mp, MDP_CLIENT_STREAMING, 0) != 0;
		m->server_streaming = pb_int(mp, MDP_SERVER_STREAMING, 0) != 0;
		m->input = pbpy_registry_type(reg, strip_dot(name_of(mp, MDP_INPUT_TYPE)), PBPY_MESSAGE);
		m->output = pbpy_registry_type(reg, strip_dot(name_of(mp, MDP_OUTPUT_TYPE)), PBPY_MESSAGE);
		if (!m->input || !m->output)
			fail("ValueError: could not find input/output types for method %s", m->name);
		m->deprecated = opt_bool(mp, MDP_OPTIONS, METHODOPT_DEPRECATED);
		m->proto = mp;
		vec_push(&s->methods, m);
	}
	reg_add_type(reg, s->type_name, s);
	return s;
}

COLD PbpyDescFile *pbpy_add_file(PbpyRegistry *reg, PbMsg *proto)
{
	PbpyDescFile *f = xalloc(sizeof *f);
	f->kind = PBPY_FILE;
	f->edition = pbpy_file_edition(proto);
	f->name = name_of(proto, FDP_NAME);
	f->proto = proto;
	for (int i = 0; i < pb_count(proto, FDP_DEPENDENCY); i++) {
		Str path = pb_bytes_at(proto, FDP_DEPENDENCY, i);
		PbpyDescFile *dep = memchr(path.p, 0, path.n) ? NULL : pbpy_registry_file(reg, path.p);
		if (!dep)
			fail("ValueError: cannot find %s, imported by %s", path.p, f->name);
		vec_push(&f->dependencies, dep);
	}
	f->deprecated = opt_bool(proto, FDP_OPTIONS, FILEOPT_DEPRECATED);

	for (int i = 0; i < pb_count(proto, FDP_ENUM_TYPE); i++)
		vec_push(&f->enums, add_enum(reg, pb_msg_at(proto, FDP_ENUM_TYPE, i), f, NULL, i));
	MapEntries me = {0};
	for (int i = 0; i < pb_count(proto, FDP_MESSAGE_TYPE); i++) {
		PbMsg *mp = pb_msg_at(proto, FDP_MESSAGE_TYPE, i);
		PbpyDescMessage *m = add_message(reg, mp, f, NULL, &me, i);
		if (is_map_entry(mp))
			map_entries_add(&me, m);
		else
			vec_push(&f->messages, m);
	}
	for (int i = 0; i < pb_count(proto, FDP_SERVICE); i++)
		vec_push(&f->services, add_service(reg, pb_msg_at(proto, FDP_SERVICE, i), f, i));
	for (int i = 0; i < pb_count(proto, FDP_EXTENSION); i++)
		vec_push(&f->extensions, add_extension(reg, pb_msg_at(proto, FDP_EXTENSION, i), f, NULL, i));
	for (int i = 0; i < me.order.n; i++)
		build_fields(reg, map_get(&me.map, me.order.v[i]), &me);
	for (int i = 0; i < f->messages.n; i++) {
		build_fields(reg, f->messages.v[i], &me);
		add_message_extensions(reg, f->messages.v[i]);
	}
	map_put(&reg->files, f->name, f);
	return f;
}
