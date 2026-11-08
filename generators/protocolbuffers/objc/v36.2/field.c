// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/field.{h,cc} (protobuf v36.2): FieldGenerator,
 * SingleFieldGenerator, ObjCObjFieldGenerator, RepeatedFieldGenerator and FieldGeneratorMap. The
 * subclasses (primitive_field.c, enum_field.c, message_field.c, map_field.c) are a FieldKind; the
 * virtual methods dispatch on it. */

static FieldGenerator *field_generator_make(const LpField *field, const GenerationOptions *generation_options);

static bool field_is_repeated_kind(const FieldGenerator *g) { return g->kind >= FIELD_REPEATED_PRIMITIVE; }

/* ObjCObjFieldGenerator and its subclasses (RepeatedFieldGenerator derives from it) */
static bool field_is_objc_obj_kind(const FieldGenerator *g) { return g->kind >= FIELD_PRIMITIVE_OBJ; }

static const char *field_variable(const FieldGenerator *g, const char *key) { return subst_value(&g->variables, key); }

static void set_common_field_variables(const LpField *descriptor, SubstitutionMap *variables)
{
	char *camel_case_name = lp_objc_field_name(descriptor);
	const char *raw_field_name = lp_field_is_group_like(descriptor) ? descriptor->message_type->name : descriptor->name;
	/* the logic here has to match -[GGPBFieldDescriptor textFormatName] */
	char *un_camel_case_name = lp_objc_un_camel_case_field_name(camel_case_name, descriptor);
	bool needs_custom_name = !str_eq(raw_field_name, un_camel_case_name);

	char *classname = lp_objc_class_name(descriptor->containing_type);
	subst_set_sub(variables, LP_SUB_ANN("classname", classname, descriptor));
	subst_set_sub(variables, LP_SUB_ANN("name", camel_case_name, descriptor));

	char *capitalized_name = lp_objc_field_name_capitalized(descriptor);
	subst_set_sub(variables, LP_SUB_ANN("hazzer_name", xcat("has", capitalized_name), descriptor));
	subst_set_sub(variables, LP_SUB_ANN("capitalized_name", capitalized_name, descriptor));
	subst_set(variables, "raw_field_name", raw_field_name);
	subst_set_sub(variables,
		      LP_SUB_ANN("field_number_name", xcat(classname, "_FieldNumber_", capitalized_name), descriptor));
	subst_set(variables, "field_number", objc_itoa(descriptor->number));
	subst_set(variables, "property_type",
		  lp_objc_field_objc_type(descriptor, LP_OBJC_TYPE_INCLUDE_SPACE_AFTER_BASIC_TYPES |
								LP_OBJC_TYPE_INCLUDE_SPACE_BEFORE_STAR));
	subst_set(variables, "storage_type",
		  lp_objc_field_objc_type(descriptor, LP_OBJC_TYPE_INCLUDE_SPACE_AFTER_BASIC_TYPES |
								LP_OBJC_TYPE_OMIT_LIGHTWEIGHT_GENERICS |
								LP_OBJC_TYPE_INCLUDE_SPACE_BEFORE_STAR));
	subst_set(variables, "field_type", get_capitalized_type(descriptor));
	subst_set(variables, "deprecated_attribute", get_optional_deprecated_attribute(descriptor, NULL));
	const char *field_flags[8];
	int n = 0;
	if (proto_field_is_repeated(descriptor))
		field_flags[n++] = "GPBFieldRepeated";
	if (proto_field_is_required(descriptor))
		field_flags[n++] = "GPBFieldRequired";
	if (proto_field_is_packed(descriptor))
		field_flags[n++] = "GPBFieldPacked";
	/* ObjC custom flags */
	if (descriptor->has_default_value)
		field_flags[n++] = "GPBFieldHasDefaultValue";
	if (needs_custom_name)
		field_flags[n++] = "GPBFieldTextFormatNameCustom";
	/* clears on a zero value if not repeated/map and without presence */
	if (!proto_field_is_repeated(descriptor) && !proto_field_has_presence(descriptor))
		field_flags[n++] = "GPBFieldClearHasIvarOnZero";
	subst_set(variables, "fieldflags", build_flags_string(FLAGTYPE_FIELD, field_flags, n));

	subst_set(variables, "default", default_value(descriptor));
	subst_set(variables, "default_name", gpb_generic_value_field_name(descriptor));

	subst_set(variables, "dataTypeSpecific_name", "clazz");
	subst_set(variables, "dataTypeSpecific_value", "Nil");

	subst_set(variables, "storage_offset_value",
		  xcat("(uint32_t)offsetof(", classname, "__storage_, ", camel_case_name, ")"));
	subst_set(variables, "storage_offset_comment", "");

	/* cleared so they can be set just when needed */
	subst_set(variables, "storage_attribute", "");
}

