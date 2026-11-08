// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/message_builder.{h,cc} (protobuf v36.2). */

static char *MapValueImmutableClassdName_message_builder(const LpMessage *descriptor);
static bool BitfieldTracksMutability_message_builder(const LpField *const descriptor);
static void MessageBuilderGenerator_Generate(JvMessageGen *g, LpPrinter *printer);
static void MessageBuilderGenerator_GenerateDescriptorMethods(JvMessageGen *g, LpPrinter *printer);
static void MessageBuilderGenerator_GenerateCommonBuilderMethods(JvMessageGen *g, LpPrinter *printer);
static void MessageBuilderGenerator_GenerateBuildPartial(JvMessageGen *g, LpPrinter *printer);
static int MessageBuilderGenerator_GenerateBuildPartialPiece(JvMessageGen *g, LpPrinter *printer, int piece,
							     int first_field);
static void MessageBuilderGenerator_GenerateBuilderParsingMethods(JvMessageGen *g, LpPrinter *printer);
static void MessageBuilderGenerator_GenerateBuilderFieldParsingCases(JvMessageGen *g, LpPrinter *printer);
static void MessageBuilderGenerator_GenerateBuilderFieldParsingCase(JvMessageGen *g, LpPrinter *printer,
								    const LpField *field);
static void MessageBuilderGenerator_GenerateBuilderPackedFieldParsingCase(JvMessageGen *g, LpPrinter *printer,
									  const LpField *field);
static void MessageBuilderGenerator_GenerateIsInitialized(JvMessageGen *g, LpPrinter *printer);

static char *MapValueImmutableClassdName_message_builder(const LpMessage *descriptor)
{
	const LpField *value_field = jv_map_value_of(descriptor);
	JV_CHECK((PT_MESSAGE) == (value_field->type));
	return jv_immutable_class_name(value_field->message_type);
}

static bool BitfieldTracksMutability_message_builder(const LpField *const descriptor)
{
	if (!proto_field_is_repeated(descriptor) || jv_is_map_field(descriptor)) {
		return false;
	}
	// TODO: update this to migrate repeated fields to use
	// ProtobufList (which tracks immutability internally). That allows us to use
	// the presence bit to skip work on the repeated field if it is not populated.
	// Once all repeated fields are held in ProtobufLists, this method shouldn't
	// be needed.
	switch (descriptor->type) {
	case PT_GROUP:
	case PT_MESSAGE: return true;
	default: return false;
	}
}
/* MessageBuilderGenerator::MessageBuilderGenerator: generator_factory.c */

