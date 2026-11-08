// Copyright 2008 Google Inc.  All rights reserved.
// Copyright (c) 2008-2025, Dave Benson and the protobuf-c authors.  All rights reserved.
// Use of this source code is governed by the BSD-style licenses reproduced in generator.c.

/* The classes of protoc-gen-c (the *.h files of protoc-gen-c/ in protobuf-c v1.5.2) as C types,
 * and the declarations shared by the files of this unity build (generator.c). */
#ifndef PROTOBUF_C_INTERNAL_H
#define PROTOBUF_C_INTERNAL_H

#include "libprotoc.h"
#include "printer_3.21.12.h"

#include <stdlib.h>
#include <string.h>

/* PROTOBUF_C_VERSION_NUMBER of protobuf-c/protobuf-c.h */
#define PROTOBUF_C_VERSION_NUMBER 1005002

/* Field numbers of descriptor.proto */
enum {
	FILEOPT_OPTIMIZE_FOR = 9, FILEOPT_CODE_SIZE = 2,
	FOPT_PACKED = 2, FOPT_DEPRECATED = 3,
};

/* ---- protobuf-c.pb.c: the options of protobuf-c/protobuf-c.proto ---- */

/* ProtobufCFileOptions (pb_c_file, FileOptions extension 1019) */
typedef struct {
	bool no_generate;
	bool has_gen_pack_helpers, gen_pack_helpers;
	bool gen_init_helpers;
	bool const_strings;
	bool use_oneof_field_name;
	bool has_c_package;
	const char *c_package;
} PbcFileOptions;

/* ProtobufCMessageOptions (pb_c_msg, MessageOptions extension 1019) */
typedef struct {
	bool has_gen_pack_helpers, gen_pack_helpers;
	bool has_gen_init_helpers, gen_init_helpers;
	const char *base_field_name;
} PbcMessageOptions;

/* ProtobufCFieldOptions (pb_c_field, FieldOptions extension 1019) */
typedef struct {
	bool string_as_bytes;
} PbcFieldOptions;

static const PbcFileOptions *pbc_file_options(const LpFile *file);
static PbcMessageOptions pbc_message_options(const LpMessage *message);
static PbcFieldOptions pbc_field_options(const LpField *field);
static bool pbc_request_parses(Str raw);

/* What is computed once per file (arena memory: c_generate resets it for every file). */
typedef struct {
	const LpFile *locations_file;
	LpSourceLocations *locations;
	Map file_options; /* file name -> PbcFileOptions* */
} Cache;
static Cache cache;

/* ---- c_helpers.c ---- */

static char *simple_itoa(int64_t n);
static char *simple_utoa(uint64_t n);
static char *simple_ftoa(float f);
static char *simple_dtoa(double d);
static char *camel_to_upper(const char *name);
static char *camel_to_lower(const char *name);
static char *to_lower(const char *name);
static char *to_camel(const char *name);
static char *full_name_to_lower(const char *full_name, const LpFile *file);
static char *full_name_to_upper(const char *full_name, const LpFile *file);
static char *full_name_to_c(const char *full_name, const LpFile *file);
static void print_comment(LpPrinter3 *printer, Str comment);
static char *convert_to_spaces(const char *input);
static char *strip_proto(const char *filename);
static char *filename_identifier(const char *filename);
static const char *get_label_name(int label);
static unsigned write_int_ranges(LpPrinter3 *printer, int n_values, const int *values, const char *name);
static char *c_escape(Str src);
static char *field_name(const LpField *field);
static const char *field_deprecated(const LpField *field);
static int field_syntax(const LpField *field);
static bool optimize_code_size(const LpFile *file);
/* GetSourceLocation of a descriptor: its leading and trailing comments (empty if none) */
static void source_comments(const void *descriptor, Str *leading, Str *trailing);

/* ---- c_field.c and the field generators ---- */

typedef enum { FG_MESSAGE, FG_STRING, FG_BYTES, FG_ENUM, FG_PRIMITIVE } FieldKind;

