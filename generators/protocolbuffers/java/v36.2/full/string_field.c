// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/string_field.{h,cc} (protobuf v36.2). */

static int ImmutableStringFieldGenerator_GetMessageBitIndex(const JvFieldGen *g);
static int ImmutableStringFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g);
static int ImmutableStringFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutableStringFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										 LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutableStringFieldGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutableStringOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableStringOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutableStringFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static int RepeatedImmutableStringFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g);
static void RepeatedImmutableStringFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
											 LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableStringFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *RepeatedImmutableStringFieldGenerator_GetBoxedType(const JvFieldGen *g);

static void SetPrimitiveVariables_string(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
					 const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "empty_list", "com.google.protobuf.LazyStringArrayList.emptyList()");

	lp_vars_set(variables, "default", jv_immutable_default_value_(descriptor, &context->options));
	lp_vars_set(variables, "default_init", xcat("= ", jv_immutable_default_value_(descriptor, &context->options)));
	lp_vars_set(variables, "capitalized_type", "String");
	lp_vars_set(variables, "tag", xcat(jv_i(((int32_t)(jv_make_tag(descriptor))))));
	lp_vars_set(variables, "tag_size", xcat(jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor)))));
	lp_vars_set(variables, "null_check", "java.util.Objects.requireNonNull(value);");
	lp_vars_set(variables, "isStringEmpty", "com.google.protobuf.GeneratedMessage.isStringEmpty");
	lp_vars_set(variables, "writeString", "com.google.protobuf.GeneratedMessage.writeString");
	lp_vars_set(variables, "computeStringSize", "com.google.protobuf.GeneratedMessage.computeStringSize");

	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(variables, "on_changed", "onChanged();");

	if (jv_has_hasbit(descriptor)) {
		// For singular messages and builders, one bit is used for the hasField bit.
		lp_vars_set(variables, "set_has_field_bit_to_local", jv_generate_set_bit_to_local(messageBitIndex));

		// Note that these have a trailing ";".
		lp_vars_set(variables, "set_has_field_bit_message", xcat(jv_generate_set_bit(messageBitIndex), ";"));

		lp_vars_set(variables, "is_field_present_message", jv_generate_get_bit(messageBitIndex));
	} else {
		lp_vars_set(variables, "set_has_field_bit_to_local", "");
		lp_vars_set(variables, "set_has_field_bit_message", "");

		lp_vars_insert(
			variables, "is_field_present_message",
			xcat("!", lp_vars_get(variables, "isStringEmpty"), "(", lp_vars_get(variables, "name"), "_)"));
	}

	lp_vars_set(variables, "get_has_field_bit_builder", jv_generate_get_bit(builderBitIndex));
	lp_vars_set(variables, "get_has_field_bit_from_local", jv_generate_get_bit_from_local(builderBitIndex));
	lp_vars_set(variables, "set_has_field_bit_builder", xcat(jv_generate_set_bit(builderBitIndex), ";"));
	lp_vars_set(variables, "clear_has_field_bit_builder", xcat(jv_generate_clear_bit(builderBitIndex), ";"));
}

// ===================================================================

static void ImmutableStringFieldGenerator_init(JvFieldGen *g)
{
	SetPrimitiveVariables_string(g->descriptor, g->message_bit_index, g->builder_bit_index,
				     jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static int ImmutableStringFieldGenerator_GetMessageBitIndex(const JvFieldGen *g) { return g->message_bit_index; }

static int ImmutableStringFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g) { return g->builder_bit_index; }

static int ImmutableStringFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static int ImmutableStringFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

// A note about how strings are handled. This code used to just store a String
// in the Message. This had two issues:
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
// So now, the field for String is maintained as an Object reference which can
// either store a String or a ByteString. The code uses an instanceof check
// to see which one it has and converts to the other one if needed. It remembers
// the last value requested (in a thread safe manner) as this is most likely
// the one needed next. The thread safety is such that if two threads both
// convert the field because the changes made by each thread were not visible to
// the other, they may cause a conversion to happen more times than would
// otherwise be necessary. This was deemed better than adding synchronization
// overhead. It will not cause any corruption issues or affect the behavior of
// the API. The instanceof check is also highly optimized in the JVM and we
// decided it was better to reduce the memory overhead by not having two
// separate fields but rather use dynamic type checking.
//
// For single fields, the logic for this is done inside the generated code. For
// repeated fields, the logic is done in LazyStringArrayList.
static void ImmutableStringFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$boolean has$capitalized_name$();\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$java.lang.String get$capitalized_name$();\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$com.google.protobuf.ByteString\n"
		      "    get$capitalized_name$Bytes();\n");
}

