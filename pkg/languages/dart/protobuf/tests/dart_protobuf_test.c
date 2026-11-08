/* pkg/languages/dart/protobuf mergeFromBuffer and ==: expected texts from package:protobuf 6.1.0
 * on Dart 3.13 (CodeGeneratorRequest.fromBuffer(...).toString() of the exception). */
#include "dart_protobuf.h"
#include "dart_std.h"
#include "test.h"

static const char *merge(const char *p, size_t n)
{
	PbMsg *m = pb_new(&pbd_CodeGeneratorRequest);
	const char *err = dpb_merge_from_buffer(m, (Str){p, n});
	return err ? err : "ok";
}
#define MERGE(lit) merge(lit, sizeof(lit) - 1)

static const char truncated[] = "InvalidProtocolBufferException: While parsing a protocol message, the input ended "
				"unexpectedly\nin the middle of a field. This could either mean that the input\nhas been "
				"truncated or that an embedded message misreported its\nown length.\n";

static void test_errors(void)
{
	CHECK_STR(MERGE(""), "ok");
	CHECK_STR(MERGE("\x00"), "InvalidProtocolBufferException: Protocol message contained an invalid tag (zero).");
	CHECK_STR(MERGE("\x0f"), "InvalidProtocolBufferException: Protocol message tag had invalid wire type.");
	CHECK_STR(MERGE("\x9c\x06"), "InvalidProtocolBufferException: Protocol message end-group tag did not match expected tag.");
	CHECK_STR(MERGE("\x9b\x06\x08\x05\x94\x06"), "InvalidProtocolBufferException: Protocol message end-group tag did not match expected tag.");
	CHECK_STR(MERGE("\x98\x06\x80\x80"), truncated);
	CHECK_STR(MERGE("\x12\x80\x80"), "InvalidProtocolBufferException: CodedBufferReader encountered a malformed varint.");
	CHECK_STR(MERGE("\x12\x64" "abc"), truncated);
	CHECK_STR(MERGE("\x12\xfd\xff\xff\xff\xff\xff\xff\xff\xff\x01" "abc"), "RangeError (end): Invalid value: Not in inclusive range 11..14: 8");
	CHECK_STR(MERGE("\x9a\x06\xfd\xff\xff\xff\xff\xff\xff\xff\xff\x01" "abc"), "RangeError: Value not in range: -3");
	CHECK_STR(MERGE("\x7a\xfd\xff\xff\xff\xff\xff\xff\xff\xff\x01" "abc"),
		  "Invalid argument(s): CodedBufferReader encountered an embedded string or message which claimed to have "
		  "negative size.");
}

/* file.message_type with n nested messages below the request (100 levels are allowed). */
static const char *deep(int n)
{
	Buf b = {0};
	for (int i = 0; i < n; i++) {
		Buf o = {0};
		pb_put_bytes(&o, i == n - 1 ? 15 : i == n - 2 ? 4 : 3, buf_view(&b));
		b = o;
	}
	PbMsg *m = pb_new(&pbd_CodeGeneratorRequest);
	const char *err = dpb_merge_from_buffer(m, buf_view(&b));
	return err ? err : "ok";
}

static void test_recursion(void)
{
	CHECK_STR(deep(100), "ok");
	CHECK_STR(deep(101), "InvalidProtocolBufferException: Protocol message had too many levels of nesting. May be "
			     "malicious.\nUse a CodedBufferReader with a defined recursion depth limit if you need to\nparse "
			     "deeply nested messages.\n");
}

static void test_values(void)
{
	/* the parameter "a\xff" decodes with U+FFFD */
	PbMsg *m = pb_new(&pbd_CodeGeneratorRequest);
	static const char req[] = "\x12\x02" "a\xff";
	CHECK(!dpb_merge_from_buffer(m, (Str){req, sizeof req - 1}));
	CHECK_STR(pb_str(m, 2), "a\xef\xbf\xbd");
	/* a varint field with a length-delimited tag reads its value at the length: compiler_version
	 * {major: (length 0 -> 0), then field 2 = 7} */
	PbMsg *v = pb_new(&pbd_CodeGeneratorRequest);
	static const char ver[] = "\x1a\x04\x0a\x00\x10\x07";
	CHECK(!dpb_merge_from_buffer(v, (Str){ver, sizeof ver - 1}));
	CHECK(pb_int(pb_msg(v, 3), 1, -1) == 0 && pb_int(pb_msg(v, 3), 2, -1) == 7);
}

