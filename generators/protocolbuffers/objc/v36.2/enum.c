// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/enum.{h,cc} (protobuf v36.2). */

/* SafelyPrintIntToCode */
static char *safely_print_int_to_code(int v)
{
	/* some compilers parse -2147483648 as two tokens */
	if (v == INT_MIN)
		return xcat(objc_itoa(v + 1), " - 1");
	return objc_itoa(v);
}

static EnumGenerator *enum_generator_new(const LpEnum *descriptor, const GenerationOptions *generation_options)
{
	EnumGenerator *g = xalloc(sizeof *g);
	g->descriptor = descriptor;
	g->generation_options = generation_options;
	g->name = lp_objc_enum_name(descriptor);
	g->alias_values_to_skip = xalloc(sizeof(bool) * (size_t)(descriptor->n_values + 1));
	/* An alias whose name overlaps a base value (or an earlier alias) gets no name; the values
	 * are still tracked for reflection and TextFormat. */
	Map value_names = {0};
	for (int i = 0; i < descriptor->n_values; i++) {
		const LpEnumValue *value = &descriptor->values[i];
		const LpEnumValue *canonical_value = proto_enum_value_by_number(descriptor, value->number);
		char *name = lp_objc_enum_value_name(value);
		if (value == canonical_value) {
			vec_push(&g->base_values, (void *)value);
			map_put(&value_names, name, "");
		} else if (map_get(&value_names, name)) {
			g->alias_values_to_skip[i] = true;
		} else {
			map_put(&value_names, name, "");
		}
		vec_push(&g->all_values, (void *)value);
	}
	return g;
}

static void enum_comments(LpPrinter *p, void *arg)
{
	const EnumGenerator *g = arg;
	emit_comments_string(p, g->generation_options, g->descriptor, 0);
}

static void enum_maybe_unknown_value(LpPrinter *p, void *arg)
{
	const EnumGenerator *g = arg;
	if (proto_enum_is_closed(g->descriptor))
		return;
	/* the unknown value */
	lp_emit(p, NULL, 0,
		"\n"
		" /**\n"
		"  * Value used if any message's field encounters a value that is not defined\n"
		"  * by this enum. The message will also have C functions to get/set the rawValue\n"
		"  * of the field.\n"
		"  **/\n"
		" $enum_name$_GPBUnrecognizedEnumeratorValue = kGPBUnrecognizedEnumeratorValue,\n");
}

typedef struct {
	const EnumGenerator *g;
	const LpEnumValue *v;
	unsigned comment_flags;
} EnumValueArgs;

static void enum_value_comments(LpPrinter *p, void *arg)
{
	const EnumValueArgs *a = arg;
	emit_comments_string(p, a->g->generation_options, a->v, a->comment_flags);
}

static void enum_values(LpPrinter *p, void *arg)
{
	const EnumGenerator *g = arg;
	unsigned comment_flags = 0;
	for (int i = 0; i < g->all_values.n; i++) {
		const LpEnumValue *v = g->all_values.v[i];
		if (g->alias_values_to_skip[v->index])
			continue;
		EnumValueArgs a = {g, v, comment_flags};
		LpSub subs[] = {
			LP_SUB_ANN("name", lp_objc_enum_value_name(v), v),
			LP_SUB_CB("comments", enum_value_comments, &a),
			LP_SUB("deprecated_attribute", get_optional_deprecated_attribute(v, NULL)),
			LP_SUB("value", safely_print_int_to_code(v->number)),
		};
		lp_emit(p, subs, 4,
			"\n"
			" $comments$\n"
			" $name$$ deprecated_attribute$ = $value$,\n");
		comment_flags = COMMENT_STRING_ADD_LEADING_NEWLINE;
	}
}

static void enum_descriptor_getter_name(LpPrinter *p, void *arg) { lp_emit(p, NULL, 0, "$enum_name$_EnumDescriptor"); }

static void enum_is_valid_value_function(LpPrinter *p, void *arg) { lp_emit(p, NULL, 0, "$enum_name$_IsValidValue"); }

static void enum_generate_header(const EnumGenerator *g, LpPrinter *p)
{
	/* No enum_extensibility attribute: ObjC proto enums are "non-frozen" for Swift (SE0192). */
	LpSub subs[] = {
		LP_SUB_ANN("enum_name", g->name, g->descriptor),
		LP_SUB_CB("enum_comments", enum_comments, (void *)g),
		LP_SUB("enum_deprecated_attribute", get_optional_deprecated_attribute(g->descriptor, g->descriptor->file)),
		LP_SUB_CB("maybe_unknown_value", enum_maybe_unknown_value, (void *)g),
		LP_SUB_CB("enum_values", enum_values, (void *)g),
		LP_SUB_CB("descriptor_getter_name", enum_descriptor_getter_name, NULL),
		LP_SUB_CB("is_valid_value_function", enum_is_valid_value_function, NULL),
	};
	subs[5].annotate = g->descriptor;
	subs[6].annotate = g->descriptor;
	lp_emit(p, subs, 7,
		"\n"
		" #pragma mark - Enum $enum_name$\n"
		"\n"
		" $enum_comments$\n"
		" typedef$ enum_deprecated_attribute$ GPB_ENUM($enum_name$) {\n"
		"   $maybe_unknown_value$\n"
		"   $enum_values$\n"
		" };\n"
		"\n"
		" GPBEnumDescriptor *$descriptor_getter_name$(void);\n"
		"\n"
		" /**\n"
		"  * Checks to see if the given value is defined by the enum or was not known at\n"
		"  * the time this source was generated.\n"
		"  **/\n"
		" BOOL $is_valid_value_function$(int32_t value);\n");
	lp_emit(p, NULL, 0, "\n");
}

