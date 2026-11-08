// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/string_field.{h,cc} (protobuf v36.2). */

static int ImmutableStringFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutableStringFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								JvUtf16 *output);
static void ImmutableStringFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutableStringFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutableStringOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								     JvUtf16 *output);
static void ImmutableStringOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutableStringFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void RepeatedImmutableStringFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									JvUtf16 *output);
static void RepeatedImmutableStringFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g,
										 LpPrinter *printer);
static const char *RepeatedImmutableStringFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);

static void SetPrimitiveVariables_lite_string(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
					      const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "empty_list", "emptyProtobufList()");

	lp_vars_set(variables, "default", jv_immutable_default_value_(descriptor, &context->options));
	lp_vars_set(variables, "default_init", xcat("= ", jv_immutable_default_value_(descriptor, &context->options)));
	lp_vars_set(variables, "capitalized_type", "java.lang.String");
	lp_vars_set(variables, "tag", xcat(jv_i(((int32_t)(jv_make_tag(descriptor))))));
	lp_vars_set(variables, "tag_size", xcat(jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor)))));

	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(variables, "required", proto_field_is_required(descriptor) ? "true" : "false");
	if (!JV_IS_OSS) {
		lp_vars_set(variables, "enforce_utf8", jv_check_utf8(descriptor) ? "true" : "false");
	}

	if (jv_has_hasbit(descriptor)) {
		if (!JV_IS_OSS) {
			lp_vars_set(variables, "bit_field_id", xcat(jv_i(messageBitIndex / 32)));
			lp_vars_set(variables, "bit_field_name", jv_get_bit_field_name_for_bit(messageBitIndex));
			lp_vars_set(variables, "bit_field_mask", xcat(jv_i(1 << (messageBitIndex % 32))));
		}
		// For singular messages and builders, one bit is used for the hasField bit.
		// Note that these have a trailing ";".
		lp_vars_set(variables, "set_has_field_bit_message", xcat(jv_generate_set_bit(messageBitIndex), ";"));
		lp_vars_set(variables, "clear_has_field_bit_message",
			    xcat(jv_generate_clear_bit(messageBitIndex), ";"));

		lp_vars_set(variables, "is_field_present_message", jv_generate_get_bit(messageBitIndex));
	} else {
		lp_vars_set(variables, "set_has_field_bit_message", "");
		lp_vars_set(variables, "clear_has_field_bit_message", "");

		lp_vars_insert(variables, "is_field_present_message",
			       xcat("!", lp_vars_get(variables, "name"), "_.isEmpty()"));
	}

	// Annotations often use { and } variables to denote text ranges.
	lp_vars_set(variables, "{", "");
	lp_vars_set(variables, "}", "");
}

// ===================================================================

