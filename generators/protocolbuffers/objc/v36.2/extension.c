// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/extension.{h,cc} (protobuf v36.2). */

static ExtensionGenerator *extension_generator_new(const LpField *descriptor, const GenerationOptions *generation_options)
{
	ExtensionGenerator *g = xalloc(sizeof *g);
	g->root_or_message_class_name = lp_objc_extension_class_name(descriptor);
	g->method_name = lp_objc_extension_method_name(descriptor);
	g->function_name = lp_objc_extension_function_name(descriptor);
	g->descriptor = descriptor;
	g->generation_options = generation_options;
	if (proto_field_is_map(descriptor))
		fail("error: Extension is a map<>! That used to be blocked by the compiler.");
	if (pb_int(descriptor->containing_type->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0)) {
		if (descriptor->type != PT_MESSAGE)
			fail("error: Extension to a message_set_wire_format message and the type wasn't a message!");
		if (proto_field_is_repeated(descriptor))
			fail("error: Extension to a message_set_wire_format message should not be repeated!");
	}
	return g;
}

static void extension_comments(LpPrinter *p, void *arg)
{
	const ExtensionGenerator *g = arg;
	emit_comments_string(p, g->generation_options, g->descriptor, 0);
}

static void extension_generate_functions_header(const ExtensionGenerator *g, LpPrinter *p)
{
	LpSub subs[] = {
		LP_SUB_ANN("function_name", g->function_name, g->descriptor),
		LP_SUB_CB("comments", extension_comments, (void *)g),
		LP_SUB("storage_attribute", lp_objc_is_retained_name(g->function_name) ? "NS_RETURNS_NOT_RETAINED" : ""),
		/* unlike message fields, the file of the extension may be deprecated */
		LP_SUB("deprecated_attribute", get_optional_deprecated_attribute(g->descriptor, g->descriptor->file)),
	};
	lp_emit(p, subs, 4,
		"\n"
		" $comments$\n"
		" GPBExtensionDescriptor *$function_name$(void)$ storage_attribute$$ deprecated_attribute$;\n");
}

static void extension_generate_methods_header(const ExtensionGenerator *g, LpPrinter *p)
{
	LpSub subs[] = {
		LP_SUB_ANN("method_name", g->method_name, g->descriptor),
		LP_SUB_CB("comments", extension_comments, (void *)g),
		LP_SUB("storage_attribute", lp_objc_is_retained_name(g->method_name) ? "NS_RETURNS_NOT_RETAINED" : ""),
		LP_SUB("deprecated_attribute", get_optional_deprecated_attribute(g->descriptor, g->descriptor->file)),
	};
	lp_emit(p, subs, 4,
		"\n"
		" $comments$\n"
		" + (GPBExtensionDescriptor *)$method_name$$ storage_attribute$$ deprecated_attribute$;\n");
}

static void extension_generate_add_extension_to_registry_function_call(const ExtensionGenerator *g, LpPrinter *p)
{
	LpSub subs[] = {LP_SUB("function_name", g->function_name)};
	lp_emit(p, subs, 1,
		"\n"
		"[registry addExtension:$function_name$()];\n"
		"      ");
}

static void extension_generate_add_extension_to_global_registry_function_call(const ExtensionGenerator *g,
										LpPrinter *p)
{
	LpSub subs[] = {LP_SUB("function_name", g->function_name)};
	lp_emit(p, subs, 1,
		"\n"
		"[GPBRootObject globallyRegisterExtension:$function_name$()];\n"
		"      ");
}

/* The variables of GenerateStaticVariablesInitialization and GenerateDescriptorFunction. */
enum { EXTENSION_N_SUBS = 11 };

