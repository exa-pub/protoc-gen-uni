/* Copyright 2021-2026 Buf Technologies, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Ported from packages/protobuf/src/registry.ts of protobuf-es v2.17.0 (createFileRegistry from a
 * FileDescriptorSet, assertValidNames of protoplugin). */
#include "internal.h"

#include <string.h>

static bool opt_deprecated(const Desc *d)
{
	static const uint8_t dep_num[] = {
		[DK_FILE] = 23, [DK_MESSAGE] = 3, [DK_ENUM] = 3, [DK_ENUM_VALUE] = 1,
		[DK_FIELD] = 3, [DK_ONEOF] = 0, [DK_SERVICE] = 33, [DK_METHOD] = 33,
	};
	return dep_num[d->kind] && pb_int(desc_options(d), dep_num[d->kind], 0) != 0;
}

static void init_desc(Desc *d, int kind, PbMsg *proto, int index)
{
	d->kind = kind;
	d->proto = proto;
	d->index = index;
	d->deprecated = opt_deprecated(d);
}

static char *ascii_lower(const char *s)
{
	char *r = xstrdup(s);
	for (char *p = r; *p; p++)
		if (*p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
	return r;
}

static const char *make_type_name(const char *name, DescMessage *parent, DescFile *file)
{
	if (parent)
		return xcat(parent->type_name, ".", name);
	const char *pkg = pb_str(file->h.proto, FDP_PACKAGE);
	return *pkg ? xcat(pkg, ".", name) : name;
}

static const char *trim_dot(const char *s) { return *s == '.' ? s + 1 : s; }

/* ---- validation ---- */

/* /^[A-Za-z0-9_]+$/ */
COLD static bool is_identifier(Str s)
{
	if (!s.n)
		return false;
	for (size_t i = 0; i < s.n; i++) {
		char c = s.p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
			return false;
	}
	return true;
}

typedef struct {
	const PbMsg *file;
	const char *err;
} NameCheck;

COLD static void check_name(NameCheck *c, const char *kind, Str name)
{
	if (!c->err && !is_identifier(name))
		c->err = xfmt("%s: invalid %s name %s", pb_str(c->file, FDP_NAME), kind, quote_json(name));
}

COLD static void check_enum(NameCheck *c, const PbMsg *e)
{
	check_name(c, "enum", pb_bytes(e, EDP_NAME));
	for (int i = 0; i < pb_count(e, EDP_VALUE); i++)
		check_name(c, "enum value", pb_bytes(pb_msg_at(e, EDP_VALUE, i), EVDP_NAME));
}

COLD static void check_message(NameCheck *c, const PbMsg *m)
{
	check_name(c, "message", pb_bytes(m, DP_NAME));
	for (int i = 0; i < pb_count(m, DP_FIELD); i++)
		check_name(c, "field", pb_bytes(pb_msg_at(m, DP_FIELD, i), FD_NAME));
	for (int i = 0; i < pb_count(m, DP_ONEOF_DECL); i++)
		check_name(c, "oneof", pb_bytes(pb_msg_at(m, DP_ONEOF_DECL, i), ODP_NAME));
	for (int i = 0; i < pb_count(m, DP_EXTENSION); i++)
		check_name(c, "extension", pb_bytes(pb_msg_at(m, DP_EXTENSION, i), FD_NAME));
	for (int i = 0; i < pb_count(m, DP_NESTED_TYPE); i++)
		check_message(c, pb_msg_at(m, DP_NESTED_TYPE, i));
	for (int i = 0; i < pb_count(m, DP_ENUM_TYPE); i++)
		check_enum(c, pb_msg_at(m, DP_ENUM_TYPE, i));
}

const char *assert_valid_names(const PbMsg *file)
{
	NameCheck c = {file, NULL};
	for (int i = 0; i < pb_count(file, FDP_MESSAGE_TYPE); i++)
		check_message(&c, pb_msg_at(file, FDP_MESSAGE_TYPE, i));
	for (int i = 0; i < pb_count(file, FDP_ENUM_TYPE); i++)
		check_enum(&c, pb_msg_at(file, FDP_ENUM_TYPE, i));
	for (int i = 0; i < pb_count(file, FDP_EXTENSION); i++)
		check_name(&c, "extension", pb_bytes(pb_msg_at(file, FDP_EXTENSION, i), FD_NAME));
	for (int i = 0; i < pb_count(file, FDP_SERVICE); i++) {
		const PbMsg *s = pb_msg_at(file, FDP_SERVICE, i);
		check_name(&c, "service", pb_bytes(s, SDP_NAME));
		for (int j = 0; j < pb_count(s, SDP_METHOD); j++)
			check_name(&c, "method", pb_bytes(pb_msg_at(s, SDP_METHOD, j), MDP_NAME));
	}
	return c.err;
}

/* ---- registry ---- */

/* Map entry messages of a file, in the order they were found. */
typedef struct {
	Vec order;
	Map by_name;
} MapEntries;

static void reg_add(Registry *r, Desc *d, const char *type_name) { map_put(&r->types, type_name, d); }

static Desc *reg_get(Registry *r, const char *type_name, int kind)
{
	Desc *d = map_get(&r->types, type_name);
	return d && d->kind == kind ? d : NULL;
}

DescMessage *registry_message(Registry *r, const char *type_name)
{
	return (DescMessage *)reg_get(r, type_name, DK_MESSAGE);
}

/* Finds a prefix shared by enum values, for example "my_enum_" for
 * enum MyEnum {MY_ENUM_A=0; MY_ENUM_B=1;}. */
static const char *find_enum_shared_prefix(const char *enum_name, const PbMsg *proto)
{
	/* camelToSnakeCase */
	Buf b = {0};
	if (*enum_name) {
		buf_addc(&b, enum_name[0]);
		for (const char *p = enum_name + 1; *p; p++) {
			if (*p >= 'A' && *p <= 'Z')
				buf_addc(&b, '_');
			buf_addc(&b, *p);
		}
	}
	const char *prefix = xcat(ascii_lower(buf_str(&b)), "_");
	size_t pl = strlen(prefix);
	for (int i = 0; i < pb_count(proto, EDP_VALUE); i++) {
		const char *name = pb_str(pb_msg_at(proto, EDP_VALUE, i), EVDP_NAME);
		if (!has_prefix(ascii_lower(name), prefix))
			return NULL;
		const char *shrt = name + pl;
		if (!*shrt || (*shrt >= '0' && *shrt <= '9'))
			return NULL;
	}
	return prefix;
}

/* The custom JSON name of an enum value, set with the option (pb.enumvalue.json).string from
 * google/protobuf/json_enumvalue_options.proto (an unknown field: extension 998, field 1). */
static Str find_enum_value_json_name(const PbMsg *proto)
{
	Str json = {NULL, 0};
	PbReader r = pb_unknown_fields(pb_msg(proto, EVDP_OPTIONS));
	PbRawField f;
	while (pb_next_field(&r, &f)) {
		if (f.num != 998 || f.wt != WT_LEN)
			continue;
		PbReader s = {f.val, f.val + f.vlen};
		PbRawField g;
		while (pb_next_field(&s, &g))
			if (g.num == 1 && g.wt == WT_LEN)
				json = (Str){xstrndup((const char *)g.val, g.vlen), g.vlen};
	}
	return json;
}

static void add_enum(PbMsg *proto, int index, DescFile *file, DescMessage *parent, Registry *reg)
{
	DescEnum *d = xalloc(sizeof *d);
	init_desc(&d->h, DK_ENUM, proto, index);
	d->file = file;
	d->parent = parent;
	d->name = pb_str(proto, EDP_NAME);
	d->type_name = make_type_name(d->name, parent, file);
	d->shared_prefix = find_enum_shared_prefix(d->name, proto);
	d->open = resolve_feature(F_ENUM_TYPE, desc_features(&d->h), parent ? &parent->h : &file->h) == 1;
	reg_add(reg, &d->h, d->type_name);
	for (int i = 0; i < pb_count(proto, EDP_VALUE); i++) {
		PbMsg *p = pb_msg_at(proto, EDP_VALUE, i);
		DescEnumValue *v = xalloc(sizeof *v);
		init_desc(&v->h, DK_ENUM_VALUE, p, i);
		v->parent = d;
		v->name = pb_str(p, EVDP_NAME);
		v->local_name = safe_object_property(d->shared_prefix ? v->name + strlen(d->shared_prefix) : v->name);
		v->number = (int32_t)pb_int(p, EVDP_NUMBER, 0);
		v->json_name = find_enum_value_json_name(p);
		if (!v->json_name.p)
			v->json_name = str_c(v->name);
		vec_push(&d->values, v);
	}
	vec_push(parent ? &parent->nested_enums : &file->enums, d);
}

static void add_message(PbMsg *proto, int index, DescFile *file, DescMessage *parent, Registry *reg, MapEntries *entries)
{
	DescMessage *d = xalloc(sizeof *d);
	init_desc(&d->h, DK_MESSAGE, proto, index);
	d->file = file;
	d->parent = parent;
	d->name = pb_str(proto, DP_NAME);
	d->type_name = make_type_name(d->name, parent, file);
	if (pb_int(pb_msg(proto, DP_OPTIONS), 7, 0)) { /* map_entry */
		if (!map_has(&entries->by_name, d->type_name))
			vec_push(&entries->order, d);
		map_put(&entries->by_name, d->type_name, d);
	} else {
		vec_push(parent ? &parent->nested_messages : &file->messages, d);
		reg_add(reg, &d->h, d->type_name);
	}
	for (int i = 0; i < pb_count(proto, DP_ENUM_TYPE); i++)
		add_enum(pb_msg_at(proto, DP_ENUM_TYPE, i), i, file, d, reg);
	for (int i = 0; i < pb_count(proto, DP_NESTED_TYPE); i++)
		add_message(pb_msg_at(proto, DP_NESTED_TYPE, i), i, file, d, reg, entries);
}

static void add_service(PbMsg *proto, int index, DescFile *file, Registry *reg)
{
	DescService *d = xalloc(sizeof *d);
	init_desc(&d->h, DK_SERVICE, proto, index);
	d->file = file;
	d->name = pb_str(proto, SDP_NAME);
	d->type_name = make_type_name(d->name, NULL, file);
	vec_push(&file->services, d);
	reg_add(reg, &d->h, d->type_name);
	for (int i = 0; i < pb_count(proto, SDP_METHOD); i++) {
		PbMsg *p = pb_msg_at(proto, SDP_METHOD, i);
		DescMethod *m = xalloc(sizeof *m);
		init_desc(&m->h, DK_METHOD, p, i);
		m->parent = d;
		bool cs = pb_int(p, MDP_CLIENT_STREAMING, 0), ss = pb_int(p, MDP_SERVER_STREAMING, 0);
		m->method_kind = cs && ss ? "bidi_streaming" : cs ? "client_streaming" : ss ? "server_streaming" : "unary";
		m->input = registry_message(reg, trim_dot(pb_str(p, MDP_INPUT_TYPE)));
		m->output = registry_message(reg, trim_dot(pb_str(p, MDP_OUTPUT_TYPE)));
		if (!m->input)
			fail("invalid MethodDescriptorProto: input_type %s not found", pb_str(p, MDP_INPUT_TYPE));
		/* sic: upstream reports the input type */
		if (!m->output)
			fail("invalid MethodDescriptorProto: output_type %s not found", pb_str(p, MDP_INPUT_TYPE));
		m->name = pb_str(p, MDP_NAME);
		const char *local = m->name;
		if (*local) {
			char *l = xstrdup(local);
			if (l[0] >= 'A' && l[0] <= 'Z')
				l[0] = (char)(l[0] - 'A' + 'a');
			local = safe_object_property(l);
		}
		m->local_name = safe_object_property(local);
		vec_push(&d->methods, m);
	}
}

static int32_t field_presence(const PbMsg *proto, bool in_oneof, bool is_ext, const Desc *parent)
{
	int64_t label = pb_int(proto, FD_LABEL, LABEL_OPTIONAL);
	if (label == LABEL_REQUIRED)
		return PRESENCE_LEGACY_REQUIRED;
	if (label == LABEL_REPEATED)
		return PRESENCE_IMPLICIT;
	if (in_oneof || pb_int(proto, FD_PROTO3_OPTIONAL, 0) || is_ext)
		return PRESENCE_EXPLICIT;
	int32_t r = resolve_feature(F_FIELD_PRESENCE, pb_msg(pb_msg(proto, FD_OPTIONS), 21), parent);
	int64_t t = pb_int(proto, FD_TYPE, 1);
	if (r == PRESENCE_IMPLICIT && (t == PT_MESSAGE || t == PT_GROUP))
		return PRESENCE_EXPLICIT;
	return r;
}

static bool is_delimited(const PbMsg *proto, const Desc *parent)
{
	if (pb_int(proto, FD_TYPE, 1) == PT_GROUP)
		return true;
	return resolve_feature(F_MESSAGE_ENCODING, pb_msg(pb_msg(proto, FD_OPTIONS), 21), parent) == 2;
}

static bool is_packed(const PbMsg *proto, const Desc *parent)
{
	if (pb_int(proto, FD_LABEL, LABEL_OPTIONAL) != LABEL_REPEATED)
		return false;
	switch (pb_int(proto, FD_TYPE, 1)) {
	case PT_STRING: case PT_BYTES: case PT_GROUP: case PT_MESSAGE: return false;
	}
	const PbMsg *o = pb_msg(proto, FD_OPTIONS);
	if (pb_has(o, FO_PACKED))
		return pb_int(o, FO_PACKED, 0) != 0;
	return resolve_feature(F_REPEATED_FIELD_ENCODING, pb_msg(o, 21), parent) == 1;
}

static DescField *find_entry_field(DescMessage *entry, int number)
{
	for (int i = 0; i < entry->fields.n; i++) {
		DescField *f = entry->fields.v[i];
		if (f->number == number)
			return f;
	}
	return NULL;
}

static void set_field_type(DescField *f, const PbMsg *proto, const Desc *parent, Registry *reg, MapEntries *entries)
{
	int64_t typ = pb_int(proto, FD_TYPE, 1);
	bool js_string = pb_int(pb_msg(proto, FD_OPTIONS), FO_JSTYPE, 0) == 1;
	const char *tn = trim_dot(pb_str(proto, FD_TYPE_NAME));
	if (pb_int(proto, FD_LABEL, LABEL_OPTIONAL) == LABEL_REPEATED) {
		DescMessage *entry = typ == PT_MESSAGE && entries ? map_get(&entries->by_name, tn) : NULL;
		if (entry) {
			DescField *key = find_entry_field(entry, 1), *value = find_entry_field(entry, 2);
			if (!key || key->field_kind != FK_SCALAR || key->scalar == PT_BYTES || key->scalar == PT_FLOAT ||
			    key->scalar == PT_DOUBLE || !value || value->field_kind == FK_LIST || value->field_kind == FK_MAP)
				fail("%s", "");
			f->field_kind = FK_MAP;
			f->map_key = key->scalar;
			f->map_kind = value->field_kind;
			f->message = value->message;
			f->enm = value->enm;
			f->scalar = value->scalar;
			return;
		}
		f->field_kind = FK_LIST;
		switch (typ) {
		case PT_MESSAGE: case PT_GROUP:
			f->list_kind = FK_MESSAGE;
			f->message = registry_message(reg, tn);
			if (!f->message)
				fail("%s", "");
			f->delimited = is_delimited(proto, parent);
			break;
		case PT_ENUM:
			f->list_kind = FK_ENUM;
			f->enm = (DescEnum *)reg_get(reg, tn, DK_ENUM);
			if (!f->enm)
				fail("%s", "");
			break;
		default:
			f->list_kind = FK_SCALAR;
			f->scalar = (int)typ;
			f->long_as_string = js_string;
		}
		f->packed = is_packed(proto, parent);
		return;
	}
	switch (typ) {
	case PT_MESSAGE: case PT_GROUP:
		f->field_kind = FK_MESSAGE;
		f->message = registry_message(reg, tn);
		if (!f->message)
			fail("invalid FieldDescriptorProto: type_name %s not found", pb_str(proto, FD_TYPE_NAME));
		f->delimited = is_delimited(proto, parent);
		break;
	case PT_ENUM:
		f->enm = (DescEnum *)reg_get(reg, tn, DK_ENUM);
		if (!f->enm)
			fail("invalid FieldDescriptorProto: type_name %s not found", pb_str(proto, FD_TYPE_NAME));
		f->field_kind = FK_ENUM;
		break;
	default:
		f->field_kind = FK_SCALAR;
		f->scalar = (int)typ;
		f->long_as_string = js_string;
	}
}

static DescField *new_extension(PbMsg *proto, int index, DescFile *file, DescMessage *parent, Registry *reg)
{
	const Desc *pref = parent ? &parent->h : &file->h;
	DescField *f = xalloc(sizeof *f);
	init_desc(&f->h, DK_FIELD, proto, index);
	f->name = pb_str(proto, FD_NAME);
	f->number = (int32_t)pb_int(proto, FD_NUMBER, 0);
	f->presence = field_presence(proto, false, true, pref);
	f->is_extension = true;
	f->file = file;
	f->parent = parent;
	f->type_name = make_type_name(f->name, parent, file);
	f->json_name = str_c(xcat("[", f->type_name, "]"));
	f->extendee = registry_message(reg, trim_dot(pb_str(proto, FD_EXTENDEE)));
	if (!f->extendee)
		fail("invalid FieldDescriptorProto: extendee %s not found", pb_str(proto, FD_EXTENDEE));
	set_field_type(f, proto, pref, reg, NULL);
	reg_add(reg, &f->h, f->type_name);
	return f;
}

static void add_message_extensions(DescMessage *m, Registry *reg)
{
	for (int i = 0; i < pb_count(m->h.proto, DP_EXTENSION); i++)
		vec_push(&m->nested_extensions, new_extension(pb_msg_at(m->h.proto, DP_EXTENSION, i), i, m->file, m, reg));
	for (int i = 0; i < m->nested_messages.n; i++)
		add_message_extensions(m->nested_messages.v[i], reg);
}

static void add_fields(DescMessage *m, Registry *reg, MapEntries *entries)
{
	int no = pb_count(m->h.proto, DP_ONEOF_DECL);
	DescOneof **all = xalloc(sizeof(DescOneof *) * (size_t)(no ? no : 1));
	bool *seen = xalloc((size_t)(no ? no : 1));
	for (int i = 0; i < no; i++) {
		DescOneof *o = xalloc(sizeof *o);
		init_desc(&o->h, DK_ONEOF, pb_msg_at(m->h.proto, DP_ONEOF_DECL, i), i);
		o->parent = m;
		o->name = pb_str(o->h.proto, ODP_NAME);
		o->local_name = safe_object_property(proto_camel_case(o->name));
		all[i] = o;
	}
	for (int i = 0; i < pb_count(m->h.proto, DP_FIELD); i++) {
		PbMsg *p = pb_msg_at(m->h.proto, DP_FIELD, i);
		int oi = -1;
		if (pb_has(p, FD_ONEOF_INDEX) && !pb_int(p, FD_PROTO3_OPTIONAL, 0)) {
			int64_t x = pb_int(p, FD_ONEOF_INDEX, 0);
			if (x < 0 || x >= no)
				fail("invalid FieldDescriptorProto: oneof #%d for field #%d not found", (int)x,
				     (int)pb_int(p, FD_NUMBER, 0));
			oi = (int)x;
		}
		DescField *f = xalloc(sizeof *f);
		init_desc(&f->h, DK_FIELD, p, i);
		f->name = pb_str(p, FD_NAME);
		f->number = (int32_t)pb_int(p, FD_NUMBER, 0);
		f->presence = field_presence(p, oi >= 0, false, &m->h);
		f->parent = m;
		f->oneof = oi >= 0 ? all[oi] : NULL;
		f->local_name = proto_camel_case(f->name);
		if (oi < 0)
			f->local_name = safe_object_property(f->local_name);
		f->json_name = pb_bytes(p, FD_JSON_NAME);
		set_field_type(f, p, &m->h, reg, entries);
		vec_push(&m->fields, f);
		if (oi < 0) {
			vec_push(&m->members, f);
		} else {
			vec_push(&all[oi]->fields, f);
			if (!seen[oi]) {
				seen[oi] = true;
				vec_push(&m->members, all[oi]);
			}
		}
	}
	for (int i = 0; i < no; i++)
		if (seen[i])
			vec_push(&m->oneofs, all[i]);
	for (int i = 0; i < m->nested_messages.n; i++)
		add_fields(m->nested_messages.v[i], reg, entries);
}

static int file_edition(const PbMsg *proto)
{
	const char *syntax = pb_str(proto, FDP_SYNTAX);
	if (!*syntax || str_eq(syntax, "proto2"))
		return ED_PROTO2;
	if (str_eq(syntax, "proto3"))
		return ED_PROTO3;
	if (str_eq(syntax, "editions")) {
		int64_t e = pb_int(proto, FDP_EDITION, 0);
		if (e == ED_UNSTABLE)
			return ED_MAXIMUM;
		if (feature_defaults((int)e))
			return (int)e;
		fail("%s: unsupported edition", pb_str(proto, FDP_NAME));
	}
	fail("%s: unsupported syntax \"%s\"", pb_str(proto, FDP_NAME), syntax);
}

static void add_file(PbMsg *proto, Registry *reg)
{
	DescFile *file = xalloc(sizeof *file);
	init_desc(&file->h, DK_FILE, proto, 0);
	file->edition = file_edition(proto);
	file->name = str_trim_suffix(pb_str(proto, FDP_NAME), ".proto");
	for (int i = 0; i < pb_count(proto, FDP_DEPENDENCY); i++) {
		const char *dep = pb_str_at(proto, FDP_DEPENDENCY, i);
		DescFile *d = map_get(&reg->file_names, dep);
		if (!d)
			fail("Cannot find %s, imported by %s", dep, pb_str(proto, FDP_NAME));
		vec_push(&file->deps, d);
	}
	MapEntries entries = {0};
	for (int i = 0; i < pb_count(proto, FDP_ENUM_TYPE); i++)
		add_enum(pb_msg_at(proto, FDP_ENUM_TYPE, i), i, file, NULL, reg);
	for (int i = 0; i < pb_count(proto, FDP_MESSAGE_TYPE); i++)
		add_message(pb_msg_at(proto, FDP_MESSAGE_TYPE, i), i, file, NULL, reg, &entries);
	for (int i = 0; i < pb_count(proto, FDP_SERVICE); i++)
		add_service(pb_msg_at(proto, FDP_SERVICE, i), i, file, reg);
	for (int i = 0; i < pb_count(proto, FDP_EXTENSION); i++)
		vec_push(&file->extensions, new_extension(pb_msg_at(proto, FDP_EXTENSION, i), i, file, NULL, reg));
	for (int i = 0; i < entries.order.n; i++)
		add_fields(entries.order.v[i], reg, &entries);
	for (int i = 0; i < file->messages.n; i++) {
		add_fields(file->messages.v[i], reg, &entries);
		add_message_extensions(file->messages.v[i], reg);
	}
	const char *name = pb_str(proto, FDP_NAME);
	if (map_has(&reg->file_names, name)) {
		for (int i = 0; i < reg->files.n; i++)
			if (str_eq(pb_str(((DescFile *)reg->files.v[i])->h.proto, FDP_NAME), name))
				reg->files.v[i] = file;
	} else {
		vec_push(&reg->files, file);
	}
	map_put(&reg->file_names, name, file);
}

Registry *registry_create(PbMsg **files, int n)
{
	Registry *reg = xalloc(sizeof *reg);
	for (int i = 0; i < n; i++) {
		const char *err = assert_valid_names(files[i]);
		if (err)
			fail("%s", err);
		add_file(files[i], reg);
	}
	return reg;
}
