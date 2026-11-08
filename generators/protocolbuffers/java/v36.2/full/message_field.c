// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/message_field.{h,cc} (protobuf v36.2). */

static int ImmutableMessageFieldGenerator_GetMessageBitIndex(const JvFieldGen *g);
static int ImmutableMessageFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g);
static int ImmutableMessageFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutableMessageFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_PrintNestedBuilderCondition(const JvFieldGen *g, LpPrinter *printer,
								       const char *regular_case,
								       const char *nested_builder_case);
static void ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(const JvFieldGen *g, LpPrinter *printer,
								      const char *method_prototype,
								      const char *regular_case,
								      const char *nested_builder_case,
								      const char *trailing_code, int semantic);
static void ImmutableMessageFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										  LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutableMessageFieldGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutableMessageOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMessageOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutableMessageFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static int RepeatedImmutableMessageFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g);
static void RepeatedImmutableMessageFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(const JvFieldGen *g, LpPrinter *printer,
									       const char *regular_case,
									       const char *nested_builder_case);
static void RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(const JvFieldGen *g, LpPrinter *printer,
									      const char *method_prototype,
									      const char *regular_case,
									      const char *nested_builder_case,
									      const char *trailing_code, int semantic);
static void RepeatedImmutableMessageFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
											  LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableMessageFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *RepeatedImmutableMessageFieldGenerator_GetBoxedType(const JvFieldGen *g);

static void SetMessageVariables_message(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
					const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "type", jv_immutable_class_name(descriptor->message_type));
	lp_vars_set(variables, "group_or_message", (jv_get_type(descriptor) == PT_GROUP) ? "Group" : "Message");
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(variables, "on_changed", "onChanged();");
	lp_vars_set(variables, "get_parser", "parser()");

	if (jv_has_hasbit(descriptor)) {
		// For singular messages and builders, one bit is used for the hasField bit.
		// Note that these have a trailing ";".
		lp_vars_set(variables, "set_has_field_bit_to_local", jv_generate_set_bit_to_local(messageBitIndex));

		lp_vars_set(variables, "is_field_present_message", jv_generate_get_bit(messageBitIndex));
	} else {
		lp_vars_set(variables, "set_has_field_bit_to_local", "");
		lp_vars_insert(variables, "is_field_present_message",
			       xcat(lp_vars_get(variables, "name"), "_ != null"));
	}

	// For repeated builders, one bit is used for whether the array is immutable.
	lp_vars_set(variables, "get_mutable_bit_builder", jv_generate_get_bit(builderBitIndex));
	lp_vars_set(variables, "set_mutable_bit_builder", jv_generate_set_bit(builderBitIndex));
	lp_vars_set(variables, "clear_mutable_bit_builder", jv_generate_clear_bit(builderBitIndex));

	lp_vars_set(variables, "get_has_field_bit_builder", jv_generate_get_bit(builderBitIndex));
	lp_vars_set(variables, "set_has_field_bit_builder", xcat(jv_generate_set_bit(builderBitIndex), ";"));
	lp_vars_set(variables, "clear_has_field_bit_builder", xcat(jv_generate_clear_bit(builderBitIndex), ";"));
	lp_vars_set(variables, "get_has_field_bit_from_local", jv_generate_get_bit_from_local(builderBitIndex));

	lp_vars_set(variables, "tag_size", xcat(jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor)))));
}

// ===================================================================

static void ImmutableMessageFieldGenerator_init(JvFieldGen *g)
{
	SetMessageVariables_message(g->descriptor, g->message_bit_index, g->builder_bit_index,
				    jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static int ImmutableMessageFieldGenerator_GetMessageBitIndex(const JvFieldGen *g) { return g->message_bit_index; }

static int ImmutableMessageFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g) { return g->builder_bit_index; }

static int ImmutableMessageFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static int ImmutableMessageFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void ImmutableMessageFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// TODO: In the future, consider having a method specific to the
	// interface so that builders can choose dynamically to either return a
	// message or a nested builder, so that asking for the interface doesn't
	// cause a message to ever be built.
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$boolean has$capitalized_name$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ get$capitalized_name$();\n");

	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$OrBuilder get$capitalized_name$OrBuilder();\n");
}

