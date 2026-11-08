/* apple/swift v1.39.0 helpers: BytecodeWriter (expected values from swift-protobuf 1.39.0
 * Tests/protoc-gen-swiftTests/Test_Bytecode.swift), default value literals and splitPath (from
 * upstream output), range merging. Includes the unity build to reach its static functions. */
#include "../generator.c"
#include "test.h"

static void test_bytecode_writer(void)
{
	NameWriter w;
	name_writer_init(&w);
	CHECK(!w.has_data);
	bc_write_uint64(&w, 1);
	bc_write_uint64(&w, 2);
	CHECK(w.has_data);
	CHECK_STR(name_writer_literal(&w), "\"\\0\\u{1}\\u{2}\"");

	name_writer_init(&w);
	bc_write_uint64(&w, 0);
	bc_write_uint64(&w, 63);
	bc_write_uint64(&w, 64);
	bc_write_uint64(&w, 123456789);
	bc_write_int32(&w, 0);
	bc_write_int32(&w, -1);
	CHECK_STR(name_writer_literal(&w), "\"\\0\\0?@\\u{1}Ut|V\\u{7}\\0\\u{7f}\\u{7f}\\u{7f}\\u{7f}\\u{7f}\\u{3}\"");

	name_writer_init(&w);
	bc_write_string(&w, "hello");
	bc_write_string(&w, "\xc3\xbctf-\xc3\xa8\xc3\xadght");
	CHECK_STR(name_writer_literal(&w), "\"\\0hello\\0\xc3\xbctf-\xc3\xa8\xc3\xadght\\0\"");

	name_writer_init(&w);
	bc_write_string(&w, "a\"b\\c\x01\x7f");
	CHECK_STR(name_writer_literal(&w), "\"\\0a\\\"b\\\\c\\u{1}\\u{7f}\\0\"");
}

static void test_name_writer(void)
{
	/* test.proto2.Sparse of testdata/proto/proto2 */
	NameWriter w;
	name_writer_init(&w);
	nw_write_same(&w, -1, "SPARSE_NEG");
	nw_write_same(&w, 10, "SPARSE_TEN");
	nw_write_same(&w, 2147483647, "SPARSE_MAX");
	CHECK_STR(name_writer_literal(&w),
		  "\"\\0\\u{2}\\u{7f}\\u{7f}\\u{7f}\\u{7f}\\u{7f}\\u{3}SPARSE_NEG\\0\\u{2}\\u{b}SPARSE_TEN\\0\\u{2}u\\u{7f}\\u{7f}\\u{7f}\\u{7f}\\u{1}SPARSE_MAX\\0\"");
	name_writer_init(&w);
	nw_write_reserved_numbers(&w, (SwRange){5, 10});
	nw_write_reserved_name(&w, "x");
	CHECK_STR(name_writer_literal(&w), "\"\\0\\u{c}\\u{5}\\u{5}\\u{b}x\\0\"");
}

static void test_default_literals(void)
{
	/* test.proto2.Defaults of testdata/proto/proto2 (protoc's C-escaped defaults) */
	CHECK_STR(escaped_to_data_literal(STR_LIT("\\000\\001\\177\\377\\n\\r\\t\\\"\\'\\\\abc?")),
		  "Data([0, 1, 127, 255, 10, 13, 9, 34, 39, 92, 97, 98, 99, 63])");
	CHECK_STR(escaped_to_data_literal(STR_LIT("")), "Data()");
	CHECK_STR(string_to_escaped_string_literal(STR_LIT("hello \"world\"\n\t\\ caf\xc3\xa9 \x01")),
		  "\"hello \\\"world\\\"\\u{0a}\\u{09}\\\\ caf\xc3\xa9 \\u{01}\"");
	CHECK_STR(string_to_escaped_string_literal((Str){"a\0b", 3}), "\"a\\0b\"");
	CHECK_STR(string_to_escaped_string_literal(STR_LIT("")), "String()");
}

static void test_split_path(void)
{
	PathParts p = split_path("a/b.c/d.e.proto");
	CHECK_STR(p.dir, "a/b.c/");
	CHECK_STR(p.base, "d.e");
	CHECK_STR(p.suffix, ".proto");
	p = split_path("noext");
	CHECK_STR(p.dir, "");
	CHECK_STR(p.base, "noext");
	CHECK_STR(p.suffix, "");
	p = split_path("dir/.hidden");
	CHECK_STR(p.base, "");
	CHECK_STR(p.suffix, ".hidden");
}

static void test_ranges(void)
{
	SwRange r[] = {{100, 200}, {6, 8}, {2, 5}, {8, 9}, {5, 6}};
	int n = sort_and_merge_continuous(r, 5);
	CHECK(n == 2);
	CHECK(r[0].start == 2 && r[0].end == 9 && r[1].start == 100 && r[1].end == 200);
	CHECK_STR(ranges_case_expression(r, n), "2..<9, 100..<200");
	SwRange one[] = {{7, 8}};
	CHECK_STR(ranges_boolean_expression(one, 1, "n"), "(7 == n)");
	CHECK_STR(ranges_boolean_expression(r, n, "n"), "(2 <= n && n < 9) || (100 <= n && n < 200)");
}

static void test_option_flags(void)
{
	/* String.lowercased(): U+212A KELVIN SIGN lowercases to "k". */
	CHECK(parse_options(STR_LIT("Visibility=PAC\xe2\x84\xaa" "AGE"))->visibility == VIS_PACKAGE);
	CHECK(parse_options(STR_LIT("FileNaming=Drop_Path"))->output_naming == NAMING_DROP_PATH);
}

