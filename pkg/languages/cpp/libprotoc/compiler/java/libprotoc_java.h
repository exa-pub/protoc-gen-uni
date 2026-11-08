// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The shared core of src/google/protobuf/compiler/java (protobuf v36.2) that the Java, Kotlin and
 * gRPC Java generators use, ported to C:
 *
 *   - options.c (options.h): Options;
 *   - context.c (context.{h,cc}): Context, the field and oneof names of a file;
 *   - names.c, name_resolver.c (names.{h,cc}, names_internal.h, name_resolver.{h,cc}): Java
 *     packages and class names (ClassNameResolver is stateless here: jv_* functions);
 *   - helpers.c, internal_helpers.c (helpers.{h,cc}, internal_helpers.{h,cc}, java_features):
 *     types, default values, bit fields, features;
 *   - doc_comment.c (doc_comment.{h,cc}): Javadoc and KDoc comments;
 *   - field_common.c (field_common.{h,cc}): the common variables of field generators;
 *   - message_serialization.c (message_serialization.{h,cc}).
 *
 * Unity build (libprotoc_java.c); memory comes from the pkg/core arena. A ClassNameResolver
 * method is a function of the same name taking the descriptor (jv_get_class_name_message). The
 * per-descriptor data of this library lives behind ProtoMessage.user and ProtoFile.user of the
 * files it sees. */
#ifndef LIBPROTOC_JAVA_H
#define LIBPROTOC_JAVA_H

#include "libprotoc.h"

/* ---- options.h ---- */

typedef struct {
	bool generate_immutable_code, generate_mutable_code, generate_shared_code;
	bool enforce_lite;
	bool annotate_code;
	const char *annotation_list_file; /* "" if none */
	const char *output_list_file;
	bool strip_nonfunctional_codegen;
	bool jvm_dsl;                 /* default true */
	bool dsl_use_concrete_types;
	bool bootstrap;
} JvOptions;

/* Options() */
JvOptions jv_default_options(void);

/* ---- java_features.pb.h: pb.JavaFeatures (FeatureSet extension 1001) ---- */

enum {
	JV_FEATURES_EXT = 1001,
	JV_LEGACY_CLOSED_ENUM = 1, JV_UTF8_VALIDATION = 2, JV_LARGE_ENUM = 3,
	JV_USE_OLD_OUTER_CLASSNAME_DEFAULT = 4, JV_NEST_IN_FILE_CLASS = 5,
	JV_UTF8_VERIFY = 2,
	JV_NEST_UNKNOWN = 0, JV_NEST_NO = 1, JV_NEST_YES = 2, JV_NEST_LEGACY = 3,
};
/* GetResolvedSourceFeatures(d).GetExtension(pb::java).<field> (enum and bool fields). */
int jv_resolved_java_feature(const ProtoDesc *d, int field);
/* GetUnresolvedSourceFeatures(d, pb::java).has_<field>() */
bool jv_unresolved_has_java_feature(const ProtoDesc *d, int field);

/* ---- descriptor.pb.h options read by the generators ---- */

enum {
	JV_FILEOPT_JAVA_PACKAGE = 1, JV_FILEOPT_JAVA_OUTER_CLASSNAME = 8, JV_FILEOPT_OPTIMIZE_FOR = 9,
	JV_FILEOPT_JAVA_MULTIPLE_FILES = 10, JV_FILEOPT_JAVA_GENERIC_SERVICES = 17,
	JV_FILEOPT_DEPRECATED = 23, JV_FILEOPT_JAVA_STRING_CHECK_UTF8 = 27,
	JV_MSGOPT_MESSAGE_SET_WIRE_FORMAT = 1, JV_MSGOPT_NO_STANDARD_DESCRIPTOR_ACCESSOR = 2,
	JV_MSGOPT_DEPRECATED = 3, JV_MSGOPT_MAP_ENTRY = 7,
	JV_FIELDOPT_DEPRECATED = 3,
	JV_ENUMOPT_ALLOW_ALIAS = 2, JV_ENUMOPT_DEPRECATED = 3,
	JV_ENUMVALOPT_DEPRECATED = 1,
	JV_SVCOPT_DEPRECATED = 33, JV_METHOPT_DEPRECATED = 33,
	JV_OPTIMIZE_SPEED = 1, JV_OPTIMIZE_CODE_SIZE = 2, JV_OPTIMIZE_LITE_RUNTIME = 3,
};
/* <descriptor>->options().<bool field>() (false for the default instance) */
bool jv_opt_bool(const ProtoDesc *d, int field);
#define JV_DEPRECATED_MSG(m) jv_opt_bool((const ProtoDesc *)(m), JV_MSGOPT_DEPRECATED)
bool jv_file_deprecated(const LpFile *f);
bool jv_message_deprecated(const LpMessage *m);
bool jv_field_deprecated(const LpField *f);
bool jv_enum_deprecated(const LpEnum *e);
bool jv_enum_value_deprecated(const LpEnumValue *v);
int jv_file_optimize_for(const LpFile *f);

