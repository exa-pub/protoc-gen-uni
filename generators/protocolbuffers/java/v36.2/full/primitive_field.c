// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/primitive_field.{h,cc} (protobuf v36.2). */

static int ImmutablePrimitiveFieldGenerator_GetMessageBitIndex(const JvFieldGen *g);
static int ImmutablePrimitiveFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g);
static int ImmutablePrimitiveFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static int ImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g);
static void ImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										    LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *ImmutablePrimitiveFieldGenerator_GetBoxedType(const JvFieldGen *g);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void ImmutablePrimitiveOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer);
static int RepeatedImmutablePrimitiveFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g);
static int RepeatedImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
											    LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateInitializationCode(const JvFieldGen *g,
										LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g,
										LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCodeFromPacked(const JvFieldGen *g,
											  LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g,
										LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer);
static void RepeatedImmutablePrimitiveFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer);
static const char *RepeatedImmutablePrimitiveFieldGenerator_GetBoxedType(const JvFieldGen *g);

/* AddPrimitiveVariableForThisAndOther: create_value(this) and create_value(other) */
typedef char *(*JvCreateValueFn)(const char *value, const LpVars *variables);

static void add_primitive_variable_for_this_and_other(const char *this_variable_name, const char *other_variable_name,
						      JvCreateValueFn create_value, LpVars *variables)
{
	lp_vars_set(variables, this_variable_name, create_value(xcat(lp_vars_get(variables, "name"), "_"), variables));
	lp_vars_set(variables, other_variable_name,
		    create_value(xcat("other.get", lp_vars_get(variables, "capitalized_name"), "()"), variables));
}

static char *create_value_bytes(const char *value, const LpVars *v)
{
	(void)v;
	return xcat("!", value, ".isEmpty()");
}
static char *create_value_float(const char *value, const LpVars *v)
{
	(void)v;
	return xcat("java.lang.Float.floatToRawIntBits(", value, ") != 0");
}
static char *create_value_double(const char *value, const LpVars *v)
{
	(void)v;
	return xcat("java.lang.Double.doubleToRawLongBits(", value, ") != 0");
}
static char *create_value_default(const char *value, const LpVars *v)
{
	return xcat(value, " != ", lp_vars_get(v, "default"));
}

