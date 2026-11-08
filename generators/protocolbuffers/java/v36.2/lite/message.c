// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/message.{h,cc} (protobuf v36.2). */

static void ImmutableMessageLiteGenerator_GenerateStaticVariables(JvMessageGen *g, LpPrinter *printer,
								  int *bytecode_estimate);
static int ImmutableMessageLiteGenerator_GenerateStaticVariableInitializers(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateInterface(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_Generate(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateDynamicMethodNewBuildMessageInfo(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateParseFromMethods(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateBuilder(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateDynamicMethodNewBuilder(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateExtensionRegistrationCode(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateConstructor(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateParser(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageLiteGenerator_GenerateInitializers(JvMessageGen *g, LpPrinter *printer);

// ===================================================================
/* ImmutableMessageLiteGenerator::ImmutableMessageLiteGenerator: generator_factory.c */

static void ImmutableMessageLiteGenerator_GenerateStaticVariables(JvMessageGen *g, LpPrinter *printer,
								  int *bytecode_estimate)
{
	// No-op for lite.
}

static int ImmutableMessageLiteGenerator_GenerateStaticVariableInitializers(JvMessageGen *g, LpPrinter *printer)
{
	// No-op for lite.
	return 0;
}

// ===================================================================

static void ImmutableMessageLiteGenerator_GenerateInterface(JvMessageGen *g, LpPrinter *printer)
{
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor,
					     /* immutable = */ true, "OrBuilder");

	LpVars variables = { 0 };
	lp_vars_set(&variables, "{", "");
	lp_vars_set(&variables, "}", "");
	lp_vars_set(&variables, "deprecation", OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "");
	lp_vars_set(&variables, "extra_interfaces", jv_extra_message_or_builder_interfaces(g->descriptor));
	lp_vars_set(&variables, "classname", (g->descriptor->name));

	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoNonnullApi\n", NULL);
	}
	if (g->descriptor->n_extension_ranges > 0) {
		lp_print_vars(printer, &variables,
			      "$deprecation$public interface ${$$classname$OrBuilder$}$ extends \n"
			      "    $extra_interfaces$\n"
			      "     com.google.protobuf.GeneratedMessageLite.\n"
			      "          ExtendableMessageOrBuilder<\n"
			      "              $classname$, $classname$.Builder> {\n");
	} else {
		lp_print_vars(printer, &variables,
			      "$deprecation$public interface ${$$classname$OrBuilder$}$ extends\n"
			      "    $extra_interfaces$\n"
			      "    com.google.protobuf.MessageLiteOrBuilder {\n");
	}
	lp_indent(printer);
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		lp_print(printer, "\n", NULL);
		JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateInterfaceMembers, printer);
	}
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		const LpOneof *oneof = g->oneofs[kv_i];
		lp_vars_set(&variables, "oneof_capitalized_name",
			    jv_context_oneof_info(g->context, oneof)->capitalized_name);
		lp_vars_set(&variables, "classname", jv_immutable_class_name(g->descriptor));
		lp_print_vars(printer, &variables,
			      "\n"
			      "public ${$$classname$.$oneof_capitalized_name$Case$}$ "
			      "get$oneof_capitalized_name$Case();\n");
	}
	lp_outdent(printer);

	lp_print(printer, "}\n", NULL);
}

// ===================================================================

static void ImmutableMessageLiteGenerator_Generate(JvMessageGen *g, LpPrinter *printer)
{
	bool is_own_file = jv_is_own_file_(g->descriptor, /* immutable = */ true);

	LpVars variables = { 0 };
	lp_vars_set(&variables, "{", "");
	lp_vars_set(&variables, "}", "");
	lp_vars_set(&variables, "static", is_own_file ? " " : " static ");
	lp_vars_set(&variables, "classname", (g->descriptor->name));
	lp_vars_set(&variables, "extra_interfaces", jv_extra_message_interfaces(g->descriptor));
	lp_vars_set(&variables, "deprecation", OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "");

	jv_write_message_doc_comment(printer, g->descriptor, &g->context->options, false);
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor,
					     /* immutable = */ true);

	// The builder_type stores the super type name of the nested Builder class.
	const char *builder_type;
	if (g->descriptor->n_extension_ranges > 0) {
		lp_print_vars(printer, &variables,
			      "$deprecation$public $static$final class ${$$classname$$}$ extends\n"
			      "    com.google.protobuf.GeneratedMessageLite.ExtendableMessage<\n"
			      "      $classname$, $classname$.Builder> implements\n"
			      "    $extra_interfaces$\n"
			      "    $classname$OrBuilder {\n");
		builder_type = xcat("com.google.protobuf.GeneratedMessageLite.ExtendableBuilder<",
				    jv_immutable_class_name(g->descriptor), ", ?>");
		(void)builder_type;
	} else {
		lp_print_vars(printer, &variables,
			      "$deprecation$public $static$final class ${$$classname$$}$ extends\n"
			      "    com.google.protobuf.GeneratedMessageLite<\n"
			      "        $classname$, $classname$.Builder> implements\n"
			      "    $extra_interfaces$\n"
			      "    $classname$OrBuilder {\n");

		builder_type = "com.google.protobuf.GeneratedMessageLite.Builder";
	}
	lp_indent(printer);

	ImmutableMessageLiteGenerator_GenerateConstructor(g, printer);

	// Nested types
	for (int i = 0; i < g->descriptor->n_enums; i++) {
		EnumLiteGenerator_Generate(new_enum_gen((&g->descriptor->enums[i]), true, g->context), printer);
	}

	for (int i = 0; i < g->descriptor->n_nested; i++) {
		// Don't generate Java classes for map entry messages.
		if (jv_is_map_entry((&g->descriptor->nested[i])))
			continue;
		JvMessageGen *messageGenerator = new_message_gen((&g->descriptor->nested[i]), g->context, true);
		ImmutableMessageLiteGenerator_GenerateInterface(messageGenerator, printer);
		ImmutableMessageLiteGenerator_Generate(messageGenerator, printer);
	}

	// Integers for bit fields.
	int totalBits = 0;
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		totalBits += JV_FGCALL0(g, (&g->descriptor->fields[i]), GetNumBitsForMessage);
	}
	int totalInts = (totalBits + 31) / 32;
	for (int i = 0; i < totalInts; i++) {
		lp_print(printer, "private int $bit_field_name$;\n", "bit_field_name", jv_get_bit_field_name(i), NULL);
	}

	// oneof
	LpVars vars = { 0 };
	lp_vars_set(&vars, "{", "");
	lp_vars_set(&vars, "}", "");
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		const LpOneof *oneof = g->oneofs[kv_i];
		lp_vars_set(&vars, "oneof_name", jv_context_oneof_info(g->context, oneof)->name);
		lp_vars_set(&vars, "oneof_capitalized_name",
			    jv_context_oneof_info(g->context, oneof)->capitalized_name);
		lp_vars_set(&vars, "oneof_index", xcat(jv_i((oneof)->index)));
		if (true) {
			// oneofCase_ and oneof_
			lp_print_vars(printer, &vars,
				      "private int $oneof_name$Case_ = 0;\n"
				      "private java.lang.Object $oneof_name$_;\n");
		}
		// OneofCase enum
		lp_print_vars(printer, &vars,
			      "public enum ${$$oneof_capitalized_name$Case$}$\n"
			      "    implements "
			      "com.google.protobuf.AbstractMessageLite.InternalOneOfEnum {\n");
		lp_indent(printer);
		for (int j = 0; j < (oneof)->n_fields; j++) {
			const LpField *field = ONEOF_FIELD((oneof), j);
			lp_print(printer, "$field_name$($field_number$),\n", "field_name", jv_upper(field->name),
				 "field_number", xcat(jv_i(field->number)), NULL);
		}
		lp_print(printer, "$cap_oneof_name$_NOT_SET(0);\n", "cap_oneof_name",
			 jv_upper(lp_vars_get(&vars, "oneof_name")), NULL);
		lp_print_vars(printer, &vars,
			      "private final int value;\n"
			      "private $oneof_capitalized_name$Case(int value) {\n"
			      "  this.value = value;\n"
			      "}\n");
		if (true) {
			lp_print_vars(printer, &vars,
				      "/**\n"
				      " * @deprecated Use {@link #forNumber(int)} instead.\n"
				      " */\n"
				      "@java.lang.Deprecated\n"
				      "public static $oneof_capitalized_name$Case valueOf(int value) {\n"
				      "  return forNumber(value);\n"
				      "}\n"
				      "\n");
		}
		if (!JV_IS_OSS) {
			lp_print(printer, "@com.google.protobuf.Internal.ProtoMethodMayReturnNull\n", NULL);
		}
		lp_print_vars(printer, &vars,
			      "public static $oneof_capitalized_name$Case forNumber(int value) {\n"
			      "  switch (value) {\n");
		for (int j = 0; j < (oneof)->n_fields; j++) {
			const LpField *field = ONEOF_FIELD((oneof), j);
			lp_print(printer, "    case $field_number$: return $field_name$;\n", "field_number",
				 xcat(jv_i(field->number)), "field_name", jv_upper(field->name), NULL);
		}
		lp_print(printer,
			 "    case 0: return $cap_oneof_name$_NOT_SET;\n"
			 "    default: return null;\n"
			 "  }\n"
			 "}\n"
			 // TODO: Rename this to "getFieldNumber" or something to
			 // disambiguate it from actual proto enums.
			 "@java.lang.Override public int getNumber() {\n"
			 "  return this.value;\n"
			 "}\n",
			 "cap_oneof_name", jv_upper(lp_vars_get(&vars, "oneof_name")), NULL);
		lp_outdent(printer);
		lp_print(printer, "};\n\n", NULL);
		// oneofCase()
		lp_print_vars(printer, &vars,
			      "@java.lang.Override\n"
			      "public $oneof_capitalized_name$Case\n"
			      "${$get$oneof_capitalized_name$Case$}$() {\n"
			      "  return $oneof_capitalized_name$Case.forNumber(\n"
			      "      $oneof_name$Case_);\n"
			      "}\n");
		lp_print_vars(printer, &vars,
			      "\n"
			      "private void ${$clear$oneof_capitalized_name$$}$() {\n"
			      "  $oneof_name$Case_ = 0;\n"
			      "  $oneof_name$_ = null;\n"
			      "}\n"
			      "\n");
	}

	// Fields
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		lp_print(printer, "public static final int $constant_name$ = $number$;\n", "constant_name",
			 jv_field_constant_name((&g->descriptor->fields[i])), "number",
			 xcat(jv_i((&g->descriptor->fields[i])->number)), NULL);
		JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateMembers, printer);
		lp_print(printer, "\n", NULL);
	}

	ImmutableMessageLiteGenerator_GenerateParseFromMethods(g, printer);
	ImmutableMessageLiteGenerator_GenerateBuilder(g, printer);

	if (jv_has_required_fields(g->descriptor)) {
		// Memoizes whether the protocol buffer is fully initialized (has all
		// required fields). 0 means false, 1 means true, and all other values
		// mean not yet computed.
		lp_print(printer, "private byte memoizedIsInitialized = 2;\n", NULL);
	}

	lp_print(printer,
		 "@java.lang.Override\n"
		 "@java.lang.SuppressWarnings({\"ThrowNull\"})\n"
		 "protected final java.lang.Object dynamicMethod(\n"
		 "    com.google.protobuf.GeneratedMessageLite.MethodToInvoke method,\n"
		 "    java.lang.Object arg0, java.lang.Object arg1) {\n"
		 "  switch (method) {\n"
		 "    case NEW_MUTABLE_INSTANCE: {\n"
		 "      return new $classname$();\n"
		 "    }\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	lp_indent(printer);
	lp_indent(printer);

	lp_print(printer, "case NEW_BUILDER: {\n", NULL);

	lp_indent(printer);
	ImmutableMessageLiteGenerator_GenerateDynamicMethodNewBuilder(g, printer);
	lp_outdent(printer);

	lp_print(printer,
		 "}\n"
		 "case BUILD_MESSAGE_INFO: {\n",
		 NULL);

	lp_indent(printer);
	ImmutableMessageLiteGenerator_GenerateDynamicMethodNewBuildMessageInfo(g, printer);
	lp_outdent(printer);

	lp_print(printer,
		 "}\n"
		 "case GET_DEFAULT_INSTANCE: {\n"
		 "  return DEFAULT_INSTANCE;\n"
		 "}\n"
		 "case GET_PARSER: {\n"
		 // Generally one would use the lazy initialization holder pattern for
		 // manipulating static fields but that has exceptional cost on Android as
		 // it will generate an extra class for every message. Instead, use the
		 // double-check locking pattern which works just as well.
		 //
		 // The "parser" temporary mirrors the "PARSER" field to eliminate a read
		 // at the final return statement.
		 "  com.google.protobuf.Parser<$classname$> parser = PARSER;\n"
		 "  if (parser == null) {\n"
		 "    synchronized ($classname$.class) {\n"
		 "      parser = PARSER;\n"
		 "      if (parser == null) {\n"
		 "        parser =\n"
		 "            new DefaultInstanceBasedParser<$classname$>(\n"
		 "                DEFAULT_INSTANCE);\n"
		 "        PARSER = parser;\n"
		 "      }\n"
		 "    }\n"
		 "  }\n"
		 "  return parser;\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	if (jv_has_required_fields(g->descriptor)) {
		lp_print(printer,
			 "}\n"
			 "case GET_MEMOIZED_IS_INITIALIZED: {\n"
			 "  return memoizedIsInitialized;\n"
			 "}\n"
			 "case SET_MEMOIZED_IS_INITIALIZED: {\n"
			 "  memoizedIsInitialized = (byte) (arg0 == null ? 0 : 1);\n"
			 "  return null;\n"
			 "}\n",
			 NULL);
	} else {
		lp_print(printer,
			 "}\n"
			 "case GET_MEMOIZED_IS_INITIALIZED: {\n"
			 "  return (byte) 1;\n"
			 "}\n"
			 "// SET_MEMOIZED_IS_INITIALIZED is never called for this message.\n"
			 "// So it can do anything. Combine with default case for smaller "
			 "codegen.\n"
			 "case SET_MEMOIZED_IS_INITIALIZED:\n",
			 NULL);
	}

	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "// Should never happen. Generates tight code to throw an exception.\n"
		 "throw null;\n",
		 NULL);
	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);

	lp_print(printer,
		 "\n"
		 "// @@protoc_insertion_point(class_scope:$full_name$)\n",
		 "full_name", g->descriptor->full_name, NULL);

	// Carefully initialize the default instance in such a way that it doesn't
	// conflict with other initialization.
	lp_print(printer, "private static final $classname$ DEFAULT_INSTANCE;\n", "classname",
		 jv_immutable_class_name(g->descriptor), NULL);

	lp_print(printer,
		 "static {\n"
		 "  $classname$ defaultInstance = new $classname$();\n"
		 "  // New instances are implicitly immutable so no need to make\n"
		 "  // immutable.\n"
		 "  DEFAULT_INSTANCE = defaultInstance;\n"
		 // Register the default instance in a map. This map will be used by
		 // experimental runtime to lookup default instance given a class instance
		 // without using Java reflection.
		 "  com.google.protobuf.GeneratedMessageLite.registerDefaultInstance(\n"
		 "    $classname$.class, defaultInstance);\n"
		 "}\n"
		 "\n",
		 "classname", g->descriptor->name, NULL);

	lp_print(printer,
		 "public static $classname$ getDefaultInstance() {\n"
		 "  return DEFAULT_INSTANCE;\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	// 'of' method for Wrappers
	if (jv_is_wrappers_proto_file(g->descriptor->file)) {
		lp_print(printer,
			 "public static $classname$ of($field_type$ value) {\n"
			 "  return newBuilder().setValue(value).build();\n"
			 "}\n"
			 "\n",
			 "classname", jv_immutable_class_name(g->descriptor), "field_type",
			 jv_primitive_type_name(jv_get_java_type((&g->descriptor->fields[0]))), NULL);
	}

	ImmutableMessageLiteGenerator_GenerateParser(g, printer);

	// Extensions must be declared after the DEFAULT_INSTANCE is initialized
	// because the DEFAULT_INSTANCE is used by the extension to lazily retrieve
	// the outer class's FileDescriptor.
	for (int i = 0; i < g->descriptor->n_extensions; i++) {
		ImmutableExtensionLiteGenerator_Generate(new_extension_gen((&g->descriptor->extensions[i]), g->context),
							 printer);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static void ImmutableMessageLiteGenerator_GenerateDynamicMethodNewBuildMessageInfo(JvMessageGen *g, LpPrinter *printer)
{
	lp_indent(printer);

	// Collect field info into a sequence of UTF-16 chars. It will be embedded
	// as a Java string in the generated code.
	JvUtf16 chars = { 0 };

	int flags = 0;
	if (OPT_message_set_wire_format(g->descriptor)) {
		flags |= 0x2;
	}
	if (!g->context->options.strip_nonfunctional_codegen) {
		if (g->descriptor->file->edition == LP_EDITION_PROTO2) {
			flags |= 0x1;
		} else if (g->descriptor->file->edition >= LP_EDITION_2023) {
			flags |= 0x4;
		}
	}

	jv_write_int_to_utf16_char_sequence(flags, &chars);
	jv_write_int_to_utf16_char_sequence(g->descriptor->n_fields, &chars);

	if (g->descriptor->n_fields == 0) {
		lp_print(printer, "java.lang.Object[] objects = null;\n", NULL);
	} else {
		// A single array of all fields (including oneof, oneofCase, hasBits).
		lp_print(printer, "java.lang.Object[] objects = new java.lang.Object[] {\n", NULL);
		lp_indent(printer);

		// Record the number of oneofs.
		jv_write_int_to_utf16_char_sequence(g->n_oneofs, &chars);
		for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
			lp_print(printer,
				 "\"$oneof_name$_\",\n"
				 "\"$oneof_name$Case_\",\n",
				 "oneof_name", jv_context_oneof_info(g->context, g->oneofs[kv_i])->name, NULL);
		}

		// Integers for bit fields.
		int total_bits = 0;
		for (int i = 0; i < g->descriptor->n_fields; i++) {
			total_bits += JV_FGCALL0(g, (&g->descriptor->fields[i]), GetNumBitsForMessage);
		}
		int total_ints = (total_bits + 31) / 32;
		for (int i = 0; i < total_ints; i++) {
			lp_print(printer, "\"$bit_field_name$\",\n", "bit_field_name", jv_get_bit_field_name(i), NULL);
		}
		jv_write_int_to_utf16_char_sequence(total_ints, &chars);

		int map_count = 0;
		int repeated_count = 0;
		const LpField **sorted_fields = jv_sort_fields_by_number(g->descriptor);
		for (int i = 0; i < g->descriptor->n_fields; i++) {
			const LpField *field = sorted_fields[i];
			if (proto_field_is_map(field)) {
				map_count++;
			} else if (proto_field_is_repeated(field)) {
				repeated_count++;
			}
		}

		jv_write_int_to_utf16_char_sequence(sorted_fields[0]->number, &chars);
		jv_write_int_to_utf16_char_sequence(sorted_fields[g->descriptor->n_fields - 1]->number, &chars);
		jv_write_int_to_utf16_char_sequence(g->descriptor->n_fields, &chars);
		jv_write_int_to_utf16_char_sequence(map_count, &chars);
		jv_write_int_to_utf16_char_sequence(repeated_count, &chars);

		Vec fields_for_is_initialized_check = { 0 };
		for (int i = 0; i < g->descriptor->n_fields; i++) {
			if (proto_field_is_required((&g->descriptor->fields[i])) ||
			    (jv_get_java_type((&g->descriptor->fields[i])) == JAVATYPE_MESSAGE &&
			     jv_has_required_fields((&g->descriptor->fields[i])->message_type))) {
				vec_push(&fields_for_is_initialized_check, (void *)(&g->descriptor->fields[i]));
			}
		}
		jv_write_int_to_utf16_char_sequence(fields_for_is_initialized_check.n, &chars);

		for (int i = 0; i < g->descriptor->n_fields; i++) {
			const LpField *field = sorted_fields[i];
			JV_FGCALL(g, field, GenerateFieldInfo, printer, &chars);
		}
		lp_outdent(printer);
		lp_print(printer, "};\n", NULL);
	}

	lp_print(printer, "java.lang.String info =\n", NULL);
	Buf line = { 0 };
	for (int i = 0; i < chars.n; i++) {
		uint16_t code = chars.v[i];
		jv_escape_utf16_to_string(code, &line);
		if (line.n >= 80) {
			lp_print(printer, "    \"$string$\" +\n", "string", buf_str(&line), NULL);
			line.n = 0;
			line.p[0] = 0;
		}
	}
	lp_print(printer, "    \"$string$\";\n", "string", buf_str(&line), NULL);

	lp_print(printer, "return newMessageInfo(DEFAULT_INSTANCE, info, objects);\n", NULL);
	lp_outdent(printer);
}

// ===================================================================

static void ImmutableMessageLiteGenerator_GenerateParseFromMethods(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public static $classname$ parseFrom(\n"
		 "    java.nio.ByteBuffer data)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, data);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    java.nio.ByteBuffer data,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, data, extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.ByteString data)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, data);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.ByteString data,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, data, extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(byte[] data)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, data);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    byte[] data,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, data, extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(java.io.InputStream input)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, input);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    java.io.InputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, input, extensionRegistry);\n"
		 "}\n"
		 "$parsedelimitedreturnannotation$\n"
		 "public static $classname$ parseDelimitedFrom(java.io.InputStream "
		 "input)\n"
		 "    throws java.io.IOException {\n"
		 "  return parseDelimitedFrom(DEFAULT_INSTANCE, input);\n"
		 "}\n"
		 "$parsedelimitedreturnannotation$\n"
		 "public static $classname$ parseDelimitedFrom(\n"
		 "    java.io.InputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  return parseDelimitedFrom(DEFAULT_INSTANCE, input, "
		 "extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.CodedInputStream input)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, input);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.CodedInputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessageLite.parseFrom(\n"
		 "      DEFAULT_INSTANCE, input, extensionRegistry);\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), "parsedelimitedreturnannotation",
		 JV_IS_OSS ? "" : "@com.google.protobuf.Internal.ProtoMethodMayReturnNull", NULL);
}

// ===================================================================

static void ImmutableMessageLiteGenerator_GenerateBuilder(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "public static Builder newBuilder() {\n"
		 "  return (Builder) DEFAULT_INSTANCE.createBuilder();\n"
		 "}\n"
		 "public static Builder newBuilder($classname$ prototype) {\n"
		 "  return DEFAULT_INSTANCE.createBuilder(prototype);\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	JvMessageGen *builderGenerator = new_message_gen(g->descriptor, g->context, true);
	MessageBuilderLiteGenerator_Generate(builderGenerator, printer);
}

// ===================================================================

static void ImmutableMessageLiteGenerator_GenerateDynamicMethodNewBuilder(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer, "return new Builder();\n", NULL);
}

