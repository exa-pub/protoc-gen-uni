// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/primitive_field.{h,cc} (protobuf v36.2). */

static bool EnableExperimentalRuntimeForLite_lite_primitive();
static int ImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void ImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								   JvUtf16 *output);
static void ImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutablePrimitiveFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutablePrimitiveOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									JvUtf16 *output);
static void ImmutablePrimitiveOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g,
										  LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g,
										LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									   JvUtf16 *output);
static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g,
										    LpPrinter *printer);
static const char *RepeatedImmutablePrimitiveFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);

static bool EnableExperimentalRuntimeForLite_lite_primitive()
{
#ifdef PROTOBUF_EXPERIMENT
	return PROTOBUF_EXPERIMENT;
#else // PROTOBUF_EXPERIMENT
	return false;
#endif // !PROTOBUF_EXPERIMENT
}

static void SetPrimitiveVariables_lite_primitive(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
						 const JvFieldGeneratorInfo *info, LpVars *variables,
						 JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);
	JvJavaType javaType = jv_get_java_type(descriptor);
	lp_vars_set(variables, "type", (jv_primitive_type_name(javaType)));
	lp_vars_set(variables, "boxed_type", (jv_boxed_primitive_type_name(javaType)));
	lp_vars_insert(variables, "field_type", lp_vars_get(variables, "type"));
	lp_vars_set(variables, "default", jv_immutable_default_value_(descriptor, &context->options));
	lp_vars_set(variables, "capitalized_type",
		    (jv_get_capitalized_type_(descriptor, /* immutable = */ true, &context->options)));
	lp_vars_set(variables, "tag", xcat(jv_i(((int32_t)(jv_make_tag(descriptor))))));
	lp_vars_set(variables, "tag_size", xcat(jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor)))));
	lp_vars_set(variables, "required", proto_field_is_required(descriptor) ? "true" : "false");

	const char *capitalized_type =
		jv_underscores_to_camel_case(jv_primitive_type_name(javaType), true /* cap_next_letter */);
	const char *name = lp_vars_get(variables, "name");
	switch (javaType) {
	case JAVATYPE_INT:
	case JAVATYPE_LONG:
	case JAVATYPE_FLOAT:
	case JAVATYPE_DOUBLE:
	case JAVATYPE_BOOLEAN:
		lp_vars_set(variables, "field_list_type",
			    xcat("com.google.protobuf.Internal.", capitalized_type, "List"));
		lp_vars_set(variables, "empty_list", xcat("empty", capitalized_type, "List()"));
		lp_vars_set(variables, "make_name_unmodifiable", xcat(name, "_.makeImmutable()"));
		lp_vars_set(variables, "repeated_get", xcat(name, "_.get", capitalized_type));
		lp_vars_set(variables, "repeated_add", xcat(name, "_.add", capitalized_type));
		lp_vars_set(variables, "repeated_set", xcat(name, "_.set", capitalized_type));
		lp_vars_set(variables, "visit_type", capitalized_type);
		lp_vars_set(variables, "visit_type_list", xcat("visit", capitalized_type, "List"));
		break;
	default:
		lp_vars_insert(
			variables, "field_list_type",
			xcat("com.google.protobuf.Internal.ProtobufList<", lp_vars_get(variables, "boxed_type"), ">"));
		lp_vars_set(variables, "empty_list", "emptyProtobufList()");
		lp_vars_set(variables, "make_name_unmodifiable", xcat(name, "_.makeImmutable()"));
		lp_vars_set(variables, "repeated_get", xcat(name, "_.get"));
		lp_vars_set(variables, "repeated_add", xcat(name, "_.add"));
		lp_vars_set(variables, "repeated_set", xcat(name, "_.set"));
		lp_vars_set(variables, "visit_type", "ByteString");
		lp_vars_set(variables, "visit_type_list", "visitList");
	}

	if (javaType == JAVATYPE_BYTES) {
		lp_vars_set(variables, "bytes_default", xcat(jv_upper(name), "_DEFAULT_VALUE"));
	}

	if (jv_is_reference_type(javaType)) {
		lp_vars_set(variables, "null_check", "  java.util.Objects.requireNonNull(value);\n");
	} else {
		lp_vars_set(variables, "null_check", "");
	}
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	int fixed_size = jv_fixed_size(jv_get_type(descriptor));
	if (fixed_size != -1) {
		lp_vars_set(variables, "fixed_size", jv_i(fixed_size));
	}

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

		switch (descriptor->type) {
		case PT_BYTES:
			lp_vars_set(variables, "is_field_present_message", xcat("!", name, "_.isEmpty()"));
			break;
		case PT_FLOAT:
			lp_vars_set(variables, "is_field_present_message",
				    xcat("java.lang.Float.floatToRawIntBits(", name, "_) != 0"));
			break;
		case PT_DOUBLE:
			lp_vars_set(variables, "is_field_present_message",
				    xcat("java.lang.Double.doubleToRawLongBits(", name, "_) != 0"));
			break;
		default:
			lp_vars_insert(variables, "is_field_present_message",
				       xcat(name, "_ != ", lp_vars_get(variables, "default")));
			break;
		}
	}

	// Annotations often use { and } variables to denote ranges.
	lp_vars_set(variables, "{", "");
	lp_vars_set(variables, "}", "");
}

// ===================================================================

static void ImmutablePrimitiveFieldLiteGenerator_init(JvFieldGen *g)
{
	SetPrimitiveVariables_lite_primitive(g->descriptor, g->message_bit_index, g->builder_bit_index,
					     jv_context_field_info(g->context, g->descriptor), &g->variables,
					     g->context);
}

