// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The types of the cpp generator (src/google/protobuf/compiler/cpp/ headers and
 * src/google/protobuf/generated_message_tctable_gen.h, protobuf v36.2) shared by the files of
 * this unity build (generator.c includes them all). */
#ifndef CPP_V36_2_CPP_H
#define CPP_V36_2_CPP_H

#include "cpp_names.h"
#include "libprotoc.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* descriptor.proto option numbers read by the generator */
enum {
	CPP_FILEOPT_OPTIMIZE_FOR = 9, CPP_FILEOPT_CC_GENERIC_SERVICES = 16, CPP_FILEOPT_DEPRECATED = 23,
	CPP_MSGOPT_MESSAGE_SET_WIRE_FORMAT = 1, CPP_MSGOPT_DEPRECATED = 3, CPP_MSGOPT_MAP_ENTRY = 7,
	CPP_FOPT_DEPRECATED = 3, CPP_FOPT_LAZY = 5, CPP_FOPT_WEAK = 10, CPP_FOPT_UNVERIFIED_LAZY = 15,
	CPP_ENUMOPT_DEPRECATED = 3, CPP_EVOPT_DEPRECATED = 1,
	CPP_SVCOPT_DEPRECATED = 33, CPP_METHOPT_DEPRECATED = 33,
	/* FileOptions.OptimizeMode */
	OPT_SPEED = 1, OPT_CODE_SIZE = 2, OPT_LITE_RUNTIME = 3,
	/* pb.cpp (FeatureSet extension 1000) */
	PB_CPP = 1000, CPPF_LEGACY_CLOSED_ENUM = 1, CPPF_STRING_TYPE = 2, CPPF_ENUM_NAME_USES_STRING_VIEW = 3,
	CPPF_REPEATED_TYPE = 4,
	CPP_STRING_TYPE_UNKNOWN = 0, CPP_STRING_TYPE_VIEW = 1, CPP_STRING_TYPE_CORD = 2, CPP_STRING_TYPE_STRING = 3,
	/* FieldOptions.CType */
	CTYPE_STRING = 0, CTYPE_CORD = 1, CTYPE_STRING_PIECE = 2,
	/* GeneratedCodeInfo.Annotation.Semantic + 1 (LpSub.semantic) */
	SEM_NONE = 1, SEM_SET = 2, SEM_ALIAS = 3,
};

/* FieldDescriptor::CppStringType */
typedef enum { CPP_STR_VIEW, CPP_STR_CORD, CPP_STR_STRING } CppStringType;
/* FieldDescriptor::CppRepeatedType */
typedef enum { CPP_REP_REPEATED, CPP_REP_PROXY } CppRepeatedType;

/* ---- Subs: a std::vector<io::Printer::Sub> or a flat_hash_map<string_view, string> of
 * variables (subs_set overwrites like operator[], subs_insert keeps an existing one like
 * emplace). The array must not change while it is pushed on a printer. */
typedef struct {
	LpSub *v;
	int n, cap;
} Subs;

static void subs_push(Subs *s, LpSub sub);
static void subs_set(Subs *s, const char *key, const char *value);
static void subs_insert(Subs *s, const char *key, const char *value);
static const char *subs_get(const Subs *s, const char *key);
static char *cpp_utoa(unsigned long long v);
static Subs subs_copy(const Subs *s);
static void subs_append(Subs *dst, const Subs *src);

#define EMIT0(p, fmt) lp_emit((p), NULL, 0, (fmt))
#define EMIT(p, fmt, ...)                                                                          \
	do {                                                                                       \
		LpSub emit_subs_[] = {__VA_ARGS__};                                               \
		lp_emit((p), emit_subs_, (int)(sizeof emit_subs_ / sizeof *emit_subs_), (fmt));     \
	} while (0)
