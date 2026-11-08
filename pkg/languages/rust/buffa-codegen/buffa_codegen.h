/* buffa-codegen 0.9.2 (https://github.com/anthropics/buffa/tree/v0.9.2/buffa-codegen, Apache-2.0,
 * Copyright Anthropic, PBC): the code generator library behind protoc-gen-buffa (anthropics/buffa)
 * and connectrpc-codegen (connectrpc/rust). Output is printed with prettyplease 0.2.37
 * (pkg/languages/rust/formatters/prettyplease). Results are arena data.
 *
 * Port status: see README.md. */
#ifndef BUFFA_CODEGEN_H
#define BUFFA_CODEGEN_H

#include "pb.h"

/* (proto path, value) pairs of the config (extern_paths, feature_overrides). */
typedef struct {
	const char *key, *value;
} BcPair;

/* CodeGenConfig (lib.rs); bc_config_default sets CodeGenConfig::default(). Fields that only the
 * buffa-build API sets (custom representations, attributes, shared corpus context) are not
 * modeled. */
typedef struct {
	bool generate_views, lazy_views, preserve_unknown_fields, generate_json, generate_arbitrary;
	bool strict_utf8_mapping, allow_message_set, generate_text, emit_register_fn, file_per_package;
	bool gate_impls_on_crate_features, generate_with_setters, generate_reflection;
	bool generate_reflection_vtable, shared_descriptor_pool, gate_reflect_on_crate_feature;
	bool idiomatic_enum_aliases, idiomatic_imports, idiomatic_field_names;
	Vec extern_paths;         /* BcPair *: (".proto.path", "::rust::path") */
	Vec feature_overrides;    /* BcPair *: (".proto.path", "enum_type:OPEN") */
	Vec unboxed_oneof_fields; /* char * */
	Vec exclude_packages;     /* char * */
	const char *type_name_prefix;
	const char *feature_gate_json, *feature_gate_views, *feature_gate_text, *feature_gate_reflect;
} BcConfig;

void bc_config_default(BcConfig *c);
/* CodeGenConfig::reflect_feature_gate: the feature name reflection is gated on, or NULL. */
const char *bc_reflect_feature_gate(const BcConfig *c);

/* A generated file (GeneratedFile: name and content). */
typedef struct {
	const char *name;
	Str content;
} BcFile;

/* generate_with_diagnostics: the files (BcFile *) for the FileDescriptorProtos of req
 * (proto_file) and its file_to_generate, or the error as CodeGenError Display prints it. */
const char *bc_generate(const BcConfig *c, const PbMsg *req, Vec *files);

/* normalize_exclude_package: the package without a leading dot, or NULL with *err set. */
char *bc_normalize_exclude_package(const char *value, const char **err);
bool bc_package_is_excluded(const char *package, const Vec *excludes);

/* The element memory limit of the request decode (lib.rs element_memory_limit_opt): the last
 * `element_memory_limit=<bytes>|unlimited|max` entry of the parameter, else
 * TOOLING_ELEMENT_MEMORY_LIMIT (1 GiB; the BUFFA_ELEMENT_MEMORY_LIMIT environment variable is not
 * read: the remote plugins run without it). Returns NULL or the error. */
const char *bc_element_memory_limit_opt(Str parameter, uint64_t *limit);
#define BC_ELEMENT_MEMORY_LIMIT_OPT "element_memory_limit"
/* lib.rs decode_request: the element-memory check of decoding the request raw (the wire bytes,
 * already parsed by pkg/pb) with the limit of the parameter. Returns NULL or the error text
 * (decode_failure). */
const char *bc_decode_request(Str raw, Str parameter);

/* ---- the API connectrpc-codegen (connectrpc/rust) uses ---- */

/* context.rs CodeGenContext::for_generate over the proto_file and file_to_generate of req; the
 * context is global and lives until the next call. */
void bc_context_for_generate(const BcConfig *c, const PbMsg *req);
/* CodeGenContext::rust_type_relative: NULL when fqn (".pkg.Msg") is not in the descriptor set. */
const char *bc_rust_type_relative(const char *fqn, const char *current_package, int nesting);
/* CodeGenContext::rust_type_relative_split: false when fqn is unknown. */
bool bc_rust_type_relative_split(const char *fqn, const char *current_package, int nesting,
				 const char **to_package, const char **within_package);
#define BC_SENTINEL_MOD "__buffa"
/* idents.rs make_field_ident (r#type, self_) and rust_path_to_tokens as text (keyword segments
 * raw). */
char *bc_make_field_ident(const char *name);
char *bc_rust_path(const char *path);
/* lib.rs proto_path_to_stem, package_to_filename, package_to_mod_filename. */
char *bc_proto_path_to_stem(const char *proto_path);
char *bc_package_to_filename(const char *package);
char *bc_package_to_mod_filename(const char *package);
/* feature_gates.rs FeatureGateNames::is_valid_name */
bool bc_is_valid_feature_name(const char *name);

#endif
