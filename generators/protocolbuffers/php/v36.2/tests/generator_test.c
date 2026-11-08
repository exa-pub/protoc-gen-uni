/* protocolbuffers/php v36.2: requests on which upstream crashes give an error. Includes the
 * unity build to reach its static functions. */
#include "../generator.c"
#include "test.h"

static Str field(Buf *b, const char *name, int number, int type, const char *type_name)
{
	/* FieldDescriptorProto: name, number, label (optional), type, type_name */
	b->n = 0;
	pb_put_bytes(b, 1, str_c(name));
	pb_put_tag(b, 3, 0);
	pb_put_varint(b, (uint64_t)number);
	pb_put_tag(b, 4, 0);
	pb_put_varint(b, 1);
	pb_put_tag(b, 5, 0);
	pb_put_varint(b, (uint64_t)type);
	if (type_name)
		pb_put_bytes(b, 6, str_c(type_name));
	return buf_view(b);
}

static Str message(Buf *b, const char *name, Str fld)
{
	b->n = 0;
	pb_put_bytes(b, 1, str_c(name));
	pb_put_bytes(b, 2, fld);
	return buf_view(b);
}

static Str file(Buf *b, const char *name, const char *package, const char *dep, Str msg)
{
	b->n = 0;
	pb_put_bytes(b, 1, str_c(name));
	pb_put_bytes(b, 2, str_c(package));
	if (dep)
		pb_put_bytes(b, 3, str_c(dep));
	pb_put_bytes(b, 4, msg);
	pb_put_bytes(b, 12, STR_LIT("proto3"));
	return buf_view(b);
}

/* A file named google/protobuf/wrappers.proto whose message has no field "value", used as the
 * type of a field: upstream dereferences FindFieldByName("value") (SIGSEGV). */
static void test_wrapper_without_value(void)
{
	Buf f = {0}, m = {0}, w = {0}, a = {0}, req = {0}, out = {0};
	file(&w, "google/protobuf/wrappers.proto", "google.protobuf", NULL,
	     message(&m, "Int32Value", field(&f, "x", 1, 5, NULL)));
	file(&a, "a.proto", "pkg", "google/protobuf/wrappers.proto",
	     message(&m, "M", field(&f, "w", 1, 11, ".google.protobuf.Int32Value")));
	pb_put_bytes(&req, 1, STR_LIT("a.proto"));
	pb_put_bytes(&req, 15, buf_view(&w));
	pb_put_bytes(&req, 15, buf_view(&a));
	CHECK(lp_plugin_main(buf_view(&req), STR_LIT(""), &php_generator, &out) == NULL);
	Buf want = {0};
	pb_put_bytes(&want, 1,
		     STR_LIT("protoc-gen-php: wrapper type google.protobuf.Int32Value has no field \"value\" "
			     "(upstream crashes)"));
	CHECK_MEM(out.p, out.n, want.p, want.n);
}

TESTS(TEST(test_wrapper_without_value))
