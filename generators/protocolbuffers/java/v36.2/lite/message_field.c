// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/message_field.{h,cc} (protobuf v36.2). */

static int ImmutableMessageFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								 JvUtf16 *output);
static const char *ImmutableMessageFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutableMessageOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								      JvUtf16 *output);
static void ImmutableMessageOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutableMessageFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void RepeatedImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g,
										LpPrinter *printer);
static void RepeatedImmutableMessageFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									 JvUtf16 *output);
static void RepeatedImmutableMessageFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g,
										  LpPrinter *printer);
static const char *RepeatedImmutableMessageFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);

static void SetMessageVariables_lite_message(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
					     const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "type", jv_immutable_class_name(descriptor->message_type));
	lp_vars_set(variables, "group_or_message", (jv_get_type(descriptor) == PT_GROUP) ? "Group" : "Message");
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(variables, "required", proto_field_is_required(descriptor) ? "true" : "false");

	if (jv_has_hasbit(descriptor)) {
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
			       xcat(lp_vars_get(variables, "name"), "_ != null"));
	}

	lp_vars_set(variables, "get_has_field_bit_from_local", jv_generate_get_bit_from_local(builderBitIndex));
	lp_vars_set(variables, "set_has_field_bit_to_local", jv_generate_set_bit_to_local(messageBitIndex));

	// Annotations often use { and } to determine ranges.
	lp_vars_set(variables, "{", "");
	lp_vars_set(variables, "}", "");
}

// ===================================================================

static void ImmutableMessageFieldLiteGenerator_init(JvFieldGen *g)
{
	SetMessageVariables_lite_message(g->descriptor, g->message_bit_index, g->builder_bit_index,
					 jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static int ImmutableMessageFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	// TODO: We don't need a has bit for messages as they have null
	// sentinels and no user should be reflecting on this. We could save some
	// bits by setting to 0 and updating the runtimes but this might come at a
	// runtime performance cost since we can't memoize has-bit reads.
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static void ImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$boolean ${$has$capitalized_name$$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ ${$get$capitalized_name$$}$();\n");
}

static void ImmutableMessageFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{

	lp_print_vars(printer, &g->variables, "private $type$ $name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);

	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $is_field_present_message$;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return $name$_ == null ? $type$.getDefaultInstance() : $name$_;\n"
		      "}\n");
	// Field.Builder setField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$($type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  $name$_ = value;\n"
		      "  $set_has_field_bit_message$\n"
		      "}\n");

	// Field.Builder mergeField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.SuppressWarnings(\"ReferenceEquality\")\n"
		      "private void merge$capitalized_name$($type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  if ($name$_ != null &&\n"
		      "      $name$_ != $type$.getDefaultInstance()) {\n"
		      "    $name$_ =\n"
		      "      $type$.newBuilder($name$_).mergeFrom(value).buildPartial();\n"
		      "  } else {\n"
		      "    $name$_ = value;\n"
		      "  }\n"
		      "  $set_has_field_bit_message$\n"
		      "}\n");

	// Field.Builder clearField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $name$_ = null;\n"
		      "  $clear_has_field_bit_message$\n"
		      "}\n");
}

static void ImmutableMessageFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// The comments above the methods below are based on a hypothetical
	// field of type "Field" called "Field".

	// boolean hasField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return instance.has$capitalized_name$();\n"
		      "}\n");
	// Field getField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return instance.get$capitalized_name$();\n"
		      "}\n");
	// Field.Builder setField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(value);\n"
		      "  return this;\n"
		      "  }\n");
	// Field.Builder setField(Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    $type$.Builder builderForValue) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(builderForValue.build());\n"
		      "  return this;\n"
		      "}\n");
	// Field.Builder mergeField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$merge$capitalized_name$$}$($type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.merge$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	// Field.Builder clearField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {"
		      "  copyOnWrite();\n"
		      "  instance.clear$capitalized_name$();\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutableMessageFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								 JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	if (jv_has_hasbit(g->descriptor)) {
		jv_write_int_to_utf16_char_sequence(g->message_bit_index, output);
	}
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
}

