/* Copyright (c) 2014 - 2024 Apple Inc. and the project authors
 * Licensed under Apache License v2.0 with Runtime Library Exception
 *
 * See LICENSE.txt for license information:
 * https://github.com/apple/swift-protobuf/blob/main/LICENSE.txt */

/* Port of Sources/SwiftProtobufPluginLibrary/Descriptor.swift, FeatureResolver.swift,
 * PluginLibEditionDefaults.swift and the descriptor helpers of Descriptor+Extensions.swift
 * (swift-protobuf 1.39.0). Where upstream crashes (force unwraps, preconditions, fatalError) on
 * input protoc never produces, the port fails with a message instead. */
#include "swift_protobuf.h"

#include <string.h>

/* Field numbers of descriptor.proto not in pb.h. */
enum {
	DP_RESERVED_RANGE = 9, DP_RESERVED_NAME = 10,
	EDP_RESERVED_RANGE = 4,
	ODP_OPTIONS = 2,
	SDP_OPTIONS = 3,
	FDP_OPT_DEPRECATED = 23,
	MO_MESSAGE_SET_WIRE_FORMAT = 1, MO_DEPRECATED = 3, MO_MAP_ENTRY = 7,
	EO_DEPRECATED = 3, EVO_DEPRECATED = 1, SO_DEPRECATED = 33, MO2_DEPRECATED = 33,
	/* the `features` field of each options message */
	FEAT_FILE = 50, FEAT_MESSAGE = 12, FEAT_FIELD = 21, FEAT_ONEOF = 1, FEAT_ENUM = 7,
	FEAT_ENUM_VALUE = 2, FEAT_SERVICE = 34, FEAT_METHOD = 35,
	/* pb.enumvalue.json (json_enumvalue_options.proto) */
	EXT_ENUMVALUE_JSON = 998,
};

/* PluginLibEditionDefaults.swift (bundledFeatureSetDefaultBytes), decoded: fixed and overridable
 * features merged per edition. */
static const SwEditionDefault edition_defaults[] = {
	{SW_EDITION_LEGACY, {{0, 1, 2, 2, 3, 1, 2, 2, 1, 1}}},
	{SW_EDITION_PROTO3, {{0, 2, 1, 1, 2, 1, 1, 2, 1, 1}}},
	{SW_EDITION_2023, {{0, 1, 1, 1, 2, 1, 1, 2, 1, 1}}},
	{SW_EDITION_2024, {{0, 1, 1, 1, 2, 1, 1, 1, 2, 1}}},
	{SW_EDITION_2026, {{0, 1, 1, 1, 2, 1, 1, 3, 4, 2}}},
};
const SwEditionDefaults sw_edition_defaults = {
	SW_EDITION_PROTO2, SW_EDITION_2026, (int)(sizeof edition_defaults / sizeof edition_defaults[0]), edition_defaults,
};

/* FeatureResolver */
typedef struct {
	int32_t edition;
	SwFeatures defaults;
} Resolver;

static void resolver_init(Resolver *r, const SwEditionDefaults *d, int32_t edition, const char *file_name)
{
	if (!(edition >= d->minimum && (edition <= d->maximum || edition == SW_EDITION_UNSTABLE)))
		fail("Failed to make a FeatureResolver for %s: Edition %d is not in the supported range", file_name,
		     (int)edition);
	int found = -1;
	for (int i = 0; i < d->n; i++) {
		if (d->defaults[i].edition > edition)
			break;
		found = i;
	}
	r->edition = edition;
	r->defaults = d->defaults[found].features;
}

/* resolve(features:resolvedParent:): merge of the set fields. */
static SwFeatures resolve(const SwFeatures *parent, const PbMsg *options, uint32_t features_field)
{
	SwFeatures r = *parent;
	PbMsg *fs = pb_msg(options, features_field);
	for (int i = 1; i <= SW_FEAT_MAX; i++)
		if (pb_has(fs, (uint32_t)i))
			r.v[i] = (int32_t)pb_int(fs, (uint32_t)i, 0);
	return r;
}

