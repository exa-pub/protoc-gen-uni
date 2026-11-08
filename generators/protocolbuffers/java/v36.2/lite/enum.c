// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/lite/enum.{h,cc} (protobuf v36.2). */

static void EnumLiteGenerator_Generate(JvEnumGen *g, LpPrinter *printer);

static void EnumLiteGenerator_Generate(JvEnumGen *g, LpPrinter *printer)
{
	jv_write_enum_doc_comment(printer, g->descriptor, &g->context->options, false);
	jv_maybe_print_generated_annotation_(g->context, printer, g->descriptor, g->immutable_api);

	if (jv_check_large_enum(g->descriptor)) {
		fail("unsupported by protoc-gen-uni: feature pb.java.large_enum");
		return;
	}

	lp_print(printer,
		 "$deprecation$public enum $classname$\n"
		 "    implements com.google.protobuf.Internal.EnumLite {\n",
		 "classname", g->descriptor->name, "deprecation",
		 OPT_deprecated(g->descriptor) ? "@java.lang.Deprecated " : "", NULL);
	lp_indent(printer);

	for (int ci = 0; ci < g->n_canonical_values; ci++) {
		const LpEnumValue *value = g->canonical_values[ci];
		LpVars vars = { 0 };
		lp_vars_set(&vars, "name", (value->name));
		lp_vars_set(&vars, "number", xcat(jv_i(value->number)));
		jv_write_enum_value_doc_comment(printer, value, &g->context->options);
		if (OPT_deprecated(value)) {
			lp_print(printer, "@java.lang.Deprecated\n", NULL);
		}
		lp_print_vars(printer, &vars, "$name$($number$),\n");
	}

	if (!proto_enum_is_closed(g->descriptor)) {
		lp_print(printer, "${$UNRECOGNIZED$}$(-1),\n", "{", "", "}", "", NULL);
	}

	lp_print(printer,
		 ";\n"
		 "\n",
		 NULL);

	// -----------------------------------------------------------------

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
		 "@java.lang.Override\n"
		 "public final int getNumber() {\n",
		 NULL);
	if (!proto_enum_is_closed(g->descriptor)) {
		lp_print(printer,
			 "  if (this == UNRECOGNIZED) {\n"
			 "    return "
			 "com.google.protobuf.Internal.throwCannotGetNumberOfUnrecognized();\n"
			 "  }\n",
			 NULL);
	}
	lp_print(printer,
		 "  return value;\n"
		 "}\n"
		 "\n",
		 NULL);
	if (true) {
		lp_print(printer,
			 "/**\n"
			 " * @param value The number of the enum to look for.\n"
			 " * @return The enum associated with the given number.\n"
			 " * @deprecated Use {@link #forNumber(int)} instead.\n"
			 " */\n"
			 "@java.lang.Deprecated\n"
			 "public static $classname$ valueOf(int value) {\n"
			 "  return forNumber(value);\n"
			 "}\n"
			 "\n",
			 "classname", g->descriptor->name, NULL);
	}

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
		 "        @java.lang.Override\n"
		 "        public $classname$ findValueByNumber(int number) {\n"
		 "          return $classname$.forNumber(number);\n"
		 "        }\n"
		 "      };\n"
		 "\n"
		 "public static com.google.protobuf.Internal.EnumVerifier \n"
		 "    internalGetVerifier() {\n"
		 "  return $classname$Verifier.INSTANCE;\n"
		 "}\n"
		 "\n"
		 "private static final class $classname$Verifier implements \n"
		 "     com.google.protobuf.Internal.EnumVerifier { \n"
		 "        static final com.google.protobuf.Internal.EnumVerifier\n"
		 "            INSTANCE = new $classname$Verifier();\n"
		 "        @java.lang.Override\n"
		 "        public boolean isInRange(int number) {\n"
		 "          return $classname$.forNumber(number) != null;\n"
		 "        }\n"
		 "      };\n"
		 "\n",
		 "classname", g->descriptor->name, NULL);
	if (!JV_IS_OSS) {
		lp_print(printer,
			 "/**\n"
			 " * Override of toString that prints the number and name.\n"
			 " * This is primarily intended as a developer aid.\n"
			 " *\n"
			 " * <p>NOTE: This implementation is liable to change in the future,\n"
			 " * and should not be relied on in code.\n"
			 " */\n"
			 "@java.lang.Override\n"
			 "public java.lang.String toString() {\n"
			 "  StringBuilder result = new StringBuilder(\"<\");\n"
			 "  result.append(getClass().getName()).append('@')\n"
			 "      .append(java.lang.Integer.toHexString(\n"
			 "        java.lang.System.identityHashCode(this)));\n",
			 NULL);
		if (!proto_enum_is_closed(g->descriptor)) {
			lp_print(printer,
				 "  if (this != UNRECOGNIZED) {\n"
				 "    result.append(\" number=\").append(getNumber());\n"
				 "  }\n",
				 NULL);
		} else {
			lp_print(printer, "  result.append(\" number=\").append(getNumber());\n", NULL);
		}
		lp_print(printer,
			 "  return result.append(\" name=\")\n"
			 "      .append(name()).append('>').toString();\n"
			 "}\n"
			 "\n",
			 NULL);
	}

	lp_print(printer,
		 "private final int value;\n\n"
		 "private $classname$(int value) {\n",
		 "classname", g->descriptor->name, NULL);
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
