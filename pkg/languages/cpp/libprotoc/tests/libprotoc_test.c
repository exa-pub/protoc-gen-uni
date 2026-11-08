/* Unit tests of libprotoc 36.2. Expected values come from the upstream runtime (absl::CEscape,
 * absl::CHexEscape, io::SimpleDtoa/SimpleFtoa as protoc 36.2 prints default values, glibc's
 * printf("%.*g")) and from protoc's own behavior. */
#include "libprotoc.c"

#include "test.h"

static void test_cescape(void)
{
	CHECK_STR(lp_cescape(STR_LIT("a\n\"'\\\x01\x7f\xff")), "a\\n\\\"\\'\\\\\\001\\177\\377");
	CHECK_STR(lp_cescape(STR_LIT("\x01" "1")), "\\0011");
	/* Utf8SafeCEscape: bytes >= 0x80 pass through */
	CHECK_STR(lp_utf8_safe_cescape(STR_LIT("a/\xc3\xa9\n\x01\x7f")), "a/\xc3\xa9\\n\\001\\177");
	/* a hex digit after a hex escape is escaped too */
	CHECK_STR(lp_chexescape(STR_LIT("\x08" "f_x\x12\x19\n\x08" "\x66" "D")), "\\x08\\x66_x\\x12\\x19\\n\\x08\\x66\\x44");
	CHECK_STR(lp_chexescape(STR_LIT("\x83" "g")), "\\x83g");
}

static void test_cunescape(void)
{
	Str out;
	CHECK(lp_cunescape(STR_LIT("a\\001\\x41\\n\\u00e9\\'"), &out));
	CHECK_MEM(out.p, out.n, "a\001A\n\xc3\xa9'", 7);
	CHECK(lp_cunescape(STR_LIT("\\0"), &out) && out.n == 1 && out.p[0] == 0);
	CHECK(!lp_cunescape(STR_LIT("\\"), &out));
	CHECK(!lp_cunescape(STR_LIT("\\x"), &out));
	CHECK(!lp_cunescape(STR_LIT("\\x100"), &out));
	CHECK(!lp_cunescape(STR_LIT("\\400"), &out));
	CHECK(!lp_cunescape(STR_LIT("\\q"), &out));
	CHECK(!lp_cunescape(STR_LIT("\\ud800"), &out));
	CHECK(!lp_cunescape(STR_LIT("\\U00110000"), &out));
}

static void test_format_g(void)
{
	CHECK_STR(lp_format_g(0.1, 15), "0.1");
	CHECK_STR(lp_format_g(0.1, 17), "0.10000000000000001");
	CHECK_STR(lp_format_g(1e20, 15), "1e+20");
	CHECK_STR(lp_format_g(123456789012345.0, 15), "123456789012345");
	CHECK_STR(lp_format_g(1234567890123456.0, 15), "1.23456789012346e+15");
	CHECK_STR(lp_format_g(0.0001, 15), "0.0001");
	CHECK_STR(lp_format_g(0.00001, 15), "1e-05");
	CHECK_STR(lp_format_g(-0.0, 15), "-0");
	CHECK_STR(lp_format_g(0.5, 0), "0.5");
	CHECK_STR(lp_format_g(2.5, 1), "2");   /* ties to even */
	CHECK_STR(lp_format_g(3.5, 1), "4");
	CHECK_STR(lp_format_g(9.99999, 3), "10");
	CHECK_STR(lp_format_g(5e-324, 17), "4.9406564584124654e-324");
	CHECK_STR(lp_format_g(1.7976931348623157e308, 17), "1.7976931348623157e+308");
}

static void test_simple_dtoa(void)
{
	CHECK_STR(lp_simple_dtoa(0.1), "0.1");
	CHECK_STR(lp_simple_dtoa(1.0 / 3), "0.33333333333333331");
	CHECK_STR(lp_simple_dtoa(-1.5e-300), "-1.5e-300");
	CHECK_STR(lp_simple_dtoa(1e300), "1e+300");
	CHECK_STR(lp_simple_dtoa(3.14), "3.14");
	CHECK_STR(lp_simple_dtoa(1e15), "1e+15");
	CHECK_STR(lp_simple_dtoa(123456.0), "123456");
	CHECK_STR(lp_simple_dtoa(INFINITY), "inf");
	CHECK_STR(lp_simple_dtoa(-INFINITY), "-inf");
	CHECK_STR(lp_simple_dtoa(NAN), "nan");
	CHECK_STR(lp_simple_dtoa(5e-324), "4.94065645841247e-324");
	CHECK_STR(lp_simple_dtoa(1.7976931348623157e308), "1.7976931348623157e+308");
	CHECK_STR(lp_simple_ftoa(0.1f), "0.1");
	CHECK_STR(lp_simple_ftoa(1.0f / 3), "0.333333343");
	CHECK_STR(lp_simple_ftoa(3.4028235e38f), "3.40282347e+38");
	CHECK_STR(lp_simple_ftoa(1.5f), "1.5");
	CHECK_STR(lp_simple_ftoa(1e-45f), "1.40129846e-45");
}

