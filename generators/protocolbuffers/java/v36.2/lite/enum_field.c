// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/enum_field.{h,cc} (protobuf v36.2). */

static bool EnableExperimentalRuntimeForLite_lite_enum();
static int ImmutableEnumFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer, JvUtf16 *output);
static const char *ImmutableEnumFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutableEnumOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableEnumOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								   JvUtf16 *output);
static void ImmutableEnumOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutableEnumFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void RepeatedImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								      JvUtf16 *output);
static void RepeatedImmutableEnumFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutableEnumFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static const char *RepeatedImmutableEnumFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);

static bool EnableExperimentalRuntimeForLite_lite_enum()
{
#ifdef PROTOBUF_EXPERIMENT
	return PROTOBUF_EXPERIMENT;
#else // PROTOBUF_EXPERIMENT
	return false;
#endif // !PROTOBUF_EXPERIMENT
}

static void SetEnumVariables_lite_enum(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
				       const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "type", jv_immutable_class_name(descriptor->enum_type));
	lp_vars_set(variables, "default", jv_immutable_default_value_(descriptor, &context->options));
	lp_vars_set(variables, "default_number", xcat(jv_i(descriptor->default_value_enum->number)));
	lp_vars_set(variables, "tag", xcat(jv_i(((int32_t)(jv_make_tag(descriptor))))));
	lp_vars_set(variables, "tag_size", xcat(jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor)))));
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(variables, "required", proto_field_is_required(descriptor) ? "true" : "false");

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
			       xcat(lp_vars_get(variables, "name"), "_ != ", lp_vars_get(variables, "default"),
				    ".getNumber()"));
	}

	if (jv_support_unknown_enum_value(descriptor)) {
		lp_vars_insert(variables, "unknown", xcat(lp_vars_get(variables, "type"), ".UNRECOGNIZED"));
	} else {
		lp_vars_insert(variables, "unknown", lp_vars_get(variables, "default"));
	}

	// Calls to Annotate() use variable ranges to know which text to annotate.
	lp_vars_set(variables, "{", "");
	lp_vars_set(variables, "}", "");
}

// ===================================================================

static void ImmutableEnumFieldLiteGenerator_init(JvFieldGen *g)
{
	SetEnumVariables_lite_enum(g->descriptor, g->message_bit_index, g->builder_bit_index,
				   jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static int ImmutableEnumFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static void ImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$boolean ${$has$capitalized_name$$}$();\n");
	}
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$int ${$get$capitalized_name$Value$}$();\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ ${$get$capitalized_name$$}$();\n");
}

static void ImmutableEnumFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
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
	lp_print_vars(printer, &g->variables, "private int $name$_;\n");
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
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public int ${$get$capitalized_name$Value$}$() {\n"
			      "  return $name$_;\n"
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
	// Generate private setters for the builder to proxy into.
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							       /* builder = */ false, /* is_private = */ true);
		lp_print_vars(printer, &g->variables,
			      "private void set$capitalized_name$Value(int value) {\n"
			      "  $set_has_field_bit_message$"
			      "  $name$_ = value;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$($type$ value) {\n"
		      "  $name$_ = value.getNumber();\n"
		      "  $set_has_field_bit_message$\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $clear_has_field_bit_message$\n"
		      "  $name$_ = $default_number$;\n"
		      "}\n");
}

static void ImmutableEnumFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
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
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public int ${$get$capitalized_name$Value$}$() {\n"
			      "  return instance.get$capitalized_name$Value();\n"
			      "}\n");
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
						    /* builder */ true, false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder "
			      "${$set$capitalized_name$Value$}$(int value) {\n"
			      "  copyOnWrite();\n"
			      "  instance.set$capitalized_name$Value(value);\n"
			      "  return this;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return instance.get$capitalized_name$();\n"
		      "}\n");
	jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
						       /* builder */ true, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
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
}

static void ImmutableEnumFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (!jv_is_default_value_java_default(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "$name$_ = $default_number$;\n");
	}
}

static void ImmutableEnumFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer, JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	if (jv_has_hasbit(g->descriptor)) {
		jv_write_int_to_utf16_char_sequence(g->message_bit_index, output);
	}
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
	if (!jv_support_unknown_enum_value((g->descriptor))) {
		jv_print_enum_verifier_logic(printer, g->descriptor, &g->variables,
					     /*var_name=*/"$type$",
					     /*terminating_string=*/",\n",
					     /*enforce_lite=*/jv_context_enforce_lite(g->context));
	}
}

