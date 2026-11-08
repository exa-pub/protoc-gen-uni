// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.

/* Port of src/google/protobuf/compiler/csharp/csharp_helpers.{h,cc} (protobuf v36.2). GetCSharpType
 * is unused upstream and not ported. */

static char *cs_itoa(int64_t v) { return xfmt("%lld", (long long)v); }

static bool cs_isdigit(char c) { return c >= '0' && c <= '9'; }
static bool cs_islower(char c) { return c >= 'a' && c <= 'z'; }
static bool cs_isupper(char c) { return c >= 'A' && c <= 'Z'; }
static bool cs_isalnum(char c) { return cs_isdigit(c) || cs_islower(c) || cs_isupper(c); }
static char cs_toupper(char c) { return cs_islower(c) ? (char)(c - 'a' + 'A') : c; }
static char cs_tolower(char c) { return cs_isupper(c) ? (char)(c - 'A' + 'a') : c; }

/* ShoutyToPascalCase: SHOUTY_CASE (or nearly so) to PascalCase. Non-alphanumeric characters are
 * skipped; an alphanumeric one is upper-cased after a non-letter (or at the start), kept after
 * a lower-case letter and lower-cased after an upper-case one. */
static char *cs_shouty_to_pascal_case(const char *input)
{
	Buf result = {0};
	char previous = '_'; /* Simple way of implementing "always start with upper" */
	for (const char *p = input; *p; p++) {
		char current = *p;
		if (!cs_isalnum(current)) {
			previous = current;
			continue;
		}
		if (!cs_isalnum(previous))
			buf_addc(&result, cs_toupper(current));
		else if (cs_isdigit(previous))
			buf_addc(&result, cs_toupper(current));
		else if (cs_islower(previous))
			buf_addc(&result, current);
		else
			buf_addc(&result, cs_tolower(current));
		previous = current;
	}
	return (char *)buf_str(&result);
}

/* TryRemovePrefix: value without prefix, ignoring case and underscores (in both), and the
 * underscores after it; value itself if prefix does not match or nothing would remain. */
static const char *cs_try_remove_prefix(const char *prefix, const char *value)
{
	/* First normalize to a lower-case no-underscores prefix to match against */
	Buf prefix_to_match = {0};
	for (const char *p = prefix; *p; p++)
		if (*p != '_')
			buf_addc(&prefix_to_match, cs_tolower(*p));
	size_t value_size = strlen(value);
	size_t prefix_index, value_index;
	for (prefix_index = 0, value_index = 0; prefix_index < prefix_to_match.n && value_index < value_size;
	     value_index++) {
		/* Skip over underscores in the value */
		if (value[value_index] == '_')
			continue;
		if (cs_tolower(value[value_index]) != prefix_to_match.p[prefix_index++])
			return value; /* Failed to match the prefix - bail out early. */
	}
	/* If we didn't finish looking through the prefix, we can't strip it. */
	if (prefix_index < prefix_to_match.n)
		return value;
	/* Step over any underscores after the prefix */
	while (value_index < value_size && value[value_index] == '_')
		value_index++;
	/* If there's nothing left (e.g. it was a prefix with only underscores afterwards), don't
	 * strip. */
	if (value_index == value_size)
		return value;
	return value + value_index;
}

/* GetEnumValueName: the enum name stripped as a prefix if possible, in PascalCase. */
static char *cs_get_enum_value_name(const char *enum_name, const char *enum_value_name)
{
	const char *stripped = cs_try_remove_prefix(enum_name, enum_value_name);
	char *result = cs_shouty_to_pascal_case(stripped);
	/* Just in case we have an enum name of FOO and a value of FOO_2... make sure the returned
	 * string is a valid identifier. */
	if (cs_isdigit(result[0]))
		return xcat("_", result);
	return result;
}

static char *cs_get_full_extension_name(const LpField *descriptor)
{
	if (descriptor->extension_scope)
		return xcat(lp_csharp_class_name_message(descriptor->extension_scope), ".Extensions.",
			    cs_get_property_name(descriptor));
	return xcat(lp_csharp_extension_class_unqualified_name(descriptor->file), ".",
		    cs_get_property_name(descriptor));
}

/* GetFieldName: the type name for group-like fields (to keep its capitalization), else the
 * field name. */
static char *cs_get_field_name(const LpField *descriptor)
{
	if (lp_field_is_group_like(descriptor))
		return (char *)descriptor->message_type->name;
	return (char *)descriptor->name;
}

static char *cs_get_field_constant_name(const LpField *field) { return xcat(cs_get_property_name(field), "FieldNumber"); }

static char *cs_get_property_name(const LpField *descriptor)
{
	/* Names of members declared or overridden in the message. */
	static const char *const reserved_member_names[] = {
		"Types",         "Descriptor", "Equals",    "ToString",       "GetHashCode", "WriteTo",
		"Clone",         "CalculateSize", "MergeFrom", "OnConstruction", "Parser",
	};
	char *property_name = lp_csharp_underscores_to_pascal_case(cs_get_field_name(descriptor));
	/* Avoid either our own type name or reserved names. */
	bool reserved = str_eq(property_name, descriptor->containing_type->name);
	for (size_t i = 0; !reserved && i < sizeof reserved_member_names / sizeof *reserved_member_names; i++)
		reserved = str_eq(property_name, reserved_member_names[i]);
	if (reserved)
		property_name = xcat(property_name, "_");
	return property_name;
}