/* HasNonZeroDefaultValue */
static bool has_non_zero_default_value(const LpField *field)
{
	if (proto_field_is_repeated(field))
		return false;
	/* some proto files set the default to the zero value: check the value itself */
	switch (lp_field_cpp_type(field)) {
	case LP_CPPTYPE_INT32: return (int32_t)field->default_value.i != 0;
	case LP_CPPTYPE_UINT32: return (uint32_t)field->default_value.u != 0;
	case LP_CPPTYPE_INT64: return field->default_value.i != 0;
	case LP_CPPTYPE_UINT64: return field->default_value.u != 0;
	case LP_CPPTYPE_DOUBLE: return field->default_value.d != 0.0;
	case LP_CPPTYPE_FLOAT: return field->default_value.f != 0.0f;
	case LP_CPPTYPE_BOOL: return field->default_value.b;
	case LP_CPPTYPE_STRING: return field->default_value_string.n != 0;
	case LP_CPPTYPE_ENUM:
		/* the default of an enum field is its first value */
		return field->default_value_enum && field->default_value_enum->number != 0;
	case LP_CPPTYPE_MESSAGE: return false;
	}
	fail("Can't get here.");
}

static FieldGenerator *field_generator_new(FieldKind kind, const LpField *descriptor,
					   const GenerationOptions *generation_options)
{
	FieldGenerator *g = xalloc(sizeof *g);
	g->kind = kind;
	g->descriptor = descriptor;
	g->generation_options = generation_options;
	set_common_field_variables(descriptor, &g->variables);
	/* ObjCObjFieldGenerator */
	if (field_is_objc_obj_kind(g)) {
		subst_set(&g->variables, "property_storage_attribute", "strong");
		if (lp_objc_is_retained_name(field_variable(g, "name")))
			subst_set(&g->variables, "storage_attribute", " NS_RETURNS_NOT_RETAINED");
	}
	return g;
}

static void field_generate_field_number_constant(const FieldGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0, "$field_number_name$ = $field_number$,\n");
	lp_pop_vars(p);
}

typedef struct {
	const FieldGenerator *g;
	bool include_default;
} FieldDescriptionArgs;

static void field_maybe_default(LpPrinter *p, void *arg)
{
	const FieldDescriptionArgs *a = arg;
	if (a->include_default)
		lp_emit(p, NULL, 0, ".defaultValue.$default_name$ = $default$,\n");
}

static void field_generate_field_description(const FieldGenerator *g, LpPrinter *p, bool include_default)
{
	/* printed in the same order as the structure decl */
	subst_install(&g->variables, p);
	FieldDescriptionArgs a = {g, include_default};
	LpSub subs[] = {
		LP_SUB("prefix", include_default ? ".core" : ""),
		LP_SUB_CB("maybe_default", field_maybe_default, &a),
	};
	lp_emit(p, subs, 2,
		"\n"
		" {\n"
		"   $maybe_default$,\n"
		"   $prefix$.name = \"$name$\",\n"
		"   $prefix$.dataTypeSpecific.$dataTypeSpecific_name$ = $dataTypeSpecific_value$,\n"
		"   $prefix$.number = $field_number_name$,\n"
		"   $prefix$.hasIndex = $has_index$,\n"
		"   $prefix$.offset = $storage_offset_value$,$storage_offset_comment$\n"
		"   $prefix$.flags = $fieldflags$,\n"
		"   $prefix$.dataType = GPBDataType$field_type$,\n"
		" },\n");
	lp_pop_vars(p);
}

