// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/full/enum.{h,cc} (protobuf v36.2). */

static void EnumNonLiteGenerator_Generate(JvEnumGen *g, LpPrinter *printer);
static bool EnumNonLiteGenerator_CanUseEnumValues(JvEnumGen *g);

static void EnumNonLiteGenerator_Generate(JvEnumGen *g, LpPrinter *printer)
{
	jv_write_enum_doc_comment(printer, g->descriptor, &g->context->options, false);
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor, g->immutable_api);

	if (jv_check_large_enum(g->descriptor)) {
		fail("unsupported by protoc-gen-uni: feature pb.java.large_enum");
		return;
	}

	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoNonnullApi\n", NULL);
	}
	lp_print(printer,
		 "$deprecation$public enum $classname$\n"
		 "    implements com.google.protobuf.ProtocolMessageEnum {\n",
		 "classname", g->descriptor->name, "deprecation",
		 OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "", NULL);
	lp_indent(printer);

	bool ordinal_is_index = true;
	const char *index_text = "ordinal()";
	for (int i = 0; i < g->n_canonical_values; i++) {
		if (g->canonical_values[i]->index != i) {
			ordinal_is_index = false;
			index_text = "index";
			break;
		}
	}

	for (int ci = 0; ci < g->n_canonical_values; ci++) {
		const LpEnumValue *value = g->canonical_values[ci];
		LpVars vars = { 0 };
		lp_vars_set(&vars, "name", (value->name));
		lp_vars_set(&vars, "index", xcat(jv_i(value->index)));
		lp_vars_set(&vars, "number", xcat(jv_i(value->number)));
		jv_write_enum_value_doc_comment(printer, value, &g->context->options);
		if (OPT_deprecated(value)) {
			lp_print(printer, "@java.lang.Deprecated\n", NULL);
		}
		if (ordinal_is_index) {
			lp_print_vars(printer, &vars, "$name$($number$),\n");
		} else {
			lp_print_vars(printer, &vars, "$name$($index$, $number$),\n");
		}
	}

	if (!proto_enum_is_closed(g->descriptor)) {
		if (ordinal_is_index) {
			lp_print(printer, "${$UNRECOGNIZED$}$(-1),\n", "{", "", "}", "", NULL);
		} else {
			lp_print(printer, "${$UNRECOGNIZED$}$(-1, -1),\n", "{", "", "}", "", NULL);
		}
	}

	lp_print(printer,
		 ";\n"
		 "\n",
		 NULL);

	// -----------------------------------------------------------------

	lp_print(printer, "static {\n", NULL);
	lp_indent(printer);
	jv_print_gencode_version_validator(printer, true, g->descriptor->name);
	lp_outdent(printer);
	lp_print(printer, "}\n", NULL);

	for (int ai = 0; ai < g->n_aliases; ai++) {
		const JvEnumAlias alias = g->aliases[ai];
		LpVars vars = { 0 };
		lp_vars_set(&vars, "classname", (g->descriptor->name));
		lp_vars_set(&vars, "name", (alias.value->name));
		lp_vars_set(&vars, "canonical_name", (alias.canonical_value->name));
		jv_write_enum_value_doc_comment(printer, alias.value, &g->context->options);
		lp_print_vars(printer, &vars, "public static final $classname$ $name$ = $canonical_name$;\n");
	}

	for (int i = 0; i < g->descriptor->n_values; i++) {
		LpVars vars = { 0 };
		lp_vars_set(&vars, "name", ((&g->descriptor->values[i])->name));
		lp_vars_set(&vars, "number", xcat(jv_i((&g->descriptor->values[i])->number)));
		lp_vars_set(&vars, "{", "");
		lp_vars_set(&vars, "}", "");
		lp_vars_set(&vars, "deprecation",
			    OPT_deprecated((&g->descriptor->values[i])) ? "@java.lang.Deprecated " : "");
		jv_write_enum_value_doc_comment(printer, (&g->descriptor->values[i]), &g->context->options);
		lp_print_vars(printer, &vars,
			      "$deprecation$public static final int ${$$name$_VALUE$}$ = "
			      "$number$;\n");
	}
	lp_print(printer, "\n", NULL);

	// -----------------------------------------------------------------

	lp_print(printer,
		 "\n"
		 "public final int getNumber() {\n",
		 NULL);
	if (!proto_enum_is_closed(g->descriptor)) {
		if (ordinal_is_index) {
			lp_print(printer,
				 "  if (this == UNRECOGNIZED) {\n"
				 "    throw new java.lang.IllegalArgumentException(\n"
				 "        \"Can't get the number of an unknown enum value.\");\n"
				 "  }\n",
				 NULL);
		} else {
			lp_print(printer,
				 "  if (index == -1) {\n"
				 "    throw new java.lang.IllegalArgumentException(\n"
				 "        \"Can't get the number of an unknown enum value.\");\n"
				 "  }\n",
				 NULL);
		}
	}
	lp_print(printer,
		 "  return value;\n"
		 "}\n"
		 "\n",
		 NULL);
	if (true) {
		lp_print(printer,
			 "/**\n"
			 " * @param value The numeric wire value of the corresponding enum "
			 "entry.\n"
			 " * @return The enum associated with the given numeric wire value.\n"
			 " * @deprecated Use {@link #forNumber(int)} instead.\n"
			 " */\n"
			 "@java.lang.Deprecated\n"
			 "public static $classname$ valueOf(int value) {\n"
			 "  return forNumber(value);\n"
			 "}\n"
			 "\n",
			 "classname", g->descriptor->name, NULL);
	}
	lp_print(printer,
		 "/**\n"
		 " * @param value The numeric wire value of the corresponding enum "
		 "entry.\n"
		 " * @return The enum associated with the given numeric wire value.\n"
		 " */\n",
		 NULL);
	if (!JV_IS_OSS) {
		lp_print(printer, "@com.google.protobuf.Internal.ProtoMethodMayReturnNull\n", NULL);
	}
	lp_print(printer,
		 "public static $classname$ forNumber(int value) {\n"
		 "  switch (value) {\n",
		 "classname", g->descriptor->name, NULL);
	lp_indent(printer);
	lp_indent(printer);

	for (int ci = 0; ci < g->n_canonical_values; ci++) {
		const LpEnumValue *value = g->canonical_values[ci];
		lp_print(printer, "case $number$: return $name$;\n", "name", value->name, "number",
			 xcat(jv_i(value->number)), NULL);
	}

	lp_outdent(printer);
	lp_outdent(printer);
	lp_print(printer,
		 "    default: return null;\n"
		 "  }\n"
		 "}\n"
		 "\n"
		 "public static com.google.protobuf.Internal.EnumLiteMap<$classname$>\n"
		 "    internalGetValueMap() {\n"
		 "  return internalValueMap;\n"
		 "}\n"
		 "private static final com.google.protobuf.Internal.EnumLiteMap<\n"
		 "    $classname$> internalValueMap =\n"
		 "      new com.google.protobuf.Internal.EnumLiteMap<$classname$>() {\n"
		 "        public $classname$ findValueByNumber(int number) {\n"
		 "          return $classname$.forNumber(number);\n"
		 "        }\n"
		 "      };\n"
		 "\n",
		 "classname", g->descriptor->name, NULL);

	// -----------------------------------------------------------------
	// Reflection

	if (jv_has_descriptor_methods_(g->descriptor, jv_context_enforce_lite(g->context))) {
		lp_print(printer,
			 "public final com.google.protobuf.Descriptors.EnumValueDescriptor\n"
			 "    getValueDescriptor() {\n",
			 NULL);
		if (!proto_enum_is_closed(g->descriptor)) {
			if (ordinal_is_index) {
				lp_print(printer,
					 "  if (this == UNRECOGNIZED) {\n"
					 "    throw new java.lang.IllegalStateException(\n"
					 "        \"Can't get the descriptor of an unrecognized enum "
					 "value.\");\n"
					 "  }\n",
					 NULL);
			} else {
				lp_print(printer,
					 "  if (index == -1) {\n"
					 "    throw new java.lang.IllegalStateException(\n"
					 "        \"Can't get the descriptor of an unrecognized enum "
					 "value.\");\n"
					 "  }\n",
					 NULL);
			}
		}
		lp_print(printer,
			 "  return getDescriptor().getValue($index_text$);\n"
			 "}\n"
			 "public final com.google.protobuf.Descriptors.EnumDescriptor\n"
			 "    getDescriptorForType() {\n"
			 "  return getDescriptor();\n"
			 "}\n"
			 "public static com.google.protobuf.Descriptors.EnumDescriptor\n"
			 "    getDescriptor() {\n",
			 "index_text", index_text, NULL);

		// TODO:  Cache statically?  Note that we can't access descriptors
		//   at module init time because it wouldn't work with descriptor.proto, but
		//   we can cache the value the first time getDescriptor() is called.
		if (g->descriptor->containing_type == NULL) {
			// The class generated for the File fully populates the descriptor with
			// extensions in both the mutable and immutable cases. (In the mutable api
			// this is accomplished by attempting to load the immutable outer class).
			lp_print(printer, "  return $file$.getDescriptor().getEnumType($index$);\n", "file",
				 jv_immutable_class_name(g->descriptor->file), "index",
				 xcat(jv_i(g->descriptor->index)), NULL);
		} else {
			lp_print(printer, "  return $parent$.$descriptor$.getEnumType($index$);\n", "parent",
				 jv_immutable_class_name(g->descriptor->containing_type), "descriptor",
				 OPT_no_standard_descriptor_accessor(g->descriptor->containing_type)
					 ? "getDefaultInstance().getDescriptorForType()"
					 : "getDescriptor()",
				 "index", xcat(jv_i(g->descriptor->index)), NULL);
		}

		lp_print(printer,
			 "}\n"
			 "\n"
			 "private static final $classname$[] VALUES = ",
			 "classname", g->descriptor->name, NULL);

		if (EnumNonLiteGenerator_CanUseEnumValues(g)) {
			// If the constants we are going to output are exactly the ones we
			// have declared in the Java enum in the same order, then we can use
			// the values() method that the Java compiler automatically generates
			// for every enum.
			lp_print(printer, "values();\n", NULL);
		} else {
			lp_print(printer, "getStaticValuesArray();\n", NULL);
			lp_print(printer, "private static $classname$[] getStaticValuesArray() {\n", "classname",
				 g->descriptor->name, NULL);
			lp_indent(printer);
			lp_print(printer,
				 "return new $classname$[] {\n"
				 "  ",
				 "classname", g->descriptor->name, NULL);
			for (int i = 0; i < g->descriptor->n_values; i++) {
				lp_print(printer, "$name$, ", "name", (&g->descriptor->values[i])->name, NULL);
			}
			lp_print(printer,
				 "\n"
				 "};\n",
				 NULL);
			lp_outdent(printer);
			lp_print(printer, "}", NULL);
		}

		lp_print(printer,
			 "\n"
			 "public static $classname$ valueOf(\n"
			 "    com.google.protobuf.Descriptors.EnumValueDescriptor desc) {\n"
			 "  if (desc.getType() != getDescriptor()) {\n"
			 "    throw new java.lang.IllegalArgumentException(\n"
			 "      \"EnumValueDescriptor is not for this type.\");\n"
			 "  }\n",
			 "classname", g->descriptor->name, NULL);
		if (!proto_enum_is_closed(g->descriptor)) {
			lp_print(printer,
				 "  if (desc.getIndex() == -1) {\n"
				 "    return UNRECOGNIZED;\n"
				 "  }\n",
				 NULL);
		}
		lp_print(printer,
			 "  return VALUES[desc.getIndex()];\n"
			 "}\n"
			 "\n",
			 NULL);

		if (!ordinal_is_index) {
			lp_print(printer, "private final int index;\n", NULL);
		}
	}

	// -----------------------------------------------------------------

	lp_print(printer, "private final int value;\n\n", NULL);

	if (ordinal_is_index) {
		lp_print(printer, "private $classname$(int value) {\n", "classname", g->descriptor->name, NULL);
	} else {
		lp_print(printer, "private $classname$(int index, int value) {\n", "classname", g->descriptor->name,
			 NULL);
	}
	if (jv_has_descriptor_methods_(g->descriptor, jv_context_enforce_lite(g->context)) && !ordinal_is_index) {
		lp_print(printer, "  this.index = index;\n", NULL);
	}
	lp_print(printer,
		 "  this.value = value;\n"
		 "}\n",
		 NULL);

	lp_print(printer,
		 "\n"
		 "// @@protoc_insertion_point(enum_scope:$full_name$)\n",
		 "full_name", g->descriptor->full_name, NULL);

	lp_outdent(printer);
	lp_print(printer, "}\n\n", NULL);
}

static bool EnumNonLiteGenerator_CanUseEnumValues(JvEnumGen *g)
{
	if (g->n_canonical_values != g->descriptor->n_values) {
		return false;
	}
	for (int i = 0; i < g->descriptor->n_values; i++) {
		if (!str_eq((&g->descriptor->values[i])->name, g->canonical_values[i]->name)) {
			return false;
		}
	}
	return true;
}
