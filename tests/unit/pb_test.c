/* pkg/pb: wire format primitives and the generic message (decode, encode, accessors). */
#include "test.h"
#include "pb.h"

#define B(s) (const uint8_t *)(s), sizeof(s) - 1

static bool read_varint(const char *s, size_t n, uint64_t *v)
{
	PbReader r = {(const uint8_t *)s, (const uint8_t *)s + n};
	return pb_read_varint(&r, v) && r.p == r.end;
}

static void test_varint(void)
{
	uint64_t v;
	CHECK(read_varint("\x00", 1, &v) && v == 0);
	CHECK(read_varint("\x7f", 1, &v) && v == 127);
	CHECK(read_varint("\xac\x02", 2, &v) && v == 300);
	CHECK(read_varint("\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01", 10, &v) && v == UINT64_MAX);
	CHECK(!read_varint("\xff\xff\xff\xff\xff\xff\xff\xff\xff\x02", 10, &v)); /* overflow */
	CHECK(!read_varint("\xff\xff\xff\xff\xff\xff\xff\xff\xff\x81\x00", 11, &v)); /* too long */
	CHECK(!read_varint("\x80", 1, &v));                                           /* truncated */
	CHECK(!read_varint("", 0, &v));
	Buf b = {0};
	pb_put_varint(&b, UINT64_MAX);
	pb_put_varint(&b, 300);
	CHECK_MEM(b.p, b.n, "\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01\xac\x02", 12);
}

static bool decodes(const uint8_t *p, size_t n)
{
	return pb_decode(pb_new(&pbd_FileDescriptorProto), p, n);
}

static void test_tags(void)
{
	CHECK(decodes(B("")));
	CHECK(!decodes(B("\x00\x00")));             /* field number 0 */
	CHECK(!decodes(B("\x80\x80\x80\x80\x10"))); /* field number 1 << 29 */
	CHECK(decodes(B("\xf8\xff\xff\xff\x0f\x00"))); /* (1 << 29) - 1 */
	CHECK(!decodes(B("\x0e")));                 /* wire type 6 */
	CHECK(!decodes(B("\x0f")));                 /* wire type 7 */
	CHECK(!decodes(B("\x0c")));                 /* end group without start */
	CHECK(!decodes(B("\x0a\x05xy")));           /* length beyond the end */
	CHECK(!decodes(B("\x91\x03\x01\x02")));     /* fixed64 truncated */
	/* unknown groups are skipped, with matching end tags */
	CHECK(decodes(B("\x93\x03\x08\x01\x9b\x03\x9c\x03\x94\x03")));
	CHECK(!decodes(B("\x93\x03\x08\x01\x9c\x03")));
}

static Buf encode(const PbMsg *m)
{
	Buf b = {0};
	pb_encode(m, &b, NULL, NULL);
	return b;
}

static void test_packed(void)
{
	/* FileDescriptorProto.public_dependency (repeated int32): both encodings accepted,
	 * written unpacked */
	PbMsg *f = pb_new(&pbd_FileDescriptorProto);
	CHECK(pb_decode(f, B("\x50\x01\x52\x02\x02\x03\x50\x04")));
	CHECK(pb_count(f, FDP_PUBLIC_DEPENDENCY) == 4 && pb_int_at(f, FDP_PUBLIC_DEPENDENCY, 3) == 4);
	Buf b = encode(f);
	CHECK_MEM(b.p, b.n, "\x50\x01\x50\x02\x50\x03\x50\x04", 8);
	/* SourceCodeInfo.Location.path (packed): written packed */
	f = pb_new(&pbd_FileDescriptorProto);
	CHECK(pb_decode(f, B("\x4a\x06\x0a\x04\x08\x04\x08\x00")));
	b = encode(f);
	CHECK_MEM(b.p, b.n, "\x4a\x06\x0a\x04\x0a\x02\x04\x00", 8);
	/* a truncated packed varint */
	CHECK(!decodes(B("\x52\x01\x80")));
}

static void test_merge(void)
{
	/* repeated singular messages are merged, the last scalar wins */
	PbMsg *f = pb_new(&pbd_FileDescriptorProto);
	CHECK(pb_decode(f, B("\x0a\x01" "a" "\x42\x03\x0a\x01" "x" "\x0a\x01" "b" "\x42\x02\x50\x01")));
	CHECK_STR(pb_str(f, FDP_NAME), "b");
	CHECK_STR(pb_str(pb_msg(f, FDP_OPTIONS), 1), "x");
	CHECK(pb_int(pb_msg(f, FDP_OPTIONS), 10, 0) == 1);
	Buf b = encode(f);
	CHECK_MEM(b.p, b.n, "\x0a\x01" "b" "\x42\x05\x0a\x01" "x" "\x50\x01", 10);
	/* merging also works across separate decodes */
	CHECK(pb_decode(f, B("\x12\x01p")));
	CHECK_STR(pb_str(f, FDP_PACKAGE), "p");
}