/* resolve(_ proto: FieldDescriptorProto, resolvedParent:) */
static SwFeatures resolve_field(const Resolver *r, const PbMsg *proto, const SwFeatures *parent)
{
	PbMsg *options = pb_msg(proto, FD_OPTIONS);
	if (r->edition >= SW_EDITION_2023)
		return resolve(parent, options, FEAT_FIELD);
	/* InferLegacyProtoFeatures, then the defaults. */
	SwFeatures f = r->defaults;
	if (pb_int(proto, FD_LABEL, LABEL_OPTIONAL) == LABEL_REQUIRED)
		f.v[SW_FEAT_FIELD_PRESENCE] = SW_PRESENCE_LEGACY_REQUIRED;
	if (pb_int(proto, FD_TYPE, PT_DOUBLE) == PT_GROUP)
		f.v[SW_FEAT_MESSAGE_ENCODING] = SW_ENCODING_DELIMITED;
	if (pb_int(options, FO_PACKED, 0))
		f.v[SW_FEAT_REPEATED_FIELD_ENCODING] = SW_REPEATED_PACKED;
	if (r->edition == SW_EDITION_PROTO3 && pb_has(options, FO_PACKED) && !pb_int(options, FO_PACKED, 0))
		f.v[SW_FEAT_REPEATED_FIELD_ENCODING] = SW_REPEATED_EXPANDED;
	return f;
}

static const char *scoped(const char *scope, const char *name) { return *scope ? xcat(scope, ".", name) : name; }

/* ---- unsupported input ----
 * The port treats names as NUL-terminated byte strings: lowercasing is ASCII only and strings
 * compare by bytes, where Swift compares Characters by Unicode canonical equivalence and lowercases
 * all of Unicode. Both agree for ASCII, so a name (or file name, package, type reference,
 * swift_prefix) with a non-ASCII character or a NUL byte fails generation (protoc and buf only
 * produce ASCII names). */

/* s as a Swift string literal, for error messages. */
COLD char *sw_quote(Str s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		if (c == 0)
			buf_adds(&b, "\\0");
		else if (c == '"' || c == '\\')
			buf_fmt(&b, "\\%c", c);
		else if (c < 0x20 || c == 0x7f)
			buf_fmt(&b, "\\u{%x}", c);
		else
			buf_addc(&b, (char)c);
	}
	buf_addc(&b, '"');
	return xstrdup(buf_str(&b));
}

/* Unicode.Scalar._escaped(asASCII: false) per scalar, for ASCII strings: printable ASCII stays,
 * \0 \t \n \r \" \' \\ escape, other controls become \u{XY}. Upstream also escapes a non-ASCII
 * scalar that would join the preceding quote or escape in a grapheme cluster. */
COLD char *sw_debug_description(Str s)
{
	Buf b = {0};
	buf_addc(&b, '"');
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		switch (c) {
		case '\\': buf_adds(&b, "\\\\"); break;
		case '\'': buf_adds(&b, "\\'"); break;
		case '"': buf_adds(&b, "\\\""); break;
		case 0: buf_adds(&b, "\\0"); break;
		case '\n': buf_adds(&b, "\\n"); break;
		case '\r': buf_adds(&b, "\\r"); break;
		case '\t': buf_adds(&b, "\\t"); break;
		default:
			if (c >= 0x80)
				return NULL;
			if (c < 0x20 || c == 0x7f) {
				buf_adds(&b, "\\u{");
				buf_addc(&b, "0123456789ABCDEF"[c >> 4]);
				buf_addc(&b, "0123456789ABCDEF"[c & 15]);
				buf_addc(&b, '}');
			} else
				buf_addc(&b, (char)c);
		}
	}
	buf_addc(&b, '"');
	return xstrdup(buf_str(&b));
}

NORETURN COLD static void unsupported(const char *kind, const char *what, Str s)
{
	fail("unsupported by protoc-gen-uni: %s%s %s", kind, what, sw_quote(s));
}