/* absl::StrCat(double): SixDigitsToBuffer */
static void test_six_digits(void)
{
	CHECK_STR(lp_six_digits(0.0), "0");
	CHECK_STR(lp_six_digits(-0.0), "-0");
	CHECK_STR(lp_six_digits(1.5), "1.5");
	CHECK_STR(lp_six_digits(3.14159265358979), "3.14159");
	CHECK_STR(lp_six_digits(123456789), "1.23457e+08");
	CHECK_STR(lp_six_digits(123456), "123456");
	CHECK_STR(lp_six_digits(12345.6), "12345.6");
	CHECK_STR(lp_six_digits(1234.05), "1234.05");
	CHECK_STR(lp_six_digits(100000.5), "100000");  /* ties to even */
	CHECK_STR(lp_six_digits(100001.5), "100002");
	CHECK_STR(lp_six_digits(999999.5), "1e+06");
	CHECK_STR(lp_six_digits(0.0001), "0.0001");
	CHECK_STR(lp_six_digits(0.00001234), "1.234e-05");
	CHECK_STR(lp_six_digits(-1.5e-300), "-1.5e-300");
	CHECK_STR(lp_six_digits(1.7976931348623157e308), "1.79769e+308");
	CHECK_STR(lp_six_digits(5e-324), "4.94066e-324");
	CHECK_STR(lp_six_digits(INFINITY), "inf");
	CHECK_STR(lp_six_digits(-INFINITY), "-inf");
	CHECK_STR(lp_six_digits(-NAN), "nan");
}

static void test_no_locale_strtod(void)
{
	const char *end;
	CHECK(lp_no_locale_strtod("1.5e3", &end) == 1500 && *end == 0);
	CHECK(lp_no_locale_strtod("-.5", &end) == -0.5 && *end == 0);
	lp_no_locale_strtod("+1", &end);
	CHECK(*end == '+');
	lp_no_locale_strtod("0x10", &end);
	CHECK(*end == 'x');
	lp_no_locale_strtod("1e", &end);
	CHECK(*end == 'e');
	CHECK(isinf(lp_no_locale_strtod("Infinity", &end)) && *end == 0);
	CHECK(isinf(lp_no_locale_strtod("1e999", &end)) && *end == 0);
}

static void test_printer(void)
{
	Buf out = {0};
	LpPrinter p;
	lp_printer_init(&p, &out, '$');
	lp_print(&p, "module $name$\n", "name", "Foo", NULL);
	lp_indent(&p);
	lp_print(&p, "$a$$b$ = $$x\n  nested\n\n", "a", "", "b", "B", NULL);
	lp_print(&p, "$empty$\nX\n", "empty", "", NULL);
	lp_outdent(&p);
	lp_print(&p, "end\n", NULL);
	CHECK_STR(buf_str(&out), "module Foo\n  B = $x\n    nested\n\n\n  X\nend\n");
}

/* io::Printer::Emit: cases of printer_unittest.cc (v36.2) with their expected output. */
static void emit_preprocessor_value(LpPrinter *p, void *arg)
{
	lp_emit(p, NULL, 0,
		"\n"
		"#if FOO\n"
		"                       0,\n"
		"#else\n"
		"                       1,\n"
		"#endif\n"
		"                     ");
}

static void emit_preprocessor_on_new_line(LpPrinter *p, void *arg)
{
	lp_emit(p, NULL, 0,
		"\n"
		"#pragma foo\n"
		"                     ");
}

