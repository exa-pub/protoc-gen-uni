// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/enum_field.{h,cc} (protobuf v36.2). */

static int ImmutableEnumFieldGenerator_GetMessageBitIndex(const JvFieldGen *g);
static int ImmutableEnumFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g);
static int ImmutableEnumFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutableEnumFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutableEnumFieldGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutableEnumOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutableEnumFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static int RepeatedImmutableEnumFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g);
static void RepeatedImmutableEnumFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										       LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderParsingCodeFromPacked(const JvFieldGen *g,
										     LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *RepeatedImmutableEnumFieldGenerator_GetBoxedType(const JvFieldGen *g);

static void SetEnumVariables_enum(const LpField *descriptor, int message_bit_index, int builder_bit_index,
				  const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "type", jv_immutable_class_name(descriptor->enum_type));

	const char *name = lp_vars_get(variables, "name");
	lp_vars_set(variables, "name_make_immutable", xcat(name, "_.makeImmutable()"));
	lp_vars_set(variables, "field_list_type", "com.google.protobuf.Internal.IntList");
	lp_vars_set(variables, "empty_list", "emptyIntList()");

	lp_vars_set(variables, "default", jv_immutable_default_value_(descriptor, &context->options));
	lp_vars_set(variables, "default_number", xcat(jv_i(descriptor->default_value_enum->number)));
	lp_vars_set(variables, "tag", xcat(jv_i(((int32_t)(jv_make_tag(descriptor))))));
	lp_vars_set(variables, "tag_size", xcat(jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor)))));
	lp_vars_set(variables, "null_check", "java.util.Objects.requireNonNull(value);");
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");

	lp_vars_set(variables, "on_changed", "onChanged();");

	if (jv_has_hasbit(descriptor)) {
		// For singular messages and builders, one bit is used for the hasField bit.
		// Note that these have a trailing ";".
		lp_vars_set(variables, "set_has_field_bit_to_local", jv_generate_set_bit_to_local(message_bit_index));
		lp_vars_set(variables, "is_field_present_message", jv_generate_get_bit(message_bit_index));
	} else {
		lp_vars_set(variables, "set_has_field_bit_to_local", "");
		lp_vars_insert(variables, "is_field_present_message",
			       xcat(lp_vars_get(variables, "name"), "_ != ", lp_vars_get(variables, "default"),
				    ".getNumber()"));
	}

	// Always track the presence of a field explicitly in the builder, regardless
	// of syntax.
	lp_vars_set(variables, "get_has_field_bit_builder", jv_generate_get_bit(builder_bit_index));
	lp_vars_set(variables, "get_has_field_bit_from_local", jv_generate_get_bit_from_local(builder_bit_index));

	// Note that these have a trailing ";".
	lp_vars_set(variables, "set_has_field_bit_builder", xcat(jv_generate_set_bit(builder_bit_index), ";"));
	lp_vars_set(variables, "clear_has_field_bit_builder", xcat(jv_generate_clear_bit(builder_bit_index), ";"));

	lp_vars_set(variables, "unknown",
		    jv_support_unknown_enum_value(descriptor) ? xcat(lp_vars_get(variables, "type"), ".UNRECOGNIZED")
							      : lp_vars_get(variables, "default"));
}

// ===================================================================

static void ImmutableEnumFieldGenerator_init(JvFieldGen *g)
{
	SetEnumVariables_enum(g->descriptor, g->message_bit_index, g->builder_bit_index,
			      jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static int ImmutableEnumFieldGenerator_GetMessageBitIndex(const JvFieldGen *g) { return g->message_bit_index; }

static int ImmutableEnumFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g) { return g->builder_bit_index; }

static int ImmutableEnumFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static int ImmutableEnumFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void ImmutableEnumFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$boolean has$capitalized_name$();\n");
	}
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$int get$capitalized_name$Value();\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ get$capitalized_name$();\n");
}

static void ImmutableEnumFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private int $name$_ = $default_number$;\n");
	jv_print_extra_field_info(&g->variables, printer);
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override $deprecation$public boolean "
			      "${$has$capitalized_name$$}$() {\n"
			      "  return $is_field_present_message$;\n"
			      "}\n");
	}
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override $deprecation$public int "
			      "${$get$capitalized_name$Value$}$() {\n"
			      "  return $name$_;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override $deprecation$public $type$ "
		      "${$get$capitalized_name$$}$() {\n"
		      "  $type$ result = $type$.forNumber($name$_);\n"
		      "  return result == null ? $unknown$ : result;\n"
		      "}\n");
}