static void test_equals(void)
{
	/* unknown fields are compared per field number, varints by value */
	static const char a[] = "\x0a\x01x\xc0\x3e\x05\xca\x3e\x01x";
	static const char b[] = "\x0a\x01x\xca\x3e\x01x\xc0\x3e\x85\x00";
	static const char c[] = "\x0a\x01x\xca\x3e\x01x\xc0\x3e\x06";
	PbMsg *x = pb_new(&pbd_FileDescriptorProto), *y = pb_new(&pbd_FileDescriptorProto), *z = pb_new(&pbd_FileDescriptorProto);
	CHECK(!dpb_merge_from_buffer(x, (Str){a, sizeof a - 1}));
	CHECK(!dpb_merge_from_buffer(y, (Str){b, sizeof b - 1}));
	CHECK(!dpb_merge_from_buffer(z, (Str){c, sizeof c - 1}));
	bool self_only = false;
	CHECK(dpb_equals(x, y));
	CHECK(dpb_hash(x, &self_only) == dpb_hash(y, &self_only));
	CHECK(!dpb_equals(x, z));
	CHECK(!self_only);
	/* a set field differs from an unset one, even with the default value */
	PbMsg *e = pb_new(&pbd_FileDescriptorProto), *f = pb_new(&pbd_FileDescriptorProto);
	static const char d[] = "\x0a\x00";
	CHECK(!dpb_merge_from_buffer(f, (Str){d, sizeof d - 1}));
	CHECK(!dpb_equals(e, f));
}

static void test_utf8(void)
{
	static const struct {
		const char *in, *want;
	} cases[] = {
		{"abc", "abc"},
		{"\xc3\xa4", "\xc3\xa4"},
		{"\xff", "\xef\xbf\xbd"},
		{"\xe0\x80", "\xef\xbf\xbd\xef\xbf\xbd"},
		{"\xe1\x80", "\xef\xbf\xbd"},
		{"\xf0\x90\x80" "A", "\xef\xbf\xbd" "A"},
		{"\xed\xa0\x80", "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
		{"\xf4\x90", "\xef\xbf\xbd\xef\xbf\xbd"},
		{"\xef\xbb\xbf" "A\xef\xbb\xbf", "A\xef\xbb\xbf"}, /* a leading BOM is dropped */
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		Str got = dart_utf8_decode_allow_malformed(str_c(cases[i].in));
		CHECK_MEM(got.p, got.n, cases[i].want, strlen(cases[i].want));
	}
}

/* dart_std toLowerCase of one code unit, values from Dart 3.13 */
static void test_lower(void)
{
	CHECK(dart_to_lower_code_unit('A') == 'a' && dart_to_lower_code_unit('_') == '_');
	CHECK(dart_to_lower_code_unit(0xC4) == 0xE4);   /* Ä */
	CHECK(dart_to_lower_code_unit(0xD7) == 0xD7);   /* × */
	CHECK(dart_to_lower_code_unit(0x130) == 'i');   /* İ */
	CHECK(dart_to_lower_code_unit(0x3A3) == 0x3C3); /* Σ */
	CHECK(dart_to_lower_code_unit(0x101) == 0x101);
	CHECK(dart_to_lower_code_unit(0x212A) == 'k');  /* Kelvin sign */
	CHECK(dart_to_lower_code_unit(0xA77D) == 0x1D79);
	CHECK(dart_to_lower_code_unit(0xFF21) == 0xFF41);
}

TESTS(TEST(test_errors), TEST(test_recursion), TEST(test_values), TEST(test_equals), TEST(test_utf8), TEST(test_lower))