static void extension_description_subs(const ExtensionGenerator *g, LpSub *subs)
{
	const LpField *d = g->descriptor;
	char *containing_type = lp_objc_class_name(d->containing_type);
	ObjectiveCType objc_type = get_objective_c_type(d);
	const char *options[2];
	int n = 0;
	if (proto_field_is_repeated(d))
		options[n++] = "GPBExtensionRepeated";
	if (proto_field_is_packed(d))
		options[n++] = "GPBExtensionPacked";
	subs[0] = LP_SUB("default", proto_field_is_repeated(d) ? "nil" : default_value(d));
	subs[1] = LP_SUB("default_name", gpb_generic_value_field_name(d));
	subs[2] = LP_SUB("enum_desc_func_name", objc_type == OBJECTIVECTYPE_ENUM
							  ? xcat(lp_objc_enum_name(d->enum_type), "_EnumDescriptor")
							  : "NULL");
	subs[3] = LP_SUB("extended_type", objc_class(containing_type));
	subs[4] = LP_SUB("extension_type", xcat("GPBDataType", get_capitalized_type(d)));
	subs[5] = LP_SUB("method_name", g->method_name);
	subs[6] = LP_SUB("number", objc_itoa(d->number));
	subs[7] = LP_SUB("options", build_flags_string(FLAGTYPE_EXTENSION, options, n));
	subs[8] = LP_SUB("root_or_message_class_name", g->root_or_message_class_name);
	subs[9] = LP_SUB("type", objc_type == OBJECTIVECTYPE_MESSAGE ? objc_class(lp_objc_class_name(d->message_type)) : "Nil");
	subs[10] = LP_SUB("function_name", g->function_name);
}

static void extension_generate_static_variables_initialization(const ExtensionGenerator *g, LpPrinter *p)
{
	LpSub subs[EXTENSION_N_SUBS];
	extension_description_subs(g, subs);
	lp_emit(p, subs, EXTENSION_N_SUBS - 1,
		"\n"
		" {\n"
		"   .defaultValue.$default_name$ = $default$,\n"
		"   .singletonName = GPBStringifySymbol($root_or_message_class_name$) \"_$method_name$\",\n"
		"   .extendedClass.clazz = $extended_type$,\n"
		"   .messageOrGroupClass.clazz = $type$,\n"
		"   .enumDescriptorFunc = $enum_desc_func_name$,\n"
		"   .fieldNumber = $number$,\n"
		"   .dataType = $extension_type$,\n"
		"   .options = $options$,\n"
		" },\n");
}

static void extension_generate_descriptor_function(const ExtensionGenerator *g, LpPrinter *p)
{
	LpSub subs[EXTENSION_N_SUBS];
	extension_description_subs(g, subs);
	lp_emit(p, subs, EXTENSION_N_SUBS,
		"\n"
		" GPBExtensionDescriptor *$function_name$(void) {\n"
		"   static GPBExtensionDescription description = {\n"
		"     .defaultValue.$default_name$ = $default$,\n"
		"     .singletonName = GPBStringifySymbol($root_or_message_class_name$) \"_$method_name$\",\n"
		"     .extendedClass.clazz = $extended_type$,\n"
		"     .messageOrGroupClass.clazz = $type$,\n"
		"     .enumDescriptorFunc = $enum_desc_func_name$,\n"
		"     .fieldNumber = $number$,\n"
		"     .dataType = $extension_type$,\n"
		"     .options = $options$,\n"
		"   };\n"
		"   static GPBExtensionDescriptor *extension;\n"
		"   static dispatch_once_t onceToken;\n"
		"   dispatch_once(&onceToken, ^{\n"
		"     extension =\n"
		"       [[GPBExtensionDescriptor alloc] initWithExtensionDescription:&description\n"
		"                                                     runtimeSupport:&$google_protobuf_runtime_support$];\n"
		"   });\n"
		"   return extension;\n"
		" }\n");
}

static void extension_determine_objective_c_class_definitions(const ExtensionGenerator *g, StringSet *fwd_decls)
{
	string_set_insert(fwd_decls, objc_class_declaration(lp_objc_class_name(g->descriptor->containing_type)));
	if (get_objective_c_type(g->descriptor) == OBJECTIVECTYPE_MESSAGE)
		string_set_insert(fwd_decls, objc_class_declaration(lp_objc_class_name(g->descriptor->message_type)));
}

static void extension_determine_needed_files(const ExtensionGenerator *g, FileSet *deps)
{
	const LpField *d = g->descriptor;
	if (d->file != d->containing_type->file)
		file_set_insert(deps, d->containing_type->file);
	ObjectiveCType objc_type = get_objective_c_type(d);
	if (objc_type == OBJECTIVECTYPE_MESSAGE) {
		if (d->file != d->message_type->file)
			file_set_insert(deps, d->message_type->file);
	} else if (objc_type == OBJECTIVECTYPE_ENUM) {
		if (d->file != d->enum_type->file)
			file_set_insert(deps, d->enum_type->file);
	}
}