static void ImmutableMessageFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private $type$ $name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $is_field_present_message$;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return $name$_ == null ? $type$.getDefaultInstance() : $name$_;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$() {\n"
		      "  return $name$_ == null ? $type$.getDefaultInstance() : $name$_;\n"
		      "}\n");
}

static void ImmutableMessageFieldGenerator_PrintNestedBuilderCondition(const JvFieldGen *g, LpPrinter *printer,
								       const char *regular_case,
								       const char *nested_builder_case)
{
	lp_print_vars(printer, &g->variables, "if ($name$Builder_ == null) {\n");
	lp_indent(printer);
	lp_print_vars(printer, &g->variables, regular_case);
	lp_outdent(printer);
	lp_print(printer, "} else {\n", NULL);
	lp_indent(printer);
	lp_print_vars(printer, &g->variables, nested_builder_case);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(const JvFieldGen *g, LpPrinter *printer,
								      const char *method_prototype,
								      const char *regular_case,
								      const char *nested_builder_case,
								      const char *trailing_code, int semantic)
{
	lp_print_vars(printer, &g->variables, method_prototype);
	lp_print(printer, " {\n", NULL);
	lp_indent(printer);
	ImmutableMessageFieldGenerator_PrintNestedBuilderCondition(g, printer, regular_case, nested_builder_case);
	if (trailing_code != NULL) {
		lp_print_vars(printer, &g->variables, trailing_code);
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void ImmutableMessageFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// When using nested-builders, the code initially works just like the
	// non-nested builder case. It only creates a nested builder lazily on
	// demand and then forever delegates to it after creation.
	lp_print_vars(printer, &g->variables, "private $type$ $name$_;\n");

	lp_print_vars(printer, &g->variables,
		      // If this builder is non-null, it is used and the other fields
		      // are ignored.
		      "private com.google.protobuf.SingleFieldBuilder<\n"
		      "    $type$, $type$.Builder, $type$OrBuilder> $name$Builder_;"
		      "\n");

	// The comments above the methods below are based on a hypothetical
	// field of type "Field" called "Field".

	// boolean hasField()
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $get_has_field_bit_builder$;\n"
		      "}\n");
	// Field getField()
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public $type$ ${$get$capitalized_name$$}$()",
		"return $name$_ == null ? $type$.getDefaultInstance() : $name$_;\n",
		"return $name$Builder_.getMessage();\n", NULL, 0);

	// Field.Builder setField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$set$capitalized_name$$}$($type$ value)",

		"java.util.Objects.requireNonNull(value);\n"
		"$name$_ = value;\n",

		"$name$Builder_.setMessage(value);\n",

		"$set_has_field_bit_builder$\n"
		"$on_changed$\n"
		"return this;\n",
		0);

	// Field.Builder setField(Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		"    $type$.Builder builderForValue)",

		"$name$_ = builderForValue.build();\n",

		"$name$Builder_.setMessage(builderForValue.build());\n",

		"$set_has_field_bit_builder$\n"
		"$on_changed$\n"
		"return this;\n",
		0);

	// Message.Builder mergeField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$merge$capitalized_name$$}$($type$ value)",
		"if ($get_has_field_bit_builder$ &&\n"
		"  $name$_ != null &&\n"
		"  $name$_ != $type$.getDefaultInstance()) {\n"
		"  get$capitalized_name$Builder().mergeFrom(value);\n"
		"} else {\n"
		"  $name$_ = value;\n"
		"}\n",

		"$name$Builder_.mergeFrom(value);\n",

		"if ($name$_ != null) {\n"
		"  $set_has_field_bit_builder$\n"
		"  $on_changed$\n"
		"}\n"
		"return this;\n",
		0);

	// Message.Builder clearField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $clear_has_field_bit_builder$\n"
		      "  $name$_ = null;\n"
		      "  if ($name$Builder_ != null) {\n"
		      "    $name$Builder_.dispose();\n"
		      "    $name$Builder_ = null;\n"
		      "  }\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	// Field.Builder getFieldBuilder()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$.Builder "
		      "${$get$capitalized_name$Builder$}$() {\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return "
		      "internalGet$capitalized_name$FieldBuilder().getBuilder();\n"
		      "}\n");
	// FieldOrBuilder getFieldOrBuilder()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$() {\n"
		      "  if ($name$Builder_ != null) {\n"
		      "    return $name$Builder_.getMessageOrBuilder();\n"
		      "  } else {\n"
		      "    return $name$_ == null ?\n"
		      "        $type$.getDefaultInstance() : $name$_;\n"
		      "  }\n"
		      "}\n");
	// SingleFieldBuilder getFieldFieldBuilder
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.SingleFieldBuilder<\n"
		      "    $type$, $type$.Builder, $type$OrBuilder> \n"
		      "    internalGet$capitalized_name$FieldBuilder() {\n"
		      "  if ($name$Builder_ == null) {\n"
		      "    $name$Builder_ = new com.google.protobuf.SingleFieldBuilder<\n"
		      "        $type$, $type$.Builder, $type$OrBuilder>(\n"
		      "            get$capitalized_name$(),\n"
		      "            getParentForChildren(),\n"
		      "            isClean());\n"
		      "    $name$_ = null;\n"
		      "  }\n"
		      "  return $name$Builder_;\n"
		      "}\n");
}

