/* Unit tests of pkg/proto. Expected values come from the upstream runtimes: protoc 36.2's own
 * edition defaults (src/google/protobuf/cpp_edition_defaults.h) and protobuf-go v1.36.12's
 * (internal/editiondefaults/editions_defaults.binpb, made by protoc) as protoc resolves them,
 * protoc's ToJsonName, FieldDescriptor predicates and source paths (descriptor.cc), and the
 * well-known type files of protobuf v36.2. */
#include "proto.h"

#include "test.h"

/* ---- FeatureSet with the pb.cpp (1000) and pb.go (1002) extensions ---- */

static const PbField f_cpp[] = {
	{1, PT_BOOL, PL_OPT, NULL, NULL, 0}, {2, PT_ENUM, PL_OPT, NULL, NULL, 0},
	{3, PT_BOOL, PL_OPT, NULL, NULL, 0}, {4, PT_ENUM, PL_OPT, NULL, NULL, 0},
};
static const PbMsgDef d_cpp = {4, f_cpp};
static const PbField f_go[] = {
	{1, PT_BOOL, PL_OPT, NULL, NULL, 0}, {2, PT_ENUM, PL_OPT, NULL, NULL, 0}, {3, PT_ENUM, PL_OPT, NULL, NULL, 0},
};
static const PbMsgDef d_go = {3, f_go};
static const PbField f_features[] = {
	{1, PT_ENUM, PL_OPT, NULL, NULL, 0}, {2, PT_ENUM, PL_OPT, NULL, NULL, 0}, {3, PT_ENUM, PL_OPT, NULL, NULL, 0},
	{4, PT_ENUM, PL_OPT, NULL, NULL, 0}, {5, PT_ENUM, PL_OPT, NULL, NULL, 0}, {6, PT_ENUM, PL_OPT, NULL, NULL, 0},
	{7, PT_ENUM, PL_OPT, NULL, NULL, 0}, {8, PT_ENUM, PL_OPT, NULL, NULL, 0}, {9, PT_ENUM, PL_OPT, NULL, NULL, 0},
	{1000, PT_MESSAGE, PL_OPT, &d_cpp, NULL, 0}, {1002, PT_MESSAGE, PL_OPT, &d_go, NULL, 0},
};
static const PbMsgDef d_features = {11, f_features};

/* PROTOBUF_INTERNAL_CPP_EDITION_DEFAULTS of protobuf v36.2 (FeatureSet with pb.cpp) */
static const char cpp_edition_defaults[] =
	"\n\'\030\204\007\"\003\302>\000*\035\010\001\020\002\030\002 \003(\0010\0028\002@\001H\001\302>\010\010\001\020\003\030\000 \001\n\'"
	"\030\347\007\"\003\302>\000*\035\010\002\020\001\030\001 \002(\0010\0018\002@\001H\001\302>\010\010\000\020\003\030\000 \001\n\'"
	"\030\350\007\"\023\010\001\020\001\030\001 \002(\0010\001\302>\004\010\000\020\003*\r8\002@\001H\001\302>\004\030\000 \001\n\'"
	"\030\351\007\"\031\010\001\020\001\030\001 \002(\0010\0018\001@\002\302>\006\010\000\020\001\030\001*\007H\001\302>\002 \001\n\'"
	"\030\352\007\"\033\010\001\020\001\030\001 \002(\0010\0018\003@\004H\002\302>\006\010\000\020\001\030\001*\005\302>\002 \001\n\'"
	"\030\217N\"\035\010\001\020\001\030\001 \002(\0010\0018\003@\004H\002\302>\010\010\000\020\001\030\001 \001*\003\302>\000 \346\007(\352\007";

