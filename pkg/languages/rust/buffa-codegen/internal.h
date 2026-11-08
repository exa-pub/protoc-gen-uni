/* Copyright Anthropic, PBC. Licensed under the Apache License, Version 2.0.
 *
 * Internal declarations of the buffa-codegen 0.9.2 port (unity build, buffa_codegen.c). Descriptors
 * are the pkg/pb messages of the request, read like buffa reads its generated descriptor structs
 * (`field.name.as_deref().unwrap_or("")`). Token streams are lists of syn nodes (`Vec` of
 * PpNode *) of the kind the quoting site produces: items, statements, match arms, fields. */
#ifndef BUFFA_CODEGEN_INTERNAL_H
#define BUFFA_CODEGEN_INTERNAL_H

#include "buffa_codegen.h"

#include "prettyplease.h"
#include "proc_macro2.h"
#include "proto.h"
#include "rust_std.h"

#include <stdlib.h>
#include <string.h>

/* ---- descriptor.proto field numbers beyond pb.h ---- */
enum {
	FOPT_FEATURES = 50, MOPT_MESSAGE_SET = 1, MOPT_MAP_ENTRY = 7, MOPT_FEATURES = 12,
	FLDOPT_PACKED = 2, FLDOPT_DEPRECATED = 3, FLDOPT_DEBUG_REDACT = 16, FLDOPT_FEATURES = 21,
	ONEOFOPT_FEATURES = 1, ENUMOPT_FEATURES = 7, EVOPT_FEATURES = 2, ODP_OPTIONS = 2,
};

/* buffa::editions::ResolvedFeatures; values are those of google.protobuf.FeatureSet
 * (PROTO_PRESENCE_* ... in proto.h). */
typedef struct {
	uint8_t presence, enum_type, rep, utf8, msg_enc, json;
} Feat;

/* context::MessageScope */
typedef struct {
	const char *pkg;  /* current_package */
	const char *fqn;  /* proto_fqn, no leading dot */
	Feat features;
	int nesting;
} Scope;

/* message::RegistryPaths: paths (strings) relative to the scope of their consts. */
typedef struct {
	Vec json_any, text_any, json_ext, text_ext;
} Reg;

/* message::ReexportCandidate */
typedef struct {
	const char *name;
	Vec items;
} Reexport;

/* imports::ImportsPhase */
enum { IMPORTS_OFF, IMPORTS_COLLECTING, IMPORTS_RESOLVING };

/* context::CodeGenContext (one per request: the global `cx`). */
typedef struct {
	const BcConfig *config;
	const PbMsg **files;
	int n_files;
	Map type_map;     /* ".pkg.Msg" -> rust path */
	Map package_of;   /* ".pkg.Msg" -> package */
	Map enum_closed;  /* ".pkg.E" -> (void *)1 open, (void *)2 closed */
	Map enum_first;   /* ".pkg.E" -> int * (only with enum-type overrides) */
	Map comments;     /* "pkg.Msg.field" -> char * */
	Map nested_mods;  /* ".pkg.Msg" -> module name */
	Map unboxed;      /* oneof variant ".pkg.Msg.field" -> (void *)1 */
	Map field_renames;/* "<name>\x01<number>" -> name */
	Map oneof_keep;   /* oneof name -> (void *)1 */
	int imports_phase;
	Map imports_collected; /* path -> (void *)1 */
	Vec imports_collected_list;
	Map imports_resolved;  /* path -> short */
	Vec imports_bindings;  /* char *[2] pairs (short, path), sorted by short */
} Ctx;

static Ctx cx;

#define CFG (cx.config)
#define SENTINEL_MOD "__buffa"

/* ---- descriptor helpers ---- */

static inline const char *dname(const PbMsg *m) { return pb_str(m, 1); } /* every *DescriptorProto: name = 1 */
static inline int fd_type(const PbMsg *f) { return (int)pb_int(f, FD_TYPE, PT_DOUBLE); }
static inline int fd_label(const PbMsg *f) { return (int)pb_int(f, FD_LABEL, LABEL_OPTIONAL); }
static inline int fd_number(const PbMsg *f) { return (int)pb_int(f, FD_NUMBER, 0); }
static inline const char *fd_type_name(const PbMsg *f) { return pb_has(f, FD_TYPE_NAME) ? pb_str(f, FD_TYPE_NAME) : NULL; }
static inline bool fd_has_oneof(const PbMsg *f) { return pb_has(f, FD_ONEOF_INDEX); }
static inline int fd_oneof(const PbMsg *f) { return (int)pb_int(f, FD_ONEOF_INDEX, 0); }
static inline bool fd_proto3_optional(const PbMsg *f) { return pb_int(f, FD_PROTO3_OPTIONAL, 0) != 0; }
static inline int n_fields(const PbMsg *m) { return pb_count(m, DP_FIELD); }
static inline const PbMsg *field_at(const PbMsg *m, int i) { return pb_msg_at(m, DP_FIELD, i); }
static inline bool msg_is_map_entry(const PbMsg *m) { return pb_int(pb_msg(m, DP_OPTIONS), MOPT_MAP_ENTRY, 0) != 0; }
static inline const char *join_fqn(const char *pkg, const char *name) { return *pkg ? xcat(pkg, ".", name) : name; }

/* ---- features.rs (buffa-descriptor features.rs) ---- */
static Feat feat_for_file(const PbMsg *file);
static Feat feat_child(Feat parent, const PbMsg *features);
static const PbMsg *msg_features(const PbMsg *m) { return pb_msg(pb_msg(m, DP_OPTIONS), MOPT_FEATURES); }
static const PbMsg *field_features(const PbMsg *f) { return pb_msg(pb_msg(f, FD_OPTIONS), FLDOPT_FEATURES); }
static const PbMsg *enum_features(const PbMsg *e) { return pb_msg(pb_msg(e, EDP_OPTIONS), ENUMOPT_FEATURES); }
static Feat feat_resolve_field(const PbMsg *field, Feat parent);

