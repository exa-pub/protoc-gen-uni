// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/message_builder.{h,cc} (protobuf v36.2). */

static void MessageBuilderLiteGenerator_Generate(JvMessageGen *g, LpPrinter *printer);
static void MessageBuilderLiteGenerator_GenerateCommonBuilderMethods(JvMessageGen *g, LpPrinter *printer);

/* MessageBuilderLiteGenerator::MessageBuilderLiteGenerator: generator_factory.c */

static void MessageBuilderLiteGenerator_Generate(JvMessageGen *g, LpPrinter *printer)
{
	jv_write_message_doc_comment(printer, g->descriptor, &g->context->options, false);
	LpVars vars = { 0 };
	lp_vars_set(&vars, "{", "");
	lp_vars_set(&vars, "}", "");
	lp_vars_set(&vars, "classname", jv_immutable_class_name(g->descriptor));
	lp_vars_set(&vars, "extra_interfaces", jv_extra_builder_interfaces(g->descriptor));
	lp_vars_set(&vars, "extendible", g->descriptor->n_extension_ranges > 0 ? "Extendable" : "");
	lp_print_vars(printer, &vars,
		      "public static final class ${$Builder$}$ extends\n"
		      "    com.google.protobuf.GeneratedMessageLite.$extendible$Builder<\n"
		      "      $classname$, Builder> implements\n"
		      "    $extra_interfaces$\n"
		      "    $classname$OrBuilder {\n");
	lp_indent(printer);

	MessageBuilderLiteGenerator_GenerateCommonBuilderMethods(g, printer);

	// oneof
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		const LpOneof *oneof = g->oneofs[kv_i];
		lp_vars_set(&vars, "oneof_name", jv_context_oneof_info(g->context, oneof)->name);
		lp_vars_set(&vars, "oneof_capitalized_name",
			    jv_context_oneof_info(g->context, oneof)->capitalized_name);
		lp_vars_set(&vars, "oneof_index", xcat(jv_i(oneof->index)));

		// oneofCase() and clearOneof()
		lp_print_vars(printer, &vars,
			      "@java.lang.Override\n"
			      "public $oneof_capitalized_name$Case\n"
			      "    ${$get$oneof_capitalized_name$Case$}$() {\n"
			      "  return instance.get$oneof_capitalized_name$Case();\n"
			      "}\n");
		lp_print_vars(printer, &vars,
			      "\n"
			      "public Builder ${$clear$oneof_capitalized_name$$}$() {\n"
			      "  copyOnWrite();\n"
			      "  instance.clear$oneof_capitalized_name$();\n"
			      "  return this;\n"
			      "}\n"
			      "\n");
	}

	for (int i = 0; i < g->descriptor->n_fields; i++) {
		lp_print(printer, "\n", NULL);
		JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateBuilderMembers, printer);
	}

	lp_print(printer,
		 "\n"
		 "// @@protoc_insertion_point(builder_scope:$full_name$)\n",
		 "full_name", g->descriptor->full_name, NULL);

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

// ===================================================================

static void MessageBuilderLiteGenerator_GenerateCommonBuilderMethods(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "// Construct using $classname$.newBuilder()\n"
		 "private Builder() {\n"
		 "  super(DEFAULT_INSTANCE);\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);
}

// ===================================================================