static void ImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										  LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "internalGet$capitalized_name$FieldBuilder();\n");
}

static void ImmutableMessageFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer) {}

static void ImmutableMessageFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No need to clear the has-bit since we clear the bitField ints all at once.
	lp_print_vars(printer, &g->variables,
		      "$name$_ = null;\n"
		      "if ($name$Builder_ != null) {\n"
		      "  $name$Builder_.dispose();\n"
		      "  $name$Builder_ = null;\n"
		      "}\n");
}

static void ImmutableMessageFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (other.has$capitalized_name$()) {\n"
		      "  merge$capitalized_name$(other.get$capitalized_name$());\n"
		      "}\n");
}

static void ImmutableMessageFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  result.$name$_ = $name$Builder_ == null\n"
		      "      ? $name$_\n"
		      "      : $name$Builder_.build();\n");
	if (g->ops->GetNumBitsForMessage(g) > 0) {
		lp_print_vars(printer, &g->variables, "  $set_has_field_bit_to_local$;\n");
	}
	lp_print(printer, "}\n", NULL);
}

static void ImmutableMessageFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_get_type(g->descriptor) == PT_GROUP) {
		lp_print_vars(printer, &g->variables,
			      "input.readGroup($number$,\n"
			      "    "
			      "internalGet$capitalized_name$FieldBuilder().getBuilder(),\n"
			      "    extensionRegistry);\n"
			      "$set_has_field_bit_builder$\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "input.readMessage(\n"
			      "    "
			      "internalGet$capitalized_name$FieldBuilder().getBuilder(),\n"
			      "    extensionRegistry);\n"
			      "$set_has_field_bit_builder$\n");
	}
}

static void ImmutableMessageFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  output.write$group_or_message$($number$, get$capitalized_name$());\n"
		      "}\n");
}

static void ImmutableMessageFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "    .compute$group_or_message$Size($number$, get$capitalized_name$());\n"
		      "}\n");
}

static void ImmutableMessageFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!get$capitalized_name$()\n"
		      "    .equals(other.get$capitalized_name$())) return false;\n");
}

static void ImmutableMessageFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "hash = (37 * hash) + $constant_name$;\n"
		      "hash = (53 * hash) + get$capitalized_name$().hashCode();\n");
}

static const char *ImmutableMessageFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->message_type);
}

// ===================================================================

static void ImmutableMessageOneofFieldGenerator_init(JvFieldGen *g)
{
	ImmutableMessageFieldGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutableMessageOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_print_extra_field_info(&g->variables, printer);
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "     return ($type$) $oneof_name$_;\n"
		      "  }\n"
		      "  return $type$.getDefaultInstance();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "     return ($type$) $oneof_name$_;\n"
		      "  }\n"
		      "  return $type$.getDefaultInstance();\n"
		      "}\n");
}

