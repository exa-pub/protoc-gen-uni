// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/csharp v36.2: a port of the C# generator of protoc v36.2
 * (buf.build/protocolbuffers/csharp, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (`csharp::Generator g; return PluginMain(argc, argv, &g);`) and
 * src/google/protobuf/compiler/csharp/csharp_*.{h,cc}, one .c file per upstream file. names.cc is
 * shared with grpc's csharp plugin and lives in pkg/languages/cpp/libprotoc (csharp_names.h), with
 * the other libprotoc parts (plugin.cc, descriptor.cc, retention.cc, io::Printer, ...).
 *
 * The class hierarchy of the field generators (csharp_field_base.h and its subclasses) becomes
 * one struct with the kind of its most derived class; each virtual method is a switch that
 * calls the implementation the C++ class would inherit or override.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "csharp_names.h"
#include "libprotoc.h"
#include "uni.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
	FILEOPT_CSHARP_NAMESPACE = 37,
	MSGOPT_DEPRECATED = 3, MSGOPT_MAP_ENTRY = 7,
	FIELDOPT_DEPRECATED = 3,
	ENUMOPT_DEPRECATED = 3,
	ENUMVALOPT_DEPRECATED = 1,
	FEATURES_PB_CSHARP = 1004, CSF_NULLABLE_REFERENCE_TYPES = 1,
};

/* csharp_options.h: Options (locations: the source locations of the file being generated, for
 * GetSourceLocation in csharp_doc_comment.c) */
typedef struct {
	const char *file_extension;
	const char *base_namespace;
	bool base_namespace_specified;
	bool internal_access;
	bool serializable;
	bool strip_nonfunctional_codegen;
	LpSourceLocations *locations;
} CsOptions;

/* The classes of csharp_*_field.h, by their most derived class. */
typedef enum {
	CS_PRIMITIVE,          /* PrimitiveFieldGenerator */
	CS_PRIMITIVE_ONEOF,    /* PrimitiveOneofFieldGenerator : PrimitiveFieldGenerator */
	CS_ENUM,               /* EnumFieldGenerator : PrimitiveFieldGenerator */
	CS_ENUM_ONEOF,         /* EnumOneofFieldGenerator : PrimitiveOneofFieldGenerator */
	CS_MESSAGE,            /* MessageFieldGenerator */
	CS_MESSAGE_ONEOF,      /* MessageOneofFieldGenerator : MessageFieldGenerator */
	CS_WRAPPER,            /* WrapperFieldGenerator */
	CS_WRAPPER_ONEOF,      /* WrapperOneofFieldGenerator : WrapperFieldGenerator */
	CS_MAP,                /* MapFieldGenerator */
	CS_REPEATED_ENUM,      /* RepeatedEnumFieldGenerator */
	CS_REPEATED_MESSAGE,   /* RepeatedMessageFieldGenerator */
	CS_REPEATED_PRIMITIVE, /* RepeatedPrimitiveFieldGenerator */
} CsFieldKind;

/* FieldGeneratorBase and the members of its subclasses */
typedef struct {
	CsFieldKind kind;
	const LpField *descriptor;
	int presence_index;
	const CsOptions *options;
	bool nrt_enabled;
	bool is_value_type; /* PrimitiveFieldGenerator, WrapperFieldGenerator */
	LpVars variables;
} CsFieldGen;

/* csharp_helpers.c */
static char *cs_get_field_name(const LpField *descriptor);
static char *cs_get_property_name(const LpField *descriptor);
static char *cs_get_field_constant_name(const LpField *field);
static char *cs_get_oneof_case_name(const LpField *descriptor);
static char *cs_get_full_extension_name(const LpField *descriptor);
static char *cs_get_enum_value_name(const char *enum_name, const char *enum_value_name);
static int cs_get_fixed_size(int type);
static char *cs_file_descriptor_to_base64(const LpFile *descriptor);
static CsFieldGen *cs_create_field_generator(const LpField *descriptor, int presence_index, const CsOptions *options);
static bool cs_is_nullable(const LpField *descriptor);
static bool cs_is_map_entry_message(const LpMessage *descriptor);
static bool cs_is_descriptor_proto(const LpFile *descriptor);
static bool cs_is_wrapper_type(const LpField *descriptor);
static const LpField *cs_wrapped_field(const LpField *descriptor);
static bool cs_supports_presence_api(const LpField *descriptor);
static bool cs_requires_presence_bit(const LpField *descriptor);
static bool cs_nrt_enabled(const LpFile *file);
static char *cs_itoa(int64_t v);

/* csharp_source_generator_base.c */
static const char *cs_class_access_level(const CsOptions *options);
static void cs_write_generated_code_attributes(LpPrinter *printer);

/* csharp_doc_comment.c */
static void cs_write_doc_comment(LpPrinter *printer, const CsOptions *options, const void *descriptor);

/* csharp_field_base.c */
static CsFieldGen *cs_field_generator_new(CsFieldKind kind, const LpField *descriptor, int presence_index,
					  const CsOptions *options);
static void cs_set_common_oneof_field_variables(CsFieldGen *g);
static void cs_add_deprecated_flag(CsFieldGen *g, LpPrinter *printer);
static void cs_add_public_member_attributes(CsFieldGen *g, LpPrinter *printer);
static char *cs_fg_name(CsFieldGen *g);
static char *cs_fg_type_name(CsFieldGen *g, const LpField *descriptor);
static void cs_fg_print(CsFieldGen *g, LpPrinter *printer, const char *text);
static void cs_fg_set(CsFieldGen *g, const char *key, const char *value);
static const char *cs_fg_var(CsFieldGen *g, const char *key);
/* the virtual methods */
static void cs_generate_cloning_code(CsFieldGen *g, LpPrinter *printer);
static void cs_generate_codec_code(CsFieldGen *g, LpPrinter *printer);
static void cs_generate_extension_code(CsFieldGen *g, LpPrinter *printer);
static void cs_generate_members(CsFieldGen *g, LpPrinter *printer);
static void cs_generate_merging_code(CsFieldGen *g, LpPrinter *printer);
static void cs_generate_parsing_code(CsFieldGen *g, LpPrinter *printer, bool use_parse_context);
static void cs_generate_serialization_code(CsFieldGen *g, LpPrinter *printer, bool use_write_context);
static void cs_generate_serialized_size_code(CsFieldGen *g, LpPrinter *printer);
static void cs_write_hash(CsFieldGen *g, LpPrinter *printer);
static void cs_write_equals(CsFieldGen *g, LpPrinter *printer);

#include "csharp_helpers.c"
#include "csharp_source_generator_base.c"
#include "csharp_doc_comment.c"
#include "csharp_primitive_field.c"
#include "csharp_enum_field.c"
#include "csharp_message_field.c"
#include "csharp_wrapper_field.c"
#include "csharp_map_field.c"
#include "csharp_repeated_enum_field.c"
#include "csharp_repeated_message_field.c"
#include "csharp_repeated_primitive_field.c"
#include "csharp_field_base.c"
#include "csharp_enum.c"
#include "csharp_message.c"
#include "csharp_reflection_class.c"
#include "csharp_generator.c"

static const char *const csharp_feature_extensions[] = {"pb.csharp", NULL};

/* csharp_generator.h: Generator */
static const LpGenerator csharp_generator = {
	.plugin_name = "protoc-gen-csharp",
	.generate = cs_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
	.feature_extensions = csharp_feature_extensions,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &csharp_generator, out);
}

const UniGenerator gen_protocolbuffers_csharp_v36_2 = {
	.name = "protocolbuffers/csharp",
	.version = "v36.2",
	.generate = generate,
};
