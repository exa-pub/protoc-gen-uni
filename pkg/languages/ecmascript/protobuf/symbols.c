/* Copyright 2021-2026 Buf Technologies, Inc.
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

/* Ported from packages/protobuf/src/codegenv2/symbols.ts (wktPublicImportPaths, also of v2.13.0),
 * codegenv2/scalar.ts (scalarTypeScriptType, scalarJsonType) and wkt/wrappers.ts
 * (isWrapperDesc). */
#include "protobuf.h"

#include <string.h>

static bool is_64bit(int s)
{
	return s == PT_UINT64 || s == PT_SFIXED64 || s == PT_FIXED64 || s == PT_SINT64 || s == PT_INT64;
}

const char *scalar_ts_type(int s, bool long_as_string)
{
	if (s == PT_STRING)
		return "string";
	if (s == PT_BOOL)
		return "boolean";
	if (is_64bit(s))
		return long_as_string ? "string" : "bigint";
	if (s == PT_BYTES)
		return "Uint8Array";
	return "number";
}

const char *scalar_json_type(int s)
{
	switch (s) {
	case PT_DOUBLE: case PT_FLOAT: return "number | \"NaN\" | \"Infinity\" | \"-Infinity\"";
	case PT_UINT64: case PT_SFIXED64: case PT_FIXED64: case PT_SINT64: case PT_INT64: return "string";
	case PT_INT32: case PT_FIXED32: case PT_UINT32: case PT_SFIXED32: case PT_SINT32: return "number";
	case PT_STRING: return "string";
	case PT_BOOL: return "boolean";
	case PT_BYTES: return "string";
	}
	return "";
}

bool is_wrapper_desc(const DescMessage *m)
{
	static const char *const names[] = {
		"google.protobuf.DoubleValue", "google.protobuf.FloatValue", "google.protobuf.Int64Value",
		"google.protobuf.UInt64Value", "google.protobuf.Int32Value", "google.protobuf.UInt32Value",
		"google.protobuf.BoolValue", "google.protobuf.StringValue", "google.protobuf.BytesValue",
	};
	bool ok = false;
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		ok |= str_eq(m->type_name, names[i]);
	if (!ok || !m->fields.n)
		return false;
	const DescField *f = m->fields.v[0];
	return f->field_kind == FK_SCALAR && str_eq(f->name, "value") && f->number == 1;
}

const char *wkt_public_import_path(const char *name)
{
	static const char *const barrel[] = {
		"google/protobuf/compiler/plugin.proto", "google/protobuf/any.proto",
		"google/protobuf/descriptor.proto", "google/protobuf/duration.proto",
		"google/protobuf/empty.proto", "google/protobuf/field_mask.proto",
		"google/protobuf/struct.proto", "google/protobuf/timestamp.proto",
		"google/protobuf/wrappers.proto",
	};
	static const char *const own[] = {
		"google/protobuf/api.proto", "google/protobuf/c_sharp_features.proto",
		"google/protobuf/cpp_features.proto", "google/protobuf/cpp_file_options.proto",
		"google/protobuf/cpp_options.proto", "google/protobuf/go_features.proto",
		"google/protobuf/java_features.proto", "google/protobuf/json_enumvalue_options.proto",
		"google/protobuf/json_options.proto", "google/protobuf/source_context.proto",
		"google/protobuf/type.proto",
	};
	for (size_t i = 0; i < sizeof barrel / sizeof *barrel; i++)
		if (str_eq(name, barrel[i]))
			return "@bufbuild/protobuf/wkt";
	for (size_t i = 0; i < sizeof own / sizeof *own; i++)
		if (str_eq(name, own[i]))
			return xcat("@bufbuild/protobuf/wkt/", str_trim_suffix(name, ".proto"), "_pb.js");
	return NULL;
}

/* v2.13.0: every well-known file it knows is imported from the barrel. */
const char *wkt_public_import_path_v2_13_0(const char *name)
{
	static const char *const files[] = {
		"compiler/plugin", "any", "api", "cpp_features", "descriptor", "duration", "empty",
		"field_mask", "go_features", "java_features", "source_context", "struct", "timestamp",
		"type", "wrappers",
	};
	if (!has_prefix(name, "google/protobuf/"))
		return NULL;
	const char *rest = name + strlen("google/protobuf/");
	for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
		size_t n = strlen(files[i]);
		if (!strncmp(rest, files[i], n) && str_eq(rest + n, ".proto"))
			return "@bufbuild/protobuf/wkt";
	}
	return NULL;
}