static void ImmutableMessageFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer) {}

static const char *ImmutableMessageFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->message_type);
}

// ===================================================================

static void ImmutableMessageOneofFieldLiteGenerator_init(JvFieldGen *g)
{
	ImmutableMessageFieldLiteGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutableMessageOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $has_oneof_case_message$;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "     return ($type$) $oneof_name$_;\n"
		      "  }\n"
		      "  return $type$.getDefaultInstance();\n"
		      "}\n");
	// Field.Builder setField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$($type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  $oneof_name$_ = value;\n"
		      "  $set_oneof_case_message$;\n"
		      "}\n");

	// Field.Builder mergeField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void merge$capitalized_name$($type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  if ($has_oneof_case_message$ &&\n"
		      "      $oneof_name$_ != $type$.getDefaultInstance()) {\n"
		      "    $oneof_name$_ = $type$.newBuilder(($type$) $oneof_name$_)\n"
		      "        .mergeFrom(value).buildPartial();\n"
		      "  } else {\n"
		      "    $oneof_name$_ = value;\n"
		      "  }\n"
		      "  $set_oneof_case_message$;\n"
		      "}\n");

	// Field.Builder clearField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $clear_oneof_case_message$;\n"
		      "    $oneof_name$_ = null;\n"
		      "  }\n"
		      "}\n");
}

static void ImmutableMessageOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								      JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	jv_write_int_to_utf16_char_sequence(g->descriptor->containing_oneof->index, output);
	lp_print_vars(printer, &g->variables, "$oneof_stored_type$.class,\n");
}

static void ImmutableMessageOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// The comments above the methods below are based on a hypothetical
	// field of type "Field" called "Field".

	// boolean hasField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return instance.has$capitalized_name$();\n"
		      "}\n");
	// Field getField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return instance.get$capitalized_name$();\n"
		      "}\n");
	// Field.Builder setField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	// Field.Builder setField(Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    $type$.Builder builderForValue) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(builderForValue.build());\n"
		      "  return this;\n"
		      "}\n");
	// Field.Builder mergeField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$merge$capitalized_name$$}$($type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.merge$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	// Field.Builder clearField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  copyOnWrite();\n"
		      "  instance.clear$capitalized_name$();\n"
		      "  return this;\n"
		      "}\n");
}

// ===================================================================

static int RepeatedImmutableMessageFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static void RepeatedImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// TODO: In the future, consider having methods specific to the
	// interface so that builders can choose dynamically to either return a
	// message or a nested builder, so that asking for the interface doesn't
	// cause a message to ever be built.
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<$type$> \n"
		      "    ${$get$capitalized_name$List$}$();\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ ${$get$capitalized_name$$}$(int index);\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables, "$deprecation$int ${$get$capitalized_name$Count$}$();\n");
}

static void RepeatedImmutableMessageFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private com.google.protobuf.Internal.ProtobufList<$type$> $name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return $name$_;\n" // note:  unmodifiable list
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.util.List<? extends $type$OrBuilder> \n"
		      "    ${$get$capitalized_name$OrBuilderList$}$() {\n"
		      "  return $name$_;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return $name$_.size();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$(\n"
		      "    int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
	lp_print_vars(printer, &g->variables,
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      // Use a temporary to avoid a redundant iget-object.
		      "  com.google.protobuf.Internal.ProtobufList<$type$> tmp = $name$_;\n"
		      "  if (!tmp.isModifiable()) {\n"
		      "    $name$_ =\n"
		      "        com.google.protobuf.GeneratedMessageLite.mutableCopy(tmp);\n"
		      "   }\n"
		      "}\n"
		      "\n");

	// Builder setRepeatedField(int index, Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$(\n"
		      "    int index, $type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.set(index, value);\n"
		      "}\n");

	// Builder addRepeatedField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void add$capitalized_name$($type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.add(value);\n"
		      "}\n");

	// Builder addRepeatedField(int index, Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void add$capitalized_name$(\n"
		      "    int index, $type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.add(index, value);\n"
		      "}\n");

	// Builder addAllRepeatedField(Iterable<Field> values)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void addAll$capitalized_name$(\n"
		      "    java.lang.Iterable<? extends $type$> values) {\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  com.google.protobuf.AbstractMessageLite.addAll(\n"
		      "      values, $name$_);\n"
		      "}\n");

	// Builder clearAllRepeatedField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $name$_ = emptyProtobufList();\n"
		      "}\n");

	// Builder removeRepeatedField(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private void remove$capitalized_name$(int index) {\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.remove(index);\n"
		      "}\n");
}

static void RepeatedImmutableMessageFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// The comments above the methods below are based on a hypothetical
	// repeated field of type "Field" called "RepeatedField".

	// List<Field> getRepeatedFieldList()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return java.util.Collections.unmodifiableList(\n"
		      "      instance.get$capitalized_name$List());\n"
		      "}\n");
	// int getRepeatedFieldCount()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return instance.get$capitalized_name$Count();\n"
		      "}");
	// Field getRepeatedField(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return instance.get$capitalized_name$(index);\n"
		      "}\n");
	// Builder setRepeatedField(int index, Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, $type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(index, value);\n"
		      "  return this;\n"
		      "}\n");
	// Builder setRepeatedField(int index, Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, $type$.Builder builderForValue) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(index,\n"
		      "      builderForValue.build());\n"
		      "  return this;\n"
		      "}\n");
	// Builder addRepeatedField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$add$capitalized_name$$}$($type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	// Builder addRepeatedField(int index, Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		      "    int index, $type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$(index, value);\n"
		      "  return this;\n"
		      "}\n");
	// Builder addRepeatedField(Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		      "    $type$.Builder builderForValue) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$(builderForValue.build());\n"
		      "  return this;\n"
		      "}\n");
	// Builder addRepeatedField(int index, Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		      "    int index, $type$.Builder builderForValue) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$(index,\n"
		      "      builderForValue.build());\n"
		      "  return this;\n"
		      "}\n");
	// Builder addAllRepeatedField(Iterable<Field> values)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		      "    java.lang.Iterable<? extends $type$> values) {\n"
		      "  copyOnWrite();\n"
		      "  instance.addAll$capitalized_name$(values);\n"
		      "  return this;\n"
		      "}\n");
	// Builder clearAllRepeatedField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  copyOnWrite();\n"
		      "  instance.clear$capitalized_name$();\n"
		      "  return this;\n"
		      "}\n");
	// Builder removeRepeatedField(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$remove$capitalized_name$$}$(int index) {\n"
		      "  copyOnWrite();\n"
		      "  instance.remove$capitalized_name$(index);\n"
		      "  return this;\n"
		      "}\n");
}

static void RepeatedImmutableMessageFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									 JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	lp_print_vars(printer, &g->variables,
		      "\"$name$_\",\n"
		      "$type$.class,\n");
}

static void RepeatedImmutableMessageFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g,
										  LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = emptyProtobufList();\n");
}

static const char *RepeatedImmutableMessageFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->message_type);
}

static const JvFieldGenOps ImmutableMessageFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableMessageFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableMessageFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableMessageFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableMessageFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableMessageFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableMessageFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps ImmutableMessageOneofFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableMessageFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableMessageOneofFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableMessageOneofFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableMessageFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableMessageFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableMessageOneofFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps RepeatedImmutableMessageFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = RepeatedImmutableMessageFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = RepeatedImmutableMessageFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutableMessageFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutableMessageFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutableMessageFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = RepeatedImmutableMessageFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = RepeatedImmutableMessageFieldLiteGenerator_GenerateFieldInfo,
};

#define RepeatedImmutableMessageFieldLiteGenerator_init ImmutableMessageFieldLiteGenerator_init