/* Returns s if it has no byte >= 0x80 and no NUL. */
const char *sw_ascii_name(Str s, const char *what)
{
	for (size_t i = 0; i < s.n; i++) {
		unsigned char c = (unsigned char)s.p[i];
		if (c == 0 || c >= 0x80)
			unsupported(c ? "non-ASCII " : "NUL byte in ", what, s);
	}
	return s.p;
}

static const char *name_at(const PbMsg *m, uint32_t num, int i, const char *what)
{
	return sw_ascii_name(pb_bytes_at(m, num, i), what);
}

static const char *name_of(const PbMsg *m, uint32_t num, const char *what) { return name_at(m, num, 0, what); }

/* A string the port reads up to the first NUL. */
static const char *no_nul(Str s, const char *what)
{
	if (memchr(s.p, 0, s.n))
		unsupported("NUL byte in ", what, s);
	return s.p;
}

/* FieldDescriptorProto.json_name: compared with the (ASCII) field name and its default JSON name,
 * by canonical equivalence in Swift. A non-ASCII string is canonically equivalent to an ASCII one
 * only through the singletons U+212A KELVIN SIGN (K), U+037E GREEK QUESTION MARK (;) and U+1FEF
 * GREEK VARIA (`), so only these are not supported. */
static const char *json_name(Str s)
{
	static const char *const singletons[] = {"\xe2\x84\xaa", "\xcd\xbe", "\xe1\xbf\xaf"};
	for (size_t i = 0; i < s.n; i++)
		for (size_t k = 0; k < sizeof singletons / sizeof singletons[0]; k++)
			if (str_has_prefix((Str){s.p + i, s.n - i}, str_c(singletons[k])))
				fail("unsupported by protoc-gen-uni: FieldDescriptorProto.json_name %s has a character "
				     "that is canonically equivalent to ASCII",
				     sw_quote(s));
	return no_nul(s, "FieldDescriptorProto.json_name");
}

/* check: Range(start..<end) and ClosedRange(start...end) trap on reversed bounds. */
static SwRange *ranges(const PbMsg *proto, uint32_t num, int *n, bool check)
{
	*n = pb_count(proto, num);
	SwRange *r = xalloc(sizeof(SwRange) * (size_t)(*n ? *n : 1));
	for (int i = 0; i < *n; i++) {
		PbMsg *m = pb_msg_at(proto, num, i);
		r[i].start = (int32_t)pb_int(m, 1, 0);
		r[i].end = (int32_t)pb_int(m, 2, 0);
		if (check && r[i].end < r[i].start)
			fail("Range requires lowerBound <= upperBound");
	}
	return r;
}

/* ---- construction ---- */

typedef struct {
	SwDescriptorSet *set;
	Resolver resolver;
} Builder;

static SwEnum *new_enum(Builder *b, PbMsg *proto, int index, const SwFeatures *parent, const char *scope)
{
	SwEnum *e = xalloc(sizeof *e);
	e->name = name_of(proto, EDP_NAME, "EnumDescriptorProto.name");
	e->full_name = scoped(scope, e->name);
	e->index = index;
	e->options = pb_msg(proto, EDP_OPTIONS);
	e->features = resolve(parent, e->options, FEAT_ENUM);
	e->deprecated = pb_int(e->options, EO_DEPRECATED, 0) != 0;
	int n;
	ranges(proto, EDP_RESERVED_RANGE, &n, true);
	for (int i = 0; i < pb_count(proto, EDP_VALUE); i++) {
		PbMsg *vp = pb_msg_at(proto, EDP_VALUE, i);
		SwEnumValue *v = xalloc(sizeof *v);
		v->name = name_of(vp, EVDP_NAME, "EnumValueDescriptorProto.name");
		v->full_name = scoped(scope, v->name);
		v->index = i;
		v->number = (int32_t)pb_int(vp, EVDP_NUMBER, 0);
		v->options = pb_msg(vp, EVDP_OPTIONS);
		v->features = resolve(&e->features, v->options, FEAT_ENUM_VALUE);
		v->deprecated = pb_int(v->options, EVO_DEPRECATED, 0) != 0;
		PbReader r = pb_unknown_fields(v->options);
		PbRawField f;
		while (pb_next_field(&r, &f)) {
			if (f.num != EXT_ENUMVALUE_JSON || f.wt != WT_LEN)
				continue;
			v->has_json = true;
			if (!v->json)
				v->json = "";
			PbReader ir = {f.val, f.val + f.vlen};
			PbRawField g;
			while (pb_next_field(&ir, &g))
				if (g.num == 1 && g.wt == WT_LEN)
					v->json = no_nul((Str){xstrndup((const char *)g.val, g.vlen), g.vlen}, "option (pb.enumvalue.json)");
		}
		vec_push(&e->values, v);
	}
	map_put(&b->set->enum_map, xcat(".", e->full_name), e);
	for (int i = 0; i < e->values.n; i++)
		VEC_AT(e->values, SwEnumValue, i)->enum_type = e;
	return e;
}