/* internal/editiondefaults/editions_defaults.binpb of protobuf-go v1.36.12 (with pb.go) */
static const uint8_t go_edition_defaults[] = {
	0x0a, 0x23, 0x18, 0x84, 0x07, 0x22, 0x03, 0xd2, 0x3e, 0x00, 0x2a, 0x19, 0x08, 0x01, 0x10, 0x02, 0x18, 0x02, 0x20,
	0x03, 0x28, 0x01, 0x30, 0x02, 0x38, 0x02, 0x40, 0x01, 0xd2, 0x3e, 0x06, 0x08, 0x01, 0x10, 0x00, 0x18, 0x01, 0x0a,
	0x23, 0x18, 0xe7, 0x07, 0x22, 0x03, 0xd2, 0x3e, 0x00, 0x2a, 0x19, 0x08, 0x02, 0x10, 0x01, 0x18, 0x01, 0x20, 0x02,
	0x28, 0x01, 0x30, 0x01, 0x38, 0x02, 0x40, 0x01, 0xd2, 0x3e, 0x06, 0x08, 0x00, 0x10, 0x00, 0x18, 0x01, 0x0a, 0x23,
	0x18, 0xe8, 0x07, 0x22, 0x13, 0x08, 0x01, 0x10, 0x01, 0x18, 0x01, 0x20, 0x02, 0x28, 0x01, 0x30, 0x01, 0xd2, 0x3e,
	0x04, 0x08, 0x00, 0x10, 0x00, 0x2a, 0x09, 0x38, 0x02, 0x40, 0x01, 0xd2, 0x3e, 0x02, 0x18, 0x01, 0x0a, 0x23, 0x18,
	0xe9, 0x07, 0x22, 0x19, 0x08, 0x01, 0x10, 0x01, 0x18, 0x01, 0x20, 0x02, 0x28, 0x01, 0x30, 0x01, 0x38, 0x01, 0x40,
	0x02, 0xd2, 0x3e, 0x06, 0x08, 0x00, 0x10, 0x03, 0x18, 0x01, 0x2a, 0x03, 0xd2, 0x3e, 0x00, 0x0a, 0x23, 0x18, 0x8f,
	0x4e, 0x22, 0x19, 0x08, 0x01, 0x10, 0x01, 0x18, 0x01, 0x20, 0x02, 0x28, 0x01, 0x30, 0x01, 0x38, 0x03, 0x40, 0x02,
	0xd2, 0x3e, 0x06, 0x08, 0x00, 0x10, 0x03, 0x18, 0x01, 0x2a, 0x03, 0xd2, 0x3e, 0x00, 0x20, 0xe6, 0x07, 0x28, 0xe9,
	0x07,
};

static PbMsg *cpp_defaults(void)
{
	PbMsg *d = proto_parse_feature_set_defaults((Str){cpp_edition_defaults, sizeof cpp_edition_defaults - 1}, &d_features);
	CHECK(d != NULL);
	return d;
}

/* Features 1..9 of a resolved FeatureSet equal want[0..8]. */
static bool features_are(const PbMsg *fs, const int want[9])
{
	for (int i = 0; i < 9; i++)
		if (proto_feature(fs, (uint32_t)(i + 1)) != want[i]) {
			fprintf(stderr, "feature %d: got %d, want %d\n", i + 1, proto_feature(fs, (uint32_t)(i + 1)), want[i]);
			return false;
		}
	return true;
}

static bool cpp_features_are(const PbMsg *fs, int legacy_closed_enum, int string_type, int sv, int f4)
{
	const PbMsg *cpp = pb_msg(fs, 1000);
	return cpp && pb_int(cpp, 1, -1) == legacy_closed_enum && pb_int(cpp, 2, -1) == string_type &&
	       pb_int(cpp, 3, -1) == sv && pb_int(cpp, 4, -1) == f4;
}

/* The FeatureSet protoc 36.2 resolves for each edition (fixed merged with overridable). */
static void test_edition_features_protoc(void)
{
	PbMsg *d = cpp_defaults();
	CHECK(pb_int(d, PROTO_FSD_MINIMUM_EDITION, 0) == PROTO_EDITION_PROTO2);
	CHECK(pb_int(d, PROTO_FSD_MAXIMUM_EDITION, 0) == PROTO_EDITION_2026);
	CHECK(pb_count(d, PROTO_FSD_DEFAULTS) == 6);

	static const int proto2[9] = {1, 2, 2, 3, 1, 2, 2, 1, 1}, proto3[9] = {2, 1, 1, 2, 1, 1, 2, 1, 1},
			 e2023[9] = {1, 1, 1, 2, 1, 1, 2, 1, 1}, e2024[9] = {1, 1, 1, 2, 1, 1, 1, 2, 1},
			 e2026[9] = {1, 1, 1, 2, 1, 1, 3, 4, 2};
	PbMsg *fs = proto_edition_features(d, PROTO_EDITION_PROTO2, &d_features);
	CHECK(features_are(fs, proto2) && cpp_features_are(fs, 1, 3, 0, 1));
	fs = proto_edition_features(d, PROTO_EDITION_PROTO3, &d_features);
	CHECK(features_are(fs, proto3) && cpp_features_are(fs, 0, 3, 0, 1));
	fs = proto_edition_features(d, PROTO_EDITION_2023, &d_features);
	CHECK(features_are(fs, e2023) && cpp_features_are(fs, 0, 3, 0, 1));
	fs = proto_edition_features(d, PROTO_EDITION_2024, &d_features);
	CHECK(features_are(fs, e2024) && cpp_features_are(fs, 0, 1, 1, 1));
	fs = proto_edition_features(d, PROTO_EDITION_2026, &d_features);
	CHECK(features_are(fs, e2026) && cpp_features_are(fs, 0, 1, 1, 1));
	fs = proto_edition_features(d, PROTO_EDITION_UNSTABLE, &d_features);
	CHECK(features_are(fs, e2026) && cpp_features_are(fs, 0, 1, 1, 1));
	/* the latest entry <= edition: LEGACY for PROTO2, 2026 for editions after it */
	CHECK(pb_int(proto_edition_defaults_entry(d, PROTO_EDITION_PROTO2), PROTO_FSED_EDITION, 0) == PROTO_EDITION_LEGACY);
	CHECK(pb_int(proto_edition_defaults_entry(d, 1005), PROTO_FSED_EDITION, 0) == PROTO_EDITION_2026);
	/* nothing before LEGACY (1_TEST_ONLY, UNKNOWN) */
	CHECK(proto_edition_features(d, 1, &d_features) == NULL);
	CHECK(proto_edition_features(d, PROTO_EDITION_UNKNOWN, &d_features) == NULL);
	/* malformed data */
	CHECK(proto_parse_feature_set_defaults(STR_LIT("\n\x05\x18"), &d_features) == NULL);
}

