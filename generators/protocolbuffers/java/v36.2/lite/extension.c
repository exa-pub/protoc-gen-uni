// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/extension.{h,cc} (protobuf v36.2). */

static void ImmutableExtensionLiteGenerator_Generate(JvExtensionGen *g, LpPrinter *printer);
static int ImmutableExtensionLiteGenerator_GenerateNonNestedInitializationCode(JvExtensionGen *g, LpPrinter *printer);
static int ImmutableExtensionLiteGenerator_GenerateRegistrationCode(JvExtensionGen *g, LpPrinter *printer);

static void ImmutableExtensionLiteGenerator_Generate(JvExtensionGen *g, LpPrinter *printer)
{
	LpVars vars = { 0 };
	const bool kUseImmutableNames = true;
	ExtensionGenerator_InitTemplateVars(g->descriptor, g->scope, kUseImmutableNames, &vars, g->context);
	lp_print_vars(printer, &vars, "public static final int $constant_name$ = $number$;\n");

	jv_write_field_doc_comment(printer, g->descriptor, &g->context->options, false);
	if (proto_field_is_repeated(g->descriptor)) {
		lp_print_vars(printer, &vars,
			      "public static final\n"
			      "  com.google.protobuf.GeneratedMessageLite.GeneratedExtension<\n"
			      "    $containing_type$,\n"
			      "    $type$> $name$ = com.google.protobuf.GeneratedMessageLite\n"
			      "        .newRepeatedGeneratedExtension(\n"
			      "      $containing_type$.getDefaultInstance(),\n"
			      "      $prototype$,\n"
			      "      $enum_map$,\n"
			      "      $number$,\n"
			      "      com.google.protobuf.WireFormat.FieldType.$type_constant$,\n"
			      "      $packed$,\n"
			      "      $singular_type$.class);\n");
	} else {
		lp_print_vars(printer, &vars,
			      "public static final\n"
			      "  com.google.protobuf.GeneratedMessageLite.GeneratedExtension<\n"
			      "    $containing_type$,\n"
			      "    $type$> $name$ = com.google.protobuf.GeneratedMessageLite\n"
			      "        .newSingularGeneratedExtension(\n"
			      "      $containing_type$.getDefaultInstance(),\n"
			      "      $default$,\n"
			      "      $prototype$,\n"
			      "      $enum_map$,\n"
			      "      $number$,\n"
			      "      com.google.protobuf.WireFormat.FieldType.$type_constant$,\n"
			      "      $singular_type$.class);\n");
	}
}

static int ImmutableExtensionLiteGenerator_GenerateNonNestedInitializationCode(JvExtensionGen *g, LpPrinter *printer)
{
	return 0;
}

static int ImmutableExtensionLiteGenerator_GenerateRegistrationCode(JvExtensionGen *g, LpPrinter *printer)
{
	lp_print(printer, "registry.add($scope$.$name$);\n", "scope", g->scope, "name",
		 jv_underscores_to_camel_case_check_reserved(g->descriptor), NULL);
	return 7;
}
