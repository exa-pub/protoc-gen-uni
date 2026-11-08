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

/* Internals of the plugin framework protobuf.plugin shared by its files. */
#ifndef PBPY_PLUGIN_INTERNAL_H
#define PBPY_PLUGIN_INTERNAL_H

#include "protobuf_py.h"

/* ---- traceback (options.c) ---- */

/* The frames of a Python 3.13 traceback.format_exc() in the remote plugin's site-packages. */
#define PBPY_SITE "/app/lib/python3.13/site-packages/protobuf/plugin/"
#define PBPY_FRAME(file, line, func, src) "  File \"" PBPY_SITE file "\", line " #line ", in " func "\n    " src "\n"
/* raise ValueError(msg) with the traceback of these frames (most recent call last). */
NORETURN COLD void pbpy_raise(const char *frames, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* ---- map_imports.c ---- */

/* MapImports: compiled `glob:target` pairs (target "" maps to the canonical module path). */
typedef struct {
	int n;
	struct MapImport *v;
} PbpyMapImports;
/* compile_map_imports over the dict entries (keys: globs, values: targets) */
COLD PbpyMapImports pbpy_compile_map_imports(const Vec *keys, const Vec *values);
/* map_import_target: the target of the first matching glob, or NULL */
const char *pbpy_map_import_target(const PbpyMapImports *m, const char *proto_name);

/* ---- options.c ---- */

/* parse_options: fills values (one per def) and returns the unparsed tokens joined by ",". stack:
 * the traceback frames of the call (for the errors). */
COLD const char *pbpy_parse_options(const PbpyOptDef *defs, int n, Str parameter, PbpyOptValue *values, const char *stack);

/* ---- schema (run.c) ---- */

struct PbpySchema {
	const PbpyPlugin *plugin;
	Vec files_to_generate, all_files;
	Map file_to_generate; /* names */
	const char *parameter;
	bool escape_module_with_hash;
	PbpyMapImports map_imports;
	Map map_targets; /* proto file name -> map_import_target (cache; "\x01": none) */
	Map generated;  /* path -> PbpyFile */
	Vec paths;      /* generated paths, in the order of their first generate_file */
};

/* ---- file.c ---- */

typedef struct {
	Str s;         /* text, or */
	PbpyIdent *id; /* an ident resolved when written */
} PbpyOut;

/* The imports of a file by module (dict[Module, set[Ident]]). */
typedef struct {
	Map by_module; /* module path -> PbpyModImports */
	Vec modules;   /* PbpyModImports *, in insertion order */
} PbpyImports;
typedef struct {
	const char *module;
	Map names;  /* ident name -> PbpyIdent */
	Vec idents; /* PbpyIdent *, in insertion order */
} PbpyModImports;

struct PbpyFile {
	const char *path, *module;
	PbpySchema *schema;
	int indent;
	bool type_checking, in_doc, has_preamble;
	const char *preamble_proto_name;
	Vec out; /* PbpyOut * */
	Map rel; /* module -> the module relative to this file (cache of _relativize) */
	PbpyImports runtime, types;
};

PbpyFile *pbpy_file_new(PbpySchema *s, const char *path, const char *module);
/* write(): the content of the file */
Str pbpy_file_write(PbpyFile *f, bool no_fmt_off);

#endif
