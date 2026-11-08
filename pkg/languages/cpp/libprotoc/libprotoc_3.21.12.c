// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* libprotoc 3.21.12: what protobuf v3.21.12 (https://github.com/protocolbuffers/protobuf, tag
 * v21.12) does differently from the 36.2 of this library, for the plugins built with it
 * (community/protobuf-c v1.5.2, against the libprotoc-dev package of Debian bookworm). Its own
 * library (cpp_libprotoc_3.21.12), linked by those generators together with printer_3.21.12.c.
 *
 *   - no editions: descriptor.proto of v3.21.12 has no FileDescriptorProto.edition, so a file
 *     with syntax "editions" fails to build with "Unrecognized syntax: editions", like any other
 *     syntax but "", "proto2" and "proto3"; MaximumKnownEdition() does not exist (the feature
 *     defaults the pool builds internally end at PROTO3, which is all a v3.21.12 file can be);
 *   - the generated pool of protoc_lib: descriptor.proto of v3.21.12 lacks the fields of 36.2 for
 *     editions and later options (features everywhere, edition, option_dependency, visibility,
 *     deprecated_legacy_json_field_conflicts, debug_redact, retention, targets, edition_defaults,
 *     feature_support, extension declarations and verification, Annotation.semantic), so a
 *     request that sets them keeps them as unknown fields that nothing reads; plugin.proto lacks
 *     CodeGeneratorRequest.source_file_descriptors and CodeGeneratorResponse.minimum_edition and
 *     maximum_edition, which plugin.cc v3.21.12 never sets. It links none of the feature protos
 *     (cpp, java, c_sharp), cpp_file_options.proto and json_enumvalue_options.proto. builtin_file
 *     edits the 36.2 files into these; the types of editions stay (unreachable from a request)
 *     because the pool of this library resolves the legacy features with them.
 *
 *   - DescriptorBuilder: the log of the default error collector is GOOGLE_LOG(ERROR), whose
 *     prefix "[libprotobuf ERROR google/protobuf/descriptor.cc:<line>] " (the lines of the
 *     Debian build) is deterministic; JSON names of fields are only checked in proto3, where
 *     names must differ once lower-cased without underscores (ValidateProto3Message); enum
 *     values that differ only by case and the enum name prefix are a warning in proto2 and an
 *     error with another text in proto3 (CheckEnumValueUniqueness); map fields are validated
 *     as before 36 (LpVersion.legacy_map_entry); a field number used twice gives no next
 *     available field number (LpVersion.legacy_field_number_error).
 *
 * plugin.cc v3.21.12 otherwise works as 36.2: PluginMain, GenerateCode, the default
 * CodeGenerator::GenerateAll and its error texts are the same. The other validation
 * differences of descriptor.cc v3.21.12 (the checks 36.2 added for names that are too long, NUL
 * bytes in json_name, ...) concern inputs that protoc and buf reject. */
#include "libprotoc.h"

#include <string.h>

/* Field numbers of descriptor.proto */
enum {
	F_FILE_NAME = 1, F_FILE_MESSAGE_TYPE = 4,
	F_MSG_NAME = 1, F_MSG_FIELD = 2, F_MSG_NESTED_TYPE = 3,
	F_FIELD_NAME = 1, F_FIELD_NUMBER = 3,
};

/* The message of file (by a path of names, nested messages after the first), or NULL. */
static PbMsg *find_message(PbMsg *file, const char *const *names)
{
	PbMsg *m = file;
	uint32_t num = F_FILE_MESSAGE_TYPE;
	for (; *names; names++, num = F_MSG_NESTED_TYPE) {
		PbMsg *next = NULL;
		for (int i = 0; i < pb_count(m, num) && !next; i++)
			if (strcmp(pb_str(pb_msg_at(m, num, i), F_MSG_NAME), *names) == 0)
				next = pb_msg_at(m, num, i);
		if (!next)
			return NULL;
		m = next;
	}
	return m;
}

/* Removes the fields with the given numbers (0-terminated) from a message of file. */
static COLD void remove_fields(PbMsg *file, const char *const *names, const int *numbers)
{
	PbMsg *m = find_message(file, names);
	PbSlot *s = m ? (PbSlot *)pb_slot(m, F_MSG_FIELD) : NULL;
	if (!s)
		return;
	int k = 0;
	for (int i = 0; i < s->n; i++) {
		int number = (int)pb_int(s->v[i].m, F_FIELD_NUMBER, 0);
		bool drop = false;
		for (const int *n = numbers; *n && !drop; n++)
			drop = *n == number;
		if (!drop)
			s->v[k++] = s->v[i];
	}
	s->n = k;
}