static void field_set_runtime_has_bit(FieldGenerator *g, int has_index)
{
	subst_set(&g->variables, "has_index", objc_itoa(has_index));
}

static void field_set_no_has_bit(FieldGenerator *g) { subst_set(&g->variables, "has_index", "GPBNoHasBit"); }

static void field_set_oneof_index_base(FieldGenerator *g, int index_base)
{
	const LpOneof *oneof = proto_field_real_oneof(g->descriptor);
	if (oneof) {
		int index = oneof->index + index_base;
		/* the sign flipped marks a oneof */
		subst_set(&g->variables, "has_index", objc_itoa(-index));
	}
}

static bool field_wants_has_property(const FieldGenerator *g) { return proto_field_has_presence(g->descriptor); }

static void field_comments(LpPrinter *p, void *arg)
{
	const FieldGenerator *g = arg;
	emit_comments_string(p, g->generation_options, g->descriptor, 0);
}

/* RepeatedFieldGenerator::EmitArrayComment (overridden by enums and maps) */
static void field_emit_array_comment(LpPrinter *p, void *arg);

/* GenerateFieldStorageDeclaration */
static void field_generate_field_storage_declaration(const FieldGenerator *g, LpPrinter *p)
{
	/* PrimitiveFieldGenerator: BOOLs are stored in the has bits */
	if (g->kind == FIELD_PRIMITIVE && get_objective_c_type(g->descriptor) == OBJECTIVECTYPE_BOOLEAN)
		return;
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0, "$storage_type$$name$;\n");
	lp_pop_vars(p);
}

/* GeneratePropertyDeclaration */
static void field_generate_property_declaration(const FieldGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	if (field_is_repeated_kind(g)) {
		/* RepeatedFieldGenerator: no has* property, but *Count (to check without autocreation) */
		LpSub subs[] = {
			LP_SUB_CB("comments", field_comments, (void *)g),
			LP_SUB_CB("array_comment", field_emit_array_comment, (void *)g),
		};
		lp_emit(p, subs, 2,
			"\n"
			" $comments$\n"
			" $array_comment$\n"
			" @property(nonatomic, readwrite, strong, null_resettable) $property_type$$name$$storage_attribute$$ deprecated_attribute$;\n"
			" /** The number of items in @c $name$ without causing the container to be created. */\n"
			" @property(nonatomic, readonly) NSUInteger $name$_Count$ deprecated_attribute$;\n");
		if (lp_objc_is_init_name(field_variable(g, "name"))) {
			/* a property name starting with init needs an annotation to get past ARC */
			lp_emit(p, NULL, 0,
				"\n"
				" - ($property_type$)$name$ GPB_METHOD_FAMILY_NONE$ deprecated_attribute$;\n");
		}
	} else if (field_is_objc_obj_kind(g)) {
		/* ObjCObjFieldGenerator: pointers, and the storage name conventions (init*, new*, ...) */
		LpSub subs[] = {LP_SUB_CB("comments", field_comments, (void *)g)};
		lp_emit(p, subs, 1,
			"\n"
			" $comments$\n"
			" @property(nonatomic, readwrite, $property_storage_attribute$, null_resettable) $property_type$$name$$storage_attribute$$ deprecated_attribute$;\n");
		if (field_wants_has_property(g)) {
			lp_emit(p, NULL, 0,
				"\n"
				" /** Test to see if @c $name$ has been set. */\n"
				" @property(nonatomic, readwrite) BOOL $hazzer_name$$ deprecated_attribute$;\n");
		}
		if (lp_objc_is_init_name(field_variable(g, "name"))) {
			lp_emit(p, NULL, 0,
				"\n"
				" - ($property_type$)$name$ GPB_METHOD_FAMILY_NONE$ deprecated_attribute$;\n");
		}
	} else {
		/* SingleFieldGenerator */
		LpSub subs[] = {LP_SUB_CB("comments", field_comments, (void *)g)};
		lp_emit(p, subs, 1,
			"\n"
			" $comments$\n"
			" @property(nonatomic, readwrite) $property_type$$name$$ deprecated_attribute$;\n");
		if (field_wants_has_property(g)) {
			lp_emit(p, NULL, 0,
				"\n"
				" @property(nonatomic, readwrite) BOOL $hazzer_name$$ deprecated_attribute$;\n");
		}
	}
	lp_emit(p, NULL, 0, "\n");
	lp_pop_vars(p);
}

