/* Unit tests of the 33.4 version of libprotoc (libprotoc_33.4.c), the SCC analyzer (scc.c) and
 * Printer::Annotate of the legacy Print API. Expected values come from protobuf v33.4
 * (cpp_edition_defaults.h) and from compiler/scc.h. */
#include "libprotoc.c"
#include "libprotoc_33.4.c"

#include "test.h"

static void test_builtin_pool_33_4(void)
{
	LpPool *b = lp_builtin_pool(&lp_version_33_4);
	const LpMessage *fs = lp_find_message(b, "google.protobuf.FeatureSet");
	CHECK(fs && proto_message_field_by_name(fs, "default_symbol_visibility") &&
	      !proto_message_field_by_name(fs, "enforce_proto_limits"));
	const LpEnum *edition = proto_pool_find_enum(&b->base, "google.protobuf.Edition");
	CHECK(edition && proto_enum_value_by_name(edition, "EDITION_2024") &&
	      !proto_enum_value_by_name(edition, "EDITION_2026"));
	CHECK(!lp_find_message(b, "pb.CSharpFeatures"));
	CHECK(lp_find_message(b, "google.protobuf.compiler.CodeGeneratorRequest") != NULL);
	const char *err = NULL;
	PbMsg *d = lp_compile_defaults(b, NULL, LP_EDITION_PROTO2, lp_version_33_4.maximum_known_edition, &err);
	CHECK(d != NULL);
	if (!d)
		return;
	Str s = lp_serialize_str(d);
	/* PROTOBUF_INTERNAL_CPP_EDITION_DEFAULTS of v33.4 without its pb.cpp features */
	static const char want[] =
		"\x0a\x17\x18\x84\x07\x22\x00\x2a\x10\x08\x01\x10\x02\x18\x02\x20\x03\x28\x01\x30\x02\x38\x02\x40\x01"
		"\x0a\x17\x18\xe7\x07\x22\x00\x2a\x10\x08\x02\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x38\x02\x40\x01"
		"\x0a\x17\x18\xe8\x07\x22\x0c\x08\x01\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x2a\x04\x38\x02\x40\x01"
		"\x0a\x17\x18\xe9\x07\x22\x10\x08\x01\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x38\x01\x40\x02\x2a\x00"
		"\x20\xe6\x07\x28\xe9\x07";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
}

static LpPool *pool_33_4(void)
{
	LpPool *builtin = lp_builtin_pool(&lp_version_33_4);
	const char *err;
	PbMsg *d = lp_compile_defaults(builtin, NULL, LP_EDITION_PROTO2, LP_EDITION_2024, &err);
	return lp_pool_new(builtin, d, &lp_msgdef_by_name(builtin, "google.protobuf.FeatureSet")->pb);
}

static PbMsg *new_file(LpPool *pool, const char *name, const char *syntax)
{
	PbMsg *f = pb_new(&lp_msgdef_by_name(pool->builtin, "google.protobuf.FileDescriptorProto")->pb);
	proto_msg_set_cs(f, FDP_NAME, name);
	proto_msg_set_cs(f, FDP_PACKAGE, "p");
	proto_msg_set_cs(f, FDP_SYNTAX, syntax);
	return f;
}

static PbMsg *add_message(PbMsg *parent, uint32_t num, const char *name)
{
	PbMsg *m = proto_msg_add(parent, num);
	proto_msg_set_cs(m, DP_NAME, name);
	return m;
}

static void add_field(PbMsg *m, const char *name, int number, const char *type_name)
{
	PbMsg *f = proto_msg_add(m, DP_FIELD);
	proto_msg_set_cs(f, FD_NAME, name);
	proto_msg_set_u(f, FD_NUMBER, (uint64_t)number);
	proto_msg_set_u(f, FD_LABEL, LABEL_OPTIONAL);
	proto_msg_set_u(f, FD_TYPE, PT_MESSAGE);
	proto_msg_set_cs(f, FD_TYPE_NAME, type_name);
}