static void ImmutableMessageOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// When using nested-builders, the code initially works just like the
	// non-nested builder case. It only creates a nested builder lazily on
	// demand and then forever delegates to it after creation.
	lp_print_vars(printer, &g->variables,
		      // If this builder is non-null, it is used and the other fields
		      // are ignored.
		      "private com.google.protobuf.SingleFieldBuilder<\n"
		      "    $type$, $type$.Builder, $type$OrBuilder> $name$Builder_;"
		      "\n");

	// The comments above the methods below are based on a hypothetical
	// field of type "Field" called "Field".

	// boolean hasField()
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $has_oneof_case_message$;\n"
		      "}\n");
	// Field getField()
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"@java.lang.Override\n"
		"$deprecation$public $type$ ${$get$capitalized_name$$}$()",

		"if ($has_oneof_case_message$) {\n"
		"  return ($type$) $oneof_name$_;\n"
		"}\n"
		"return $type$.getDefaultInstance();\n",

		"if ($has_oneof_case_message$) {\n"
		"  return $name$Builder_.getMessage();\n"
		"}\n"
		"return $type$.getDefaultInstance();\n",

		NULL, 0);

	// Field.Builder setField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$set$capitalized_name$$}$($type$ value)",

		"java.util.Objects.requireNonNull(value);\n"
		"$oneof_name$_ = value;\n"
		"$on_changed$\n",

		"$name$Builder_.setMessage(value);\n",

		"$set_oneof_case_message$;\n"
		"return this;\n",
		0);

	// Field.Builder setField(Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		"    $type$.Builder builderForValue)",

		"$oneof_name$_ = builderForValue.build();\n"
		"$on_changed$\n",

		"$name$Builder_.setMessage(builderForValue.build());\n",

		"$set_oneof_case_message$;\n"
		"return this;\n",
		0);

	// Field.Builder mergeField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$merge$capitalized_name$$}$($type$ value)",

		"if ($has_oneof_case_message$ &&\n"
		"    $oneof_name$_ != $type$.getDefaultInstance()) {\n"
		"  $oneof_name$_ = $type$.newBuilder(($type$) $oneof_name$_)\n"
		"      .mergeFrom(value).buildPartial();\n"
		"} else {\n"
		"  $oneof_name$_ = value;\n"
		"}\n"
		"$on_changed$\n",

		"if ($has_oneof_case_message$) {\n"
		"  $name$Builder_.mergeFrom(value);\n"
		"} else {\n"
		"  $name$Builder_.setMessage(value);\n"
		"}\n",

		"$set_oneof_case_message$;\n"
		"return this;\n",
		0);

	// Field.Builder clearField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	ImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$clear$capitalized_name$$}$()",

		"if ($has_oneof_case_message$) {\n"
		"  $clear_oneof_case_message$;\n"
		"  $oneof_name$_ = null;\n"
		"  $on_changed$\n"
		"}\n",

		"if ($has_oneof_case_message$) {\n"
		"  $clear_oneof_case_message$;\n"
		"  $oneof_name$_ = null;\n"
		"}\n"
		"$name$Builder_.clear();\n",

		"return this;\n", 0);

	// $type$.Builder getFieldBuilder
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$.Builder "
		      "${$get$capitalized_name$Builder$}$() {\n"
		      "  return "
		      "internalGet$capitalized_name$FieldBuilder().getBuilder();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$() {\n"
		      "  if (($has_oneof_case_message$) && ($name$Builder_ != null)) {\n"
		      "    return $name$Builder_.getMessageOrBuilder();\n"
		      "  } else {\n"
		      "    if ($has_oneof_case_message$) {\n"
		      "      return ($type$) $oneof_name$_;\n"
		      "    }\n"
		      "    return $type$.getDefaultInstance();\n"
		      "  }\n"
		      "}\n");
	// SingleFieldBuilder internalGetFieldFieldBuilder
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.SingleFieldBuilder<\n"
		      "    $type$, $type$.Builder, $type$OrBuilder> \n"
		      "    ${$internalGet$capitalized_name$FieldBuilder$}$() {\n"
		      "  if ($name$Builder_ == null) {\n"
		      "    if (!($has_oneof_case_message$)) {\n"
		      "      $oneof_name$_ = $type$.getDefaultInstance();\n"
		      "    }\n"
		      "    $name$Builder_ = new com.google.protobuf.SingleFieldBuilder<\n"
		      "        $type$, $type$.Builder, $type$OrBuilder>(\n"
		      "            ($type$) $oneof_name$_,\n"
		      "            getParentForChildren(),\n"
		      "            isClean());\n"
		      "    $oneof_name$_ = null;\n"
		      "  }\n"
		      "  $set_oneof_case_message$;\n"
		      "  $on_changed$\n"
		      "  return $name$Builder_;\n"
		      "}\n");
}

static void ImmutableMessageOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// Make sure the builder gets cleared.
	lp_print_vars(printer, &g->variables,
		      "if ($name$Builder_ != null) {\n"
		      "  $name$Builder_.clear();\n"
		      "}\n");
}

