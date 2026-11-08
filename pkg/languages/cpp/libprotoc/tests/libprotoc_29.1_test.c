/* Unit tests of the 29.1 version of libprotoc (libprotoc_29.1.c on top of libprotoc_33.4.c).
 * Expected values come from protobuf v29.1: cpp_edition_defaults.h
 * (PROTOBUF_INTERNAL_CPP_EDITION_DEFAULTS, CompileDefaults with pb.cpp for PROTO2..2024 from its
 * descriptor.proto) and io/printer.cc (the debug check of an undefined variable). */
#include "libprotoc.c"
#include "libprotoc_33.4.c"
#include "libprotoc_29.1.c"

#include "test.h"

static void test_builtin_pool_29_1(void)
{
	LpPool *b = lp_builtin_pool(&lp_version_29_1);
	const LpMessage *fs = lp_find_message(b, "google.protobuf.FeatureSet");
	CHECK(fs && proto_message_field_by_name(fs, "json_format") &&
	      !proto_message_field_by_name(fs, "enforce_naming_style") &&
	      !proto_message_field_by_name(fs, "default_symbol_visibility") &&
	      !proto_message_field_by_name(fs, "enforce_proto_limits"));
	const LpMessage *fdp = lp_find_message(b, "google.protobuf.FileDescriptorProto");
	CHECK(fdp && proto_message_field_by_name(fdp, "edition") && !proto_message_field_by_name(fdp, "option_dependency"));
	const LpMessage *dp = lp_find_message(b, "google.protobuf.DescriptorProto");
	CHECK(dp && !proto_message_field_by_name(dp, "visibility"));
	const LpEnum *edition = proto_pool_find_enum(&b->base, "google.protobuf.Edition");
	CHECK(edition && proto_enum_value_by_name(edition, "EDITION_2024") &&
	      !proto_enum_value_by_name(edition, "EDITION_2026") && !proto_enum_value_by_name(edition, "EDITION_UNSTABLE"));
	CHECK(!lp_find_message(b, "pb.CSharpFeatures") && lp_find_message(b, "pb.CppFeatures"));

	const char *err = NULL;
	static const char *const cpp[] = {"pb.cpp", NULL};
	PbMsg *d = lp_compile_defaults(b, cpp, LP_EDITION_PROTO2, LP_EDITION_2024, &err);
	CHECK(d != NULL);
	if (!d)
		return;
	Str s = lp_serialize_str(d);
	static const char want[] =
		"\n\037\030\204\007\"\003\302>\000*\025\010\001\020\002\030\002 \003(\0010\002\302>\006\010\001\020\003\030"
		"\000\n\037\030\347\007\"\003\302>\000*\025\010\002\020\001\030\001 \002(\0010\001\302>\006\010\000\020\003"
		"\030\000\n\037\030\350\007\"\023\010\001\020\001\030\001 \002(\0010\001\302>\004\010\000\020\003*\005\302>"
		"\002\030\000\n\037\030\351\007\"\025\010\001\020\001\030\001 \002(\0010\001\302>\006\010\000\020\001\030\001*"
		"\003\302>\000 \346\007(\351\007";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
}

static void print_undefined(void *arg)
{
	LpPrinter *p = arg;
	lp_print(p, "v$version$\n", "file", "a.proto", NULL);
}

/* Printer::Print in a build without NDEBUG: an undefined variable is a failed check. */
static void test_printer_debug_checks(void)
{
	Buf out = {0};
	LpPrinter p;
	lp_printer_init(&p, &out, '$');
	print_undefined(&p);
	CHECK_STR(buf_str(&out), "v\n");
	p.debug_checks = true;
	CHECK(try_run(print_undefined, &p) != NULL);
	Str msg = fail_message();
	CHECK_MEM(msg.p, msg.n, "undefined variable: \"version\"", 29);
}

/* A generator that writes the compiler version and the edition of its file. */
static bool version_generate(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx, Buf *error)
{
	(void)g, (void)parameter, (void)error;
	LpCompilerVersion v = lp_context_compiler_version(ctx);
	Buf *out = lp_context_open(ctx, "out");
	buf_fmt(out, "%d.%d.%d", (int)v.major, (int)v.minor, (int)v.patch);
	buf_add_str(out, v.suffix);
	buf_fmt(out, " %s", lp_edition_name(file->edition));
	return true;
}

/* GenerateCode with CodeGeneratorRequest.compiler_version, for a file of edition 2024: 29.1
 * compiles the defaults for the generator's editions (PROTO2..2024). */
static const char *run_version(bool with_version, Str *content)
{
	static const LpGenerator gen = {
		.plugin_name = "p", .generate = version_generate, .supported_features = 3,
		.minimum_edition = LP_EDITION_PROTO2, .maximum_edition = LP_EDITION_2024, .version = &lp_version_29_1,
	};
	LpPool *b = lp_builtin_pool(&lp_version_29_1);
	PbMsg *req = pb_new(&lp_msgdef_by_name(b, "google.protobuf.compiler.CodeGeneratorRequest")->pb);
	proto_msg_add_s(req, CGR_FILE_TO_GENERATE, str_c("a.proto"));
	PbMsg *f = proto_msg_add(req, CGR_PROTO_FILE);
	proto_msg_set_cs(f, FDP_NAME, "a.proto");
	proto_msg_set_cs(f, FDP_SYNTAX, "editions");
	proto_msg_set_u(f, FDP_EDITION, LP_EDITION_2024);
	if (with_version) {
		PbMsg *v = proto_msg_mutable(req, 3);
		proto_msg_set_u(v, 1, 29);
		proto_msg_set_u(v, 2, 1);
		proto_msg_set_cs(v, 4, "-rc1");
	}
	Buf raw = {0}, out = {0};
	lp_msg_serialize(req, &raw);
	const char *err = lp_plugin_main(buf_view(&raw), (Str){0}, &gen, &out);
	if (err)
		return err;
	PbMsg *resp = pb_new(&lp_msgdef_by_name(b, "google.protobuf.compiler.CodeGeneratorResponse")->pb);
	if (!lp_decode(resp, (const uint8_t *)out.p, out.n))
		return "bad response";
	if (pb_has(resp, CGRESP_ERROR))
		return pb_str(resp, CGRESP_ERROR);
	*content = pb_bytes(pb_msg(resp, CGRESP_FILE), CGRESP_FILE_CONTENT);
	return NULL;
}

static void test_compiler_version(void)
{
	Str c = {0};
	const char *err = run_version(true, &c);
	CHECK(err == NULL);
	CHECK_MEM(c.p, c.n, "29.1.0-rc1 2024", 15);
	err = run_version(false, &c);
	CHECK(err == NULL);
	CHECK_MEM(c.p, c.n, "0.0.0 2024", 10);
}

TESTS(TEST(test_builtin_pool_29_1), TEST(test_printer_debug_checks), TEST(test_compiler_version))
