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

/* Port of Sources/protoc-gen-grpc-swift-2/Options.swift: GeneratorOptions(parameter:) over the
 * parsedPairs of SwiftProtobufPluginLibrary. Keys and values compare by bytes: Swift compares
 * strings by canonical equivalence, which differs only for strings with characters such as
 * U+212A KELVIN SIGN, and no key or value here contains a "K". */

typedef enum { FILE_NAMING_FULL_PATH, FILE_NAMING_PATH_TO_UNDERSCORES, FILE_NAMING_DROP_PATH } FileNaming;

/* ProtobufCodeGenerator.Config.ModuleNames */
typedef struct {
	const char *grpc_core, *grpc_protobuf, *swift_protobuf;
} ModuleNames;

/* GeneratorOptions with ProtobufCodeGenerator.Config */
typedef struct {
	SwModuleMappings *proto_to_module_mappings;
	FileNaming file_naming;
	Vec extra_module_imports; /* const char * */
	GcPlatform *availability_overrides;
	int n_availability_overrides;
	/* config */
	GcAccessLevel access_level;
	bool access_level_on_imports, generate_client, generate_server;
	int indentation;
	ModuleNames module_names;
} Options;

/* GenerationError.description, with the parts that come from the parameter as Str (they may
 * contain NUL). */
static NORETURN COLD void generation_error(const char *a, Str b, const char *c, Str d, const char *e)
{
	Buf m = {0};
	buf_adds(&m, a);
	buf_add_str(&m, b);
	buf_adds(&m, c);
	buf_add_str(&m, d);
	buf_adds(&m, e);
	buf_str(&m);
	fail_str(buf_view(&m));
}

static NORETURN COLD void invalid_parameter_value(Str name, Str value)
{
	generation_error("Unknown value for generation parameter '", name, "': '", value, "'");
}

/* Bool(value.lowercased()): only an ASCII letter lowercases to one of "true" and "false". */
static bool parse_bool(Str key, Str value)
{
	char lower[6] = {0};
	if (value.n > 5)
		invalid_parameter_value(key, value);
	for (size_t i = 0; i < value.n; i++)
		lower[i] = (char)(value.p[i] >= 'A' && value.p[i] <= 'Z' ? value.p[i] + 0x20 : value.p[i]);
	if (!strcmp(lower, "true") && value.n == 4)
		return true;
	if (!strcmp(lower, "false") && value.n == 5)
		return false;
	invalid_parameter_value(key, value);
}

/* A module name for the generated code: printed as is, so only a NUL byte is unsupported. */
static const char *module_name(Str key, Str value)
{
	if (!value.n)
		invalid_parameter_value(key, value);
	if (memchr(value.p, 0, value.n))
		fail("unsupported by protoc-gen-uni: NUL byte in parameter %s", xstrndup(key.p, key.n));
	return xstrndup(value.p, value.n);
}

/* Reading the mappings file is not ported: the remote plugin (buf.build) cannot see local files,
 * so it always reports LoadError.failToOpen, and so does the port. */
static NORETURN COLD void load_module_mappings(const char *path)
{
	fail("Parameter 'ProtoPathModuleMappings=%s': failToOpen(path: %s)", path, sw_swift_debug_quote(path));
}

static COLD Options *parse_options(Str parameter)
{
	Options *o = xalloc(sizeof *o);
	o->access_level = GC_LEVEL_INTERNAL;
	o->generate_client = o->generate_server = true;
	o->indentation = 4;
	o->module_names = (ModuleNames){"GRPCCore", "GRPCProtobuf", "SwiftProtobuf"};
	const char *module_map_path = NULL;
	Vec availability = {0};

	SwParamPair *pairs;
	int n = sw_parse_parameter(parameter, &pairs);
	for (int i = 0; i < n; i++) {
		Str key = pairs[i].key, value = pairs[i].value;
		if (str_eqc(key, "Visibility")) {
			if (str_eqc(value, "Internal"))
				o->access_level = GC_LEVEL_INTERNAL;
			else if (str_eqc(value, "Public"))
				o->access_level = GC_LEVEL_PUBLIC;
			else if (str_eqc(value, "Package"))
				o->access_level = GC_LEVEL_PACKAGE;
			else
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "Server")) {
			o->generate_server = parse_bool(key, value);
		} else if (str_eqc(key, "Client")) {
			o->generate_client = parse_bool(key, value);
		} else if (str_eqc(key, "ProtoPathModuleMappings")) {
			/* The path goes to the error text: ASCII only, so that it is quoted as Swift does. */
			if (value.n)
				module_map_path = sw_ascii_name(value, "parameter ProtoPathModuleMappings");
		} else if (str_eqc(key, "FileNaming")) {
			if (str_eqc(value, "FullPath"))
				o->file_naming = FILE_NAMING_FULL_PATH;
			else if (str_eqc(value, "PathToUnderscores"))
				o->file_naming = FILE_NAMING_PATH_TO_UNDERSCORES;
			else if (str_eqc(value, "DropPath"))
				o->file_naming = FILE_NAMING_DROP_PATH;
			else
				invalid_parameter_value(key, value);
		} else if (str_eqc(key, "ExtraModuleImports")) {
			vec_push(&o->extra_module_imports, (void *)module_name(key, value));
		} else if (str_eqc(key, "GRPCModuleName")) {
			o->module_names.grpc_core = module_name(key, value);
		} else if (str_eqc(key, "GRPCProtobufModuleName")) {
			o->module_names.grpc_protobuf = module_name(key, value);
		} else if (str_eqc(key, "SwiftProtobufModuleName")) {
			o->module_names.swift_protobuf = module_name(key, value);
		} else if (str_eqc(key, "Availability")) {
			Str *parts;
			if (!value.n || swift_split(value, ' ', 1, &parts, "parameter Availability") != 2)
				invalid_parameter_value(key, value);
			GcPlatform *p = xalloc(sizeof *p);
			p->os = module_name(key, parts[0]);
			p->version = module_name(key, parts[1]);
			vec_push(&availability, p);
		} else if (str_eqc(key, "ReflectionData")) {
			fail("Unsupported parameter 'ReflectionData': The reflection service uses descriptor sets. Refer "
			     "to the protoc docs and the '--descriptor_set_out' option for more information.");
		} else if (str_eqc(key, "UseAccessLevelOnImports")) {
			o->access_level_on_imports = parse_bool(key, value);
		} else {
			generation_error("Unknown generation parameter '", key, "'", STR_LIT(""), "");
		}
	}

	if (module_map_path)
		load_module_mappings(module_map_path);
	o->proto_to_module_mappings = sw_module_mappings(o->module_names.swift_protobuf);
	o->n_availability_overrides = availability.n;
	o->availability_overrides = xalloc(sizeof(GcPlatform) * (size_t)(availability.n + 1));
	for (int i = 0; i < availability.n; i++)
		o->availability_overrides[i] = *(GcPlatform *)availability.v[i];
	return o;
}
