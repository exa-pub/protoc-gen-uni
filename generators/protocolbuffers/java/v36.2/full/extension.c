// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/extension.{h,cc} (protobuf v36.2), with ExtensionGenerator::InitTemplateVars (generator_factory.h). */

static void ExtensionGenerator_InitTemplateVars(const LpField *descriptor, const char *scope, bool immutable,
						LpVars *vars, JvContext *context);
static void ImmutableExtensionGenerator_Generate(JvExtensionGen *g, LpPrinter *printer);
static int ImmutableExtensionGenerator_GenerateNonNestedInitializationCode(JvExtensionGen *g, LpPrinter *printer);
static int ImmutableExtensionGenerator_GenerateRegistrationCode(JvExtensionGen *g, LpPrinter *printer);

// Initializes the vars referenced in the generated code templates.
static void ExtensionGenerator_InitTemplateVars(const LpField *descriptor, const char *scope, bool immutable,
						LpVars *vars, JvContext *context)
{
	lp_vars_set(vars, "scope", scope);
	lp_vars_set(vars, "name", jv_underscores_to_camel_case_check_reserved(descriptor));
	lp_vars_set(vars, "containing_type", jv_get_class_name_gen(descriptor->containing_type, immutable));
	lp_vars_set(vars, "number", xcat(jv_i(descriptor->number)));
	lp_vars_set(vars, "constant_name", jv_field_constant_name(descriptor));
	lp_vars_set(vars, "index", xcat(jv_i(descriptor->index)));
	lp_vars_set(vars, "default",
		    proto_field_is_repeated(descriptor) ? ""
							: jv_default_value_(descriptor, immutable, &context->options));
	lp_vars_set(vars, "type_constant", (FieldTypeName(jv_get_type(descriptor))));
	lp_vars_set(vars, "packed", proto_field_is_packed(descriptor) ? "true" : "false");
	lp_vars_set(vars, "enum_map", "null");
	lp_vars_set(vars, "prototype", "null");

	JvJavaType java_type = jv_get_java_type(descriptor);
	const char *singular_type;
	switch (java_type) {
	case JAVATYPE_MESSAGE:
		singular_type = jv_get_class_name_gen(descriptor->message_type, immutable);
		lp_vars_set(vars, "prototype", xcat(singular_type, ".getDefaultInstance()"));
		break;
	case JAVATYPE_ENUM:
		singular_type = jv_get_class_name_gen(descriptor->enum_type, immutable);
		lp_vars_set(vars, "enum_map", xcat(singular_type, ".internalGetValueMap()"));
		break;
	case JAVATYPE_STRING: singular_type = "java.lang.String"; break;
	case JAVATYPE_BYTES: singular_type = immutable ? "com.google.protobuf.ByteString" : "byte[]"; break;
	default: singular_type = (jv_boxed_primitive_type_name(java_type)); break;
	}
	lp_vars_set(vars, "type",
		    proto_field_is_repeated(descriptor) ? xcat("java.util.List<", singular_type, ">") : singular_type);
	lp_vars_set(vars, "singular_type", singular_type);
}

static void ImmutableExtensionGenerator_Generate(JvExtensionGen *g, LpPrinter *printer)
{
	LpVars vars = { 0 };
	const bool kUseImmutableNames = true;
	ExtensionGenerator_InitTemplateVars(g->descriptor, g->scope, kUseImmutableNames, &vars, g->context);
	lp_print_vars(printer, &vars, "public static final int $constant_name$ = $number$;\n");

	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	if (g->descriptor->extension_scope == NULL) {
		// Non-nested
		lp_print_vars(printer, &vars,
			      "public static final\n"
			      "  com.google.protobuf.GeneratedMessage.GeneratedExtension<\n"
			      "    $containing_type$,\n"
			      "    $type$> $name$ = com.google.protobuf.GeneratedMessage\n"
			      "        .newFileScopedGeneratedExtension(\n"
			      "      $singular_type$.class,\n"
			      "      $prototype$);\n");
	} else {
		// Nested
		lp_print_vars(printer, &vars,
			      "public static final\n"
			      "  com.google.protobuf.GeneratedMessage.GeneratedExtension<\n"
			      "    $containing_type$,\n"
			      "    $type$> $name$ = com.google.protobuf.GeneratedMessage\n"
			      "        .newMessageScopedGeneratedExtension(\n"
			      "      $scope$.getDefaultInstance(),\n"
			      "      $index$,\n"
			      "      $singular_type$.class,\n"
			      "      $prototype$);\n");
	}
}

static int ImmutableExtensionGenerator_GenerateNonNestedInitializationCode(JvExtensionGen *g, LpPrinter *printer)
{
	int bytecode_estimate = 0;
	if (g->descriptor->extension_scope == NULL) {
		// Only applies to non-nested extensions.
		lp_print(printer, "$name$.internalInit(descriptor.getExtension($index$));\n", "name",
			 jv_underscores_to_camel_case_check_reserved(g->descriptor), "index",
			 xcat(jv_i(g->descriptor->index)), NULL);
		bytecode_estimate += 21;
	}
	return bytecode_estimate;
}

static int ImmutableExtensionGenerator_GenerateRegistrationCode(JvExtensionGen *g, LpPrinter *printer)
{
	lp_print(printer, "registry.add($scope$.$name$);\n", "scope", g->scope, "name",
		 jv_underscores_to_camel_case_check_reserved(g->descriptor), NULL);
	return 7;
}