// ===================================================================

static void ImmutableMessageLiteGenerator_GenerateExtensionRegistrationCode(JvMessageGen *g, LpPrinter *printer)
{
	for (int i = 0; i < g->descriptor->n_extensions; i++) {
		ImmutableExtensionLiteGenerator_GenerateRegistrationCode(
			new_extension_gen((&g->descriptor->extensions[i]), g->context), printer);
	}

	for (int i = 0; i < g->descriptor->n_nested; i++) {
		ImmutableMessageLiteGenerator_GenerateExtensionRegistrationCode(
			new_message_gen((&g->descriptor->nested[i]), g->context, true), printer);
	}
}

// ===================================================================
static void ImmutableMessageLiteGenerator_GenerateConstructor(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer, "private $classname$() {\n", "classname", g->descriptor->name, NULL);
	lp_indent(printer);

	// Initialize all fields to default.
	ImmutableMessageLiteGenerator_GenerateInitializers(g, printer);

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);
}

// ===================================================================
static void ImmutableMessageLiteGenerator_GenerateParser(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "private static volatile com.google.protobuf.Parser<$classname$> "
		 "PARSER;\n"
		 "\n"
		 "public static com.google.protobuf.Parser<$classname$> parser() {\n"
		 "  return DEFAULT_INSTANCE.getParserForType();\n"
		 "}\n",
		 "classname", g->descriptor->name, NULL);
}

// ===================================================================
static void ImmutableMessageLiteGenerator_GenerateInitializers(JvMessageGen *g, LpPrinter *printer)
{
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		if (!jv_is_real_oneof((&g->descriptor->fields[i]))) {
			JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateInitializationCode, printer);
		}
	}
}
