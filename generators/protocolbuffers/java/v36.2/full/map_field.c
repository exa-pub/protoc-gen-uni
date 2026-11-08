// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/map_field.{h,cc} (protobuf v36.2). */

static char *WireType_map(const LpField *field);
static void ImmutableMapFieldGenerator_SetMessageVariables(JvFieldGen *g, const JvFieldGeneratorInfo *info);
static int ImmutableMapFieldGenerator_GetMessageBitIndex(const JvFieldGen *g);
static int ImmutableMapFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g);
static void ImmutableMapFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateMapGetters(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateMessageMapBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateMessageMapGetters(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutableMapFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutableMapFieldGenerator_GetBoxedType(const JvFieldGen *g);

static const char *TypeName_map(const LpField *field, bool boxed)
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

static char *WireType_map(const LpField *field)
{
	return xcat("com.google.protobuf.WireFormat.FieldType.", FieldTypeName(field->type));
}

static void ImmutableMapFieldGenerator_SetMessageVariables(JvFieldGen *g, const JvFieldGeneratorInfo *info);
static void ImmutableMapFieldGenerator_init(JvFieldGen *g)
{
	ImmutableMapFieldGenerator_SetMessageVariables(g, jv_context_field_info(g->context, g->descriptor));
}

static void ImmutableMapFieldGenerator_SetMessageVariables(JvFieldGen *g, const JvFieldGeneratorInfo *info)
{
	jv_set_common_field_variables(g->descriptor, info, &g->variables);

	lp_vars_set(&g->variables, "type", jv_immutable_class_name(g->descriptor->message_type));
	const LpField *key = jv_map_key_field(g->descriptor);
	const LpField *value = jv_map_value_field(g->descriptor);
	const JvJavaType keyJavaType = jv_get_java_type(key);
	const JvJavaType valueJavaType = jv_get_java_type(value);

	// The code that generates the open-source version appears not to understand
	// #else, so we have an #ifndef instead.
	const char *pass_through_nullness =
		JV_IS_OSS ? "/* nullable */\n" : "@com.google.protobuf.Internal.ProtoPassThroughNullness ";

	lp_vars_set(&g->variables, "key_type", TypeName_map(key, false));
	const char *boxed_key_type = TypeName_map(key, true);
	lp_vars_set(&g->variables, "boxed_key_type", boxed_key_type);
	// Used for calling the serialization function.
	lp_vars_set(&g->variables, "short_key_type",
		    strrchr(boxed_key_type, '.') ? strrchr(boxed_key_type, '.') + 1 : boxed_key_type);
	lp_vars_set(&g->variables, "key_wire_type", WireType_map(key));
	lp_vars_set(&g->variables, "key_default_value", jv_default_value_(key, true, &g->context->options));
	lp_vars_set(&g->variables, "key_null_check",
		    jv_is_reference_type(keyJavaType) ? "java.util.Objects.requireNonNull(key, \"map key\");" : "");
	lp_vars_set(&g->variables, "value_null_check",
		    valueJavaType != JAVATYPE_ENUM && jv_is_reference_type(valueJavaType)
			    ? "java.util.Objects.requireNonNull(value, \"map value\");"
			    : "");
	if (valueJavaType == JAVATYPE_ENUM) {
		// We store enums as Integers internally.
		lp_vars_set(&g->variables, "value_type", "int");
		lp_vars_insert(&g->variables, "value_type_pass_through_nullness",
			       lp_vars_get(&g->variables, "value_type"));
		lp_vars_set(&g->variables, "boxed_value_type", "java.lang.Integer");
		lp_vars_set(&g->variables, "value_wire_type", WireType_map(value));
		lp_vars_set(&g->variables, "value_default_value",
			    xcat(jv_default_value_(value, true, &g->context->options), ".getNumber()"));

		lp_vars_set(&g->variables, "value_enum_type", TypeName_map(value, false));

		lp_vars_insert(&g->variables, "value_enum_type_pass_through_nullness",
			       xcat(pass_through_nullness, lp_vars_get(&g->variables, "value_enum_type")));

		if (jv_support_unknown_enum_value(value)) {
			// Map unknown values to a special UNRECOGNIZED value if supported.
			lp_vars_insert(&g->variables, "unrecognized_value",
				       xcat(lp_vars_get(&g->variables, "value_enum_type"), ".UNRECOGNIZED"));
		} else {
			// Map unknown values to the default value if we don't have UNRECOGNIZED.
			lp_vars_set(&g->variables, "unrecognized_value",
				    jv_default_value_(value, true, &g->context->options));
		}
	} else {
		lp_vars_set(&g->variables, "value_type", TypeName_map(value, false));

		lp_vars_insert(&g->variables, "value_type_pass_through_nullness",
			       xcat((jv_is_reference_type(valueJavaType) ? pass_through_nullness : ""),
				    lp_vars_get(&g->variables, "value_type")));

		lp_vars_set(&g->variables, "boxed_value_type", TypeName_map(value, true));
		lp_vars_set(&g->variables, "value_wire_type", WireType_map(value));
		lp_vars_set(&g->variables, "value_default_value", jv_default_value_(value, true, &g->context->options));
	}

	lp_vars_insert(&g->variables, "type_parameters",
		       xcat(lp_vars_get(&g->variables, "boxed_key_type"), ", ",
			    lp_vars_get(&g->variables, "boxed_value_type")));

	if (valueJavaType == JAVATYPE_MESSAGE) {
		lp_vars_set(&g->variables, "value_interface_type",
			    xcat(lp_vars_get(&g->variables, "boxed_value_type"), "OrBuilder"));
		lp_vars_set(&g->variables, "value_builder_type",
			    xcat(lp_vars_get(&g->variables, "boxed_value_type"), ".Builder"));
		lp_vars_set(&g->variables, "builder_type_parameters",
			    xcat(lp_vars_get(&g->variables, "boxed_key_type"), ", ",
				 lp_vars_get(&g->variables, "value_interface_type"), ", ",
				 lp_vars_get(&g->variables, "boxed_value_type"), ", ",
				 lp_vars_get(&g->variables, "value_builder_type")));
	}
	// TODO: Add @deprecated javadoc when generating javadoc is supported
	// by the proto compiler
	lp_vars_set(&g->variables, "deprecation", OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(&g->variables, "on_changed", "onChanged();");

	lp_vars_insert(&g->variables, "default_entry",
		       xcat(lp_vars_get(&g->variables, "capitalized_name"), "DefaultEntryHolder.defaultEntry"));
	lp_vars_insert(&g->variables, "map_field_parameter", lp_vars_get(&g->variables, "default_entry"));
	lp_vars_set(&g->variables, "descriptor",
		    xcat(jv_immutable_class_name(g->descriptor->file), ".internal_",
			 jv_unique_file_scope_identifier(g->descriptor->message_type), "_descriptor, "));
	lp_vars_set(&g->variables, "get_has_field_bit_builder", jv_generate_get_bit(g->builder_bit_index));
	lp_vars_set(&g->variables, "get_has_field_bit_from_local",
		    jv_generate_get_bit_from_local(g->builder_bit_index));
	lp_vars_set(&g->variables, "set_has_field_bit_builder", xcat(jv_generate_set_bit(g->builder_bit_index), ";"));
	lp_vars_set(&g->variables, "clear_has_field_bit_builder",
		    xcat(jv_generate_clear_bit(g->builder_bit_index), ";"));
}

static int ImmutableMapFieldGenerator_GetMessageBitIndex(const JvFieldGen *g) { return g->message_bit_index; }

static int ImmutableMapFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g) { return g->builder_bit_index; }

static int ImmutableMapFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static int ImmutableMapFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void ImmutableMapFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
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
				      "$deprecation$$value_type_pass_through_nullness$ "
				      "${$get$capitalized_name$ValueOrDefault$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type_pass_through_nullness$ defaultValue);\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$$value_type$ ${$get$capitalized_name$ValueOrThrow$}$(\n"
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
			      "$deprecation$$value_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type_pass_through_nullness$ defaultValue);\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$$value_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key);\n");
	}
}