static void ImmutableStringFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "@SuppressWarnings(\"serial\")\n"
		      "private volatile java.lang.Object $name$_ = $default$;\n");
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
		      "  java.lang.Object ref = $name$_;\n"
		      "  if (ref instanceof java.lang.String) {\n"
		      "    return (java.lang.String) ref;\n"
		      "  } else {\n"
		      "    com.google.protobuf.ByteString bs = \n"
		      "        (com.google.protobuf.ByteString) ref;\n"
		      "    java.lang.String s = bs.toStringUtf8();\n");
	if (jv_check_utf8(g->descriptor)) {

		lp_print_vars(printer, &g->variables, "    $name$_ = s;\n");

	} else {
		lp_print_vars(printer, &g->variables,
			      "    if (bs.isValidUtf8()) {\n"
			      "      $name$_ = s;\n"
			      "    }\n");
	}
	lp_print_vars(printer, &g->variables,
		      "    return s;\n"
		      "  }\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  java.lang.Object ref = $name$_;\n"
		      "  if (ref instanceof java.lang.String) {\n"
		      "    com.google.protobuf.ByteString b = \n"
		      "        com.google.protobuf.ByteString.copyFromUtf8(\n"
		      "            (java.lang.String) ref);\n"
		      "    $name$_ = b;\n"
		      "    return b;\n"
		      "  } else {\n"
		      "    return (com.google.protobuf.ByteString) ref;\n"
		      "  }\n"
		      "}\n");
}

static void ImmutableStringFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private java.lang.Object $name$_ $default_init$;\n");
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
			      "  return $get_has_field_bit_builder$;\n"
			      "}\n");
	}

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.lang.String ${$get$capitalized_name$$}$() {\n"
		      "  java.lang.Object ref = $name$_;\n"
		      "  if (!(ref instanceof java.lang.String)) {\n"
		      "    com.google.protobuf.ByteString bs =\n"
		      "        (com.google.protobuf.ByteString) ref;\n"
		      "    java.lang.String s = bs.toStringUtf8();\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "    $name$_ = s;\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "    if (bs.isValidUtf8()) {\n"
			      "      $name$_ = s;\n"
			      "    }\n");
	}
	lp_print_vars(printer, &g->variables,
		      "    return s;\n"
		      "  } else {\n"
		      "    return (java.lang.String) ref;\n"
		      "  }\n"
		      "}\n");

	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  java.lang.Object ref = $name$_;\n"
		      "  if (ref instanceof String) {\n"
		      "    com.google.protobuf.ByteString b = \n"
		      "        com.google.protobuf.ByteString.copyFromUtf8(\n"
		      "            (java.lang.String) ref);\n"
		      "    $name$_ = b;\n"
		      "    return b;\n"
		      "  } else {\n"
		      "    return (com.google.protobuf.ByteString) ref;\n"
		      "  }\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  $null_check$\n"
		      "  $name$_ = value;\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables, "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n");
	// The default value is not a simple literal so we want to avoid executing
	// it multiple times.  Instead, get the default out of the default instance.
	lp_print_vars(printer, &g->variables, "  $name$_ = getDefaultInstance().get$capitalized_name$();\n");
	lp_print_vars(printer, &g->variables,
		      "  $clear_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");

	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							 /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n"
		      "  $null_check$\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "  checkByteStringIsUtf8(value);\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  $name$_ = value;\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										 LpPrinter *printer)
{
	// noop for primitives
}

static void ImmutableStringFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $default$;\n");
}

static void ImmutableStringFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $default$;\n");
}

static void ImmutableStringFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		// Allow a slight breach of abstraction here in order to avoid forcing
		// all string fields to Strings when copying fields from a Message.
		lp_print_vars(printer, &g->variables,
			      "if (other.has$capitalized_name$()) {\n"
			      "  $name$_ = other.$name$_;\n"
			      "  $set_has_field_bit_builder$\n"
			      "  $on_changed$\n"
			      "}\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "if (!other.get$capitalized_name$().isEmpty()) {\n"
			      "  $name$_ = other.$name$_;\n"
			      "  $set_has_field_bit_builder$\n"
			      "  $on_changed$\n"
			      "}\n");
	}
}

static void ImmutableStringFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  result.$name$_ = $name$_;\n");
	if (g->ops->GetNumBitsForMessage(g) > 0) {
		lp_print_vars(printer, &g->variables, "  $set_has_field_bit_to_local$;\n");
	}
	lp_print(printer, "}\n", NULL);
}

