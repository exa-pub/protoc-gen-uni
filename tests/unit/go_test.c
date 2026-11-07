/* pkg/languages/go: strs naming, strconv.FormatFloat, struct tags, edition features, the Go wire
 * format, and protogen naming, import paths and parameter errors. Expected values were produced
 * with the Go implementation (google.golang.org/protobuf v1.36.12, Go 1.26.5). */
#include "go1_26_5.h"
#include "protogen.h"
#include "strs.h"
#include "tag.h"
#include "test.h"
#include "genid.h"

static void test_go_camel_case(void)
{
	static const struct {
		const char *in, *want;
	} tests[] = {
		{"", ""},
		{"one", "One"},
		{"one_two", "OneTwo"},
		{"One_Two", "One_Two"},
		{"_my_field_name_2", "XMyFieldName_2"},
		{"Something_Capped", "Something_Capped"},
		{"my_Name", "My_Name"},
		{"OneTwo", "OneTwo"},
		{"_", "X"},
		{"_a_", "XA_"},
		{"one.two", "OneTwo"},
		{"one.Two", "One_Two"},
		{"one_two.three_four", "OneTwoThreeFour"},
		{"one_two.Three_four", "OneTwo_ThreeFour"},
		{"_one._two", "XOne_XTwo"},
		{"SCREAMING_SNAKE_CASE", "SCREAMING_SNAKE_CASE"},
		{"double__underscore", "Double_Underscore"},
		{"foo_bar_baz_123", "FooBarBaz_123"},
		{"a1b2_c3", "A1B2C3"},
		{"x__y", "X_Y"},
		{"__z", "XZ"},
		{"abc.def.ghi", "AbcDefGhi"},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++)
		CHECK_STR(strs_go_camel_case(tests[i].in), tests[i].want);
}

static void test_go_sanitized(void)
{
	static const struct {
		const char *in, *want;
	} tests[] = {
		{"", "_"},
		{"foo", "foo"},
		{"foo/bar.proto", "foo_bar_proto"},
		{"a-b-c", "a_b_c"},
		{"1abc", "_1abc"},
		{"type", "_type"},
		{"func", "_func"},
		{"_x", "__x"},
		{"\xc3\xa9", "\xc3\xa9"},
		{"\xe6\x97\xa5\xe6\x9c\xac", "\xe6\x97\xa5\xe6\x9c\xac"},
		{"a\xff" "b", "a_b"},
		{"foo bar", "foo_bar"},
		{"map", "_map"},
		{"string", "string"},
		{"\xc3\x9c" "n\xc3\xaf" "c\xc3\xb6" "d\xc3\xa9.proto", "\xc3\x9c" "n\xc3\xaf" "c\xc3\xb6" "d\xc3\xa9_proto"},
		{"x\xe2\x80\xa8y", "x_y"},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++)
		CHECK_STR(strs_go_sanitized(tests[i].in, strlen(tests[i].in)), tests[i].want);
	CHECK_STR(strs_go_sanitized("a\0b", 3), "a_b");
}

static void test_trim_enum_prefix(void)
{
	static const struct {
		const char *s, *prefix, *want;
	} tests[] = {
		{"FOO_BAR", "foo", "BAR"}, {"FOO_BAR", "foobar", "FOO_BAR"}, {"FOOBAR_X", "foobar", "X"},
		{"FOO__BAR", "foo", "BAR"}, {"_FOO_BAR", "foo", "BAR"},     {"BAR", "foo", "BAR"},
		{"FOO", "foo", "FOO"},      {"FOO_", "foo", "FOO_"},         {"F_O_O_X", "foo", "X"},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++)
		CHECK_STR(strs_trim_enum_prefix(tests[i].s, tests[i].prefix), tests[i].want);
	CHECK_STR(strs_json_camel_case("foo_bar_baz"), "fooBarBaz");
	CHECK_STR(strs_json_camel_case("_foo__bar1_2"), "FooBar12");
}