static void ImmutableMessageOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$ &&\n"
		      "    $name$Builder_ != null) {\n"
		      "  result.$oneof_name$_ = $name$Builder_.build();\n"
		      "}\n");
}

static void ImmutableMessageOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "merge$capitalized_name$(other.get$capitalized_name$());\n");
}

static void ImmutableMessageOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_get_type(g->descriptor) == PT_GROUP) {
		lp_print_vars(printer, &g->variables,
			      "input.readGroup($number$,\n"
			      "    "
			      "internalGet$capitalized_name$FieldBuilder().getBuilder(),\n"
			      "    extensionRegistry);\n"
			      "$set_oneof_case_message$;\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "input.readMessage(\n"
			      "    "
			      "internalGet$capitalized_name$FieldBuilder().getBuilder(),\n"
			      "    extensionRegistry);\n"
			      "$set_oneof_case_message$;\n");
	}
}

static void ImmutableMessageOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  output.write$group_or_message$($number$, ($type$) $oneof_name$_);\n"
		      "}\n");
}

static void ImmutableMessageOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "    .compute$group_or_message$Size($number$, ($type$) $oneof_name$_);\n"
		      "}\n");
}

// ===================================================================

static int RepeatedImmutableMessageFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static int RepeatedImmutableMessageFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void RepeatedImmutableMessageFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// TODO: In the future, consider having methods specific to the
	// interface so that builders can choose dynamically to either return a
	// message or a nested builder, so that asking for the interface doesn't
	// cause a message to ever be built.
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<$type$> \n"
		      "    get$capitalized_name$List();\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ get$capitalized_name$(int index);\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables, "$deprecation$int get$capitalized_name$Count();\n");

	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<? extends $type$OrBuilder> \n"
		      "    get$capitalized_name$OrBuilderList();\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$$type$OrBuilder get$capitalized_name$OrBuilder(\n"
		      "    int index);\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "@SuppressWarnings(\"serial\")\n"
		      "private java.util.List<$type$> $name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return $name$_;\n" // note:  unmodifiable list
		      "}\n");
	// List<FieldOrBuilder> getFieldOrBuilderList()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<? extends $type$OrBuilder> \n"
		      "    ${$get$capitalized_name$OrBuilderList$}$() {\n"
		      "  return $name$_;\n"
		      "}\n");
	// int getFieldCount()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return $name$_.size();\n"
		      "}\n");
	// Field getField(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
	// FieldOrBuilder getFieldOrBuilder(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$(\n"
		      "    int index) {\n"
		      "  return $name$_.get(index);\n"
		      "}\n");
}