static void test_editions_33_4(void)
{
	/* edition 2024 is the last one */
	LpPool *pool = pool_33_4();
	PbMsg *f = new_file(pool, "a.proto", "editions");
	proto_msg_set_u(f, FDP_EDITION, LP_EDITION_2024);
	Buf log = {0};
	CHECK(lp_build_file(pool, f, &log) != NULL);
	/* java_multiple_files is an error of its own in 2024 */
	f = new_file(pool, "b.proto", "editions");
	proto_msg_set_u(f, FDP_EDITION, LP_EDITION_2024);
	proto_msg_set_u(proto_msg_mutable(f, FDP_OPTIONS), FILEOPT_JAVA_MULTIPLE_FILES, 1);
	log.n = 0;
	CHECK(lp_build_file(pool, f, &log) == NULL);
	CHECK(strstr(buf_str(&log), "The file option `java_multiple_files` is not supported in editions 2024") != NULL);
	/* a feature before its edition, with the texts of 33.4 */
	f = new_file(pool, "c.proto", "editions");
	proto_msg_set_u(f, FDP_EDITION, LP_EDITION_2023);
	proto_msg_set_u(proto_msg_mutable(proto_msg_mutable(f, FDP_OPTIONS), FILEOPT_FEATURES),
			PROTO_FEATURE_DEFAULT_SYMBOL_VISIBILITY, 2);
	log.n = 0;
	CHECK(lp_build_file(pool, f, &log) == NULL);
	CHECK(strstr(buf_str(&log), "Feature google.protobuf.FeatureSet.default_symbol_visibility wasn't introduced "
				    "until edition 2024 and can't be used in edition 2023") != NULL);
}

static void test_scc_deps(const LpMessage *desc, Vec *out, void *arg)
{
	(void)arg;
	for (int i = 0; i < desc->n_fields; i++)
		if (desc->fields[i].message_type)
			vec_push(out, desc->fields[i].message_type);
	for (int i = 0; i < desc->n_nested; i++)
		vec_push(out, &desc->nested[i]);
	if (desc->containing_type)
		vec_push(out, desc->containing_type);
}

static void test_scc(void)
{
	LpPool *pool = pool_33_4();
	PbMsg *f = new_file(pool, "s.proto", "proto3");
	PbMsg *c = add_message(f, FDP_MESSAGE_TYPE, "C");
	PbMsg *b = add_message(f, FDP_MESSAGE_TYPE, "B");
	PbMsg *a = add_message(f, FDP_MESSAGE_TYPE, "A");
	PbMsg *d = add_message(f, FDP_MESSAGE_TYPE, "D");
	add_field(c, "b", 1, ".p.B");
	add_field(b, "a", 1, ".p.A");
	add_field(a, "b", 1, ".p.B");
	PbMsg *e = add_message(d, DP_NESTED_TYPE, "E");
	add_field(e, "d", 1, ".p.D");
	Buf log = {0};
	LpFile *file = lp_build_file(pool, f, &log);
	CHECK(file != NULL);
	if (!file)
		return;
	LpSccAnalyzer *an = lp_scc_analyzer_new(test_scc_deps, NULL);
	const LpScc *sc = lp_scc_get(an, &file->messages[0]);
	CHECK(sc->n == 1 && sc->n_children == 1);
	const LpScc *sab = lp_scc_get(an, &file->messages[1]);
	CHECK(sab == sc->children[0] && sab == lp_scc_get(an, &file->messages[2]));
	CHECK(sab->n == 2 && str_eq(lp_scc_representative(sab)->full_name, "p.A") &&
	      str_eq(sab->descriptors[1]->full_name, "p.B") && sab->n_children == 0);
	const LpScc *sd = lp_scc_get(an, &file->messages[3]);
	CHECK(sd->n == 2 && str_eq(sd->descriptors[0]->full_name, "p.D") && str_eq(sd->descriptors[1]->full_name, "p.D.E"));
}

static void test_annotate(void)
{
	LpPool *pool = pool_33_4();
	PbMsg *f = new_file(pool, "an.proto", "proto3");
	add_message(f, FDP_MESSAGE_TYPE, "M");
	Buf log = {0};
	LpFile *file = lp_build_file(pool, f, &log);
	CHECK(file != NULL);
	if (!file)
		return;
	Buf out = {0}, annotations = {0};
	LpPrinter p;
	lp_printer_init(&p, &out, '$');
	p.annotations = &annotations;
	lp_print(&p, "x\n  $a$ = $b$($a$)", "a", "AA", "b", "BBB", NULL);
	lp_annotate(&p, "a", &file->messages[0]); /* used twice: nothing */
	lp_annotate(&p, "b", &file->messages[0]);
	lp_annotate(&p, "c", &file->messages[0]); /* not substituted */
	CHECK_STR(buf_str(&out), "x\n  AA = BBB(AA)");
	/* GeneratedCodeInfo.annotation {path: [4, 0], source_file: "an.proto", begin: 9, end: 12} */
	static const char want[] = "\x0a\x12\x0a\x02\x04\x00\x12\x08" "an.proto" "\x18\x09\x20\x0c";
	CHECK_MEM(annotations.p, annotations.n, want, sizeof want - 1);
	/* the next Print starts a new substitution map */
	lp_print(&p, "$x$", "x", "", NULL);
	lp_annotate(&p, "b", &file->messages[0]);
	CHECK(annotations.n == sizeof want - 1);
}

TESTS(TEST(test_builtin_pool_33_4), TEST(test_editions_33_4), TEST(test_scc), TEST(test_annotate))
