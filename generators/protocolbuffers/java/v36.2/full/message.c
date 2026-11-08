// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/message.{h,cc} (protobuf v36.2), with message_serialization.h. */

static char *MapValueImmutableClassdName_message(const LpMessage *descriptor);
static void ImmutableMessageGenerator_GenerateStaticVariables(JvMessageGen *g, LpPrinter *printer,
							      int *bytecode_estimate);
static int ImmutableMessageGenerator_GenerateStaticVariableInitializers(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateFieldAccessorTable(JvMessageGen *g, LpPrinter *printer,
								 int *bytecode_estimate);
static int ImmutableMessageGenerator_GenerateFieldAccessorTableInitializer(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateInterface(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_Generate(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateMessageSerializationMethods(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateParseFromMethods(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateBuilder(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateDescriptorMethods(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateIsInitialized(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateEqualsAndHashCode(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateExtensionRegistrationCode(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateParser(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateInitializers(JvMessageGen *g, LpPrinter *printer);
static void ImmutableMessageGenerator_GenerateAnyMethods(JvMessageGen *g, LpPrinter *printer);

static char *MapValueImmutableClassdName_message(const LpMessage *descriptor)
{
	const LpField *value_field = jv_map_value_of(descriptor);
	JV_CHECK((PT_MESSAGE) == (value_field->type));
	return jv_immutable_class_name(value_field->message_type);
}
// ===================================================================

/* MessageGenerator::MessageGenerator: generator_factory.c */

// ===================================================================
/* ImmutableMessageGenerator::ImmutableMessageGenerator: generator_factory.c */

static void ImmutableMessageGenerator_GenerateStaticVariables(JvMessageGen *g, LpPrinter *printer,
							      int *bytecode_estimate)
{
	// Because descriptor.proto (com.google.protobuf.DescriptorProtos) is
	// used in the construction of descriptors, we have a tricky bootstrapping
	// problem.  To help control static initialization order, we make sure all
	// descriptors and other static data that depends on them are members of
	// the outermost class in the file.  This way, they will be initialized in
	// a deterministic order.

	LpVars vars = { 0 };
	lp_vars_set(&vars, "identifier", jv_unique_file_scope_identifier(g->descriptor));
	lp_vars_set(&vars, "index", xcat(jv_i(g->descriptor->index)));
	lp_vars_set(&vars, "classname", jv_immutable_class_name(g->descriptor));
	if (g->descriptor->containing_type != NULL) {
		lp_vars_set(&vars, "parent", jv_unique_file_scope_identifier(g->descriptor->containing_type));
	}
	if (jv_nested_in_file_class_(*g->descriptor)) {
		lp_vars_set(&vars, "private", "private ");
	} else {
		// We can only make these package-private since the classes that use them
		// are in separate files.
		lp_vars_set(&vars, "private", "");
	}
	if (*bytecode_estimate <= JV_MAX_STATIC_SIZE) {
		lp_vars_set(&vars, "final", "final ");
	} else {
		lp_vars_set(&vars, "final", "");
	}

	// The descriptor for this type.
	lp_print_vars(printer, &vars,
		      // TODO: final needs to be added back. The way to fix it is to
		      // generate methods that can construct the types, and then still declare
		      // the types, and then init them in clinit with the new method calls.
		      "$private$static $final$com.google.protobuf.Descriptors.Descriptor\n"
		      "  internal_$identifier$_descriptor;\n");
	*bytecode_estimate += 30;

	// And the FieldAccessorTable.
	ImmutableMessageGenerator_GenerateFieldAccessorTable(g, printer, bytecode_estimate);

	// Generate static members for all nested types.
	for (int i = 0; i < g->descriptor->n_nested; i++) {
		// TODO:  Reuse MessageGenerator objects?
		ImmutableMessageGenerator_GenerateStaticVariables(
			new_message_gen((&g->descriptor->nested[i]), g->context, false), printer, bytecode_estimate);
	}
}

static int ImmutableMessageGenerator_GenerateStaticVariableInitializers(JvMessageGen *g, LpPrinter *printer)
{
	int bytecode_estimate = 0;
	LpVars vars = { 0 };
	lp_vars_set(&vars, "identifier", jv_unique_file_scope_identifier(g->descriptor));
	lp_vars_set(&vars, "index", xcat(jv_i(g->descriptor->index)));
	lp_vars_set(&vars, "classname", jv_immutable_class_name(g->descriptor));
	if (g->descriptor->containing_type != NULL) {
		lp_vars_set(&vars, "parent", jv_unique_file_scope_identifier(g->descriptor->containing_type));
	}

	// The descriptor for this type.
	if (g->descriptor->containing_type == NULL) {
		lp_print_vars(printer, &vars,
			      "internal_$identifier$_descriptor =\n"
			      "  getDescriptor().getMessageType($index$);\n");
		bytecode_estimate += 30;
	} else {
		lp_print_vars(printer, &vars,
			      "internal_$identifier$_descriptor =\n"
			      "  internal_$parent$_descriptor.getNestedType($index$);\n");
		bytecode_estimate += 30;
	}

	// And the FieldAccessorTable.
	bytecode_estimate += ImmutableMessageGenerator_GenerateFieldAccessorTableInitializer(g, printer);

	// Generate static member initializers for all nested types.
	for (int i = 0; i < g->descriptor->n_nested; i++) {
		// TODO:  Reuse MessageGenerator objects?
		bytecode_estimate += ImmutableMessageGenerator_GenerateStaticVariableInitializers(
			new_message_gen((&g->descriptor->nested[i]), g->context, false), printer);
	}
	return bytecode_estimate;
}

static void ImmutableMessageGenerator_GenerateFieldAccessorTable(JvMessageGen *g, LpPrinter *printer,
								 int *bytecode_estimate)
{
	LpVars vars = { 0 };
	lp_vars_set(&vars, "identifier", jv_unique_file_scope_identifier(g->descriptor));
	if (jv_nested_in_file_class_(*g->descriptor)) {
		lp_vars_set(&vars, "private", "private ");
	} else {
		// We can only make these package-private since the classes that use them
		// are in separate files.
		lp_vars_set(&vars, "private", "");
	}
	if (*bytecode_estimate <= JV_MAX_STATIC_SIZE) {
		lp_vars_set(&vars, "final", "final ");
	} else {
		lp_vars_set(&vars, "final", "");
	}
	lp_print_vars(printer, &vars,
		      "$private$static $final$\n"
		      "  com.google.protobuf.GeneratedMessage.FieldAccessorTable\n"
		      "    internal_$identifier$_fieldAccessorTable;\n");

	// The following bytecode_estimate calculation logic must stay in sync with
	// the similar logic in the GenerateFieldAccessorTableInitializer method below
	// to make sure that the generated static final fields are initialized  in the
	// static initialization block directly.
	//
	// 6 bytes per field and oneof
	*bytecode_estimate += 10 + 6 * g->descriptor->n_fields + 6 * g->descriptor->n_oneofs;
}

static int ImmutableMessageGenerator_GenerateFieldAccessorTableInitializer(JvMessageGen *g, LpPrinter *printer)
{
	int bytecode_estimate = 10;
	lp_print(printer,
		 "internal_$identifier$_fieldAccessorTable = new\n"
		 "  com.google.protobuf.GeneratedMessage.FieldAccessorTable(\n"
		 "    internal_$identifier$_descriptor,\n"
		 "    new java.lang.String[] { ",
		 "identifier", jv_unique_file_scope_identifier(g->descriptor), NULL);
	// All the bytecode_estimate calculation logic in this method must stay in
	// sync with the similar logic in the GenerateFieldAccessorTable method
	// above. See the corresponding comment in GenerateFieldAccessorTable for
	// details.
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);
		bytecode_estimate += 6;
		lp_print(printer, "\"$field_name$\", ", "field_name", info->capitalized_name, NULL);
	}

	for (int i = 0; i < g->descriptor->n_real_oneofs; i++) {
		const LpOneof *oneof = (&g->descriptor->oneofs[i]);
		const JvOneofGeneratorInfo *info = jv_context_oneof_info(g->context, oneof);
		bytecode_estimate += 6;
		lp_print(printer, "\"$oneof_name$\", ", "oneof_name", info->capitalized_name, NULL);
	}
	lp_print(printer, "});\n", NULL);
	return bytecode_estimate;
}

// ===================================================================

static void ImmutableMessageGenerator_GenerateInterface(JvMessageGen *g, LpPrinter *printer)
{
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor,
					     /* immutable = */ true, "OrBuilder");
	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoNonnullApi\n", NULL);
	}
	if (g->descriptor->n_extension_ranges > 0) {
		lp_print(printer,
			 "$deprecation$public interface ${$$classname$OrBuilder$}$ extends\n"
			 "    $extra_interfaces$\n"
			 "    com.google.protobuf.GeneratedMessage.\n"
			 "        ExtendableMessageOrBuilder<$classname$> {\n",
			 "deprecation", OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "",
			 "extra_interfaces", jv_extra_message_or_builder_interfaces(g->descriptor), "classname",
			 g->descriptor->name, "{", "", "}", "", NULL);
	} else {
		lp_print(printer,
			 "$deprecation$public interface ${$$classname$OrBuilder$}$ extends\n"
			 "    $extra_interfaces$\n"
			 "    com.google.protobuf.MessageOrBuilder {\n",
			 "deprecation", OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "",
			 "extra_interfaces", jv_extra_message_or_builder_interfaces(g->descriptor), "classname",
			 g->descriptor->name, "{", "", "}", "", NULL);
	}
	lp_indent(printer);
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		lp_print(printer, "\n", NULL);
		JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateInterfaceMembers, printer);
	}
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		lp_print(printer,
			 "\n"
			 "$classname$.$oneof_capitalized_name$Case "
			 "get$oneof_capitalized_name$Case();\n",
			 "oneof_capitalized_name", jv_context_oneof_info(g->context, g->oneofs[kv_i])->capitalized_name,
			 "classname", jv_immutable_class_name(g->descriptor), NULL);
	}
	lp_outdent(printer);

	lp_print(printer, "}\n", NULL);
}

// ===================================================================

static void ImmutableMessageGenerator_Generate(JvMessageGen *g, LpPrinter *printer)
{
	bool is_own_file = jv_is_own_file_(g->descriptor, /* immutable = */ true);

	LpVars variables = { 0 };
	lp_vars_set(&variables, "static", is_own_file ? "" : "static ");
	lp_vars_set(&variables, "classname", (g->descriptor->name));
	lp_vars_set(&variables, "extra_interfaces", jv_extra_message_interfaces(g->descriptor));
	lp_vars_set(&variables, "deprecation", OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "");

	jv_write_message_doc_comment(printer, g->descriptor, &g->context->options, false);
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor,
					     /* immutable = */ true);
	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoNonnullApi\n", NULL);
	}

	// The builder_type stores the super type name of the nested Builder class.
	const char *builder_type;
	if (g->descriptor->n_extension_ranges > 0) {
		lp_print_vars(printer, &variables, "$deprecation$public $static$final class $classname$ extends\n");
		lp_print_vars(printer, &variables,
			      "    com.google.protobuf.GeneratedMessage.ExtendableMessage<\n"
			      "      $classname$> implements\n"
			      "    $extra_interfaces$\n"
			      "    $classname$OrBuilder {\n");
		builder_type = xcat("com.google.protobuf.GeneratedMessage.ExtendableBuilder<",
				    jv_immutable_class_name(g->descriptor), ", ?>");
	} else {
		lp_print_vars(printer, &variables, "$deprecation$public $static$final class $classname$ extends\n");
		lp_print_vars(printer, &variables,
			      "    com.google.protobuf.GeneratedMessage implements\n"
			      "    $extra_interfaces$\n"
			      "    $classname$OrBuilder {\n");
		builder_type = "com.google.protobuf.GeneratedMessage.Builder<?>";
	}
	lp_print(printer, "private static final long serialVersionUID = 0L;\n", NULL);

	lp_indent(printer);

	lp_print(printer, "static {\n", NULL);
	lp_indent(printer);
	jv_print_gencode_version_validator(printer, true, g->descriptor->name);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);

	// Using builder_type, instead of Builder, prevents the Builder class from
	// being loaded into PermGen space when the default instance is created.
	// This optimizes the PermGen space usage for clients that do not modify
	// messages.
	lp_print(printer,
		 "// Use $classname$.newBuilder() to construct.\n"
		 "private $classname$($buildertype$ builder) {\n"
		 "  super(builder);\n"
		 "}\n",
		 "classname", g->descriptor->name, "buildertype", builder_type, NULL);
	lp_print(printer, "private $classname$() {\n", "classname", g->descriptor->name, NULL);
	lp_indent(printer);
	ImmutableMessageGenerator_GenerateInitializers(g, printer);
	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);

	ImmutableMessageGenerator_GenerateDescriptorMethods(g, printer);

	// Nested types
	for (int i = 0; i < g->descriptor->n_enums; i++) {
		EnumNonLiteGenerator_Generate(new_enum_gen((&g->descriptor->enums[i]), true, g->context), printer);
	}

	for (int i = 0; i < g->descriptor->n_nested; i++) {
		// Don't generate Java classes for map entry messages.
		if (jv_is_map_entry((&g->descriptor->nested[i])))
			continue;
		JvMessageGen *messageGenerator = new_message_gen((&g->descriptor->nested[i]), g->context, false);
		ImmutableMessageGenerator_GenerateInterface(messageGenerator, printer);
		ImmutableMessageGenerator_Generate(messageGenerator, printer);
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
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		const LpOneof *oneof = g->oneofs[kv_i];
		lp_vars_set(&vars, "oneof_name", jv_context_oneof_info(g->context, oneof)->name);
		lp_vars_set(&vars, "oneof_capitalized_name",
			    jv_context_oneof_info(g->context, oneof)->capitalized_name);
		lp_vars_set(&vars, "oneof_index", xcat(jv_i((oneof)->index)));
		lp_vars_set(&vars, "{", "");
		lp_vars_set(&vars, "}", "");
		// oneofCase_ and oneof_
		lp_print_vars(printer, &vars,
			      "private int $oneof_name$Case_ = 0;\n"
			      "@SuppressWarnings(\"serial\")\n"
			      "private java.lang.Object $oneof_name$_;\n");
		// OneofCase enum
		lp_print_vars(printer, &vars,
			      "public enum ${$$oneof_capitalized_name$Case$}$\n"
			      // TODO: Remove EnumLite when we want to break compatibility with
			      // 3.x users
			      "    implements com.google.protobuf.Internal.EnumLite,\n"
			      "        com.google.protobuf.AbstractMessage.InternalOneOfEnum {\n");
		lp_indent(printer);
		for (int j = 0; j < (oneof)->n_fields; j++) {
			const LpField *field = ONEOF_FIELD((oneof), j);
			lp_print(printer, "$deprecation$$field_name$($field_number$),\n", "deprecation",
				 OPT_deprecated(field) ? "@java.lang.Deprecated " : "", "field_name",
				 jv_upper(field->name), "field_number", xcat(jv_i(field->number)), NULL);
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
				      " * @param value The number of the enum to look for.\n"
				      " * @return The enum associated with the given number.\n"
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
			 "public int getNumber() {\n"
			 "  return this.value;\n"
			 "}\n",
			 "cap_oneof_name", jv_upper(lp_vars_get(&vars, "oneof_name")), NULL);
		lp_outdent(printer);
		lp_print(printer, "};\n\n", NULL);
		// oneofCase()
		lp_print_vars(printer, &vars,
			      "public $oneof_capitalized_name$Case\n"
			      "${$get$oneof_capitalized_name$Case$}$() {\n"
			      "  return $oneof_capitalized_name$Case.forNumber(\n"
			      "      $oneof_name$Case_);\n"
			      "}\n"
			      "\n");
	}

	if (jv_is_any_message(g->descriptor)) {
		ImmutableMessageGenerator_GenerateAnyMethods(g, printer);
	}

	// Fields
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		lp_print(printer, "public static final int $constant_name$ = $number$;\n", "constant_name",
			 jv_field_constant_name((&g->descriptor->fields[i])), "number",
			 xcat(jv_i((&g->descriptor->fields[i])->number)), NULL);
		JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateMembers, printer);
		lp_print(printer, "\n", NULL);
	}

	if (jv_context_has_generated_methods(g->context, g->descriptor)) {
		ImmutableMessageGenerator_GenerateIsInitialized(g, printer);
		ImmutableMessageGenerator_GenerateMessageSerializationMethods(g, printer);
		ImmutableMessageGenerator_GenerateEqualsAndHashCode(g, printer);
	}

	ImmutableMessageGenerator_GenerateParseFromMethods(g, printer);
	ImmutableMessageGenerator_GenerateBuilder(g, printer);

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
		 "  DEFAULT_INSTANCE = new $classname$();\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

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

	ImmutableMessageGenerator_GenerateParser(g, printer);

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public $classname$ getDefaultInstanceForType() {\n"
		 "  return DEFAULT_INSTANCE;\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	// Extensions must be declared after the DEFAULT_INSTANCE is initialized
	// because the DEFAULT_INSTANCE is used by the extension to lazily retrieve
	// the outer class's FileDescriptor.
	for (int i = 0; i < g->descriptor->n_extensions; i++) {
		ImmutableExtensionGenerator_Generate(new_extension_gen((&g->descriptor->extensions[i]), g->context),
						     printer);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

// ===================================================================

static void ImmutableMessageGenerator_GenerateMessageSerializationMethods(JvMessageGen *g, LpPrinter *printer)
{
	const LpField **sorted_fields = jv_sort_fields_by_number(g->descriptor);

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public void writeTo(com.google.protobuf.CodedOutputStream output)\n"
		 "                    throws java.io.IOException {\n",
		 NULL);
	lp_indent(printer);

	if (jv_has_packed_fields(g->descriptor)) {
		// writeTo(CodedOutputStream output) might be invoked without
		// getSerializedSize() ever being called, but we need the memoized
		// sizes in case this message has packed fields. Rather than emit checks
		// for each packed field, just call getSerializedSize() up front. In most
		// cases, getSerializedSize() will have already been called anyway by one
		// of the wrapper writeTo() methods, making this call cheap.
		lp_print(printer, "getSerializedSize();\n", NULL);
	}

	if (g->descriptor->n_extension_ranges > 0) {
		if (OPT_message_set_wire_format(g->descriptor)) {
			lp_print(printer,
				 "com.google.protobuf.GeneratedMessage\n"
				 "  .ExtendableMessage.ExtensionSerializer\n"
				 "    extensionWriter = newMessageSetExtensionSerializer();\n",
				 NULL);
		} else {
			lp_print(printer,
				 "com.google.protobuf.GeneratedMessage\n"
				 "  .ExtendableMessage.ExtensionSerializer\n"
				 "    extensionWriter = newExtensionSerializer();\n",
				 NULL);
		}
	}

	jv_generate_serialize_fields_and_extensions(printer, (const void *const *)g->field_generators, jv_fg_serialize,
						    g->descriptor, sorted_fields);

	if (OPT_message_set_wire_format(g->descriptor)) {
		lp_print(printer, "getUnknownFields().writeAsMessageSetTo(output);\n", NULL);
	} else {
		lp_print(printer, "getUnknownFields().writeTo(output);\n", NULL);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);

	// Chunk up the getSerializedSize() into chunks to be JIT friendly.
	const int kNumFieldsPerChunk = 32;
	int num_chunks = (g->descriptor->n_fields + kNumFieldsPerChunk - 1) / kNumFieldsPerChunk;
	for (int i = 0; i < num_chunks; ++i) {
		lp_print(printer,
			 "private int computeSerializedSize_$chunk$() {\n"
			 "  int size = 0;\n",
			 "chunk", xcat(jv_i(i)), NULL);
		lp_indent(printer);

		int chunk_field_start = i * kNumFieldsPerChunk;
		int chunk_field_end = JV_MIN(chunk_field_start + kNumFieldsPerChunk, g->descriptor->n_fields);

		for (int j = chunk_field_start; j < chunk_field_end; ++j) {
			JV_FGCALL(g, sorted_fields[j], GenerateSerializedSizeCode, printer);
		}
		lp_outdent(printer);
		lp_print(printer, "  return size;\n", NULL);
		lp_print(printer, "}\n", NULL);
	}

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public int getSerializedSize() {\n"
		 "  int size = memoizedSize;\n"
		 "  if (size != -1) return size;\n"
		 "\n",
		 NULL);
	lp_indent(printer);

	lp_print(printer, "size = 0;\n", NULL);

	for (int i = 0; i < num_chunks; ++i) {
		lp_print(printer, "size += computeSerializedSize_$chunk$();\n", "chunk", xcat(jv_i(i)), NULL);
	}

	if (g->descriptor->n_extension_ranges > 0) {
		if (OPT_message_set_wire_format(g->descriptor)) {
			lp_print(printer, "size += extensionsSerializedSizeAsMessageSet();\n", NULL);
		} else {
			lp_print(printer, "size += extensionsSerializedSize();\n", NULL);
		}
	}

	if (OPT_message_set_wire_format(g->descriptor)) {
		lp_print(printer, "size += getUnknownFields().getSerializedSizeAsMessageSet();\n", NULL);
	} else {
		lp_print(printer, "size += getUnknownFields().getSerializedSize();\n", NULL);
	}

	lp_print(printer,
		 "memoizedSize = size;\n"
		 "return size;\n",
		 NULL);

	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);
}

