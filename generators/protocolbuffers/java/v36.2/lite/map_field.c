// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/map_field.{h,cc} (protobuf v36.2). */

static char *WireType_lite_map(const LpField *field);
static void ImmutableMapFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer, JvUtf16 *output);
static void ImmutableMapFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutableMapFieldLiteGenerator_GetBoxedType(const JvFieldGen *g);

static const char *TypeName_lite_map(const LpField *field, bool boxed)
{
	if (jv_get_java_type(field) == JAVATYPE_MESSAGE) {
		return jv_immutable_class_name(field->message_type);
	} else if (jv_get_java_type(field) == JAVATYPE_ENUM) {
		return jv_immutable_class_name(field->enum_type);
	} else {
		return (boxed ? jv_boxed_primitive_type_name(jv_get_java_type(field))
			      : jv_primitive_type_name(jv_get_java_type(field)));
	}
}

static char *WireType_lite_map(const LpField *field)
{
	return xcat("com.google.protobuf.WireFormat.FieldType.", FieldTypeName(field->type));
}

static void SetMessageVariables_lite_map(const LpField *descriptor, int messageBitIndex, int builderBitIndex,
					 const JvFieldGeneratorInfo *info, JvContext *context, LpVars *variables)
{
	jv_set_common_field_variables(descriptor, info, variables);

	lp_vars_set(variables, "type", jv_immutable_class_name(descriptor->message_type));
	const LpField *key = jv_map_key_field(descriptor);
	const LpField *value = jv_map_value_field(descriptor);
	const JvJavaType keyJavaType = jv_get_java_type(key);
	const JvJavaType valueJavaType = jv_get_java_type(value);

	const char *pass_through_nullness =
		JV_IS_OSS ? "/* nullable */\n" : "@com.google.protobuf.Internal.ProtoPassThroughNullness ";

	lp_vars_set(variables, "key_type", TypeName_lite_map(key, false));
	lp_vars_set(variables, "boxed_key_type", TypeName_lite_map(key, true));
	lp_vars_set(variables, "key_wire_type", WireType_lite_map(key));
	lp_vars_set(variables, "key_default_value", jv_default_value_(key, true, &context->options));
	lp_vars_set(variables, "key_null_check",
		    jv_is_reference_type(keyJavaType) ? "java.util.Objects.requireNonNull(key);" : "");
	lp_vars_set(variables, "value_null_check",
		    jv_is_reference_type(valueJavaType) ? "java.util.Objects.requireNonNull(value);" : "");

	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		// We store enums as Integers internally.
		lp_vars_set(variables, "value_type", "int");
		lp_vars_set(variables, "value_type_pass_through_nullness", "int");
		lp_vars_set(variables, "boxed_value_type", "java.lang.Integer");
		lp_vars_set(variables, "value_wire_type", WireType_lite_map(value));
		lp_vars_set(variables, "value_default_value",
			    xcat(jv_default_value_(value, true, &context->options), ".getNumber()"));

		lp_vars_set(variables, "value_enum_type", TypeName_lite_map(value, false));

		lp_vars_insert(variables, "value_enum_type_pass_through_nullness",
			       xcat(pass_through_nullness, lp_vars_get(variables, "value_enum_type")));

		if (jv_support_unknown_enum_value(value)) {
			// Map unknown values to a special UNRECOGNIZED value if supported.
			lp_vars_insert(variables, "unrecognized_value",
				       xcat(lp_vars_get(variables, "value_enum_type"), ".UNRECOGNIZED"));
		} else {
			// Map unknown values to the default value if we don't have UNRECOGNIZED.
			lp_vars_set(variables, "unrecognized_value", jv_default_value_(value, true, &context->options));
		}
	} else {
		lp_vars_set(variables, "value_type", TypeName_lite_map(value, false));

		lp_vars_insert(variables, "value_type_pass_through_nullness",
			       xcat((jv_is_reference_type(valueJavaType) ? pass_through_nullness : ""),
				    lp_vars_get(variables, "value_type")));

		lp_vars_set(variables, "boxed_value_type", TypeName_lite_map(value, true));
		lp_vars_set(variables, "value_wire_type", WireType_lite_map(value));
		lp_vars_set(variables, "value_default_value", jv_default_value_(value, true, &context->options));
	}
	lp_vars_insert(
		variables, "type_parameters",
		xcat(lp_vars_get(variables, "boxed_key_type"), ", ", lp_vars_get(variables, "boxed_value_type")));
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(variables, "deprecation", OPT_deprecated(descriptor) ? "@java.lang.Deprecated " : "");

	lp_vars_insert(variables, "default_entry",
		       xcat(lp_vars_get(variables, "capitalized_name"), "DefaultEntryHolder.defaultEntry"));
	// { and } variables are used as delimiters when emitting annotations.
	lp_vars_set(variables, "{", "");
	lp_vars_set(variables, "}", "");
}

