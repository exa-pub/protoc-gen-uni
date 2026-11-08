/* Port of @bufbuild/protoplugin (protobuf-es v2.17.0, packages/protoplugin/src), the framework of
 * ECMAScript code generator plugins: parameter parsing, the schema, generated files with
 * automatic imports, file preambles and JSDoc comments. Each .c file names its upstream source;
 * the former Go port (git c9b2e00, pkg/languages/ecmascript/protoplugin) has the same file split. The
 * TypeScript-compiler based transpilation (transpile.ts) is not ported. */
#ifndef PROTOPLUGIN_H
#define PROTOPLUGIN_H

#include "protobuf.h"

enum { TARGET_JS = 1, TARGET_TS = 2, TARGET_DTS = 4 };

/* The version of @bufbuild/protoplugin (with the @bufbuild/protobuf it requires) a plugin is
 * built on. The library implements v2.17.0; an older version changes only the parts that differ.
 * v2.13.0: no map_imports option, no escaping in the preamble, import_extension applies to every
 * import path, names are not validated, comments keep their line breaks, editions up to 2024,
 * well-known types are all imported from the @bufbuild/protobuf/wkt barrel. */
enum { PROTOPLUGIN_V2_17_0, PROTOPLUGIN_V2_13_0 };

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
	int protoplugin; /* PROTOPLUGIN_V2_17_0 (0) or an older version */
	/* Parses the plugin's own options. Returns NULL on success, or the reason of an error ("" for
	 * none). NULL if the plugin has no options: an unknown option is an error. */
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
	int protoplugin; /* EsPlugin.protoplugin */
	Map proto_files; /* name -> the first CodeGeneratorRequest.proto_file with the name */
	Map generating;  /* DescFile.name of the files to generate */
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

/* safe-identifier.ts: escapes reserved words and built-in names. */
const char *safe_identifier(const char *name);

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

/* source-code-info.ts: comments of descriptors (getComments, getSyntaxComments,
 * getPackageComments) and getDeclarationString. */
typedef struct {
	Str *leading_detached;
	int n_leading_detached;
	Str leading, trailing; /* p == NULL if absent */
} Comments;
Comments get_comments(const Desc *d);
Comments get_syntax_comments(DescFile *f);
Comments get_package_comments(DescFile *f);
Str declaration_string(const Desc *d);
/* Whether comments have their line breaks normalized to LF (true, added after 2.13.0). Plugins
 * built on an older @bufbuild/protoplugin set it to false before generating (2.5.2: protobuf-ts;
 * es_plugin_run sets it from EsPlugin.protoplugin). */
extern bool comments_normalize_line_breaks;

#endif
