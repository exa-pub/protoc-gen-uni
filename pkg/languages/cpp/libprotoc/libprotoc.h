// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 36.2: the parts of protobuf's C++ library (https://github.com/protocolbuffers/protobuf,
 * tag v36.2) that the protoc plugins built from it (buf.build/protocolbuffers/<lang> v36.2) rely on,
 * ported to C with the same semantics:
 *
 *   - plugin.c: compiler/plugin.cc PluginMain/GenerateCode and compiler/code_generator.cc (the
 *     response, supported features and editions, parameter parsing);
 *   - descriptor.c: descriptor.cc DescriptorPool/DescriptorBuilder (BuildFile with its validation
 *     and error texts, feature resolution, CopyTo) into the descriptor model of pkg/proto, which
 *     has the accessors of descriptor.h;
 *   - feature_resolver.c: feature_resolver.cc (CompileDefaults, Create with its validation,
 *     lifetimes; GetEditionFeatureSetDefaults and MergeFeatures are pkg/proto's);
 *   - retention.c: compiler/retention.cc (StripSourceRetentionOptions);
 *   - message.c: the wire format of generated and dynamic messages (generated_message_*,
 *     wire_format.cc, dynamic_message.cc semantics) over pkg/pb PbMsg values;
 *   - embedded.c: the serialized descriptors compiled into protoc_lib (descriptor.proto,
 *     plugin.proto, the pb.cpp/pb.java/pb.csharp feature sets, pb.file.cpp, pb.enumvalue.json);
 *   - printer.c: io/printer.{h,cc} (the legacy Print($var$) API, and Emit with Subs, callbacks
 *     and annotations collected as GeneratedCodeInfo);
 *   - debug_string.c: the DebugString methods of descriptor.cc (first lines) and the
 *     TextFormat field value printer of their options;
 *   - source_location.c: FileDescriptor::GetSourceLocation (descriptor.cc);
 *   - reflection.c: DynamicMessageFactory and Reflection::ListFields for builder-pool messages;
 *   - compiler/java/: the shared core of compiler/java (libprotoc_java.h, its own library);
 *   - objectivec_names.c: compiler/objectivec/names.cc (objectivec_names.h), the ObjC names shared
 *     by the objc generator and grpc's objective_c plugin;
 *   - php_names.c: compiler/php/names.cc (php_names.h), the PHP class names shared by the php
 *     generator and grpc's php plugin;
 *   - csharp_names.c: compiler/csharp/names.cc (csharp_names.h), the C# names shared by the
 *     csharp generator and grpc's csharp plugin;
 *   - cpp_names.c: compiler/cpp/names.h (cpp_names.h), the C++ names shared by the cpp
 *     generator and grpc's cpp plugin;
 *   - scc.c: compiler/scc.h (SCCAnalyzer);
 *   - libprotoc_<version>.c: what another protobuf version does differently (LpVersion), each in
 *     its own library; printer_3.21.12.c: the io::Printer of 3.21.12 (printer_3.21.12.h), in the
 *     library of that version;
 *   - strutil.c: absl escaping (CEscape, CHexEscape, CUnescape, Base64Escape), io/strtod.cc (SimpleDtoa,
 *     SimpleFtoa, NoLocaleStrtod) and absl number formatting.
 *
 * Unity build (libprotoc.c): everything is static except the API below. Memory comes from the
 * pkg/core arena.
 *
 * Messages (options, features, descriptor protos) are pkg/pb PbMsg values whose definition comes
 * from a descriptor pool (LpMsgDef); the pb.h accessors and the proto_msg_* operations of
 * pkg/proto work on them. */
#ifndef LIBPROTOC_H
#define LIBPROTOC_H

#include "proto.h"

/* ---- descriptors (descriptor.h) ----
 *
 * The descriptors are those of pkg/proto (proto.h), whose model is protoc's: libprotoc builds
 * them (descriptor.c) and the FieldDescriptor & co. predicates are the proto_* functions. Options
 * hold what upstream keeps in options(): the features move to proto_features, and the legacy
 * FieldOptions.ctype of a field is kept in its `user` (descriptor.c field_legacy_ctype). */

enum {
	LP_EDITION_UNKNOWN = PROTO_EDITION_UNKNOWN, LP_EDITION_LEGACY = PROTO_EDITION_LEGACY,
	LP_EDITION_PROTO2 = PROTO_EDITION_PROTO2, LP_EDITION_PROTO3 = PROTO_EDITION_PROTO3,
	LP_EDITION_2023 = PROTO_EDITION_2023, LP_EDITION_2024 = PROTO_EDITION_2024,
	LP_EDITION_2026 = PROTO_EDITION_2026, LP_EDITION_UNSTABLE = PROTO_EDITION_UNSTABLE,
	LP_EDITION_MAX = PROTO_EDITION_MAX,
};

/* FieldDescriptor::CppType */
enum {
	LP_CPPTYPE_INT32 = 1, LP_CPPTYPE_INT64 = 2, LP_CPPTYPE_UINT32 = 3, LP_CPPTYPE_UINT64 = 4,
	LP_CPPTYPE_DOUBLE = 5, LP_CPPTYPE_FLOAT = 6, LP_CPPTYPE_BOOL = 7, LP_CPPTYPE_ENUM = 8,
	LP_CPPTYPE_STRING = 9, LP_CPPTYPE_MESSAGE = 10,
};

typedef struct LpPool LpPool;
typedef ProtoFile LpFile;
typedef ProtoMessage LpMessage;
typedef ProtoField LpField;
typedef ProtoOneof LpOneof;
typedef ProtoEnum LpEnum;
typedef ProtoEnumValue LpEnumValue;
typedef ProtoService LpService;
typedef ProtoMethod LpMethod;
typedef ProtoExtensionRange LpExtensionRange;
typedef ProtoReservedRange LpReservedRange;

int lp_field_cpp_type(const LpField *f);
/* FieldDescriptor::DefaultValueAsString (has_default_value must be set). */
char *lp_field_default_value_as_string(const LpField *f, bool quote_string_type);
/* internal::ShortEditionName: "PROTO2", "2023", ... ("" for unknown values). */
const char *lp_edition_name(int edition);
/* FieldDescriptor::camelcase_name() */
char *lp_field_camelcase_name(const LpField *f);
/* internal::cpp::IsGroupLike: a TYPE_GROUP field whose name is its type's name lower-cased, with
 * the type declared in the same file and scope. */
bool lp_field_is_group_like(const LpField *f);
/* FieldDescriptor::legacy_proto_ctype(): the FieldOptions.ctype the field had (moved out of its
 * options), -1 if none (has_legacy_proto_ctype). */
int lp_field_legacy_ctype(const LpField *f);
/* FileDescriptor::CopyTo: the FileDescriptorProto of a file (no source_code_info). */
PbMsg *lp_file_to_proto(const LpFile *file);

/* SourceLocation (descriptor.h) and FileDescriptor::GetSourceLocation: the table of a file's
 * locations by path is built by lp_source_locations and kept by the caller. */
typedef struct {
	int start_line, end_line, start_column, end_column;
	Str leading_comments, trailing_comments;
	const Str *leading_detached_comments; /* n_leading_detached_comments elements */
	int n_leading_detached_comments;
} LpSourceLocation;
typedef struct LpSourceLocations LpSourceLocations;
LpSourceLocations *lp_source_locations(const LpFile *file);
/* GetSourceLocation of a descriptor of that file (debug_string.c: source_location.c). */
bool lp_get_source_location(const LpSourceLocations *t, const ProtoDesc *d, LpSourceLocation *out);
/* FileDescriptor::GetSourceLocation(path, out): the location of a path of n elements. */
bool lp_get_source_location_path(const LpSourceLocations *t, const int32_t *path, int n, LpSourceLocation *out);

/* The first line (without the newline) of FieldDescriptor::DebugString(),
 * EnumValueDescriptor::DebugString() and MethodDescriptor::DebugString() (default
 * DebugStringOptions). */
char *lp_field_debug_string_first_line(const LpField *f);
char *lp_enum_value_debug_string_first_line(const LpEnumValue *v);
char *lp_method_debug_string_first_line(const LpMethod *m);

/* DynamicMessageFactory: data parsed as a message of type full_name of the file's pool; NULL if
 * the pool does not define the type or the data does not parse. */
PbMsg *lp_dynamic_message_parse(const LpFile *file, const char *full_name, Str data);
/* Reflection: the descriptor of slot i of a message with a pool definition (extensions
 * included; the slots are in field number order, like ListFields). */
const LpField *lp_msg_slot_field(const PbMsg *m, int i);

/* ---- protobuf versions ----
 *
 * libprotoc implements protobuf 36.2. A plugin built with another protobuf version names it in
 * LpGenerator.version: what that version does differently from 36.2. Each version is defined by
 * the versioned files of this library (libprotoc_<version>.c), compiled into their own library
 * (cpp_libprotoc_<version>) that the generator links, so only the generators of that version
 * carry it. NULL is 36.2. A new version describes its difference here and in its file.
 *
 * The generated pool of an older version is described against a newer one (base): a version's
 * builtin_file edits what the builtin_file of its base made of the 36.2 files, so the next older
 * version only adds its own difference (and links the library of its base). */
typedef struct LpVersion LpVersion;
struct LpVersion {
	const char *name;          /* "33.4" */
	int maximum_known_edition; /* MaximumKnownEdition() (compiler/code_generator.h) */
	/* The newer version whose generated pool builtin_file starts from (NULL: 36.2). */
	const LpVersion *base;
	/* The generated pool of the version's protoc_lib, from the one of its base (embedded.c):
	 * called for each builtin file i (0: descriptor.proto, then cpp_features, java_features,
	 * c_sharp_features, cpp_file_options, json_enumvalue_options and plugin.proto) the base links,
	 * before the pool builds it; edits the FileDescriptorProto (pkg/pb definitions) into the
	 * version's and returns false for a file the version does not link. lp_desc_find and
	 * lp_desc_remove_if help. */
	bool (*builtin_file)(int i, PbMsg *file);
	/* DescriptorBuilder::ValidateOptions(FileDescriptor): `java_multiple_files` in editions 2024
	 * and later is an error of its own (36.2 removes the option with its feature_support). */
	bool java_multiple_files_error;
	/* Feature lifetimes (descriptor.cc, FeatureResolver::ValidateFeatureLifetimes) as before
	 * 34: only the features of each descriptor are validated (not its options), only feature
	 * extensions are recursed into, and the errors start with "Feature " without removal_error. */
	bool legacy_feature_lifetimes;
	/* The prefixes the log of the version writes before the lines of the default error
	 * collector of BuildFile (NULL: none; the ABSL_LOG prefixes of 36.2 have times and thread
	 * ids): log_file_prefix before "Invalid proto descriptor for file ...", log_error_prefix
	 * before each "  <element>: <error>". */
	const char *log_file_prefix, *log_error_prefix;
	/* The prefix of the warnings of BuildFile (GOOGLE_LOG(WARNING) "<file> <element>:
	 * <warning>"), which the log has before the errors of a file that fails; NULL: warnings are
	 * not logged. */
	const char *log_warning_prefix;
	/* No editions (3.x): a file of unknown syntax gets no feature resolver, nor its error. */
	bool no_editions;
	/* Instead of CheckFieldJsonNameUniqueness (ValidateOptions(Descriptor)), the version's checks
	 * of the JSON names of a message's fields: appends the errors (const char *, element: the
	 * message) to errors. */
	void (*field_json_name_errors)(const LpMessage *message, Vec *errors);
	/* Instead of 36.2's error of CheckEnumValueUniqueness, the version's for value, whose name
	 * is previous's without the enum name prefix and case (different numbers): the error, with
	 * *warning set when it is only a warning. The check then runs in BuildEnum (as in 3.x), not
	 * in ValidateOptions. */
	const char *(*enum_value_name_error)(const LpEnum *e, const LpEnumValue *value, const LpEnumValue *previous,
					     bool *warning);
	/* Map fields as before 36 (descriptor.cc ValidateOptions(FieldDescriptor) and
	 * ValidateMapEntry): a map field that is not repeated, or whose entry is not named
	 * <CamelName>Entry in the field's scope, only gets "map_entry should not be set explicitly";
	 * no "Groups cannot be map entries" nor "synthetic MapEntry" errors. */
	bool legacy_map_entry;
	/* DescriptorBuilder as in 3.19: no limits on the package (length 511: "Package name is too
	 * long", 100 dots: "Exceeds Maximum Package Depth") nor on message nesting ("Reached maximum
	 * recursion limit for nested messages."), nested types built right after the fields (before
	 * the enums, ranges and extensions), no "Suggested field numbers" error, and DetectMapConflicts
	 * reports every conflicting map entry. */
	bool legacy_descriptor_builder;
	/* A field number used twice in a message is "Field number N has already been used in ... by
	 * field ...." without the next available field number (3.x). */
	bool legacy_field_number_error;
	/* CodeGenerator::BuildFeatureSetDefaults before 30: the defaults span the generator's own
	 * edition range [minimum_edition, maximum_edition] when it supports editions, else [PROTO2,
	 * maximum_known_edition] (MinimumAllowedEdition(), MaximumAllowedEdition()). */
	bool generator_edition_defaults;
	/* The words that compiler/php/names.cc of 36.2 reserves and the version does not
	 * (php_names.c, ReservedNamePrefix), NULL-terminated; NULL: none. */
	const char *const *php_unreserved_names;
};

/* Helpers of builtin_file: the element of the repeated message field num of m whose name
 * (field 1) is name, or NULL; removes the elements of a repeated message field for which drop
 * returns true. */
PbMsg *lp_desc_find(const PbMsg *m, uint32_t num, const char *name);
void lp_desc_remove_if(PbMsg *m, uint32_t num, bool (*drop)(const PbMsg *e, const void *arg), const void *arg);

/* protobuf 35.1 (libprotoc_35.1.c, library cpp_libprotoc_35.1): grpc/{cpp,csharp,objc,php,
 * python,ruby} v1.85.1. */
extern const LpVersion lp_version_35_1;
/* protobuf 33.4 (libprotoc_33.4.c, library cpp_libprotoc_33.4): protocolbuffers/js v4.0.3. */
extern const LpVersion lp_version_33_4;
/* protobuf 29.1 (libprotoc_29.1.c, library cpp_libprotoc_29.1, on top of 33.4): grpc/web
 * v2.1.1. */
extern const LpVersion lp_version_29_1;
/* protobuf 3.21.12 (libprotoc_3.21.12.c, library cpp_libprotoc_3.21.12): community/protobuf-c
 * v1.5.2. The library also has the io::Printer of that version (printer_3.21.12.h). */
extern const LpVersion lp_version_3_21_12;
/* The descriptor checks of 3.21.12 (LpVersion.field_json_name_errors, enum_value_name_error),
 * which the older versions share. */
void lp_3_21_12_field_json_name_errors(const LpMessage *message, Vec *errors);
const char *lp_3_21_12_enum_value_name_error(const LpEnum *e, const LpEnumValue *value, const LpEnumValue *previous,
					     bool *warning);
/* protobuf 3.19.1 (libprotoc_3.19.1.c, library cpp_libprotoc_3.19.1, on top of 3.21.12):
 * grpc/node v1.13.1. */
extern const LpVersion lp_version_3_19_1;

/* ---- SCC analyzer (compiler/scc.h) ----
 *
 * SCCAnalyzer<DepsGenerator>: the strongly connected components of the message graph given by
 * deps (Tarjan, iterative); each SCC lists its messages sorted by full name. */
typedef struct LpScc LpScc;
struct LpScc {
	const LpMessage **descriptors; /* sorted by full_name */
	int n;
	const LpScc **children; /* in the order AddChildren finds them */
	int n_children;
};
/* DepsGenerator: appends the messages desc depends on to out (none may be NULL). */
typedef void (*LpSccDeps)(const LpMessage *desc, Vec *out, void *arg);
typedef struct LpSccAnalyzer LpSccAnalyzer;
LpSccAnalyzer *lp_scc_analyzer_new(LpSccDeps deps, void *arg);
const LpScc *lp_scc_get(LpSccAnalyzer *a, const LpMessage *desc);
static inline const LpMessage *lp_scc_representative(const LpScc *scc) { return scc->descriptors[0]; }

/* ---- plugin (compiler/plugin.cc, compiler/code_generator.h) ---- */

typedef struct LpContext LpContext;

/* CodeGenerator */
typedef struct LpGenerator LpGenerator;
struct LpGenerator {
	/* argv[0] of the upstream plugin, for the messages it prints ("protoc-gen-ruby"). */
	const char *plugin_name;
	/* Generate(file, parameter, context, error): returns false and/or writes the error (a
	 * std::string, may contain NUL) on failure. */
	bool (*generate)(const LpGenerator *g, const LpFile *file, const char *parameter, LpContext *ctx,
			 Buf *error);
	/* GenerateAll; NULL: the default (Generate per file, errors prefixed with the file name). */
	bool (*generate_all)(const LpGenerator *g, LpFile *const *files, int n, const char *parameter,
			     LpContext *ctx, Buf *error);
	uint64_t supported_features;
	int minimum_edition, maximum_edition;
	/* GetFeatureExtensions: full names of FeatureSet extensions ("pb.cpp"), NULL-terminated. */
	const char *const *feature_extensions;
	/* The protobuf version the plugin is built with (NULL: 36.2). */
	const LpVersion *version;
	void *user;
};

/* PluginMain + GenerateCode: parses the serialized request raw (with the generated message
 * semantics of protoc_lib) using parameter as CodeGeneratorRequest.parameter, builds the pool, runs
 * the generator and writes the serialized response to out. Returns NULL, or the message upstream
 * prints to stderr before exiting with status 1 when the request cannot be parsed. Errors that
 * upstream only prints (invalid descriptors, unknown files to generate) go to
 * CodeGeneratorResponse.error. */
const char *lp_plugin_main(Str raw, Str parameter, const LpGenerator *gen, Buf *out);

/* GeneratorContext::Open: adds a file to the response; its content is the returned buffer. */
Buf *lp_context_open(LpContext *ctx, const char *filename);
/* GeneratorContext::OpenForInsert: adds a file with CodeGeneratorResponse.File.insertion_point
 * (protoc inserts its content at that insertion point of a file generated in the same run). */
Buf *lp_context_open_for_insert(LpContext *ctx, const char *filename, const char *insertion_point);

/* GeneratorContext::GetCompilerVersion: CodeGeneratorRequest.compiler_version (0.0.0 and no
 * suffix when the request has none). */
typedef struct {
	int32_t major, minor, patch;
	Str suffix;
} LpCompilerVersion;
LpCompilerVersion lp_context_compiler_version(const LpContext *ctx);

/* ParseGeneratorParameter: "k1=v1,k2,..." split on ',' (empty parts skipped), then on the first
 * '='. Returns the number of pairs; *keys and *values are arena arrays. */
int lp_parse_generator_parameter(const char *text, char ***keys, char ***values);
/* StripProto (code_generator_lite.cc): filename without ".protodevel" or else ".proto". */
char *lp_strip_proto(const char *filename);
/* IsKnownFeatureProto (code_generator_lite.cc): google/protobuf/cpp_features.proto and
 * java_features.proto. */
bool lp_is_known_feature_proto(const char *filename);

/* ---- retention (compiler/retention.h) ---- */

/* StripSourceRetentionOptions(file): the FileDescriptorProto of file (CopyTo, without
 * source_code_info) with source-retention options removed. */
PbMsg *lp_strip_source_retention_options(const LpFile *file);
/* MessageLite::SerializeToString of a message with a pool definition. */
void lp_msg_serialize(const PbMsg *m, Buf *out);

/* ---- io::Printer (legacy Print API and Emit) ---- */

typedef struct LpPrinter LpPrinter;
typedef struct LpSub LpSub;

/* A frame of variables (WithVars, WithDefs): subs[0..n) looked up by key. */
typedef struct {
	LpSub *subs;
	int n;
} LpVarFrame;

struct LpPrinter {
	Buf *out;
	char delim;
	size_t indent;
	size_t spaces_per_indent; /* Options::spaces_per_indent (2 after lp_printer_init) */
	bool at_start_of_line;
	/* The plugin is built without NDEBUG (Bazel fastbuild): the checks of the legacy Print that
	 * release builds skip (checks_are_debug_only) fail() instead (an undefined variable). */
	bool debug_checks;
	/* Emit state (printer.c) */
	size_t base;            /* out->n at init: sink_.bytes_written() is out->n - base */
	Buf *annotations;       /* AnnotationProtoCollector<GeneratedCodeInfo>: the serialized
				 * GeneratedCodeInfo (its annotation entries); NULL: no collector */
	LpVarFrame *frames;     /* var_lookups_ (innermost last) */
	int n_frames, cap_frames;
	size_t paren_depth;
	size_t *paren_omit;     /* paren_depth_to_omit_ */
	int n_paren_omit, cap_paren_omit;
	void *tok;              /* TokenizeFormat scratch stacks */
	void *subst;            /* substitutions_ of the last legacy Print (with a collector) */
	int n_subst, cap_subst;
};

void lp_printer_init(LpPrinter *p, Buf *out, char delim);
/* Printer::Print(text, "var1", value1, "var2", value2, ..., NULL). */
void lp_print(LpPrinter *p, const char *text, ...);
void lp_printv(LpPrinter *p, const char *text, int n_vars, const char *const *vars);
void lp_indent(LpPrinter *p);
void lp_outdent(LpPrinter *p);
/* Printer::WriteRaw: data as is (NUL bytes included), indented at the start of a line. */
void lp_write_raw(LpPrinter *p, Str data);
/* Printer::Annotate(varname, descriptor) after a legacy Print: annotates the range of varname's
 * substitution in the last Print call with the descriptor's path and file (nothing if varname
 * was not substituted or was substituted more than once, or without an annotation collector). */
void lp_annotate(LpPrinter *p, const char *varname, const void *desc);

/* The variable maps passed to Printer::Print(vars, text) (absl::flat_hash_map<string_view,
 * string>): pairs in insertion order, lp_vars_set overwrites (operator[]), lp_vars_insert keeps
 * an existing value (insert). Values are not copied. */
typedef struct {
	const char **kv; /* key, value, key, value, ... */
	int n, cap;      /* pairs */
} LpVars;
void lp_vars_set(LpVars *v, const char *key, const char *value);
void lp_vars_insert(LpVars *v, const char *key, const char *value);
/* NULL if absent */
const char *lp_vars_get(const LpVars *v, const char *key);
LpVars lp_vars_copy(const LpVars *v);
/* Printer::Print(vars, text) */
void lp_print_vars(LpPrinter *p, const LpVars *v, const char *text);

/* Printer::Sub: a string value (value != NULL) or a callback (cb, called with arg) that prints
 * through the printer. consume_after NULL is the default ("" for strings, ";," for callbacks): one
 * of its characters is dropped from the text right after the variable (WithSuffix). annotate: the
 * descriptor of AnnotatedAs (NULL: none), or annotate_file for a file-path record; semantic:
 * AnnotationCollector::Semantic + 1 (0: none). LP_SUB, LP_SUB_CB and LP_SUB_ANN build the common
 * cases. */
typedef void (*LpEmitCallback)(LpPrinter *p, void *arg);
struct LpSub {
	const char *key;
	const char *value;
	LpEmitCallback cb;
	void *arg;
	const char *consume_after;
	bool consume_parens_if_empty; /* ConditionalFunctionCall() */
	const void *annotate;         /* const ProtoDesc * */
	const char *annotate_file;
	int semantic;
	bool is_called;               /* internal: recursion check of callbacks */
};
#define LP_SUB(k, v) ((LpSub){.key = (k), .value = (v)})
#define LP_SUB_CB(k, f, a) ((LpSub){.key = (k), .cb = (f), .arg = (a)})
#define LP_SUB_ANN(k, v, d) ((LpSub){.key = (k), .value = (v), .annotate = (d)})

/* Printer::Emit(vars, format): pushes subs[0..n) as a frame (callbacks allowed), prints format
 * with raw-string indentation stripping and pops the frame. Format errors fail() (fatal checks
 * upstream). */
void lp_emit(LpPrinter *p, LpSub *subs, int n, const char *format);
/* Printer::Print(vars, text) with the frames of WithVars: pushes subs[0..n) as a frame and prints
 * text like the legacy Print (no raw strings, $ var$ spaces, format errors skipped), looking up
 * the variables in all frames (annotations of AnnotatedAs subs included); Annotate(varname) does
 * not see its substitutions. */
void lp_print_subs(LpPrinter *p, LpSub *subs, int n, const char *text);
/* Printer::FormatInternal(args, vars, format) (the Formatter of compiler/cpp): $1$ ... are
 * args[0] ...; ${1$ ... $}$ annotate with args[i], a serialized GeneratedCodeInfo.Annotation
 * (path, source_file, semantic). */
void lp_format_internal(LpPrinter *p, const char *const *args, int n_args, LpSub *vars, int n_vars, const char *format);
/* WithVars(subs) and its cleanup: frames nest; subs must live until the pop. */
void lp_push_vars(LpPrinter *p, LpSub *subs, int n);
void lp_pop_vars(LpPrinter *p);
/* LookupVar: the string value of a variable (fail() if undefined or a callback). */
const char *lp_lookup_var(LpPrinter *p, const char *var);

/* ---- strings (absl, io/strtod.h) ---- */

char *lp_cescape(Str s);
char *lp_chexescape(Str s);
/* absl::Utf8SafeCEscape: CEscape that passes bytes >= 0x80 through. */
char *lp_utf8_safe_cescape(Str s);
/* absl::CUnescape; returns false on invalid escapes. */
bool lp_cunescape(Str s, Str *out);
/* absl::Base64Escape (standard alphabet, padded) */
char *lp_base64_escape(Str s);
char *lp_simple_dtoa(double v);
char *lp_simple_ftoa(float v);
/* absl::SNPrintF("%.*g", prec, v) for finite v (exact decimal rounding, ties to even). */
char *lp_format_g(double v, int prec);
/* absl::StrCat(double) (numbers_internal::SixDigitsToBuffer): 6 significant digits like %g,
 * "inf", "-inf" and "nan" (no sign). */
char *lp_six_digits(double v);
/* io::NoLocaleStrtod (absl::from_chars syntax); *end receives the end of the parsed number. */
double lp_no_locale_strtod(const char *s, const char **end);

#endif