static SwField *new_field(Builder *b, PbMsg *proto, int index, const SwFeatures *parent, bool is_extension)
{
	SwField *f = xalloc(sizeof *f);
	f->name = name_of(proto, FD_NAME, "FieldDescriptorProto.name");
	const char *extendee = name_of(proto, FD_EXTENDEE, "FieldDescriptorProto.extendee");
	if (is_extension ? !*extendee || pb_has(proto, FD_ONEOF_INDEX)
			 : *extendee || (!pb_has(proto, FD_ONEOF_INDEX) && pb_int(proto, FD_PROTO3_OPTIONAL, 0)))
		fail("invalid field %s", f->name);
	f->index = index;
	f->features = resolve_field(&b->resolver, proto, parent);
	f->has_default = pb_has(proto, FD_DEFAULT_VALUE);
	f->default_value = pb_bytes(proto, FD_DEFAULT_VALUE);
	int raw_type = (int)pb_int(proto, FD_TYPE, PT_DOUBLE);
	if (raw_type != PT_STRING && raw_type != PT_BYTES) /* numbers, inf/nan, enum value names */
		sw_ascii_name(f->default_value, "FieldDescriptorProto.default_value");
	if (!pb_has(proto, FD_JSON_NAME))
		fail("field %s has no json_name", f->name); /* precondition(proto.hasJsonName) */
	f->json_name = json_name(pb_bytes(proto, FD_JSON_NAME));
	f->is_extension = is_extension;
	f->number = (int32_t)pb_int(proto, FD_NUMBER, 0);
	int type = (int)pb_int(proto, FD_TYPE, PT_DOUBLE);
	f->type = type == PT_MESSAGE && f->features.v[SW_FEAT_MESSAGE_ENCODING] == SW_ENCODING_DELIMITED ? PT_GROUP : type;
	int label = (int)pb_int(proto, FD_LABEL, LABEL_OPTIONAL);
	f->label = f->features.v[SW_FEAT_FIELD_PRESENCE] == SW_PRESENCE_LEGACY_REQUIRED && label == LABEL_OPTIONAL
			   ? LABEL_REQUIRED
			   : label;
	f->options = pb_msg(proto, FD_OPTIONS);
	f->deprecated = pb_int(f->options, FO_DEPRECATED, 0) != 0;
	f->has_oneof_index = pb_has(proto, FD_ONEOF_INDEX);
	f->oneof_index = (int32_t)pb_int(proto, FD_ONEOF_INDEX, 0);
	f->proto3_optional = pb_int(proto, FD_PROTO3_OPTIONAL, 0) != 0;
	if (is_extension)
		f->extendee = extendee;
	if (f->type == PT_GROUP || f->type == PT_MESSAGE || f->type == PT_ENUM)
		f->type_name = name_of(proto, FD_TYPE_NAME, "FieldDescriptorProto.type_name");
	return f;
}

