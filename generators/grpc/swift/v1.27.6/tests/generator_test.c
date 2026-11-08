/* grpc/swift v1.27.6: option parsing and its error texts (as String(describing:) prints the
 * GenerationError of grpc-swift 1.27.6 Options.swift), and the output file names of
 * GenerateGRPC.swift. Includes the unity build to reach its static functions. */
#include "../generator.c"
#include "test.h"

static GeneratorOptions *parsed;

static void run_parse(void *arg) { parsed = parse_options(*(Str *)arg); }

/* The error of GeneratorOptions(parameter:), or "" on success. */
static const char *parse_error(Str parameter)
{
	parsed = NULL;
	const char *err = try_run(run_parse, &parameter);
	return err ? err : "";
}

static void test_options(void)
{
	CHECK_STR(parse_error(STR_LIT("")), "");
	CHECK(parsed->visibility == VIS_INTERNAL && parsed->generate_server && parsed->generate_client);
	CHECK(!parsed->generate_test_client && !parsed->keep_method_casing && !parsed->generate_reflection_data);
	CHECK(str_eqc(parsed->grpc_module_name, "GRPC") && str_eqc(parsed->swift_protobuf_module_name, "SwiftProtobuf"));

	CHECK_STR(parse_error(STR_LIT(" Visibility = Package , FileNaming=DropPath,ExtraModuleImports=A,ExtraModuleImports=B")), "");
	CHECK(parsed->visibility == VIS_PACKAGE && parsed->file_naming == FILE_NAMING_DROP_PATH);
	CHECK(parsed->extra_module_imports.n == 2);

	/* Kelvin sign: canonically equivalent to "KeepMethodCasing". */
	CHECK_STR(parse_error(STR_LIT("\xe2\x84\xaa" "eepMethodCasing=true")), "");
	CHECK(parsed->keep_method_casing);

	CHECK_STR(parse_error(STR_LIT("_V2=false")), "");
	CHECK_STR(parse_error(STR_LIT("_V2=true")), "unsupportedV2");
	CHECK_STR(parse_error(STR_LIT("_V2")), "unsupportedV2");
	CHECK_STR(parse_error(STR_LIT("foo=bar")), "unknownParameter(name: \"foo\")");
	CHECK_STR(parse_error(STR_LIT("it's")), "unknownParameter(name: \"it\\'s\")");
	CHECK_STR(parse_error(STR_LIT("Visibility=public")), "invalidParameterValue(name: \"Visibility\", value: \"public\")");
	CHECK_STR(parse_error(STR_LIT("Server=TRUE")), "invalidParameterValue(name: \"Server\", value: \"TRUE\")");
	CHECK_STR(parse_error(STR_LIT("ExtraModuleImports=")), "invalidParameterValue(name: \"ExtraModuleImports\", value: \"\")");
	CHECK_STR(parse_error(STR_LIT("GRPCModuleName")), "invalidParameterValue(name: \"GRPCModuleName\", value: \"\")");
	CHECK_STR(parse_error(STR_LIT("Visibility=Public,foo")), "unknownParameter(name: \"foo\")");

	/* An empty mappings path is ignored; any other is not read (the remote plugin cannot see
	 * local files). */
	CHECK_STR(parse_error(STR_LIT("ProtoPathModuleMappings=")), "");
	CHECK_STR(parse_error(STR_LIT("ProtoPathModuleMappings=m.asciipb")),
		  "wrappedError(message: \"Parameter \\'ProtoPathModuleMappings=m.asciipb\\'\", error: "
		  "SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: \"m.asciipb\"))");
	CHECK_STR(parse_error(STR_LIT("f\xc3\xa9=1")), "unsupported by protoc-gen-uni: non-ASCII character in parameter \"f\xc3\xa9\"");
}

static void test_file_names(void)
{
	SwFile f = {.name = "a/b/c.proto"};
	CHECK_STR(output_file_name("grpc", &f, FILE_NAMING_FULL_PATH, "swift"), "a/b/c.grpc.swift");
	CHECK_STR(output_file_name("grpc", &f, FILE_NAMING_PATH_TO_UNDERSCORES, "swift"), "a_b_c.grpc.swift");
	CHECK_STR(output_file_name("grpc", &f, FILE_NAMING_DROP_PATH, "reflection"), "c.grpc.reflection");
	f.name = "x.y/noext";
	CHECK_STR(output_file_name("grpc", &f, FILE_NAMING_FULL_PATH, "swift"), "x.y/noext.grpc.swift");
	f.name = "d/v1.2.proto";
	CHECK_STR(output_file_name("grpc", &f, FILE_NAMING_FULL_PATH, "swift"), "d/v1.2.grpc.swift");

	generated_file_names = (Map){0};
	SwFile g = {.name = "x/c.proto"}, h = {.name = "y/c.proto"};
	f.name = "a/b/c.proto";
	CHECK_STR(unique_output_file_name(&f, FILE_NAMING_DROP_PATH, "swift"), "c.grpc.swift");
	CHECK_STR(unique_output_file_name(&f, FILE_NAMING_DROP_PATH, "reflection"), "c.grpc.reflection");
	CHECK_STR(unique_output_file_name(&g, FILE_NAMING_DROP_PATH, "swift"), "c.1.grpc.swift");
	CHECK_STR(unique_output_file_name(&h, FILE_NAMING_DROP_PATH, "swift"), "c.2.grpc.swift");
}

TESTS(TEST(test_options), TEST(test_file_names))