/* protobuf-go's defaults (made by protoc for descriptor.proto and go_features.proto) resolve to
 * the same core features. */
static void test_edition_features_go(void)
{
	PbMsg *d = proto_parse_feature_set_defaults((Str){(const char *)go_edition_defaults, sizeof go_edition_defaults},
						    &d_features);
	CHECK(d && pb_int(d, PROTO_FSD_MAXIMUM_EDITION, 0) == PROTO_EDITION_2024);
	static const int proto2[9] = {1, 2, 2, 3, 1, 2, 2, 1, 0}, proto3[9] = {2, 1, 1, 2, 1, 1, 2, 1, 0},
			 e2024[9] = {1, 1, 1, 2, 1, 1, 1, 2, 0};
	PbMsg *fs = proto_edition_features(d, PROTO_EDITION_PROTO2, &d_features);
	CHECK(features_are(fs, proto2));
	const PbMsg *go = pb_msg(fs, 1002);
	CHECK(pb_int(go, 1, 0) == 1 && pb_int(go, 3, 0) == 1); /* legacy_unmarshal_json_enum, strip_enum_prefix */
	CHECK(features_are(proto_edition_features(d, PROTO_EDITION_PROTO3, &d_features), proto3));
	fs = proto_edition_features(d, PROTO_EDITION_2024, &d_features);
	CHECK(features_are(fs, e2024) && pb_int(pb_msg(fs, 1002), 2, 0) == 3); /* api_level API_OPAQUE */
}

/* MergeFeatures: edition defaults <- parent <- child, extensions merged field by field. */
static void test_merge_features(void)
{
	PbMsg *base = proto_edition_features(cpp_defaults(), PROTO_EDITION_2023, &d_features);
	PbMsg *parent = pb_new(&d_features), *child = pb_new(&d_features);
	proto_msg_set_u(parent, PROTO_FEATURE_FIELD_PRESENCE, PROTO_PRESENCE_IMPLICIT);
	proto_msg_set_u(parent, PROTO_FEATURE_ENUM_TYPE, PROTO_ENUM_TYPE_CLOSED);
	proto_msg_set_u(child, PROTO_FEATURE_ENUM_TYPE, PROTO_ENUM_TYPE_OPEN);
	proto_msg_set_u(proto_msg_mutable(child, 1000), 2, 2); /* string_type = CORD */
	PbMsg *m = proto_merge_features(base, parent, child);
	static const int want[9] = {2, 1, 1, 2, 1, 1, 2, 1, 1};
	CHECK(features_are(m, want) && cpp_features_are(m, 0, 2, 0, 1));
	/* the inputs are unchanged */
	CHECK(proto_feature(base, PROTO_FEATURE_FIELD_PRESENCE) == PROTO_PRESENCE_EXPLICIT);
	CHECK(cpp_features_are(base, 0, 3, 0, 1));
	static const int e2023[9] = {1, 1, 1, 2, 1, 1, 2, 1, 1};
	PbMsg *copy = proto_merge_features(base, NULL, NULL);
	CHECK(copy != base && features_are(copy, e2023));
}

/* ---- descriptors ---- */

typedef struct {
	PbMsg *defaults;
	PbMsg *edition_features;
	ProtoFile *file;
} Fixture;

static PbMsg *field_proto(const char *name, int number, int type, int label)
{
	PbMsg *p = pb_new(&pbd_FieldDescriptorProto);
	proto_msg_set_cs(p, FD_NAME, name);
	proto_msg_set_u(p, FD_NUMBER, (uint64_t)number);
	proto_msg_set_u(p, FD_TYPE, (uint64_t)type);
	proto_msg_set_u(p, FD_LABEL, (uint64_t)label);
	return p;
}

/* Fills a field like a builder: names, legacy features, resolved features, the label and type
 * after them. */
