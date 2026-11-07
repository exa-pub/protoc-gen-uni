/* pkg/uni: parameter splitting, generator lookup and dispatch, with the cases and error texts of
 * pkg/uni/uni_test.go of the Go implementation. */
#include "test.h"
#include "uni.h"

static void test_split_parameter(void)
{
	static const struct {
		const char *in, *name, *version, *rest;
		bool err;
	} tests[] = {
		{"", "", "", "", false},
		{"uni-name=a/b,uni-version=v1.0.0", "a/b", "v1.0.0", "", false},
		{"target=ts,uni-name=a/b,import_extension=js,uni-version=v1.0.0", "a/b", "v1.0.0",
		 "target=ts,import_extension=js", false},
		{" uni-name = a/b ,x, y=1", "a/b", "", "x, y=1", false},
		{"Mfoo.proto=example.com/foo,paths=source_relative", "", "", "Mfoo.proto=example.com/foo,paths=source_relative", false},
		{",a,,uni-name=x,", "x", "", ",a,,", false},
		{"\xc2\xa0uni-name\xe3\x80\x80=\tv\xc2\x85", "v", "", "", false}, /* unicode.IsSpace */
		{"uni-name=a,uni-name=b", NULL, NULL, NULL, true},
		{"uni-version=a,uni-version=b", NULL, NULL, NULL, true},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		Str name, version, rest;
		const char *err = uni_split_parameter(str_c(tests[i].in), &name, &version, &rest);
		CHECK((err != NULL) == tests[i].err);
		if (err)
			continue;
		CHECK_MEM(name.p, name.n, tests[i].name, strlen(tests[i].name));
		CHECK_MEM(version.p, version.n, tests[i].version, strlen(tests[i].version));
		CHECK_MEM(rest.p, rest.n, tests[i].rest, strlen(tests[i].rest));
	}
	Str n, v, r;
	CHECK_STR(uni_split_parameter(STR_LIT("uni-name=a,uni-name=b"), &n, &v, &r),
		  "protoc-gen-uni: duplicate parameter \"uni-name\"");
	CHECK_STR(uni_split_parameter(STR_LIT("uni-version=a, uni-version =b"), &n, &v, &r),
		  "protoc-gen-uni: duplicate parameter \"uni-version\"");
	/* NUL is not a separator */
	CHECK(!uni_split_parameter(STR_LIT("x\0,uni-name=a\0b"), &n, &v, &r));
	CHECK_MEM(n.p, n.n, "a\0b", 3);
	CHECK_MEM(r.p, r.n, "x\0", 2);
}

static Str got_param;
static bool got_param_set;
static const char *gen_ab(PbMsg *req, Str raw, Buf *out)
{
	got_param_set = pb_has(req, CGR_PARAMETER);
	got_param = pb_bytes(req, CGR_PARAMETER);
	buf_adds(out, "ok");
	return NULL;
}

static const UniGenerator g1 = {"a/b", "v1.0.0", gen_ab}, g2 = {"a/b", "v1.1.0", gen_ab},
			  g3 = {"c/d", "v1.0.0", gen_ab}, g0 = {"0/z", "v2.0.0", gen_ab};
/* deliberately unsorted: errors list sorted names and versions */
static const UniGenerator *const gens[] = {&g2, &g3, &g1, &g0};

static const char *run(Str param, Buf *out)
{
	PbMsg *req = pb_new(&pbd_CodeGeneratorRequest);
	pb_set_bytes(req, CGR_PARAMETER, param);
	*out = (Buf){0};
	got_param_set = false;
	uni_run(req, (Str){"", 0}, gens, 4, out);
	return buf_str(out);
}

/* The error message of a response, or "" if none. */
static const char *response_error(Buf *out)
{
	PbReader r = {(const uint8_t *)out->p, (const uint8_t *)out->p + out->n};
	PbRawField f;
	while (pb_next_field(&r, &f))
		if (f.num == CGRESP_ERROR && f.wt == WT_LEN)
			return xstrndup((const char *)f.val, f.vlen);
	return "";
}