static void test_format_float(void)
{
	static const struct {
		double f;
		const char *g64, *g32;
	} tests[] = {
		{0, "0", "0"},
		{1, "1", "1"},
		{-1, "-1", "-1"},
		{0.1, "0.1", "0.1"},
		{1.5, "1.5", "1.5"},
		{1e+06, "1e+06", "1e+06"},
		{1e+21, "1e+21", "1e+21"},
		{1e+20, "1e+20", "1e+20"},
		{1.23456789e+08, "1.23456789e+08", "1.2345679e+08"},
		{1e-05, "1e-05", "1e-05"},
		{0.0001, "0.0001", "0.0001"},
		{0.000123, "0.000123", "0.000123"},
		{3.4028234663852886e+38, "3.4028234663852886e+38", "3.4028235e+38"},
		{5e-324, "5e-324", "0"},
		{2.2250738585072014e-308, "2.2250738585072014e-308", "0"},
		{-0.0, "-0", "-0"},
		{100000, "100000", "100000"},
		{1.234567e+06, "1.234567e+06", "1.234567e+06"},
		{0.30000000000000004, "0.30000000000000004", "0.3"},
		{0.3333333333333333, "0.3333333333333333", "0.33333334"},
		{2.5e-07, "2.5e-07", "2.5e-07"},
		{9.007199254740992e+15, "9.007199254740992e+15", "9.007199e+15"},
		{4.35, "4.35", "4.35"},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		CHECK_STR(go1_26_5_format_float(tests[i].f, 'g', -1, 64), tests[i].g64);
		CHECK_STR(go1_26_5_format_float(tests[i].f, 'g', -1, 32), tests[i].g32);
	}
	CHECK_STR(go1_26_5_format_float(1.0 / 0.0, 'g', -1, 64), "+Inf");
	CHECK_STR(go1_26_5_format_float(3.14159, 'e', 3, 64), "3.142e+00");
	CHECK_STR(go1_26_5_format_float(3.14159, 'f', 2, 64), "3.14");
}

/* ---- struct tags ---- */

static GoFile tag_file = {.d = {.kind = GD_FILE, .name = "a.proto", .full_name = "pkg"}, .syntax = GO_PROTO2};

static GoField field(const char *name, int32_t num, int kind, int card)
{
	GoField f = {0};
	f.d.kind = GD_FIELD;
	f.d.name = name;
	f.d.file = &tag_file;
	f.number = num;
	f.kind = kind;
	f.cardinality = card;
	f.json_name = str_c(strs_json_camel_case(name));
	return f;
}

static void check_tag(const GoField *f, const char *enum_name, const char *want)
{
	Str got = tag_marshal(f, enum_name);
	CHECK_MEM(got.p, got.n, want, strlen(want));
}

