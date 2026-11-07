/* Port of @bufbuild/protoplugin (protobuf-es v2.17.0, packages/protoplugin/src), the framework of
 * ECMAScript code generator plugins: parameter parsing, the schema, generated files with
 * automatic imports, file preambles and JSDoc comments. Each .c file names its upstream source;
 * the former Go port (git c9b2e00, pkg/languages/ecmascript/protoplugin) has the same file split. The
 * TypeScript-compiler based transpilation (transpile.ts) is not ported. */
#ifndef PROTOPLUGIN_H
#define PROTOPLUGIN_H

#include "protobuf.h"

enum { TARGET_JS = 1, TARGET_TS = 2, TARGET_DTS = 4 };

/* import-symbol.ts */
typedef struct {
	const char *name, *from;
	bool type_only;
	const char *id; /* import("from").name */
} ImportSymbol;

/* runtime-imports.ts */
typedef struct {
	ImportSymbol *is_message, *message, *create, *from_json, *from_json_string, *from_binary,
		*to_binary, *to_json, *to_json_string, *proto_int64, *json_value, *json_object, *unknown_enum;
	/* codegen */
	ImportSymbol *boot, *file_desc, *enum_desc, *ext_desc, *message_desc, *service_desc, *ts_enum,
		*obj_enum, *gen_file, *gen_enum, *gen_extension, *gen_message, *gen_service;
} RuntimeImports;

typedef struct {
	const char *pattern, *target;
} PatternTarget;

/* parameter.ts: the options of every plugin. */
typedef struct {
	int targets[3]; /* in order of the option */
	int n_targets;
	const char *import_extension; /* "none", "js", "ts" */
	bool legacy_commonjs, ts_nocheck, elide_plugin_version, keep_empty_files, bootstrap_wkt;
	Vec map_imports, rewrite_imports; /* PatternTarget* */
} EsOptions;

/* An option that the framework does not know, for the plugin's parse_options. */
typedef struct {
	Str key, value;
} RawOption;

typedef struct Schema Schema;
typedef struct GeneratedFile GeneratedFile;

/* create-es-plugin.ts: a plugin built on the framework. */
typedef struct {
	const char *name, *version;
	/* Parses the plugin's own options. Returns NULL on success, or the reason of an error ("" for
	 * none). */
	const char *(*parse_options)(RawOption *opts, int n, void **out);
	void (*generate_ts)(Schema *s);
	void (*generate_js)(Schema *s);
	void (*generate_dts)(Schema *s);
} EsPlugin;

/* schema.ts */
struct Schema {
	Vec files, all_files; /* DescFile*: files to generate, all files */
	EsOptions opts;
	void *plugin_options;
	PbMsg *request;
	int target; /* while generating */
	Vec generated; /* GeneratedFile* */
	RuntimeImports *runtime;
	const char *plugin_name, *plugin_version /* NULL if elided */;
	Str sanitized_parameter;
};

/* generated-file.ts */
struct GeneratedFile {
	Schema *schema;
	const char *name, *import_path;
	bool legacy_commonjs;
	Str preamble; /* p == NULL if none */
	Vec el;
	RuntimeImports *rt;
};

/* Runs the plugin on the request and writes the serialized CodeGeneratorResponse to out. Errors
 * are reported in CodeGeneratorResponse.error, prefixed with the plugin name. */
void es_plugin_run(const EsPlugin *p, PbMsg *req, Buf *out);

GeneratedFile *gf_new(Schema *s, const char *name);
void gf_preamble(GeneratedFile *f, DescFile *file);
void gf_s(GeneratedFile *f, const char *s);
void gf_str(GeneratedFile *f, Str s);
void gf_sf(GeneratedFile *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void gf_nl(GeneratedFile *f);
void gf_sym(GeneratedFile *f, ImportSymbol *sym);
void gf_export(GeneratedFile *f, const char *decl, const char *name);
void gf_lit(GeneratedFile *f, Str s); /* string literal */
void gf_jsdoc_text(GeneratedFile *f, Str text, const char *indent);
void gf_jsdoc(GeneratedFile *f, const Desc *d, const char *indent);
ImportSymbol *gf_import_schema(GeneratedFile *f, const Desc *d, bool type_only);
ImportSymbol *gf_import_shape(GeneratedFile *f, const Desc *d);
ImportSymbol *gf_import_json(GeneratedFile *f, const Desc *d);
ImportSymbol *gf_import_valid(GeneratedFile *f, const Desc *d);

#endif