static void ImmutableMapFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "private static final class $capitalized_name$DefaultEntryHolder {\n"
		      "  static final com.google.protobuf.MapEntry<\n"
		      "      $type_parameters$> defaultEntry =\n"
		      "          com.google.protobuf.MapEntry\n"
		      "          .<$type_parameters$>newDefaultInstance(\n"
		      "              $descriptor$\n"
		      "              $key_wire_type$,\n"
		      "              $key_default_value$,\n"
		      "              $value_wire_type$,\n"
		      "              $value_default_value$);\n"
		      "}\n");
	lp_print_vars(printer, &g->variables,
		      "@SuppressWarnings(\"serial\")\n"
		      "private com.google.protobuf.MapField<\n"
		      "    $type_parameters$> $name$_;\n"
		      "private com.google.protobuf.MapField<$type_parameters$>\n"
		      "internalGet$capitalized_name$() {\n"
		      "  if ($name$_ == null) {\n"
		      "    return com.google.protobuf.MapField.emptyMapField(\n"
		      "        $map_field_parameter$);\n"
		      "  }\n"
		      "  return $name$_;\n"
		      "}\n");
	if (jv_get_java_type(jv_map_value_field(g->descriptor)) == JAVATYPE_ENUM) {
		lp_print_vars(printer, &g->variables,
			      "private static final\n"
			      "com.google.protobuf.Internal.MapAdapter.Converter<\n"
			      "    java.lang.Integer, $value_enum_type$> $name$ValueConverter =\n"
			      "        com.google.protobuf.Internal.MapAdapter.newEnumConverter(\n"
			      "            $value_enum_type$.internalGetValueMap(),\n"
			      "            $unrecognized_value$);\n");
		lp_print_vars(printer, &g->variables,
			      "private static final java.util.Map<$boxed_key_type$, "
			      "$value_enum_type$>\n"
			      "internalGetAdapted$capitalized_name$Map(\n"
			      "    java.util.Map<$boxed_key_type$, $boxed_value_type$> map) {\n"
			      "  return new com.google.protobuf.Internal.MapAdapter<\n"
			      "      $boxed_key_type$, $value_enum_type$, java.lang.Integer>(\n"
			      "          map, $name$ValueConverter);\n"
			      "}\n");
	}
	ImmutableMapFieldGenerator_GenerateMapGetters(g, printer);
}