static void ImmutableEnumFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private int $name$_ = $default_number$;\n");
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override $deprecation$public boolean "
			      "${$has$capitalized_name$$}$() {\n"
			      "  return $get_has_field_bit_builder$;\n"
			      "}\n");
	}
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override $deprecation$public int "
			      "${$get$capitalized_name$Value$}$() {\n"
			      "  return $name$_;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							       /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder "
			      "${$set$capitalized_name$Value$}$(int value) {\n"
			      "  $name$_ = value;\n"
			      "  $set_has_field_bit_builder$\n"
			      "  onChanged();\n"
			      "  return this;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  $type$ result = $type$.forNumber($name$_);\n"
		      "  return result == null ? $unknown$ : result;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
		      "  $null_check$\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $name$_ = value.getNumber();\n"
		      "  onChanged();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $clear_has_field_bit_builder$\n"
		      "  $name$_ = $default_number$;\n"
		      "  onChanged();\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	// noop for enums
}

static void ImmutableEnumFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $default_number$;\n");
}

static void ImmutableEnumFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $default_number$;\n");
}

static void ImmutableEnumFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "if (other.has$capitalized_name$()) {\n");
	} else {
		lp_print_vars(printer, &g->variables, "if (other.$name$_ != $default_number$) {\n");
	}
	lp_indent(printer);
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "set$capitalized_name$Value(other.get$capitalized_name$Value());\n");
	} else {
		lp_print_vars(printer, &g->variables, "set$capitalized_name$(other.get$capitalized_name$());\n");
	}
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void ImmutableEnumFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  result.$name$_ = $name$_;\n");
	if (g->ops->GetNumBitsForMessage(g) > 0) {
		lp_print_vars(printer, &g->variables, "  $set_has_field_bit_to_local$;\n");
	}
	lp_print(printer, "}\n", NULL);
}

static void ImmutableEnumFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "$name$_ = input.readEnum();\n"
			      "$set_has_field_bit_builder$\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "int tmpRaw = input.readEnum();\n"
			      "$type$ tmpValue =\n"
			      "    $type$.forNumber(tmpRaw);\n"
			      "if (tmpValue == null) {\n"
			      "  mergeUnknownVarintField($number$, tmpRaw);\n"
			      "} else {\n"
			      "  $name$_ = tmpRaw;\n"
			      "  $set_has_field_bit_builder$\n"
			      "}\n");
	}
}

static void ImmutableEnumFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  output.writeEnum($number$, $name$_);\n"
		      "}\n");
}

static void ImmutableEnumFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "    .computeEnumSize($number$, $name$_);\n"
		      "}\n");
}

static void ImmutableEnumFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "if ($name$_ != other.$name$_) return false;\n");
}

static void ImmutableEnumFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "hash = (37 * hash) + $constant_name$;\n"
		      "hash = (53 * hash) + $name$_;\n");
}

static const char *ImmutableEnumFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->enum_type);
}

// ===================================================================

static void ImmutableEnumOneofFieldGenerator_init(JvFieldGen *g)
{
	ImmutableEnumFieldGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutableEnumOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_print_extra_field_info(&g->variables, printer);
	JV_DCHECK(proto_field_has_presence(g->descriptor));
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $has_oneof_case_message$;\n"
		      "}\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public int ${$get$capitalized_name$Value$}$() {\n"
			      "  if ($has_oneof_case_message$) {\n"
			      "    return (java.lang.Integer) $oneof_name$_;\n"
			      "  }\n"
			      "  return $default_number$;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $type$ result = $type$.forNumber(\n"
		      "        (java.lang.Integer) $oneof_name$_);\n"
		      "    return result == null ? $unknown$ : result;\n"
		      "  }\n"
		      "  return $default$;\n"
		      "}\n");
}

