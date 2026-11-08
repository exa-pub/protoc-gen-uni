/* Unit tests of the 35.1 version of libprotoc (libprotoc_35.1.c). Expected values come from
 * protobuf v35.1: cpp_edition_defaults.h (PROTOBUF_INTERNAL_CPP_EDITION_DEFAULTS, the defaults
 * of CompileDefaults with pb.cpp from its descriptor.proto) and descriptor.cc
 * (ValidateMapEntry), compiler/php/names.cc (kReservedNames). */
#include "libprotoc.c"
#include "libprotoc_35.1.c"

#include "test.h"

static void test_builtin_pool_35_1(void)
{
	LpPool *b = lp_builtin_pool(&lp_version_35_1);
	const LpMessage *fs = lp_find_message(b, "google.protobuf.FeatureSet");
	CHECK(fs && proto_message_field_by_name(fs, "default_symbol_visibility") &&
	      !proto_message_field_by_name(fs, "enforce_proto_limits"));
	CHECK(!lp_find_message(b, "google.protobuf.FeatureSet.ProtoLimitsFeature"));
	const LpEnum *edition = proto_pool_find_enum(&b->base, "google.protobuf.Edition");
	CHECK(edition && proto_enum_value_by_name(edition, "EDITION_2026"));
	CHECK(!lp_find_message(b, "pb.CSharpFeatures") && !lp_find_message(b, "pb.enumvalue.JsonEnumValueOptions"));
	CHECK(lp_find_message(b, "pb.CppFeatures") && lp_find_message(b, "pb.JavaFeatures"));
	CHECK(lp_find_message(b, "google.protobuf.compiler.CodeGeneratorRequest") != NULL);
	const char *err = NULL;
	static const char *const cpp[] = {"pb.cpp", NULL};
	PbMsg *d = lp_compile_defaults(b, cpp, LP_EDITION_PROTO2, lp_version_35_1.maximum_known_edition, &err);
	CHECK(d != NULL);
	if (!d)
		return;
	Str s = lp_serialize_str(d);
	/* PROTOBUF_INTERNAL_CPP_EDITION_DEFAULTS of v35.1: STYLE2026 at EDITION_UNSTABLE, no STRICT */
	static const char want[] =
		"\n%\030\204\007\"\003\302>\000*\033\010\001\020\002\030\002 \003(\0010\0028\002@\001\302>\010\010\001\020"
		"\003\030\000 \001\n%\030\347\007\"\003\302>\000*\033\010\002\020\001\030\001 \002(\0010\0018\002@\001\302>"
		"\010\010\000\020\003\030\000 \001\n%\030\350\007\"\023\010\001\020\001\030\001 \002(\0010\001\302>\004\010\000"
		"\020\003*\0138\002@\001\302>\004\030\000 \001\n%\030\351\007\"\031\010\001\020\001\030\001 \002(\0010\0018\001"
		"@\002\302>\006\010\000\020\001\030\001*\005\302>\002 \001\n%\030\217N\"\033\010\001\020\001\030\001 \002(\0010"
		"\0018\003@\002\302>\010\010\000\020\001\030\001 \001*\003\302>\000 \346\007(\351\007";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
}

