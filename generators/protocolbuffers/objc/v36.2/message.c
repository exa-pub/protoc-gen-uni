// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/message.{h,cc} (protobuf v36.2). */

/* OrderGroupForFieldDescriptor: after the has bits, 4-byte values, then pointers, then 8-byte
 * values (BOOLs are stored in the has bits). */
static int order_group_for_field_descriptor(const LpField *descriptor)
{
	/* anything repeated is a GPB*Array/NSArray, so a pointer */
	if (proto_field_is_repeated(descriptor))
		return 3;
	switch (descriptor->type) {
	case PT_DOUBLE: case PT_INT64: case PT_SINT64: case PT_UINT64: case PT_SFIXED64: case PT_FIXED64:
		return 4;
	case PT_GROUP: case PT_MESSAGE: case PT_STRING: case PT_BYTES:
		return 3;
	case PT_FLOAT: case PT_INT32: case PT_SINT32: case PT_UINT32: case PT_SFIXED32: case PT_FIXED32: case PT_ENUM:
		return 2;
	case PT_BOOL:
		return 99;
	}
	fail("Can't get here.");
}

static int field_ordering_by_number(const void *a, const void *b)
{
	const LpField *x = *(const LpField *const *)a, *y = *(const LpField *const *)b;
	return (x->number > y->number) - (x->number < y->number);
}

static int field_ordering_by_storage_size(const void *a, const void *b)
{
	const LpField *x = *(const LpField *const *)a, *y = *(const LpField *const *)b;
	int ga = order_group_for_field_descriptor(x), gb = order_group_for_field_descriptor(y);
	if (ga != gb)
		return ga < gb ? -1 : 1;
	/* within the group, by field number */
	return (x->number > y->number) - (x->number < y->number);
}

static const LpField **sort_fields(const LpMessage *descriptor, int (*cmp)(const void *, const void *))
{
	const LpField **fields = xalloc(sizeof(LpField *) * (size_t)(descriptor->n_fields + 1));
	for (int i = 0; i < descriptor->n_fields; i++)
		fields[i] = &descriptor->fields[i];
	if (descriptor->n_fields > 1)
		qsort(fields, (size_t)descriptor->n_fields, sizeof *fields, cmp);
	return fields;
}

/* SimpleExtensionRange */
typedef struct {
	int start; /* inclusive */
	int end;   /* exclusive */
} SimpleExtensionRange;

static int extension_range_ordering(const void *a, const void *b)
{
	const LpExtensionRange *x = *(const LpExtensionRange *const *)a, *y = *(const LpExtensionRange *const *)b;
	return (x->start > y->start) - (x->start < y->start);
}

/* SimpleExtensionRange::Normalize: the ranges sorted, contiguous ones merged. */
static int normalize_extension_ranges(const LpMessage *descriptor, SimpleExtensionRange **out)
{
	int n = descriptor->n_extension_ranges;
	const LpExtensionRange **sorted = xalloc(sizeof(LpExtensionRange *) * (size_t)(n + 1));
	for (int i = 0; i < n; i++)
		sorted[i] = &descriptor->extension_ranges[i];
	if (n > 1)
		qsort(sorted, (size_t)n, sizeof *sorted, extension_range_ordering);
	SimpleExtensionRange *result = xalloc(sizeof(SimpleExtensionRange) * (size_t)(n + 1));
	int m = 0;
	for (int i = 0; i < n; i++) {
		if (m > 0 && result[m - 1].end == sorted[i]->start)
			result[m - 1].end = sorted[i]->end;
		else
			result[m++] = (SimpleExtensionRange){sorted[i]->start, sorted[i]->end};
	}
	*out = result;
	return m;
}