static void MessageBuilderGenerator_Generate(JvMessageGen *g, LpPrinter *printer)
{
	jv_write_message_doc_comment(printer, g->descriptor, &g->context->options, false);
	if (g->descriptor->n_extension_ranges > 0) {
		lp_print(printer,
			 "public static final class Builder extends\n"
			 "    com.google.protobuf.GeneratedMessage.ExtendableBuilder<\n"
			 "      $classname$, Builder> implements\n"
			 "    $extra_interfaces$\n"
			 "    $classname$OrBuilder {\n",
			 "classname", jv_immutable_class_name(g->descriptor), "extra_interfaces",
			 jv_extra_builder_interfaces(g->descriptor), NULL);
	} else {
		lp_print(printer,
			 "public static final class Builder extends\n"
			 "    com.google.protobuf.GeneratedMessage.Builder<Builder> "
			 "implements\n"
			 "    $extra_interfaces$\n"
			 "    $classname$OrBuilder {\n",
			 "classname", jv_immutable_class_name(g->descriptor), "extra_interfaces",
			 jv_extra_builder_interfaces(g->descriptor), NULL);
	}
	lp_indent(printer);

	MessageBuilderGenerator_GenerateDescriptorMethods(g, printer);
	MessageBuilderGenerator_GenerateCommonBuilderMethods(g, printer);

	if (jv_context_has_generated_methods(g->context, g->descriptor)) {
		MessageBuilderGenerator_GenerateIsInitialized(g, printer);
		MessageBuilderGenerator_GenerateBuilderParsingMethods(g, printer);
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
		lp_vars_set(&vars, "oneof_index", xcat(jv_i(oneof->index)));
		// oneofCase_ and oneof_
		lp_print_vars(printer, &vars,
			      "private int $oneof_name$Case_ = 0;\n"
			      "private java.lang.Object $oneof_name$_;\n");
		// getOneofCase()
		lp_print_vars(printer, &vars,
			      "public $oneof_capitalized_name$Case\n"
			      "    ${$get$oneof_capitalized_name$Case$}$() {\n"
			      "  return $oneof_capitalized_name$Case.forNumber(\n"
			      "      $oneof_name$Case_);\n"
			      "}\n");
		// clearOneof()
		lp_print_vars(printer, &vars,
			      "\n"
			      "public Builder ${$clear$oneof_capitalized_name$$}$() {\n"
			      "  $oneof_name$Case_ = 0;\n"
			      "  $oneof_name$_ = null;\n"
			      "  onChanged();\n"
			      "  return this;\n"
			      "}\n"
			      "\n");
	}

	// Integers for bit fields.
	int totalBits = 0;
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		totalBits += JV_FGCALL0(g, (&g->descriptor->fields[i]), GetNumBitsForBuilder);
	}
	int totalInts = (totalBits + 31) / 32;
	for (int i = 0; i < totalInts; i++) {
		lp_print(printer, "private int $bit_field_name$;\n", "bit_field_name", jv_get_bit_field_name(i), NULL);
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

static void MessageBuilderGenerator_GenerateDescriptorMethods(JvMessageGen *g, LpPrinter *printer)
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
		lp_print(printer,
			 "@SuppressWarnings({\"rawtypes\"})\n"
			 "protected com.google.protobuf.MapFieldReflectionAccessor "
			 "internalGetMutableMapFieldReflection(\n"
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
				 "  return internalGetMutable$capitalized_name$();\n",
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

static void MessageBuilderGenerator_GenerateCommonBuilderMethods(JvMessageGen *g, LpPrinter *printer)
{
	// Decide if we really need to have the "maybeForceBuilderInitialization()"
	// method.
	// TODO: Remove the need for this entirely
	bool need_maybe_force_builder_init = false;
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		if ((&g->descriptor->fields[i])->message_type != NULL &&
		    !jv_is_real_oneof((&g->descriptor->fields[i])) && jv_has_hasbit((&g->descriptor->fields[i]))) {
			need_maybe_force_builder_init = true;
			break;
		}
	}

	const char *force_builder_init = need_maybe_force_builder_init ? "  maybeForceBuilderInitialization();" : "";

	lp_print(printer,
		 "// Construct using $classname$.newBuilder()\n"
		 "private Builder() {\n"
		 "$force_builder_init$\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), "force_builder_init", force_builder_init, NULL);

	lp_print(printer,
		 "private Builder(\n"
		 "    com.google.protobuf.GeneratedMessage.BuilderParent parent) {\n"
		 "  super(parent);\n"
		 "$force_builder_init$\n"
		 "}\n",
		 "classname", jv_immutable_class_name(g->descriptor), "force_builder_init", force_builder_init, NULL);

	if (need_maybe_force_builder_init) {
		lp_print(printer,
			 "private void maybeForceBuilderInitialization() {\n"
			 "  if (com.google.protobuf.GeneratedMessage\n"
			 "          .alwaysUseFieldBuilders) {\n",
			 NULL);

		lp_indent(printer);
		lp_indent(printer);
		for (int i = 0; i < g->descriptor->n_fields; i++) {
			if (!jv_is_real_oneof((&g->descriptor->fields[i]))) {
				JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateFieldBuilderInitializationCode,
					  printer);
			}
		}
		lp_outdent(printer);
		lp_outdent(printer);

		lp_print(printer,
			 "  }\n"
			 "}\n",
			 NULL);
	}

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public Builder clear() {\n"
		 "  super.clear();\n",
		 NULL);

	lp_indent(printer);
	int totalBuilderInts = (g->descriptor->n_fields + 31) / 32;
	for (int i = 0; i < totalBuilderInts; i++) {
		lp_print(printer, "$bit_field_name$ = 0;\n", "bit_field_name", jv_get_bit_field_name(i), NULL);
	}

	for (int i = 0; i < g->descriptor->n_fields; i++) {
		JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateBuilderClearCode, printer);
	}

	for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
		lp_print(printer,
			 "$oneof_name$Case_ = 0;\n"
			 "$oneof_name$_ = null;\n",
			 "oneof_name", jv_context_oneof_info(g->context, g->oneofs[kv_i])->name, NULL);
	}

	lp_outdent(printer);

	lp_print(printer,
		 "  return this;\n"
		 "}\n"
		 "\n",
		 NULL);

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public com.google.protobuf.Descriptors.Descriptor\n"
		 "    getDescriptorForType() {\n"
		 "  return $fileclass$.internal_$identifier$_descriptor;\n"
		 "}\n"
		 "\n",
		 "fileclass", jv_immutable_class_name(g->descriptor->file), "identifier",
		 jv_unique_file_scope_identifier(g->descriptor), NULL);

	// LITE runtime implements this in GeneratedMessageLite.
	lp_print(printer,
		 "@java.lang.Override\n"
		 "public $classname$ getDefaultInstanceForType() {\n"
		 "  return $classname$.getDefaultInstance();\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	lp_print(printer,
		 "@java.lang.Override\n"
		 "public $classname$ build() {\n"
		 "  $classname$ result = buildPartial();\n"
		 "  if (!result.isInitialized()) {\n"
		 "    throw newUninitializedMessageException(result);\n"
		 "  }\n"
		 "  return result;\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	MessageBuilderGenerator_GenerateBuildPartial(g, printer);

	// We include these methods only in open source to maintain long term ABI
	// compatibility, and there should be no need to include them in Google3.
	if (true && g->descriptor->n_extension_ranges > 0) {
		lp_print(printer,
			 "public <Type> Builder setExtension(\n"
			 "    com.google.protobuf.GeneratedMessage.GeneratedExtension<\n"
			 "        $classname$, Type> extension,\n"
			 "    Type value) {\n"
			 "  return super.setExtension(extension, value);\n"
			 "}\n"
			 "public <Type> Builder setExtension(\n"
			 "    com.google.protobuf.GeneratedMessage.GeneratedExtension<\n"
			 "        $classname$, java.util.List<Type>> extension,\n"
			 "    int index, Type value) {\n"
			 "  return super.setExtension(extension, index, value);\n"
			 "}\n"
			 "public <Type> Builder addExtension(\n"
			 "    com.google.protobuf.GeneratedMessage.GeneratedExtension<\n"
			 "        $classname$, java.util.List<Type>> extension,\n"
			 "    Type value) {\n"
			 "  return super.addExtension(extension, value);\n"
			 "}\n"
			 "public <Type> Builder clearExtension(\n"
			 "    com.google.protobuf.GeneratedMessage.GeneratedExtension<\n"
			 "        $classname$, Type> extension) {\n"
			 "  return super.clearExtension(extension);\n"
			 "}\n",
			 "classname", jv_immutable_class_name(g->descriptor), NULL);
	}

	// -----------------------------------------------------------------

	if (jv_context_has_generated_methods(g->context, g->descriptor)) {
		lp_print(printer,
			 "@java.lang.Override\n"
			 "public Builder mergeFrom(com.google.protobuf.Message other) {\n"
			 "  if (other instanceof $classname$) {\n"
			 "    return mergeFrom(($classname$)other);\n"
			 "  } else {\n"
			 "    super.mergeFrom(other);\n"
			 "    return this;\n"
			 "  }\n"
			 "}\n"
			 "\n",
			 "classname", jv_immutable_class_name(g->descriptor), NULL);

		lp_print(printer,
			 "public Builder mergeFrom($classname$ other) {\n"
			 // Optimization:  If other is the default instance, we know none of its
			 //   fields are set so we can skip the merge.
			 "  if (other == $classname$.getDefaultInstance()) return this;\n",
			 "classname", jv_immutable_class_name(g->descriptor), NULL);
		lp_indent(printer);

		for (int i = 0; i < g->descriptor->n_fields; i++) {
			if (!jv_is_real_oneof((&g->descriptor->fields[i]))) {
				JV_FGCALL(g, (&g->descriptor->fields[i]), GenerateMergingCode, printer);
			}
		}

		// Merge oneof fields.
		for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
			const LpOneof *oneof = g->oneofs[kv_i];
			lp_print(printer, "switch (other.get$oneof_capitalized_name$Case()) {\n",
				 "oneof_capitalized_name", jv_context_oneof_info(g->context, oneof)->capitalized_name,
				 NULL);
			lp_indent(printer);
			for (int j = 0; j < oneof->n_fields; j++) {
				const LpField *field = ONEOF_FIELD(oneof, j);
				lp_print(printer, "case $field_name$: {\n", "field_name", jv_upper(field->name), NULL);
				lp_indent(printer);
				JV_FGCALL(g, field, GenerateMergingCode, printer);
				lp_print(printer, "break;\n", NULL);
				lp_outdent(printer);
				lp_print(printer, "}\n", NULL);
			}
			lp_print(printer,
				 "case $cap_oneof_name$_NOT_SET: {\n"
				 "  break;\n"
				 "}\n",
				 "cap_oneof_name", jv_upper(jv_context_oneof_info(g->context, oneof)->name), NULL);
			lp_outdent(printer);
			lp_print(printer, "}\n", NULL);
		}

		lp_outdent(printer);

		// if message type has extensions
		if (g->descriptor->n_extension_ranges > 0) {
			lp_print(printer, "  this.mergeExtensionFields(other);\n", NULL);
		}

		lp_print(printer, "  this.mergeUnknownFields(other.getUnknownFields());\n", NULL);

		lp_print(printer, "  onChanged();\n", NULL);

		lp_print(printer,
			 "  return this;\n"
			 "}\n"
			 "\n",
			 NULL);
	}
}