static void ImmutableStringFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "$name$_ = "
			      "input.readStringRequireUtf8();\n"
			      "$set_has_field_bit_builder$\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "$name$_ = input.readBytes();\n"
			      "$set_has_field_bit_builder$\n");
	}
}

static void ImmutableStringFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  $writeString$(output, $number$, $name$_);\n"
		      "}\n");
}

static void ImmutableStringFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  size += $computeStringSize$($number$, $name$_);\n"
		      "}\n");
}

static void ImmutableStringFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!get$capitalized_name$()\n"
		      "    .equals(other.get$capitalized_name$())) return false;\n");
}

static void ImmutableStringFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "hash = (37 * hash) + $constant_name$;\n");
	lp_print_vars(printer, &g->variables, "hash = (53 * hash) + get$capitalized_name$().hashCode();\n");
}

static const char *ImmutableStringFieldGenerator_GetBoxedType(const JvFieldGen *g) { return "java.lang.String"; }

// ===================================================================

static void ImmutableStringOneofFieldGenerator_init(JvFieldGen *g)
{
	ImmutableStringFieldGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutableStringOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_print_extra_field_info(&g->variables, printer);
	JV_DCHECK(proto_field_has_presence(g->descriptor));
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $has_oneof_case_message$;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.lang.String ${$get$capitalized_name$$}$() {\n"
		      "  if ($negated_has_oneof_case_message$) {\n"
		      "    return $default$;\n"
		      "  }\n"
		      "  java.lang.Object ref = $oneof_name$_;\n"
		      "  if (ref instanceof java.lang.String) {\n"
		      "    return (java.lang.String) ref;\n"
		      "  } else {\n"
		      "    com.google.protobuf.ByteString bs = \n"
		      "        (com.google.protobuf.ByteString) ref;\n"
		      "    java.lang.String s = bs.toStringUtf8();\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "    $oneof_name$_ = s;\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "    if (bs.isValidUtf8()) {\n"
			      "      $oneof_name$_ = s;\n"
			      "    }\n");
	}
	lp_print_vars(printer, &g->variables,
		      "    return s;\n"
		      "  }\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);

	lp_print_vars(printer, &g->variables,
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  if ($negated_has_oneof_case_message$) {\n"
		      "    return com.google.protobuf.ByteString.copyFromUtf8($default$);\n"
		      "  }\n"
		      "  java.lang.Object ref = $oneof_name$_;\n"
		      "  if (ref instanceof java.lang.String) {\n"
		      "    com.google.protobuf.ByteString b = \n"
		      "        com.google.protobuf.ByteString.copyFromUtf8(\n"
		      "            (java.lang.String) ref);\n"
		      "    $oneof_name$_ = b;\n"
		      "    return b;\n"
		      "  } else {\n"
		      "    return (com.google.protobuf.ByteString) ref;\n"
		      "  }\n"
		      "}\n");
}

static void ImmutableStringOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
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
		      "  if ($negated_has_oneof_case_message$) {\n"
		      "    return $default$;\n"
		      "  }\n"
		      "  java.lang.Object ref = $oneof_name$_;\n"
		      "  if (!(ref instanceof java.lang.String)) {\n"
		      "    com.google.protobuf.ByteString bs =\n"
		      "        (com.google.protobuf.ByteString) ref;\n"
		      "    java.lang.String s = bs.toStringUtf8();\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "      $oneof_name$_ = s;\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "      if (bs.isValidUtf8()) {\n"
			      "        $oneof_name$_ = s;\n"
			      "      }\n");
	}
	lp_print_vars(printer, &g->variables,
		      "    return s;\n"
		      "  } else {\n"
		      "    return (java.lang.String) ref;\n"
		      "  }\n"
		      "}\n");

	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false,
							 false, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$() {\n"
		      "  if ($negated_has_oneof_case_message$) {\n"
		      "    return com.google.protobuf.ByteString.copyFromUtf8("
		      "        $default$);\n"
		      "  }\n"
		      "  java.lang.Object ref = $oneof_name$_;\n"
		      "  if (ref instanceof String) {\n"
		      "    com.google.protobuf.ByteString b = \n"
		      "        com.google.protobuf.ByteString.copyFromUtf8(\n"
		      "            (java.lang.String) ref);\n"
		      "    $oneof_name$_ = b;\n"
		      "    return b;\n"
		      "  } else {\n"
		      "    return (com.google.protobuf.ByteString) ref;\n"
		      "  }\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  $null_check$\n"
		      "  $set_oneof_case_message$;\n"
		      "  $oneof_name$_ = value;\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $clear_oneof_case_message$;\n"
		      "    $oneof_name$_ = null;\n"
		      "    $on_changed$\n"
		      "  }\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							 /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n"
		      "  $null_check$\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "  checkByteStringIsUtf8(value);\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  $set_oneof_case_message$;\n"
		      "  $oneof_name$_ = value;\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutableStringOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No-Op: String fields in oneofs are correctly cleared by clearing the oneof
}

