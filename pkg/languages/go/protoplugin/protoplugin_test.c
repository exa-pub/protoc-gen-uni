/* Unit tests of pkg/languages/go/protoplugin. The expected values come from the upstream Go
 * library (github.com/bufbuild/protoplugin@750e09ce93e1 with protobuf-go v1.36.11): protoplugin.Run
 * on the same requests, and ResponseWriter.ToCodeGeneratorResponse + proto.Marshal (hex). */
#include "internal.h"
#include "protogen.h"
#include "test.h"

/* ---- request encoding ---- */

typedef struct {
	const char *name;
	size_t name_n; /* 0: strlen(name) */
	const char *deps[3];
} TFile;

static void put_file(Buf *b, uint32_t num, const TFile *f)
{
	Buf m = {0};
	pb_put_bytes(&m, 1, (Str){f->name, f->name_n ? f->name_n : strlen(f->name)});
	for (int i = 0; i < 3 && f->deps[i]; i++)
		pb_put_bytes(&m, 3, str_c(f->deps[i]));
	pb_put_bytes(b, num, buf_view(&m));
}

typedef struct {
	const char *gen[3];
	TFile proto[3];
	TFile sfd[3];
	bool version;
	int32_t major, minor, patch;
} TReq;

static const char *validate(const TReq *r)
{
	Buf b = {0};
	for (int i = 0; i < 3 && r->gen[i]; i++)
		pb_put_bytes(&b, 1, str_c(r->gen[i]));
	if (r->version) {
		Buf v = {0};
		int32_t parts[3] = {r->major, r->minor, r->patch};
		for (int i = 0; i < 3; i++) {
			pb_put_tag(&v, (uint32_t)i + 1, WT_VARINT);
			pb_put_varint(&v, (uint64_t)(int64_t)parts[i]);
		}
		pb_put_bytes(&b, 3, buf_view(&v));
	}
	for (int i = 0; i < 3 && r->proto[i].name; i++)
		put_file(&b, 15, &r->proto[i]);
	for (int i = 0; i < 3 && r->sfd[i].name; i++)
		put_file(&b, 17, &r->sfd[i]);
	const char *perr;
	PbMsg *req = pg_parse_request((const uint8_t *)b.p, b.n, &perr);
	if (!req)
		return perr;
	const char *err = gopp_validate_request(req);
	return err ? err : "<nil>";
}

#define F(n) {.name = (n)}