static void ImmutableMapFieldLiteGenerator_init(JvFieldGen *g)
{
	SetMessageVariables_lite_map(g->descriptor, g->message_bit_index, 0,
				     jv_context_field_info(g->context, g->descriptor), g->context, &g->variables);
}

static int ImmutableMapFieldLiteGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static void ImmutableMapFieldLiteGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables, "$deprecation$int ${$get$capitalized_name$Count$}$();\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$boolean ${$contains$capitalized_name$$}$(\n"
		      "    $key_type$ key);\n");
	const LpField *value = jv_map_value_field(g->descriptor);
	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
				      "${$get$capitalized_name$$}$();\n");
		}
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
			      "${$get$capitalized_name$Map$}$();\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$$value_enum_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_enum_type_pass_through_nullness$ "
			      "        defaultValue);\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$$value_enum_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key);\n");
		if (jv_support_unknown_enum_value(value)) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$ValueMap()} instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "java.util.Map<$type_parameters$>\n"
				      "${$get$capitalized_name$Value$}$();\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$java.util.Map<$type_parameters$>\n"
				      "${$get$capitalized_name$ValueMap$}$();\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$\n"
				      "$value_type_pass_through_nullness$ "
				      "${$get$capitalized_name$ValueOrDefault$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type_pass_through_nullness$ defaultValue);\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$\n"
				      "$value_type$ ${$get$capitalized_name$ValueOrThrow$}$(\n"
				      "    $key_type$ key);\n");
		}
	} else {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "java.util.Map<$type_parameters$>\n"
				      "${$get$capitalized_name$$}$();\n");
		}
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$java.util.Map<$type_parameters$>\n"
			      "${$get$capitalized_name$Map$}$();\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$\n"
			      "$value_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type_pass_through_nullness$ defaultValue);\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$\n"
			      "$value_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key);\n");
	}
}

static void ImmutableMapFieldLiteGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "private static final class $capitalized_name$DefaultEntryHolder {\n"
		      "  static final com.google.protobuf.MapEntryLite<\n"
		      "      $type_parameters$> defaultEntry =\n"
		      "          com.google.protobuf.MapEntryLite\n"
		      "          .<$type_parameters$>newDefaultInstance(\n"
		      "              $key_wire_type$,\n"
		      "              $key_default_value$,\n"
		      "              $value_wire_type$,\n"
		      "              $value_default_value$);\n"
		      "}\n");
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.MapFieldLite<\n"
		      "    $type_parameters$> $name$_ =\n"
		      "        com.google.protobuf.MapFieldLite.emptyMapField();\n"
		      "private com.google.protobuf.MapFieldLite<$type_parameters$>\n"
		      "internalGet$capitalized_name$() {\n"
		      "  return $name$_;\n"
		      "}\n"
		      "private com.google.protobuf.MapFieldLite<$type_parameters$>\n"
		      "internalGetMutable$capitalized_name$() {\n"
		      "  if (!$name$_.isMutable()) {\n"
		      "    $name$_ = $name$_.mutableCopy();\n"
		      "  }\n"
		      "  return $name$_;\n"
		      "}\n");
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$\n"
		      "public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return internalGet$capitalized_name$().size();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$\n"
		      "public boolean ${$contains$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  return internalGet$capitalized_name$().containsKey(key);\n"
		      "}\n");
	const LpField *value = jv_map_value_field(g->descriptor);
	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		lp_print_vars(printer, &g->variables,
			      "private static final\n"
			      "com.google.protobuf.Internal.MapAdapter.Converter<\n"
			      "    java.lang.Integer, $value_enum_type$> $name$ValueConverter =\n"
			      "        com.google.protobuf.Internal.MapAdapter.newEnumConverter(\n"
			      "            $value_enum_type$.internalGetValueMap(),\n"
			      "            $unrecognized_value$);\n");
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
				      "${$get$capitalized_name$$}$() {\n"
				      "  return get$capitalized_name$Map();\n"
				      "}\n");
		}
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
			      "${$get$capitalized_name$Map$}$() {\n"
			      "  return java.util.Collections.unmodifiableMap(\n"
			      "      new com.google.protobuf.Internal.MapAdapter<\n"
			      "        $boxed_key_type$, $value_enum_type$, java.lang.Integer>(\n"
			      "            internalGet$capitalized_name$(),\n"
			      "            $name$ValueConverter));\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_enum_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_enum_type_pass_through_nullness$ defaultValue) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
			      "      internalGet$capitalized_name$();\n"
			      "  return map.containsKey(key)\n"
			      "         ? $name$ValueConverter.doForward(map.get(key))\n"
			      "         : defaultValue;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_enum_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
			      "      internalGet$capitalized_name$();\n"
			      "  if (!map.containsKey(key)) {\n"
			      "    throw new java.lang.IllegalArgumentException();\n"
			      "  }\n"
			      "  return $name$ValueConverter.doForward(map.get(key));\n"
			      "}\n");
		if (jv_support_unknown_enum_value(value)) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$ValueMap()} instead.\n"
				      " */\n"
				      "@java.lang.Override\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$boxed_key_type$, $boxed_value_type$>\n"
				      "${$get$capitalized_name$Value$}$() {\n"
				      "  return get$capitalized_name$ValueMap();\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$\n"
				      "public java.util.Map<$boxed_key_type$, $boxed_value_type$>\n"
				      "${$get$capitalized_name$ValueMap$}$() {\n"
				      "  return java.util.Collections.unmodifiableMap(\n"
				      "      internalGet$capitalized_name$());\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$\n"
				      "public $value_type_pass_through_nullness$ "
				      "${$get$capitalized_name$ValueOrDefault$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type_pass_through_nullness$ defaultValue) {\n"
				      "  $key_null_check$\n"
				      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
				      "      internalGet$capitalized_name$();\n"
				      "  return map.containsKey(key) ? map.get(key) : defaultValue;\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$\n"
				      "public $value_type$ ${$get$capitalized_name$ValueOrThrow$}$(\n"
				      "    $key_type$ key) {\n"
				      "  $key_null_check$\n"
				      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
				      "      internalGet$capitalized_name$();\n"
				      "  if (!map.containsKey(key)) {\n"
				      "    throw new java.lang.IllegalArgumentException();\n"
				      "  }\n"
				      "  return map.get(key);\n"
				      "}\n");
		}
	} else {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Override\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$type_parameters$> "
				      "${$get$capitalized_name$$}$() {\n"
				      "  return get$capitalized_name$Map();\n"
				      "}\n");
		}
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public java.util.Map<$type_parameters$> "
			      "${$get$capitalized_name$Map$}$() {\n"
			      "  return java.util.Collections.unmodifiableMap(\n"
			      "      internalGet$capitalized_name$());\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type_pass_through_nullness$ defaultValue) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$type_parameters$> map =\n"
			      "      internalGet$capitalized_name$();\n"
			      "  $boxed_value_type$ v = map.get(key);\n"
			      "  return v != null ? v : defaultValue;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$type_parameters$> map =\n"
			      "      internalGet$capitalized_name$();\n"
			      "  $boxed_value_type$ v = map.get(key);\n"
			      "  if (v == null) {\n"
			      "    throw new java.lang.IllegalArgumentException();\n"
			      "  }\n"
			      "  return v;\n"
			      "}\n");
	}

	// Generate private setters for the builder to proxy into.
	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "private java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
			      "getMutable$capitalized_name$Map() {\n"
			      "  return new com.google.protobuf.Internal.MapAdapter<\n"
			      "      $boxed_key_type$, $value_enum_type$, java.lang.Integer>(\n"
			      "          internalGetMutable$capitalized_name$(),\n"
			      "          $name$ValueConverter);\n"
			      "}\n");
		if (jv_support_unknown_enum_value(value)) {
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "private java.util.Map<$boxed_key_type$, $boxed_value_type$>\n"
				      "getMutable$capitalized_name$ValueMap() {\n"
				      "  return internalGetMutable$capitalized_name$();\n"
				      "}\n");
		}
	} else {
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "private java.util.Map<$type_parameters$>\n"
			      "getMutable$capitalized_name$Map() {\n"
			      "  return internalGetMutable$capitalized_name$();\n"
			      "}\n");
	}
}