static void init_field(ProtoField *f, ProtoFile *file, ProtoMessage *m, PbMsg *proto, int index)
{
	f->kind = PROTO_FIELD;
	f->index = index;
	f->file = file;
	f->containing_type = m;
	f->proto = proto;
	f->name = pb_str(proto, FD_NAME);
	f->full_name = xcat(m->full_name, ".", f->name);
	f->json_name = proto_json_name(f->name);
	f->number = (int)pb_int(proto, FD_NUMBER, 0);
	f->type = (int)pb_int(proto, FD_TYPE, 0);
	f->label = (int)pb_int(proto, FD_LABEL, LABEL_OPTIONAL);
	f->proto3_optional = pb_int(proto, FD_PROTO3_OPTIONAL, 0) != 0;
	f->options = pb_msg(proto, FD_OPTIONS);
}

static void resolve_field(ProtoField *f, const PbMsg *edition_features)
{
	PbMsg *own = pb_new(&d_features);
	proto_infer_legacy_field_features(own, f->proto, f->options, f->file->edition);
	f->merged_features = proto_merge_features(edition_features, proto_field_parent_features(f), own);
	if (proto_feature(f->merged_features, PROTO_FEATURE_FIELD_PRESENCE) == PROTO_PRESENCE_LEGACY_REQUIRED)
		f->label = LABEL_REQUIRED;
}

/* proto3: message M { int32 a = 1; optional int32 b = 2; repeated int32 c = 3; repeated string d = 4;
 * map<string, int32> e = 5; M sub = 6; oneof o { int32 f = 7; } repeated int32 g = 8 [packed = false];
 * message EEntry {...} enum E { E_ZERO = 0; E_ONE = 1; } } */
static ProtoFile *proto3_file(void)
{
	PbMsg *defaults = cpp_defaults();
	PbMsg *ef = proto_edition_features(defaults, PROTO_EDITION_PROTO3, &d_features);
	ProtoFile *file = xalloc(sizeof *file);
	file->kind = PROTO_FILE;
	file->name = "a.proto";
	file->package = file->full_name = "pkg";
	file->edition = PROTO_EDITION_PROTO3;
	file->merged_features = proto_merge_features(ef, NULL, NULL);
	file->n_messages = 1;
	ProtoMessage *m = file->messages = xalloc(sizeof *m);
	m->kind = PROTO_MESSAGE;
	m->file = file;
	m->name = "M";
	m->full_name = "pkg.M";
	m->merged_features = file->merged_features;
	m->n_nested = 1;
	ProtoMessage *entry = m->nested = xalloc(sizeof *entry);
	entry->kind = PROTO_MESSAGE;
	entry->file = file;
	entry->containing_type = m;
	entry->name = "EEntry";
	entry->full_name = "pkg.M.EEntry";
	PbMsg *dp = pb_new(&pbd_DescriptorProto);
	entry->options = proto_msg_mutable(dp, DP_OPTIONS);
	proto_msg_set_u(entry->options, PROTO_MSGOPT_MAP_ENTRY, 1);
	entry->merged_features = m->merged_features;

	m->n_oneofs = 2;
	m->n_real_oneofs = 1;
	m->oneofs = xalloc(sizeof(ProtoOneof) * 2);
	const char *oneof_names[2] = {"o", "_b"};
	for (int i = 0; i < 2; i++) {
		m->oneofs[i].kind = PROTO_ONEOF;
		m->oneofs[i].index = i;
		m->oneofs[i].containing_type = m;
		m->oneofs[i].name = oneof_names[i];
		m->oneofs[i].merged_features = m->merged_features;
	}

	PbMsg *protos[8] = {
		field_proto("a", 1, PT_INT32, LABEL_OPTIONAL),  field_proto("b", 2, PT_INT32, LABEL_OPTIONAL),
		field_proto("c", 3, PT_INT32, LABEL_REPEATED),  field_proto("d", 4, PT_STRING, LABEL_REPEATED),
		field_proto("e", 5, PT_MESSAGE, LABEL_REPEATED), field_proto("sub", 6, PT_MESSAGE, LABEL_OPTIONAL),
		field_proto("f", 7, PT_INT32, LABEL_OPTIONAL),  field_proto("g", 8, PT_INT32, LABEL_REPEATED),
	};
	proto_msg_set_u(protos[1], FD_PROTO3_OPTIONAL, 1);
	proto_msg_set_u(proto_msg_mutable(protos[7], FD_OPTIONS), PROTO_FOPT_PACKED, 0);
	m->n_fields = 8;
	m->fields = xalloc(sizeof(ProtoField) * 8);
	for (int i = 0; i < 8; i++)
		init_field(&m->fields[i], file, m, protos[i], i);
	m->fields[4].message_type = entry;
	m->fields[5].message_type = m;
	m->fields[6].containing_oneof = &m->oneofs[0];
	m->oneofs[0].first_field = 6;
	m->oneofs[0].n_fields = 1;
	m->fields[1].containing_oneof = &m->oneofs[1];
	m->oneofs[1].first_field = 1;
	m->oneofs[1].n_fields = 1;
	for (int i = 0; i < 8; i++)
		resolve_field(&m->fields[i], ef);

	m->n_enums = 1;
	ProtoEnum *e = m->enums = xalloc(sizeof *e);
	e->kind = PROTO_ENUM;
	e->file = file;
	e->containing_type = m;
	e->name = "E";
	e->full_name = "pkg.M.E";
	e->merged_features = m->merged_features;
	e->n_values = 2;
	e->values = xalloc(sizeof(ProtoEnumValue) * 2);
	const char *value_names[2] = {"E_ZERO", "E_ONE"};
	for (int i = 0; i < 2; i++) {
		e->values[i].kind = PROTO_ENUM_VALUE;
		e->values[i].index = i;
		e->values[i].type = e;
		e->values[i].name = value_names[i];
		e->values[i].full_name = xcat("pkg.M.", value_names[i]);
		e->values[i].number = i;
	}
	return file;
}