static void set_primitive_variables(const LpField *descriptor, int message_bit_index, int builder_bit_index,
				    const JvFieldGeneratorInfo *info, LpVars *variables, JvContext *context)
{
	jv_set_common_field_variables(descriptor, info, variables);
	JvJavaType java_type = jv_get_java_type(descriptor);

	lp_vars_set(variables, "type", jv_primitive_type_name(java_type));
	lp_vars_set(variables, "boxed_type", jv_boxed_primitive_type_name(java_type));
	lp_vars_insert(variables, "field_type", lp_vars_get(variables, "type"));

	const char *name = lp_vars_get(variables, "name");
	lp_vars_set(variables, "name_make_immutable", xcat(name, "_.makeImmutable()"));
	if (java_type == JAVATYPE_BOOLEAN || java_type == JAVATYPE_DOUBLE || java_type == JAVATYPE_FLOAT ||
	    java_type == JAVATYPE_INT || java_type == JAVATYPE_LONG) {
		char *capitalized_type = jv_underscores_to_camel_case(jv_primitive_type_name(java_type), true);
		lp_vars_set(variables, "field_list_type",
			    xcat("com.google.protobuf.Internal.", capitalized_type, "List"));
		lp_vars_set(variables, "empty_list", xcat("empty", capitalized_type, "List()"));
		lp_vars_set(variables, "repeated_get", xcat(name, "_.get", capitalized_type));
		lp_vars_set(variables, "repeated_add", xcat(name, "_.add", capitalized_type));
		lp_vars_set(variables, "repeated_set", xcat(name, "_.set", capitalized_type));
	} else {
		lp_vars_set(variables, "field_list_type",
			    "com.google.protobuf.Internal.ProtobufList<com.google.protobuf."
			    "ByteString>");
		lp_vars_set(variables, "empty_list", "emptyList(com.google.protobuf.ByteString.class)");
		lp_vars_set(variables, "repeated_get", xcat(name, "_.get"));
		lp_vars_set(variables, "repeated_add", xcat(name, "_.add"));
		lp_vars_set(variables, "repeated_set", xcat(name, "_.set"));
	}

	lp_vars_set(variables, "default", jv_default_value(descriptor, true, &context->options));
	lp_vars_set(variables, "default_init",
		    jv_is_default_value_java_default(descriptor)
			    ? ""
			    : xcat("= ", jv_default_value(descriptor, true, &context->options)));
	lp_vars_set(variables, "capitalized_type", jv_get_capitalized_type(descriptor, true));
	lp_vars_set(variables, "tag", jv_i(jv_make_tag(descriptor)));
	lp_vars_set(variables, "tag_size", jv_i(jv_tag_size(descriptor->number, jv_get_type(descriptor))));
	if (jv_is_reference_type(jv_get_java_type(descriptor)))
		lp_vars_set(variables, "null_check", "java.util.Objects.requireNonNull(value);");
	else
		lp_vars_set(variables, "null_check", "");
	lp_vars_set(variables, "deprecation", jv_field_deprecated(descriptor) ? "@java.lang.Deprecated " : "");
	int fixed_size = jv_fixed_size(jv_get_type(descriptor));
	if (fixed_size != -1)
		lp_vars_set(variables, "fixed_size", jv_i(fixed_size));
	lp_vars_set(variables, "on_changed", "onChanged();");

	if (jv_has_hasbit(descriptor)) {
		lp_vars_set(variables, "set_has_field_bit_to_local",
			    xcat(jv_generate_set_bit_to_local(message_bit_index), ";"));
		lp_vars_set(variables, "is_field_present_message", jv_generate_get_bit(message_bit_index));
		lp_vars_set(variables, "is_other_field_present_message",
			    xcat("other.has", lp_vars_get(variables, "capitalized_name"), "()"));
	} else {
		lp_vars_set(variables, "set_has_field_bit_to_local", "");
		JvCreateValueFn fn;
		switch (descriptor->type) {
		case PT_BYTES: fn = create_value_bytes; break;
		case PT_FLOAT: fn = create_value_float; break;
		case PT_DOUBLE: fn = create_value_double; break;
		default: fn = create_value_default; break;
		}
		add_primitive_variable_for_this_and_other("is_field_present_message", "is_other_field_present_message",
							  fn, variables);
	}

	lp_vars_set(variables, "get_has_field_bit_builder", jv_generate_get_bit(builder_bit_index));
	lp_vars_set(variables, "get_has_field_bit_from_local", jv_generate_get_bit_from_local(builder_bit_index));
	lp_vars_set(variables, "set_has_field_bit_builder", xcat(jv_generate_set_bit(builder_bit_index), ";"));
	lp_vars_set(variables, "clear_has_field_bit_builder", xcat(jv_generate_clear_bit(builder_bit_index), ";"));
}

static void ImmutablePrimitiveFieldGenerator_init(JvFieldGen *g)
{
	set_primitive_variables(g->descriptor, g->message_bit_index, g->builder_bit_index,
				jv_context_field_info(g->context, g->descriptor), &g->variables, g->context);
}

static void ImmutablePrimitiveOneofFieldGenerator_init(JvFieldGen *g)
{
	ImmutablePrimitiveFieldGenerator_init(g);
	const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, g->descriptor->containing_oneof);
	jv_set_common_oneof_variables(g->descriptor, info, &g->variables);
}

#define RepeatedImmutablePrimitiveFieldGenerator_init ImmutablePrimitiveFieldGenerator_init

static int ImmutablePrimitiveFieldGenerator_GetMessageBitIndex(const JvFieldGen *g) { return g->message_bit_index; }

static int ImmutablePrimitiveFieldGenerator_GetBuilderBitIndex(const JvFieldGen *g) { return g->builder_bit_index; }