static void ImmutableMessageGenerator_GenerateParseFromMethods(JvMessageGen *g, LpPrinter *printer)
{
	// Note:  These are separate from GenerateMessageSerializationMethods()
	//   because they need to be generated even for messages that are optimized
	//   for code size.
	lp_print(printer,
		 "public static $classname$ parseFrom(\n"
		 "    java.nio.ByteBuffer data)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return PARSER.parseFrom(data);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    java.nio.ByteBuffer data,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return PARSER.parseFrom(data, extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.ByteString data)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return PARSER.parseFrom(data);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.ByteString data,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return PARSER.parseFrom(data, extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(byte[] data)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return PARSER.parseFrom(data);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    byte[] data,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "  return PARSER.parseFrom(data, extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(java.io.InputStream input)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessage\n"
		 "      .parseWithIOException(PARSER, input);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    java.io.InputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessage\n"
		 "      .parseWithIOException(PARSER, input, extensionRegistry);\n"
		 "}\n"
		 "$parsedelimitedreturnannotation$\n"
		 "public static $classname$ parseDelimitedFrom(java.io.InputStream "
		 "input)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessage\n"
		 "      .parseDelimitedWithIOException(PARSER, input);\n"
		 "}\n"
		 "$parsedelimitedreturnannotation$\n"
		 "public static $classname$ parseDelimitedFrom(\n"
		 "    java.io.InputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessage\n"
		 "      .parseDelimitedWithIOException(PARSER, input, "
		 "extensionRegistry);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.CodedInputStream input)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessage\n"
		 "      .parseWithIOException(PARSER, input);\n"
		 "}\n"
		 "public static $classname$ parseFrom(\n"
		 "    com.google.protobuf.CodedInputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  return com.google.protobuf.GeneratedMessage\n"
		 "      .parseWithIOException(PARSER, input, extensionRegistry);\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), "parsedelimitedreturnannotation",
		 JV_IS_OSS ? "" : "@com.google.protobuf.Internal.ProtoMethodMayReturnNull", NULL);
}

// ===================================================================

static void ImmutableMessageGenerator_GenerateBuilder(JvMessageGen *g, LpPrinter *printer)
{
	// LITE_RUNTIME implements this at the GeneratedMessageLite level.
	lp_print(printer,
		 "@java.lang.Override\n"
		 "public Builder newBuilderForType() { return newBuilder(); }\n",
		 NULL);

	lp_print(printer,
		 "public static Builder newBuilder() {\n"
		 "  return DEFAULT_INSTANCE.toBuilder();\n"
		 "}\n"
		 "public static Builder newBuilder($classname$ prototype) {\n"
		 "  return DEFAULT_INSTANCE.toBuilder().mergeFrom(prototype);\n"
		 "}\n"
		 "@java.lang.Override\n"
		 "public Builder toBuilder() {\n"
		 "  return this == DEFAULT_INSTANCE\n"
		 "      ? new Builder() : new Builder().mergeFrom(this);\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	lp_print(printer,
		 "@java.lang.Override\n"
		 "protected Builder newBuilderForType(\n"
		 "    com.google.protobuf.GeneratedMessage.BuilderParent parent) {\n"
		 "  Builder builder = new Builder(parent);\n"
		 "  return builder;\n"
		 "}\n",
		 NULL);

	JvMessageGen *builderGenerator = new_message_gen(g->descriptor, g->context, false);
	MessageBuilderGenerator_Generate(builderGenerator, printer);
}

static void ImmutableMessageGenerator_GenerateDescriptorMethods(JvMessageGen *g, LpPrinter *printer)
{
	if (!OPT_no_standard_descriptor_accessor(g->descriptor)) {
		lp_print(printer,
			 "public static final com.google.protobuf.Descriptors.Descriptor\n"
			 "    getDescriptor() {\n"
			 "  return $fileclass$.internal_$identifier$_descriptor;\n"
			 "}\n"
			 "\n",
			 "fileclass", jv_immutable_class_name(g->descriptor->file), "identifier",
			 jv_unique_file_scope_identifier(g->descriptor), NULL);
	}

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public com.google.protobuf.Descriptors.Descriptor "
		 "getDescriptorForType() {\n"
		 "  return $fileclass$.internal_$identifier$_descriptor;\n"
		 "}\n"
		 "\n",
		 "fileclass", jv_immutable_class_name(g->descriptor->file), "identifier",
		 jv_unique_file_scope_identifier(g->descriptor), NULL);

	Vec map_fields = { 0 };
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		if (jv_get_java_type(field) == JAVATYPE_MESSAGE && jv_is_map_entry(field->message_type)) {
			vec_push(&map_fields, (void *)field);
		}
	}
	if (map_fields.n) {
		lp_print(printer,
			 "@SuppressWarnings({\"rawtypes\"})\n"
			 "@java.lang.Override\n"
			 "protected com.google.protobuf.MapFieldReflectionAccessor "
			 "internalGetMapFieldReflection(\n"
			 "    int number) {\n"
			 "  switch (number) {\n",
			 NULL);
		lp_indent(printer);
		lp_indent(printer);
		for (int field_i = 0; field_i < map_fields.n; field_i++) {
			const LpField *field = VEC_AT(map_fields, LpField, field_i);
			const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);
			lp_print(printer,
				 "case $number$:\n"
				 "  return internalGet$capitalized_name$();\n",
				 "number", xcat(jv_i(field->number)), "capitalized_name", info->capitalized_name, NULL);
		}
		lp_print(printer,
			 "default:\n"
			 "  throw new RuntimeException(\n"
			 "      \"Invalid map field number: \" + number);\n",
			 NULL);
		lp_outdent(printer);
		lp_outdent(printer);
		lp_print(printer,
			 "  }\n"
			 "}\n",
			 NULL);
	}
	lp_print(printer,
		 "@java.lang.Override\n"
		 "protected com.google.protobuf.GeneratedMessage.FieldAccessorTable\n"
		 "    internalGetFieldAccessorTable() {\n"
		 "  return $fileclass$.internal_$identifier$_fieldAccessorTable\n"
		 "      .ensureFieldAccessorsInitialized(\n"
		 "          $classname$.class, $classname$.Builder.class);\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), "fileclass",
		 jv_immutable_class_name(g->descriptor->file), "identifier",
		 jv_unique_file_scope_identifier(g->descriptor), NULL);
}