static void MessageBuilderGenerator_GenerateBuildPartial(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "@java.lang.Override\n"
		 "public $classname$ buildPartial() {\n"
		 "  $classname$ result = new $classname$(this);\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	lp_indent(printer);

	// Handle the repeated fields first so that the "mutable bits" are cleared.
	bool has_repeated_fields = false;
	for (int i = 0; i < g->descriptor->n_fields; ++i) {
		if (BitfieldTracksMutability_message_builder((&g->descriptor->fields[i]))) {
			has_repeated_fields = true;
			lp_print(printer, "buildPartialRepeatedFields(result);\n", NULL);
			break;
		}
	}

	// One buildPartial#() per from_bit_field
	int totalBuilderInts = (g->descriptor->n_fields + 31) / 32;
	if (totalBuilderInts > 0) {
		for (int i = 0; i < totalBuilderInts; ++i) {
			lp_print(printer, "if ($bit_field_name$ != 0) { buildPartial$piece$(result); }\n",
				 "bit_field_name", jv_get_bit_field_name(i), "piece", xcat(jv_i(i)), NULL);
		}
	}

	if (g->n_oneofs != 0) {
		lp_print(printer, "buildPartialOneofs(result);\n", NULL);
	}

	lp_outdent(printer);
	lp_print(printer,
		 "  onBuilt();\n"
		 "  return result;\n"
		 "}\n"
		 "\n",
		 "classname", jv_immutable_class_name(g->descriptor), NULL);

	// Build Repeated Fields
	if (has_repeated_fields) {
		lp_print(printer, "private void buildPartialRepeatedFields($classname$ result) {\n", "classname",
			 jv_immutable_class_name(g->descriptor), NULL);
		lp_indent(printer);
		for (int i = 0; i < g->descriptor->n_fields; ++i) {
			if (BitfieldTracksMutability_message_builder((&g->descriptor->fields[i]))) {
				const JvFieldGen *field = JV_FG(g, (&g->descriptor->fields[i]));
				field->ops->GenerateBuildingCode(field, printer);
			}
		}
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);
	}

	// Build non-oneof fields
	int start_field = 0;
	for (int i = 0; i < totalBuilderInts; i++) {
		start_field = MessageBuilderGenerator_GenerateBuildPartialPiece(g, printer, i, start_field);
	}

	// Build Oneofs
	if (g->n_oneofs != 0) {
		lp_print(printer, "private void buildPartialOneofs($classname$ result) {\n", "classname",
			 jv_immutable_class_name(g->descriptor), NULL);
		lp_indent(printer);
		for (int kv_i = 0; kv_i < g->n_oneofs; kv_i++) {
			const LpOneof *oneof = g->oneofs[kv_i];
			lp_print(printer,
				 "result.$oneof_name$Case_ = $oneof_name$Case_;\n"
				 "result.$oneof_name$_ = this.$oneof_name$_;\n",
				 "oneof_name", jv_context_oneof_info(g->context, oneof)->name, NULL);
			for (int i = 0; i < oneof->n_fields; ++i) {
				if (ONEOF_FIELD(oneof, i)->message_type != NULL) {
					const JvFieldGen *field = JV_FG(g, ONEOF_FIELD(oneof, i));
					field->ops->GenerateBuildingCode(field, printer);
				}
			}
		}
		lp_outdent(printer);
		lp_print(printer, "}\n\n", NULL);
	}
}