static void test_printer_emit(void)
{
	Buf out = {0};
	LpPrinter p;
	/* Emit */
	lp_printer_init(&p, &out, '$');
	lp_emit(&p, NULL, 0, "\n      class Foo {\n        int x, y, z;\n      };\n    ");
	lp_emit(&p, NULL, 0, "\n      public final class Bar {\n        Bar() {}\n      }\n    ");
	CHECK_STR(buf_str(&out), "class Foo {\n  int x, y, z;\n};\npublic final class Bar {\n  Bar() {}\n}\n");
	/* EmitWithSubs */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	LpSub subs[] = {LP_SUB("class", "Foo"), LP_SUB("f1", "x"), LP_SUB("f2", "y"), LP_SUB("f3", "z"), LP_SUB("init", "42")};
	lp_emit(&p, subs, 5,
		"\n          class $class$ {\n            int $f1$, $f2$, $f3$ = $init$;\n          };\n        ");
	CHECK_STR(buf_str(&out), "class Foo {\n  int x, y, z = 42;\n};\n");
	/* EmitComments */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	lp_emit(&p, NULL, 0, "\n      // Yes.\n      //~ No.\n    ");
	lp_emit(&p, NULL, 0, "//~ Not a raw string.");
	CHECK_STR(buf_str(&out), "// Yes.\n//~ Not a raw string.");
	/* EmitConsumeAfter */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	LpSub consume[] = {LP_SUB("class", "Foo"), LP_SUB("var", "int x;")};
	consume[1].consume_after = ";";
	lp_emit(&p, consume, 2, "\n          class $class$ {\n            $var$;\n          };\n        ");
	CHECK_STR(buf_str(&out), "class Foo {\n  int x;\n};\n");
	/* EmitConditionalFunctionCall */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	LpSub casts[] = {LP_SUB("weak_cast", ""), LP_SUB("strong_cast", "static_cast<void*>")};
	casts[0].consume_parens_if_empty = casts[1].consume_parens_if_empty = true;
	lp_emit(&p, casts, 2,
		"\n"
		"          $weak_cast$(weak);\n"
		"          $weak_cast$(weak + (1234 * 89) + zomg);\n"
		"          $strong_cast$(strong);\n"
		"          $weak_cast$($strong_cast$($weak_cast$(1 + 2)));\n"
		"          $weak_cast$(boy_this_expression_got_really_long +\n"
		"                      what_kind_of_monster_does_this);\n"
		"        ");
	CHECK_STR(buf_str(&out), "weak;\nweak + (1234 * 89) + zomg;\nstatic_cast<void*>(strong);\nstatic_cast<void*>(1 + 2);\n"
				 "boy_this_expression_got_really_long +\n            what_kind_of_monster_does_this;\n");
	/* EmitWithSpacedVars (WithVars) */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	LpSub vars[] = {LP_SUB("is_final", "final"), LP_SUB("isnt_final", ""), LP_SUB("class", "Foo")};
	lp_push_vars(&p, vars, 3);
	lp_emit(&p, NULL, 0, "\n      public $is_final $class $class$ {\n        // Stuff.\n      }\n    ");
	lp_emit(&p, NULL, 0, "\n      public $isnt_final $class $class$ {\n        // Stuff.\n      }\n    ");
	CHECK_STR(lp_lookup_var(&p, "class"), "Foo");
	lp_pop_vars(&p);
	CHECK_STR(buf_str(&out), "public final class Foo {\n  // Stuff.\n}\npublic class Foo {\n  // Stuff.\n}\n");
	/* EmitWithIndentAndIgnoredCommentOnFirstLine */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	lp_indent(&p);
	LpSub f[] = {LP_SUB("f1", "x"), LP_SUB("f2", "y"), LP_SUB("f3", "z")};
	lp_emit(&p, f, 3,
		"\n      //~ First line comment.\n      class Foo {\n        int $f1$, $f2$, $f3$;\n      };\n    ");
	CHECK_STR(buf_str(&out), "  class Foo {\n    int x, y, z;\n  };\n");
	/* EmitWithCPPDirectiveOnFirstLine */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	lp_emit(&p, f, 3,
		"\n#if NDEBUG\n#pragma foo\n      class Foo {\n        int $f1$, $f2$, $f3$;\n      };\n#endif\n    ");
	CHECK_STR(buf_str(&out), "#if NDEBUG\n#pragma foo\nclass Foo {\n  int x, y, z;\n};\n#endif\n");
	/* EmitWithPreprocessor (callbacks) */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	lp_indent(&p);
	LpSub cbs[] = {LP_SUB_CB("value", emit_preprocessor_value, NULL),
		       LP_SUB_CB("on_new_line", emit_preprocessor_on_new_line, NULL)};
	lp_emit(&p, cbs, 2, "\n                   int val = ($value$, 0);\n                   $on_new_line$;\n                 ");
	CHECK_STR(buf_str(&out), "  int val = (\n  #if FOO\n  0,\n  #else\n  1,\n  #endif\n   0);\n  #pragma foo\n");
	/* Print(vars, text) over WithVars frames: $ var$ spaces, a value ending in a newline does
	 * not indent the rest of its line, undefined variables are skipped */
	out.n = 0;
	lp_printer_init(&p, &out, '$');
	LpSub outer[] = {LP_SUB("jvm_synthetic", "@X\n"), LP_SUB("empty", "")};
	lp_push_vars(&p, outer, 2);
	lp_indent(&p);
	LpSub args[] = {LP_SUB("message", "M")};
	lp_print_subs(&p, args, 1, "a $message$\n$  jvm_synthetic$  get()\n$  empty$x$undefined$b\n");
	lp_pop_vars(&p);
	CHECK_STR(buf_str(&out), "  a M\n    @X\n  get()\n  xb\n");
}