static void ImmutableMapFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_get_java_type(jv_map_value_field(g->descriptor)) == JAVATYPE_MESSAGE) {
		return ImmutableMapFieldGenerator_GenerateMessageMapBuilderMembers(g, printer);
	}
	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.MapField<\n"
		      "    $type_parameters$> $name$_;\n"
		      "$deprecation$private com.google.protobuf.MapField<$type_parameters$>\n"
		      "    internalGet$capitalized_name$() {\n"
		      "  if ($name$_ == null) {\n"
		      "    return com.google.protobuf.MapField.emptyMapField(\n"
		      "        $map_field_parameter$);\n"
		      "  }\n"
		      "  return $name$_;\n"
		      "}\n"
		      "$deprecation$private com.google.protobuf.MapField<$type_parameters$>\n"
		      "    internalGetMutable$capitalized_name$() {\n"
		      "  if ($name$_ == null) {\n"
		      "    $name$_ = com.google.protobuf.MapField.newMapField(\n"
		      "        $map_field_parameter$);\n"
		      "  }\n"
		      "  if (!$name$_.isMutable()) {\n"
		      "    $name$_ = $name$_.copy();\n"
		      "  }\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return $name$_;\n"
		      "}\n");
	ImmutableMapFieldGenerator_GenerateMapGetters(g, printer);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $clear_has_field_bit_builder$\n"
		      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
		      "      .clear();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$remove$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
		      "      .remove(key);\n"
		      "  return this;\n"
		      "}\n");
	const LpField *value = jv_map_value_field(g->descriptor);
	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use alternate mutation accessors instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
				      "    ${$getMutable$capitalized_name$$}$() {\n"
				      "  $set_has_field_bit_builder$\n"
				      "  return internalGetAdapted$capitalized_name$Map(\n"
				      "       internalGetMutable$capitalized_name$().getMutableMap());\n"
				      "}\n");
		}

		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$put$capitalized_name$$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_enum_type$ value) {\n"
			      "  $key_null_check$\n"
			      "  $value_null_check$\n"
			      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
			      "      .put(key, $name$ValueConverter.doBackward(value));\n"
			      "  $set_has_field_bit_builder$\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$putAll$capitalized_name$$}$(\n"
			      "    java.util.Map<$boxed_key_type$, $value_enum_type$> values) {\n"
			      "  internalGetAdapted$capitalized_name$Map(\n"
			      "      internalGetMutable$capitalized_name$().getMutableMap())\n"
			      "          .putAll(values);\n"
			      "  $set_has_field_bit_builder$\n"
			      "  return this;\n"
			      "}\n");
		if (jv_support_unknown_enum_value(value)) {
			if (true) {
				lp_print_vars(printer, &g->variables,
					      "/**\n"
					      " * Use alternate mutation accessors instead.\n"
					      " */\n"
					      "@java.lang.Deprecated\n"
					      "public java.util.Map<$boxed_key_type$, $boxed_value_type$>\n"
					      "${$getMutable$capitalized_name$Value$}$() {\n"
					      "  $set_has_field_bit_builder$\n"
					      "  return internalGetMutable$capitalized_name$().getMutableMap();\n"
					      "}\n");
			}

			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$public Builder ${$put$capitalized_name$Value$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type$ value) {\n"
				      "  $key_null_check$\n"
				      "  $value_null_check$\n"
				      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
				      "      .put(key, value);\n"
				      "  $set_has_field_bit_builder$\n"
				      "  return this;\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "$deprecation$public Builder ${$putAll$capitalized_name$Value$}$(\n"
				      "    java.util.Map<$boxed_key_type$, $boxed_value_type$> values) {\n"
				      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
				      "      .putAll(values);\n"
				      "  $set_has_field_bit_builder$\n"
				      "  return this;\n"
				      "}\n");
		}
	} else {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use alternate mutation accessors instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$type_parameters$>\n"
				      "    ${$getMutable$capitalized_name$$}$() {\n"
				      "  $set_has_field_bit_builder$\n"
				      "  return internalGetMutable$capitalized_name$().getMutableMap();\n"
				      "}\n");
		}

		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$put$capitalized_name$$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type$ value) {\n"
			      "  $key_null_check$\n"
			      "  $value_null_check$\n"
			      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
			      "      .put(key, value);\n"
			      "  $set_has_field_bit_builder$\n"
			      "  return this;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "$deprecation$public Builder ${$putAll$capitalized_name$$}$(\n"
			      "    java.util.Map<$type_parameters$> values) {\n"
			      "  internalGetMutable$capitalized_name$().getMutableMap()\n"
			      "      .putAll(values);\n"
			      "  $set_has_field_bit_builder$\n"
			      "  return this;\n"
			      "}\n");
	}
}