static void test_field_predicates_proto3(void)
{
	ProtoFile *file = proto3_file();
	ProtoMessage *m = &file->messages[0];
	ProtoField *a = &m->fields[0], *b = &m->fields[1], *c = &m->fields[2], *d = &m->fields[3], *e = &m->fields[4],
		   *sub = &m->fields[5], *f = &m->fields[6], *g = &m->fields[7];
	/* implicit presence in proto3, except for messages, oneof members and proto3 optional */
	CHECK(!proto_field_has_presence(a) && proto_field_has_presence(b) && proto_field_has_presence(sub) &&
	      proto_field_has_presence(f));
	CHECK(!proto_field_has_presence(c) && !proto_field_has_presence(e));
	CHECK(proto_field_has_optional_keyword(b) && !proto_field_has_optional_keyword(a));
	/* synthetic oneofs */
	CHECK(proto_oneof_is_synthetic(&m->oneofs[1]) && !proto_oneof_is_synthetic(&m->oneofs[0]));
	CHECK(proto_field_real_oneof(b) == NULL && proto_field_real_oneof(f) == &m->oneofs[0]);
	CHECK(proto_oneof_field(&m->oneofs[0], 0) == f);
	/* packed by default in proto3, [packed = false] is EXPANDED; strings are not packable */
	CHECK(proto_field_is_packed(c) && !proto_field_is_packed(g) && proto_field_is_packable(g));
	CHECK(!proto_field_is_packable(d) && !proto_field_is_packed(d) && !proto_type_is_packable(PT_BYTES));
	CHECK(proto_feature(g->merged_features, PROTO_FEATURE_REPEATED_FIELD_ENCODING) == PROTO_REPEATED_EXPANDED);
	/* maps */
	CHECK(proto_field_is_map(e) && !proto_field_is_map(sub) && proto_message_is_map_entry(&m->nested[0]));
	CHECK(proto_field_is_repeated(e) && !proto_field_is_required(a));
	CHECK(proto_field_requires_utf8_validation(d) && !proto_field_requires_utf8_validation(a));
	/* enums are open in proto3 */
	CHECK(!proto_enum_is_closed(&m->enums[0]));
	CHECK(proto_enum_value_by_number(&m->enums[0], 1) == &m->enums[0].values[1]);
	CHECK(proto_enum_value_by_name(&m->enums[0], "E_ZERO") == &m->enums[0].values[0]);
	CHECK(proto_enum_value_by_number(&m->enums[0], 2) == NULL);
	CHECK(proto_message_field_by_number(m, 7) == f && proto_message_field_by_name(m, "sub") == sub);
	CHECK(proto_message_field_by_number(m, 9) == NULL);
	/* parents */
	CHECK(proto_desc_parent((ProtoDesc *)f) == (ProtoDesc *)m && proto_desc_parent((ProtoDesc *)m) == (ProtoDesc *)file);
	CHECK(proto_desc_file((ProtoDesc *)&m->enums[0].values[1]) == file);
	CHECK(proto_field_parent_features(f) == m->oneofs[0].merged_features);
}

/* proto2: required is LEGACY_REQUIRED, groups DELIMITED, [packed = true] PACKED, enums closed,
 * presence explicit; the optional keyword of proto2. */