#define S_(k, v) LP_SUB((k), (v))
#define SCB(k, f, a) LP_SUB_CB((k), (f), (void *)(a))
#define SINT(k, n) LP_SUB((k), xfmt("%lld", (long long)(n)))
#define SUINT(k, n) LP_SUB((k), cpp_utoa((unsigned long long)(n)))
#define SBOOL(k, b) LP_SUB((k), (b) ? "1" : "0")
/* Sub(k, v).WithSuffix(s) */
#define SSUF(k, v, s) ((LpSub){.key = (k), .value = (v), .consume_after = (s)})
#define SCBSUF(k, f, a, s) ((LpSub){.key = (k), .cb = (f), .arg = (void *)(a), .consume_after = (s)})
/* Sub(k, v).AnnotatedAs(d) and AnnotatedAs({d, semantic}) */
#define SANN(k, v, d) LP_SUB_ANN((k), (v), (d))
#define SANNSEM(k, v, d, sem) ((LpSub){.key = (k), .value = (v), .annotate = (d), .semantic = (sem)})
/* WithVars(...) for a scope: push, then POP_VARS */
#define PUSH_SUBS(p, s) lp_push_vars((p), (s)->v, (s)->n)
#define POP_VARS(p) lp_pop_vars(p)

/* ---- options.h ---- */

typedef enum { ENFORCE_NONE, ENFORCE_SPEED, ENFORCE_CODE_SIZE, ENFORCE_LITE } EnforceOptimizeMode;

typedef struct MessageSCCAnalyzer MessageSCCAnalyzer;
typedef struct CppState CppState;

typedef struct {
	MessageSCCAnalyzer *scc_analyzer;
	const char *dllexport_decl;
	const char *runtime_include_base;
	const char *annotation_pragma_name;
	const char *annotation_guard_name;
	/* FieldListenerOptions */
	bool inject_field_listener_events;
	Vec forbidden_field_listener_events; /* const char * */
	EnforceOptimizeMode enforce_mode;
	int num_cc_files;
	bool proto_h;
	bool transitive_pb_h;
	bool annotate_headers;
	bool lite_implicit_weak_fields;
	bool descriptor_implicit_weak_messages;
	bool bootstrap;
	bool opensource_runtime;
	bool annotate_accessor;
	bool force_split;
	bool force_eagerly_verified_lazy;
	bool force_inline_string;
	bool strip_nonfunctional_codegen;
	bool experimental_use_micro_string;
	CppState *state; /* per request caches (HasBootstrapProblem) */
} Options;

struct CppState {
	Map bootstrap_problem; /* file name -> "1" (cached) or "0" (non_cached) */
	Map bootstrap_codesize; /* file name -> has_opt_codesize_extension of the cached run */
};

/* ---- helpers.h ---- */

typedef enum { ARENA_DTOR_NONE = 0, ARENA_DTOR_REQUIRED = 1 } ArenaDtorNeeds;
typedef enum { HASBIT_NONE, HASBIT_TRUE, HASBIT_HINT } HasbitMode;
typedef enum { UTF8_STRICT, UTF8_NONE } Utf8CheckMode;
typedef enum { FILE_PB_H, FILE_PROTO_H, FILE_PROTO_STATIC_REFLECTION_H } GeneratedFileType;
typedef enum { INIT_PRIORITY_101, INIT_PRIORITY_102 } InitPriority;
typedef enum {
	VERIFY_SIMPLE_INT32_NEVER, VERIFY_SIMPLE_INT32_ALWAYS, VERIFY_CUSTOM, VERIFY_CUSTOM_INT32_NEVER,
	VERIFY_CUSTOM_INT32_ALWAYS,
} VerifySimpleType;

typedef struct {
	bool is_recursive, contains_cord, contains_extension, contains_required, contains_weak;
} MessageAnalysis;

struct MessageSCCAnalyzer {
	LpSccAnalyzer *analyzer;
	const Options *options;
	Map analysis_cache; /* representative full name -> MessageAnalysis* */
};

/* ---- generated_message_tctable_gen.h ---- */

/* TcParseFunction: kNone and PROTOBUF_TC_PARSE_FUNCTION_LIST in order (tctable_gen.c names) */
typedef uint8_t TcParseFunction;

enum { TC_FAST_EMPTY = 0, TC_FAST_MP_FIELD = 1, TC_FAST_FIELD = 2, TC_FAST_NON_FIELD = 3 };