/* ---- helpers.h ---- */

extern const char jv_thick_separator[];
extern const char jv_thin_separator[];

typedef enum {
	JAVATYPE_INT, JAVATYPE_LONG, JAVATYPE_FLOAT, JAVATYPE_DOUBLE, JAVATYPE_BOOLEAN, JAVATYPE_STRING,
	JAVATYPE_BYTES, JAVATYPE_ENUM, JAVATYPE_MESSAGE
} JvJavaType;

/* Field accessor kinds (doc_comment.h FieldAccessorType) */
typedef enum {
	JV_HAZZER, JV_GETTER, JV_SETTER, JV_CLEARER,
	JV_LIST_COUNT, JV_LIST_GETTER, JV_LIST_INDEXED_GETTER, JV_LIST_INDEXED_SETTER, JV_LIST_ADDER,
	JV_LIST_MULTI_ADDER,
} JvFieldAccessorType;

bool jv_is_forbidden_kotlin(const char *field_name);
void jv_print_generated_annotation(LpPrinter *p, char delimiter, const char *annotation_file,
				   const JvOptions *options);
void jv_print_enum_verifier_logic(LpPrinter *p, const LpField *descriptor, const LpVars *variables,
				  const char *var_name, const char *terminating_string, bool enforce_lite);
void jv_print_gencode_version_validator(LpPrinter *p, bool oss_runtime, const char *java_class_name);
char *jv_to_camel_case(const char *input, bool lower_first);
char *jv_camel_case_field_name(const LpField *field);
char *jv_unique_file_scope_identifier(const LpMessage *descriptor);
char *jv_file_class_name(const LpFile *file, bool immutable);
char *jv_java_package_to_dir(const char *package_name);
char *jv_escape_kotlin_keywords(const char *name);
char *jv_extra_message_interfaces(const LpMessage *descriptor);
char *jv_extra_builder_interfaces(const LpMessage *descriptor);
char *jv_extra_message_or_builder_interfaces(const LpMessage *descriptor);
bool jv_is_descriptor_proto(const LpMessage *descriptor);
char *jv_get_oneof_stored_type(const LpField *field);

const char *jv_validate_nest_in_file_class_message(const LpMessage *descriptor); /* NULL: ok */
const char *jv_validate_nest_in_file_class_enum(const LpEnum *descriptor);
/* NestedInFileClass(descriptor, immutable) (helpers.h, with the validation check) */
bool jv_nested_in_file_class_message(const LpMessage *d, bool immutable);
bool jv_nested_in_file_class_enum(const LpEnum *d, bool immutable);
bool jv_nested_in_file_class_service(const LpService *d, bool immutable);
bool jv_is_own_file_message(const LpMessage *d, bool immutable);
bool jv_is_own_file_enum(const LpEnum *d, bool immutable);
bool jv_is_own_file_service(const LpService *d, bool immutable);
char *jv_annotation_file_name(const char *name, const char *suffix);

char *jv_field_constant_name(const LpField *field);
int jv_get_type(const LpField *field);
JvJavaType jv_get_java_type(const LpField *field);
const char *jv_primitive_type_name(JvJavaType type);         /* NULL for enum and message */
const char *jv_boxed_primitive_type_name(JvJavaType type);   /* NULL for enum and message */
const char *jv_kotlin_type_name(JvJavaType type);            /* NULL for enum and message */
const char *jv_field_type_name(int field_type);
char *jv_default_value(const LpField *field, bool immutable, const JvOptions *options);
#define jv_immutable_default_value(field) jv_default_value((field), true, NULL)
bool jv_is_default_value_java_default(const LpField *field);
bool jv_is_byte_string_with_custom_default_value(const LpField *field);
bool jv_has_descriptor_methods(bool enforce_lite);
bool jv_has_generic_services(const LpFile *file, bool enforce_lite);