static MessageGenerator *message_generator_new(const char *file_description_name, const LpMessage *descriptor,
					       const GenerationOptions *generation_options)
{
	MessageGenerator *g = xalloc(sizeof *g);
	g->file_description_name = file_description_name;
	g->descriptor = descriptor;
	g->generation_options = generation_options;
	g->field_generators = field_generator_map(descriptor, generation_options);
	g->class_name = lp_objc_class_name(descriptor);
	g->deprecated_attribute = (char *)get_optional_deprecated_attribute(descriptor, descriptor->file);
	if (pb_int(descriptor->options, MSGOPT_MAP_ENTRY, 0))
		fail("error: MessageGenerator create of a map<>!");
	if (pb_int(descriptor->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0) && descriptor->n_fields != 0)
		fail("error: MessageGenerator message_set_wire_format should never have fields!");
	g->n_oneof_generators = descriptor->n_real_oneofs;
	g->oneof_generators = xalloc(sizeof(OneofGenerator *) * (size_t)(g->n_oneof_generators + 1));
	for (int i = 0; i < descriptor->n_real_oneofs; i++)
		g->oneof_generators[i] = oneof_generator_new(&descriptor->oneofs[i], generation_options);

	/* Has bits: CalculateHasBits assigns them, SetOneofIndexBase gives the fields of a oneof a
	 * negative index that groups them. */
	int num_has_bits = field_generator_map_calculate_has_bits(g->field_generators, descriptor->n_fields);
	int sizeof_has_storage = (num_has_bits + 31) / 32;
	/* never zero length (it also keeps the negative indices of oneof only messages) */
	if (sizeof_has_storage == 0)
		sizeof_has_storage = 1;
	for (int i = 0; i < g->n_oneof_generators; i++)
		oneof_set_oneof_index_base(g->oneof_generators[i], sizeof_has_storage);
	for (int i = 0; i < descriptor->n_fields; i++)
		field_set_oneof_index_base(g->field_generators[i], sizeof_has_storage);
	/* one int32 per oneof (the field number) */
	g->sizeof_has_storage = sizeof_has_storage + g->n_oneof_generators;
	return g;
}

static void message_add_extension_generators(MessageGenerator *g, Vec *extension_generators)
{
	for (int i = 0; i < g->descriptor->n_extensions; i++) {
		const LpField *extension = &g->descriptor->extensions[i];
		if (!g->generation_options->strip_custom_options || !extension_is_custom_option(extension)) {
			ExtensionGenerator *e = extension_generator_new(extension, g->generation_options);
			vec_push(extension_generators, e);
			vec_push(&g->extension_generators, e);
		}
	}
}

static void message_determine_forward_declarations(const MessageGenerator *g, StringSet *fwd_decls,
						   bool include_external_types)
{
	for (int i = 0; i < g->descriptor->n_fields; i++)
		field_determine_forward_declarations(g->field_generators[i], fwd_decls, include_external_types);
}

static void message_determine_needed_files(const MessageGenerator *g, FileSet *deps)
{
	for (int i = 0; i < g->descriptor->n_fields; i++)
		field_determine_needed_files(g->field_generators[i], deps);
}

static void message_determine_objective_c_class_definitions(const MessageGenerator *g, StringSet *fwd_decls)
{
	/* this class, as a linker symbol, to reference it without calling +class */
	string_set_insert(fwd_decls, objc_class_declaration(g->class_name));
	for (int i = 0; i < g->descriptor->n_fields; i++)
		field_determine_objective_c_class_definitions(g->field_generators[i], fwd_decls);
	if (g->descriptor->containing_type)
		string_set_insert(fwd_decls, objc_class_declaration(lp_objc_class_name(g->descriptor->containing_type)));
}

static bool message_includes_oneof_definition(const MessageGenerator *g) { return g->n_oneof_generators > 0; }

/* ---- GenerateMessageHeader ---- */

static void message_comments(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	emit_comments_string(p, g->generation_options, g->descriptor, COMMENT_STRING_FORCE_MULTILINE);
}

static void message_fieldnum_enum(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	if (g->descriptor->n_fields == 0)
		return;
	LpSub subs[] = {LP_SUB_ANN("field_number_enum_name", xcat(lp_lookup_var(p, "classname"), "_FieldNumber"),
				   g->descriptor)};
	lp_emit(p, subs, 1,
		"\n"
		" typedef GPB_ENUM($field_number_enum_name$) {\n"
		"   $message_fieldnum_enum_values$,\n"
		" };\n");
	lp_emit(p, NULL, 0, "\n");
}