static void ImmutableEnumOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	JV_DCHECK(proto_field_has_presence(g->descriptor));
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return $has_oneof_case_message$;\n"
		      "}\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public int ${$get$capitalized_name$Value$}$() {\n"
			      "  if ($has_oneof_case_message$) {\n"
			      "    return ((java.lang.Integer) $oneof_name$_).intValue();\n"
			      "  }\n"
			      "  return $default_number$;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							       /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder "
			      "${$set$capitalized_name$Value$}$(int value) {\n"
			      "  $set_oneof_case_message$;\n"
			      "  $oneof_name$_ = value;\n"
			      "  onChanged();\n"
			      "  return this;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $type$ result = $type$.forNumber(\n"
		      "        (java.lang.Integer) $oneof_name$_);\n"
		      "    return result == null ? $unknown$ : result;\n"
		      "  }\n"
		      "  return $default$;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
		      "  $null_check$\n"
		      "  $set_oneof_case_message$;\n"
		      "  $oneof_name$_ = value.getNumber();\n"
		      "  onChanged();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $clear_oneof_case_message$;\n"
		      "    $oneof_name$_ = null;\n"
		      "    onChanged();\n"
		      "  }\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutableEnumOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No-op: Enum fields in oneofs are correctly cleared by clearing the oneof
}

static void ImmutableEnumOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No-Op: Handled by single statement for the oneof
}

static void ImmutableEnumOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "set$capitalized_name$Value(other.get$capitalized_name$Value());\n");
	} else {
		lp_print_vars(printer, &g->variables, "set$capitalized_name$(other.get$capitalized_name$());\n");
	}
}

static void ImmutableEnumOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "int rawValue = input.readEnum();\n"
			      "$set_oneof_case_message$;\n"
			      "$oneof_name$_ = rawValue;\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "int rawValue = input.readEnum();\n"
			      "$type$ value =\n"
			      "    $type$.forNumber(rawValue);\n"
			      "if (value == null) {\n"
			      "  mergeUnknownVarintField($number$, rawValue);\n"
			      "} else {\n"
			      "  $set_oneof_case_message$;\n"
			      "  $oneof_name$_ = rawValue;\n"
			      "}\n");
	}
}

static void ImmutableEnumOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  output.writeEnum($number$, ((java.lang.Integer) $oneof_name$_));\n"
		      "}\n");
}

static void ImmutableEnumOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "    .computeEnumSize($number$, ((java.lang.Integer) $oneof_name$_));\n"
		      "}\n");
}

static void ImmutableEnumOneofFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "if (get$capitalized_name$Value()\n"
			      "    != other.get$capitalized_name$Value()) return false;\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "if (!get$capitalized_name$()\n"
			      "    .equals(other.get$capitalized_name$())) return false;\n");
	}
}

static void ImmutableEnumOneofFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "hash = (37 * hash) + $constant_name$;\n"
			      "hash = (53 * hash) + get$capitalized_name$Value();\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "hash = (37 * hash) + $constant_name$;\n"
			      "hash = (53 * hash) + get$capitalized_name$().getNumber();\n");
	}
}

// ===================================================================

static int RepeatedImmutableEnumFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static int RepeatedImmutableEnumFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void RepeatedImmutableEnumFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$java.util.List<$type$> get$capitalized_name$List();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$int get$capitalized_name$Count();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ get$capitalized_name$(int index);\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$java.util.List<java.lang.Integer>\n"
			      "get$capitalized_name$ValueList();\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$int get$capitalized_name$Value(int index);\n");
	}
}

static void RepeatedImmutableEnumFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "@SuppressWarnings(\"serial\")\n"
		      "private com.google.protobuf.Internal.IntList $name$_ =\n"
		      "    $empty_list$;\n"
		      "private static final "
		      "    com.google.protobuf.Internal.IntListAdapter.IntConverter<\n"
		      "    $type$> $name$_converter_ =\n"
		      "        new com.google.protobuf.Internal.IntListAdapter.IntConverter<\n"
		      "            $type$>() {\n"
		      "          public $type$ convert(int from) {\n"
		      "            $type$ result = $type$.forNumber(from);\n"
		      "            return result == null ? $unknown$ : result;\n"
		      "          }\n"
		      "        };\n");
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return new com.google.protobuf.Internal.IntListAdapter<\n"
		      "      $type$>($name$_, $name$_converter_);\n"
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_converter_.convert($name$_.getInt(index));\n"
		      "}\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public java.util.List<java.lang.Integer>\n"
			      "${$get$capitalized_name$ValueList$}$() {\n"
			      "  return $name$_;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public int "
			      "${$get$capitalized_name$Value$}$(int index) {\n"
			      "  return $name$_.getInt(index);\n"
			      "}\n");
	}

	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "private int $name$MemoizedSerializedSize;\n");
	}
}