static int ImmutablePrimitiveFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g)
{
	return jv_has_hasbit(g->descriptor) ? 1 : 0;
}

static int ImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void ImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables, "$deprecation$boolean has$capitalized_name$();\n");
	}
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ get$capitalized_name$();\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private $field_type$ $name$_ = $default$;\n");
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
}

static void ImmutablePrimitiveFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "private $field_type$ $name$_ $default_init$;\n");

	if (proto_field_has_presence(g->descriptor)) {
		jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_HAZZER, &g->context->options, false,
						    false, false);
		lp_print_vars(printer, &g->variables,
			      "@java.lang.Override\n"
			      "$deprecation$public boolean ${$has$capitalized_name$$}$() {\n"
			      "  return $get_has_field_bit_builder$;\n"
			      "}\n");
	}

	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "@java.lang.Override\n"
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  return $name$_;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_SETTER, &g->context->options, /* builder */ true,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$set$capitalized_name$$}$($type$ value) {\n"
		      "  $null_check$\n"
		      "  $name$_ = value;\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_CLEARER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$clear$capitalized_name$$}$() {\n"
		      "  $clear_has_field_bit_builder$\n");
	JvJavaType type = jv_get_java_type(g->descriptor);
	if (type == JAVATYPE_STRING || type == JAVATYPE_BYTES) {
		// The default value is not a simple literal so we want to avoid executing
		// it multiple times.  Instead, get the default out of the default instance.
		lp_print_vars(printer, &g->variables, "  $name$_ = getDefaultInstance().get$capitalized_name$();\n");
	} else {
		lp_print_vars(printer, &g->variables, "  $name$_ = $default$;\n");
	}
	lp_print_vars(printer, &g->variables,
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
										    LpPrinter *printer)
{
	// noop for primitives
}

static void ImmutablePrimitiveFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (!jv_is_default_value_java_default(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "$name$_ = $default$;\n");
	}
}

static void ImmutablePrimitiveFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No need to clear the has-bit since we clear the bitField ints all at once.
	lp_print_vars(printer, &g->variables, "$name$_ = $default$;\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_other_field_present_message$) {\n"
		      "  set$capitalized_name$(other.get$capitalized_name$());\n"
		      "}\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  result.$name$_ = $name$_;\n");
	if (g->ops->GetNumBitsForMessage(g) > 0) {
		lp_print_vars(printer, &g->variables, "  $set_has_field_bit_to_local$\n");
	}
	lp_print(printer, "}\n", NULL);
}

static void ImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$name$_ = input.read$capitalized_type$();\n"
		      "$set_has_field_bit_builder$\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  output.write$capitalized_type$($number$, $name$_);\n"
		      "}\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($is_field_present_message$) {\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "    .compute$capitalized_type$Size($number$, $name$_);\n"
		      "}\n");
}

static void ImmutablePrimitiveFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	switch (jv_get_java_type(g->descriptor)) {
	case JAVATYPE_INT:
	case JAVATYPE_LONG:
	case JAVATYPE_BOOLEAN:
		lp_print_vars(printer, &g->variables,
			      "if (get$capitalized_name$()\n"
			      "    != other.get$capitalized_name$()) return false;\n");
		break;

	case JAVATYPE_FLOAT:
		lp_print_vars(printer, &g->variables,
			      "if (java.lang.Float.floatToIntBits(get$capitalized_name$())\n"
			      "    != java.lang.Float.floatToIntBits(\n"
			      "        other.get$capitalized_name$())) return false;\n");
		break;

	case JAVATYPE_DOUBLE:
		lp_print_vars(printer, &g->variables,
			      "if (java.lang.Double.doubleToLongBits(get$capitalized_name$())\n"
			      "    != java.lang.Double.doubleToLongBits(\n"
			      "        other.get$capitalized_name$())) return false;\n");
		break;

	case JAVATYPE_STRING:
	case JAVATYPE_BYTES:
		lp_print_vars(printer, &g->variables,
			      "if (!get$capitalized_name$()\n"
			      "    .equals(other.get$capitalized_name$())) return false;\n");
		break;

	case JAVATYPE_ENUM:
	case JAVATYPE_MESSAGE:
	default: fail("Can't get here."); break;
	}
}