static void test_parameter(void)
{
	char **k, **v;
	int n = lp_parse_generator_parameter(",a=1=2,,b,", &k, &v);
	CHECK(n == 2);
	CHECK_STR(k[0], "a");
	CHECK_STR(v[0], "1=2");
	CHECK_STR(k[1], "b");
	CHECK_STR(v[1], "");
}

/* The generated pool builds, and the FeatureSetDefaults compiled for a generator without feature
 * extensions match protoc's (FeatureResolver::CompileDefaults(PROTO2, 2026)). */
static void test_builtin_pool(void)
{
	LpPool *b = lp_builtin_pool(NULL);
	CHECK(lp_find_message(b, "google.protobuf.compiler.CodeGeneratorRequest") != NULL);
	const char *err;
	PbMsg *d = lp_compile_defaults(b, NULL, LP_EDITION_PROTO2, LP_EDITION_2026, &err);
	CHECK(d != NULL);
	Str s = lp_serialize_str(d);
	/* FeatureSetDefaults of the FeatureSet edition_defaults of descriptor.proto v36.2 */
	static const char want[] =
		"\x0a\x19\x18\x84\x07\x22\x00\x2a\x12\x08\x01\x10\x02\x18\x02\x20\x03\x28\x01\x30\x02\x38\x02\x40\x01\x48\x01"
		"\x0a\x19\x18\xe7\x07\x22\x00\x2a\x12\x08\x02\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x38\x02\x40\x01\x48\x01"
		"\x0a\x19\x18\xe8\x07\x22\x0c\x08\x01\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x2a\x06\x38\x02\x40\x01\x48\x01"
		"\x0a\x19\x18\xe9\x07\x22\x10\x08\x01\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x38\x01\x40\x02\x2a\x02\x48\x01"
		"\x0a\x19\x18\xea\x07\x22\x12\x08\x01\x10\x01\x18\x01\x20\x02\x28\x01\x30\x01\x38\x03\x40\x04\x48\x02\x2a\x00"
		"\x20\xe6\x07\x28\xea\x07";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
}


static void test_base64(void)
{
	CHECK_STR(lp_base64_escape(STR_LIT("")), "");
	CHECK_STR(lp_base64_escape(STR_LIT("f")), "Zg==");
	CHECK_STR(lp_base64_escape(STR_LIT("fo")), "Zm8=");
	CHECK_STR(lp_base64_escape(STR_LIT("foo")), "Zm9v");
	CHECK_STR(lp_base64_escape(STR_LIT("\xff\xfe")), "//4=");
}

/* ---- DescriptorBuilder errors (texts of descriptor.cc) ---- */

static LpPool *test_pool(void)
{
	LpPool *builtin = lp_builtin_pool(NULL);
	const char *err;
	PbMsg *d = lp_compile_defaults(builtin, NULL, LP_EDITION_PROTO2, LP_EDITION_2026, &err);
	return lp_pool_new(builtin, d, &lp_msgdef_by_name(builtin, "google.protobuf.FeatureSet")->pb);
}

static PbMsg *new_file(LpPool *pool, const char *name, const char *package, const char *syntax)
{
	PbMsg *f = pb_new(&lp_msgdef_by_name(pool->builtin, "google.protobuf.FileDescriptorProto")->pb);
	proto_msg_set_cs(f, FDP_NAME, name);
	if (package)
		proto_msg_set_cs(f, FDP_PACKAGE, package);
	if (syntax)
		proto_msg_set_cs(f, FDP_SYNTAX, syntax);
	return f;
}

static PbMsg *add_message(PbMsg *parent, uint32_t num, const char *name)
{
	PbMsg *m = proto_msg_add(parent, num);
	proto_msg_set_cs(m, DP_NAME, name);
	return m;
}

static PbMsg *add_field(PbMsg *m, const char *name, int number, int label, int type, const char *type_name)
{
	PbMsg *f = proto_msg_add(m, DP_FIELD);
	proto_msg_set_cs(f, FD_NAME, name);
	proto_msg_set_u(f, FD_NUMBER, (uint64_t)number);
	proto_msg_set_u(f, FD_LABEL, (uint64_t)label);
	if (type)
		proto_msg_set_u(f, FD_TYPE, (uint64_t)type);
	if (type_name)
		proto_msg_set_cs(f, FD_TYPE_NAME, type_name);
	return f;
}