typedef struct {
	const EnumGenerator *g;
	Str text_blob;
	TextFormatDecodeData *text_format_decode_data;
} EnumSourceArgs;

/* The 40-byte lines ("allow for escaping") of a blob, the last one ending with ';'. */
static void emit_blob_lines(LpPrinter *p, Str blob)
{
	enum { kBytesPerLine = 40 };
	for (size_t i = 0; i < blob.n; i += kBytesPerLine) {
		size_t n = blob.n - i < kBytesPerLine ? blob.n - i : kBytesPerLine;
		LpSub subs[] = {
			LP_SUB("data", escape_trigraphs(lp_cescape((Str){blob.p + i, n}))),
			LP_SUB("ending_semi", (i + kBytesPerLine) < blob.n ? "" : ";"),
		};
		lp_emit(p, subs, 2,
			"\n"
			" \"$data$\"$ending_semi$\n");
	}
}

static void enum_values_name_blob(LpPrinter *p, void *arg)
{
	const EnumSourceArgs *a = arg;
	emit_blob_lines(p, a->text_blob);
}

static void enum_source_values(LpPrinter *p, void *arg)
{
	const EnumSourceArgs *a = arg;
	for (int i = 0; i < a->g->all_values.n; i++) {
		LpSub subs[] = {LP_SUB("value_name", lp_objc_enum_value_name(a->g->all_values.v[i]))};
		lp_emit(p, subs, 1,
			"\n"
			" $value_name$,\n");
	}
}

static void enum_maybe_extra_text_format_decl(LpPrinter *p, void *arg)
{
	const EnumSourceArgs *a = arg;
	if (!a->text_format_decode_data->n)
		return;
	LpSub subs[] = {LP_SUB("extraTextFormatInfo", lp_cescape(tf_data(a->text_format_decode_data)))};
	lp_emit(p, subs, 1,
		"\n"
		" static const char *extraTextFormatInfo = \"$extraTextFormatInfo$\";\n");
}

static void enum_cases(LpPrinter *p, void *arg)
{
	const EnumSourceArgs *a = arg;
	for (int i = 0; i < a->g->base_values.n; i++) {
		LpSub subs[] = {LP_SUB("case_name", lp_objc_enum_value_name(a->g->base_values.v[i]))};
		lp_emit(p, subs, 1,
			"\n"
			" case $case_name$:\n");
	}
}

static void enum_generate_source(const EnumGenerator *g, LpPrinter *p)
{
	/* The TextFormat decode info is keyed by the index into the value descriptions (enum values
	 * may be aliased). */
	TextFormatDecodeData text_format_decode_data = {0};
	Buf text_blob = {0};
	for (int i = 0; i < g->all_values.n; i++) {
		const LpEnumValue *v = g->all_values.v[i];
		char *short_name = lp_objc_enum_value_short_name(v);
		buf_adds(&text_blob, short_name);
		buf_addc(&text_blob, '\0');
		if (!str_eq(lp_objc_un_camel_case_enum_short_name(short_name), v->name))
			tf_add_string(&text_format_decode_data, i, short_name, v->name);
	}

	EnumSourceArgs a = {g, buf_view(&text_blob), &text_format_decode_data};
	LpSub subs[] = {
		LP_SUB("name", g->name),
		LP_SUB_CB("values_name_blob", enum_values_name_blob, &a),
		LP_SUB_CB("values", enum_source_values, &a),
		LP_SUB_CB("maybe_extra_text_format_decl", enum_maybe_extra_text_format_decl, &a),
		/* the extra line, formatted */
		LP_SUB("maybe_extraTextFormatInfo", text_format_decode_data.n == 0
							    ? ""
							    : "\n                              "
							      "extraTextFormatInfo:extraTextFormatInfo"),
		LP_SUB("enum_flags", proto_enum_is_closed(g->descriptor) ? "GPBEnumDescriptorInitializationFlag_IsClosed"
									 : "GPBEnumDescriptorInitializationFlag_None"),
		LP_SUB_CB("enum_cases", enum_cases, &a),
	};
	lp_emit(p, subs, 7,
		"\n"
		" #pragma mark - Enum $name$\n"
		"\n"
		" GPBEnumDescriptor *$name$_EnumDescriptor(void) {\n"
		"   static _Atomic(GPBEnumDescriptor*) descriptor = nil;\n"
		"   if (!descriptor) {\n"
		"     static const char *valueNames =\n"
		"         $values_name_blob$\n"
		"     static const int32_t values[] = {\n"
		"         $values$\n"
		"     };\n"
		"     $maybe_extra_text_format_decl$\n"
		"     GPBEnumDescriptor *worker =\n"
		"         [GPBEnumDescriptor allocDescriptorForName:GPBNSStringifySymbol($name$)\n"
		"                                    runtimeSupport:&$google_protobuf_runtime_support$\n"
		"                                        valueNames:valueNames\n"
		"                                            values:values\n"
		"                                             count:(uint32_t)(sizeof(values) / sizeof(int32_t))\n"
		"                                      enumVerifier:$name$_IsValidValue\n"
		"                                             flags:$enum_flags$$maybe_extraTextFormatInfo$];\n"
		"     GPBEnumDescriptor *expected = nil;\n"
		"     if (!atomic_compare_exchange_strong(&descriptor, &expected, worker)) {\n"
		"       [worker release];\n"
		"     }\n"
		"   }\n"
		"   return descriptor;\n"
		" }\n"
		"\n"
		" BOOL $name$_IsValidValue(int32_t value__) {\n"
		"   switch (value__) {\n"
		"     $enum_cases$\n"
		"       return YES;\n"
		"     default:\n"
		"       return NO;\n"
		"   }\n"
		" }\n");
	lp_emit(p, NULL, 0, "\n");
}