static int MessageBuilderGenerator_GenerateBuildPartialPiece(JvMessageGen *g, LpPrinter *printer, int piece,
							     int first_field)
{
	lp_print(printer,
		 "private void buildPartial$piece$($classname$ result) {\n"
		 "  int from_$bit_field_name$ = $bit_field_name$;\n",
		 "classname", jv_immutable_class_name(g->descriptor), "piece", xcat(jv_i(piece)), "bit_field_name",
		 jv_get_bit_field_name(piece), NULL);
	lp_indent(printer);
	/* absl::btree_set<int> declared_to_bitfields: message bitfield indexes, iterated in order */
	int n_bitfields = g->descriptor->n_fields / 32 + 2;
	bool *declared_to_bitfields = xalloc((size_t)n_bitfields);

	int bit = 0;
	int next = first_field;
	for (; bit < 32 && next < g->descriptor->n_fields; ++next) {
		const JvFieldGen *field = JV_FG(g, (&g->descriptor->fields[next]));
		bit += field->ops->GetNumBitsForBuilder(field);

		// Skip oneof fields that are handled separately
		if (jv_is_real_oneof((&g->descriptor->fields[next]))) {
			continue;
		}

		// Skip repeated fields because they are currently handled
		// in separate buildPartial sub-methods.
		if (BitfieldTracksMutability_message_builder((&g->descriptor->fields[next]))) {
			continue;
		}
		// Skip fields without presence bits in the builder
		if (field->ops->GetNumBitsForBuilder(field) == 0) {
			continue;
		}

		// Track message bits if necessary
		if (field->ops->GetNumBitsForMessage(field) > 0) {
			int to_bitfield = field->ops->GetMessageBitIndex(field) / 32;
			if (to_bitfield < n_bitfields && !declared_to_bitfields[to_bitfield]) {
				lp_print(printer, "int to_$bit_field_name$ = 0;\n", "bit_field_name",
					 jv_get_bit_field_name(to_bitfield), NULL);
				declared_to_bitfields[to_bitfield] = true;
			}
		}

		// Copy the field from the builder to the message
		field->ops->GenerateBuildingCode(field, printer);
	}

	// Copy the bit field results to the generated message
	for (int to_bitfield = 0; to_bitfield < n_bitfields; to_bitfield++) {
		if (!declared_to_bitfields[to_bitfield])
			continue;
		lp_print(printer, "result.$bit_field_name$ |= to_$bit_field_name$;\n", "bit_field_name",
			 jv_get_bit_field_name(to_bitfield), NULL);
	}

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);

	return next;
}