static void test_field_predicates_proto2(void)
{
	PbMsg *ef = proto_edition_features(cpp_defaults(), PROTO_EDITION_PROTO2, &d_features);
	ProtoFile file = {.kind = PROTO_FILE, .name = "b.proto", .package = "", .full_name = "", .edition = PROTO_EDITION_PROTO2};
	file.merged_features = ef;
	ProtoMessage m = {.kind = PROTO_MESSAGE, .name = "N", .full_name = "N", .file = &file, .merged_features = ef};
	PbMsg *protos[4] = {
		field_proto("r", 1, PT_INT32, LABEL_REQUIRED), field_proto("o", 2, PT_INT32, LABEL_OPTIONAL),
		field_proto("grp", 3, PT_GROUP, LABEL_OPTIONAL), field_proto("p", 4, PT_SINT64, LABEL_REPEATED),
	};
	proto_msg_set_u(proto_msg_mutable(protos[3], FD_OPTIONS), PROTO_FOPT_PACKED, 1);
	ProtoField f[4] = {0};
	for (int i = 0; i < 4; i++) {
		init_field(&f[i], &file, &m, protos[i], i);
		resolve_field(&f[i], ef);
	}
	CHECK(proto_field_is_required(&f[0]) && f[0].label == LABEL_REQUIRED && proto_field_has_presence(&f[0]));
	CHECK(!proto_field_is_required(&f[1]) && proto_field_has_presence(&f[1]));
	CHECK(proto_field_has_optional_keyword(&f[1]) && !proto_field_has_optional_keyword(&f[0]));
	CHECK(proto_feature(f[2].merged_features, PROTO_FEATURE_MESSAGE_ENCODING) == PROTO_MESSAGE_DELIMITED);
	CHECK(proto_feature(f[1].merged_features, PROTO_FEATURE_MESSAGE_ENCODING) == PROTO_MESSAGE_LENGTH_PREFIXED);
	CHECK(proto_field_is_packed(&f[3]));
	CHECK(proto_feature(f[1].merged_features, PROTO_FEATURE_UTF8_VALIDATION) == PROTO_UTF8_NONE);
	ProtoEnum e = {.kind = PROTO_ENUM, .file = &file, .merged_features = ef};
	CHECK(proto_enum_is_closed(&e));
	/* editions ignore the legacy rules: a required label is not LEGACY_REQUIRED in 2023 */
	PbMsg *own = pb_new(&d_features);
	proto_infer_legacy_field_features(own, protos[0], NULL, PROTO_EDITION_2023);
	CHECK(proto_msg_is_empty(own));
}

/* ---- pool ---- */

static void test_pool(void)
{
	ProtoFile *file = proto3_file();
	ProtoMessage *m = &file->messages[0];
	ProtoPool pool = {0};
	file->pool = &pool;
	proto_pool_add_file(&pool, file);
	CHECK(proto_pool_find_file(&pool, "a.proto") == file && proto_pool_find_file(&pool, "b.proto") == NULL);
	CHECK(proto_pool_add_symbol(&pool, proto_symbol_new(PROTO_SYM_PACKAGE, file, file, "pkg")));
	CHECK(proto_pool_add_symbol(&pool, proto_symbol_new(PROTO_MESSAGE, m, file, m->full_name)));
	CHECK(proto_pool_add_symbol(&pool, proto_symbol_new(PROTO_ENUM, &m->enums[0], file, m->enums[0].full_name)));
	CHECK(proto_pool_add_symbol(&pool, proto_symbol_new(PROTO_FIELD, &m->fields[0], file, m->fields[0].full_name)));
	CHECK(!proto_pool_add_symbol(&pool, proto_symbol_new(PROTO_FIELD, &m->fields[1], file, m->full_name)));
	CHECK(proto_pool_find_message(&pool, "pkg.M") == m && proto_pool_find_message(&pool, "pkg.M.E") == NULL);
	CHECK(proto_pool_find_enum(&pool, "pkg.M.E") == &m->enums[0]);
	CHECK(proto_symbol_is_package(proto_pool_find_symbol(&pool, "pkg")));
	CHECK(proto_symbol_is_aggregate(proto_pool_find_symbol(&pool, "pkg.M")));
	CHECK(!proto_symbol_is_aggregate(proto_pool_find_symbol(&pool, "pkg.M.a")) && !proto_symbol_is_type(NULL));
	CHECK(proto_file_in_package(file, "pkg") && !proto_file_in_package(file, "pk"));

	/* extensions by extendee and number, in the order they were added */
	m->id = 7;
	ProtoField x1 = {.kind = PROTO_FIELD, .is_extension = true, .number = 100, .containing_type = m};
	ProtoField x2 = {.kind = PROTO_FIELD, .is_extension = true, .number = 101, .containing_type = m};
	ProtoField x3 = {.kind = PROTO_FIELD, .is_extension = true, .number = 100, .containing_type = m};
	CHECK(proto_pool_add_extension(&pool, &x2) && proto_pool_add_extension(&pool, &x1));
	CHECK(!proto_pool_add_extension(&pool, &x3));
	CHECK(proto_pool_find_extension(&pool, m, 100) == &x1 && proto_pool_find_extension(&pool, m, 102) == NULL);
	const Vec *xs = proto_pool_extensions(&pool, "pkg.M");
	CHECK(xs && xs->n == 2 && xs->v[0] == &x2 && xs->v[1] == &x1);
	CHECK(proto_pool_extensions(&pool, "pkg.Other") == NULL);
}

/* ---- names ---- */