// ===================================================================

static void ImmutableMessageGenerator_GenerateIsInitialized(JvMessageGen *g, LpPrinter *printer)
{
	// Memoizes whether the protocol buffer is fully initialized (has all
	// required fields). -1 means not yet computed. 0 means false and 1 means
	// true.
	if (true) {
		// Leave this as non-transient in OSS to avoid breaking customers that are
		// holding GSON wrong.
		// TODO: Remove this in a future PBJ breaking release.
		lp_print(printer, "private byte memoizedIsInitialized = -1;\n", NULL);
	} else {
		// If the message transitively has no required fields or extensions,
		// isInitialized() is always true.
		if (!jv_has_required_fields(g->descriptor)) {
			lp_print(printer,
				 "/**\n"
				 "  * @deprecated This always returns true for this type as it \n"
				 "  *   does not transitively contain any required fields.\n"
				 "  */\n"
				 "@java.lang.Deprecated\n"
				 "@java.lang.Override\n"
				 "public final boolean isInitialized() {\n"
				 "  return true;\n"
				 "}\n"
				 "\n",
				 NULL);
			return;
		}

		lp_print(printer, "private transient byte memoizedIsInitialized = -1;\n", NULL);
	}

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public final boolean isInitialized() {\n",
		 NULL);
	lp_indent(printer);

	// Don't directly compare to -1 to avoid an Android x86 JIT bug.
	lp_print(printer,
		 "byte isInitialized = memoizedIsInitialized;\n"
		 "if (isInitialized == 1) return true;\n"
		 "if (isInitialized == 0) return false;\n"
		 "\n",
		 NULL);

	// Check that all required fields in this message are set.
	// TODO:  We can optimize this when we switch to putting all the
	//   "has" fields into a single bitfield.
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);

		if (proto_field_is_required(field)) {
			lp_print(printer,
				 "if (!has$name$()) {\n"
				 "  memoizedIsInitialized = 0;\n"
				 "  return false;\n"
				 "}\n",
				 "name", info->capitalized_name, NULL);
		}
	}

	// Now check that all embedded messages are initialized.
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);
		if (jv_get_java_type(field) == JAVATYPE_MESSAGE && jv_has_required_fields(field->message_type)) {
			if (proto_field_is_required(field)) {
				lp_print(printer,
					 "if (!get$name$().isInitialized()) {\n"
					 "  memoizedIsInitialized = 0;\n"
					 "  return false;\n"
					 "}\n",
					 "type", jv_immutable_class_name(field->message_type), "name",
					 info->capitalized_name, NULL);
			} else if (proto_field_is_repeated(field)) {
				if (jv_is_map_entry(field->message_type)) {
					lp_print(printer,
						 "for ($type$ item : get$name$Map().values()) {\n"
						 "  if (!item.isInitialized()) {\n"
						 "    memoizedIsInitialized = 0;\n"
						 "    return false;\n"
						 "  }\n"
						 "}\n",
						 "type", MapValueImmutableClassdName_message(field->message_type),
						 "name", info->capitalized_name, NULL);
				} else {
					lp_print(printer,
						 "for (int i = 0; i < get$name$Count(); i++) {\n"
						 "  if (!get$name$(i).isInitialized()) {\n"
						 "    memoizedIsInitialized = 0;\n"
						 "    return false;\n"
						 "  }\n"
						 "}\n",
						 "type", jv_immutable_class_name(field->message_type), "name",
						 info->capitalized_name, NULL);
				}
			} else {
				lp_print(printer,
					 "if (has$name$()) {\n"
					 "  if (!get$name$().isInitialized()) {\n"
					 "    memoizedIsInitialized = 0;\n"
					 "    return false;\n"
					 "  }\n"
					 "}\n",
					 "name", info->capitalized_name, NULL);
			}
		}
	}

	if (g->descriptor->n_extension_ranges > 0) {
		lp_print(printer,
			 "if (!extensionsAreInitialized()) {\n"
			 "  memoizedIsInitialized = 0;\n"
			 "  return false;\n"
			 "}\n",
			 NULL);
	}

	lp_outdent(printer);

	lp_print(printer, "  memoizedIsInitialized = 1;\n", NULL);

	lp_print(printer,
		 "  return true;\n"
		 "}\n"
		 "\n",
		 NULL);
}