typedef struct {
	int kind; /* std::variant index: TC_FAST_* */
	TcParseFunction func;
	const LpField *field;
	uint16_t coded_tag;
	uint16_t nonfield_info; /* NonField */
	uint8_t hasbit_idx;     /* Field */
	uint8_t aux_idx;        /* Field */
	uint32_t field_index;   /* MpField */
	uint8_t function_index; /* MpField */
	float presence_probability;
} FastFieldInfo;

typedef struct {
	const LpField *field;
	int hasbit_idx;
	uint16_t aux_idx;
	uint16_t type_card;
	Utf8CheckMode utf8_check_mode;
} FieldEntryInfo;

typedef enum {
	AUX_NOTHING = 0, AUX_SPLIT_OFFSET, AUX_SPLIT_SIZEOF, AUX_SUB_MESSAGE_GLOBALS, AUX_SUB_TABLE,
	AUX_SUB_MESSAGE_GLOBALS_WEAK, AUX_MESSAGE_VERIFY_FUNC, AUX_SELF_VERIFY_FUNC, AUX_ENUM_RANGE,
	AUX_ENUM_VALIDATOR, AUX_NUMERIC_OFFSET, AUX_MAP_AUX_INFO,
} AuxType;

typedef struct {
	AuxType type;
	const LpField *field;
	uint32_t offset;
	int32_t first, last; /* enum_range */
} AuxEntry;

typedef struct {
	uint16_t skipmap;
	uint16_t field_entry_offset;
} SkipEntry16;

typedef struct {
	uint32_t first_fnum;
	SkipEntry16 *entries;
	int n_entries, cap_entries;
} SkipEntryBlock;

typedef struct {
	uint32_t skipmap32;
	SkipEntryBlock *blocks;
	int n_blocks, cap_blocks;
} NumToEntryTable;

typedef struct {
	const LpField *field;
	int has_bit_index;
	float presence_probability;
	uint16_t lazy_opt;
	bool is_implicitly_weak;
	bool use_direct_tcparser_table;
	bool should_split;
	int str_kind; /* 0: monostate, 1: StringInlined, 2: MicroString */
	uint8_t micro_string_sso;
} TcFieldOptions;

typedef struct {
	bool is_lite;
	bool uses_codegen;
} TcMessageOptions;

typedef struct {
	TcParseFunction fallback_function;
	FastFieldInfo *fast_path_fields;
	int n_fast_path_fields;
	FieldEntryInfo *field_entries;
	int n_field_entries;
	AuxEntry *aux_entries;
	int n_aux_entries, cap_aux_entries;
	NumToEntryTable num_to_entry_table;
	uint8_t *field_name_data;
	size_t n_field_name_data;
	int table_size_log2;
} TailCallTableInfo;

/* ---- field_layout.h ---- */

typedef struct {
	int *has_bit_indices; /* NULL: empty (by field index) */
	int n_has_bit_indices;
	int max_has_bit_index;
	const LpField **fields; /* optimized order */
	int n_fields;
} FieldLayout;

/* ---- field.h ---- */

typedef struct FieldGenerator FieldGenerator;

typedef void (*FieldGenFn)(const FieldGenerator *g, LpPrinter *p);

/* The virtual methods of FieldGeneratorBase; NULL is the base implementation. */
typedef struct {
	bool (*requires_arena)(const FieldGenerator *g);
	void (*make_vars)(const FieldGenerator *g, Subs *out);
	FieldGenFn generate_private_members;
	FieldGenFn generate_secondary_private_members;
	FieldGenFn generate_static_members;
	FieldGenFn generate_accessor_declarations;
	FieldGenFn generate_inline_accessor_definitions;
	FieldGenFn generate_non_inline_accessor_definitions;
	FieldGenFn generate_clearing_code;
	FieldGenFn generate_message_clearing_code;
	FieldGenFn generate_merging_code;
	FieldGenFn generate_copy_constructor_code;
	FieldGenFn generate_swapping_code;
	FieldGenFn generate_destructor_code;
	FieldGenFn generate_arena_destructor_code;
	FieldGenFn generate_member_constexpr_constructor;
	FieldGenFn generate_member_constructor;
	FieldGenFn generate_member_copy_constructor;
	FieldGenFn generate_oneof_copy_construct;
	FieldGenFn generate_aggregate_initializer;
	FieldGenFn generate_constexpr_aggregate_initializer;
	FieldGenFn generate_copy_aggregate_initializer;
	FieldGenFn generate_serialize_with_cached_sizes_to_array;
	FieldGenFn generate_byte_size;
	FieldGenFn generate_is_initialized;
	bool (*needs_is_initialized)(const FieldGenerator *g);
	bool (*is_inlined)(const FieldGenerator *g);
	ArenaDtorNeeds (*needs_arena_destructor)(const FieldGenerator *g);
} FieldGeneratorVtbl;