/* descriptor.cc ToJsonName */
static void test_json_name(void)
{
	CHECK_STR(proto_json_name("foo_bar_baz"), "fooBarBaz");
	CHECK_STR(proto_json_name("foo__bar_"), "fooBar");
	CHECK_STR(proto_json_name("_foo"), "Foo");
	CHECK_STR(proto_json_name("foo_1bar"), "foo1bar");
	CHECK_STR(proto_json_name("FooBar"), "FooBar");
	CHECK_STR(proto_json_name("foo_bAr"), "fooBAr");
	CHECK_STR(proto_json_name("fooBar_"), "fooBar");
	CHECK_STR(proto_json_name(""), "");
}

/* ---- source locations ---- */

static PbMsg *add_location(PbMsg *sci, const int32_t *path, int n, const char *leading)
{
	PbMsg *loc = proto_msg_add(sci, SCI_LOCATION);
	for (int i = 0; i < n; i++)
		proto_msg_add_u(loc, LOC_PATH, (uint64_t)(int64_t)path[i]);
	if (leading)
		proto_msg_set_cs(loc, LOC_LEADING, leading);
	return loc;
}

static bool path_is(const ProtoDesc *d, const int32_t *want, int n)
{
	int got_n;
	int32_t *got = proto_desc_source_path(d, &got_n);
	return got_n == n && (n == 0 || memcmp(got, want, sizeof(int32_t) * (size_t)n) == 0);
}

static void test_source_locations(void)
{
	ProtoFile *file = proto3_file();
	ProtoMessage *m = &file->messages[0];
	/* the paths of descriptor.cc GetLocationPath */
	static const int32_t p_msg[] = {4, 0}, p_nested[] = {4, 0, 3, 0}, p_field[] = {4, 0, 2, 6},
			     p_oneof[] = {4, 0, 8, 1}, p_value[] = {4, 0, 4, 0, 2, 1};
	CHECK(path_is((ProtoDesc *)file, NULL, 0));
	CHECK(path_is((ProtoDesc *)m, p_msg, 2));
	CHECK(path_is((ProtoDesc *)&m->nested[0], p_nested, 4));
	CHECK(path_is((ProtoDesc *)&m->fields[6], p_field, 4));
	CHECK(path_is((ProtoDesc *)&m->oneofs[1], p_oneof, 4));
	CHECK(path_is((ProtoDesc *)&m->enums[0].values[1], p_value, 6));
	ProtoField ext_file = {.kind = PROTO_FIELD, .index = 3, .is_extension = true, .file = file};
	ProtoField ext_msg = {.kind = PROTO_FIELD, .index = 1, .is_extension = true, .file = file, .extension_scope = m};
	ProtoExtensionRange range = {.kind = PROTO_EXTENSION_RANGE, .index = 2, .containing_type = m};
	static const int32_t p_ext_file[] = {7, 3}, p_ext_msg[] = {4, 0, 6, 1}, p_range[] = {4, 0, 5, 2};
	CHECK(path_is((ProtoDesc *)&ext_file, p_ext_file, 2));
	CHECK(path_is((ProtoDesc *)&ext_msg, p_ext_msg, 4));
	CHECK(path_is((ProtoDesc *)&range, p_range, 4));
	ProtoService svc = {.kind = PROTO_SERVICE, .index = 1, .file = file};
	ProtoMethod method = {.kind = PROTO_METHOD, .index = 4, .service = &svc};
	static const int32_t p_method[] = {6, 1, 2, 4};
	CHECK(path_is((ProtoDesc *)&method, p_method, 4));

	/* locations by path: the first of duplicates */
	PbMsg *fdp = pb_new(&pbd_FileDescriptorProto);
	PbMsg *sci = proto_msg_mutable(fdp, FDP_SOURCE_CODE_INFO);
	add_location(sci, NULL, 0, NULL);
	PbMsg *first = add_location(sci, p_msg, 2, " First.\n");
	add_location(sci, p_msg, 2, " Second.\n");
	PbMsg *field_loc = add_location(sci, p_field, 4, NULL);
	proto_msg_set_cs(field_loc, LOC_TRAILING, " Trailing.\n");
	file->source_code_info = sci;
	CHECK(proto_desc_location((ProtoDesc *)m) == first);
	CHECK_STR(pb_str(proto_desc_location((ProtoDesc *)m), LOC_LEADING), " First.\n");
	CHECK_STR(pb_str(proto_desc_location((ProtoDesc *)&m->fields[6]), LOC_TRAILING), " Trailing.\n");
	CHECK(proto_desc_location((ProtoDesc *)&m->fields[0]) == NULL);
	CHECK(proto_file_location(file, NULL, 0) != NULL);
	/* a file without source code info */
	ProtoFile *bare = proto3_file();
	CHECK(proto_desc_location((ProtoDesc *)&bare->messages[0]) == NULL);
}

/* ---- well-known types ---- */