/* message M { option map_entry: FooEntry nested; repeated FooEntry <field> = 1; } */
static const char *build_map_field(const LpVersion *version, const char *field)
{
	LpPool *builtin = lp_builtin_pool(version);
	const char *err;
	PbMsg *d = lp_compile_defaults(builtin, NULL, LP_EDITION_PROTO2, LP_EDITION_2024, &err);
	LpPool *pool = lp_pool_new(builtin, d, &lp_msgdef_by_name(builtin, "google.protobuf.FeatureSet")->pb);
	PbMsg *f = pb_new(&lp_msgdef_by_name(builtin, "google.protobuf.FileDescriptorProto")->pb);
	proto_msg_set_cs(f, FDP_NAME, "m.proto");
	proto_msg_set_cs(f, FDP_PACKAGE, "p");
	proto_msg_set_cs(f, FDP_SYNTAX, "proto3");
	PbMsg *m = proto_msg_add(f, FDP_MESSAGE_TYPE);
	proto_msg_set_cs(m, DP_NAME, "M");
	PbMsg *entry = proto_msg_add(m, DP_NESTED_TYPE);
	proto_msg_set_cs(entry, DP_NAME, "FooEntry");
	proto_msg_set_u(proto_msg_mutable(entry, DP_OPTIONS), MSGOPT_MAP_ENTRY, 1);
	static const char *const names[] = {"key", "value"};
	for (int i = 0; i < 2; i++) {
		PbMsg *kv = proto_msg_add(entry, DP_FIELD);
		proto_msg_set_cs(kv, FD_NAME, names[i]);
		proto_msg_set_u(kv, FD_NUMBER, (uint64_t)(i + 1));
		proto_msg_set_u(kv, FD_LABEL, LABEL_OPTIONAL);
		proto_msg_set_u(kv, FD_TYPE, PT_STRING);
	}
	PbMsg *fd = proto_msg_add(m, DP_FIELD);
	proto_msg_set_cs(fd, FD_NAME, field);
	proto_msg_set_u(fd, FD_NUMBER, 1);
	proto_msg_set_u(fd, FD_LABEL, LABEL_REPEATED);
	proto_msg_set_u(fd, FD_TYPE, PT_MESSAGE);
	proto_msg_set_cs(fd, FD_TYPE_NAME, ".p.M.FooEntry");
	Buf log = {0};
	return lp_build_file(pool, f, &log) ? NULL : buf_str(&log);
}

static void test_legacy_map_entry(void)
{
	CHECK(build_map_field(&lp_version_35_1, "foo") == NULL);
	CHECK(build_map_field(NULL, "foo") == NULL);
	/* the entry is not named after the field */
	const char *log = build_map_field(&lp_version_35_1, "bar");
	CHECK_STR(log, "Invalid proto descriptor for file \"m.proto\":\n"
		       "  p.M.bar: map_entry should not be set explicitly. Use map<KeyType, ValueType> instead.\n");
	log = build_map_field(NULL, "bar");
	CHECK(log && strstr(log, "is a synthetic MapEntry message type") && !strstr(log, "map_entry should not"));
}

/* compiler/php/names.cc: 35.1 does not reserve mixed, never and object (36.2 does). */
static const LpFile *build_php_names(const LpVersion *version)
{
	LpPool *builtin = lp_builtin_pool(version);
	const char *err;
	PbMsg *d = lp_compile_defaults(builtin, NULL, LP_EDITION_PROTO2, LP_EDITION_2024, &err);
	LpPool *pool = lp_pool_new(builtin, d, &lp_msgdef_by_name(builtin, "google.protobuf.FeatureSet")->pb);
	PbMsg *f = pb_new(&lp_msgdef_by_name(builtin, "google.protobuf.FileDescriptorProto")->pb);
	proto_msg_set_cs(f, FDP_NAME, "n.proto");
	proto_msg_set_cs(f, FDP_PACKAGE, "p");
	proto_msg_set_cs(f, FDP_SYNTAX, "proto3");
	static const char *const names[] = {"Mixed", "NEVER", "object", "Class"};
	for (int i = 0; i < 4; i++)
		proto_msg_set_cs(proto_msg_add(f, FDP_MESSAGE_TYPE), DP_NAME, names[i]);
	Buf log = {0};
	return lp_build_file(pool, f, &log);
}

static void test_php_reserved_names(void)
{
	const LpFile *v35 = build_php_names(&lp_version_35_1), *v36 = build_php_names(NULL);
	CHECK(v35 && v36 && v35->n_messages == 4 && v36->n_messages == 4);
	if (!v35 || !v36)
		return;
	static const char *const want35[] = {"Mixed", "NEVER", "object", "PBClass"};
	static const char *const want36[] = {"PBMixed", "PBNEVER", "PBobject", "PBClass"};
	for (int i = 0; i < 4; i++) {
		CHECK_STR(lp_php_generated_class_name_message(&v35->messages[i]), want35[i]);
		CHECK_STR(lp_php_generated_class_name_message(&v36->messages[i]), want36[i]);
	}
	CHECK(lp_php_is_reserved_name("Mixed")); /* the 36.2 words */
}

TESTS(TEST(test_builtin_pool_35_1), TEST(test_legacy_map_entry), TEST(test_php_reserved_names))