static void test_validate_request(void)
{
	static const struct {
		TReq r;
		const char *want;
	} cases[] = {
		{{.gen = {"a.proto"}, .proto = {F("a.proto")}}, "<nil>"},
		{{.gen = {"a.proto"}}, "CodeGeneratorRequest: proto_file: empty"},
		{{.proto = {F("a.proto")}}, "CodeGeneratorRequest: file_to_generate: empty"},
		{{.gen = {""}, .proto = {F("a.proto")}}, "CodeGeneratorRequest: file_to_generate: path was empty"},
		{{.gen = {"/x/../a.proto"}, .proto = {F("a.proto")}},
		 "CodeGeneratorRequest: file_to_generate: path \"/a.proto\" should be relative"},
		{{.gen = {"a/../../b.proto"}, .proto = {F("a.proto")}},
		 "CodeGeneratorRequest: file_to_generate: path \"../b.proto\" should not jump context"},
		{{.gen = {"./a//b/.proto"}, .proto = {F("a.proto")}},
		 "CodeGeneratorRequest: file_to_generate: path \"./a//b/.proto\" to be given as \"a/b/.proto\""},
		{{.gen = {"a.prot"}, .proto = {F("a.proto")}},
		 "CodeGeneratorRequest: file_to_generate: path \"a.prot\" should have the .proto file extension"},
		{{.gen = {"a.proto", "a.proto"}, .proto = {F("a.proto")}},
		 "CodeGeneratorRequest: file_to_generate: duplicate path \"a.proto\""},
		{{.gen = {"a.proto"}, .proto = {{.name = "a.proto", .deps = {"b.proto", "b.proto"}}}},
		 "CodeGeneratorRequest: proto_file.dependency: duplicate path \"b.proto\""},
		{{.gen = {"a.proto"}, .proto = {F("a.proto"), {.name = "x\0\"y", .name_n = 4}}},
		 "CodeGeneratorRequest: proto_file.name: path \"x\\x00\\\"y\" should have the .proto file extension"},
		{{.gen = {"a.proto"}, .proto = {F("a.proto"), F("a.proto")}},
		 "CodeGeneratorRequest: proto_file: duplicate path \"a.proto\""},
		{{.gen = {"b.proto"}, .proto = {F("a.proto")}},
		 "CodeGeneratorRequest: file_to_generate: path \"b.proto\" is not contained within proto_file"},
		{{.gen = {"a.proto"}, .proto = {F("a.proto"), F("b.proto")}, .sfd = {F("a.proto"), F("b.proto")}},
		 "CodeGeneratorRequest: source_file_descriptors: path \"b.proto\" is not contained within file_to_generate"},
		{{.gen = {"a.proto"}, .proto = {F("a.proto")}, .sfd = {F("b.proto")}},
		 "CodeGeneratorRequest: file_to_generate: path \"a.proto\" is not contained within source_file_descriptors"},
		{{.gen = {"a.proto"}, .proto = {F("a.proto")}, .version = true, .major = 1, .minor = -2, .patch = 3},
		 "CodeGeneratorRequest: compiler_version: minor: negative: -2"},
		{{.gen = {"a.proto"}, .proto = {F("a.proto")}, .version = true, .major = 1, .minor = 2, .patch = 3}, "<nil>"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++)
		CHECK_STR(validate(&cases[i].r), cases[i].want);
}

/* ---- response writer ---- */

static const char *hex(Buf *b)
{
	Buf h = {0};
	for (size_t i = 0; i < b->n; i++)
		buf_fmt(&h, "%02x", (unsigned char)b->p[i]);
	return buf_str(&h);
}

static const char *finish(GoppResponseWriter *w)
{
	Buf out = {0};
	const char *err = gopp_to_response(w, &out);
	return err ? xcat("err ", err) : hex(&out);
}

static GoppFile file(const char *name, const char *ip, const char *content)
{
	GoppFile f = {0};
	if (name)
		f.name = str_c(name), f.has_name = true;
	if (ip)
		f.insertion_point = str_c(ip), f.has_insertion_point = true;
	if (content)
		f.content = str_c(content), f.has_content = true;
	return f;
}

static void test_response_writer(void)
{
	GoppResponseWriter *w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("x"));
	gopp_add_error(w, STR_LIT(""));
	gopp_add_error(w, STR_LIT("e1"));
	gopp_add_error(w, STR_LIT("e2"));
	gopp_set_feature_proto3_optional(w);
	gopp_set_feature_supports_editions(w, 999, 0x7FFFFFFF);
	CHECK_STR(finish(w), "0a0665313b206532100318e70720ffffffff077a090a04612e676f7a0178");
	CHECK_STR(finish(w), "err ResponseWriter cannot be reused");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("x"));
	GoppFile fs[] = {file(NULL, NULL, "y"), file(NULL, NULL, NULL), file("b.go", "ip", NULL), file(NULL, NULL, "z")};
	gopp_add_response_files(w, fs, 2);
	gopp_add_response_files(w, fs + 2, 2);
	CHECK_STR(finish(w), "7a0a0a04612e676f7a0278797a0d0a04622e676f120269707a017a");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_response_files(w, fs, 1);
	CHECK_STR(finish(w), "err CodeGeneratorResponse: file: first value had no name set");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("x"));
	GoppFile ip = file(NULL, "y", NULL);
	gopp_add_response_files(w, &ip, 1);
	CHECK_STR(finish(w), "err CodeGeneratorResponse: file: empty name with non-empty insertion point");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("./a.go"), STR_LIT("x"));
	CHECK_STR(finish(w), "err CodeGeneratorResponse: file: path \"./a.go\" is not equal to \"a.go\", and therefore does "
			     "not conform to the Protobuf generation specification. The path must be non-empty, relative, "
			     "use \"/\" instead of \"\\\" as the path separator, and not use \".\" or \"..\" as part of the "
			     "path.");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("x"));
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("y"));
	CHECK_STR(finish(w), "err CodeGeneratorResponse: file: duplicate generated file name \"a.go\".");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("x"));
	GoppFile dup = file("a.go", "ip", "y");
	gopp_add_response_files(w, &dup, 1);
	CHECK_STR(finish(w), "7a090a04612e676f7a01787a0d0a04612e676f120269707a0179");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_add_file(w, STR_LIT("/a.go"), STR_LIT("x"));
	CHECK_STR(finish(w), "err CodeGeneratorResponse: file: path \"/a.go\" should be relative");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_set_supported_features(w, 13);
	CHECK_STR(finish(w), "err CodeGeneratorResponse: supported_features: unknown CodeGeneratorResponse.Features: 1101");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_set_supported_features(w, 2);
	gopp_set_maximum_edition(w, 5);
	CHECK_STR(finish(w), "err CodeGeneratorResponse: supported_features: FEATURE_SUPPORTS_EDITIONS specified but no "
			     "minimum_edition set");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_set_supported_features(w, 2);
	gopp_set_minimum_edition(w, 5);
	CHECK_STR(finish(w), "err CodeGeneratorResponse: supported_features: FEATURE_SUPPORTS_EDITIONS specified but no "
			     "maximum_edition set");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_set_feature_supports_editions(w, 1001, 1000);
	CHECK_STR(finish(w), "err CodeGeneratorResponse: minimum_edition 1001 is greater than maximum_edition 1000");

	w = gopp_response_writer_new(NULL, NULL);
	gopp_set_minimum_edition(w, -1);
	gopp_set_supported_features(w, 0);
	CHECK_STR(finish(w), "18ffffffffffffffffff01");
}

