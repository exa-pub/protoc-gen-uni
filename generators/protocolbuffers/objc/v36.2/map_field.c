// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/map_field.{h,cc} (protobuf v36.2):
 * MapFieldGenerator, a RepeatedFieldGenerator (no has* methods, comments for the contained
 * type) with the generator of the map's value field. */

static void map_field_init(FieldGenerator *g)
{
	const LpMessage *entry = g->descriptor->message_type;
	const LpField *key_descriptor = proto_message_field_by_number(entry, 1);
	const LpField *value_descriptor = proto_message_field_by_number(entry, 2);
	FieldGenerator *value = field_generator_make(value_descriptor, g->generation_options);
	g->value_field_generator = value;

	/* some variables of the value */
	subst_set(&g->variables, "field_type", field_variable(value, "field_type"));
	subst_set(&g->variables, "default", field_variable(value, "default"));
	subst_set(&g->variables, "default_name", field_variable(value, "default_name"));

	/* custom field flags */
	const char *field_flags[3];
	int n = 0;
	field_flags[n++] = xcat("GPBFieldMapKey", get_capitalized_type(key_descriptor));
	/* the text format custom name computed for the map field */
	if (strstr(field_variable(g, "fieldflags"), "GPBFieldTextFormatNameCustom"))
		field_flags[n++] = "GPBFieldTextFormatNameCustom";
	/* from the value's flags */
	if (strstr(field_variable(value, "fieldflags"), "GPBFieldHasDefaultValue"))
		field_flags[n++] = "GPBFieldHasDefaultValue";
	subst_set(&g->variables, "fieldflags", build_flags_string(FLAGTYPE_FIELD, field_flags, n));

	subst_set(&g->variables, "dataTypeSpecific_name", field_variable(value, "dataTypeSpecific_name"));
	subst_set(&g->variables, "dataTypeSpecific_value", field_variable(value, "dataTypeSpecific_value"));
}

/* MapFieldGenerator::EmitArrayComment: what the values of the map are. */
static void map_field_emit_array_comment(const FieldGenerator *g, LpPrinter *p)
{
	const LpField *value_descriptor = proto_message_field_by_number(g->descriptor->message_type, 2);
	if (get_objective_c_type(value_descriptor) == OBJECTIVECTYPE_ENUM) {
		LpSub subs[] = {
			LP_SUB("name", field_variable(g, "name")),
			LP_SUB("enum_name", field_variable(g->value_field_generator, "enum_name")),
		};
		lp_emit(p, subs, 2,
			"\n"
			" // |$name$| values are |$enum_name$|\n");
	}
}

/* RepeatedFieldGenerator::EmitArrayComment and its overrides */
static void field_emit_array_comment(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	if (g->kind == FIELD_REPEATED_ENUM)
		repeated_enum_field_emit_array_comment(g, p);
	else if (g->kind == FIELD_MAP)
		map_field_emit_array_comment(g, p);
}

static void map_field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
						     bool include_external_types)
{
	/* enum values need no forward declaration: GPB*EnumDictionary is not generic */
	const LpField *value_descriptor = proto_message_field_by_number(g->descriptor->message_type, 2);
	if (get_objective_c_type(value_descriptor) != OBJECTIVECTYPE_MESSAGE)
		return;
	const LpMessage *value_msg_descriptor = value_descriptor->message_type;
	if ((include_external_types && !lp_objc_is_protobuf_library_bundled_proto_file(value_msg_descriptor->file)) ||
	    g->descriptor->file == value_msg_descriptor->file)
		string_set_insert(fwd_decls, xcat("@class ", field_variable(g->value_field_generator, "msg_type"), ";"));
}

static void map_field_determine_objective_c_class_definitions(const FieldGenerator *g, StringSet *fwd_decls)
{
	const LpField *value_descriptor = proto_message_field_by_number(g->descriptor->message_type, 2);
	if (get_objective_c_type(value_descriptor) == OBJECTIVECTYPE_MESSAGE)
		string_set_insert(fwd_decls, objc_class_declaration(field_variable(g->value_field_generator, "msg_type")));
}

static void map_field_determine_needed_files(const FieldGenerator *g, FileSet *deps)
{
	const LpField *value_descriptor = proto_message_field_by_number(g->descriptor->message_type, 2);
	ObjectiveCType value_objc_type = get_objective_c_type(value_descriptor);
	if (value_objc_type == OBJECTIVECTYPE_MESSAGE) {
		if (g->descriptor->file != value_descriptor->message_type->file)
			file_set_insert(deps, value_descriptor->message_type->file);
	} else if (value_objc_type == OBJECTIVECTYPE_ENUM) {
		if (g->descriptor->file != value_descriptor->enum_type->file)
			file_set_insert(deps, value_descriptor->enum_type->file);
	}
}