static void test_run(void)
{
	Buf out;
	CHECK_STR(run(STR_LIT("x=1,uni-name=a/b,uni-version=v1.0.0,y"), &out), "ok");
	CHECK(got_param_set && str_eqc(got_param, "x=1,y"));
	CHECK_STR(run(STR_LIT("uni-name=a/b,uni-version=v1.0.0"), &out), "ok");
	CHECK(!got_param_set);
	CHECK_STR(run(STR_LIT("uni-name=a/b,uni-version=v1.0.0,"), &out), "ok");
	CHECK(!got_param_set);

	static const struct {
		const char *param, *want;
	} errs[] = {
		{"", "protoc-gen-uni: parameters uni-name=<owner>/<plugin> and uni-version=<version> are required, "
		     "e.g. uni-name=bufbuild/es,uni-version=v2.17.0"},
		{"uni-name=a/b", "protoc-gen-uni: parameters uni-name=<owner>/<plugin> and uni-version=<version> are "
				 "required, e.g. uni-name=bufbuild/es,uni-version=v2.17.0"},
		{"uni-name=x/y,uni-version=v1.0.0", "protoc-gen-uni: unknown generator \"x/y\", available: 0/z, a/b, c/d"},
		{"uni-name=a/b,uni-version=v9.9.9",
		 "protoc-gen-uni: unknown version \"v9.9.9\" of \"a/b\", available: v1.0.0, v1.1.0"},
		{"uni-name=a/b,uni-name=a/b,uni-version=v1.0.0", "protoc-gen-uni: duplicate parameter \"uni-name\""},
		{"uni-name=\xe2\x80\x8b\"é\x01\xff,uni-version=v1",
		 "protoc-gen-uni: unknown generator \"\\u200b\\\"é\\x01\\xff\", available: 0/z, a/b, c/d"},
	};
	for (size_t i = 0; i < sizeof errs / sizeof *errs; i++) {
		run(str_c(errs[i].param), &out);
		CHECK_STR(response_error(&out), errs[i].want);
		/* the response has nothing but the error */
		CHECK(out.n == 2 + strlen(errs[i].want) || strlen(errs[i].want) >= 128);
	}
}

static void test_main(void)
{
	Buf out = {0};
	CHECK_STR(uni_main((const uint8_t *)"\x12\x03", 2, gens, 4, &out),
		  "protoc-gen-uni: parse request: proto: cannot parse invalid wire-format data");
	/* parameter "uni-name=a/b,uni-version=v1.1.0" */
	static const char req[] = "\x12\x1funi-name=a/b,uni-version=v1.1.0";
	CHECK(uni_main((const uint8_t *)req, sizeof req - 1, gens, 4, &out) == NULL);
	CHECK_STR(buf_str(&out), "ok");
	/* a FileDescriptorProto with 120 nested messages: valid, but deeper than PB_MAX_DEPTH */
	Buf msg = {0};
	for (int i = 0; i < 120; i++) {
		Buf b = {0};
		pb_put_bytes(&b, i ? 3 : 1, i ? buf_view(&msg) : str_c("M")); /* nested_type, name */
		msg = b;
	}
	Buf file = {0}, deep = {0};
	pb_put_bytes(&file, 4, buf_view(&msg)); /* message_type */
	pb_put_bytes(&deep, 15, buf_view(&file)); /* proto_file */
	pb_put_bytes(&deep, 2, STR_LIT("x,uni-name=a/b,uni-version=v1.1.0"));
	/* a generator without parses_raw: a parse error */
	out = (Buf){0};
	CHECK_STR(uni_main((const uint8_t *)deep.p, deep.n, gens, 4, &out),
		  "protoc-gen-uni: parse request: messages nested deeper than 100 levels are not supported");
	CHECK(out.n == 0);
	/* a generator with parses_raw gets the parameter (the last one, after groups nested deeper
	 * than PB_MAX_DEPTH) and the raw request */
	static const UniGenerator raw_ab = {"a/b", "v1.1.0", gen_ab, true};
	static const UniGenerator *const raw_gens[] = {&g1, &raw_ab};
	Buf group = {0};
	for (int i = 0; i < 150; i++)
		pb_put_tag(&group, 7, WT_SGROUP);
	for (int i = 0; i < 150; i++)
		pb_put_tag(&group, 7, WT_EGROUP);
	Buf deep2 = {0};
	buf_add(&deep2, deep.p, deep.n);
	buf_add(&deep2, group.p, group.n);
	pb_put_bytes(&deep2, 2, STR_LIT("uni-version=v1.1.0,y,uni-name=a/b"));
	out = (Buf){0};
	CHECK(uni_main((const uint8_t *)deep2.p, deep2.n, raw_gens, 2, &out) == NULL);
	CHECK_STR(buf_str(&out), "ok");
	CHECK(got_param_set && str_eqc(got_param, "y"));
	/* no uni-name: the error response */
	out = (Buf){0};
	CHECK(uni_main((const uint8_t *)deep.p, deep.n - 35, raw_gens, 2, &out) == NULL);
	CHECK_STR(response_error(&out), "protoc-gen-uni: parameters uni-name=<owner>/<plugin> and "
					"uni-version=<version> are required, e.g. uni-name=bufbuild/es,uni-version=v2.17.0");
	/* malformed top level (an unterminated group) after the deep part */
	buf_add(&deep2, group.p, group.n - 1);
	out = (Buf){0};
	CHECK_STR(uni_main((const uint8_t *)deep2.p, deep2.n, raw_gens, 2, &out),
		  "protoc-gen-uni: parse request: messages nested deeper than 100 levels are not supported");
}

TESTS(TEST(test_split_parameter), TEST(test_run), TEST(test_main))