static void ImmutableMapFieldLiteGenerator_GenerateFieldInfo(const JvFieldGen *g, LpPrinter *printer, JvUtf16 *output)
{
	jv_write_int_to_utf16_char_sequence(g->descriptor->number, output);
	jv_write_int_to_utf16_char_sequence(jv_get_experimental_java_field_type(g->descriptor), output);
	lp_print_vars(printer, &g->variables,
		      "\"$name$_\",\n"
		      "$default_entry$,\n");
	const LpField *value = jv_map_value_field(g->descriptor);
	if (!jv_support_unknown_enum_value(value) && jv_get_java_type(value) == JAVATYPE_ENUM) {
		jv_print_enum_verifier_logic(printer, jv_map_value_field(g->descriptor), &g->variables,
					     /*var_name=*/"$value_enum_type$",
					     /*terminating_string=*/",\n",
					     /*enforce_lite=*/jv_context_enforce_lite(g->context));
	}
}

static void ImmutableMapFieldLiteGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$\n"
		      "public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return instance.get$capitalized_name$Map().size();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$\n"
		      "public boolean ${$contains$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  return instance.get$capitalized_name$Map().containsKey(key);\n"
		      "}\n");
	lp_print_vars(printer, &g->variables,
		      "$deprecation$\n"
		      "public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  copyOnWrite();\n"
		      "  instance.getMutable$capitalized_name$Map().clear();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$\n"
		      "public Builder ${$remove$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  copyOnWrite();\n"
		      "  instance.getMutable$capitalized_name$Map().remove(key);\n"
		      "  return this;\n"
		      "}\n");
	const LpField *value = jv_map_value_field(g->descriptor);
	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
				      "${$get$capitalized_name$$}$() {\n"
				      "  return get$capitalized_name$Map();\n"
				      "}\n");
		}
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
			      "${$get$capitalized_name$Map$}$() {\n"
			      "  return java.util.Collections.unmodifiableMap(\n"
			      "      instance.get$capitalized_name$Map());\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_enum_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_enum_type_pass_through_nullness$ defaultValue) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$boxed_key_type$, $value_enum_type$> map =\n"
			      "      instance.get$capitalized_name$Map();\n"
			      "  return map.containsKey(key)\n"
			      "         ? map.get(key)\n"
			      "         : defaultValue;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_enum_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$boxed_key_type$, $value_enum_type$> map =\n"
			      "      instance.get$capitalized_name$Map();\n"
			      "  if (!map.containsKey(key)) {\n"
			      "    throw new java.lang.IllegalArgumentException();\n"
			      "  }\n"
			      "  return map.get(key);\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$put$capitalized_name$$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_enum_type$ value) {\n"
			      "  $key_null_check$\n"
			      "  $value_null_check$\n"
			      "  copyOnWrite();\n"
			      "  instance.getMutable$capitalized_name$Map().put(key, value);\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$putAll$capitalized_name$$}$(\n"
			      "    java.util.Map<$boxed_key_type$, $value_enum_type$> values) {\n"
			      "  copyOnWrite();\n"
			      "  instance.getMutable$capitalized_name$Map().putAll(values);\n"
			      "  return this;\n"
			      "}\n");
		if (jv_support_unknown_enum_value(value)) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$ValueMap()} instead.\n"
				      " */\n"
				      "@java.lang.Override\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$boxed_key_type$, $boxed_value_type$>\n"
				      "${$get$capitalized_name$Value$}$() {\n"
				      "  return get$capitalized_name$ValueMap();\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$\n"
				      "public java.util.Map<$boxed_key_type$, $boxed_value_type$>\n"
				      "${$get$capitalized_name$ValueMap$}$() {\n"
				      "  return java.util.Collections.unmodifiableMap(\n"
				      "      instance.get$capitalized_name$ValueMap());\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$\n"
				      "public $value_type_pass_through_nullness$ "
				      "${$get$capitalized_name$ValueOrDefault$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type_pass_through_nullness$ defaultValue) {\n"
				      "  $key_null_check$\n"
				      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
				      "      instance.get$capitalized_name$ValueMap();\n"
				      "  return map.containsKey(key) ? map.get(key) : defaultValue;\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$\n"
				      "public $value_type$ ${$get$capitalized_name$ValueOrThrow$}$(\n"
				      "    $key_type$ key) {\n"
				      "  $key_null_check$\n"
				      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
				      "      instance.get$capitalized_name$ValueMap();\n"
				      "  if (!map.containsKey(key)) {\n"
				      "    throw new java.lang.IllegalArgumentException();\n"
				      "  }\n"
				      "  return map.get(key);\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$public Builder ${$put$capitalized_name$Value$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type$ value) {\n"
				      "  $key_null_check$\n"
				      "  copyOnWrite();\n"
				      "  instance.getMutable$capitalized_name$ValueMap().put(key, value);\n"
				      "  return this;\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$public Builder ${$putAll$capitalized_name$Value$}$(\n"
				      "    java.util.Map<$boxed_key_type$, $boxed_value_type$> values) {\n"
				      "  copyOnWrite();\n"
				      "  instance.getMutable$capitalized_name$ValueMap().putAll(values);\n"
				      "  return this;\n"
				      "}\n");
		}
	} else {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Override\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$type_parameters$> "
				      "${$get$capitalized_name$$}$() {\n"
				      "  return get$capitalized_name$Map();\n"
				      "}\n");
		}
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$"
			      "public java.util.Map<$type_parameters$> "
			      "${$get$capitalized_name$Map$}$() {\n"
			      "  return java.util.Collections.unmodifiableMap(\n"
			      "      instance.get$capitalized_name$Map());\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type_pass_through_nullness$ defaultValue) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$type_parameters$> map =\n"
			      "      instance.get$capitalized_name$Map();\n"
			      "  return map.containsKey(key) ? map.get(key) : defaultValue;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$\n"
			      "public $value_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$type_parameters$> map =\n"
			      "      instance.get$capitalized_name$Map();\n"
			      "  if (!map.containsKey(key)) {\n"
			      "    throw new java.lang.IllegalArgumentException();\n"
			      "  }\n"
			      "  return map.get(key);\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$"
			      "public Builder ${$put$capitalized_name$$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type$ value) {\n"
			      "  $key_null_check$\n"
			      "  $value_null_check$\n"
			      "  copyOnWrite();\n"
			      "  instance.getMutable$capitalized_name$Map().put(key, value);\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$"
			      "public Builder ${$putAll$capitalized_name$$}$(\n"
			      "    java.util.Map<$type_parameters$> values) {\n"
			      "  copyOnWrite();\n"
			      "  instance.getMutable$capitalized_name$Map().putAll(values);\n"
			      "  return this;\n"
			      "}\n");
	}
}

static void ImmutableMapFieldLiteGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	// Nothing to initialize.
}

static const char *ImmutableMapFieldLiteGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->message_type);
}

static const JvFieldGenOps ImmutableMapFieldLiteGenerator_ops = {
	.GetNumBitsForMessage = ImmutableMapFieldLiteGenerator_GetNumBitsForMessage,
	.GenerateInterfaceMembers = ImmutableMapFieldLiteGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableMapFieldLiteGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableMapFieldLiteGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableMapFieldLiteGenerator_GenerateInitializationCode,
	.GetBoxedType = ImmutableMapFieldLiteGenerator_GetBoxedType,
	.GenerateFieldInfo = ImmutableMapFieldLiteGenerator_GenerateFieldInfo,
};