#define NAMES(...) ((const char *const[]){__VA_ARGS__, NULL})
#define NUMBERS(...) ((const int[]){__VA_ARGS__, 0})

static COLD void patch_descriptor_proto(PbMsg *file)
{
	remove_fields(file, NAMES("FileDescriptorProto"), NUMBERS(14, 15));
	remove_fields(file, NAMES("DescriptorProto"), NUMBERS(11));
	remove_fields(file, NAMES("EnumDescriptorProto"), NUMBERS(6));
	remove_fields(file, NAMES("FileOptions"), NUMBERS(50));
	remove_fields(file, NAMES("MessageOptions"), NUMBERS(11, 12));
	remove_fields(file, NAMES("FieldOptions"), NUMBERS(16, 17, 19, 20, 21, 22));
	remove_fields(file, NAMES("OneofOptions"), NUMBERS(1));
	remove_fields(file, NAMES("EnumOptions"), NUMBERS(6, 7));
	remove_fields(file, NAMES("EnumValueOptions"), NUMBERS(2, 3, 4));
	remove_fields(file, NAMES("ServiceOptions"), NUMBERS(34));
	remove_fields(file, NAMES("MethodOptions"), NUMBERS(35));
	remove_fields(file, NAMES("ExtensionRangeOptions"), NUMBERS(2, 3, 50));
	remove_fields(file, NAMES("GeneratedCodeInfo", "Annotation"), NUMBERS(5));
}

static COLD void patch_plugin_proto(PbMsg *file)
{
	remove_fields(file, NAMES("CodeGeneratorRequest"), NUMBERS(17));
	remove_fields(file, NAMES("CodeGeneratorResponse"), NUMBERS(3, 4));
}

/* LpVersion.builtin_file */
static COLD bool builtin_file(int i, PbMsg *file)
{
	switch (i) {
	case 0: patch_descriptor_proto(file); return true;
	case 6: patch_plugin_proto(file); return true;
	default: return false; /* the feature protos, cpp_file_options, json_enumvalue_options */
	}
}

/* ToLowercaseWithoutUnderscores */
static char *lowercase_without_underscores(const char *name)
{
	char *result = xalloc(strlen(name) + 1), *r = result;
	for (; *name; name++)
		if (*name != '_')
			*r++ = *name >= 'A' && *name <= 'Z' ? (char)(*name - 'A' + 'a') : *name;
	return result;
}

/* ValidateProto3Message: in proto3, field names must be unique once lower-cased without
 * underscores. */
void lp_3_21_12_field_json_name_errors(const LpMessage *message, Vec *errors)
{
	if (message->file->edition != LP_EDITION_PROTO3)
		return;
	Map name_to_field = {0};
	for (int i = 0; i < message->n_fields; i++) {
		const LpField *field = &message->fields[i];
		char *lowercase_name = lowercase_without_underscores(field->name);
		const LpField *other = map_get(&name_to_field, lowercase_name);
		if (other)
			vec_push(errors, xcat("The JSON camel-case name of field \"", field->name, "\" conflicts with field \"",
					      other->name, "\". This is not allowed in proto3."));
		else
			map_put(&name_to_field, lowercase_name, (void *)field);
	}
}

/* CheckEnumValueUniqueness: a warning in proto2 */
const char *lp_3_21_12_enum_value_name_error(const LpEnum *e, const LpEnumValue *value, const LpEnumValue *previous,
					     bool *warning)
{
	*warning = e->file->edition == LP_EDITION_PROTO2;
	return xcat("Enum name ", value->name, " has the same name as ", previous->name,
		    " if you ignore case and strip out the enum name prefix (if any). This is error-prone and can lead "
		    "to undefined behavior. Please avoid doing this. If you are using allow_alias, please assign the "
		    "same numeric value to both enums.");
}

const LpVersion lp_version_3_21_12 = {
	.name = "3.21.12",
	.maximum_known_edition = LP_EDITION_PROTO3,
	.builtin_file = builtin_file,
	.log_file_prefix = "[libprotobuf ERROR google/protobuf/descriptor.cc:4201] ",
	.log_error_prefix = "[libprotobuf ERROR google/protobuf/descriptor.cc:4204] ",
	.no_editions = true,
	.field_json_name_errors = lp_3_21_12_field_json_name_errors,
	.enum_value_name_error = lp_3_21_12_enum_value_name_error,
	.legacy_map_entry = true,
	.legacy_field_number_error = true,
};