static void message_fieldnum_enum_values(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	const LpField **sorted_fields = sort_fields(g->descriptor, field_ordering_by_number);
	for (int i = 0; i < g->descriptor->n_fields; i++)
		field_generate_field_number_constant(g->field_generators[sorted_fields[i]->index], p);
}

static void message_oneof_enums(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	for (int i = 0; i < g->n_oneof_generators; i++)
		oneof_generate_case_enum(g->oneof_generators[i], p);
}

static void message_properties(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	bool *seen_oneofs = xalloc(sizeof(bool) * (size_t)(g->n_oneof_generators + 1));
	for (int i = 0; i < g->descriptor->n_fields; i++) {
		const LpField *field = &g->descriptor->fields[i];
		const LpOneof *oneof = proto_field_real_oneof(field);
		if (oneof && !seen_oneofs[oneof->index]) {
			seen_oneofs[oneof->index] = true;
			oneof_generate_public_case_property_declaration(g->oneof_generators[oneof->index], p);
		}
		field_generate_property_declaration(g->field_generators[i], p);
	}
}

static void message_wkt_extra(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	if (!is_wkt_with_objc_category(g->descriptor))
		return;
	lp_emit(p, NULL, 0,
		"\n"
		" // NOTE: There are some Objective-C specific methods/properties in\n"
		" // GPBWellKnownTypes.h that will likely be useful.\n");
	lp_emit(p, NULL, 0, "\n");
}

static void message_extension_methods(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = arg;
	for (int i = 0; i < g->extension_generators.n; i++)
		extension_generate_methods_header(g->extension_generators.v[i], p);
}

static void message_generate_class_based_extension_header(const MessageGenerator *g, LpPrinter *p)
{
	LpSub subs[] = {LP_SUB_CB("extension_methods", message_extension_methods, (void *)g)};
	lp_emit(p, subs, 1,
		"\n"
		" @interface $classname$ (DynamicMethods)\n"
		"\n"
		" $extension_methods$\n"
		" @end\n");
	lp_emit(p, NULL, 0, "\n");
}

static void message_generate_message_header(const MessageGenerator *g, LpPrinter *p)
{
	LpSub vars[] = {LP_SUB_ANN("classname", g->class_name, g->descriptor)};
	lp_push_vars(p, vars, 1);
	LpSub subs[] = {
		LP_SUB("deprecated_attribute", g->deprecated_attribute),
		LP_SUB_CB("message_comments", message_comments, (void *)g),
		LP_SUB_CB("message_fieldnum_enum", message_fieldnum_enum, (void *)g),
		LP_SUB_CB("message_fieldnum_enum_values", message_fieldnum_enum_values, (void *)g),
		LP_SUB_CB("oneof_enums", message_oneof_enums, (void *)g),
		LP_SUB_CB("message_properties", message_properties, (void *)g),
		LP_SUB_CB("wkt_extra", message_wkt_extra, (void *)g),
	};
	subs[0].consume_after = ";"; /* WithSuffix(";") */
	lp_emit(p, subs, 7,
		"\n"
		" #pragma mark - $classname$\n"
		"\n"
		" $message_fieldnum_enum$\n"
		" $oneof_enums$\n"
		" $message_comments$\n"
		" $deprecated_attribute$;\n"
		" GPB_FINAL @interface $classname$ : GPBMessage\n"
		"\n"
		" $message_properties$\n"
		" $wkt_extra$\n"
		" @end\n");
	lp_emit(p, NULL, 0, "\n");

	for (int i = 0; i < g->descriptor->n_fields; i++)
		field_generate_c_function_declarations(g->field_generators[i], p);

	if (g->n_oneof_generators) {
		for (int i = 0; i < g->n_oneof_generators; i++)
			oneof_generate_clear_function_declaration(g->oneof_generators[i], p);
		lp_emit(p, NULL, 0, "\n");
	}

	if (g->extension_generators.n) {
		if (emit_c_function_extensions(g->generation_options)) {
			lp_emit(p, NULL, 0,
				"\n"
				"#pragma mark - $classname$ Extensions\n"
				"\n");
			for (int i = 0; i < g->extension_generators.n; i++)
				extension_generate_functions_header(g->extension_generators.v[i], p);
			lp_emit(p, NULL, 0, "\n");
		}
		if (emit_class_based_extensions(g->generation_options))
			message_generate_class_based_extension_header(g, p);
	}
	lp_pop_vars(p);
}