static PbMsg *add_enum(PbMsg *parent, uint32_t num, const char *name, const char *v0, int n0, const char *v1, int n1)
{
	PbMsg *e = proto_msg_add(parent, num);
	proto_msg_set_cs(e, EDP_NAME, name);
	PbMsg *v = proto_msg_add(e, EDP_VALUE);
	proto_msg_set_cs(v, EVDP_NAME, v0);
	proto_msg_set_u(v, EVDP_NUMBER, (uint64_t)(int64_t)n0);
	if (v1) {
		v = proto_msg_add(e, EDP_VALUE);
		proto_msg_set_cs(v, EVDP_NAME, v1);
		proto_msg_set_u(v, EVDP_NUMBER, (uint64_t)(int64_t)n1);
	}
	return e;
}

static const char *build_log(LpPool *pool, PbMsg *f)
{
	Buf log = {0};
	LpFile *r = lp_build_file(pool, f, &log);
	return r ? "OK" : buf_str(&log);
}

static void test_builder_errors(void)
{
	LpPool *pool = test_pool();
	PbMsg *f = new_file(pool, "a.proto", "pkg", NULL);
	PbMsg *m = add_message(f, FDP_MESSAGE_TYPE, "M");
	add_field(m, "f", 1, LABEL_OPTIONAL, 0, "Foo");
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n  pkg.M.f: \"Foo\" is not defined.\n");

	pool = test_pool();
	f = new_file(pool, "a.proto", "pkg", NULL);
	add_message(f, FDP_MESSAGE_TYPE, "M");
	add_message(f, FDP_MESSAGE_TYPE, "M");
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n  pkg.M: \"M\" is already defined in \"pkg\".\n");

	pool = test_pool();
	f = new_file(pool, "a.proto", "pkg", NULL);
	m = add_message(f, FDP_MESSAGE_TYPE, "M");
	add_field(m, "a", 1, LABEL_OPTIONAL, PT_INT32, NULL);
	add_field(m, "b", 1, LABEL_OPTIONAL, PT_INT32, NULL);
	add_field(m, "c", 2, LABEL_OPTIONAL, PT_INT32, NULL);
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n"
				      "  pkg.M.b: Field number 1 has already been used in \"pkg.M\" by field \"a\". Next available "
				      "field number is 3.\n");

	pool = test_pool();
	f = new_file(pool, "a.proto", "pkg", NULL);
	proto_msg_add_s(f, FDP_DEPENDENCY, STR_LIT("b.proto"));
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n  b.proto: Import \"b.proto\" has not been loaded.\n");

	/* a closed enum in a proto3 message; an undeclared dependency */
	pool = test_pool();
	f = new_file(pool, "e.proto", "pkg", NULL);
	add_enum(f, FDP_ENUM_TYPE, "E", "E_A", 0, NULL, 0);
	CHECK_STR(build_log(pool, f), "OK");
	PbMsg *g = new_file(pool, "g.proto", "pkg3", "proto3");
	proto_msg_add_s(g, FDP_DEPENDENCY, STR_LIT("e.proto"));
	m = add_message(g, FDP_MESSAGE_TYPE, "M");
	add_field(m, "e", 1, LABEL_OPTIONAL, PT_ENUM, ".pkg.E");
	CHECK_STR(build_log(pool, g), "Invalid proto descriptor for file \"g.proto\":\n"
				      "  pkg3.M.e: Enum type \"pkg.E\" is not an open enum, but is used in \"pkg3.M\" which is a "
				      "proto3 message type.\n");
	PbMsg *h = new_file(pool, "h.proto", "pkg", NULL);
	m = add_message(h, FDP_MESSAGE_TYPE, "N");
	add_field(m, "e", 1, LABEL_OPTIONAL, PT_ENUM, "E");
	CHECK_STR(build_log(pool, h), "Invalid proto descriptor for file \"h.proto\":\n"
				      "  pkg.N.e: \"pkg.E\" seems to be defined in \"e.proto\", which is not imported by \"h.proto\".  "
				      "To use it here, please add the necessary import.\n");

	/* naming/legacy.proto of testdata, as protoc reports it */
	pool = test_pool();
	f = new_file(pool, "a.proto", "test.naming", "proto3");
	add_enum(f, FDP_ENUM_TYPE, "Dupe", "DUPE_A", 0, "DUPE__A", 1);
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n"
				      "  test.naming.DUPE__A: Enum name DUPE__A has the same name as DUPE_A if you ignore case and "
				      "strip out the enum name prefix (if any). (If you are using allow_alias, please assign the "
				      "same number to each enum value name.)\n");

	/* enum values are siblings of their type */
	pool = test_pool();
	f = new_file(pool, "a.proto", "pkg", "proto3");
	add_enum(f, FDP_ENUM_TYPE, "A", "X", 0, NULL, 0);
	add_enum(f, FDP_ENUM_TYPE, "B", "X", 0, NULL, 0);
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n"
				      "  pkg.X: \"X\" is already defined in \"pkg\".\n"
				      "  pkg.X: Note that enum values use C++ scoping rules, meaning that enum values are siblings "
				      "of their type, not children of it.  Therefore, \"X\" must be unique within \"pkg\", not just "
				      "within \"B\".\n");
}