static void test_wkt(void)
{
	CHECK(proto_wkt("google.protobuf.Timestamp") == PROTO_WKT_TIMESTAMP);
	CHECK_STR(proto_wkt_file(PROTO_WKT_TIMESTAMP), "google/protobuf/timestamp.proto");
	CHECK(proto_wkt("google.protobuf.Field.Kind") == PROTO_WKT_FIELD_KIND);
	CHECK_STR(proto_wkt_file(PROTO_WKT_FIELD_KIND), "google/protobuf/type.proto");
	CHECK_STR(proto_wkt_full_name(PROTO_WKT_NULL_VALUE), "google.protobuf.NullValue");
	CHECK_STR(proto_wkt_file(PROTO_WKT_NULL_VALUE), "google/protobuf/struct.proto");
	CHECK_STR(proto_wkt_file(PROTO_WKT_MIXIN), "google/protobuf/api.proto");
	CHECK(proto_wkt("google.protobuf.FileDescriptorProto") == PROTO_WKT_NONE);
	CHECK(proto_wkt("google.protobuf") == PROTO_WKT_NONE && proto_wkt("Any") == PROTO_WKT_NONE);
	CHECK(proto_wkt_is_wrapper(PROTO_WKT_INT64_VALUE) && proto_wkt_is_wrapper(PROTO_WKT_BYTES_VALUE));
	CHECK(!proto_wkt_is_wrapper(PROTO_WKT_DURATION) && !proto_wkt_is_wrapper(PROTO_WKT_NONE));
	CHECK(proto_is_wkt_file("google/protobuf/any.proto") && proto_is_wkt_file("google/protobuf/wrappers.proto"));
	CHECK(!proto_is_wkt_file("google/protobuf/descriptor.proto"));
	CHECK(!proto_is_wkt_file("google/protobuf/compiler/plugin.proto"));
	CHECK_STR(proto_wkt_full_name(PROTO_WKT_NONE), "");
}

/* ---- messages ---- */

/* MergeFrom: scalars overwrite, messages merge, repeated fields and unknown fields append. */
static void test_msg_merge(void)
{
	PbMsg *a = pb_new(&pbd_FieldDescriptorProto), *b = pb_new(&pbd_FieldDescriptorProto);
	proto_msg_set_cs(a, FD_NAME, "a");
	proto_msg_set_u(a, FD_NUMBER, 1);
	proto_msg_set_u(proto_msg_mutable(a, FD_OPTIONS), PROTO_FOPT_PACKED, 1);
	proto_msg_set_cs(b, FD_NAME, "b");
	proto_msg_set_u(proto_msg_mutable(b, FD_OPTIONS), FO_DEPRECATED, 1);
	pb_put_tag(&b->unknown, 99, WT_VARINT);
	pb_put_varint(&b->unknown, 5);
	PbMsg *c = proto_msg_clone(a);
	proto_msg_merge(c, b);
	CHECK_STR(pb_str(c, FD_NAME), "b");
	CHECK(pb_int(c, FD_NUMBER, 0) == 1);
	CHECK(pb_int(pb_msg(c, FD_OPTIONS), PROTO_FOPT_PACKED, 0) == 1 && pb_int(pb_msg(c, FD_OPTIONS), FO_DEPRECATED, 0) == 1);
	CHECK(c->unknown.n == 3); /* tag 99 (2 bytes) and 5 */
	/* the clone is deep: a is unchanged */
	CHECK_STR(pb_str(a, FD_NAME), "a");
	CHECK(!pb_has(pb_msg(a, FD_OPTIONS), FO_DEPRECATED) && a->unknown.n == 0);
	/* repeated fields append */
	PbMsg *d = pb_new(&pbd_DescriptorProto), *e = pb_new(&pbd_DescriptorProto);
	proto_msg_add(d, DP_FIELD);
	proto_msg_add(e, DP_FIELD);
	proto_msg_add(e, DP_FIELD);
	proto_msg_merge(d, e);
	CHECK(pb_count(d, DP_FIELD) == 3);
	proto_msg_clear(d, DP_FIELD);
	CHECK(pb_count(d, DP_FIELD) == 0 && proto_msg_is_empty(d) && proto_msg_is_empty(NULL));
	CHECK(proto_msg_clone(NULL) == NULL);
	CHECK(proto_msg_field_index(&pbd_FieldDescriptorProto, FD_PROTO3_OPTIONAL) >= 0);
	CHECK(proto_msg_field_index(&pbd_FieldDescriptorProto, 16) == -1);
}

TESTS(TEST(test_edition_features_protoc), TEST(test_edition_features_go), TEST(test_merge_features),
      TEST(test_field_predicates_proto3), TEST(test_field_predicates_proto2), TEST(test_pool), TEST(test_json_name),
      TEST(test_source_locations), TEST(test_wkt), TEST(test_msg_merge))
