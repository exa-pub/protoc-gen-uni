/* Copyright 2022-2025 The Connect Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

/* Port of Plugins/ConnectPluginUtilities/GeneratorOptions.swift (connect-swift 1.2.3).
 *
 * Keys and values compare as Swift strings, by canonical equivalence: of the names below only
 * "KeepMethodCasing" has an equivalent spelling, with U+212A KELVIN SIGN for the K. Errors are
 * thrown as CommandLineParameter.Error and printed with String(describing:), so their strings
 * are quoted by String.debugDescription. */
#include "connect_plugin_utilities.h"

#include <string.h>

/* An associated String value as String(describing:) prints it. */
static char *describe(Str s)
{
	char *q = sw_debug_description(s);
	if (!q)
		fail("unsupported by protoc-gen-uni: non-ASCII character in parameter %s", sw_quote(s));
	return q;
}

NORETURN COLD static void invalid_parameter_value(Str key, Str value)
{
	fail("invalidParameterValue(key: %s, value: %s)", describe(key), describe(value));
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

CpuOptions *cpu_parse_options(Str parameter)
{
	CpuOptions *o = xalloc(sizeof *o);
	o->file_naming = CPU_FULL_PATH;
	o->generate_async_methods = true;
	o->generate_service_metadata = true;
	o->proto_to_module_mappings = sw_module_mappings(NULL);
	o->swift_protobuf_module_name = STR_LIT("SwiftProtobuf");
	o->visibility = CPU_INTERNAL;

	SwParamPair *pairs;
	int n = sw_parse_parameter(parameter, &pairs);
	for (int i = 0; i < n; i++) {
		Str key = pairs[i].key, value = pairs[i].value;
		bool ok = true;
		if (str_eqc(key, "ExtraModuleImports")) {
			Str *v = xalloc(sizeof *v);
			*v = value;
			vec_push(&o->extra_module_imports, v);
		} else if (str_eqc(key, "FileNaming")) {
			if (str_eqc(value, "FullPath"))
				o->file_naming = CPU_FULL_PATH;
			else if (str_eqc(value, "PathToUnderscores"))
				o->file_naming = CPU_PATH_TO_UNDERSCORES;
			else if (str_eqc(value, "DropPath"))
				o->file_naming = CPU_DROP_PATH;
			else
				ok = false;
		} else if (str_eqc(key, "GenerateAsyncMethods")) {
			ok = parse_bool(value, &o->generate_async_methods);
		} else if (str_eqc(key, "GenerateCallbackMethods")) {
			ok = parse_bool(value, &o->generate_callback_methods);
		} else if (str_eqc(key, "GenerateServiceMetadata")) {
			ok = parse_bool(value, &o->generate_service_metadata);
		} else if (str_eqc(key, "KeepMethodCasing") || str_eqc(key, "\xe2\x84\xaa" "eepMethodCasing")) {
			ok = parse_bool(value, &o->keep_method_casing);
		} else if (str_eqc(key, "ProtoPathModuleMappings")) {
			/* ProtoFileToModuleMappings(path:) reads a local file. The remote plugin cannot see
			 * the user's files, so the port fails as it does for a missing file. */
			fail("deserializationError(key: \"ProtoPathModuleMappings\", error: "
			     "SwiftProtobufPluginLibrary.ProtoFileToModuleMappings.LoadError.failToOpen(path: %s))",
			     describe(value));
		} else if (str_eqc(key, "SwiftProtobufModuleName")) {
			o->swift_protobuf_module_name = value;
		} else if (str_eqc(key, "Visibility")) {
			if (str_eqc(value, "Internal"))
				o->visibility = CPU_INTERNAL;
			else if (str_eqc(value, "Public"))
				o->visibility = CPU_PUBLIC;
			else if (str_eqc(value, "Package"))
				o->visibility = CPU_PACKAGE;
			else
				ok = false;
		} else {
			fail("unknownParameter(string: %s)", describe(key));
		}
		if (!ok)
			invalid_parameter_value(key, value);
	}
	return o;
}