static void ImmutableMapFieldGenerator_GenerateMapGetters(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return internalGet$capitalized_name$().getMap().size();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$contains$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  return internalGet$capitalized_name$().getMap().containsKey(key);\n"
		      "}\n");
	const LpField *value = jv_map_value_field(g->descriptor);
	if (jv_get_java_type(value) == JAVATYPE_ENUM) {
		if (true) {
			lp_print_vars(printer, &g->variables,
				      "/**\n"
				      " * Use {@link #get$capitalized_name$Map()} instead.\n"
				      " */\n"
				      "@java.lang.Override\n"
				      "@java.lang.Deprecated\n"
				      "public java.util.Map<$boxed_key_type$, $value_enum_type$>\n"
				      "${$get$capitalized_name$$}$() {\n"
				      "  return get$capitalized_name$Map();\n"
				      "}\n");
		}

		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public java.util.Map<$boxed_key_type$, "
			      "$value_enum_type$>\n"
			      "${$get$capitalized_name$Map$}$() {\n"
			      "  return internalGetAdapted$capitalized_name$Map(\n"
			      "      internalGet$capitalized_name$().getMap());"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public $value_enum_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_enum_type_pass_through_nullness$ defaultValue) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
			      "      internalGet$capitalized_name$().getMap();\n"
			      "  return map.containsKey(key)\n"
			      "         ? $name$ValueConverter.doForward(map.get(key))\n"
			      "         : defaultValue;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public $value_enum_type$ "
			      "${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
			      "      internalGet$capitalized_name$().getMap();\n"
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
				      "$deprecation$public java.util.Map<$boxed_key_type$, "
				      "$boxed_value_type$>\n"
				      "${$get$capitalized_name$ValueMap$}$() {\n"
				      "  return internalGet$capitalized_name$().getMap();\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$public $value_type_pass_through_nullness$ "
				      "${$get$capitalized_name$ValueOrDefault$}$(\n"
				      "    $key_type$ key,\n"
				      "    $value_type_pass_through_nullness$ defaultValue) {\n"
				      "  $key_null_check$\n"
				      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
				      "      internalGet$capitalized_name$().getMap();\n"
				      "  return map.containsKey(key) ? map.get(key) : defaultValue;\n"
				      "}\n");
			jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
			lp_print_vars(printer, &g->variables,
				      "@java.lang.Override\n"
				      "$deprecation$public $value_type$ "
				      "${$get$capitalized_name$ValueOrThrow$}$(\n"
				      "    $key_type$ key) {\n"
				      "  $key_null_check$\n"
				      "  java.util.Map<$boxed_key_type$, $boxed_value_type$> map =\n"
				      "      internalGet$capitalized_name$().getMap();\n"
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
			      "$deprecation$public java.util.Map<$type_parameters$> "
			      "${$get$capitalized_name$Map$}$() {\n"
			      "  return internalGet$capitalized_name$().getMap();\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public $value_type_pass_through_nullness$ "
			      "${$get$capitalized_name$OrDefault$}$(\n"
			      "    $key_type$ key,\n"
			      "    $value_type_pass_through_nullness$ defaultValue) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$type_parameters$> map =\n"
			      "      internalGet$capitalized_name$().getMap();\n"
			      "  return map.containsKey(key) ? map.get(key) : defaultValue;\n"
			      "}\n");
		jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public $value_type$ ${$get$capitalized_name$OrThrow$}$(\n"
			      "    $key_type$ key) {\n"
			      "  $key_null_check$\n"
			      "  java.util.Map<$type_parameters$> map =\n"
			      "      internalGet$capitalized_name$().getMap();\n"
			      "  if (!map.containsKey(key)) {\n"
			      "    throw new java.lang.IllegalArgumentException();\n"
			      "  }\n"
			      "  return map.get(key);\n"
			      "}\n");
	}
}