static void ImmutableStringFieldLiteGenerator_init(JvFieldGen *g)
{
	SetPrimitiveVariables_lite_string(g->descriptor, g->message_bit_index, g->builder_bit_index,
					  jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static int ImmutableStringFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

// A note about how strings are handled. In the SPEED and CODE_SIZE runtimes,
// strings are not stored as java.lang.String in the Message because of two
// issues:
//
//  1. It wouldn't roundtrip byte arrays that were not valid UTF-8 encoded
//     strings, but rather fields that were raw bytes incorrectly marked
//     as strings in the proto file. This is common because in the proto1
//     syntax, string was the way to indicate bytes and C++ engineers can
//     easily make this mistake without affecting the C++ API. By converting to
//     strings immediately, some java code might corrupt these byte arrays as
//     it passes through a java server even if the field was never accessed by
//     application code.
//
//  2. There's a performance hit to converting between bytes and strings and
//     it many cases, the field is never even read by the application code. This
//     avoids unnecessary conversions in the common use cases.
//
// In the LITE_RUNTIME, we store strings as java.lang.String because we assume
// that the users of this runtime are not subject to proto1 constraints and are
// running code on devices that are user facing. That is, the developers are
// properly incentivized to only fetch the data they need to read and wish to
// reduce the number of allocations incurred when running on a user's device.

// TODO: Consider dropping all of the *Bytes() methods. They really
//     shouldn't be necessary or used on devices.
static void ImmutableStringFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$boolean ${$has$capitalized_name$$}$();\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$java.lang.String ${$get$capitalized_name$$}$();\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$();\n");
}

static void ImmutableStringFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (!JV_IS_OSS) {
		lp_print_vars(printer, &g->variables,
			      "@com.google.protobuf.ProtoField(\n"
			      "  isRequired=$required$)\n");
		if (jv_has_hasbit(g->descriptor)) {
			lp_print_vars(printer, &g->variables,
				      "@com.google.protobuf.ProtoPresenceCheckedField(\n"
				      "  presenceBitsId=$bit_field_id$,\n"
				      "  mask=$bit_field_mask$)\n");
		}
	}
	lp_print_vars(printer, &g->variables, "private java.lang.String $name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);

	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
			      "  return $is_field_present_message$;\n"
			      "}\n");
	}

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.lang.String ${$get$capitalized_name$$}$() {\n"
		      "  return $name$_;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  return com.google.protobuf.ByteString.copyFromUtf8($name$_);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$(\n"
		      "    java.lang.String value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  $set_has_field_bit_message$\n"
		      "  $name$_ = value;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $clear_has_field_bit_message$\n"
		      // The default value is not a simple literal so we want to
		      // avoid executing it multiple times.  Instead, get the default
		      // out of the default instance.
		      "  $name$_ = getDefaultInstance().get$capitalized_name$();\n"
		      "}\n");

	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							 /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$Bytes(\n"
		      "    com.google.protobuf.ByteString value) {\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "  checkByteStringIsUtf8(value);\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  $name$_ = value.toStringUtf8();\n"
		      "  $set_has_field_bit_message$\n"
		      "}\n");
}

static void ImmutableStringFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
			      "  return instance.has$capitalized_name$();\n"
			      "}\n");
	}

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.lang.String ${$get$capitalized_name$$}$() {\n"
		      "  return instance.get$capitalized_name$();\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  return instance.get$capitalized_name$Bytes();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  copyOnWrite();\n"
		      "  instance.clear$capitalized_name$();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							 /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$Bytes(value);\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutableStringFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	if (jv_has_hasbit(g->descriptor)) {
		jv_write_int_to_utf16_char_sequence(g->message_bit_index, output);
	}
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
}

static void ImmutableStringFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $default$;\n");
}

static const char *ImmutableStringFieldLiteGenerator_GetBoxedType(const JvFieldGen *g) { return "java.lang.String"; }

// ===================================================================

static void ImmutableStringOneofFieldLiteGenerator_init(JvFieldGen *g)
{
	ImmutableStringFieldLiteGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutableStringOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_print_extra_field_info(&g->variables, printer);
	JV_DCHECK(proto_field_has_presence(g->descriptor));
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $has_oneof_case_message$;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.lang.String ${$get$capitalized_name$$}$() {\n"
		      "  java.lang.String ref $default_init$;\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    ref = (java.lang.String) $oneof_name$_;\n"
		      "  }\n"
		      "  return ref;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  java.lang.String ref $default_init$;\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    ref = (java.lang.String) $oneof_name$_;\n"
		      "  }\n"
		      "  return com.google.protobuf.ByteString.copyFromUtf8(ref);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void ${$set$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  $set_oneof_case_message$;\n"
		      "  $oneof_name$_ = value;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void ${$clear$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $clear_oneof_case_message$;\n"
		      "    $oneof_name$_ = null;\n"
		      "  }\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							 /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void ${$set$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "  checkByteStringIsUtf8(value);\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  $oneof_name$_ = value.toStringUtf8();\n"
		      "  $set_oneof_case_message$;\n"
		      "}\n");
}

static void ImmutableStringOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								     JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	jv_write_int_to_utf16_char_sequence(g->descriptor->containing_oneof->index, output);
}