static const char *ImmutableEnumFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->enum_type);
}

// ===================================================================

static void ImmutableEnumOneofFieldLiteGenerator_init(JvFieldGen *g)
{
	ImmutableEnumFieldLiteGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutableEnumOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
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
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
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
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $type$ result = $type$.forNumber((java.lang.Integer) "
		      "$oneof_name$_);\n"
		      "    return result == null ? $unknown$ : result;\n"
		      "  }\n"
		      "  return $default$;\n"
		      "}\n");
	// Generate private setters for the builder to proxy into.
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							       /* builder = */ false, /* is_private = */ true);
		lp_print_vars(printer, &g->variables,
			      "private void set$capitalized_name$Value(int value) {\n"
			      "  $set_oneof_case_message$;\n"
			      "  $oneof_name$_ = value;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$($type$ value) {\n"
		      "  $oneof_name$_ = value.getNumber();\n"
		      "  $set_oneof_case_message$;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    $clear_oneof_case_message$;\n"
		      "    $oneof_name$_ = null;\n"
		      "  }\n"
		      "}\n");
}

static void ImmutableEnumOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								   JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	jv_write_int_to_utf16_char_sequence(g->descriptor->containing_oneof->index, output);
	if (!jv_support_unknown_enum_value(g->descriptor)) {
		jv_print_enum_verifier_logic(printer, g->descriptor, &g->variables,
					     /*var_name=*/"$type$",
					     /*terminating_string=*/",\n",
					     /*enforce_lite=*/jv_context_enforce_lite(g->context));
	}
}

static void ImmutableEnumOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	JV_DCHECK(proto_field_has_presence(g->descriptor));
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
		      "  return instance.has$capitalized_name$();\n"
		      "}\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options,
							       false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public int ${$get$capitalized_name$Value$}$() {\n"
			      "  return instance.get$capitalized_name$Value();\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							       /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder "
			      "${$set$capitalized_name$Value$}$(int value) {\n"
			      "  copyOnWrite();\n"
			      "  instance.set$capitalized_name$Value(value);\n"
			      "  return this;\n"
			      "}\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return instance.get$capitalized_name$();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
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
}

// ===================================================================

static int RepeatedImmutableEnumFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static void RepeatedImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$int ${$get$capitalized_name$Count$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ ${$get$capitalized_name$$}$(int index);\n");
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$java.util.List<java.lang.Integer>\n"
			      "${$get$capitalized_name$ValueList$}$();\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$int ${$get$capitalized_name$Value$}$(int index);\n");
	}
}

static void RepeatedImmutableEnumFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.Internal.IntList $name$_;\n"
		      "private static final "
		      "com.google.protobuf.Internal.IntListAdapter.IntConverter<\n"
		      "    $type$> $name$_converter_ =\n"
		      "        new com.google.protobuf.Internal.IntListAdapter.IntConverter<\n"
		      "            $type$>() {\n"
		      "          @java.lang.Override\n"
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
		      // NB: Do not use the "$name$_converter_" field; the usage of generics
		      // (and requisite upcasts to Object) prevent optimizations.  Even
		      // without any optimizations, the below code is cheaper because it
		      // avoids boxing an int and a checkcast from the generics.
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  $type$ result = $type$.forNumber($name$_.getInt(index));\n"
		      "  return result == null ? $unknown$ : result;\n"
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

	if (!EnableExperimentalRuntimeForLite_lite_enum() && proto_field_is_packed(g->descriptor) &&
	    jv_context_has_generated_methods(g->context, g->descriptor->containing_type)) {
		lp_print_vars(printer, &g->variables, "private int $name$MemoizedSerializedSize;\n");
	}

	// Generate private setters for the builder to proxy into.
	lp_print_vars(printer, &g->variables,
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      // Use a temporary to avoid a redundant iget-object.
		      "  com.google.protobuf.Internal.IntList tmp = $name$_;\n"
		      "  if (!tmp.isModifiable()) {\n"
		      "    $name$_ =\n"
		      "        com.google.protobuf.GeneratedMessageLite.mutableCopy(tmp);\n"
		      "  }\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$(\n"
		      "    int index, $type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.setInt(index, value.getNumber());\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void add$capitalized_name$($type$ value) {\n"
		      "  java.util.Objects.requireNonNull(value);\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $name$_.addInt(value.getNumber());\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void addAll$capitalized_name$(\n"
		      "    java.lang.Iterable<? extends $type$> values) {\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  for ($type$ value : values) {\n"
		      "    $name$_.addInt(value.getNumber());\n"
		      "  }\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* is_private */ true, false);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $name$_ = emptyIntList();\n"
		      "}\n");

	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
							       /* builder = */ false, /* is_private = */ true);
		lp_print_vars(printer, &g->variables,
			      "private void set$capitalized_name$Value(\n"
			      "    int index, int value) {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  $name$_.setInt(index, value);\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER,
							       &g->context->options, /* builder = */ false,
							       /* is_private = */ true);
		lp_print_vars(printer, &g->variables,
			      "private void add$capitalized_name$Value(int value) {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  $name$_.addInt(value);\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER,
							       &g->context->options, /* builder = */ false,
							       /* is_private = */ true);
		lp_print_vars(printer, &g->variables,
			      "private void addAll$capitalized_name$Value(\n"
			      "    java.lang.Iterable<java.lang.Integer> values) {\n"
			      "  ensure$capitalized_name$IsMutable();\n"
			      "  for (int value : values) {\n"
			      "    $name$_.addInt(value);\n"
			      "  }\n"
			      "}\n");
	}
}

static void RepeatedImmutableEnumFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								      JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
	if (!jv_support_unknown_enum_value(g->descriptor)) {
		jv_print_enum_verifier_logic(printer, g->descriptor, &g->variables,
					     /*var_name=*/"$type$",
					     /*terminating_string=*/",\n",
					     /*enforce_lite=*/jv_context_enforce_lite(g->context));
	}
}

static void RepeatedImmutableEnumFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$type$> "
		      "${$get$capitalized_name$List$}$() {\n"
		      "  return instance.get$capitalized_name$List();\n"
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return instance.get$capitalized_name$(index);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, $type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.set$capitalized_name$(index, value);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$add$capitalized_name$$}$($type$ value) {\n"
		      "  copyOnWrite();\n"
		      "  instance.add$capitalized_name$(value);\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		      "    java.lang.Iterable<? extends $type$> values) {\n"
		      "  copyOnWrite();\n"
		      "  instance.addAll$capitalized_name$(values);"
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
	if (jv_support_unknown_enum_value(g->descriptor)) {
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public java.util.List<java.lang.Integer>\n"
			      "${$get$capitalized_name$ValueList$}$() {\n"
			      "  return java.util.Collections.unmodifiableList(\n"
			      "      instance.get$capitalized_name$ValueList());\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER,
							       &g->context->options, false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public int "
			      "${$get$capitalized_name$Value$}$(int index) {\n"
			      "  return instance.get$capitalized_name$Value(index);\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER,
							       &g->context->options, /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$set$capitalized_name$Value$}$(\n"
			      "    int index, int value) {\n"
			      "  copyOnWrite();\n"
			      "  instance.set$capitalized_name$Value(index, value);\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER,
							       &g->context->options, /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder "
			      "${$add$capitalized_name$Value$}$(int value) {\n"
			      "  copyOnWrite();\n"
			      "  instance.add$capitalized_name$Value(value);\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_enum_value_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER,
							       &g->context->options, /* builder */ true, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$addAll$capitalized_name$Value$}$(\n"
			      "    java.lang.Iterable<java.lang.Integer> values) {\n"
			      "  copyOnWrite();\n"
			      "  instance.addAll$capitalized_name$Value(values);\n"
			      "  return this;\n"
			      "}\n");
	}
}

static void RepeatedImmutableEnumFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = emptyIntList();\n");
}

static const char *RepeatedImmutableEnumFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->enum_type);
}

static const JvFieldGenOps ImmutableEnumFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableEnumFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableEnumFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableEnumFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableEnumFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableEnumFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableEnumFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps ImmutableEnumOneofFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableEnumFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableEnumOneofFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableEnumOneofFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableEnumFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableEnumFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableEnumOneofFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps RepeatedImmutableEnumFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = RepeatedImmutableEnumFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = RepeatedImmutableEnumFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutableEnumFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutableEnumFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutableEnumFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = RepeatedImmutableEnumFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = RepeatedImmutableEnumFieldLiteGenerator_GenerateFieldInfo,
};

#define RepeatedImmutableEnumFieldLiteGenerator_init ImmutableEnumFieldLiteGenerator_init