/* GeneratePropertyImplementation */
static void field_generate_property_implementation(const FieldGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	if (field_is_repeated_kind(g))
		lp_emit(p, NULL, 0, "@dynamic $name$, $name$_Count;\n");
	else if (field_wants_has_property(g))
		lp_emit(p, NULL, 0, "@dynamic $hazzer_name$, $name$;\n");
	else
		lp_emit(p, NULL, 0, "@dynamic $name$;\n");
	lp_pop_vars(p);
}

/* RuntimeUsesHasBit */
static bool field_runtime_uses_has_bit(const FieldGenerator *g)
{
	/* repeated: the array (or map/dict) having anything is what is used */
	if (field_is_repeated_kind(g))
		return false;
	/* in a oneof, the oneof tracks what is set instead */
	return proto_field_real_oneof(g->descriptor) == NULL;
}

/* ExtraRuntimeHasBitsNeeded and SetExtraRuntimeHasBitsBase (PrimitiveFieldGenerator) */
static int field_extra_runtime_has_bits_needed(const FieldGenerator *g)
{
	return g->kind == FIELD_PRIMITIVE && get_objective_c_type(g->descriptor) == OBJECTIVECTYPE_BOOLEAN ? 1 : 0;
}

static void field_set_extra_runtime_has_bits_base(FieldGenerator *g, int index_base)
{
	if (g->kind != FIELD_PRIMITIVE)
		fail("Error: should have overridden SetExtraRuntimeHasBitsBase().");
	if (get_objective_c_type(g->descriptor) == OBJECTIVECTYPE_BOOLEAN) {
		/* the offset is the has bit of the value */
		subst_set(&g->variables, "storage_offset_value", objc_itoa(index_base));
		subst_set(&g->variables, "storage_offset_comment", "  // Stored in _has_storage_ to save space.");
	}
}

static bool field_needs_textformat_name_support(const FieldGenerator *g)
{
	return strstr(field_variable(g, "fieldflags"), "GPBFieldTextFormatNameCustom") != NULL;
}

/* The subclass methods (enum_field.c, message_field.c, map_field.c). */
static void enum_field_generate_c_function_declarations(const FieldGenerator *g, LpPrinter *p);
static void enum_field_generate_c_function_implementations(const FieldGenerator *g, LpPrinter *p);
static void enum_field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
						      bool include_external_types);
static void message_field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
							 bool include_external_types);
static void map_field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
						     bool include_external_types);
static void map_field_determine_objective_c_class_definitions(const FieldGenerator *g, StringSet *fwd_decls);
static void map_field_determine_needed_files(const FieldGenerator *g, FileSet *deps);

static void field_generate_c_function_declarations(const FieldGenerator *g, LpPrinter *p)
{
	if (g->kind == FIELD_ENUM)
		enum_field_generate_c_function_declarations(g, p);
}

static void field_generate_c_function_implementations(const FieldGenerator *g, LpPrinter *p)
{
	if (g->kind == FIELD_ENUM)
		enum_field_generate_c_function_implementations(g, p);
}

static void field_determine_forward_declarations(const FieldGenerator *g, StringSet *fwd_decls,
						 bool include_external_types)
{
	switch (g->kind) {
	case FIELD_ENUM: enum_field_determine_forward_declarations(g, fwd_decls, include_external_types); break;
	case FIELD_MESSAGE:
	case FIELD_REPEATED_MESSAGE:
		message_field_determine_forward_declarations(g, fwd_decls, include_external_types);
		break;
	case FIELD_MAP: map_field_determine_forward_declarations(g, fwd_decls, include_external_types); break;
	default: break;
	}
}