static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "private $field_list_type$ $name$_ = $empty_list$;\n"
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      "  if (!$name$_.isModifiable()) {\n"
		      "    $name$_ = makeMutableCopy($name$_);\n"
		      "  }\n"
		      "  $set_has_field_bit_builder$\n"
		      "}\n");

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      // Note:  We return an unmodifiable list because otherwise the caller
		      //   could hold on to the returned list and modify it after the message
		      //   has been built, thus mutating the message which is supposed to be
		      //   immutable.
		      "$deprecation$public java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return new com.google.protobuf.Internal.IntListAdapter<\n"
		      "      $type$>($name$_, $name$_converter_);\n"
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $name$_converter_.convert($name$_.getInt(index));\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, $type$ value) {\n"
		      "  $null_check$\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.setInt(index, value.getNumber());\n"
		      "  onChanged();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$add$capitalized_name$$}$($type$ value) {\n"
		      "  $null_check$\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.addInt(value.getNumber());\n"
		      "  onChanged();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		      "    java.lang.Iterable<? extends $type$> values) {\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  for ($type$ value : values) {\n"
		      "    $name$_.addInt(value.getNumber());\n"
		      "  }\n"
		      "  onChanged();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $name$_ = $empty_list$;\n"
		      "  $clear_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public java.util.List<java.lang.Integer>\n"
			      "${$get$capitalized_name$ValueList$}$() {\n"
			      "  $name$_.makeImmutable();\n"
			      "  return $name$_;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public int "
			      "${$get$capitalized_name$Value$}$(int index) {\n"
			      "  return $name$_.getInt(index);\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER,
							       &g->context->options, /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$set$capitalized_name$Value$}$(\n"
			      "    int index, int value) {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  $name$_.setInt(index, value);\n"
			      "  onChanged();\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER,
							       &g->context->options, /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder "
			      "${$add$capitalized_name$Value$}$(int value) {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  $name$_.addInt(value);\n"
			      "  onChanged();\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER,
							       &g->context->options, /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$addAll$capitalized_name$Value$}$(\n"
			      "    java.lang.Iterable<java.lang.Integer> values) {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  for (int value : values) {\n"
			      "    $name$_.addInt(value);\n"
			      "  }\n"
			      "  onChanged();\n"
			      "  return this;\n"
			      "}\n");
	}
}

static void RepeatedImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										       LpPrinter *printer)
{
	// noop for enums
}

static void RepeatedImmutableEnumFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $empty_list$;\n");
}

static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $empty_list$;\n");
}

static void RepeatedImmutableEnumFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
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
		      "    $name_make_immutable$;\n"
		      "    $set_has_field_bit_builder$\n"
		      "  } else {\n"
		      "    ensure$capitalized_name$IsMutable();\n"
		      "    $name$_.addAll(other.$name$_);\n"
		      "  }\n"
		      "  $on_changed$\n"
		      "}\n");
}

static void RepeatedImmutableEnumFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// The code below ensures that the result has an immutable list. If our
	// list is immutable, we can just reuse it. If not, we make it immutable.
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  $name_make_immutable$;\n"
		      "  result.$name$_ = $name$_;\n"
		      "}\n");
}

static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// Read and store the enum
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "int tmpRaw = input.readEnum();\n"
			      "ensure$capitalized_name$IsMutable();\n"
			      "$name$_.addInt(tmpRaw);\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "int tmpRaw = input.readEnum();\n"
			      "$type$ tmpValue =\n"
			      "    $type$.forNumber(tmpRaw);\n"
			      "if (tmpValue == null) {\n"
			      "  mergeUnknownVarintField($number$, tmpRaw);\n"
			      "} else {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  $name$_.addInt(tmpRaw);\n"
			      "}\n");
	}
}