static char *cs_get_oneof_case_name(const LpField *descriptor)
{
	/* The name in a oneof case enum is the same as for the property, but as we always have a
	 * "None" value as well, we need to reserve that by appending an underscore. */
	char *property_name = cs_get_property_name(descriptor);
	return str_eq(property_name, "None") ? xstrdup("None_") : property_name;
}

/* GetFixedSize: the size in bytes of fixed-size encodings, else -1. */
static int cs_get_fixed_size(int type)
{
	switch (type) {
	case PT_FIXED32: case PT_SFIXED32: case PT_FLOAT: return 4;
	case PT_FIXED64: case PT_SFIXED64: case PT_DOUBLE: return 8;
	case PT_BOOL: return 1;
	default: return -1;
	}
}

/* FileDescriptorToBase64 (StringToBase64 is absl::Base64Escape) */
static char *cs_file_descriptor_to_base64(const LpFile *descriptor)
{
	Buf fdp_bytes = {0};
	lp_msg_serialize(lp_strip_source_retention_options(descriptor), &fdp_bytes);
	return lp_base64_escape(buf_view(&fdp_bytes));
}

static CsFieldGen *cs_create_field_generator(const LpField *descriptor, int presence_index, const CsOptions *options)
{
	CsFieldKind kind;
	bool repeated = proto_field_is_repeated(descriptor);
	bool in_oneof = proto_field_real_oneof(descriptor) != NULL;
	switch (descriptor->type) {
	case PT_GROUP:
	case PT_MESSAGE:
		if (repeated)
			kind = proto_field_is_map(descriptor) ? CS_MAP : CS_REPEATED_MESSAGE;
		else if (cs_is_wrapper_type(descriptor))
			kind = in_oneof ? CS_WRAPPER_ONEOF : CS_WRAPPER;
		else
			kind = in_oneof ? CS_MESSAGE_ONEOF : CS_MESSAGE;
		break;
	case PT_ENUM:
		kind = repeated ? CS_REPEATED_ENUM : in_oneof ? CS_ENUM_ONEOF : CS_ENUM;
		break;
	default:
		kind = repeated ? CS_REPEATED_PRIMITIVE : in_oneof ? CS_PRIMITIVE_ONEOF : CS_PRIMITIVE;
		break;
	}
	return cs_field_generator_new(kind, descriptor, presence_index, options);
}

static bool cs_is_nullable(const LpField *descriptor)
{
	if (proto_field_is_repeated(descriptor))
		return true;
	switch (descriptor->type) {
	case PT_MESSAGE: case PT_GROUP: case PT_STRING: case PT_BYTES: return true;
	default: return false;
	}
}

static bool cs_is_map_entry_message(const LpMessage *descriptor)
{
	return pb_int(descriptor->options, MSGOPT_MAP_ENTRY, 0) != 0;
}

static bool cs_is_descriptor_proto(const LpFile *descriptor)
{
	return str_eq(descriptor->name, "google/protobuf/descriptor.proto") ||
	       str_eq(descriptor->name, "net/proto2/proto/descriptor.proto");
}

static bool cs_is_wrapper_type(const LpField *descriptor)
{
	return descriptor->type == PT_MESSAGE &&
	       str_eq(descriptor->message_type->file->name, "google/protobuf/wrappers.proto");
}

/* descriptor->message_type()->field(0) of a wrapper type. A message of a
 * google/protobuf/wrappers.proto without fields is undefined behavior upstream. */
static const LpField *cs_wrapped_field(const LpField *descriptor)
{
	const LpMessage *m = descriptor->message_type;
	if (m->n_fields == 0)
		fail("unsupported by protoc-gen-uni: field %s of type %s from google/protobuf/wrappers.proto, which "
		     "has no fields", descriptor->full_name, m->full_name);
	return &m->fields[0];
}

static bool cs_supports_presence_api(const LpField *descriptor)
{
	/* Unlike most languages, we don't generate Has/Clear members for message types, because they
	 * can always be set to null in C#. */
	if (descriptor->type == PT_MESSAGE)
		return false;
	return proto_field_has_presence(descriptor);
}

static bool cs_requires_presence_bit(const LpField *descriptor)
{
	return cs_supports_presence_api(descriptor) && !cs_is_nullable(descriptor) && !descriptor->is_extension &&
	       !proto_field_real_oneof(descriptor);
}

/* Generator::GetResolvedSourceFeatures(*file).GetExtension(pb::csharp).nullable_reference_types() */
static bool cs_nrt_enabled(const LpFile *file)
{
	return pb_int(pb_msg(file->merged_features, FEATURES_PB_CSHARP), CSF_NULLABLE_REFERENCE_TYPES, 0) != 0;
}
