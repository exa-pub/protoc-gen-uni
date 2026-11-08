// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/cpp/extension.{h,cc} (protobuf v36.2). */

static bool ext_is_scoped(const ExtensionGenerator *g) { return g->descriptor->extension_scope != NULL; }

static ExtensionGenerator *extension_generator_new(const LpField *descriptor, const Options *options)
{
	ExtensionGenerator *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->options = *options;
	const char *rep = is_repeated(descriptor) ? "Repeated" : "";
	switch (cpp_type(descriptor)) {
	case LP_CPPTYPE_ENUM:
		g->type_traits = xcat(rep, "EnumTypeTraits< ", qualified_enum_class_name(descriptor->enum_type, options), ">");
		break;
	case LP_CPPTYPE_STRING:
		g->type_traits = xcat(rep, "StringTypeTraits");
		break;
	case LP_CPPTYPE_MESSAGE:
		g->type_traits = xcat(rep, "MessageTypeTraits< ", qualified_class_name(descriptor->message_type, options), " >");
		break;
	default:
		g->type_traits = xcat(rep, "PrimitiveTypeTraits< ", primitive_type_name(options, cpp_type(descriptor)), " >");
		break;
	}
	Subs *v = &g->variables;
	set_common_message_data_variables(descriptor->containing_type, v);
	subs_set(v, "extendee", qualified_class_name(descriptor->containing_type, options));
	subs_set(v, "type_traits", g->type_traits);
	subs_set(v, "name", lp_cpp_resolve_known_name_collisions(descriptor->name,
								  descriptor->extension_scope ? LP_CPP_CTX_MESSAGE : LP_CPP_CTX_FILE,
								  LP_CPP_KIND_VALUE));
	subs_set(v, "constant_name", field_constant_name(descriptor));
	subs_set(v, "field_type", cpp_itoa(descriptor->type));
	if (descriptor->type == PT_STRING && !proto_field_requires_utf8_validation(descriptor))
		subs_set(v, "field_type", cpp_itoa(PT_BYTES));
	subs_set(v, "repeated", is_repeated(descriptor) ? "true" : "false");
	subs_set(v, "packed", proto_field_is_packed(descriptor) ? "true" : "false");
	subs_set(v, "dllexport_decl", options->dllexport_decl);
	subs_set(v, "scope", ext_is_scoped(g) ? xcat(class_name(descriptor->extension_scope), "::") : "");
	subs_set(v, "scoped_name", lp_cpp_extension_name(descriptor));
	subs_set(v, "number", cpp_itoa(descriptor->number));
	return g;
}

static void ext_generate_feature_defaults(const ExtensionGenerator *g, LpPrinter *p)
{
	if (!g->descriptor->message_type)
		return;
	if (!str_eq(g->descriptor->containing_type->full_name, "google.protobuf.FeatureSet"))
		return;
	fail("unsupported by protoc-gen-uni: the extension %s of google.protobuf.FeatureSet (the C++ feature "
	     "set defaults of FeatureResolver::CompileDefaults are not ported)",
	     g->descriptor->full_name);
}

/* p->WithAnnotations({{"name", descriptor_}, {"constant_name", descriptor_}}) */
static Subs ext_annotations(const ExtensionGenerator *g, bool constant_name)
{
	Subs a = {0};
	subs_push(&a, (LpSub){.key = "name", .value = subs_get(&g->variables, "name"), .annotate = g->descriptor});
	if (constant_name)
		subs_push(&a, (LpSub){.key = "constant_name", .value = subs_get(&g->variables, "constant_name"),
				      .annotate = g->descriptor});
	return a;
}

static void ext_generate_declaration(const ExtensionGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	Subs ann = ext_annotations(g, true);
	PUSH_SUBS(p, &ann);
	EMIT(p,
	     "\n"
	     " inline $constant_qualifier $constexpr int $constant_name$ = $number$;\n"
	     " $id_qualifier$ $pbi$::ExtensionIdentifier<\n"
	     "     $extendee$, $pbi$::$type_traits$, $field_type$, $packed$>($name$);\n",
	     S_("constant_qualifier", ext_is_scoped(g) ? "static" : ""),
	     S_("id_qualifier", ext_is_scoped(g)		? "static"
				: !*g->options.dllexport_decl ? "extern"
							      : xcat(g->options.dllexport_decl, " extern")));
	POP_VARS(p);
	POP_VARS(p);
}