static void RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(const JvFieldGen *g, LpPrinter *printer,
									       const char *regular_case,
									       const char *nested_builder_case)
{
	lp_print_vars(printer, &g->variables, "if ($name$Builder_ == null) {\n");
	lp_indent(printer);
	lp_print_vars(printer, &g->variables, regular_case);
	lp_outdent(printer);
	lp_print(printer, "} else {\n", NULL);
	lp_indent(printer);
	lp_print_vars(printer, &g->variables, nested_builder_case);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(const JvFieldGen *g, LpPrinter *printer,
									      const char *method_prototype,
									      const char *regular_case,
									      const char *nested_builder_case,
									      const char *trailing_code, int semantic)
{
	lp_print_vars(printer, &g->variables, method_prototype);
	lp_print(printer, " {\n", NULL);
	lp_indent(printer);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(g, printer, regular_case,
									   nested_builder_case);
	if (trailing_code != NULL) {
		lp_print_vars(printer, &g->variables, trailing_code);
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void RepeatedImmutableMessageFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// When using nested-builders, the code initially works just like the
	// non-nested builder case. It only creates a nested builder lazily on
	// demand and then forever delegates to it after creation.

	lp_print_vars(printer, &g->variables,
		      // Used when the builder is null.
		      // One field is the list and the other field keeps track of whether the
		      // list is immutable. If it's immutable, the invariant is that it must
		      // either an instance of Collections.emptyList() or it's an ArrayList
		      // wrapped in a Collections.unmodifiableList() wrapper and nobody else has
		      // a reference to the underlying ArrayList. This invariant allows us to
		      // share instances of lists between protocol buffers avoiding expensive
		      // memory allocations. Note, immutable is a strong guarantee here -- not
		      // just that the list cannot be modified via the reference but that the
		      // list can never be modified.
		      "private java.util.List<$type$> $name$_ =\n"
		      "  java.util.Collections.emptyList();\n"

		      "private void ensure$capitalized_name$IsMutable() {\n"
		      "  if (!$get_mutable_bit_builder$) {\n"
		      "    $name$_ = new java.util.ArrayList<$type$>($name$_);\n"
		      "    $set_mutable_bit_builder$;\n"
		      "   }\n"
		      "}\n"
		      "\n");

	lp_print_vars(printer, &g->variables,
		      // If this builder is non-null, it is used and the other fields are
		      // ignored.
		      "private com.google.protobuf.RepeatedFieldBuilder<\n"
		      "    $type$, $type$.Builder, $type$OrBuilder> $name$Builder_;\n"
		      "\n");

	// The comments above the methods below are based on a hypothetical
	// repeated field of type "Field" called "RepeatedField".

	// List<Field> getRepeatedFieldList()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public java.util.List<$type$> "
		"${$get$capitalized_name$List$}$()",

		"return java.util.Collections.unmodifiableList($name$_);\n",
		"return $name$Builder_.getMessageList();\n",

		NULL, 0);

	// int getRepeatedFieldCount()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public int ${$get$capitalized_name$Count$}$()",

		"return $name$_.size();\n", "return $name$Builder_.getCount();\n",

		NULL, 0);

	// Field getRepeatedField(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index)",

		"return $name$_.get(index);\n",

		"return $name$Builder_.getMessage(index);\n",

		NULL, 0);

	// Builder setRepeatedField(int index, Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		"    int index, $type$ value)",
		"java.util.Objects.requireNonNull(value);\n"
		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.set(index, value);\n"
		"$on_changed$\n",
		"$name$Builder_.setMessage(index, value);\n", "return this;\n", 0);

	// Builder setRepeatedField(int index, Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		"    int index, $type$.Builder builderForValue)",

		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.set(index, builderForValue.build());\n"
		"$on_changed$\n",

		"$name$Builder_.setMessage(index, builderForValue.build());\n",

		"return this;\n", 0);

	// Builder addRepeatedField(Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$add$capitalized_name$$}$($type$ value)",

		"java.util.Objects.requireNonNull(value);\n"
		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.add(value);\n"

		"$on_changed$\n",

		"$name$Builder_.addMessage(value);\n",

		"return this;\n", 0);

	// Builder addRepeatedField(int index, Field value)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		"    int index, $type$ value)",

		"java.util.Objects.requireNonNull(value);\n"
		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.add(index, value);\n"
		"$on_changed$\n",

		"$name$Builder_.addMessage(index, value);\n",

		"return this;\n", 0);

	// Builder addRepeatedField(Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		"    $type$.Builder builderForValue)",

		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.add(builderForValue.build());\n"
		"$on_changed$\n",

		"$name$Builder_.addMessage(builderForValue.build());\n",

		"return this;\n", 0);

	// Builder addRepeatedField(int index, Field.Builder builderForValue)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$add$capitalized_name$$}$(\n"
		"    int index, $type$.Builder builderForValue)",

		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.add(index, builderForValue.build());\n"
		"$on_changed$\n",

		"$name$Builder_.addMessage(index, builderForValue.build());\n",

		"return this;\n", 0);

	// Builder addAllRepeatedField(Iterable<Field> values)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer,
		"$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		"    java.lang.Iterable<? extends $type$> values)",

		"ensure$capitalized_name$IsMutable();\n"
		"com.google.protobuf.AbstractMessageLite.Builder.addAll(\n"
		"    values, $name$_);\n"
		"$on_changed$\n",

		"$name$Builder_.addAllMessages(values);\n",

		"return this;\n", 0);

	// Builder clearRepeatedField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$clear$capitalized_name$$}$()",

		"$name$_ = java.util.Collections.emptyList();\n"
		"$clear_mutable_bit_builder$;\n"
		"$on_changed$\n",

		"$name$Builder_.clear();\n",

		"return this;\n", 0);

	// Builder removeRepeatedField(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderFunction(
		g, printer, "$deprecation$public Builder ${$remove$capitalized_name$$}$(int index)",

		"ensure$capitalized_name$IsMutable();\n"
		"$name$_.remove(index);\n"
		"$on_changed$\n",

		"$name$Builder_.remove(index);\n",

		"return this;\n", 0);

	// Field.Builder getRepeatedFieldBuilder(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$.Builder ${$get$capitalized_name$Builder$}$(\n"
		      "    int index) {\n"
		      "  return "
		      "internalGet$capitalized_name$FieldBuilder().getBuilder(index);"
		      "\n"
		      "}\n");
	// FieldOrBuilder getRepeatedFieldOrBuilder(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$OrBuilder "
		      "${$get$capitalized_name$OrBuilder$}$(\n"
		      "    int index) {\n"
		      "  if ($name$Builder_ == null) {\n"
		      "    return $name$_.get(index);"
		      "  } else {\n"
		      "    return $name$Builder_.getMessageOrBuilder(index);\n"
		      "  }\n"
		      "}\n");
	// List<FieldOrBuilder> getRepeatedFieldOrBuilderList()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.util.List<? extends $type$OrBuilder> \n"
		      "     ${$get$capitalized_name$OrBuilderList$}$() {\n"
		      "  if ($name$Builder_ != null) {\n"
		      "    return $name$Builder_.getMessageOrBuilderList();\n"
		      "  } else {\n"
		      "    return java.util.Collections.unmodifiableList($name$_);\n"
		      "  }\n"
		      "}\n");
	// Field.Builder addRepeatedField()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$.Builder "
		      "${$add$capitalized_name$Builder$}$() {\n"
		      "  return "
		      "internalGet$capitalized_name$FieldBuilder().addBuilder(\n"
		      "      $type$.getDefaultInstance());\n"
		      "}\n");
	// Field.Builder addRepeatedFieldBuilder(int index)
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$.Builder ${$add$capitalized_name$Builder$}$(\n"
		      "    int index) {\n"
		      "  return "
		      "internalGet$capitalized_name$FieldBuilder().addBuilder(\n"
		      "      index, $type$.getDefaultInstance());\n"
		      "}\n");
	// List<Field.Builder> getRepeatedFieldBuilderList()
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.util.List<$type$.Builder> \n"
		      "     ${$get$capitalized_name$BuilderList$}$() {\n"
		      "  return "
		      "internalGet$capitalized_name$FieldBuilder()."
		      "getBuilderList();\n"
		      "}\n"
		      "private com.google.protobuf.RepeatedFieldBuilder<\n"
		      "    $type$, $type$.Builder, $type$OrBuilder> \n"
		      "    internalGet$capitalized_name$FieldBuilder() {\n"
		      "  if ($name$Builder_ == null) {\n"
		      "    $name$Builder_ = new "
		      "com.google.protobuf.RepeatedFieldBuilder<\n"
		      "        $type$, $type$.Builder, $type$OrBuilder>(\n"
		      "            $name$_,\n"
		      "            $get_mutable_bit_builder$,\n"
		      "            getParentForChildren(),\n"
		      "            isClean());\n"
		      "    $name$_ = null;\n"
		      "  }\n"
		      "  return $name$Builder_;\n"
		      "}\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
											  LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "internalGet$capitalized_name$FieldBuilder();\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = java.util.Collections.emptyList();\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(
		g, printer, "$name$_ = java.util.Collections.emptyList();\n",

		"$name$_ = null;\n"
		"$name$Builder_.clear();\n");

	lp_print_vars(printer, &g->variables, "$clear_mutable_bit_builder$;\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// The code below does two optimizations (non-nested builder case):
	//   1. If the other list is empty, there's nothing to do. This ensures we
	//      don't allocate a new array if we already have an immutable one.
	//   2. If the other list is non-empty and our current list is empty, we can
	//      reuse the other list which is guaranteed to be immutable.
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(
		g, printer,
		"if (!other.$name$_.isEmpty()) {\n"
		"  if ($name$_.isEmpty()) {\n"
		"    $name$_ = other.$name$_;\n"
		"    $clear_mutable_bit_builder$;\n"
		"  } else {\n"
		"    ensure$capitalized_name$IsMutable();\n"
		"    $name$_.addAll(other.$name$_);\n"
		"  }\n"
		"  $on_changed$\n"
		"}\n",

		"if (!other.$name$_.isEmpty()) {\n"
		"  if ($name$Builder_.isEmpty()) {\n"
		"    $name$Builder_.dispose();\n"
		"    $name$Builder_ = null;\n"
		"    $name$_ = other.$name$_;\n"
		"    $clear_mutable_bit_builder$;\n"
		"    $name$Builder_ = \n"
		"      com.google.protobuf.GeneratedMessage.alwaysUseFieldBuilders "
		"?\n"
		"         internalGet$capitalized_name$FieldBuilder() : null;\n"
		"  } else {\n"
		"    $name$Builder_.addAllMessages(other.$name$_);\n"
		"  }\n"
		"}\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// The code below (non-nested builder case) ensures that the result has an
	// immutable list. If our list is immutable, we can just reuse it. If not,
	// we make it immutable.
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(
		g, printer,
		"if ($get_mutable_bit_builder$) {\n"
		"  $name$_ = java.util.Collections.unmodifiableList($name$_);\n"
		"  $clear_mutable_bit_builder$;\n"
		"}\n"
		"result.$name$_ = $name$_;\n",

		"result.$name$_ = $name$Builder_.build();\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_get_type(g->descriptor) == PT_GROUP) {
		lp_print_vars(printer, &g->variables,
			      "$type$ m =\n"
			      "    input.readGroup($number$,\n"
			      "        $type$.$get_parser$,\n"
			      "        extensionRegistry);\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "$type$ m =\n"
			      "    input.readMessage(\n"
			      "        $type$.$get_parser$,\n"
			      "        extensionRegistry);\n");
	}
	RepeatedImmutableMessageFieldGenerator_PrintNestedBuilderCondition(g, printer,
									   "ensure$capitalized_name$IsMutable();\n"
									   "$name$_.add(m);\n",
									   "$name$Builder_.addMessage(m);\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "for (int i = 0; i < $name$_.size(); i++) {\n"
		      "  output.write$group_or_message$($number$, $name$_.get(i));\n"
		      "}\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "\n"
		      "    {\n"
		      "      final int count = $name$_.size();\n"
		      "      for (int i = 0; i < count; i++) {\n"
		      "        size += com.google.protobuf.CodedOutputStream\n"
		      "          .compute$group_or_message$SizeNoTag($name$_.get(i));\n"
		      "      }\n"
		      "      size += $tag_size$ * count;\n"
		      "    }\n"
		      "    ");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!get$capitalized_name$List()\n"
		      "    .equals(other.get$capitalized_name$List())) return false;\n");
}

static void RepeatedImmutableMessageFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (get$capitalized_name$Count() > 0) {\n"
		      "  hash = (37 * hash) + $constant_name$;\n"
		      "  hash = (53 * hash) + get$capitalized_name$List().hashCode();\n"
		      "}\n");
}

static const char *RepeatedImmutableMessageFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->message_type);
}

static const JvFieldGenOps ImmutableMessageFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableMessageFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableMessageFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableMessageFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableMessageFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableMessageFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableMessageFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableMessageFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableMessageFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableMessageFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableMessageFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableMessageFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableMessageFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableMessageFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableMessageFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableMessageFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableMessageFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableMessageFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps ImmutableMessageOneofFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableMessageFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableMessageFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableMessageFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableMessageFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableMessageFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableMessageOneofFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableMessageOneofFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableMessageFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableMessageOneofFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableMessageOneofFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableMessageOneofFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableMessageOneofFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableMessageOneofFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableMessageFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableMessageFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableMessageFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableMessageOneofFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps RepeatedImmutableMessageFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableMessageFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableMessageFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = RepeatedImmutableMessageFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = RepeatedImmutableMessageFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = RepeatedImmutableMessageFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutableMessageFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutableMessageFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutableMessageFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = RepeatedImmutableMessageFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = RepeatedImmutableMessageFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = RepeatedImmutableMessageFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = RepeatedImmutableMessageFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = RepeatedImmutableMessageFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode =
		RepeatedImmutableMessageFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = RepeatedImmutableMessageFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = RepeatedImmutableMessageFieldGenerator_GenerateHashCode,
	.GetBoxedType = RepeatedImmutableMessageFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = RepeatedImmutableMessageFieldGenerator_GenerateSerializationCode,
};

#define RepeatedImmutableMessageFieldGenerator_init ImmutableMessageFieldGenerator_init