static void test_closed_enum(void)
{
	/* FieldDescriptorProto.label = 5 is not a Label: kept as an unknown field, after the known
	 * fields */
	PbMsg *fd = pb_new(&pbd_FieldDescriptorProto);
	CHECK(pb_decode(fd, B("\x20\x05\x0a\x01x\x28\x09")));
	CHECK(!pb_has(fd, FD_LABEL) && pb_int(fd, FD_TYPE, 0) == PT_STRING);
	Buf b = encode(fd);
	CHECK_MEM(b.p, b.n, "\x0a\x01x\x28\x09\x20\x05", 7);
	/* negative values are sign-extended int32 (like protobuf-es varint32write) */
	fd = pb_new(&pbd_FieldDescriptorProto);
	CHECK(pb_decode(fd, B("\x20\xff\xff\xff\xff\x0f")));
	b = encode(fd);
	CHECK_MEM(b.p, b.n, "\x20\xff\xff\xff\xff\xff\xff\xff\xff\xff\x01", 11);
	/* enum values are int32: 1 + 2^32 is LABEL_OPTIONAL */
	fd = pb_new(&pbd_FieldDescriptorProto);
	CHECK(pb_decode(fd, B("\x20\x81\x80\x80\x80\x10")));
	CHECK(pb_int(fd, FD_LABEL, 0) == LABEL_OPTIONAL);
}

static void test_wire_type_mismatch(void)
{
	/* name (string) sent as a varint: kept as unknown, not as the field */
	PbMsg *f = pb_new(&pbd_FileDescriptorProto);
	CHECK(pb_decode(f, B("\x08\x05\x12\x01p")));
	CHECK(!pb_has(f, FDP_NAME));
	Buf b = encode(f);
	CHECK_MEM(b.p, b.n, "\x12\x01p\x08\x05", 5);
}

static void test_bytes_and_doubles(void)
{
	PbMsg *f = pb_new(&pbd_FileDescriptorProto);
	/* name with NUL; options.uninterpreted_option.double_value = 1.5 (little endian) */
	CHECK(pb_decode(f, B("\x0a\x03" "a\0b" "\x42\x0c\xba\x3e\x09\x31\x00\x00\x00\x00\x00\x00\xf8\x3f")));
	Str name = pb_bytes(f, FDP_NAME);
	CHECK_MEM(name.p, name.n, "a\0b", 3);
	CHECK_STR(pb_str(f, FDP_NAME), "a");
	Buf b = encode(f);
	CHECK_MEM(b.p, b.n, "\x0a\x03" "a\0b" "\x42\x0c\xba\x3e\x09\x31\x00\x00\x00\x00\x00\x00\xf8\x3f", 19);
	pb_set_bytes(f, FDP_NAME, STR_LIT("x\0"));
	CHECK(pb_bytes(f, FDP_NAME).n == 2);
	pb_set_str(f, FDP_NAME, NULL);
	CHECK(!pb_has(f, FDP_NAME) && pb_str(f, FDP_NAME)[0] == 0);
	/* accessors of absent messages */
	CHECK(pb_msg(NULL, 1) == NULL && pb_int(NULL, 1, 42) == 42 && pb_count(NULL, 1) == 0);
}

/* A FileDescriptorProto whose message_type nests `levels` messages. */
static Buf nested(int levels)
{
	Buf b = {0};
	for (int i = 0; i < levels; i++) {
		Buf w = {0};
		pb_put_bytes(&w, i == levels - 1 ? FDP_MESSAGE_TYPE : DP_NESTED_TYPE, (Str){b.p, b.n});
		b = w;
	}
	return b;
}

static void test_depth(void)
{
	/* at most 100 messages deep, counting the top-level message (protobuf-es) */
	Buf b = nested(99);
	CHECK(decodes((const uint8_t *)b.p, b.n));
	b = nested(100);
	CHECK(!decodes((const uint8_t *)b.p, b.n));
	/* groups inside an unknown field count too */
	Buf g = {0};
	for (int i = 0; i < 99; i++)
		pb_put_tag(&g, 50, WT_SGROUP);
	for (int i = 0; i < 99; i++)
		pb_put_tag(&g, 50, WT_EGROUP);
	CHECK(decodes((const uint8_t *)g.p, g.n));
	Buf g2 = {0};
	pb_put_tag(&g2, 50, WT_SGROUP);
	buf_add(&g2, g.p, g.n);
	pb_put_tag(&g2, 50, WT_EGROUP);
	CHECK(!decodes((const uint8_t *)g2.p, g2.n));
}

static void test_raw_fields(void)
{
	static const char data[] = "\x08\x96\x01\x12\x02hi\x1d\x01\x02\x03\x04";
	PbReader r = {(const uint8_t *)data, (const uint8_t *)data + sizeof data - 1};
	PbRawField f;
	CHECK(pb_next_field(&r, &f) && f.num == 1 && f.wt == WT_VARINT && f.v == 150);
	CHECK(pb_next_field(&r, &f) && f.num == 2 && f.wt == WT_LEN && f.vlen == 2 && f.val[0] == 'h');
	CHECK(pb_next_field(&r, &f) && f.num == 3 && f.wt == WT_I32);
	CHECK(!pb_next_field(&r, &f));
}

TESTS(TEST(test_varint), TEST(test_tags), TEST(test_packed), TEST(test_merge), TEST(test_closed_enum),
      TEST(test_wire_type_mismatch), TEST(test_bytes_and_doubles), TEST(test_depth), TEST(test_raw_fields))