static void ImmutablePrimitiveFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "hash = (37 * hash) + $constant_name$;\n");
	switch (jv_get_java_type(g->descriptor)) {
	case JAVATYPE_INT:
		lp_print_vars(printer, &g->variables, "hash = (53 * hash) + get$capitalized_name$();\n");
		break;

	case JAVATYPE_LONG:
		lp_print_vars(printer, &g->variables,
			      "hash = (53 * hash) + com.google.protobuf.Internal.hashLong(\n"
			      "    get$capitalized_name$());\n");
		break;

	case JAVATYPE_BOOLEAN:
		lp_print_vars(printer, &g->variables,
			      "hash = (53 * hash) + com.google.protobuf.Internal.hashBoolean(\n"
			      "    get$capitalized_name$());\n");
		break;

	case JAVATYPE_FLOAT:
		lp_print_vars(printer, &g->variables,
			      "hash = (53 * hash) + java.lang.Float.floatToIntBits(\n"
			      "    get$capitalized_name$());\n");
		break;

	case JAVATYPE_DOUBLE:
		lp_print_vars(printer, &g->variables,
			      "hash = (53 * hash) + com.google.protobuf.Internal.hashLong(\n"
			      "    java.lang.Double.doubleToLongBits(get$capitalized_name$()));\n");
		break;

	case JAVATYPE_STRING:
	case JAVATYPE_BYTES:
		lp_print_vars(printer, &g->variables, "hash = (53 * hash) + get$capitalized_name$().hashCode();\n");
		break;

	case JAVATYPE_ENUM:
	case JAVATYPE_MESSAGE:
	default: fail("Can't get here."); break;
	}
}

static const char *ImmutablePrimitiveFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return (jv_boxed_primitive_type_name(jv_get_java_type(g->descriptor)));
}

// ===================================================================

static void ImmutablePrimitiveOneofFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
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
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$() {\n"
		      "  if ($has_oneof_case_message$) {\n"
		      "    return ($boxed_type$) $oneof_name$_;\n"
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
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	// No-Op: When a primitive field is in a oneof, clearing the oneof clears that
	// field.
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// no-op
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "set$capitalized_name$(other.get$capitalized_name$());\n");
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$oneof_name$_ = input.read$capitalized_type$();\n"
		      "$set_oneof_case_message$;\n");
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  output.write$capitalized_type$(\n");
	// $type$ and $boxed_type$ is the same for bytes fields so we don't need to
	// do redundant casts.
	if (jv_get_java_type(g->descriptor) == JAVATYPE_BYTES) {
		lp_print_vars(printer, &g->variables, "      $number$, ($type$) $oneof_name$_);\n");
	} else {
		lp_print_vars(printer, &g->variables, "      $number$, ($type$)(($boxed_type$) $oneof_name$_));\n");
	}
	lp_print(printer, "}\n", NULL);
}

static void ImmutablePrimitiveOneofFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if ($has_oneof_case_message$) {\n"
		      "  size += com.google.protobuf.CodedOutputStream\n"
		      "    .compute$capitalized_type$Size(\n");
	// $type$ and $boxed_type$ is the same for bytes fields so we don't need to
	// do redundant casts.
	if (jv_get_java_type(g->descriptor) == JAVATYPE_BYTES) {
		lp_print_vars(printer, &g->variables, "        $number$, ($type$) $oneof_name$_);\n");
	} else {
		lp_print_vars(printer, &g->variables, "        $number$, ($type$)(($boxed_type$) $oneof_name$_));\n");
	}
	lp_print(printer, "}\n", NULL);
}

// ===================================================================

static int RepeatedImmutablePrimitiveFieldGenerator_GetNumBitsForMessage(const JvFieldGen *g) { return 0; }