static void test_tag_marshal(void)
{
	GoField f = field("foo_bar", 1, PT_INT32, GO_OPTIONAL);
	check_tag(&f, NULL, "varint,1,opt,name=foo_bar,json=fooBar");
	f = field("x", 2, PT_SINT64, GO_REQUIRED);
	check_tag(&f, NULL, "zigzag64,2,req,name=x");
	f = field("vals", 3, PT_FIXED32, GO_REPEATED);
	f.features.IsPacked = true;
	check_tag(&f, NULL, "fixed32,3,rep,packed,name=vals");
	f = field("s", 4, PT_STRING, GO_OPTIONAL);
	f.has_default = true;
	f.default_value.s = STR_LIT("a,b\"c");
	check_tag(&f, NULL, "bytes,4,opt,name=s,def=a,b\"c");
	f = field("b", 5, PT_BYTES, GO_OPTIONAL);
	f.has_default = true;
	f.default_value.s = STR_LIT("\x00\n\x7f'\\");
	check_tag(&f, NULL, "bytes,5,opt,name=b,def=\\000\\n\\177\\'\\\\");
	f = field("fl", 6, PT_FLOAT, GO_OPTIONAL);
	f.has_default = true;
	f.default_value.f = (double)(float)0.1;
	check_tag(&f, NULL, "fixed32,6,opt,name=fl,def=0.1");
	f = field("d", 7, PT_DOUBLE, GO_OPTIONAL);
	f.has_default = true;
	f.default_value.f = -1.0 / 0.0;
	check_tag(&f, NULL, "fixed64,7,opt,name=d,def=-inf");
	f = field("e", 8, PT_ENUM, GO_OPTIONAL);
	f.has_default = true;
	f.default_value.i = -2;
	check_tag(&f, "pkg.Color", "varint,8,opt,name=e,enum=pkg.Color,def=-2");
	f = field("ok", 9, PT_BOOL, GO_OPTIONAL);
	f.has_default = true;
	f.default_value.b = true;
	check_tag(&f, NULL, "varint,9,opt,name=ok,def=1");
	/* proto3, oneof, explicit json_name equal to the name, extensions */
	static GoFile p3 = {.d = {.kind = GD_FILE, .name = "b.proto"}, .syntax = GO_PROTO3};
	GoOneof o = {0};
	f = field("u", 10, PT_UINT64, GO_OPTIONAL);
	f.d.file = &p3;
	f.oneof = &o;
	f.json_name = STR_LIT("u");
	check_tag(&f, NULL, "varint,10,opt,name=u,proto3,oneof");
	f = field("ext_field", 100, PT_STRING, GO_OPTIONAL);
	f.d.file = &p3;
	f.is_extension = true;
	f.json_name = STR_LIT("[pkg.ext_field]");
	check_tag(&f, NULL, "bytes,100,opt,name=ext_field");
	/* groups use the message name */
	GoMessage group = {.d = {.name = "MyGroup"}};
	f = field("mygroup", 11, PT_GROUP, GO_OPTIONAL);
	f.message_type = &group;
	check_tag(&f, NULL, "group,11,opt,name=MyGroup,json=mygroup");
}

/* ---- features and the wire format ---- */