/* ---- idents.rs, oneof.rs naming ---- */
static bool is_rust_keyword(const char *s);
static char *make_field_ident(const char *name);
static char *escape_mod_ident(const char *name);
static char *rust_path(const char *path); /* rust_path_to_tokens: keyword segments raw */
static char *to_snake_case(const char *s);
static char *to_pascal_case(const char *s);
static char *to_upper_camel_case(const char *s);
static char *to_shouty_snake_case(const char *s);
static char *upper_ascii(const char *s);

/* ---- context.rs ---- */
static void ctx_init(const BcConfig *c, const PbMsg *req, const Vec *to_generate);
static const char *ctx_comment(const char *fqn);
static __attribute__((unused)) const char *ctx_rust_type(const char *fqn);
static const char *ctx_rust_type_relative(const char *fqn, const char *pkg, int nesting);
typedef struct {
	const char *to_package, *within_package;
	bool is_extern;
} SplitPath;
static __attribute__((unused)) bool ctx_rust_type_relative_split(const char *fqn, const char *pkg, int nesting, SplitPath *out);
static int ctx_is_enum_closed(const char *fqn); /* -1 unknown, 0 open, 1 closed */
static char *ctx_nested_module_name(const char *pkg, const char *name);
static const char *ctx_field_rust_name(const char *name, int number);
static char *ctx_field_ident(const char *name, int number);
static const char *ctx_oneof_rust_name(const char *name);
static char *ctx_oneof_ident(const char *name);
static const char *ctx_field_rename_note(const char *name, int number);
static void ctx_message_non_snake_attr(const PbMsg *msg, Vec *attrs);
static const char *ctx_root_path(const char *path, int nesting);
static bool ctx_oneof_unboxed(const char *variant_fqn);
enum { ANC_ONEOF, ANC_VIEW, ANC_VIEW_ONEOF, ANC_LAZY_VIEW };
static char *ancillary_prefix(int kind, const char *pkg, const char *fqn, int from_nesting);
static const char *prefixed_type_name(const char *proto_name);

/* imports.rs ImportResolver: the qualified (or short) path of a runtime type at a depth */
#define OPTION_PATH "::core::option::Option"
#define STRING_PATH "::buffa::alloc::string::String"
#define VEC_PATH "::buffa::alloc::vec::Vec"
#define MESSAGE_FIELD_PATH "::buffa::MessageField"
#define ENUM_VALUE_PATH "::buffa::EnumValue"
#define HASHMAP_PATH "::buffa::__private::HashMap"

/* ---- comments.rs ---- */
static void comments_collect(const PbMsg *file);
static void doc_attrs(Vec *attrs, const char *comment, const char *scope_fqn);
static void doc_attrs_with_tag(Vec *attrs, const char *comment, const char *tag, const char *scope_fqn);

/* ---- feature_gates.rs ---- */
static void cfg_block(Vec *out, Vec *items, const char *gate);

/* ---- node helpers (quote! glue, quote.c) ---- */
static PpNode *lit_str(const char *s);
static PpNode *lit_strn(Str s);
static PpNode *lit_i32(int64_t v);
static PpNode *lit_u32(uint64_t v);
static PpNode *lit_suffixed(int64_t v, const char *suffix);
static PpNode *lit_usuffixed(uint64_t v, const char *suffix);
static PpNode *doc_attr(const char *text);
static PpNode *attr_src(const char *path, const char *tokens); /* #[path(tokens)] */
static PpNode *attr_word(const char *path);                    /* #[path] */
static PpNode *mac_src(const char *path, int delim, const char *tokens);
static PpNode *self_f(const char *field);
static PpNode *arg(const char *name, PpNode *ty);
static PpNode *ty_ref(PpNode *ty);
static PpNode *ty_ref_mut(PpNode *ty);
static PpNode *ty_static_str(void);
static PpNode *ty_impl(const char *path);
static PpNode *ty_g1(const char *path, PpNode *arg);
static PpNode *ty_g2(const char *path, PpNode *a, PpNode *b);
static PpNode *e_call1(const char *f, PpNode *a);
static PpNode *e_call2(const char *f, PpNode *a, PpNode *b);
static PpNode *e_call3(const char *f, PpNode *a, PpNode *b, PpNode *c);
static PpNode *e_some(PpNode *e);
static PpNode *e_none(void);
static PpNode *e_ok_unit(void);
static PpNode *e_meth0(PpNode *recv, const char *m);
static PpNode *e_meth1(PpNode *recv, const char *m, PpNode *a);
static PpNode *s_semi(PpNode *e);
static PpNode *s_expr(PpNode *e);
static PpNode *s_let(const char *name, bool mut, PpNode *init);
static PpNode *allow_unused_use(const char *tree);
static PpNode *item_mod_super(const char *name, Vec *items); /* pub mod name { #[allow(unused_imports)] use super::*; items } */
static void push_all(PpNode *parent, const Vec *nodes);
static void attrs_all(PpNode *node, const Vec *attrs);
static void vcat(Vec *dst, const Vec *src);
static PpNode *fn_self(const char *name, bool mut, PpNode *ret, PpNode *block);
static PpNode *const_static_str(const char *name, const char *value);
static PpNode *e_qpath(PpNode *self_ty, const char *trait, const char *rest);
static PpNode *e_call1_q(PpNode *self_ty, const char *trait, const char *method, PpNode *a);

#endif