static void lenient(void *ctx, Str warning)
{
	Buf *b = ctx;
	buf_add_str(b, warning);
	buf_addc(b, '\n');
}

static void test_lenient(void)
{
	Buf warnings = {0};
	GoppResponseWriter *w = gopp_response_writer_new(lenient, &warnings);
	gopp_add_file(w, STR_LIT("./a.go"), STR_LIT("x"));
	gopp_add_file(w, STR_LIT("a.go"), STR_LIT("y"));
	CHECK_STR(finish(w), "7a090a04612e676f7a0178");
	CHECK_STR(buf_str(&warnings),
		  "path \"./a.go\" is not equal to \"a.go\", and therefore does not conform to the Protobuf generation "
		  "specification. The path must be non-empty, relative, use \"/\" instead of \"\\\" as the path separator, "
		  "and not use \".\" or \"..\" as part of the path. Generation will continue without error here, but please "
		  "raise an issue with the maintainer of the plugin and reference "
		  "https://github.com/protocolbuffers/protobuf/blob/95e6c5b4746dd7474d540ce4fb375e3f79a086f8/src/google/"
		  "protobuf/compiler/plugin.proto#L122\n"
		  "duplicate generated file name \"a.go\". Generation will continue without error here and drop the second "
		  "occurrence of this file, but please raise an issue with the maintainer of the plugin.\n");
}

static void test_path_clean(void)
{
	/* filepath.Clean (Go) */
	static const char *const cases[][2] = {
		{"", "."},         {".", "."},         {"..", ".."},         {"a/..", "."},     {"a/../..", ".."},
		{"/../a", "/a"},   {"a//b/./c/", "a/b/c"}, {"../../a/b/../c", "../../a/c"}, {"/", "/"}, {"..a/.b", "..a/.b"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
		Str got = gopp_path_clean(str_c(cases[i][0]));
		CHECK_MEM(got.p, got.n, cases[i][1], strlen(cases[i][1]));
	}
}

static void test_parse_response(void)
{
	Buf b = {0};
	pb_put_bytes(&b, 1, STR_LIT("err"));
	Buf f = {0};
	pb_put_bytes(&f, 1, STR_LIT("a.go"));
	pb_put_bytes(&f, 15, STR_LIT("x"));
	pb_put_bytes(&b, 15, buf_view(&f));
	GoppFile *files;
	int n;
	Str error;
	CHECK(gopp_parse_response(buf_view(&b), &files, &n, &error));
	CHECK(n == 1);
	CHECK_MEM(error.p, error.n, "err", 3);
	CHECK(files[0].has_name && files[0].has_content && !files[0].has_insertion_point);
	CHECK_MEM(files[0].content.p, files[0].content.n, "x", 1);
	CHECK(!gopp_parse_response(STR_LIT("\x7a\x05"), &files, &n, &error));
}

TESTS(TEST(test_validate_request), TEST(test_response_writer), TEST(test_lenient), TEST(test_path_clean),
      TEST(test_parse_response))