static void field_determine_objective_c_class_definitions(const FieldGenerator *g, StringSet *fwd_decls)
{
	switch (g->kind) {
	case FIELD_MESSAGE:
	case FIELD_REPEATED_MESSAGE:
		string_set_insert(fwd_decls, objc_class_declaration(field_variable(g, "msg_type")));
		break;
	case FIELD_MAP: map_field_determine_objective_c_class_definitions(g, fwd_decls); break;
	default: break;
	}
}

static void field_determine_needed_files(const FieldGenerator *g, FileSet *deps)
{
	const LpField *d = g->descriptor;
	switch (g->kind) {
	case FIELD_ENUM:
	case FIELD_REPEATED_ENUM:
		if (d->file != d->enum_type->file)
			file_set_insert(deps, d->enum_type->file);
		break;
	case FIELD_MESSAGE:
	case FIELD_REPEATED_MESSAGE:
		if (d->file != d->message_type->file)
			file_set_insert(deps, d->message_type->file);
		break;
	case FIELD_MAP: map_field_determine_needed_files(g, deps); break;
	default: break;
	}
}

/* FieldGeneratorMap */
static FieldGenerator **field_generator_map(const LpMessage *descriptor, const GenerationOptions *generation_options)
{
	FieldGenerator **gens = xalloc(sizeof(FieldGenerator *) * (size_t)(descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++)
		gens[i] = field_generator_make(&descriptor->fields[i], generation_options);
	return gens;
}

/* CalculateHasBits: assigns the has bits and returns the number of bits needed. */
static int field_generator_map_calculate_has_bits(FieldGenerator **gens, int n)
{
	int total_bits = 0;
	for (int i = 0; i < n; i++) {
		if (field_runtime_uses_has_bit(gens[i])) {
			field_set_runtime_has_bit(gens[i], total_bits);
			++total_bits;
		} else {
			field_set_no_has_bit(gens[i]);
		}
		int extra_bits = field_extra_runtime_has_bits_needed(gens[i]);
		if (extra_bits) {
			field_set_extra_runtime_has_bits_base(gens[i], total_bits);
			total_bits += extra_bits;
		}
	}
	return total_bits;
}

static bool field_generator_map_any_non_zero_default(const LpMessage *descriptor)
{
	for (int i = 0; i < descriptor->n_fields; i++)
		if (has_non_zero_default_value(&descriptor->fields[i]))
			return true;
	return false;
}

/* The subclass constructors (after the FieldGenerator ones). */
static void primitive_obj_field_init(FieldGenerator *g);
static void enum_field_init(FieldGenerator *g);
static void message_field_init(FieldGenerator *g);
static void map_field_init(FieldGenerator *g);

/* FieldGenerator::Make */
static FieldGenerator *field_generator_make(const LpField *field, const GenerationOptions *generation_options)
{
	FieldGenerator *g;
	if (proto_field_is_repeated(field)) {
		switch (get_objective_c_type(field)) {
		case OBJECTIVECTYPE_MESSAGE:
			if (proto_field_is_map(field)) {
				g = field_generator_new(FIELD_MAP, field, generation_options);
				map_field_init(g);
			} else {
				g = field_generator_new(FIELD_REPEATED_MESSAGE, field, generation_options);
				message_field_init(g);
			}
			return g;
		case OBJECTIVECTYPE_ENUM:
			g = field_generator_new(FIELD_REPEATED_ENUM, field, generation_options);
			enum_field_init(g);
			return g;
		default:
			return field_generator_new(FIELD_REPEATED_PRIMITIVE, field, generation_options);
		}
	}
	switch (get_objective_c_type(field)) {
	case OBJECTIVECTYPE_MESSAGE:
		g = field_generator_new(FIELD_MESSAGE, field, generation_options);
		message_field_init(g);
		return g;
	case OBJECTIVECTYPE_ENUM:
		g = field_generator_new(FIELD_ENUM, field, generation_options);
		enum_field_init(g);
		return g;
	default:
		if (is_reference_type(field)) {
			g = field_generator_new(FIELD_PRIMITIVE_OBJ, field, generation_options);
			primitive_obj_field_init(g);
			return g;
		}
		return field_generator_new(FIELD_PRIMITIVE, field, generation_options);
	}
}