static void ImmutableStringOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// Allow a slight breach of abstraction here in order to avoid forcing
	// all string fields to Strings when copying fields from a Message.
	lp_print_vars(printer, &g->variables,
		      "$set_oneof_case_message$;\n"
		      "$oneof_name$_ = other.$oneof_name$_;\n"
		      "$on_changed$\n");
}

static void ImmutableStringOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No-Op: oneof fields are built by a single statement
}

static void ImmutableStringOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "$set_oneof_case_message$;\n"
			      "$oneof_name$_ = "
			      "input.readStringRequireUtf8();\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "com.google.protobuf.ByteString bs = input.readBytes();\n"
			      "$set_oneof_case_message$;\n"
			      "$oneof_name$_ = bs;\n");
	}
}

static void ImmutableStringOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  $writeString$(output, $number$, $oneof_name$_);\n"
		      "}\n");
}

static void ImmutableStringOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  size += $computeStringSize$($number$, $oneof_name$_);\n"
		      "}\n");
}

// ===================================================================

static int RepeatedImmutableStringFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static int RepeatedImmutableStringFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void RepeatedImmutableStringFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      // NOTE: the same method in the implementation class actually returns
		      // com.google.protobuf.ProtocolStringList (a subclass of List). It's
		      // changed between protobuf 2.5.0 release and protobuf 2.6.1 release.
		      // To retain binary compatibility with both 2.5.0 and 2.6.1 generated
		      // code, we make this interface method return List so both methods
		      // with different return types exist in the compiled byte code.
		      "$deprecation$java.util.List<java.lang.String>\n"
		      "    get$capitalized_name$List();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$int get$capitalized_name$Count();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables, "$deprecation$java.lang.String get$capitalized_name$(int index);\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							 &g->context->options, false, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$com.google.protobuf.ByteString\n"
		      "    get$capitalized_name$Bytes(int index);\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "@SuppressWarnings(\"serial\")\n"
		      "private com.google.protobuf.LazyStringArrayList $name$_ =\n"
		      "    $empty_list$;\n");
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public com.google.protobuf.ProtocolStringList\n"
		      "    ${$get$capitalized_name$List$}$() {\n"
		      "  return $name$_;\n" // note:  unmodifiable list
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return $name$_.size();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.lang.String "
		      "${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							 &g->context->options, false, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$(int index) {\n"
		      "  return $name$_.getByteString(index);\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// One field is the list and the bit field keeps track of whether the
	// list is immutable. If it's immutable, the invariant is that it must
	// either an instance of Collections.emptyList() or it's an ArrayList
	// wrapped in a Collections.unmodifiableList() wrapper and nobody else has
	// a reference to the underlying ArrayList. This invariant allows us to
	// share instances of lists between protocol buffers avoiding expensive
	// memory allocations. Note, immutable is a strong guarantee here -- not
	// just that the list cannot be modified via the reference but that the
	// list can never be modified.
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.LazyStringArrayList $name$_ =\n"
		      "    $empty_list$;\n");

	lp_print_vars(printer, &g->variables,
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      "  if (!$name$_.isModifiable()) {\n"
		      "    $name$_ = new com.google.protobuf.LazyStringArrayList($name$_);\n"
		      "  }\n"
		      "  $set_has_field_bit_builder$\n"
		      "}\n");

	// Note:  We return an unmodifiable list because otherwise the caller
	//   could hold on to the returned list and modify it after the message
	//   has been built, thus mutating the message which is supposed to be
	//   immutable.
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public com.google.protobuf.ProtocolStringList\n"
		      "    ${$get$capitalized_name$List$}$() {\n"
		      "  $name$_.makeImmutable();\n"
		      "  return $name$_;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return $name$_.size();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.lang.String "
		      "${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							 &g->context->options, false, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public com.google.protobuf.ByteString\n"
		      "    ${$get$capitalized_name$Bytes$}$(int index) {\n"
		      "  return $name$_.getByteString(index);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, java.lang.String value) {\n"
		      "  $null_check$\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.set(index, value);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		      "    java.lang.String value) {\n"
		      "  $null_check$\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.add(value);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		      "    java.lang.Iterable<java.lang.String> values) {\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  com.google.protobuf.AbstractMessageLite.Builder.addAll(\n"
		      "      values, $name$_);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $name$_ =\n"
		      "    $empty_list$;\n"
		      "  $clear_has_field_bit_builder$;\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_string_bytes_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
							 /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$Bytes$}$(\n"
		      "    com.google.protobuf.ByteString value) {\n"
		      "  $null_check$\n");
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "  checkByteStringIsUtf8(value);\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.add(value);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
											 LpPrinter *printer)
{
	// noop for primitives
}