static void ImmutableMapFieldGenerator_GenerateMessageMapBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "private static final class $capitalized_name$Converter implements "
		      "com.google.protobuf.MapFieldBuilder.Converter<$boxed_key_type$, "
		      "$value_interface_type$, $boxed_value_type$> "
		      "{\n");
	{
		lp_indent(printer);
		lp_print(printer, "@java.lang.Override\n", NULL);
		lp_print_vars(printer, &g->variables,
			      "public $boxed_value_type$ build($value_interface_type$ val) {\n");
		{
			lp_indent(printer);
			lp_print_vars(printer, &g->variables,
				      "if (val instanceof $boxed_value_type$) {"
				      " return ($boxed_value_type$) val; }\n");
			lp_print_vars(printer, &g->variables, "return (($value_builder_type$) val).build();\n");
			lp_outdent(printer);
		}
		lp_print(printer, "}\n\n", NULL);

		lp_print(printer, "@java.lang.Override\n", NULL);
		lp_print_vars(printer, &g->variables,
			      "public com.google.protobuf.MapEntry<$boxed_key_type$, "
			      "$boxed_value_type$> defaultEntry() {\n");
		{
			lp_indent(printer);
			lp_print_vars(printer, &g->variables,
				      "return $capitalized_name$DefaultEntryHolder.defaultEntry;\n");
			lp_outdent(printer);
		}
		lp_print(printer, "}\n", NULL);
		lp_outdent(printer);
	}
	lp_print(printer, "};\n", NULL);
	lp_print_vars(printer, &g->variables,
		      "private static final $capitalized_name$Converter "
		      "$name$Converter = new $capitalized_name$Converter();\n\n");

	lp_print_vars(printer, &g->variables,
		      "private com.google.protobuf.MapFieldBuilder<\n"
		      "    $builder_type_parameters$> $name$_;\n"
		      "$deprecation$private "
		      "com.google.protobuf.MapFieldBuilder<$builder_type_parameters$>\n"
		      "    internalGet$capitalized_name$() {\n"
		      "  if ($name$_ == null) {\n"
		      "    return new com.google.protobuf.MapFieldBuilder<>($name$Converter);\n"
		      "  }\n"
		      "  return $name$_;\n"
		      "}\n"
		      "$deprecation$private "
		      "com.google.protobuf.MapFieldBuilder<$builder_type_parameters$>\n"
		      "    internalGetMutable$capitalized_name$() {\n"
		      "  if ($name$_ == null) {\n"
		      "    $name$_ = new "
		      "com.google.protobuf.MapFieldBuilder<>($name$Converter);\n"
		      "  }\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return $name$_;\n"
		      "}\n");
	ImmutableMapFieldGenerator_GenerateMessageMapGetters(g, printer);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $clear_has_field_bit_builder$\n"
		      "  internalGetMutable$capitalized_name$().clear();\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$remove$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  internalGetMutable$capitalized_name$().ensureBuilderMap()\n"
		      "      .remove(key);\n"
		      "  return this;\n"
		      "}\n");
	if (true) {
		lp_print_vars(printer, &g->variables,
			      "/**\n"
			      " * Use alternate mutation accessors instead.\n"
			      " */\n"
			      "@java.lang.Deprecated\n"
			      "public java.util.Map<$type_parameters$>\n"
			      "    ${$getMutable$capitalized_name$$}$() {\n"
			      "  $set_has_field_bit_builder$\n"
			      "  return internalGetMutable$capitalized_name$().ensureMessageMap();\n"
			      "}\n");
	}

	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$put$capitalized_name$$}$(\n"
		      "    $key_type$ key,\n"
		      "    $value_type$ value) {\n"
		      "  $key_null_check$\n"
		      "  $value_null_check$\n"
		      "  internalGetMutable$capitalized_name$().ensureBuilderMap()\n"
		      "      .put(key, value);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$putAll$capitalized_name$$}$(\n"
		      "    java.util.Map<$type_parameters$> values) {\n"
		      "  for (java.util.Map.Entry<$type_parameters$> e : values.entrySet()) {\n"
		      "    java.util.Objects.requireNonNull(e.getKey());\n"
		      "    java.util.Objects.requireNonNull(e.getValue());\n"
		      "  }\n"
		      "  internalGetMutable$capitalized_name$().ensureBuilderMap()\n"
		      "      .putAll(values);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $value_builder_type$ "
		      "${$put$capitalized_name$BuilderIfAbsent$}$(\n"
		      "    $key_type$ key) {\n"
		      "  java.util.Map<$boxed_key_type$, $value_interface_type$> builderMap = "
		      "internalGetMutable$capitalized_name$().ensureBuilderMap();\n"
		      "  $value_interface_type$ entry = builderMap.get(key);\n"
		      "  if (entry == null) {\n"
		      "    entry = $value_type$.newBuilder();\n"
		      "    builderMap.put(key, entry);\n"
		      "  }\n"
		      "  if (entry instanceof $value_type$) {\n"
		      "    entry = (($value_type$) entry).toBuilder();\n"
		      "    builderMap.put(key, entry);\n"
		      "  }\n"
		      "  return ($value_builder_type$) entry;\n"
		      "}\n");
}