/* ---- GenerateSource ---- */

typedef struct {
	const MessageGenerator *g;
	const LpField **sorted_fields;
	const LpField **size_order_fields;
	SimpleExtensionRange *sorted_extensions;
	int n_sorted_extensions;
	bool has_fields;
	bool need_defaults;
	Str text_format_data;
} MessageSourceArgs;

static void message_clang_diagnostic_push(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	if (!*a->g->deprecated_attribute)
		return;
	/* no warnings when compiling the impl of this deprecated class */
	lp_emit(p, NULL, 0,
		"\n"
		" #pragma clang diagnostic push\n"
		" #pragma clang diagnostic ignored \"-Wdeprecated-implementations\"\n");
	lp_emit(p, NULL, 0, "\n");
}

static void message_clang_diagnostic_pop(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	if (!*a->g->deprecated_attribute)
		return;
	lp_emit(p, NULL, 0,
		"\n"
		" #pragma clang diagnostic pop\n");
	lp_emit(p, NULL, 0, "\n");
}

static void message_property_implementation(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = ((const MessageSourceArgs *)arg)->g;
	for (int i = 0; i < g->n_oneof_generators; i++)
		oneof_generate_property_implementation(g->oneof_generators[i], p);
	for (int i = 0; i < g->descriptor->n_fields; i++)
		field_generate_property_implementation(g->field_generators[i], p);
}

static void message_storage_fields(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	for (int i = 0; i < a->g->descriptor->n_fields; i++)
		field_generate_field_storage_declaration(a->g->field_generators[a->size_order_fields[i]->index], p);
}

static void message_descriptor_startup(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = ((const MessageSourceArgs *)arg)->g;
	/* scoped extensions: start the root class, where the runtime support for extensions lives */
	if (g->extension_generators.n && emit_class_based_extensions(g->generation_options)) {
		lp_emit(p, NULL, 0,
			"\n"
			" // Start up the root class to support the scoped extensions.\n"
			" __unused Class rootStartup = [$root_class_name$ class];\n");
	}
}

static void message_declare_fields_static(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	if (!a->has_fields)
		return;
	lp_emit(p, NULL, 0,
		"\n"
		" static $field_description_type$ fields[] = {\n"
		"   $declare_fields_static_fields$,\n"
		" };\n");
}

static void message_declare_fields_static_fields(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	for (int i = 0; i < a->g->descriptor->n_fields; i++)
		field_generate_field_description(a->g->field_generators[a->sorted_fields[i]->index], p, a->need_defaults);
}

static void message_oneof_support(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = ((const MessageSourceArgs *)arg)->g;
	if (!g->n_oneof_generators)
		return;
	LpSub subs[] = {LP_SUB("first_has_index", oneof_has_index_as_string(g->oneof_generators[0]))};
	lp_emit(p, subs, 1,
		"\n"
		" static const char *oneofs[] = {\n"
		"   $declare_oneof_static_oneofs$,\n"
		" };\n"
		" [localDescriptor setupOneofs:oneofs\n"
		"                        count:(uint32_t)(sizeof(oneofs) / sizeof(char*))\n"
		"                firstHasIndex:$first_has_index$];\n");
}

static void message_declare_oneof_static_oneofs(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = ((const MessageSourceArgs *)arg)->g;
	for (int i = 0; i < g->n_oneof_generators; i++) {
		LpSub subs[] = {LP_SUB("name", oneof_descriptor_name(g->oneof_generators[i]))};
		lp_emit(p, subs, 1,
			"\n"
			" \"$name$\",\n");
	}
}