static Str swift_prefix; /* option swift_prefix of the file of run_request if .p is set */

/* A request with one proto3 file "a.proto" (package pkg) holding message msg_name with one string
 * field field_name, json_name json, generated with parameter param; returns the response error
 * ("" if none). */
static const char *run_request(Str msg_name, Str field_name, Str json, Str param)
{
	Buf field = {0}, msg = {0}, file = {0}, req = {0};
	pb_put_bytes(&field, FD_NAME, field_name);
	pb_put_tag(&field, FD_NUMBER, WT_VARINT);
	pb_put_varint(&field, 1);
	pb_put_tag(&field, FD_LABEL, WT_VARINT);
	pb_put_varint(&field, LABEL_OPTIONAL);
	pb_put_tag(&field, FD_TYPE, WT_VARINT);
	pb_put_varint(&field, PT_STRING);
	pb_put_bytes(&field, FD_JSON_NAME, json);
	pb_put_bytes(&msg, DP_NAME, msg_name);
	pb_put_bytes(&msg, DP_FIELD, buf_view(&field));
	pb_put_bytes(&file, FDP_NAME, STR_LIT("a.proto"));
	pb_put_bytes(&file, FDP_PACKAGE, STR_LIT("pkg"));
	pb_put_bytes(&file, FDP_MESSAGE_TYPE, buf_view(&msg));
	pb_put_bytes(&file, FDP_SYNTAX, STR_LIT("proto3"));
	if (swift_prefix.p) {
		Buf options = {0};
		pb_put_bytes(&options, 39, swift_prefix);
		pb_put_bytes(&file, FDP_OPTIONS, buf_view(&options));
	}
	pb_put_bytes(&req, CGR_FILE_TO_GENERATE, STR_LIT("a.proto"));
	if (param.n)
		pb_put_bytes(&req, CGR_PARAMETER, param);
	pb_put_bytes(&req, CGR_PROTO_FILE, buf_view(&file));
	PbMsg *m = pb_new(&pbd_CodeGeneratorRequest);
	Str raw = buf_view(&req);
	CHECK(pb_decode(m, (const uint8_t *)raw.p, raw.n));
	Buf out = {0};
	CHECK(gen_apple_swift_v1_39_0.generate(m, raw, &out) == NULL);
	Str o = buf_view(&out);
	PbReader r = {(const uint8_t *)o.p, (const uint8_t *)o.p + o.n};
	PbRawField f;
	while (pb_next_field(&r, &f))
		if (f.num == CGRESP_ERROR)
			return xstrndup((const char *)f.val, f.vlen);
	return "";
}

static void test_unsupported(void)
{
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("my_field"), STR_LIT("myField"), STR_LIT("")), "");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("my_field"), STR_LIT("caf\xc3\xa9"), STR_LIT("")), "");
	CHECK_STR(run_request(STR_LIT("M\xc3\xa9g"), STR_LIT("f"), STR_LIT("f"), STR_LIT("")),
		  "unsupported by protoc-gen-uni: non-ASCII DescriptorProto.name \"M\xc3\xa9g\"");
	CHECK_STR(run_request(STR_LIT("Msg"), (Str){"a\0b", 3}, STR_LIT("f"), STR_LIT("")),
		  "unsupported by protoc-gen-uni: NUL byte in FieldDescriptorProto.name \"a\\0b\"");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("a_k"), STR_LIT("a\xe2\x84\xaa"), STR_LIT("")),
		  "unsupported by protoc-gen-uni: FieldDescriptorProto.json_name \"a\xe2\x84\xaa\" has a character "
		  "that is canonically equivalent to ASCII");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("f"), STR_LIT("f"), (Str){"ExtraModuleImports=A\0B", 22}),
		  "unsupported by protoc-gen-uni: NUL byte in parameter ExtraModuleImports \"A\\0B\"");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("f"), STR_LIT("f"), STR_LIT("ProtoPathModuleMappings=m\xc3\xa9.asciipb")),
		  "unsupported by protoc-gen-uni: non-ASCII parameter ProtoPathModuleMappings \"m\xc3\xa9.asciipb\"");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("f"), STR_LIT("f"), STR_LIT("ProtoPathModuleMappings=m.asciipb")),
		  "Parameter 'ProtoPathModuleMappings=m.asciipb': failToOpen(path: \"m.asciipb\")");
	/* swift_prefix: upstream's check of the identifier comes first. */
	swift_prefix = STR_LIT("P\xc3\xa9-");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("f"), STR_LIT("f"), STR_LIT("")),
		  "a.proto has an 'swift_prefix' that isn't a valid Swift identifier (P\xc3\xa9-).");
	swift_prefix = STR_LIT("P\xc3\xa9");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("f"), STR_LIT("f"), STR_LIT("")),
		  "unsupported by protoc-gen-uni: non-ASCII option swift_prefix \"P\xc3\xa9\"");
	swift_prefix = STR_LIT("Pfx");
	CHECK_STR(run_request(STR_LIT("Msg"), STR_LIT("f"), STR_LIT("f"), STR_LIT("")), "");
	swift_prefix = (Str){0};
}

TESTS(TEST(test_bytecode_writer), TEST(test_name_writer), TEST(test_default_literals), TEST(test_split_path),
      TEST(test_ranges), TEST(test_option_flags), TEST(test_unsupported))
