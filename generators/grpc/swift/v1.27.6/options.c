/*
 * Copyright 2017, gRPC Authors All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Port of Sources/protoc-gen-grpc-swift/Options.swift (grpc-swift 1.27.6), built with Swift 6.3
 * (`compiler(>=6.0)`: the _V2 option exists).
 *
 * Keys and values compare as Swift strings, by canonical equivalence: of the names below only
 * "KeepMethodCasing" has an equivalent spelling, with U+212A KELVIN SIGN for the K. Errors are
 * GenerationError values printed with String(describing:), so their strings are quoted by
 * String.debugDescription. */

typedef enum { VIS_INTERNAL, VIS_PUBLIC, VIS_PACKAGE } Visibility;
typedef enum { FILE_NAMING_FULL_PATH, FILE_NAMING_PATH_TO_UNDERSCORES, FILE_NAMING_DROP_PATH } FileNaming;

typedef struct {
	Visibility visibility;
	bool generate_server, generate_client, generate_test_client, keep_method_casing;
	SwModuleMappings *proto_to_module_mappings;
	FileNaming file_naming;
	Vec extra_module_imports; /* Str * */
	Str grpc_module_name, swift_protobuf_module_name;
	bool generate_reflection_data;
	bool use_access_level_on_imports; /* parsed, not used by the v1 generator */
} GeneratorOptions;

/* Visibility.sourceSnippet */
static const char *const visibility_snippets[] = {"internal", "public", "package"};

/* An associated String value as String(describing:) prints it. */
static char *describe(Str s)
{
	char *q = sw_debug_description(s);
	if (!q)
		fail("unsupported by protoc-gen-uni: non-ASCII character in parameter %s", sw_quote(s));
	return q;
}

/* Swift's Bool(String): only "true" and "false". */
static bool parse_bool(Str value, bool *out)
{
	if (str_eqc(value, "true"))
		*out = true;
	else if (str_eqc(value, "false"))
		*out = false;
	else
		return false;
	return true;
}

static Str *new_str(Str s)
{
	Str *p = xalloc(sizeof *p);
	*p = s;
	return p;
}

/* GeneratorOptions(parameter:) */
COLD static GeneratorOptions *parse_options(Str parameter)
{
	GeneratorOptions *o = xalloc(sizeof *o);
	o->visibility = VIS_INTERNAL;
	o->generate_server = true;
	o->generate_client = true;
	o->proto_to_module_mappings = sw_module_mappings(NULL);
	o->file_naming = FILE_NAMING_FULL_PATH;
	o->grpc_module_name = STR_LIT("GRPC");
	o->swift_protobuf_module_name = STR_LIT("SwiftProtobuf");

	SwParamPair *pairs;
	int n = sw_parse_parameter(parameter, &pairs);
	for (int i = 0; i < n; i++) {
		Str key = pairs[i].key, value = pairs[i].value;
		bool ok = true;
		if (str_eqc(key, "Visibility")) {
			if (str_eqc(value, "Internal"))
				o->visibility = VIS_INTERNAL;
			else if (str_eqc(value, "Public"))
				o->visibility = VIS_PUBLIC;
			else if (str_eqc(value, "Package"))
				o->visibility = VIS_PACKAGE;
			else
				ok = false;
		} else if (str_eqc(key, "Server")) {
			ok = parse_bool(value, &o->generate_server);
		} else if (str_eqc(key, "Client")) {
			ok = parse_bool(value, &o->generate_client);
		} else if (str_eqc(key, "TestClient")) {
			ok = parse_bool(value, &o->generate_test_client);
		} else if (str_eqc(key, "KeepMethodCasing") || str_eqc(key, "\xe2\x84\xaa" "eepMethodCasing")) {
			ok = parse_bool(value, &o->keep_method_casing);
		} else if (str_eqc(key, "ProtoPathModuleMappings")) {
			/* ProtoFileToModuleMappings(path:) reads a local file. The remote plugin cannot see
			 * the user's files, so the port fails as it does for a missing file. */
			if (value.n) {
				Buf message = {0};
				buf_adds(&message, "Parameter 'ProtoPathModuleMappings=");
				buf_add_str(&message, value);
				buf_addc(&message, '\'');
				fail("wrappedError(message: %s, error: "
				     "SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: %s))",
				     describe(buf_view(&message)), describe(value));
			}
		} else if (str_eqc(key, "FileNaming")) {
			if (str_eqc(value, "FullPath"))
				o->file_naming = FILE_NAMING_FULL_PATH;
			else if (str_eqc(value, "PathToUnderscores"))
				o->file_naming = FILE_NAMING_PATH_TO_UNDERSCORES;
			else if (str_eqc(value, "DropPath"))
				o->file_naming = FILE_NAMING_DROP_PATH;
			else
				ok = false;
		} else if (str_eqc(key, "ExtraModuleImports")) {
			if (value.n)
				vec_push(&o->extra_module_imports, new_str(value));
			else
				ok = false;
		} else if (str_eqc(key, "GRPCModuleName")) {
			if (value.n)
				o->grpc_module_name = value;
			else
				ok = false;
		} else if (str_eqc(key, "SwiftProtobufModuleName")) {
			if (value.n)
				o->swift_protobuf_module_name = value;
			else
				ok = false;
		} else if (str_eqc(key, "ReflectionData")) {
			ok = parse_bool(value, &o->generate_reflection_data);
		} else if (str_eqc(key, "_V2")) {
			bool v2;
			if (!parse_bool(value, &v2) || v2)
				fail("unsupportedV2");
			/* _V2 is false, ignore it. */
		} else if (str_eqc(key, "UseAccessLevelOnImports")) {
			ok = parse_bool(value, &o->use_access_level_on_imports);
		} else {
			fail("unknownParameter(name: %s)", describe(key));
		}
		if (!ok)
			fail("invalidParameterValue(name: %s, value: %s)", describe(key), describe(value));
	}
	return o;
}