static SwMessage *new_message(Builder *b, PbMsg *proto, int index, const SwFeatures *parent, const char *scope)
{
	SwMessage *m = xalloc(sizeof *m);
	m->name = name_of(proto, DP_NAME, "DescriptorProto.name");
	m->full_name = scoped(scope, m->name);
	m->index = index;
	m->options = pb_msg(proto, DP_OPTIONS);
	m->features = resolve(parent, m->options, FEAT_MESSAGE);
	m->is_any = str_eq(m->full_name, "google.protobuf.Any");
	m->message_set_wire_format = pb_int(m->options, MO_MESSAGE_SET_WIRE_FORMAT, 0) != 0;
	m->deprecated = pb_int(m->options, MO_DEPRECATED, 0) != 0;
	m->map_entry = pb_int(m->options, MO_MAP_ENTRY, 0) != 0;
	m->reserved_ranges = ranges(proto, DP_RESERVED_RANGE, &m->n_reserved_ranges, true);
	for (int i = 0; i < pb_count(proto, DP_RESERVED_NAME); i++)
		vec_push(&m->reserved_names, (void *)no_nul(pb_bytes_at(proto, DP_RESERVED_NAME, i), "DescriptorProto.reserved_name"));
	int n_oneofs = pb_count(proto, DP_ONEOF_DECL);
	SwFeatures *oneof_features = xalloc(sizeof(SwFeatures) * (size_t)(n_oneofs ? n_oneofs : 1));
	for (int i = 0; i < n_oneofs; i++)
		oneof_features[i] = resolve(&m->features, pb_msg(pb_msg_at(proto, DP_ONEOF_DECL, i), ODP_OPTIONS), FEAT_ONEOF);
	m->extension_ranges = ranges(proto, DP_EXTENSION_RANGE, &m->n_extension_ranges, false);
	for (int i = 0; i < pb_count(proto, DP_ENUM_TYPE); i++)
		vec_push(&m->enums, new_enum(b, pb_msg_at(proto, DP_ENUM_TYPE, i), i, &m->features, m->full_name));
	for (int i = 0; i < pb_count(proto, DP_NESTED_TYPE); i++)
		vec_push(&m->messages, new_message(b, pb_msg_at(proto, DP_NESTED_TYPE, i), i, &m->features, m->full_name));
	for (int i = 0; i < pb_count(proto, DP_FIELD); i++) {
		PbMsg *fp = pb_msg_at(proto, DP_FIELD, i);
		bool in_real_oneof = pb_has(fp, FD_ONEOF_INDEX) && !pb_int(fp, FD_PROTO3_OPTIONAL, 0);
		const SwFeatures *pf = &m->features;
		if (in_real_oneof) {
			int64_t oi = pb_int(fp, FD_ONEOF_INDEX, 0);
			if (oi < 0 || oi >= n_oneofs)
				fail("%s: oneof_index out of range", m->full_name);
			pf = &oneof_features[oi];
		}
		vec_push(&m->fields, new_field(b, fp, i, pf, false));
	}
	for (int i = 0; i < n_oneofs; i++) {
		PbMsg *op = pb_msg_at(proto, DP_ONEOF_DECL, i);
		SwOneof *o = xalloc(sizeof *o);
		o->name = name_of(op, ODP_NAME, "OneofDescriptorProto.name");
		o->index = i;
		o->features = oneof_features[i];
		o->options = pb_msg(op, ODP_OPTIONS);
		vec_push(&m->oneofs, o);
	}
	for (int i = 0; i < pb_count(proto, DP_EXTENSION); i++)
		vec_push(&m->extensions, new_field(b, pb_msg_at(proto, DP_EXTENSION, i), i, &m->features, true));
	map_put(&b->set->message_map, xcat(".", m->full_name), m);
	return m;
}

