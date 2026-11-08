// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* The types of the objc generator (src/google/protobuf/compiler/objectivec/ headers, protobuf v36.2)
 * shared by the files of this unity build (generator.c includes them all). */
#ifndef OBJC_V36_2_OBJECTIVEC_H
#define OBJC_V36_2_OBJECTIVEC_H

#include "libprotoc.h"
#include "objectivec_names.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
	FILEOPT_DEPRECATED = 23, FILEOPT_OBJC_CLASS_PREFIX = 36,
	MSGOPT_MESSAGE_SET_WIRE_FORMAT = 1, MSGOPT_DEPRECATED = 3, MSGOPT_MAP_ENTRY = 7,
	FOPT_DEPRECATED = 3, ENUMOPT_DEPRECATED = 3, EVOPT_DEPRECATED = 1,
};

/* options.h: ExtensionGenerationMode and GenerationOptions */
typedef enum { EXT_MODE_CLASS_BASED, EXT_MODE_MIGRATION, EXT_MODE_C_FUNCTION } ExtensionGenerationMode;

typedef struct {
	const char *generate_for_named_framework;
	const char *named_framework_to_proto_path_mappings_path;
	const char *runtime_import_prefix;
	bool headers_use_forward_declarations;
	bool strip_custom_options;
	bool generate_minimal_imports;
	bool experimental_multi_source_generation;
	bool experimental_strip_nonfunctional_codegen;
	const char *annotation_pragma_name;
	const char *annotation_guard_name;
	ExtensionGenerationMode extension_generation_mode;
	Map *source_locations; /* file id -> LpSourceLocations* (GetSourceLocation tables) */
} GenerationOptions;

static bool emit_class_based_extensions(const GenerationOptions *o)
{
	return o->extension_generation_mode == EXT_MODE_CLASS_BASED || o->extension_generation_mode == EXT_MODE_MIGRATION;
}

static bool emit_c_function_extensions(const GenerationOptions *o)
{
	return o->extension_generation_mode == EXT_MODE_C_FUNCTION || o->extension_generation_mode == EXT_MODE_MIGRATION;
}

/* helpers.h: SubstitutionMap (overwritable Printer::Subs) */
typedef struct {
	LpSub *subs;
	int n, cap;
} SubstitutionMap;

/* A set of strings (absl::btree_set<std::string>): sorted and unique when finished. */
typedef struct {
	Vec v;
} StringSet;

/* A set of files (absl::flat_hash_set<const FileDescriptor*>): v in insertion order (with erased
 * files, see file_set_erase), ids marks the members. */
typedef struct {
	Vec v;
	Map ids; /* file id -> "" (NULL: erased) */
	int size;
} FileSet;

/* field.h: FieldGenerator and its subclasses (the kind selects the overrides) */
typedef enum {
	FIELD_PRIMITIVE,          /* PrimitiveFieldGenerator : SingleFieldGenerator */
	FIELD_ENUM,               /* EnumFieldGenerator : SingleFieldGenerator */
	FIELD_PRIMITIVE_OBJ,      /* PrimitiveObjFieldGenerator : ObjCObjFieldGenerator */
	FIELD_MESSAGE,            /* MessageFieldGenerator : ObjCObjFieldGenerator */
	FIELD_REPEATED_PRIMITIVE, /* RepeatedPrimitiveFieldGenerator : RepeatedFieldGenerator */
	FIELD_REPEATED_ENUM,      /* RepeatedEnumFieldGenerator : RepeatedFieldGenerator */
	FIELD_REPEATED_MESSAGE,   /* RepeatedMessageFieldGenerator : RepeatedFieldGenerator */
	FIELD_MAP,                /* MapFieldGenerator : RepeatedFieldGenerator */
} FieldKind;

typedef struct FieldGenerator FieldGenerator;
struct FieldGenerator {
	FieldKind kind;
	const LpField *descriptor;
	const GenerationOptions *generation_options;
	SubstitutionMap variables;
	FieldGenerator *value_field_generator; /* FIELD_MAP */
};

/* oneof.h */
typedef struct {
	const LpOneof *descriptor;
	const GenerationOptions *generation_options;
	SubstitutionMap variables;
} OneofGenerator;

/* enum.h */
typedef struct {
	const LpEnum *descriptor;
	const GenerationOptions *generation_options;
	Vec base_values;           /* const LpEnumValue* */
	Vec all_values;            /* const LpEnumValue* */
	bool *alias_values_to_skip; /* by value index */
	char *name;
} EnumGenerator;

/* extension.h */
typedef struct {
	char *root_or_message_class_name;
	char *method_name;
	char *function_name;
	const LpField *descriptor;
	const GenerationOptions *generation_options;
} ExtensionGenerator;

/* message.h */
typedef struct {
	const char *file_description_name;
	const LpMessage *descriptor;
	const GenerationOptions *generation_options;
	FieldGenerator **field_generators; /* by field index (FieldGeneratorMap) */
	char *class_name;
	char *deprecated_attribute;
	Vec extension_generators;          /* const ExtensionGenerator* */
	OneofGenerator **oneof_generators;
	int n_oneof_generators;
	int sizeof_has_storage;
} MessageGenerator;

/* file.h: FileGenerator::CommonState */
typedef struct {
	bool include_custom_options;
	Map deps_info_cache; /* file id -> MinDepsEntry* */
} CommonState;

typedef struct {
	int edition;
	const LpFile *file;
	const GenerationOptions *generation_options;
	CommonState *common_state;
	char *root_class_name;
	char *file_unique_symbol_name;
	char *file_registry_function_name;
	char *file_description_name;
	bool is_bundled_proto;
	Vec enum_generators;      /* EnumGenerator* */
	Vec message_generators;   /* MessageGenerator* */
	size_t file_scoped_extension_count;
	Vec extension_generators; /* ExtensionGenerator* */
} FileGenerator;

#endif