static void RepeatedImmutableEnumFieldGenerator_GenerateBuilderParsingCodeFromPacked(const JvFieldGen *g,
										     LpPrinter *printer)
{
	if (jv_support_unknown_enum_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "int length = input.readRawVarint32();\n"
			      "int limit = input.pushLimit(length);\n"
			      "ensure$capitalized_name$IsMutable();\n"
			      "while (input.getBytesUntilLimit() > 0) {\n"
			      "  $name$_.addInt(input.readEnum());\n"
			      "}\n"
			      "input.popLimit(limit);\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "int length = input.readRawVarint32();\n"
			      "int limit = input.pushLimit(length);\n"
			      "ensure$capitalized_name$IsMutable();\n"
			      "while (input.getBytesUntilLimit() > 0) {\n"
			      "  int tmpRaw = input.readEnum();\n"
			      "  $type$ tmpValue =\n"
			      "      $type$.forNumber(tmpRaw);\n"
			      "  if (tmpValue == null) {\n"
			      "    mergeUnknownVarintField($number$, tmpRaw);\n"
			      "  } else {\n"
			      "    $name$_.addInt(tmpRaw);\n"
			      "  }\n"
			      "}\n"
			      "input.popLimit(limit);\n");
	}
}
static void RepeatedImmutableEnumFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "if (get$capitalized_name$List().size() > 0) {\n"
			      "  output.writeUInt32NoTag($tag$);\n"
			      "  output.writeUInt32NoTag($name$MemoizedSerializedSize);\n"
			      "}\n"
			      "for (int i = 0; i < $name$_.size(); i++) {\n"
			      "  output.writeEnumNoTag($name$_.getInt(i));\n"
			      "}\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "for (int i = 0; i < $name$_.size(); i++) {\n"
			      "  output.writeEnum($number$, $name$_.getInt(i));\n"
			      "}\n");
	}
}

static void RepeatedImmutableEnumFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "{\n"
		      "  int dataSize = 0;\n");
	lp_indent(printer);

	lp_print_vars(printer, &g->variables,
		      "for (int i = 0; i < $name$_.size(); i++) {\n"
		      "  dataSize += com.google.protobuf.CodedOutputStream\n"
		      "    .computeEnumSizeNoTag($name$_.getInt(i));\n"
		      "}\n");
	lp_print(printer, "size += dataSize;\n", NULL);
	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "if (!get$capitalized_name$List().isEmpty()) {"
			      "  size += $tag_size$;\n"
			      "  size += com.google.protobuf.CodedOutputStream\n"
			      "    .computeUInt32SizeNoTag(dataSize);\n"
			      "}");
	} else {
		lp_print_vars(printer, &g->variables, "size += $tag_size$ * $name$_.size();\n");
	}

	// cache the data size for packed fields.
	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "$name$MemoizedSerializedSize = dataSize;\n");
	}

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void RepeatedImmutableEnumFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "if (!$name$_.equals(other.$name$_)) return false;\n");
}

static void RepeatedImmutableEnumFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (get$capitalized_name$Count() > 0) {\n"
		      "  hash = (37 * hash) + $constant_name$;\n"
		      "  hash = (53 * hash) + $name$_.hashCode();\n"
		      "}\n");
}

static const char *RepeatedImmutableEnumFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->enum_type);
}

static const JvFieldGenOps ImmutableEnumFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableEnumFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableEnumFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableEnumFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableEnumFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableEnumFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableEnumFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableEnumFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableEnumFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableEnumFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableEnumFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableEnumFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableEnumFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableEnumFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableEnumFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableEnumFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableEnumFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableEnumFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps ImmutableEnumOneofFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableEnumFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableEnumFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableEnumFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableEnumFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableEnumFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableEnumOneofFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableEnumOneofFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableEnumFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableEnumOneofFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableEnumOneofFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableEnumOneofFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableEnumOneofFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableEnumOneofFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableEnumOneofFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableEnumOneofFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableEnumFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableEnumOneofFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps RepeatedImmutableEnumFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableEnumFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableEnumFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = RepeatedImmutableEnumFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = RepeatedImmutableEnumFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = RepeatedImmutableEnumFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutableEnumFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutableEnumFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutableEnumFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = RepeatedImmutableEnumFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = RepeatedImmutableEnumFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = RepeatedImmutableEnumFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = RepeatedImmutableEnumFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = RepeatedImmutableEnumFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode =
		RepeatedImmutableEnumFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateBuilderParsingCodeFromPacked =
		RepeatedImmutableEnumFieldGenerator_GenerateBuilderParsingCodeFromPacked,
	.GenerateEqualsCode = RepeatedImmutableEnumFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = RepeatedImmutableEnumFieldGenerator_GenerateHashCode,
	.GetBoxedType = RepeatedImmutableEnumFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = RepeatedImmutableEnumFieldGenerator_GenerateSerializationCode,
};

#define RepeatedImmutableEnumFieldGenerator_init ImmutableEnumFieldGenerator_init