static void ImmutableMapFieldGenerator_GenerateMessageMapGetters(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return internalGet$capitalized_name$().ensureBuilderMap().size();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public boolean ${$contains$capitalized_name$$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  return "
		      "internalGet$capitalized_name$().ensureBuilderMap().containsKey(key);\n"
		      "}\n");
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
		      "$deprecation$public java.util.Map<$type_parameters$> "
		      "${$get$capitalized_name$Map$}$() {\n"
		      "  return internalGet$capitalized_name$().getImmutableMap();\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $value_type_pass_through_nullness$ "
		      "${$get$capitalized_name$OrDefault$}$(\n"
		      "    $key_type$ key,\n"
		      "    $value_type_pass_through_nullness$ defaultValue) {\n"
		      "  $key_null_check$\n"
		      "  java.util.Map<$boxed_key_type$, $value_interface_type$> map = "
		      "internalGetMutable$capitalized_name$().ensureBuilderMap();\n"
		      "  return map.containsKey(key) ? $name$Converter.build(map.get(key)) : "
		      "defaultValue;\n"
		      "}\n");
	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $value_type$ ${$get$capitalized_name$OrThrow$}$(\n"
		      "    $key_type$ key) {\n"
		      "  $key_null_check$\n"
		      "  java.util.Map<$boxed_key_type$, $value_interface_type$> map = "
		      "internalGetMutable$capitalized_name$().ensureBuilderMap();\n"
		      "  if (!map.containsKey(key)) {\n"
		      "    throw new java.lang.IllegalArgumentException();\n"
		      "  }\n"
		      "  return $name$Converter.build(map.get(key));\n"
		      "}\n");
}

static void ImmutableMapFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	// Nothing to initialize.
}

static void ImmutableMapFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	// Nothing to initialize.
}

static void ImmutableMapFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No need to clear the has-bit since we clear the bitField ints all at once.
	lp_print_vars(printer, &g->variables, "internalGetMutable$capitalized_name$().clear();\n");
}

