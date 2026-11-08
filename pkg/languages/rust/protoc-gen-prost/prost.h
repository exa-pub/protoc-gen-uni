/* The shared core of the protoc-gen-prost plugin family (neoeinstein-prost, -prost-crate,
 * -prost-serde): ports of protoc-gen-prost 0.5.0 (https://github.com/neoeinstein/protoc-gen-prost,
 * Apache-2.0: parameter parsing, ModuleRequestSet, the response and the file descriptor set
 * generator) and of the prost-build 0.14.1 pieces they use (https://github.com/tokio-rs/prost,
 * Apache-2.0: ident.rs, module.rs, ast.rs comments). Results are arena strings. */
#ifndef PROST_H
#define PROST_H

#include "pb.h"

/* ---- ident.c (prost-build src/ident.rs) ---- */

/* Rust keywords become raw identifiers (r#type); _, super, self, Self, extern and crate get an
 * underscore suffix; a leading digit gets an underscore prefix. */
char *prost_sanitize_identifier(const char *s);
/* heck snake case, sanitized: a field or module identifier. */
char *prost_to_snake(const char *s);
/* heck upper camel case, sanitized: a type identifier. */
char *prost_to_upper_camel(const char *s);
/* Strips the enum name prefix from an enum value name (both upper camel case), unless the rest
 * does not start with an uppercase char; then sanitizes. */
char *prost_strip_enum_prefix(const char *prefix, const char *name);

/* ---- module.c (prost-build src/module.rs) ---- */

/* A Rust module path; ordered like Vec<String> (element-wise byte order, then length). */
typedef struct {
	int n;
	char **parts;
} ProstModule;

/* The module of a protobuf package: the non-empty dot-separated parts, prost_to_snake each. */
ProstModule *prost_module_from_package(const char *package);
int prost_module_cmp(const ProstModule *a, const ProstModule *b);
/* The parts joined with "." plus ".rs", or default ".rs" for the root module. */
char *prost_module_to_file_name_or(const ProstModule *m, const char *def);
/* The parts joined with "::" (Display). */
char *prost_module_display(const ProstModule *m);

/* ---- ast.c (prost-build src/ast.rs, without the cleanup-markdown feature) ---- */

typedef struct {
	int n_detached;
	Str **detached; /* lines of each leading detached comment */
	int *n_detached_lines;
	Str *leading, *trailing;
	int n_leading, n_trailing;
} ProstComments;

/* Comments::from_location of a SourceCodeInfo.Location (NULL: no comments). */
ProstComments prost_comments_from_location(const PbMsg *loc);
/* Comments::append_with_indent: rustdoc lines, four spaces per indent level. */
void prost_comments_append_with_indent(const ProstComments *c, int indent, Buf *buf);
/* Comments::sanitize_line: <url> for http(s) URLs, \[ \] for brackets that are not Markdown
 * links, and a leading space unless the line starts with exactly one space. */
void prost_sanitize_line(Str line, Buf *out);

/* ---- decode.c (prost 0.14.1 decoding of the prost-types 0.14.1 messages) ---- */

/* The error of `CodeGeneratorRequest::decode(raw)` as Display shows it ("failed to decode Protobuf
 * message: ..."), or NULL if prost decodes the request. */
const char *prost_decode_request(Str raw);

/* ---- params.c (protoc-gen-prost src/lib.rs: Params, Param, InvalidParameter) ---- */

enum { PROST_PARAMETER, PROST_VALUE, PROST_KEY_VALUE };

/* "param", "param=value" or "param=key=value" (value: "\," and "\\" unescaped). */
typedef struct {
	int kind;
	const char *param, *key, *value;
} ProstParam;

/* Params::from_protoc_plugin_opts: the matches of the parameter regex in s. */
int prost_params_parse(Str s, ProstParam **out);
/* The value of a Value or KeyValue parameter, NULL for a Parameter (Param::value). */
const char *prost_param_value(const ProstParam *p);
/* Matches a boolean flag: `name` or `name=true` set *on, `name=false` is accepted. Returns false
 * if p is not this flag. */
bool prost_param_flag(const ProstParam *p, const char *name, bool *on);
/* The error text of an unhandled parameter: "invalid parameter: <param>[=<key>][=<value>]". */
char *prost_invalid_parameter(const ProstParam *p);

/* ---- request.c (protoc-gen-prost src/lib.rs: ModuleRequestSet, ModuleRequest) ---- */

typedef struct {
	const char *proto_package_name; /* of the first file of the module */
	ProstModule *module;
	bool flat_output_dir;
	const char *output_filename; /* NULL unless a file of the module is generated */
	Vec files;                   /* PbMsg *: FileDescriptorProto */
	Vec raw;                     /* Str *: their bytes as in the request */
} ProstModuleRequest;

typedef struct {
	Vec requests; /* ProstModuleRequest *, ordered by module */
} ProstModuleRequestSet;

/* Groups all proto files of req (raw: the serialized request) by module. default_package_filename
 * names the file of the root module (NULL: "_"). */
ProstModuleRequestSet *prost_module_request_set_new(PbMsg *req, Str raw, const char *default_package_filename,
						    bool flat_output_dir);
ProstModuleRequest *prost_module_request_set_for_module(const ProstModuleRequestSet *set, const ProstModule *m);
/* "" with flat_output_dir, else the module parts joined with "/" plus "/" (if any). */
char *prost_module_request_output_dir(const ProstModuleRequest *r);
/* output_dir + output_filename, or NULL. */
char *prost_module_request_output_filepath(const ProstModuleRequest *r);

/* ---- response.c (protoc-gen-prost src/generator.rs, src/generator/file_descriptor_set.rs) ---- */

/* A CodeGeneratorResponse.File. */
typedef struct {
	const char *name, *insertion_point;
	Str content;
} ProstFile;

/* ModuleRequest::write_to_file / append_to_file (insertion point "module"): NULL if the module
 * has no output file. */
ProstFile *prost_write_to_file(const ProstModuleRequest *r, Str content);
ProstFile *prost_append_to_file(const ProstModuleRequest *r, Str content);
/* FileDescriptorSetGenerator: for every module with an output file, appends FILE_DESCRIPTOR_SET
 * (the module's files as in the request) to it. */
void prost_file_descriptor_set_generate(const ProstModuleRequestSet *set, Vec *files);
/* GeneratorResultExt::unwrap_codegen_response: the files (ProstFile *) or the error, with
 * supported_features = FEATURE_PROTO3_OPTIONAL. */
void prost_write_response(Buf *out, const Vec *files);
void prost_write_error(Buf *out, Str error);

#endif