/* internal/editiondefaults/editions_defaults.binpb of protobuf-go v1.36.12 */
static const uint8_t editions_defaults[] = {
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

static bool features_eq(GoEditionFeatures a, GoEditionFeatures b)
{
	return a.IsFieldPresence == b.IsFieldPresence && a.IsLegacyRequired == b.IsLegacyRequired && a.IsOpenEnum == b.IsOpenEnum &&
	       a.IsPacked == b.IsPacked && a.IsUTF8Validated == b.IsUTF8Validated && a.IsDelimitedEncoded == b.IsDelimitedEncoded &&
	       a.IsJSONCompliant == b.IsJSONCompliant && a.GenerateLegacyUnmarshalJSON == b.GenerateLegacyUnmarshalJSON &&
	       a.APILevel == b.APILevel && a.StripEnumPrefix == b.StripEnumPrefix;
}

static PbMsg *feature_set(const uint8_t *p, size_t n)
{
	PbMsg *m = pb_new(&gopb_schema_msg(gopb_schema_base(), "google.protobuf.FeatureSet")->pb);
	CHECK(gopb_decode(m, p, n) == NULL);
	return m;
}

/* features_defaults (a table) equals getFeatureSetFor over the embedded binpb bytes. */
static void test_features_defaults(void)
{
	PbReader r = {editions_defaults, editions_defaults + sizeof editions_defaults};
	PbRawField f;
	int n = 0;
	while (pb_next_field(&r, &f)) {
		if (f.num != 1)
			continue;
		PbReader dr = {f.val, f.val + f.vlen};
		PbRawField df;
		int edition = 0;
		PbMsg *overridable = NULL, *fixed = NULL;
		while (pb_next_field(&dr, &df)) {
			if (df.num == 3)
				edition = (int)df.v;
			else if (df.num == 4)
				overridable = feature_set(df.val, df.vlen);
			else if (df.num == 5)
				fixed = feature_set(df.val, df.vlen);
		}
		/* fs := Clone(fixed); Merge(fs, overridable); merged into empty features */
		GoEditionFeatures got = features_merge(features_merge((GoEditionFeatures){0}, fixed), overridable);
		CHECK(features_eq(got, features_for_edition(edition)));
		n++;
	}
	CHECK(n == 5);
	CHECK(features_for_edition(GENID_Edition_EDITION_PROTO2).IsFieldPresence);
	CHECK(!features_for_edition(GENID_Edition_EDITION_PROTO2).IsOpenEnum);
	CHECK(features_for_edition(GENID_Edition_EDITION_2024).APILevel == 3);
	CHECK(features_for_edition(GENID_Edition_EDITION_2023).APILevel == 0);
}

static void test_features_merge_and_wire(void)
{
	/* FeatureSet{field_presence: IMPLICIT, [pb.go]: {api_level: API_HYBRID}}, with an unknown
	 * field 9 and pb.go written last on the wire */
	static const uint8_t in[] = {0x08, 0x02, 0x48, 0x01, 0xd2, 0x3e, 0x02, 0x10, 0x02};
	PbMsg *fs = feature_set(in, sizeof in);
	GoEditionFeatures base = features_for_edition(GENID_Edition_EDITION_2023);
	GoEditionFeatures got = features_merge(base, fs);
	CHECK(!got.IsFieldPresence && !got.IsLegacyRequired);
	CHECK(got.APILevel == 2 && got.StripEnumPrefix == base.StripEnumPrefix);
	/* deterministic Marshal: extensions first, then fields by number, then unknown fields */
	Buf out = {0};
	gopb_encode(fs, &out);
	static const uint8_t want[] = {0xd2, 0x3e, 0x02, 0x10, 0x02, 0x08, 0x02, 0x48, 0x01};
	CHECK_MEM(out.p, out.n, want, sizeof want);
	/* closed enums keep unknown values; retention=SOURCE fields (7, 8) are stripped */
	static const uint8_t in2[] = {0x10, 0x07, 0x38, 0x01, 0x40, 0x02};
	PbMsg *fs2 = feature_set(in2, sizeof in2);
	CHECK(pb_int(fs2, 2, 0) == 7);
	gopb_strip_source_retention(fs2);
	out = (Buf){0};
	gopb_encode(fs2, &out);
	CHECK_MEM(out.p, out.n, "\x10\x07", 2);
	/* a wrong wire type for a known field or the extension keeps it unknown */
	static const uint8_t in3[] = {0xd0, 0x3e, 0x01, 0x0a, 0x00};
	PbMsg *fs3 = feature_set(in3, sizeof in3);
	CHECK(!pb_has(fs3, 1002) && !pb_has(fs3, 1));
	out = (Buf){0};
	gopb_encode(fs3, &out);
	CHECK_MEM(out.p, out.n, in3, sizeof in3);
	/* malformed input */
	PbMsg *bad = pb_new(&gopb_schema_msg(gopb_schema_base(), "google.protobuf.FeatureSet")->pb);
	CHECK_STR(gopb_decode(bad, (const uint8_t *)"\x08", 1), "proto: cannot parse invalid wire-format data");
	bool lazy;
	CHECK(!features_use_presence_for_field(true, false, false, false, true, &lazy));
	CHECK(features_use_presence_for_field(false, false, true, true, true, &lazy) && lazy);
	CHECK(!features_use_presence_for_field(false, false, true, false, true, &lazy) && !lazy);
	CHECK(features_use_presence_for_field(false, false, false, false, true, &lazy));
}

/* ---- protogen ---- */

static void put_str(Buf *b, uint32_t num, const char *s) { pb_put_bytes(b, num, str_c(s)); }
static void put_int(Buf *b, uint32_t num, int64_t v)
{
	pb_put_tag(b, num, WT_VARINT);
	pb_put_varint(b, (uint64_t)v);
}
static void put_msg(Buf *b, uint32_t num, const Buf *m) { pb_put_bytes(b, num, (Str){m->p ? m->p : "", m->n}); }

static Buf fd_field(const char *name, int num, int type, int label, const char *type_name, int oneof)
{
	Buf f = {0};
	put_str(&f, 1, name);
	put_int(&f, 3, num);
	put_int(&f, 4, label);
	put_int(&f, 5, type);
	if (type_name)
		put_str(&f, 6, type_name);
	if (oneof >= 0)
		put_int(&f, 9, oneof);
	return f;
}

/* a/b.proto: package pkg.v1; go_package "example.com/a;apb"; message Msg { int32 string = 1;
 * int32 get_x = 2; oneof choice { Nested nested = 3; } message Nested {} } enum Color {
 * COLOR_RED = 0; } */
static Buf test_request(const char *go_package)
{
	Buf nested = {0}, msg = {0}, ev = {0}, en = {0}, opts = {0}, file = {0}, req = {0}, oneof = {0};
	put_str(&nested, 1, "Nested");
	put_str(&msg, 1, "Msg");
	Buf f1 = fd_field("string", 1, PT_INT32, LABEL_OPTIONAL, NULL, -1);
	Buf f2 = fd_field("get_x", 2, PT_INT32, LABEL_OPTIONAL, NULL, -1);
	Buf f3 = fd_field("nested", 3, PT_MESSAGE, LABEL_OPTIONAL, ".pkg.v1.Msg.Nested", 0);
	put_msg(&msg, 2, &f1);
	put_msg(&msg, 2, &f2);
	put_msg(&msg, 2, &f3);
	put_msg(&msg, 3, &nested);
	put_str(&oneof, 1, "choice");
	put_msg(&msg, 8, &oneof);
	put_str(&ev, 1, "COLOR_RED");
	put_int(&ev, 2, 0);
	put_str(&en, 1, "Color");
	put_msg(&en, 2, &ev);
	put_str(&file, 1, "a/b.proto");
	put_str(&file, 2, "pkg.v1");
	put_msg(&file, 4, &msg);
	put_msg(&file, 5, &en);
	if (go_package) {
		put_str(&opts, 11, go_package);
		put_msg(&file, 8, &opts);
	}
	put_str(&file, 12, "proto3");
	put_str(&req, 1, "a/b.proto");
	put_msg(&req, 15, &file);
	return req;
}

static PgPlugin *new_plugin(const char *go_package, const char *param, const char **err)
{
	Buf b = test_request(go_package);
	PbMsg *req = pg_parse_request((const uint8_t *)b.p, b.n, err);
	if (!req)
		return NULL;
	pb_set_bytes(req, GENID_CodeGeneratorRequest_Parameter_field_number, str_c(param));
	PgOptions opts = {0};
	Str e;
	PgPlugin *gen = pg_options_new(&opts, req, &e);
	*err = gen ? NULL : xstrndup(e.p, e.n);
	return gen;
}

/* proto.Unmarshal checks required fields (descriptor.proto: NamePart only); detrand.Bool()
 * selects the no-break spaces of the "proto:" prefix. */
static void test_decode_required(void)
{
	const GoMsgDef *uo = gopb_schema_msg(gopb_schema_base(), "google.protobuf.UninterpretedOption");
	CHECK_STR(gopb_decode(pb_new(&uo->pb), (const uint8_t *)"\x12\x00", 2),
		  "proto: required field google.protobuf.UninterpretedOption.NamePart.name_part not set");
	CHECK_STR(gopb_decode(pb_new(&uo->pb), (const uint8_t *)"\x12\x03\x0a\x01x", 5),
		  "proto: required field google.protobuf.UninterpretedOption.NamePart.is_extension not set");
	CHECK(gopb_decode(pb_new(&uo->pb), (const uint8_t *)"\x12\x05\x0a\x01x\x10\x00", 7) == NULL);
	go_detrand = true;
	CHECK_STR(gopb_decode(pb_new(&uo->pb), (const uint8_t *)"\x12", 1), "proto:\xc2\xa0" "cannot parse invalid wire-format data");
	go_detrand = false;
}

static void test_protogen_names(void)
{
	const char *err;
	PgPlugin *gen = new_plugin("example.com/a;apb", "paths=source_relative", &err);
	CHECK(gen != NULL);
	if (!gen)
		return;
	PgFile *f = map_get(&gen->FilesByPath, "a/b.proto");
	CHECK(f && f->Generate);
	CHECK_STR(f->GoPackageName, "apb");
	CHECK_STR(f->GoImportPath, "example.com/a");
	CHECK_STR(f->GeneratedFilenamePrefix, "a/b");
	CHECK_STR(f->GoDescriptorIdent.GoName, "File_a_b_proto");
	PgMessage *m = f->Messages[0];
	CHECK_STR(m->GoIdent.GoName, "Msg");
	/* "String" is a method of every message; "GetX" collides with the getter of "x" */
	CHECK_STR(m->Fields[0]->GoName, "String_");
	CHECK_STR(m->Fields[1]->GoName, "GetX");
	/* the oneof wrapper type collides with the nested message Msg_Nested */
	CHECK_STR(m->Fields[2]->GoIdent.GoName, "Msg_Nested_");
	CHECK_STR(m->Oneofs[0]->GoName, "Choice");
	CHECK_STR(m->Messages[0]->GoIdent.GoName, "Msg_Nested");
	CHECK_STR(f->Enums[0]->Values[0]->GoIdent.GoName, "Color_COLOR_RED");
	CHECK(m->APILevel == PG_API_OPEN);
	const char *compat;
	CHECK_STR(pg_field_method_name(m->Fields[0], "Get", &compat), "GetString_");
	CHECK(compat == NULL);

	/* QualifiedGoIdent: package names from the import path, numbered on conflicts */
	PgGeneratedFile *g = pg_new_generated_file(gen, "x.pb.go", "example.com/a");
	CHECK_STR(pg_qualified_go_ident(g, pg_ident("example.com/a", "Local")), "Local");
	CHECK_STR(pg_qualified_go_ident(g, pg_ident("example.com/b/proto", "X")), "proto.X");
	CHECK_STR(pg_qualified_go_ident(g, pg_ident("example.com/c/proto", "Y")), "proto1.Y");
	CHECK_STR(pg_qualified_go_ident(g, pg_ident("example.com/b/proto", "Z")), "proto.Z");
	CHECK_STR(pg_qualified_go_ident(g, pg_ident("example.com/string", "S")), "string1.S");
	CHECK_STR(pg_qualified_go_ident(g, pg_ident("example.com/go-pkg.v2", "T")), "go_pkg_v2.T");
	P(g, "package apb");
	P(g, "var _ = ", pg_ident("example.com/b/proto", "X"), " // ", 42);
	pg_import(g, "example.com/blank");
	Str content, cerr;
	CHECK(pg_content(g, &content, &cerr));
	const char *want = "package apb\n\nimport (\n\tproto \"example.com/b/proto\"\n\t_ \"example.com/blank\"\n\tproto1 "
			   "\"example.com/c/proto\"\n\tgo_pkg_v2 \"example.com/go-pkg.v2\"\n\tstring1 \"example.com/string\"\n)\n\nvar _ = "
			   "proto.X // 42\n";
	CHECK_MEM(content.p, content.n, want, strlen(want));

	/* Comments.String */
	Str c = pg_comments_string(pg_comments(" a\n b\n"));
	CHECK_MEM(c.p, c.n, "// a\n// b\n", 10);
	c = pg_comments_string(pg_comments(""));
	CHECK(c.n == 0);
}

static void test_protogen_import_paths(void)
{
	const char *err;
	/* paths=import: the file name comes from the import path (cleaned by path.Join) */
	PgPlugin *gen = new_plugin("example.com/x/../y/z;zpb", "", &err);
	CHECK(gen != NULL);
	if (gen) {
		PgFile *f = map_get(&gen->FilesByPath, "a/b.proto");
		CHECK_STR(f->GeneratedFilenamePrefix, "example.com/y/z/b");
		CHECK_STR(f->GoPackageName, "zpb");
	}
	/* M overrides go_package; the package name derives from the go_package import path */
	gen = new_plugin("example.com/a/v1", "Ma/b.proto=example.com/m/other", &err);
	CHECK(gen != NULL);
	if (gen) {
		PgFile *f = map_get(&gen->FilesByPath, "a/b.proto");
		CHECK_STR(f->GoImportPath, "example.com/m/other");
		CHECK_STR(f->GoPackageName, "v1");
	}
	gen = new_plugin(NULL, "Ma/b.proto=example.com/m/other-pkg", &err);
	CHECK(gen != NULL);
	if (gen)
		CHECK_STR(((PgFile *)map_get(&gen->FilesByPath, "a/b.proto"))->GoPackageName, "other_pkg");
	/* apilevelM and default_api_level */
	gen = new_plugin("example.com/a", "default_api_level=API_HYBRID,apilevelMa/b.proto=API_OPAQUE", &err);
	CHECK(gen != NULL);
	if (gen)
		CHECK(((PgFile *)map_get(&gen->FilesByPath, "a/b.proto"))->Messages[0]->APILevel == PG_API_OPAQUE);
}

static void test_protogen_errors(void)
{
	static const struct {
		const char *go_package, *param, *want;
	} tests[] = {
		{"example.com/a", "paths=bad", "unknown path type \"bad\": want \"import\" or \"source_relative\""},
		{"example.com/a", "annotate_code=1", "bad value for parameter \"annotate_code\": want \"true\" or \"false\""},
		{"example.com/a", "default_api_level=OPEN",
		 "unknown API level \"OPEN\" for parameter \"default_api_level\": want \"API_OPEN\", \"API_HYBRID\" or \"API_OPAQUE\""},
		{"example.com/a", "apilevelMx=1",
		 "unknown API level \"1\" for parameter \"apilevelMx\": want \"API_OPEN\", \"API_HYBRID\" or \"API_OPAQUE\""},
		{"example.com/a", "module=example.com,paths=source_relative", "cannot use module= with paths=source_relative"},
		{"apb", "",
		 "invalid Go import path \"apb\" for \"a/b.proto\"\n\nThe import path must contain at least one period ('.') or "
		 "forward slash ('/') character.\n\nSee https://protobuf.dev/reference/go/go-generated#package for more information.\n"},
		{NULL, "",
		 "unable to determine Go import path for \"a/b.proto\"\n\nPlease specify either:\n\t\xe2\x80\xa2 a \"go_package\" "
		 "option in the .proto source file, or\n\t\xe2\x80\xa2 a \"M\" argument on the command line.\n\nSee "
		 "https://protobuf.dev/reference/go/go-generated#package for more information.\n"},
		{"example.com/a;apb", "Mc.proto=example.com/a;other",
		 "Go package \"example.com/a\" has inconsistent names other (c.proto) and apb (a/b.proto)"},
	};
	for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
		const char *err = NULL;
		PgPlugin *gen = new_plugin(tests[i].go_package, tests[i].param, &err);
		CHECK(gen == NULL);
		CHECK_STR(err ? err : "", tests[i].want);
	}
	/* unknown parameters go to ParamFunc (here: none, so they are ignored) */
	const char *err;
	CHECK(new_plugin("example.com/a", "foo=bar,,", &err) != NULL);
}

TESTS(TEST(test_go_camel_case), TEST(test_go_sanitized), TEST(test_trim_enum_prefix), TEST(test_format_float),
      TEST(test_tag_marshal), TEST(test_features_defaults), TEST(test_features_merge_and_wire), TEST(test_decode_required),
      TEST(test_protogen_names), TEST(test_protogen_import_paths), TEST(test_protogen_errors))