char *jv_get_bit_field_name(int index);
char *jv_get_bit_field_name_for_bit(int bit_index);
char *jv_generate_get_bit(int bit_index);
char *jv_generate_set_bit(int bit_index);
char *jv_generate_clear_bit(int bit_index);
char *jv_generate_get_bit_from_local(int bit_index);
char *jv_generate_set_bit_to_local(int bit_index);
char *jv_generate_get_bit_mutable_local(int bit_index);
char *jv_generate_set_bit_mutable_local(int bit_index);
bool jv_is_reference_type(JvJavaType type);
const char *jv_get_capitalized_type(const LpField *field, bool immutable);
int jv_fixed_size(int type);
/* SortFieldsByNumber: a new array of the message's fields */
const LpField **jv_sort_fields_by_number(const LpMessage *descriptor);
bool jv_has_packed_fields(const LpMessage *descriptor);
bool jv_has_required_fields(const LpMessage *descriptor);
bool jv_is_real_oneof(const LpField *descriptor);
bool jv_has_hasbit(const LpField *descriptor);
bool jv_support_unknown_enum_value(const LpField *field);
bool jv_has_repeated_fields(const LpMessage *descriptor);
bool jv_is_map_entry(const LpMessage *descriptor);
bool jv_is_map_field(const LpField *descriptor);
bool jv_is_any_message(const LpMessage *descriptor);
bool jv_is_wrappers_proto_file(const LpFile *descriptor);

/* std::vector<uint16_t> */
typedef struct {
	uint16_t *v;
	int n, cap;
} JvUtf16;
void jv_utf16_push(JvUtf16 *o, uint16_t c);
void jv_write_uint32_to_utf16_char_sequence(uint32_t number, JvUtf16 *output);
void jv_write_int_to_utf16_char_sequence(int value, JvUtf16 *output);
void jv_escape_utf16_to_string(uint16_t code, Buf *output);
const LpField *jv_map_key_field(const LpField *descriptor);
const LpField *jv_map_value_field(const LpField *descriptor);
const char *jv_jvm_synthetic(bool jvm_dsl);
/* JvmNameContext and JvmName(name, context) */
typedef struct {
	const JvOptions *options;
	LpPrinter *printer;
	bool lite;
} JvJvmNameContext;
void jv_jvm_name(const char *name, const JvJvmNameContext *context);

/* ---- internal_helpers.h ---- */

bool jv_check_utf8(const LpField *descriptor);
bool jv_check_large_enum(const LpEnum *descriptor);
int jv_get_experimental_java_field_type(const LpField *field);

/* ---- names.h, names_internal.h ---- */

char *jv_underscores_to_camel_case(const char *input, bool cap_next_letter);
char *jv_underscores_to_camel_case_field(const LpField *field);
char *jv_underscores_to_camel_case_method(const LpMethod *method);
char *jv_underscores_to_camel_case_check_reserved(const LpField *field);
char *jv_underscores_to_capitalized_camel_case(const LpField *field);
char *jv_capitalized_field_name(const LpField *field);
char *jv_capitalized_oneof_name(const LpOneof *oneof);
/* FileJavaPackage(file) */
char *jv_file_java_package(const LpFile *file);
char *jv_java_package_directory(const LpFile *file);
bool jv_nested_in_file_class_impl_message(const LpMessage *m);
bool jv_nested_in_file_class_impl_enum(const LpEnum *e);
bool jv_nested_in_file_class_impl_service(const LpService *s);
char *jv_join_package(const char *a, const char *b);
char *jv_kotlin_factory_name(const LpMessage *descriptor);

/* ---- name_resolver.h (ClassNameResolver) ---- */

typedef enum { JV_NO_MATCH, JV_EXACT_EQUAL, JV_EQUAL_IGNORE_CASE } JvNameEquality;

char *jv_get_file_class_name(const LpFile *file, bool immutable, bool kotlin);
char *jv_get_file_immutable_class_name(const LpFile *file);
char *jv_get_file_default_immutable_class_name(const LpFile *file);
bool jv_has_conflicting_class_name(const LpFile *file, const char *classname, JvNameEquality equality_mode);
char *jv_get_descriptor_class_name(const LpFile *file);
char *jv_get_class_name_message(const LpMessage *d, bool immutable, bool kotlin);
char *jv_get_class_name_enum(const LpEnum *d, bool immutable, bool kotlin);
char *jv_get_class_name_service(const LpService *d, bool immutable, bool kotlin);
char *jv_get_class_name_file(const LpFile *d, bool immutable, bool kotlin);
#define jv_immutable_class_name_message(d) jv_get_class_name_message((d), true, false)
#define jv_immutable_class_name_enum(d) jv_get_class_name_enum((d), true, false)
#define jv_immutable_class_name_service(d) jv_get_class_name_service((d), true, false)
#define jv_immutable_class_name_file(d) jv_get_class_name_file((d), true, false)
char *jv_get_extension_identifier_name(const LpField *d, bool immutable, bool kotlin);
char *jv_get_java_immutable_class_name_message(const LpMessage *d);
char *jv_get_java_immutable_class_name_enum(const LpEnum *d);
char *jv_get_java_immutable_class_name_service(const LpService *d);
char *jv_get_kotlin_factory_name(const LpMessage *d);
char *jv_get_fully_qualified_kotlin_factory_name(const LpMessage *d);
char *jv_get_kotlin_extensions_class_name(const LpMessage *d);
char *jv_get_kotlin_extensions_class_name_escaped(const LpMessage *d);
char *jv_get_file_java_package(const LpFile *file, bool immutable);
char *jv_get_class_full_name(const char *name_without_package, const LpFile *file, bool immutable,
			     bool is_own_file, bool kotlin);