static void ImmutableMapFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "internalGetMutable$capitalized_name$().mergeFrom(\n"
		      "    other.internalGet$capitalized_name$());\n"
		      "$set_has_field_bit_builder$\n");
}

static void ImmutableMapFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (jv_get_java_type(jv_map_value_field(g->descriptor)) == JAVATYPE_MESSAGE) {
		lp_print_vars(printer, &g->variables,
			      "if ($get_has_field_bit_from_local$) {\n"
			      "  result.$name$_ = "
			      "internalGet$capitalized_name$().build($map_field_parameter$);\n"
			      "}\n");
		return;
	}
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  result.$name$_ = internalGet$capitalized_name$();\n"
		      "  result.$name$_.makeImmutable();\n"
		      "}\n");
}

static void ImmutableMapFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	const LpField *value = jv_map_value_field(g->descriptor);
	const JvJavaType type = jv_get_java_type(value);
	if (type == JAVATYPE_MESSAGE) {
		lp_print_vars(printer, &g->variables,
			      "com.google.protobuf.MapEntry<$type_parameters$>\n"
			      "$name$__ = input.readMessage(\n"
			      "    $default_entry$.getParserForType(), extensionRegistry);\n"
			      "internalGetMutable$capitalized_name$().ensureBuilderMap().put(\n"
			      "    $name$__.getKey(), $name$__.getValue());\n"
			      "$set_has_field_bit_builder$\n");
		return;
	}
	if (!jv_support_unknown_enum_value(value) && type == JAVATYPE_ENUM) {
		lp_print_vars(printer, &g->variables,
			      "com.google.protobuf.ByteString bytes = input.readBytes();\n"
			      "com.google.protobuf.MapEntry<$type_parameters$>\n"
			      "$name$__ = $default_entry$.getParserForType().parseFrom(bytes);\n"
			      "if ($value_enum_type$.forNumber($name$__.getValue()) == null) {\n"
			      "  mergeUnknownLengthDelimitedField($number$, bytes);\n"
			      "} else {\n"
			      "  internalGetMutable$capitalized_name$().getMutableMap().put(\n"
			      "      $name$__.getKey(), $name$__.getValue());\n"
			      "  $set_has_field_bit_builder$\n"
			      "}\n");
		return;
	}
	lp_print_vars(printer, &g->variables,
		      "com.google.protobuf.MapEntry<$type_parameters$>\n"
		      "$name$__ = input.readMessage(\n"
		      "    $default_entry$.getParserForType(), extensionRegistry);\n"
		      "internalGetMutable$capitalized_name$().getMutableMap().put(\n"
		      "    $name$__.getKey(), $name$__.getValue());\n"
		      "$set_has_field_bit_builder$\n");
}
static void ImmutableMapFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "com.google.protobuf.GeneratedMessage\n"
		      "  .serialize$short_key_type$MapTo(\n"
		      "    output,\n"
		      "    internalGet$capitalized_name$(),\n"
		      "    $default_entry$,\n"
		      "    $number$);\n");
}

static void ImmutableMapFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "for (java.util.Map.Entry<$type_parameters$> entry\n"
		      "     : internalGet$capitalized_name$().getMap().entrySet()) {\n"
		      "  com.google.protobuf.MapEntry<$type_parameters$>\n"
		      "  $name$__ = $default_entry$.newBuilderForType()\n"
		      "      .setKey(entry.getKey())\n"
		      "      .setValue(entry.getValue())\n"
		      "      .buildPartial();\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "      .computeMessageSize($number$, $name$__);\n"
		      "}\n");
}

static void ImmutableMapFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!internalGet$capitalized_name$().equals(\n"
		      "    other.internalGet$capitalized_name$())) return false;\n");
}

static void ImmutableMapFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!internalGet$capitalized_name$().getMap().isEmpty()) {\n"
		      "  hash = (37 * hash) + $constant_name$;\n"
		      "  hash = (53 * hash) + internalGet$capitalized_name$().hashCode();\n"
		      "}\n");
}

static const char *ImmutableMapFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return jv_immutable_class_name(g->descriptor->message_type);
}

static const JvFieldGenOps ImmutableMapFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutableMapFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutableMapFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutableMapFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutableMapFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutableMapFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutableMapFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutableMapFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutableMapFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutableMapFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutableMapFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutableMapFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutableMapFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutableMapFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode = ImmutableMapFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutableMapFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutableMapFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutableMapFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutableMapFieldGenerator_GenerateSerializationCode,
};