// ===================================================================

static void MessageBuilderGenerator_GenerateBuilderParsingMethods(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "@java.lang.Override\n"
		 "public Builder mergeFrom(\n"
		 "    com.google.protobuf.CodedInputStream input,\n"
		 "    com.google.protobuf.ExtensionRegistryLite extensionRegistry)\n"
		 "    throws java.io.IOException {\n"
		 "  java.util.Objects.requireNonNull(extensionRegistry);\n"
		 "  try {\n"
		 "    boolean done = false;\n"
		 "    while (!done) {\n"
		 "      int tag = input.readTag();\n"
		 "      switch (tag) {\n"
		 "        case 0:\n" // zero signals EOF / limit reached
		 "          done = true;\n"
		 "          break;\n",
		 NULL);
	lp_indent(printer); // method
	lp_indent(printer); // try
	lp_indent(printer); // while
	lp_indent(printer); // switch
	MessageBuilderGenerator_GenerateBuilderFieldParsingCases(g, printer);
	lp_outdent(printer); // switch
	lp_outdent(printer); // while
	lp_outdent(printer); // try
	lp_outdent(printer); // method
	lp_print(printer,
		 "        default: {\n"
		 "          if (!super.parseUnknownField(input, extensionRegistry, tag)) "
		 "{\n"
		 "            done = true; // was an endgroup tag\n"
		 "          }\n"
		 "          break;\n"
		 "        } // default:\n"
		 "      } // switch (tag)\n"
		 "    } // while (!done)\n"
		 "  } catch (com.google.protobuf.InvalidProtocolBufferException e) {\n"
		 "    throw e.unwrapIOException();\n"
		 "  } finally {\n"
		 "    onChanged();\n"
		 "  } // finally\n"
		 "  return this;\n"
		 "}\n",
		 NULL);
}