/* AddPackage without the package limits (legacy_descriptor_builder, 3.19.1): the order of its
 * errors (the first component first, as upstream unwinds its recursion), no recursion on a deep
 * package, and the "unsupported" error over 1 MiB of prefixes. */
static LpPool *legacy_pool(void)
{
	static const LpVersion legacy = {.name = "legacy", .legacy_descriptor_builder = true};
	LpPool *pool = test_pool();
	pool->version = &legacy;
	return pool;
}

static char *repeat_package(int levels)
{
	char *s = xalloc((size_t)levels * 2);
	for (int i = 0; i < levels; i++) {
		s[2 * i] = 'a';
		s[2 * i + 1] = i + 1 < levels ? '.' : 0;
	}
	return s;
}

static void test_legacy_package(void)
{
	LpPool *pool = legacy_pool();
	CHECK_STR(build_log(pool, new_file(pool, "a.proto", "x-y.a..b-c", NULL)),
		  "Invalid proto descriptor for file \"a.proto\":\n"
		  "  x-y: \"x-y\" is not a valid identifier.\n"
		  "  x-y.a.: Missing name.\n"
		  "  x-y.a..b-c: \"b-c\" is not a valid identifier.\n");
	pool = legacy_pool();
	PbMsg *b = new_file(pool, "b.proto", NULL, NULL);
	add_message(b, FDP_MESSAGE_TYPE, "q");
	CHECK_STR(build_log(pool, b), "OK");
	PbMsg *f = new_file(pool, "a.proto", "q.r.s", NULL);
	add_field(add_message(f, FDP_MESSAGE_TYPE, "M"), "f", 1, LABEL_OPTIONAL, 0, "M");
	CHECK_STR(build_log(pool, f),
		  "Invalid proto descriptor for file \"a.proto\":\n"
		  "  q: \"q\" is already defined (as something other than a package) in file \"b.proto\".\n");
	CHECK(lp_find_symbol(pool, "q.r") && lp_find_symbol(pool, "q.r")->kind == PROTO_SYM_SUB_PACKAGE);
	/* 36.2 limits 100 dots; 700 levels take 490 KB of prefixes */
	pool = legacy_pool();
	f = new_file(pool, "a.proto", repeat_package(700), "proto3");
	add_field(add_message(f, FDP_MESSAGE_TYPE, "M"), "f", 1, LABEL_OPTIONAL, 0, "M");
	CHECK_STR(build_log(pool, f), "OK");
	CHECK(lp_find_symbol(pool, "a") && lp_find_symbol(pool, "a")->kind == PROTO_SYM_SUB_PACKAGE);
	pool = test_pool();
	CHECK(strstr(build_log(pool, new_file(pool, "a.proto", repeat_package(700), "proto3")),
		     ": Package name is too long\n") != NULL);
	/* 50,000 levels (the depth that crashed the recursion on a 1 MiB stack) */
	pool = legacy_pool();
	CHECK_STR(build_log(pool, new_file(pool, "a.proto", repeat_package(50000), NULL)),
		  "unsupported by protoc-gen-uni: the package of \"a.proto\" has 49999 dots in 99999 bytes (its "
		  "prefixes would take over 1 MiB; libprotoc 3.21 rejects over 100 dots or 511 bytes)\n");
}