// ===================================================================

static void ImmutableMessageGenerator_GenerateEqualsAndHashCode(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "@java.lang.Override\n"
		 "public boolean equals(",
		 NULL);
	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoMethodAcceptsNullParameter\n", NULL);
	}
	lp_print(printer, "final java.lang.Object obj) {\n", NULL);
	lp_indent(printer);
	lp_print(printer,
		 "if (obj == this) {\n"
		 " return true;\n"
		 "}\n"
		 "if (!(obj instanceof $classname$)) {\n"
		 // don't simply return false because mutable and immutable types
		 // can be equal
		 "  return super.equals(obj);\n"
		 "}\n"
		 "$classname$ other = ($classname$) obj;\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		if (!jv_is_real_oneof(field)) {
			const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);
			if (proto_field_has_presence(field)) {
				lp_print(printer,
					 "if (has$name$() != other.has$name$()) return false;\n"
					 "if (has$name$()) {\n",
					 "name", info->capitalized_name, NULL);
				lp_indent(printer);
			}
			JV_FGCALL(g, field, GenerateEqualsCode, printer);
			if (proto_field_has_presence(field)) {
				lp_outdent(printer);
				lp_print(printer, "}\n", NULL);
			}
		}
	}

	// Compare oneofs.
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		const LpOneof *oneof = g->oneofs[kv_i];
		lp_print(printer,
			 "if (!get$oneof_capitalized_name$Case().equals("
			 "other.get$oneof_capitalized_name$Case())) return false;\n",
			 "oneof_capitalized_name", jv_context_oneof_info(g->context, oneof)->capitalized_name, NULL);
		lp_print(printer, "switch ($oneof_name$Case_) {\n", "oneof_name",
			 jv_context_oneof_info(g->context, oneof)->name, NULL);
		lp_indent(printer);
		for (int j = 0; j < (oneof)->n_fields; j++) {
			const LpField *field = ONEOF_FIELD((oneof), j);
			lp_print(printer, "case $field_number$:\n", "field_number", xcat(jv_i(field->number)), NULL);
			lp_indent(printer);
			JV_FGCALL(g, field, GenerateEqualsCode, printer);
			lp_print(printer, "break;\n", NULL);
			lp_outdent(printer);
		}
		lp_print(printer,
			 "case 0:\n"
			 "default:\n",
			 NULL);
		lp_outdent(printer);
		lp_print(printer, "}\n", NULL);
	}

	// Always consider unknown fields for equality. This will sometimes return
	// false for non-canonical ordering when running in LITE_RUNTIME but it's
	// the best we can do.
	lp_print(printer,
		 "if (!getUnknownFields().equals(other.getUnknownFields())) return "
		 "false;\n",
		 NULL);
	if (g->descriptor->n_extension_ranges > 0) {
		lp_print(printer, "if (!extensionsEquals(other)) return false;\n", NULL);
	}
	lp_print(printer, "return true;\n", NULL);
	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public int hashCode() {\n",
		 NULL);
	lp_indent(printer);
	lp_print(printer, "if (memoizedHashCode != 0) {\n", NULL);
	lp_indent(printer);
	lp_print(printer, "return memoizedHashCode;\n", NULL);
	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "int hash = 41;\n",
		 NULL);

	// If we output a getDescriptor() method, use that as it is more efficient.
	if (OPT_no_standard_descriptor_accessor(g->descriptor)) {
		lp_print(printer, "hash = (19 * hash) + getDescriptorForType().hashCode();\n", NULL);
	} else {
		lp_print(printer, "hash = (19 * hash) + getDescriptor().hashCode();\n", NULL);
	}

	// hashCode non-oneofs.
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		if (!jv_is_real_oneof(field)) {
			const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);
			if (proto_field_has_presence(field)) {
				lp_print(printer, "if (has$name$()) {\n", "name", info->capitalized_name, NULL);
				lp_indent(printer);
			}
			JV_FGCALL(g, field, GenerateHashCode, printer);
			if (proto_field_has_presence(field)) {
				lp_outdent(printer);
				lp_print(printer, "}\n", NULL);
			}
		}
	}

	// hashCode oneofs.
	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		const LpOneof *oneof = g->oneofs[kv_i];
		lp_print(printer, "switch ($oneof_name$Case_) {\n", "oneof_name",
			 jv_context_oneof_info(g->context, oneof)->name, NULL);
		lp_indent(printer);
		for (int j = 0; j < (oneof)->n_fields; j++) {
			const LpField *field = ONEOF_FIELD((oneof), j);
			lp_print(printer, "case $field_number$:\n", "field_number", xcat(jv_i(field->number)), NULL);
			lp_indent(printer);
			JV_FGCALL(g, field, GenerateHashCode, printer);
			lp_print(printer, "break;\n", NULL);
			lp_outdent(printer);
		}
		lp_print(printer,
			 "case 0:\n"
			 "default:\n",
			 NULL);
		lp_outdent(printer);
		lp_print(printer, "}\n", NULL);
	}

	if (g->descriptor->n_extension_ranges > 0) {
		lp_print(printer, "hash = hashFields(hash, getExtensionFields());\n", NULL);
	}

	lp_print(printer, "hash = (29 * hash) + getUnknownFields().hashCode();\n", NULL);
	lp_print(printer,
		 "memoizedHashCode = hash;\n"
		 "return hash;\n",
		 NULL);
	lp_outdent(printer);
	lp_print(printer,
		 "}\n"
		 "\n",
		 NULL);
}