static void MessageBuilderGenerator_GenerateBuilderFieldParsingCases(JvMessageGen *g, LpPrinter *printer)
{
	const LpField **sorted_fields = jv_sort_fields_by_number(g->descriptor);
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = sorted_fields[i];
		MessageBuilderGenerator_GenerateBuilderFieldParsingCase(g, printer, field);
		if (proto_field_is_packable(field)) {
			MessageBuilderGenerator_GenerateBuilderPackedFieldParsingCase(g, printer, field);
		}
	}
}

static void MessageBuilderGenerator_GenerateBuilderFieldParsingCase(JvMessageGen *g, LpPrinter *printer,
								    const LpField *field)
{
	uint32_t tag = jv_wire_format_lite_make_tag(field->number, jv_wire_type_for_field_type(field->type));
	const char *tagString = xcat(jv_i(((int32_t)(tag))));
	lp_print(printer, "case $tag$: {\n", "tag", tagString, NULL);
	lp_indent(printer);

	JV_FGCALL(g, field, GenerateBuilderParsingCode, printer);

	lp_outdent(printer);
	lp_print(printer,
		 "  break;\n"
		 "} // case $tag$\n",
		 "tag", tagString, NULL);
}

static void MessageBuilderGenerator_GenerateBuilderPackedFieldParsingCase(JvMessageGen *g, LpPrinter *printer,
									  const LpField *field)
{
	// To make packed = true wire compatible, we generate parsing code from a
	// packed version of this field regardless of field->options().packed().
	uint32_t tag = jv_wire_format_lite_make_tag(field->number, JV_WIRETYPE_LENGTH_DELIMITED);
	const char *tagString = xcat(jv_i(((int32_t)(tag))));
	lp_print(printer, "case $tag$: {\n", "tag", tagString, NULL);
	lp_indent(printer);

	JV_FGCALL(g, field, GenerateBuilderParsingCodeFromPacked, printer);

	lp_outdent(printer);
	lp_print(printer,
		 "  break;\n"
		 "} // case $tag$\n",
		 "tag", tagString, NULL);
}

// ===================================================================

static void MessageBuilderGenerator_GenerateIsInitialized(JvMessageGen *g, LpPrinter *printer)
{
	lp_print(printer,
		 "@java.lang.Override\n"
		 "public final boolean isInitialized() {\n",
		 NULL);
	lp_indent(printer);

	// Check that all required fields in this message are set.
	// TODO:  We can optimize this when we switch to putting all the
	//   "has" fields into a single bitfield.
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = (&g->descriptor->fields[i]);
		const JvFieldGeneratorInfo *info = jv_context_field_info(g->context, field);

		if (proto_field_is_required(field)) {
			lp_print(printer,
				 "if (!has$name$()) {\n"
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
					 "  return false;\n"
					 "}\n",
					 "type", jv_immutable_class_name(field->message_type), "name",
					 info->capitalized_name, NULL);
			} else if (proto_field_is_repeated(field)) {
				if (jv_is_map_entry(field->message_type)) {
					lp_print(printer,
						 "for ($type$ item : get$name$Map().values()) {\n"
						 "  if (!item.isInitialized()) {\n"
						 "    return false;\n"
						 "  }\n"
						 "}\n",
						 "type",
						 MapValueImmutableClassdName_message_builder(field->message_type),
						 "name", info->capitalized_name, NULL);
				} else {
					lp_print(printer,
						 "for (int i = 0; i < get$name$Count(); i++) {\n"
						 "  if (!get$name$(i).isInitialized()) {\n"
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
					 "    return false;\n"
					 "  }\n"
					 "}\n",
					 "name", info->capitalized_name, NULL);
			};
		}
	}

	if (g->descriptor->n_extension_ranges > 0) {
		lp_print(printer,
			 "if (!extensionsAreInitialized()) {\n"
			 "  return false;\n"
			 "}\n",
			 NULL);
	}

	lp_outdent(printer);

	lp_print(printer,
		 "  return true;\n"
		 "}\n"
		 "\n",
		 NULL);
}

// ===================================================================
