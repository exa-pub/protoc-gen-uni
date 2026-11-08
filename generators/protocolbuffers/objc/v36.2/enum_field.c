// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/enum_field.{h,cc} (protobuf v36.2):
 * EnumFieldGenerator and RepeatedEnumFieldGenerator. */

/* SetEnumVariables */
static void enum_field_init(FieldGenerator *g)
{
	const LpField *descriptor = g->descriptor;
	SubstitutionMap *variables = &g->variables;
	char *type = lp_objc_enum_name(descriptor->enum_type);
	char *enum_desc_func = xcat(type, "_EnumDescriptor");
	subst_set(variables, "enum_name", type);
	/* With forward declarations, a singular field of an enum of another file is declared as
	 * "enum NAME" to support the forward declaration of the enum. */
	if (g->generation_options->headers_use_forward_declarations && !proto_field_is_repeated(descriptor) &&
	    !lp_objc_is_protobuf_library_bundled_proto_file(descriptor->enum_type->file) &&
	    descriptor->file != descriptor->enum_type->file)
		subst_set(variables, "property_type", xcat("enum ", type, " "));
	subst_set(variables, "enum_verifier", xcat(type, "_IsValidValue"));
	subst_set(variables, "enum_desc_func", enum_desc_func);

	subst_set(variables, "dataTypeSpecific_name", "enumDescFunc");
	subst_set(variables, "dataTypeSpecific_value", enum_desc_func);

	subst_set(variables, "owning_message_class", lp_objc_class_name(descriptor->containing_type));
}

static void enum_field_generate_c_function_declarations(const FieldGenerator *g, LpPrinter *p)
{
	if (proto_enum_is_closed(g->descriptor->enum_type))
		return;
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0,
		"\n"
		" /**\n"
		"  * Fetches the raw value of a @c $owning_message_class$'s @c $name$ property, even\n"
		"  * if the value was not defined by the enum at the time the code was generated.\n"
		"  **/\n"
		" int32_t $owning_message_class$_$capitalized_name$_RawValue($owning_message_class$ *message);\n"
		" /**\n"
		"  * Sets the raw value of an @c $owning_message_class$'s @c $name$ property, allowing\n"
		"  * it to be set to a value that was not defined by the enum at the time the code\n"
		"  * was generated.\n"
		"  **/\n"
		" void Set$owning_message_class$_$capitalized_name$_RawValue($owning_message_class$ *message, int32_t value);\n");
	lp_emit(p, NULL, 0, "\n");
	lp_pop_vars(p);
}

static void enum_field_generate_c_function_implementations(const FieldGenerator *g, LpPrinter *p)
{
	if (proto_enum_is_closed(g->descriptor->enum_type))
		return;
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0,
		"\n"
		" int32_t $owning_message_class$_$capitalized_name$_RawValue($owning_message_class$ *message) {\n"
		"   GPBDescriptor *descriptor = [$owning_message_class$ descriptor];\n"
		"   GPBFieldDescriptor *field = [descriptor fieldWithNumber:$field_number_name$];\n"
		"   return GPBGetMessageRawEnumField(message, field);\n"
		" }\n"
		"\n"
		" void Set$owning_message_class$_$capitalized_name$_RawValue($owning_message_class$ *message, int32_t value) {\n"
		"   GPBDescriptor *descriptor = [$owning_message_class$ descriptor];\n"
		"   GPBFieldDescriptor *field = [descriptor fieldWithNumber:$field_number_name$];\n"
		"   GPBSetMessageRawEnumField(message, field, value);\n"
		" }\n");
	lp_emit(p, NULL, 0, "\n");
	lp_pop_vars(p);
}

static void enum_field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
						      bool include_external_types)
{
	/* An enum of another file (not a WKT) needs a forward declaration; the enums of this file
	 * are output before the messages. */
	const LpField *d = g->descriptor;
	if (include_external_types && d->file != d->enum_type->file &&
	    !lp_objc_is_protobuf_library_bundled_proto_file(d->enum_type->file))
		string_set_insert(fwd_decls, xcat("GPB_ENUM_FWD_DECLARE(", field_variable(g, "enum_name"), ");"));
}

/* RepeatedEnumFieldGenerator::EmitArrayComment (FIELD_REPEATED_ENUM) */
static void repeated_enum_field_emit_array_comment(const FieldGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0,
		"\n"
		" // |$name$| contains |$enum_name$|\n");
	lp_pop_vars(p);
}