/* The OptionInterpreter is not ported: uninterpreted_option fails the build and is named. */
static void test_uninterpreted_option(void)
{
	LpPool *pool = test_pool();
	PbMsg *f = new_file(pool, "a.proto", "pkg", "proto3");
	PbMsg *m = add_message(f, FDP_MESSAGE_TYPE, "M");
	PbMsg *fd = add_field(m, "f", 1, LABEL_OPTIONAL, PT_INT32, NULL);
	PbMsg *uo = proto_msg_add(proto_msg_mutable(fd, FD_OPTIONS), OPT_UNINTERPRETED);
	proto_msg_set_cs(uo, 8, "x"); /* aggregate_value */
	CHECK_STR(build_log(pool, f), "unsupported by protoc-gen-uni: uninterpreted_option in the options of pkg.M.f in "
				      "\"a.proto\" (the option interpreter of protoc is not ported; protoc and buf send "
				      "interpreted options)\n");
	/* earlier errors win, like upstream skips interpretation after errors */
	pool = test_pool();
	f = new_file(pool, "a.proto", "pkg", "proto3");
	proto_msg_add(proto_msg_mutable(f, FDP_OPTIONS), OPT_UNINTERPRETED);
	m = add_message(f, FDP_MESSAGE_TYPE, "M");
	add_field(m, "f", 1, LABEL_OPTIONAL, 0, "Foo");
	CHECK_STR(build_log(pool, f), "Invalid proto descriptor for file \"a.proto\":\n  pkg.M.f: \"Foo\" is not defined.\n");
}

/* Map entries keep their order and duplicates, and always write key and value (wire_format.cc:
 * "Map entry fields always need to be serialized"), also with implicit presence. */
static void test_map_entry(void)
{
	LpPool *pool = test_pool();
	PbMsg *f = new_file(pool, "m.proto", "pkg", "proto3");
	PbMsg *m = add_message(f, FDP_MESSAGE_TYPE, "M");
	add_field(m, "m", 1, LABEL_REPEATED, PT_MESSAGE, ".pkg.M.MEntry");
	add_field(m, "n", 2, LABEL_REPEATED, PT_MESSAGE, ".pkg.M.NEntry");
	PbMsg *e = add_message(m, DP_NESTED_TYPE, "MEntry");
	add_field(e, "key", 1, LABEL_OPTIONAL, PT_STRING, NULL);
	add_field(e, "value", 2, LABEL_OPTIONAL, PT_SINT32, NULL);
	proto_msg_set_u(proto_msg_mutable(e, DP_OPTIONS), MSGOPT_MAP_ENTRY, 1);
	e = add_message(m, DP_NESTED_TYPE, "NEntry");
	add_field(e, "key", 1, LABEL_OPTIONAL, PT_FIXED32, NULL);
	add_field(e, "value", 2, LABEL_OPTIONAL, PT_MESSAGE, ".pkg.M");
	proto_msg_set_u(proto_msg_mutable(e, DP_OPTIONS), MSGOPT_MAP_ENTRY, 1);
	CHECK_STR(build_log(pool, f), "OK");
	PbMsg *v = pb_new(&lp_msgdef_by_name(pool, "pkg.M")->pb);
	static const char in[] = "\x0a\x00" "\x12\x00" "\x0a\x05\x10\x03\x0a\x01z" "\x0a\x03\x0a\x01z" "\x0a\x02\x18\x01";
	CHECK(lp_decode(v, (const uint8_t *)in, sizeof in - 1));
	Str s = lp_serialize_str(v);
	static const char want[] = "\x0a\x04\x0a\x00\x10\x00" "\x0a\x05\x0a\x01z\x10\x03" "\x0a\x05\x0a\x01z\x10\x00"
				   "\x0a\x06\x0a\x00\x10\x00\x18\x01" "\x12\x07\x0d\x00\x00\x00\x00\x12\x00";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
}

/* CopyTo re-renders defaults, qualifies names and drops source info; json_name stays when set. */
static void test_copy_to(void)
{
	LpPool *pool = test_pool();
	PbMsg *f = new_file(pool, "a.proto", "pkg", "proto2");
	PbMsg *m = add_message(f, FDP_MESSAGE_TYPE, "M");
	PbMsg *fd = add_field(m, "d", 1, LABEL_OPTIONAL, PT_DOUBLE, NULL);
	proto_msg_set_cs(fd, FD_DEFAULT_VALUE, "1e1");
	fd = add_field(m, "b", 2, LABEL_OPTIONAL, PT_BYTES, NULL);
	proto_msg_set_cs(fd, FD_DEFAULT_VALUE, "\\x41\\001");
	add_field(m, "s", 3, LABEL_OPTIONAL, 0, "M");
	proto_msg_mutable(f, FDP_SOURCE_CODE_INFO);
	Buf log = {0};
	LpFile *file = lp_build_file(pool, f, &log);
	CHECK(file != NULL);
	if (!file)
		return;
	Str s = lp_serialize_str(lp_strip_source_retention_options(file));
	static const char want[] = "\n\x07" "a.proto\x12\x03pkg\"7\n\x01M"
				   "\x12\x0d\n\x01" "d\x18\x01 \x01(\x01:\x02" "10"
				   "\x12\x10\n\x01" "b\x18\x02 \x01(\x0c:\x05" "A\\001"
				   "\x12\x11\n\x01s\x18\x03 \x01(\x0b" "2\x06.pkg.M";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
}