// ===================================================================

static void ImmutableMessageGenerator_GenerateExtensionRegistrationCode(JvMessageGen *g, LpPrinter *printer)
{
	for (int i = 0; i < g->descriptor->n_extensions; i++) {
		ImmutableExtensionGenerator_GenerateRegistrationCode(
			new_extension_gen((&g->descriptor->extensions[i]), g->context), printer);
	}

	for (int i = 0; i < g->descriptor->n_nested; i++) {
		ImmutableMessageGenerator_GenerateExtensionRegistrationCode(
			new_message_gen((&g->descriptor->nested[i]), g->context, false), printer);
	}
}

// ===================================================================
static void ImmutableMessageGenerator_GenerateParser(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "private static final com.google.protobuf.Parser<$classname$>\n"
		 "    PARSER = new com.google.protobuf.AbstractParser<$classname$>() {\n"
		 "  @java.lang.Override\n"
		 "  public $classname$ parsePartialFrom(\n"
		 "      com.google.protobuf.CodedInputStream input,\n"
		 "      com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "      throws com.google.protobuf.InvalidProtocolBufferException {\n"
		 "    Builder builder = newBuilder();\n"
		 "    try {\n"
		 "      builder.mergeFrom(input, extensionRegistry);\n"
		 // clang-format off
    "    } catch (com.google.protobuf.InvalidProtocolBufferException e) {\n"
    "      throw e.setUnfinishedMessage(builder.buildPartial());\n"
    "    } catch (com.google.protobuf.UninitializedMessageException e) {\n"
    "      throw "
    "e.asInvalidProtocolBufferException().setUnfinishedMessage(builder."
    "buildPartial());\n"
    "    } catch (java.io.IOException e) {\n"
    "      throw new com.google.protobuf.InvalidProtocolBufferException(e)\n"
    "          .setUnfinishedMessage(builder.buildPartial());\n"
		 // clang-format on
		 "    }\n"
		 "    return builder.buildPartial();\n"
		 "  }\n"
		 "};\n"
		 "\n"
		 "public static com.google.protobuf.Parser<$classname$> parser() {\n"
		 "  return PARSER;\n"
		 "}\n"
		 "\n"
		 "@java.lang.Override\n"
		 "public com.google.protobuf.Parser<$classname$> getParserForType() {\n"
		 "  return PARSER;\n"
		 "}\n"
		 "\n",
		 "classname", g->descriptor->name, NULL);
}