static SwService *new_service(Builder *b, PbMsg *proto, int index, const SwFeatures *file_features, const char *scope)
{
	SwService *s = xalloc(sizeof *s);
	s->name = name_of(proto, SDP_NAME, "ServiceDescriptorProto.name");
	s->full_name = scoped(scope, s->name);
	s->index = index;
	s->options = pb_msg(proto, SDP_OPTIONS);
	s->features = resolve(file_features, s->options, FEAT_SERVICE);
	s->deprecated = pb_int(s->options, SO_DEPRECATED, 0) != 0;
	for (int i = 0; i < pb_count(proto, SDP_METHOD); i++) {
		PbMsg *mp = pb_msg_at(proto, SDP_METHOD, i);
		SwMethod *m = xalloc(sizeof *m);
		m->name = name_of(mp, MDP_NAME, "MethodDescriptorProto.name");
		m->index = i;
		m->options = pb_msg(mp, MDP_OPTIONS);
		m->features = resolve(&s->features, m->options, FEAT_METHOD);
		m->deprecated = pb_int(m->options, MO2_DEPRECATED, 0) != 0;
		m->client_streaming = pb_int(mp, MDP_CLIENT_STREAMING, 0) != 0;
		m->server_streaming = pb_int(mp, MDP_SERVER_STREAMING, 0) != 0;
		m->input_type = map_get(&b->set->message_map, name_of(mp, MDP_INPUT_TYPE, "MethodDescriptorProto.input_type"));
		m->output_type = map_get(&b->set->message_map, name_of(mp, MDP_OUTPUT_TYPE, "MethodDescriptorProto.output_type"));
		if (!m->input_type || !m->output_type)
			fail("%s.%s: unknown input or output type", s->full_name, m->name);
		m->service = s;
		vec_push(&s->methods, m);
	}
	map_put(&b->set->service_map, xcat(".", s->full_name), s);
	return s;
}

/* ---- bind ---- */

static void bind_field(SwDescriptorSet *set, SwField *f, SwFile *file, SwMessage *containing_type)
{
	f->file = file;
	if (f->is_extension) {
		f->containing_type = map_get(&set->message_map, f->extendee);
		if (!f->containing_type)
			fail("%s: unknown extendee %s", f->name, f->extendee);
		f->extension_scope = containing_type;
	} else {
		f->containing_type = containing_type;
	}
	if (f->type_name) {
		if (f->type == PT_ENUM) {
			f->enum_type = map_get(&set->enum_map, f->type_name);
			if (!f->enum_type)
				fail("%s: unknown enum %s", f->name, f->type_name);
		} else {
			SwMessage *t = map_get(&set->message_map, f->type_name);
			if (!t)
				fail("%s: unknown message %s", f->name, f->type_name);
			f->message_type = t;
			if (f->type == PT_GROUP && (t->map_entry || (f->containing_type && f->containing_type->map_entry)))
				f->type = PT_MESSAGE;
		}
	}
}

static void bind_enum(SwEnum *e, SwFile *file, SwMessage *containing_type)
{
	e->file = file;
	e->containing_type = containing_type;
}

static void bind_message(SwDescriptorSet *set, SwMessage *m, SwFile *file, SwMessage *containing_type)
{
	m->file = file;
	m->containing_type = containing_type;
	for (int i = 0; i < m->enums.n; i++)
		bind_enum(m->enums.v[i], file, m);
	for (int i = 0; i < m->messages.n; i++)
		bind_message(set, m->messages.v[i], file, m);
	for (int i = 0; i < m->fields.n; i++)
		bind_field(set, m->fields.v[i], file, m);
	for (int i = 0; i < m->oneofs.n; i++)
		VEC_AT(m->oneofs, SwOneof, i)->containing_type = m;
	for (int i = 0; i < m->extensions.n; i++)
		bind_field(set, m->extensions.v[i], file, m);

	/* OneofDescriptor.fields, _isSynthetic and realOneofs (lazy upstream). Synthetic oneofs come
	 * after the real ones. */
	bool seen_synthetic = false;
	for (int i = 0; i < m->oneofs.n; i++) {
		SwOneof *o = m->oneofs.v[i];
		for (int j = 0; j < m->fields.n; j++) {
			SwField *f = m->fields.v[j];
			if (f->has_oneof_index && f->oneof_index == o->index)
				vec_push(&o->fields, f);
		}
		o->is_synthetic = o->fields.n == 1 && VEC_AT(o->fields, SwField, 0)->proto3_optional;
		if (seen_synthetic && !o->is_synthetic)
			fail("%s: synthetic oneofs must come last", m->full_name);
		seen_synthetic = o->is_synthetic;
		if (!o->is_synthetic)
			vec_push(&m->real_oneofs, o);
	}
}

