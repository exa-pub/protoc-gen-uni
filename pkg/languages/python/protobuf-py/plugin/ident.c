/* Copyright (c) 2025-2026 Buf Technologies, Inc.
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
 * limitations under the License. */

/* protobuf/plugin/_ident.py of protobuf-py v0.7.0: Module and Ident. */
#include "internal.h"

#include <string.h>

PbpyIdent *pbpy_ident(const char *module, const char *name, bool type_only)
{
	PbpyIdent *id = xalloc(sizeof *id);
	id->name = name;
	id->module = module;
	id->type_only = type_only;
	return id;
}

const char *pbpy_module_child(const char *module, const char *name)
{
	return xcat(module, ".", pbpy_escape_identifier(name));
}

const char *pbpy_module_for_desc(const PbpyDescFile *desc, const char *suffix, bool escape_with_hash)
{
	bool pb = str_eq(suffix, "_pb");
	if (pb && desc->module_pb[escape_with_hash])
		return desc->module_pb[escape_with_hash];
	const char *name = str_trim_suffix(desc->name, ".proto");
	Vec parts = str_split(name, '/');
	Buf b = {0};
	for (int i = 0; i < parts.n; i++) {
		buf_addc(&b, '.');
		buf_adds(&b, pbpy_escape_proto_module_component(parts.v[i], escape_with_hash));
	}
	buf_adds(&b, suffix);
	const char *module = xstrdup(buf_str(&b));
	if (pb)
		((PbpyDescFile *)desc)->module_pb[escape_with_hash] = module;
	return module;
}

PbpyIdent *pbpy_ident_for_desc(const void *desc, bool type_only, bool escape_module_with_hash)
{
	const PbpyDesc *d = desc;
	if (d->kind == PBPY_FILE) {
		const char *module = pbpy_module_for_desc(desc, "_pb", escape_module_with_hash);
		const char *dot = strrchr(module, '.');
		const char *parent = dot == module ? "." : xstrndup(module, (size_t)(dot - module));
		PbpyIdent *id = pbpy_ident(parent, dot + 1, type_only);
		id->desc = desc;
		return id;
	}
	const char *identifier;
	const PbpyDescFile *file;
	if (d->kind == PBPY_MESSAGE) {
		const PbpyDescMessage *m = desc;
		identifier = m->local_qualname;
		file = m->file;
	} else if (d->kind == PBPY_ENUM) {
		const PbpyDescEnum *e = desc;
		identifier = e->local_qualname;
		file = e->file;
	} else {
		const PbpyDescExtension *x = desc;
		identifier = pbpy_escape_extension_name(x->name);
		if (x->parent)
			identifier = xcat(x->parent->local_qualname, ".", identifier);
		file = x->file;
	}
	PbpyIdent *id = pbpy_ident(pbpy_module_for_desc(file, "_pb", escape_module_with_hash), identifier, type_only);
	id->desc = desc;
	return id;
}