static int RepeatedImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder(const JvFieldGen *g) { return 1; }

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers(const JvFieldGen *g, LpPrinter *printer)
{
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$java.util.List<$boxed_type$> "
		      "get$capitalized_name$List();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_COUNT, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables, "$deprecation$int get$capitalized_name$Count();\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables, "$deprecation$$type$ get$capitalized_name$(int index);\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateMembers(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "@SuppressWarnings(\"serial\")\n"
		      "private $field_list_type$ $name$_ =\n"
		      "    $empty_list$;\n");
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
		      "$deprecation$public int ${$get$capitalized_name$Count$}$() {\n"
		      "  return $name$_.size();\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_GETTER, &g->context->options, false,
					    false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $repeated_get$(index);\n"
		      "}\n");
	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "private int $name$MemoizedSerializedSize = -1;\n");
	}
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderMembers(const JvFieldGen *g, LpPrinter *printer)
{
	// We use a ProtobufArrayList because it starts as a mutable list that can be
	// switched to immutable when references are handed out. This allows copy-free
	// sharing. A bit in the bitfield tracks whether there are any items in the
	// list. The presence bit allows us to skip work on blocks of 32 fields by
	// by checking if the entire bit-field int == 0 (none of the fields are
	// present).
	lp_print_vars(printer, &g->variables,
		      "private $field_list_type$ $name$_ = $empty_list$;\n"
		      "private void ensure$capitalized_name$IsMutable() {\n"
		      "  if (!$name$_.isModifiable()) {\n"
		      "    $name$_ = makeMutableCopy($name$_);\n"
		      "  }\n"
		      "  $set_has_field_bit_builder$\n"
		      "}\n");
	if (jv_fixed_size(jv_get_type(g->descriptor)) != -1) {
		lp_print_vars(printer, &g->variables,
			      "private void ensure$capitalized_name$IsMutable(int capacity) {\n"
			      "  if (!$name$_.isModifiable()) {\n"
			      "    $name$_ = makeMutableCopy($name$_, capacity);\n"
			      "  }\n"
			      "  $set_has_field_bit_builder$\n"
			      "}\n");
	}

	// Note:  We return an unmodifiable list because otherwise the caller
	//   could hold on to the returned list and modify it after the message
	//   has been built, thus mutating the message which is supposed to be
	//   immutable.
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_GETTER, &g->context->options, false, false,
					    false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public java.util.List<$boxed_type$>\n"
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
		      "$deprecation$public $type$ ${$get$capitalized_name$$}$(int index) {\n"
		      "  return $repeated_get$(index);\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_INDEXED_SETTER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$set$capitalized_name$$}$(\n"
		      "    int index, $type$ value) {\n"
		      "  $null_check$\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $repeated_set$(index, value);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder "
		      "${$add$capitalized_name$$}$($type$ value) {\n"
		      "  $null_check$\n"
		      "  ensure$capitalized_name$IsMutable();\n"
		      "  $repeated_add$(value);\n"
		      "  $set_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
	jv_write_field_accessor_doc_comment(printer, g->descriptor, JV_LIST_MULTI_ADDER, &g->context->options,
					    /* builder */ true, false, false);
	lp_print_vars(printer, &g->variables,
		      "$deprecation$public Builder ${$addAll$capitalized_name$$}$(\n"
		      "    java.lang.Iterable<? extends $boxed_type$> values) {\n"
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
		      "  $name$_ = $empty_list$;\n"
		      "  $clear_has_field_bit_builder$\n"
		      "  $on_changed$\n"
		      "  return this;\n"
		      "}\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode(const JvFieldGen *g,
											    LpPrinter *printer)
{
	// noop for primitives
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateInitializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $empty_list$;\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderClearCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables, "$name$_ = $empty_list$;\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateMergingCode(const JvFieldGen *g, LpPrinter *printer)
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
		      "    $set_has_field_bit_builder$\n");
	lp_print_vars(printer, &g->variables,
		      "  } else {\n"
		      "    ensure$capitalized_name$IsMutable();\n"
		      "    $name$_.addAll(other.$name$_);\n"
		      "  }\n"
		      "  $on_changed$\n"
		      "}\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuildingCode(const JvFieldGen *g, LpPrinter *printer)
{
	// The code below ensures that the result has an immutable list. If our
	// list is immutable, we can just reuse it. If not, we make it immutable.
	lp_print_vars(printer, &g->variables,
		      "if ($get_has_field_bit_from_local$) {\n"
		      "  $name_make_immutable$;\n"
		      "  result.$name$_ = $name$_;\n"
		      "}\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "$type$ v = input.read$capitalized_type$();\n"
		      "ensure$capitalized_name$IsMutable();\n"
		      "$repeated_add$(v);\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCodeFromPacked(const JvFieldGen *g,
											  LpPrinter *printer)
{
	if (jv_fixed_size(jv_get_type(g->descriptor)) != -1) {
		// 4K limit on pre-allocations to prevent OOM from malformed input.
		lp_print_vars(printer, &g->variables,
			      "int length = input.readRawVarint32();\n"
			      "int limit = input.pushLimit(length);\n"
			      "int alloc = length > 4096 ? 4096 : length;\n"
			      "ensure$capitalized_name$IsMutable(alloc / $fixed_size$);\n"
			      "while (input.getBytesUntilLimit() > 0) {\n"
			      "  $repeated_add$(input.read$capitalized_type$());\n"
			      "}\n"
			      "input.popLimit(limit);\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "int length = input.readRawVarint32();\n"
			      "int limit = input.pushLimit(length);\n"
			      "ensure$capitalized_name$IsMutable();\n"
			      "while (input.getBytesUntilLimit() > 0) {\n"
			      "  $repeated_add$(input.read$capitalized_type$());\n"
			      "}\n"
			      "input.popLimit(limit);\n");
	}
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateSerializationCode(const JvFieldGen *g, LpPrinter *printer)
{
	if (proto_field_is_packed(g->descriptor)) {
		// We invoke getSerializedSize in writeTo for messages that have packed
		// fields in ImmutableMessageGenerator::GenerateMessageSerializationMethods.
		// That makes it safe to rely on the memoized size here.
		lp_print_vars(printer, &g->variables,
			      "if (get$capitalized_name$List().size() > 0) {\n"
			      "  output.writeUInt32NoTag($tag$);\n"
			      "  output.writeUInt32NoTag($name$MemoizedSerializedSize);\n"
			      "}\n"
			      "for (int i = 0; i < $name$_.size(); i++) {\n"
			      "  output.write$capitalized_type$NoTag($repeated_get$(i));\n"
			      "}\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "for (int i = 0; i < $name$_.size(); i++) {\n"
			      "  output.write$capitalized_type$($number$, $repeated_get$(i));\n"
			      "}\n");
	}
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateSerializedSizeCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "{\n"
		      "  int dataSize = 0;\n");
	lp_indent(printer);

	if (jv_fixed_size(jv_get_type(g->descriptor)) == -1) {
		lp_print_vars(printer, &g->variables,
			      "for (int i = 0; i < $name$_.size(); i++) {\n"
			      "  dataSize += com.google.protobuf.CodedOutputStream\n"
			      "    .compute$capitalized_type$SizeNoTag($repeated_get$(i));\n"
			      "}\n");
	} else {
		lp_print_vars(printer, &g->variables,
			      "dataSize = $fixed_size$ * get$capitalized_name$List().size();\n");
	}

	lp_print(printer, "size += dataSize;\n", NULL);

	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables,
			      "if (!get$capitalized_name$List().isEmpty()) {\n"
			      "  size += $tag_size$;\n"
			      "  size += com.google.protobuf.CodedOutputStream\n"
			      "      .computeInt32SizeNoTag(dataSize);\n"
			      "}\n");
	} else {
		lp_print_vars(printer, &g->variables, "size += $tag_size$ * get$capitalized_name$List().size();\n");
	}

	// cache the data size for packed fields.
	if (proto_field_is_packed(g->descriptor)) {
		lp_print_vars(printer, &g->variables, "$name$MemoizedSerializedSize = dataSize;\n");
	}

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateEqualsCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (!get$capitalized_name$List()\n"
		      "    .equals(other.get$capitalized_name$List())) return false;\n");
}

static void RepeatedImmutablePrimitiveFieldGenerator_GenerateHashCode(const JvFieldGen *g, LpPrinter *printer)
{
	lp_print_vars(printer, &g->variables,
		      "if (get$capitalized_name$Count() > 0) {\n"
		      "  hash = (37 * hash) + $constant_name$;\n"
		      "  hash = (53 * hash) + get$capitalized_name$List().hashCode();\n"
		      "}\n");
}

static const char *RepeatedImmutablePrimitiveFieldGenerator_GetBoxedType(const JvFieldGen *g)
{
	return (jv_boxed_primitive_type_name(jv_get_java_type(g->descriptor)));
}

static const JvFieldGenOps ImmutablePrimitiveFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutablePrimitiveFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutablePrimitiveFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutablePrimitiveFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutablePrimitiveFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutablePrimitiveFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutablePrimitiveFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutablePrimitiveFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutablePrimitiveFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutablePrimitiveFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutablePrimitiveFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode =
		ImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutablePrimitiveFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutablePrimitiveFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutablePrimitiveFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutablePrimitiveFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps ImmutablePrimitiveOneofFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutablePrimitiveFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutablePrimitiveFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = ImmutablePrimitiveFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = ImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = ImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = ImmutablePrimitiveOneofFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = ImmutablePrimitiveFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = ImmutablePrimitiveOneofFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = ImmutablePrimitiveOneofFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = ImmutablePrimitiveOneofFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = ImmutablePrimitiveOneofFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode =
		ImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateEqualsCode = ImmutablePrimitiveFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = ImmutablePrimitiveFieldGenerator_GenerateHashCode,
	.GetBoxedType = ImmutablePrimitiveFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = ImmutablePrimitiveOneofFieldGenerator_GenerateSerializationCode,
};
static const JvFieldGenOps RepeatedImmutablePrimitiveFieldGenerator_ops = {
	.GetMessageBitIndex = ImmutablePrimitiveFieldGenerator_GetMessageBitIndex,
	.GetBuilderBitIndex = ImmutablePrimitiveFieldGenerator_GetBuilderBitIndex,
	.GetNumBitsForMessage = RepeatedImmutablePrimitiveFieldGenerator_GetNumBitsForMessage,
	.GetNumBitsForBuilder = RepeatedImmutablePrimitiveFieldGenerator_GetNumBitsForBuilder,
	.GenerateInterfaceMembers = RepeatedImmutablePrimitiveFieldGenerator_GenerateInterfaceMembers,
	.GenerateMembers = RepeatedImmutablePrimitiveFieldGenerator_GenerateMembers,
	.GenerateBuilderMembers = RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderMembers,
	.GenerateInitializationCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateInitializationCode,
	.GenerateBuilderClearCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderClearCode,
	.GenerateMergingCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateMergingCode,
	.GenerateBuildingCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateBuildingCode,
	.GenerateBuilderParsingCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCode,
	.GenerateSerializedSizeCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateSerializedSizeCode,
	.GenerateFieldBuilderInitializationCode =
		RepeatedImmutablePrimitiveFieldGenerator_GenerateFieldBuilderInitializationCode,
	.GenerateBuilderParsingCodeFromPacked =
		RepeatedImmutablePrimitiveFieldGenerator_GenerateBuilderParsingCodeFromPacked,
	.GenerateEqualsCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateEqualsCode,
	.GenerateHashCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateHashCode,
	.GetBoxedType = RepeatedImmutablePrimitiveFieldGenerator_GetBoxedType,
	.GenerateSerializationCode = RepeatedImmutablePrimitiveFieldGenerator_GenerateSerializationCode,
};