/* Generated-message parsing: unknown values of closed enums and fields with another wire type
 * become unknown fields; packed input of an unpacked field is accepted; the last scalar wins. */
static void test_decode(void)
{
	LpPool *b = lp_builtin_pool(NULL);
	PbMsg *m = pb_new(&lp_msgdef_by_name(b, "google.protobuf.FieldOptions")->pb);
	static const char in[] = "\x08\x05" "\x9a\x01\x02\x01\x63" "\x10\x01" "\x10\x00" "\x1a\x01\x41";
	CHECK(lp_decode(m, (const uint8_t *)in, sizeof in - 1));
	CHECK(!pb_has(m, 1));
	CHECK(pb_count(m, 19) == 1);
	CHECK(pb_int(m, 2, 7) == 0);
	Str s = lp_serialize_str(m);
	static const char want[] = "\x10\x00" "\x98\x01\x01" "\x08\x05" "\x98\x01\x63" "\x1a\x01\x41";
	CHECK_MEM(s.p, s.n, want, sizeof want - 1);
	/* a missing required field of UninterpretedOption.NamePart */
	PbMsg *u = pb_new(&lp_msgdef_by_name(b, "google.protobuf.UninterpretedOption")->pb);
	CHECK(lp_decode(u, (const uint8_t *)"\x12\x02\x0a\x00", 4));
	CHECK(!lp_is_initialized(u));
	/* truncated, end group at the top level */
	CHECK(!lp_decode(u, (const uint8_t *)"\x12\x05\x0a", 3));
	CHECK(!lp_decode(u, (const uint8_t *)"\x0c", 1));
}

/* compiler/csharp/names.cc: namespaces, class names and output files. */
static void test_csharp_names(void)
{
	CHECK_STR(lp_csharp_underscores_to_camel_case("foo_bar.baz2x", true, true), "FooBar.Baz2X");
	CHECK_STR(lp_csharp_underscores_to_camel_case("Foo_bar", false, false), "fooBar");
	CHECK_STR(lp_csharp_underscores_to_camel_case("_2abc", true, false), "_2Abc");
	CHECK_STR(lp_csharp_underscores_to_camel_case("a#", false, false), "a_");
	LpPool *pool = test_pool();
	PbMsg *f = new_file(pool, "dir/foo_bar.v1.proto", "my_pkg.sub", "proto3");
	PbMsg *m = add_message(f, FDP_MESSAGE_TYPE, "M");
	add_message(m, DP_NESTED_TYPE, "N");
	Buf log = {0};
	LpFile *file = lp_build_file(pool, f, &log);
	CHECK(file != NULL);
	if (!file)
		return;
	CHECK_STR(lp_csharp_file_namespace(file), "MyPkg.Sub");
	CHECK_STR(lp_csharp_class_name_message(&file->messages[0].nested[0]), "global::MyPkg.Sub.M.Types.N");
	CHECK_STR(lp_csharp_reflection_class_name(file), "global::MyPkg.Sub.FooBarV1Reflection");
	CHECK_STR(lp_csharp_extension_class_unqualified_name(file), "FooBarV1Extensions");
	char *error = "";
	CHECK_STR(lp_csharp_output_file(file, ".cs", false, "", &error), "FooBarV1.cs");
	CHECK_STR(lp_csharp_output_file(file, ".cs", true, "", &error), "MyPkg/Sub/FooBarV1.cs");
	CHECK_STR(lp_csharp_output_file(file, ".g.cs", true, "MyPkg", &error), "Sub/FooBarV1.g.cs");
	CHECK_STR(lp_csharp_output_file(file, ".cs", true, "MyPkg.Sub", &error), "FooBarV1.cs");
	CHECK_STR(error, "");
	CHECK_STR(lp_csharp_output_file(file, ".cs", true, "MyPkg.S", &error), "");
	CHECK_STR(error, "Namespace MyPkg.Sub is not a prefix namespace of base namespace MyPkg.S");
}

TESTS(TEST(test_decode), TEST(test_uninterpreted_option), TEST(test_legacy_package), TEST(test_map_entry), TEST(test_cescape), TEST(test_base64), TEST(test_builder_errors), TEST(test_copy_to), TEST(test_cunescape), TEST(test_format_g), TEST(test_simple_dtoa), TEST(test_six_digits),
      TEST(test_no_locale_strtod), TEST(test_printer), TEST(test_printer_emit), TEST(test_parameter), TEST(test_builtin_pool), TEST(test_csharp_names))