static void ImmutableStringOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	JV_DCHECK(proto_field_has_presence(g->descriptor));
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return instance.has$capitalized_name$();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.lang.String ${$get$capitalized_name$$}$() {\n"
		      "  return instance.get$capitalized_name$();\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  return instance.get$capitalized_name$Bytes();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  copyOnWrite();\n"
		      "  instance.clear$capitalized_name$();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							 /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$Bytes(value);\n"
		      "  return this;\n"
		      "}\n");
}

// ===================================================================

static int RepeatedImmutableStringFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static void RepeatedImmutableStringFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<java.lang.String>\n"
		      "    ${$get$capitalized_name$List$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$int ${$get$capitalized_name$Count$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.lang.String "
		      "${$get$capitalized_name$$}$(int index);\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$(int index);\n");
}

static void RepeatedImmutableStringFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.Internal.ProtobufList<java.lang.String> "
		      "$name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<java.lang.String> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return $name$_;\n" // note:  unmodifiable list
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return $name$_.size();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.lang.String "
		      "${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							 &g->context->options, false, false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$(int index) {\n"
		      "  return com.google.protobuf.ByteString.copyFromUtf8(\n"
		      "      $name$_.get(index));\n"
		      "}\n");
	lp_print_vars(printer, &g->variables,
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      // Use a temporary to avoid a redundant iget-object.
		      "  com.google.protobuf.Internal.ProtobufList<java.lang.String> tmp =\n"
		      "      $name$_;"
		      "  if (!tmp.isModifiable()) {\n"
		      "    $name$_ =\n"
		      "        com.google.protobuf.GeneratedMessageLite.mutableCopy(tmp);\n"
		      "   }\n"
		      "}\n");

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$(\n"
		      "    int index, java.lang.String value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.set(index, value);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void add$capitalized_name$(\n"
		      "    java.lang.String value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.add(value);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void addAll$capitalized_name$(\n"
		      "    java.lang.Iterable<java.lang.String> values) {\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  com.google.protobuf.AbstractMessageLite.addAll(\n"
		      "      values, $name$_);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $name$_ = $empty_list$;\n"
		      "}\n");

	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
							 /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void add$capitalized_name$Bytes(\n"
		      "    com.google.protobuf.ByteString value) {\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "  checkByteStringIsUtf8(value);\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.add(value.toStringUtf8());\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<java.lang.String>\n"
		      "    ${$get$capitalized_name$List$}$() {\n"
		      "  return java.util.Collections.unmodifiableList(\n"
		      "      instance.get$capitalized_name$List());\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return instance.get$capitalized_name$Count();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.lang.String "
		      "${$get$capitalized_name$$}$(int index) {\n"
		      "  return instance.get$capitalized_name$(index);\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							 &g->context->options, false, false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$(int index) {\n"
		      "  return instance.get$capitalized_name$Bytes(index);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, java.lang.String value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(index, value);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		      "    java.lang.Iterable<java.lang.String> values) {\n"
		      "  copyOnWrite();\n"
		      "  instance.addAll$capitalized_name$(values);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  copyOnWrite();\n"
		      "  instance.clear$capitalized_name$();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
							 /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$Bytes(value);\n"
		      "  return this;\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
}

static void RepeatedImmutableStringFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g,
										 LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $empty_list$;\n");
}

static const char *RepeatedImmutableStringFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return "java.lang.String";
}

static const JvFieldGenOps ImmutableStringFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableStringFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableStringFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableStringFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableStringFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableStringFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableStringFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableStringFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps ImmutableStringOneofFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableStringFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableStringFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableStringOneofFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableStringOneofFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableStringFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableStringFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableStringOneofFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps RepeatedImmutableStringFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = RepeatedImmutableStringFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = RepeatedImmutableStringFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutableStringFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutableStringFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutableStringFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = RepeatedImmutableStringFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = RepeatedImmutableStringFieldLiteGenerator_GenerateFieldInfo,
};

#define RepeatedImmutableStringFieldLiteGenerator_init ImmutableStringFieldLiteGenerator_init