static void ext_generate_definition(ExtensionGenerator *g, LpPrinter *p)
{
	PUSH_SUBS(p, &g->variables);
	if (cpp_type(g->descriptor) == LP_CPPTYPE_STRING) {
		EMIT(p,
		     "\n"
		     " const std::string $default_str$($default_val$);\n"
		     " PROTOBUF_CONSTINIT$ dllexport_decl$\n"
		     "     PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 ::_pbi::ExtensionIdentifier<\n"
		     "         $extendee$, ::_pbi::$type_traits$, $field_type$, $packed$>\n"
		     "         $scoped_name$($constant_name$, $default_str$);\n",
		     S_("default_str", xcat(str_replace_all(subs_get(&g->variables, "scoped_name"), "::", "_"), "_default")),
		     S_("default_val", default_value(&g->options, g->descriptor)));
	} else if (g->descriptor->message_type) {
		EMIT(p,
		     "\n"
		     " PROTOBUF_CONSTINIT$ dllexport_decl$\n"
		     "     PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 ::_pbi::ExtensionIdentifier<\n"
		     "         $extendee$, ::_pbi::$type_traits$, $field_type$, $packed$>\n"
		     "         $scoped_name$($constant_name$, &$globals_name$);\n",
		     S_("globals_name", qualified_msg_globals_instance_name(g->descriptor->message_type, &g->options)));
	} else {
		EMIT(p,
		     "\n"
		     " PROTOBUF_CONSTINIT$ dllexport_decl$\n"
		     "     PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 ::_pbi::ExtensionIdentifier<\n"
		     "         $extendee$, ::_pbi::$type_traits$, $field_type$, $packed$>\n"
		     "         $scoped_name$($constant_name$, $default_val$);\n",
		     S_("default_val", default_value(&g->options, g->descriptor)));
	}
	POP_VARS(p);
}

static bool ext_will_generate_registration(const ExtensionGenerator *g, InitPriority priority)
{
	if (!using_implicit_weak_descriptor(g->descriptor->file, &g->options))
		return priority == INIT_PRIORITY_102;
	return true;
}

static void ext_verify_funcs(LpPrinter *p, void *arg) { EMIT0(p, "nullptr,"); }

static void ext_generate_registration(ExtensionGenerator *g, LpPrinter *p, InitPriority priority)
{
	const LpField *d = g->descriptor;
	const Options *o = &g->options;
	if (using_implicit_weak_descriptor(d->file, o))
		fail("unsupported by protoc-gen-uni: descriptor_implicit_weak_messages");
	PUSH_SUBS(p, &g->variables);
	LpSub vars2[] = {
		S_("extendee_table", descriptor_table_name(d->containing_type->file, o)),
		SBOOL("preregister", priority == INIT_PRIORITY_101),
		S_("is_utf8", proto_field_requires_utf8_validation(d) ? "/*is_utf8=*/true" : "/*is_utf8=*/false"),
	};
	lp_push_vars(p, vars2, 3);
	switch (cpp_type(d)) {
	case LP_CPPTYPE_ENUM:
		if (priority == INIT_PRIORITY_102)
			EMIT(p,
			     "\n"
			     " ::_pbi::ExtensionSet::RegisterEnumExtension(\n"
			     "     &$extendee$::default_instance(), $number$, $field_type$,\n"
			     "     $repeated$, $packed$, $enum_name$_internal_data_),\n",
			     S_("enum_name", qualified_enum_class_name(d->enum_type, o)));
		break;
	case LP_CPPTYPE_MESSAGE: {
		const char *message_type = field_message_type_name(d, o);
		LpSub v[] = {S_("verify", "nullptr"), S_("message_type", message_type), S_("lazy", "kUndefined")};
		lp_push_vars(p, v, 3);
		if (priority == INIT_PRIORITY_102)
			EMIT(p,
			     "\n"
			     " ::_pbi::ExtensionSet::RegisterMessageExtension(\n"
			     "     &$extendee$::default_instance(), $number$, $field_type$,\n"
			     "     $repeated$, $packed$, &$message_type$::default_instance(),\n"
			     "     $verify_funcs$, ::_pbi::LazyAnnotation::$lazy$),\n",
			     SCB("verify_funcs", ext_verify_funcs, NULL));
		POP_VARS(p);
		break;
	}
	default:
		if (priority == INIT_PRIORITY_102)
			EMIT0(p, "\n"
				 " ::_pbi::ExtensionSet::RegisterExtension(\n"
				 "     &$extendee$::default_instance(), $number$, $field_type$,\n"
				 "     $repeated$, $packed$, $is_utf8$),\n");
		break;
	}
	POP_VARS(p);
	POP_VARS(p);
}