/* ---- context.h, field_common.h ---- */

typedef struct {
	const char *name, *capitalized_name, *disambiguated_reason;
	const JvOptions *options;
} JvFieldGeneratorInfo;

typedef struct {
	const char *name, *capitalized_name;
} JvOneofGeneratorInfo;

typedef struct {
	JvOptions options;
	const LpFile *file;
	LpSourceLocations *locations; /* of file, built on first use */
} JvContext;

JvContext *jv_context_new(const LpFile *file, const JvOptions *options);
const JvFieldGeneratorInfo *jv_context_field_info(const JvContext *c, const LpField *field);
const JvOneofGeneratorInfo *jv_context_oneof_info(const JvContext *c, const LpOneof *oneof);
bool jv_context_enforce_lite(const JvContext *c);
bool jv_context_has_generated_methods(const JvContext *c, const LpMessage *descriptor);
/* MaybePrintGeneratedAnnotation */
void jv_maybe_print_generated_annotation_message(JvContext *c, LpPrinter *p, const LpMessage *d, bool immutable,
						 const char *suffix);
void jv_maybe_print_generated_annotation_enum(JvContext *c, LpPrinter *p, const LpEnum *d, bool immutable,
					      const char *suffix);
void jv_maybe_print_generated_annotation_service(JvContext *c, LpPrinter *p, const LpService *d,
						 bool immutable, const char *suffix);
/* descriptor->GetSourceLocation(&location) for descriptors of any file */
bool jv_get_source_location(const ProtoDesc *d, LpSourceLocation *out);

void jv_set_common_field_variables(const LpField *descriptor, const JvFieldGeneratorInfo *info, LpVars *variables);
void jv_set_common_oneof_variables(const LpField *descriptor, const JvOneofGeneratorInfo *info, LpVars *variables);
void jv_print_extra_field_info(const LpVars *variables, LpPrinter *p);
char *jv_get_kotlin_property_name(const char *capitalized_name);

/* ---- doc_comment.h ---- */

char *jv_escape_javadoc(Str input);
void jv_write_message_doc_comment(LpPrinter *p, const LpMessage *message, const JvOptions *options, bool kdoc);
void jv_write_field_doc_comment(LpPrinter *p, const LpField *field, const JvOptions *options, bool kdoc);
void jv_write_field_accessor_doc_comment(LpPrinter *p, const LpField *field, JvFieldAccessorType type,
					 const JvOptions *options, bool builder, bool kdoc, bool is_private);
void jv_write_field_enum_value_accessor_doc_comment(LpPrinter *p, const LpField *field, JvFieldAccessorType type,
						    const JvOptions *options, bool builder, bool is_private);
void jv_write_field_string_bytes_accessor_doc_comment(LpPrinter *p, const LpField *field,
						      JvFieldAccessorType type, const JvOptions *options,
						      bool builder, bool kdoc, bool is_private);
void jv_write_enum_doc_comment(LpPrinter *p, const LpEnum *enum_, const JvOptions *options, bool kdoc);
void jv_write_enum_value_doc_comment(LpPrinter *p, const LpEnumValue *value, const JvOptions *options);
void jv_write_service_doc_comment(LpPrinter *p, const LpService *service, const JvOptions *options);
void jv_write_method_doc_comment(LpPrinter *p, const LpMethod *method, const JvOptions *options);

/* ---- message_serialization.h ---- */

/* The field generators of a message, by field index (FieldGeneratorMap::field_generators):
 * serialize(gens[i], p) prints field i's serialization code. */
typedef void (*JvSerializeFn)(const void *gen, LpPrinter *p);
void jv_generate_serialize_extension_range(LpPrinter *p, const LpExtensionRange *range);
void jv_generate_serialize_fields_and_extensions(LpPrinter *p, const void *const *gens, JvSerializeFn serialize,
						 const LpMessage *descriptor, const LpField **sorted_fields);

/* generator_common.h */
enum { JV_MAX_STATIC_SIZE = 1 << 15 };

#endif