static int ImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static void ImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$boolean has$capitalized_name$();\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ ${$get$capitalized_name$$}$();\n");
}

static void ImmutablePrimitiveFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_is_byte_string_with_custom_default_value(g->descriptor)) {
		// allocate this once statically since we know ByteStrings are immutable
		// values that can be reused.
		lp_print_vars(printer, &g->variables,
			      "private static final $field_type$ $bytes_default$ = $default$;\n");
	}
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
	lp_print_vars(printer, &g->variables, "private $field_type$ $name$_;\n");
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return $name$_;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$($type$ value) {\n"
		      "$null_check$"
		      "  $set_has_field_bit_message$\n"
		      "  $name$_ = value;\n"
		      "}\n");

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void clear$capitalized_name$() {\n"
		      "  $clear_has_field_bit_message$\n");
	JvJavaType type = jv_get_java_type(g->descriptor);
	if (type == JAVATYPE_STRING || type == JAVATYPE_BYTES) {
		// The default value is not a simple literal so we want to avoid executing
		// it multiple times.  Instead, get the default out of the default instance.
		lp_print_vars(printer, &g->variables, "  $name$_ = getDefaultInstance().get$capitalized_name$();\n");
	} else {
		lp_print_vars(printer, &g->variables, "  $name$_ = $default$;\n");
	}
	lp_print_vars(printer, &g->variables, "}\n");
}

static void ImmutablePrimitiveFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
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

static void ImmutablePrimitiveFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
								   JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	if (jv_has_hasbit(g->descriptor)) {
		jv_write_int_to_utf16_char_sequence(g->message_bit_index, output);
	}
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
}

static void ImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_is_byte_string_with_custom_default_value(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "$name$_ = $bytes_default$;\n");
	} else if (!jv_is_default_value_java_default(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "$name$_ = $default$;\n");
	}
}

static const char *ImmutablePrimitiveFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return (jv_boxed_primitive_type_name(jv_get_java_type(g->descriptor)));
}

// ===================================================================

static void ImmutablePrimitiveOneofFieldLiteGenerator_init(JvFieldGen *g)
{
	ImmutablePrimitiveFieldLiteGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

static void ImmutablePrimitiveOneofFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    return ($boxed_type$) $oneof_name$_;\n"
		      "  }\n"
		      "  return $default$;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$($type$ value) {\n"
		      "$null_check$"
		      "  $set_oneof_case_message$;\n"
		      "  $oneof_name$_ = value;\n"
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

static void ImmutablePrimitiveOneofFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	jv_write_int_to_utf16_char_sequence(g->descriptor->containing_oneof->index, output);
}

static void ImmutablePrimitiveOneofFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
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

static int RepeatedImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g,
										  LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<$boxed_type$> "
		      "${$get$capitalized_name$List$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$int ${$get$capitalized_name$Count$}$();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ ${$get$capitalized_name$$}$(int index);\n");
}

static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private $field_list_type$ $name$_;\n");
	jv_print_extra_field_info(&g->variables, printer);
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$boxed_type$>\n"
		      "    ${$get$capitalized_name$List$}$() {\n"
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $repeated_get$(index);\n"
		      "}\n");
	if (!EnableExperimentalRuntimeForLite_lite_primitive() && proto_field_is_packed(g->descriptor) &&
	    jv_context_has_generated_methods(g->context, g->descriptor->containing_type)) {
		lp_print_vars(printer, &g->variables, "private int $name$MemoizedSerializedSize = -1;\n");
	}

	lp_print_vars(printer, &g->variables,
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      // Use a temporary to avoid a redundant iget-object.
		      "  $field_list_type$ tmp = $name$_;\n"
		      "  if (!tmp.isModifiable()) {\n"
		      "    $name$_ =\n"
		      "        com.google.protobuf.GeneratedMessageLite.mutableCopy(tmp);\n"
		      "   }\n"
		      "}\n");

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void set$capitalized_name$(\n"
		      "    int index, $type$ value) {\n"
		      "$null_check$"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $repeated_set$(index, value);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void add$capitalized_name$($type$ value) {\n"
		      "$null_check$"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $repeated_add$(value);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ false, /* kdoc */ false, /* is_private */ true);
	lp_print_vars(printer, &g->variables,
		      "private void addAll$capitalized_name$(\n"
		      "    java.lang.Iterable<? extends $boxed_type$> values) {\n"
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
}

static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public java.util.List<$boxed_type$>\n"
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return instance.get$capitalized_name$(index);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
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
		      "    java.lang.Iterable<? extends $boxed_type$> values) {\n"
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
}

static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer,
									   JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	lp_print_vars(printer, &g->variables, "\"$name$_\",\n");
}

static void RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g,
										    LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $empty_list$;\n");
}

static const char *RepeatedImmutablePrimitiveFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return (jv_boxed_primitive_type_name(jv_get_java_type(g->descriptor)));
}

static const JvFieldGenOps ImmutablePrimitiveFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutablePrimitiveFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutablePrimitiveFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutablePrimitiveFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutablePrimitiveFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps ImmutablePrimitiveOneofFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutablePrimitiveOneofFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutablePrimitiveOneofFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutablePrimitiveFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutablePrimitiveOneofFieldLiteGenerator_GenerateFieldInfo,
};
static const JvFieldGenOps RepeatedImmutablePrimitiveFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = RepeatedImmutablePrimitiveFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = RepeatedImmutablePrimitiveFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = RepeatedImmutablePrimitiveFieldLiteGenerator_GenerateFieldInfo,
};

#define RepeatedImmutablePrimitiveFieldLiteGenerator_init ImmutablePrimitiveFieldLiteGenerator_init