static void message_text_format_decode_support(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	if (!a->text_format_data.n)
		return;
	lp_emit(p, NULL, 0,
		"\n"
		" #if !GPBOBJC_SKIP_MESSAGE_TEXTFORMAT_EXTRAS\n"
		"   static const char *extraTextFormatInfo =\n"
		"     $text_format_decode_support_blob$\n"
		"   [localDescriptor setupExtraTextInfo:extraTextFormatInfo];\n"
		" #endif  // !GPBOBJC_SKIP_MESSAGE_TEXTFORMAT_EXTRAS\n");
}

static void message_text_format_decode_support_blob(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	enum { kBytesPerLine = 40 };
	Str blob = a->text_format_data;
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

static void message_ranges(LpPrinter *p, void *arg)
{
	const MessageSourceArgs *a = arg;
	for (int i = 0; i < a->n_sorted_extensions; i++) {
		LpSub subs[] = {
			LP_SUB("start", objc_itoa(a->sorted_extensions[i].start)),
			LP_SUB("end", objc_itoa(a->sorted_extensions[i].end)),
		};
		lp_emit(p, subs, 2, "{ .start = $start$, .end = $end$ },\n");
	}
}

static void message_extension_range_support(LpPrinter *p, void *arg)
{
	MessageSourceArgs *a = arg;
	if (!a->n_sorted_extensions)
		return;
	LpSub subs[] = {LP_SUB_CB("ranges", message_ranges, a)};
	lp_emit(p, subs, 1,
		"\n"
		" static const GPBExtensionRange ranges[] = {\n"
		"   $ranges$,\n"
		" };\n"
		" [localDescriptor setupExtensionRanges:ranges\n"
		"                                 count:(uint32_t)(sizeof(ranges) / sizeof(GPBExtensionRange))];\n");
}

static void message_containing_type_support(LpPrinter *p, void *arg)
{
	const MessageGenerator *g = ((const MessageSourceArgs *)arg)->g;
	if (!g->descriptor->containing_type)
		return;
	LpSub subs[] = {LP_SUB("parent_class_ref", objc_class(lp_objc_class_name(g->descriptor->containing_type)))};
	lp_emit(p, subs, 1,
		"\n"
		" [localDescriptor setupContainingMessageClass:$parent_class_ref$];\n");
}

static void message_generate_source(const MessageGenerator *g, LpPrinter *p)
{
	const LpMessage *d = g->descriptor;
	MessageSourceArgs a = {.g = g};
	a.sorted_fields = sort_fields(d, field_ordering_by_number);
	a.size_order_fields = sort_fields(d, field_ordering_by_storage_size);
	a.n_sorted_extensions = normalize_extension_ranges(d, &a.sorted_extensions);
	a.has_fields = d->n_fields > 0;
	a.need_defaults = field_generator_map_any_non_zero_default(d);

	TextFormatDecodeData text_format_decode_data = {0};
	for (int i = 0; i < d->n_fields; i++) {
		const FieldGenerator *fg = g->field_generators[a.sorted_fields[i]->index];
		if (field_needs_textformat_name_support(fg))
			tf_add_string(&text_format_decode_data, a.sorted_fields[i]->number, field_variable(fg, "name"),
				      field_variable(fg, "raw_field_name"));
	}
	a.text_format_data = tf_data(&text_format_decode_data);

	const char *field_description_type =
		a.need_defaults ? "GPBMessageFieldDescriptionWithDefault" : "GPBMessageFieldDescription";
	const char *init_flags[2];
	int n_init_flags = 0;
	if (a.need_defaults)
		init_flags[n_init_flags++] = "GPBDescriptorInitializationFlag_FieldsWithDefault";
	if (pb_int(d->options, MSGOPT_MESSAGE_SET_WIRE_FORMAT, 0))
		init_flags[n_init_flags++] = "GPBDescriptorInitializationFlag_WireFormat";

	LpSub subs[] = {
		LP_SUB("classname", g->class_name),
		LP_SUB_CB("clang_diagnostic_push", message_clang_diagnostic_push, &a),
		LP_SUB_CB("clang_diagnostic_pop", message_clang_diagnostic_pop, &a),
		LP_SUB_CB("property_implementation", message_property_implementation, &a),
		LP_SUB("sizeof_has_storage", objc_itoa(g->sizeof_has_storage)),
		LP_SUB_CB("storage_fields", message_storage_fields, &a),
		LP_SUB_CB("descriptor_startup", message_descriptor_startup, &a),
		LP_SUB("field_description_type", field_description_type),
		LP_SUB_CB("declare_fields_static", message_declare_fields_static, &a),
		LP_SUB_CB("declare_fields_static_fields", message_declare_fields_static_fields, &a),
		LP_SUB("message_name", d->name),
		LP_SUB("class_reference", objc_class(g->class_name)),
		LP_SUB("file_description_name", g->file_description_name),
		LP_SUB("fields", a.has_fields ? "fields" : "NULL"),
		LP_SUB("fields_count", a.has_fields ? xcat("(uint32_t)(sizeof(fields) / sizeof(", field_description_type, "))")
						    : "0"),
		LP_SUB("init_flags", build_flags_string(FLAGTYPE_DESCRIPTOR_INITIALIZATION, init_flags, n_init_flags)),
		LP_SUB_CB("oneof_support", message_oneof_support, &a),
		LP_SUB_CB("declare_oneof_static_oneofs", message_declare_oneof_static_oneofs, &a),
		LP_SUB_CB("text_format_decode_support", message_text_format_decode_support, &a),
		LP_SUB_CB("text_format_decode_support_blob", message_text_format_decode_support_blob, &a),
		LP_SUB_CB("extension_range_support", message_extension_range_support, &a),
		LP_SUB_CB("containing_type_support", message_containing_type_support, &a),
	};
	lp_emit(p, subs, (int)(sizeof subs / sizeof *subs),
		"\n"
		" #pragma mark - $classname$\n"
		"\n"
		" $clang_diagnostic_push$;\n"
		" @implementation $classname$\n"
		"\n"
		" $property_implementation$\n"
		"\n"
		" typedef struct $classname$__storage_ {\n"
		"   uint32_t _has_storage_[$sizeof_has_storage$];\n"
		"   $storage_fields$,\n"
		" } $classname$__storage_;\n"
		"\n"
		" // This method is threadsafe because it is initially called\n"
		" // in +initialize for each subclass.\n"
		" + (GPBDescriptor *)descriptor {\n"
		"   static GPBDescriptor *descriptor = nil;\n"
		"   if (!descriptor) {\n"
		"     $descriptor_startup$;\n"
		"     $declare_fields_static$;\n"
		"     GPBDescriptor *localDescriptor =\n"
		"         [GPBDescriptor allocDescriptorForClass:$class_reference$\n"
		"                                    messageName:@\"$message_name$\"\n"
		"                                 runtimeSupport:&$google_protobuf_runtime_support$\n"
		"                                fileDescription:&$file_description_name$\n"
		"                                         fields:$fields$\n"
		"                                     fieldCount:$fields_count$\n"
		"                                    storageSize:sizeof($classname$__storage_)\n"
		"                                          flags:$init_flags$];\n"
		"     $oneof_support$;\n"
		"     $text_format_decode_support$;\n"
		"     $extension_range_support$;\n"
		"     $containing_type_support$;\n"
		"     #if defined(DEBUG) && DEBUG\n"
		"       NSAssert(descriptor == nil, @\"Startup recursed!\");\n"
		"     #endif  // DEBUG\n"
		"     descriptor = localDescriptor;\n"
		"   }\n"
		"   return descriptor;\n"
		" }\n"
		"\n"
		" @end\n"
		"\n"
		" $clang_diagnostic_pop$;\n");

	for (int i = 0; i < d->n_fields; i++)
		field_generate_c_function_implementations(g->field_generators[i], p);
	for (int i = 0; i < g->n_oneof_generators; i++)
		oneof_generate_clear_function_implementation(g->oneof_generators[i], p);
}