/* FieldGenerator and its subclasses */
typedef struct {
	const LpField *descriptor;
	FieldKind kind;
	LpVars3 variables; /* variables_ of the string, bytes and enum generators */
} FieldGenerator;

/* FieldGeneratorMap: one per field of the message (index order) */
typedef struct {
	const LpMessage *descriptor;
	FieldGenerator **field_generators;
} FieldGeneratorMap;

static void field_generator_map_init(FieldGeneratorMap *m, const LpMessage *descriptor);
static const FieldGenerator *field_generator_map_get(const FieldGeneratorMap *m, const LpField *field);
static void field_generate_struct_members(const FieldGenerator *g, LpPrinter3 *printer);
static void field_generate_descriptor_initializer(const FieldGenerator *g, LpPrinter3 *printer);
static void field_generate_default_value_declarations(const FieldGenerator *g, LpPrinter3 *printer);
static void field_generate_default_value_implementations(const FieldGenerator *g, LpPrinter3 *printer);
static char *field_get_default_value(const FieldGenerator *g);
static void field_generate_static_init(const FieldGenerator *g, LpPrinter3 *printer);
static void field_generate_descriptor_initializer_generic(const FieldGenerator *g, LpPrinter3 *printer,
							  bool optional_uses_has, const char *type_macro,
							  const char *descriptor_addr);

/* ---- c_enum.c ---- */

typedef struct {
	const LpEnum *descriptor;
	const char *dllexport_decl;
} EnumGenerator;

static void enum_generate_definition(const EnumGenerator *g, LpPrinter3 *printer);
static void enum_generate_descriptor_declarations(const EnumGenerator *g, LpPrinter3 *printer);
static void enum_generate_enum_descriptor(const EnumGenerator *g, LpPrinter3 *printer);

/* ---- c_message.c ---- */

typedef struct MessageGenerator MessageGenerator;
struct MessageGenerator {
	const LpMessage *descriptor;
	const char *dllexport_decl;
	FieldGeneratorMap field_generators;
	MessageGenerator *nested_generators;
	EnumGenerator *enum_generators;
};

static void message_generator_init(MessageGenerator *g, const LpMessage *descriptor, const char *dllexport_decl);
static void message_generate_struct_typedef(const MessageGenerator *g, LpPrinter3 *printer);
static void message_generate_enum_definitions(const MessageGenerator *g, LpPrinter3 *printer);
static void message_generate_struct_definition(const MessageGenerator *g, LpPrinter3 *printer);
static void message_generate_helper_function_declarations(const MessageGenerator *g, LpPrinter3 *printer,
							  bool is_pack_deep, bool gen_pack, bool gen_init);
static void message_generate_descriptor_declarations(const MessageGenerator *g, LpPrinter3 *printer);
static void message_generate_closure_typedef(const MessageGenerator *g, LpPrinter3 *printer);
static void message_generate_helper_function_definitions(const MessageGenerator *g, LpPrinter3 *printer,
							 bool is_pack_deep, bool gen_pack, bool gen_init);
static void message_generate_message_descriptor(const MessageGenerator *g, LpPrinter3 *printer, bool gen_init);

/* ---- c_service.c ---- */

typedef struct {
	const LpService *descriptor;
	LpVars3 vars;
} ServiceGenerator;

static void service_generator_init(ServiceGenerator *g, const LpService *descriptor, const char *dllexport_decl);
static void service_generate_main_h_file(ServiceGenerator *g, LpPrinter3 *printer);
static void service_generate_descriptor_declarations(ServiceGenerator *g, LpPrinter3 *printer);
static void service_generate_c_file(ServiceGenerator *g, LpPrinter3 *printer);

/* ---- c_file.c ---- */

typedef struct {
	const LpFile *file;
	MessageGenerator *message_generators;
	EnumGenerator *enum_generators;
	ServiceGenerator *service_generators;
} FileGenerator;

static void file_generator_init(FileGenerator *g, const LpFile *file, const char *dllexport_decl);
static void file_generate_header(FileGenerator *g, LpPrinter3 *printer);
static void file_generate_source(FileGenerator *g, LpPrinter3 *printer);

#endif
