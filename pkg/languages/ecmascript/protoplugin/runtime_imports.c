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

/* Ported from packages/protoplugin/src/runtime-imports.ts and import-symbol.ts. */
#include "internal.h"

#include <stddef.h>
#include <string.h>

ImportSymbol *new_import_symbol(const char *name, const char *from, bool type_only)
{
	ImportSymbol *s = xalloc(sizeof *s);
	s->name = name;
	s->from = from;
	s->type_only = type_only;
	s->id = xcat("import(\"", from, "\").", name);
	return s;
}

/* The symbols of @bufbuild/protobuf; with bootstrap_wkt, relative paths into its sources. */
RuntimeImports *create_runtime_imports(bool bootstrap)
{
	enum { TYPE = 1, CODEGEN = 2 };
	static const struct {
		uint8_t field; /* offsetof(RuntimeImports, ...) / sizeof(ImportSymbol *) */
		uint8_t flags;
		const char *name, *boot;
	} syms[] = {
#define F(f) (uint8_t)(offsetof(RuntimeImports, f) / sizeof(ImportSymbol *))
		{F(is_message), 0, "isMessage", "../is-message.js"},
		{F(message), TYPE, "Message", "../types.js"},
		{F(create), 0, "create", "../create.js"},
		{F(from_json), 0, "fromJson", "../from-json.js"},
		{F(from_json_string), 0, "fromJsonString", "../from-json.js"},
		{F(from_binary), 0, "fromBinary", "../from-binary.js"},
		{F(to_binary), 0, "toBinary", "../to-binary.js"},
		{F(to_json), 0, "toJson", "../to-json.js"},
		{F(to_json_string), 0, "toJsonString", "../to-json.js"},
		{F(proto_int64), 0, "protoInt64", "../proto-int64.js"},
		{F(json_value), TYPE, "JsonValue", "../json-value.js"},
		{F(json_object), TYPE, "JsonObject", "../json-value.js"},
		{F(unknown_enum), TYPE, "UnknownEnum", "../types.js"},
		{F(boot), CODEGEN, "boot", "../codegenv2/boot.js"},
		{F(file_desc), CODEGEN, "fileDesc", "../codegenv2/file.js"},
		{F(enum_desc), CODEGEN, "enumDesc", "../codegenv2/enum.js"},
		{F(ext_desc), CODEGEN, "extDesc", "../codegenv2/extension.js"},
		{F(message_desc), CODEGEN, "messageDesc", "../codegenv2/message.js"},
		{F(service_desc), CODEGEN, "serviceDesc", "../codegenv2/service.js"},
		{F(ts_enum), CODEGEN, "tsEnum", "../codegenv2/enum.js"},
		{F(obj_enum), CODEGEN, "objEnum", "../codegenv2/enum.js"},
		{F(gen_file), CODEGEN | TYPE, "GenFile", "../codegenv2/types.js"},
		{F(gen_enum), CODEGEN | TYPE, "GenEnum", "../codegenv2/types.js"},
		{F(gen_extension), CODEGEN | TYPE, "GenExtension", "../codegenv2/types.js"},
		{F(gen_message), CODEGEN | TYPE, "GenMessage", "../codegenv2/types.js"},
		{F(gen_service), CODEGEN | TYPE, "GenService", "../codegenv2/types.js"},
#undef F
	};
	ImportSymbol *all[sizeof(RuntimeImports) / sizeof(ImportSymbol *)];
	for (size_t i = 0; i < sizeof syms / sizeof *syms; i++) {
		const char *from = bootstrap ? syms[i].boot
				   : syms[i].flags & CODEGEN ? "@bufbuild/protobuf/codegenv2"
							     : "@bufbuild/protobuf";
		all[syms[i].field] = new_import_symbol(syms[i].name, from, syms[i].flags & TYPE);
	}
	RuntimeImports *r = xalloc(sizeof *r);
	memcpy(r, all, sizeof *r);
	return r;
}