static void RepeatedImmutableStringFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$name$_ =\n"
		      "    $empty_list$;\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$name$_ =\n"
		      "    $empty_list$;\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// The code below does two optimizations:
	//   1. If the other list is empty, there's nothing to do. This ensures we
	//      don't allocate a new array if we already have an immutable one.
	//   2. If the other list is non-empty and our current list is empty, we can
	//      reuse the other list which is guaranteed to be immutable.
	lp_print_vars(printer, &g->variables,
		      "if (!other.$name$_.isEmpty()) {\n"
		      "  if ($name$_.isEmpty()) {\n"
		      "    $name$_ = other.$name$_;\n"
		      "    $set_has_field_bit_builder$\n"
		      "  } else {\n"
		      "    ensure$capitalized_name$IsMutable();\n"
		      "    $name$_.addAll(other.$name$_);\n"
		      "  }\n"
		      "  $on_changed$\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// The code below ensures that the result has an immutable list. If our
	// list is immutable, we can just reuse it. If not, we make it immutable.
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  $name$_.makeImmutable();\n"
		      "  result.$name$_ = $name$_;\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_check_utf8(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "ensure$capitalized_name$IsMutable();\n"
			      "$name$_.add(input.readStringRequireUtf8());\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "com.google.protobuf.ByteString bs = input.readBytes();\n"
			      "ensure$capitalized_name$IsMutable();\n"
			      "$name$_.add(bs);\n");
	}
}

static void RepeatedImmutableStringFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "for (int i = 0; i < $name$_.size(); i++) {\n"
		      "  $writeString$(output, $number$, $name$_.getRaw(i));\n"
		      "}\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "{\n"
		      "  int dataSize = 0;\n");
	lp_indent(printer);

	lp_print_vars(printer, &g->variables,
		      "for (int i = 0; i < $name$_.size(); i++) {\n"
		      "  dataSize += computeStringSizeNoTag($name$_.getRaw(i));\n"
		      "}\n");

	lp_print(printer, "size += dataSize;\n", NULL);

	lp_print_vars(printer, &g->variables, "size += $tag_size$ * get$capitalized_name$List().size();\n");

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void RepeatedImmutableStringFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!get$capitalized_name$List()\n"
		      "    .equals(other.get$capitalized_name$List())) return false;\n");
}

static void RepeatedImmutableStringFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (get$capitalized_name$Count() > 0) {\n"
		      "  hash = (37 * hash) + $constant_name$;\n"
		      "  hash = (53 * hash) + get$capitalized_name$List().hashCode();\n"
		      "}\n");
}

static const char *RepeatedImmutableStringFieldGenerator_GetBoxedType(const JvFieldGen *g) { return "String"; }

static const JvFieldGenOps ImmutableStringFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableStringFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableStringFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableStringFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableStringFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableStringFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableStringFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableStringFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableStringFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableStringFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableStringFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableStringFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableStringFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableStringFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableStringFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableStringFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableStringFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableStringFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps ImmutableStringOneofFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableStringFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableStringFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableStringFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableStringFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableStringFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableStringOneofFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableStringOneofFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableStringFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableStringOneofFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableStringOneofFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableStringOneofFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableStringOneofFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableStringOneofFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableStringFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableStringFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableStringFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableStringOneofFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps RepeatedImmutableStringFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableStringFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableStringFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = RepeatedImmutableStringFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = RepeatedImmutableStringFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = RepeatedImmutableStringFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutableStringFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutableStringFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutableStringFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = RepeatedImmutableStringFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = RepeatedImmutableStringFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = RepeatedImmutableStringFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = RepeatedImmutableStringFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = RepeatedImmutableStringFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode =
		RepeatedImmutableStringFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = RepeatedImmutableStringFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = RepeatedImmutableStringFieldGenerator_GenerateHashCode,
	.GetBoxedType = RepeatedImmutableStringFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = RepeatedImmutableStringFieldGenerator_GenerateSerializationCode,
};

#define RepeatedImmutableStringFieldGenerator_init ImmutableStringFieldGenerator_init