/* FieldGenerator with its FieldGeneratorBase impl_ (the subclass is vt plus the members
 * below). */
struct FieldGenerator {
	const FieldGeneratorVtbl *vt;
	const LpField *field;
	const Options *options;
	Subs variables; /* variables_ */
	bool should_split, is_trivial, has_trivial_value, has_trivial_zero_default, has_brace_default_assign,
		is_message, is_group, is_string, is_bytes, is_inlined, is_foreign, is_lazy, is_weak, is_oneof,
		has_default_constexpr_constructor;
	/* subclass members */
	CppRepeatedType cpp_repeated_type;
	bool has_required; /* message fields: has_required_ */
	bool has_hasbit;   /* message/string fields: has_hasbit_ */
	bool is_oneof_impl; /* message fields: is_oneof_ */
	bool is_inlined_impl;
	const LpField *key, *val; /* map fields */
	const char *field_name;   /* cord/string fields */
	/* FieldGenerator */
	Subs field_vars, tracker_vars, per_generator_vars;
};

/* FieldGeneratorTable: the generators of a message, by field index. */
typedef struct {
	const LpMessage *descriptor;
	FieldGenerator *fields;
	int n;
} FieldGeneratorTable;

/* ---- field_chunk.h ---- */

typedef struct {
	bool has_hasbit;
	bool is_rarely_present;
	bool should_split;
	const LpField **fields;
	int n, cap;
} FieldChunk;

typedef struct {
	FieldChunk *v;
	int n, cap;
} FieldChunks;

/* ---- parse_function_generator.h ---- */

typedef struct {
	const LpMessage *descriptor;
	const Options *options;
	Subs variables;
	TailCallTableInfo *tc_table_info;
	const LpField **ordered_fields;
	int n_ordered_fields;
	bool has_hasbits;
	int index_in_file_messages;
} ParseFunctionGenerator;

/* ---- enum.h, extension.h, service.h ---- */

typedef struct {
	const LpEnum *enum_;
	Options options;
	int *sorted_unique_values;
	int n_sorted_unique_values;
	bool generate_array_size;
	bool should_cache;
	bool has_reflection;
	const LpEnumValue *limits_min, *limits_max;
} EnumGenerator;

typedef struct {
	const LpField *descriptor;
	const char *type_traits;
	Options options;
	Subs variables;
} ExtensionGenerator;

typedef struct {
	const LpService *descriptor;
	const Options *options;
	Subs vars;
	int index_in_metadata;
} ServiceGenerator;

/* ---- message.h ---- */

typedef struct {
	const LpMessage *descriptor;
	int index_in_file_messages;
	Options options;
	FieldGeneratorTable field_generators;
	Vec enum_generators;      /* const EnumGenerator* */
	Vec extension_generators; /* const ExtensionGenerator* */
	int num_required_fields;
	int num_weak_fields;
	FieldLayout field_layout;
	ParseFunctionGenerator *parse_function_generator;
	Subs variables;
} MessageGenerator;

/* ---- file.h ---- */

typedef struct {
	void (*fn)(void *self, int i, InitPriority priority, LpPrinter *p);
	void *self;
	int i;
	InitPriority priority;
	const LpMessage *pinned;
} StaticInitializer;

typedef struct {
	const LpFile *file;
	Options options;
	MessageGenerator **message_generators;
	int n_message_generators;
	int *message_generators_topologically_ordered;
	Vec enum_generators;      /* EnumGenerator* */
	Vec extension_generators; /* ExtensionGenerator* */
	ServiceGenerator **service_generators;
	int n_service_generators;
	Map weak_deps;
	Vec static_initializers[2]; /* StaticInitializer* */
	Subs variables;
} FileGenerator;

#endif
