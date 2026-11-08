/* pkg/languages/swift/connect-plugin-utilities: the cases of connect-swift 1.2.3
 * Tests/UnitTests/ConnectPluginUtilitiesTests/FilePathComponentsTests.swift, plus option errors
 * checked against buf.build/connectrpc/swift:v1.2.3. */
#include "connect_plugin_utilities.h"
#include "test.h"

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

static char *str(Str s) { return xstrndup(s.p, s.n); }

static void test_file_path_components(void)
{
	static const struct {
		const char *path, *directory, *base, *suffix, *full, *drop, *underscores;
	} cases[] = {
		{"/foo/bar/baz.proto", "/foo/bar", "baz", ".proto", "/foo/bar/baz.connect.swift", "baz.connect.swift",
		 "foo_bar_baz.connect.swift"},
		{"foo/bar/baz.proto", "foo/bar", "baz", ".proto", "foo/bar/baz.connect.swift", "baz.connect.swift",
		 "foo_bar_baz.connect.swift"},
		{"baz.proto", "", "baz", ".proto", "baz.connect.swift", "baz.connect.swift", "baz.connect.swift"},
		{"/baz.proto", "", "baz", ".proto", "baz.connect.swift", "baz.connect.swift", "baz.connect.swift"},
		/* split(separator:) drops empty parts; a single part goes nowhere (base is dropLast()) */
		{"a/b..c.proto", "a", "b.c", ".proto", "a/b.c.connect.swift", "b.c.connect.swift", "a_b.c.connect.swift"},
		{"a/baz", "a", "", "", "a/.connect.swift", ".connect.swift", "a_.connect.swift"},
		{"_a//b.proto", "_a/", "b", ".proto", "_a//b.connect.swift", "b.connect.swift", "a__b.connect.swift"},
	};
	for (size_t i = 0; i < LEN(cases); i++) {
		CpuFilePathComponents c = cpu_file_path_components(cases[i].path);
		CHECK_STR(str(c.directory), cases[i].directory);
		CHECK_STR(str(c.base), cases[i].base);
		CHECK_STR(str(c.suffix), cases[i].suffix);
		CHECK_STR(cpu_output_file_path(&c, ".connect.swift", CPU_FULL_PATH), cases[i].full);
		CHECK_STR(cpu_output_file_path(&c, ".connect.swift", CPU_DROP_PATH), cases[i].drop);
		CHECK_STR(cpu_output_file_path(&c, ".connect.swift", CPU_PATH_TO_UNDERSCORES), cases[i].underscores);
	}
}

static Str parse_error;

static void run_parse(void *arg) { cpu_parse_options(*(Str *)arg); }

static const char *parse(const char *parameter)
{
	Str p = str_c(parameter);
	if (!try_run(run_parse, &p))
		return "";
	parse_error = fail_message();
	return str(parse_error);
}

static void test_options(void)
{
	static const struct {
		const char *parameter, *error;
	} cases[] = {
		{"", ""},
		{"Visibility=Public, FileNaming=PathToUnderscores", ""},
		{"ExtraModuleImports=,KeepMethodCasing=true", ""},
		{"\xe2\x84\xaa" "eepMethodCasing=true", ""},
		{"Visibility=public", "invalidParameterValue(key: \"Visibility\", value: \"public\")"},
		{"foo=bar", "unknownParameter(string: \"foo\")"},
		{"a\x01" "b\x7f=1", "unknownParameter(string: \"a\\u{01}b\\u{7F}\")"},
		{"Visibility=a\"b\\c'd\tq", "invalidParameterValue(key: \"Visibility\", value: \"a\\\"b\\\\c\\'d\\tq\")"},
		{"KeepMethodCasing=TRUE", "invalidParameterValue(key: \"KeepMethodCasing\", value: \"TRUE\")"},
		{"ProtoPathModuleMappings=x",
		 "deserializationError(key: \"ProtoPathModuleMappings\", error: "
		 "SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: \"x\"))"},
		{"ProtoPathModuleMappings=",
		 "deserializationError(key: \"ProtoPathModuleMappings\", error: "
		 "SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: \"\"))"},
	};
	for (size_t i = 0; i < LEN(cases); i++)
		CHECK_STR(parse(cases[i].parameter), cases[i].error);
}

static void test_options_values(void)
{
	CpuOptions *o = cpu_parse_options(str_c("GenerateCallbackMethods=true,GenerateAsyncMethods=false,"
						"GenerateServiceMetadata=false,FileNaming=DropPath,Visibility=Package,"
						"SwiftProtobufModuleName=SP,ExtraModuleImports=A,ExtraModuleImports=B"));
	CHECK(o->generate_callback_methods && !o->generate_async_methods && !o->generate_service_metadata);
	CHECK(o->file_naming == CPU_DROP_PATH && o->visibility == CPU_PACKAGE && !o->keep_method_casing);
	CHECK_STR(str(o->swift_protobuf_module_name), "SP");
	CHECK(o->extra_module_imports.n == 2);
}

TESTS(TEST(test_file_path_components), TEST(test_options), TEST(test_options_values))