static SwFile *new_file(SwDescriptorSet *set, PbMsg *proto, const SwEditionDefaults *defaults)
{
	SwFile *f = xalloc(sizeof *f);
	f->proto = proto;
	f->name = name_of(proto, FDP_NAME, "FileDescriptorProto.name");
	f->package = name_of(proto, FDP_PACKAGE, "FileDescriptorProto.package");
	if (pb_has(proto, FDP_EDITION)) {
		f->edition = (int32_t)pb_int(proto, FDP_EDITION, 0);
	} else {
		Str syntax = pb_bytes(proto, FDP_SYNTAX);
		if (syntax.n == 0 || str_eqc(syntax, "proto2"))
			f->edition = SW_EDITION_PROTO2;
		else if (str_eqc(syntax, "proto3"))
			f->edition = SW_EDITION_PROTO3;
		else
			fail("protoc provided an expected value (\"%s\") for syntax/edition: %s", syntax.p, f->name);
	}
	Builder b = {set, {0}};
	resolver_init(&b.resolver, defaults, f->edition, f->name);
	f->options = pb_msg(proto, FDP_OPTIONS);
	f->features = resolve(&b.resolver.defaults, f->options, FEAT_FILE);
	f->deprecated = pb_int(f->options, FDP_OPT_DEPRECATED, 0) != 0;

	for (int i = 0; i < pb_count(proto, FDP_ENUM_TYPE); i++)
		vec_push(&f->enums, new_enum(&b, pb_msg_at(proto, FDP_ENUM_TYPE, i), i, &f->features, f->package));
	for (int i = 0; i < pb_count(proto, FDP_MESSAGE_TYPE); i++)
		vec_push(&f->messages, new_message(&b, pb_msg_at(proto, FDP_MESSAGE_TYPE, i), i, &f->features, f->package));
	for (int i = 0; i < pb_count(proto, FDP_EXTENSION); i++)
		vec_push(&f->extensions, new_field(&b, pb_msg_at(proto, FDP_EXTENSION, i), i, &f->features, true));
	for (int i = 0; i < pb_count(proto, FDP_SERVICE); i++)
		vec_push(&f->services, new_service(&b, pb_msg_at(proto, FDP_SERVICE, i), i, &f->features, f->package));

	for (int i = 0; i < pb_count(proto, FDP_DEPENDENCY); i++) {
		const char *dep_name = name_at(proto, FDP_DEPENDENCY, i, "FileDescriptorProto.dependency");
		SwFile *dep = map_get(&set->file_map, dep_name);
		if (!dep)
			fail("%s: dependency %s not found", f->name, dep_name);
		vec_push(&f->dependencies, dep);
	}
	for (int i = 0; i < pb_count(proto, FDP_PUBLIC_DEPENDENCY); i++) {
		int64_t j = pb_int_at(proto, FDP_PUBLIC_DEPENDENCY, i);
		if (j < 0 || j >= f->dependencies.n)
			fail("%s: public dependency index out of range", f->name);
		vec_push(&f->public_dependencies, f->dependencies.v[j]);
	}
	for (int i = 0; i < pb_count(proto, FDP_WEAK_DEPENDENCY); i++) {
		int64_t j = pb_int_at(proto, FDP_WEAK_DEPENDENCY, i);
		if (j < 0 || j >= f->dependencies.n)
			fail("%s: weak dependency index out of range", f->name);
	}

	map_put(&set->file_map, f->name, f);

	for (int i = 0; i < f->enums.n; i++)
		bind_enum(f->enums.v[i], f, NULL);
	for (int i = 0; i < f->messages.n; i++)
		bind_message(set, f->messages.v[i], f, NULL);
	for (int i = 0; i < f->extensions.n; i++)
		bind_field(set, f->extensions.v[i], f, NULL);
	for (int i = 0; i < f->services.n; i++)
		VEC_AT(f->services, SwService, i)->file = f;
	return f;
}