// ===================================================================
static void ImmutableMessageGenerator_GenerateInitializers(JvMessageGen *g, LpPrinter *printer)
{
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		if (!jv_is_real_oneof((&g->descriptor->fields[i]))) {
			JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateInitializationCode, printer);
		}
	}
}

static void ImmutableMessageGenerator_GenerateAnyMethods(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "private static String getTypeUrl(\n"
		 "    java.lang.String typeUrlPrefix,\n"
		 "    com.google.protobuf.Descriptors.Descriptor descriptor) {\n"
		 "  return typeUrlPrefix.endsWith(\"/\")\n"
		 "      ? typeUrlPrefix + descriptor.getFullName()\n"
		 "      : typeUrlPrefix + \"/\" + descriptor.getFullName();\n"
		 "}\n"
		 "\n"
		 "private static String getTypeNameFromTypeUrl(\n"
		 "    java.lang.String typeUrl) {\n"
		 "  int pos = typeUrl.lastIndexOf('/');\n"
		 "  return pos == -1 ? \"\" : typeUrl.substring(pos + 1);\n"
		 "}\n"
		 "\n"
		 "public static <T extends com.google.protobuf.Message> Any pack(\n"
		 "    T message) {\n"
		 "  return Any.newBuilder()\n"
		 "      .setTypeUrl(getTypeUrl(\"type.googleapis.com\",\n"
		 "                             message.getDescriptorForType()))\n"
		 "      .setValue(message.toByteString())\n"
		 "      .build();\n"
		 "}\n"
		 "\n"
		 "/**\n"
		 " * Packs a message using the given type URL prefix. The type URL will\n"
		 " * be constructed by concatenating the message type's full name to the\n"
		 " * prefix with an optional \"/\" separator if the prefix doesn't end\n"
		 " * with \"/\" already.\n"
		 " */\n"
		 "public static <T extends com.google.protobuf.Message> Any pack(\n"
		 "    T message, java.lang.String typeUrlPrefix) {\n"
		 "  return Any.newBuilder()\n"
		 "      .setTypeUrl(getTypeUrl(typeUrlPrefix,\n"
		 "                             message.getDescriptorForType()))\n"
		 "      .setValue(message.toByteString())\n"
		 "      .build();\n"
		 "}\n"
		 "\n"
		 "public <T extends com.google.protobuf.Message> boolean is(\n"
		 "    java.lang.Class<T> clazz) {\n"
		 "  T defaultInstance =\n"
		 "      com.google.protobuf.Internal.getDefaultInstance(clazz);\n"
		 "  return getTypeNameFromTypeUrl(getTypeUrl()).equals(\n"
		 "      defaultInstance.getDescriptorForType().getFullName());\n"
		 "}\n"
		 "\n"
		 "public boolean isSameTypeAs(com.google.protobuf.Message message) {\n"
		 "  return getTypeNameFromTypeUrl(getTypeUrl()).equals(\n"
		 "      message.getDescriptorForType().getFullName());\n"
		 "}\n"
		 "\n"
		 "@SuppressWarnings(\"serial\")\n"
		 "private volatile com.google.protobuf.Message cachedUnpackValue;\n"
		 "\n"
		 "@java.lang.SuppressWarnings(\"unchecked\")\n"
		 "public <T extends com.google.protobuf.Message> T unpack(\n"
		 "    java.lang.Class<T> clazz)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n",
		 NULL);
	lp_print(printer,
		 "  boolean invalidClazz = false;\n"
		 "  if (cachedUnpackValue != null) {\n"
		 "    if (cachedUnpackValue.getClass() == clazz) {\n"
		 "      return (T) cachedUnpackValue;\n"
		 "    }\n"
		 "    invalidClazz = true;\n"
		 "  }\n"
		 "  if (invalidClazz || !is(clazz)) {\n"
		 "    throw new com.google.protobuf.InvalidProtocolBufferException(\n"
		 "        \"Type of the Any message does not match the given class.\");\n"
		 "  }\n"
		 "  T defaultInstance =\n"
		 "      com.google.protobuf.Internal.getDefaultInstance(clazz);\n"
		 "  T result = (T) defaultInstance.getParserForType()\n"
		 "      .parseFrom(getValue());\n"
		 "  cachedUnpackValue = result;\n"
		 "  return result;\n"
		 "}\n"
		 "\n"
		 "@java.lang.SuppressWarnings(\"unchecked\")\n"
		 "public <T extends com.google.protobuf.Message> T unpackSameTypeAs("
		 "T message)\n"
		 "    throws com.google.protobuf.InvalidProtocolBufferException {\n",
		 NULL);
	lp_print(printer,
		 "  boolean invalidValue = false;\n"
		 "  if (cachedUnpackValue != null) {\n"
		 "    if (cachedUnpackValue.getClass() == message.getClass()) {\n"
		 "      return (T) cachedUnpackValue;\n"
		 "    }\n"
		 "    invalidValue = true;\n"
		 "  }\n"
		 "  if (invalidValue || !isSameTypeAs(message)) {\n"
		 "    throw new com.google.protobuf.InvalidProtocolBufferException(\n"
		 "        \"Type of the Any message does not match the given "
		 "exemplar.\");\n"
		 "  }\n"
		 "  T result = (T) message.getParserForType().parseFrom(getValue());\n"
		 "  cachedUnpackValue = result;\n"
		 "  return result;\n"
		 "}\n"
		 "\n",
		 NULL);
}
