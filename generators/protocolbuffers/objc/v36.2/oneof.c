// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/oneof.{h,cc} (protobuf v36.2). */

static OneofGenerator *oneof_generator_new(const LpOneof *descriptor, const GenerationOptions *generation_options)
{
	OneofGenerator *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->generation_options = generation_options;
	subst_set(&g->variables, "enum_name", lp_objc_oneof_enum_name(descriptor));
	subst_set(&g->variables, "name", lp_objc_oneof_name(descriptor));
	subst_set(&g->variables, "capitalized_name", lp_objc_oneof_name_capitalized(descriptor));
	subst_set(&g->variables, "raw_index", objc_itoa(descriptor->index));
	subst_set(&g->variables, "owning_message_class", lp_objc_class_name(descriptor->containing_type));
	return g;
}

static void oneof_set_oneof_index_base(OneofGenerator *g, int index_base)
{
	int index = g->descriptor->index + index_base;
	/* the sign flipped marks a oneof */
	subst_set(&g->variables, "index", objc_itoa(-index));
}

static void oneof_cases(LpPrinter *p, void *arg)
{
	const OneofGenerator *g = arg;
	for (int j = 0; j < g->descriptor->n_fields; j++) {
		const LpField *field = proto_oneof_field(g->descriptor, j);
		LpSub subs[] = {
			LP_SUB_ANN("enum_entry_name",
				   xcat(lp_lookup_var(p, "enum_name"), "_", lp_objc_field_name_capitalized(field)), field),
			LP_SUB("field_number", objc_itoa(field->number)),
		};
		lp_emit(p, subs, 2,
			"\n"
			" $enum_entry_name$ = $field_number$,\n");
	}
}

static void oneof_generate_case_enum(const OneofGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	LpSub subs[] = {LP_SUB_CB("cases", oneof_cases, (void *)g)};
	lp_emit(p, subs, 1,
		"\n"
		" typedef GPB_ENUM($enum_name$) {\n"
		"   $enum_name$_GPBUnsetOneOfCase = 0,\n"
		"   $cases$\n"
		" };\n");
	lp_emit(p, NULL, 0, "\n");
	lp_pop_vars(p);
}

static void oneof_comments(LpPrinter *p, void *arg)
{
	const OneofGenerator *g = arg;
	emit_comments_string(p, g->generation_options, g->descriptor, 0);
}

static void oneof_generate_public_case_property_declaration(const OneofGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	LpSub subs[] = {
		LP_SUB_ANN("oneof_getter_name", xcat(lp_lookup_var(p, "name"), "OneOfCase"), g->descriptor),
		LP_SUB_CB("comments", oneof_comments, (void *)g),
	};
	lp_emit(p, subs, 2,
		"\n"
		" $comments$;\n"
		" @property(nonatomic, readonly) $enum_name$ $oneof_getter_name$;\n");
	lp_emit(p, NULL, 0, "\n");
	lp_pop_vars(p);
}

static void oneof_generate_clear_function_declaration(const OneofGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	LpSub subs[] = {
		LP_SUB_ANN("clear_function_name",
			   xcat(lp_lookup_var(p, "owning_message_class"), "_Clear", lp_lookup_var(p, "capitalized_name"),
				"OneOfCase"),
			   g->descriptor),
	};
	lp_emit(p, subs, 1,
		"\n"
		" /**\n"
		"  * Clears whatever value was set for the oneof '$name$'.\n"
		"  **/\n"
		" void $clear_function_name$($owning_message_class$ *message);\n");
	lp_pop_vars(p);
}

static void oneof_generate_property_implementation(const OneofGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0,
		"\n"
		" @dynamic $name$OneOfCase;\n");
	lp_pop_vars(p);
}

static void oneof_generate_clear_function_implementation(const OneofGenerator *g, LpPrinter *p)
{
	subst_install(&g->variables, p);
	lp_emit(p, NULL, 0,
		"\n"
		" void $owning_message_class$_Clear$capitalized_name$OneOfCase($owning_message_class$ *message) {\n"
		"   GPBDescriptor *descriptor = [$owning_message_class$ descriptor];\n"
		"   GPBOneofDescriptor *oneof = [descriptor.oneofs objectAtIndex:$raw_index$];\n"
		"   GPBClearOneof(message, oneof);\n"
		" }\n");
	lp_pop_vars(p);
}

static const char *oneof_descriptor_name(const OneofGenerator *g) { return subst_value(&g->variables, "name"); }

static const char *oneof_has_index_as_string(const OneofGenerator *g) { return subst_value(&g->variables, "index"); }