SwDescriptorSet *sw_descriptor_set(PbMsg *req, const SwEditionDefaults *defaults)
{
	SwDescriptorSet *set = xalloc(sizeof *set);
	if (!defaults)
		defaults = &sw_edition_defaults;
	for (int i = 0; i < pb_count(req, CGR_PROTO_FILE); i++)
		vec_push(&set->files, new_file(set, pb_msg_at(req, CGR_PROTO_FILE, i), defaults));
	return set;
}

SwFile *sw_file_named(SwDescriptorSet *set, const char *name) { return map_get(&set->file_map, name); }

/* ---- FieldDescriptor / EnumDescriptor / Descriptor helpers ---- */

bool sw_enum_is_closed(const SwEnum *e) { return e->features.v[SW_FEAT_ENUM_TYPE] == SW_ENUM_TYPE_CLOSED; }

bool sw_field_is_required(const SwField *f)
{
	return f->features.v[SW_FEAT_FIELD_PRESENCE] == SW_PRESENCE_LEGACY_REQUIRED;
}

bool sw_field_is_repeated(const SwField *f) { return f->label == LABEL_REPEATED; }

bool sw_field_is_packable_type(int type)
{
	return type != PT_STRING && type != PT_GROUP && type != PT_MESSAGE && type != PT_BYTES;
}

bool sw_field_is_packed(const SwField *f)
{
	return sw_field_is_repeated(f) && sw_field_is_packable_type(f->type) &&
	       f->features.v[SW_FEAT_REPEATED_FIELD_ENCODING] == SW_REPEATED_PACKED;
}

bool sw_field_is_map(const SwField *f) { return f->type == PT_MESSAGE && f->message_type->map_entry; }

bool sw_field_has_presence(const SwField *f)
{
	if (sw_field_is_repeated(f))
		return false;
	if (f->type == PT_GROUP || f->type == PT_MESSAGE)
		return true;
	return f->is_extension || f->has_oneof_index || f->features.v[SW_FEAT_FIELD_PRESENCE] != SW_PRESENCE_IMPLICIT;
}

SwOneof *sw_field_containing_oneof(const SwField *f)
{
	if (!f->has_oneof_index)
		return NULL;
	if (!f->containing_type || f->oneof_index < 0 || f->oneof_index >= f->containing_type->oneofs.n)
		fail("%s: oneof_index out of range", f->name);
	return f->containing_type->oneofs.v[f->oneof_index];
}

SwOneof *sw_field_real_containing_oneof(const SwField *f)
{
	SwOneof *o = sw_field_containing_oneof(f);
	return o && !o->is_synthetic ? o : NULL;
}

char *sw_field_full_name(const SwField *f)
{
	if (!f->is_extension)
		return xcat(f->containing_type->full_name, ".", f->name);
	if (f->extension_scope)
		return xcat(f->extension_scope->full_name, ".", f->name);
	return *f->file->package ? xcat(f->file->package, ".", f->name) : xstrdup(f->name);
}

bool sw_field_is_group_like(const SwField *f)
{
	if (f->type != PT_GROUP)
		return false;
	const SwMessage *t = f->message_type;
	if (!str_eq(f->name, sw_ascii_lower(t->name)))
		return false;
	if (f->is_extension) {
		if (!f->extension_scope)
			return t->file == f->file && !t->containing_type;
		return t->containing_type == f->extension_scope;
	}
	return t->containing_type == f->containing_type;
}

const char *sw_field_naming_base(const SwField *f) { return sw_field_is_group_like(f) ? f->message_type->name : f->name; }

bool sw_message_map_key_value(const SwMessage *m, SwField **key, SwField **value)
{
	if (!m->map_entry)
		return false;
	if (m->fields.n != 2)
		fail("%s: map entry must have two fields", m->full_name); /* precondition(fields.count == 2) */
	*key = m->fields.v[0];
	*value = m->fields.v[1];
	return true;
}
