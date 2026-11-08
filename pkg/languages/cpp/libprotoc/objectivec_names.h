// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 36.2: src/google/protobuf/compiler/objectivec/names.h, the Objective-C names shared
 * by the objc generator of protoc and grpc's objective_c plugin (objectivec_names.c). Strings are
 * arena allocated.
 *
 * The prefix mode of upstream is a process global set by generator options
 * (use_package_as_prefix, package_as_prefix_forced_prefix); here it is reset by
 * lp_objc_reset_prefix_mode() at the start of a request. Its environment variables
 * (GPB_OBJC_USE_PACKAGE_AS_PREFIX, ...) are not read, and the files named by
 * package_to_prefix_mappings_path and proto_package_prefix_exceptions_path are not read: callers
 * reject non-empty paths. */
#ifndef LIBPROTOC_OBJECTIVEC_NAMES_H
#define LIBPROTOC_OBJECTIVEC_NAMES_H

#include "libprotoc.h"

void lp_objc_reset_prefix_mode(void);
void lp_objc_set_use_package_as_default_prefix(bool on_or_off);
void lp_objc_set_forced_package_prefix(const char *prefix);

bool lp_objc_is_retained_name(const char *name);
bool lp_objc_is_init_name(const char *name);
bool lp_objc_is_create_name(const char *name);

char *lp_objc_file_class_prefix(const LpFile *file);
char *lp_objc_file_path(const LpFile *file);
char *lp_objc_file_path_basename(const LpFile *file);
char *lp_objc_file_class_name(const LpFile *file);
char *lp_objc_extension_registry_function_name(const LpFile *file);
char *lp_objc_file_unique_symbol_name(const LpFile *file, const char *suffix);

char *lp_objc_class_name(const LpMessage *descriptor);
char *lp_objc_enum_name(const LpEnum *descriptor);
char *lp_objc_enum_value_name(const LpEnumValue *descriptor);
char *lp_objc_enum_value_short_name(const LpEnumValue *descriptor);
char *lp_objc_un_camel_case_enum_short_name(const char *name);
char *lp_objc_extension_class_name(const LpField *descriptor);
char *lp_objc_extension_function_name(const LpField *descriptor);
char *lp_objc_extension_method_name(const LpField *descriptor);
char *lp_objc_field_name(const LpField *field);
char *lp_objc_field_name_capitalized(const LpField *field);

/* FieldObjCTypeOptions */
enum {
	LP_OBJC_TYPE_OMIT_LIGHTWEIGHT_GENERICS = 1 << 0,
	LP_OBJC_TYPE_INCLUDE_SPACE_BEFORE_STAR = 1 << 1,
	LP_OBJC_TYPE_INCLUDE_SPACE_AFTER_BASIC_TYPES = 1 << 2,
};
char *lp_objc_field_objc_type(const LpField *field, unsigned options);

char *lp_objc_oneof_enum_name(const LpOneof *descriptor);
char *lp_objc_oneof_name(const LpOneof *descriptor);
char *lp_objc_oneof_name_capitalized(const LpOneof *descriptor);
char *lp_objc_un_camel_case_field_name(const char *name, const LpField *field);

/* ProtobufLibraryFrameworkName */
#define LP_OBJC_PROTOBUF_LIBRARY_FRAMEWORK_NAME "Protobuf"
char *lp_objc_protobuf_framework_import_symbol(const char *framework_name);
bool lp_objc_is_protobuf_library_bundled_proto_file(const LpFile *file);

/* Options (the validation options of ValidateObjCClassPrefixes). expected_prefixes_path is ""
 * or "-": the file it names is not read. */
typedef struct {
	const char *expected_prefixes_path;
	const char *const *expected_prefixes_suppressions;
	int n_expected_prefixes_suppressions;
	bool prefixes_must_be_registered;
	bool require_prefixes;
} LpObjcValidationOptions;
/* ValidateObjCClassPrefixes: NULL, or the error. */
const char *lp_objc_validate_objc_class_prefixes(LpFile *const *files, int n, const LpObjcValidationOptions *options);

#endif
