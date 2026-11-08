// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/java/file.{h,cc}: FileGenerator. */

typedef struct {
	const LpFile *file;
	char *java_package;
	JvMessageGen **message_generators;
	JvExtensionGen **extension_generators;
	JvContext *context;
	JvGeneratorFactory *generator_factory;
	JvOptions options;
	bool immutable_api;
	char *classname;
} JvFileGenerator;

/* CollectExtensions(message, extensions): false if there are unknown fields. extensions is
 * the FieldDescriptorSet (sorted by full name, unique). */
static bool collect_extensions_message(const PbMsg *message, Vec *extensions, int depth)
{
	if (message->unknown.n > 0)
		return false;
	if (depth > 200)
		return true;
	for (int i = 0; i < message->def->n; i++) {
		const PbSlot *s = &message->slots[i];
		if (!s->n)
			continue;
		const LpField *field = lp_msg_slot_field(message, i);
		if (field->is_extension)
			vec_push(extensions, (void *)field);
		if (jv_get_java_type(field) == JAVATYPE_MESSAGE && message->def->f[i].type != PT_BYTES) {
			for (int j = 0; j < s->n; j++)
				if (!collect_extensions_message(s->v[j].m, extensions, depth + 1))
					return false;
		}
	}
	return true;
}

static int cmp_field_full_name(const void *a, const void *b)
{
	return strcmp((*(const LpField *const *)a)->full_name, (*(const LpField *const *)b)->full_name);
}

/* sort by full name and drop duplicates (btree_set) */
static void field_set_normalize(Vec *v)
{
	if (v->n == 0)
		return;
	qsort(v->v, (size_t)v->n, sizeof(void *), cmp_field_full_name);
	int k = 0;
	for (int i = 0; i < v->n; i++)
		if (k == 0 || VEC_AT(*v, LpField, k - 1) != VEC_AT(*v, LpField, i))
			v->v[k++] = v->v[i];
	v->n = k;
}

static void collect_public_dependencies(const LpFile *file, Map *dependencies)
{
	if (!file || map_has(dependencies, file->name))
		return;
	map_put(dependencies, file->name, (void *)file);
	for (int i = 0; i < file->n_public_dependencies; i++)
		collect_public_dependencies(file->dependencies[file->public_dependencies[i]], dependencies);
}

/* CollectExtensions(file, options, extensions, optional_extensions) */
static void collect_extensions_file(const LpFile *file, const JvOptions *options, Vec *extensions,
				    Vec *optional_extensions)
{
	PbMsg *file_proto = lp_strip_source_retention_options(file);
	Buf file_data = {0};
	lp_msg_serialize(file_proto, &file_data);
	if (!proto_pool_find_message(file->pool, "google.protobuf.FileDescriptorProto"))
		return;
	PbMsg *dynamic_file_proto =
		lp_dynamic_message_parse(file, "google.protobuf.FileDescriptorProto", buf_view(&file_data));
	JV_CHECK(dynamic_file_proto != NULL);
	extensions->n = 0;
	collect_extensions_message(dynamic_file_proto, extensions, 0);
	field_set_normalize(extensions);
	if (options->strip_nonfunctional_codegen) {
		int k = 0;
		for (int i = 0; i < extensions->n; i++) {
			const LpField *f = VEC_AT(*extensions, LpField, i);
			if (!str_eq(f->containing_type->full_name, "google.protobuf.FeatureSet"))
				extensions->v[k++] = (void *)f;
		}
		extensions->n = k;
	}
	Map dependencies = {0};
	map_put(&dependencies, file->name, (void *)file);
	for (int i = 0; i < file->n_dependencies; i++)
		collect_public_dependencies(file->dependencies[i], &dependencies);
	int k = 0;
	for (int i = 0; i < extensions->n; i++) {
		const LpField *f = VEC_AT(*extensions, LpField, i);
		if (map_get(&dependencies, f->file->name) != f->file)
			vec_push(optional_extensions, (void *)f);
		else
			extensions->v[k++] = (void *)f;
	}
	extensions->n = k;
	field_set_normalize(optional_extensions);
}

/* MaybeRestartJavaMethod */
static void maybe_restart_java_method(LpPrinter *printer, int *bytecode_estimate, const char *method_name,
				      int *method_num, const char *chain_statement, const char *method_decl)
{
	if (*bytecode_estimate > JV_MAX_STATIC_SIZE) {
		++(*method_num);
		lp_print(printer, chain_statement, "method_name", method_name, "method_num", jv_i(*method_num), NULL);
		lp_outdent(printer);
		JV_PRINT(printer, "}\n");
		lp_print(printer, method_decl, "method_name", method_name, "method_num", jv_i(*method_num), NULL);
		lp_indent(printer);
		*bytecode_estimate = 0;
	}
}

static JvFileGenerator *jv_file_generator_new(const LpFile *file, const JvOptions *options, bool immutable_api)
{
	JvFileGenerator *g = xalloc(sizeof *g);
	g->file = file;
	g->java_package = jv_file_java_package(file);
	g->context = jv_context_new(file, options);
	if (!immutable_api)
		fail("Check failed: immutable_api Open source release does not support the mutable API");
	g->generator_factory = xalloc(sizeof *g->generator_factory);
	g->generator_factory->context = g->context;
	g->generator_factory->lite = !jv_has_descriptor_methods(jv_context_enforce_lite(g->context));
	g->options = *options;
	g->immutable_api = immutable_api;
	g->classname = jv_get_file_class_name(file, immutable_api, false);
	g->message_generators = xalloc(sizeof(JvMessageGen *) * (size_t)(file->n_messages + 1));
	for (int i = 0; i < file->n_messages; i++)
		g->message_generators[i] = jv_new_message_generator(g->generator_factory, &file->messages[i]);
	g->extension_generators = xalloc(sizeof(JvExtensionGen *) * (size_t)(file->n_extensions + 1));
	for (int i = 0; i < file->n_extensions; i++)
		g->extension_generators[i] = jv_new_extension_generator(g->generator_factory, &file->extensions[i]);
	return g;
}

static void validate_enum(const LpFile *file, const LpEnum *enm, Buf *error)
{
	if (enm->containing_type && str_eq(enm->containing_type->name, enm->name))
		buf_adds(error, xcat(file->name, ": Cannot generate Java output because the enum \"", enm->full_name,
				     "\" would be an enum nested inside a class with the same name, "
				     "which is not allowed in the Java language. "
				     "Please rename either the enum or containing message name.\n"));
}

static void validate_field(const LpField *field, Buf *error)
{
	if (field->enum_type && !jv_support_unknown_enum_value(field) && !proto_field_has_presence(field) &&
	    !proto_field_is_repeated(field))
		buf_adds(error, xcat("Field ", field->full_name, " has a closed enum type with implicit presence.\n"));
}

static void validate_enum2(const LpEnum *enm, Buf *error)
{
	if (jv_check_large_enum(enm) && proto_enum_is_closed(enm))
		buf_adds(error, xcat(enm->full_name,
				     " is a closed enum and can not be used with the large_enum feature.  "
				     "Please migrate to an open enum first, which is a better fit for "
				     "extremely large enums.\n"));
	const char *status = jv_validate_nest_in_file_class_enum(enm);
	if (status)
		buf_adds(error, status);
}

/* VisitDescriptors in the order of descriptor_visitor.h: a message, then its enums, fields,
 * nested messages and extensions; a file's messages, enums and extensions. Each pass below is
 * one VisitDescriptors call of FileGenerator::Validate. */
typedef enum { VISIT_ENUM_NESTED_NAME, VISIT_FIELD, VISIT_ENUM, VISIT_MESSAGE } VisitKind;

static void visit_message(const LpFile *file, const LpMessage *m, VisitKind kind, Buf *error);

static void visit_enum(const LpFile *file, const LpEnum *e, VisitKind kind, Buf *error)
{
	if (kind == VISIT_ENUM_NESTED_NAME)
		validate_enum(file, e, error);
	else if (kind == VISIT_ENUM)
		validate_enum2(e, error);
}

static void visit_field(const LpField *f, VisitKind kind, Buf *error)
{
	if (kind == VISIT_FIELD)
		validate_field(f, error);
}

static void visit_message(const LpFile *file, const LpMessage *m, VisitKind kind, Buf *error)
{
	if (kind == VISIT_MESSAGE) {
		const char *status = jv_validate_nest_in_file_class_message(m);
		if (status)
			buf_adds(error, status);
	}
	for (int i = 0; i < m->n_enums; i++)
		visit_enum(file, &m->enums[i], kind, error);
	for (int i = 0; i < m->n_fields; i++)
		visit_field(&m->fields[i], kind, error);
	for (int i = 0; i < m->n_nested; i++)
		visit_message(file, &m->nested[i], kind, error);
	for (int i = 0; i < m->n_extensions; i++)
		visit_field(&m->extensions[i], kind, error);
}

static void visit_file(const LpFile *file, VisitKind kind, Buf *error)
{
	for (int i = 0; i < file->n_messages; i++)
		visit_message(file, &file->messages[i], kind, error);
	for (int i = 0; i < file->n_enums; i++)
		visit_enum(file, &file->enums[i], kind, error);
	for (int i = 0; i < file->n_extensions; i++)
		visit_field(&file->extensions[i], kind, error);
}

static bool jv_file_generator_validate(JvFileGenerator *g, Buf *error)
{
	if (strpbrk(g->java_package, ";\r\n "))
		buf_adds(error, xcat(g->file->name, ": java_package contains invalid characters (", g->java_package, ")\n"));
	if (strpbrk(g->classname, ";\r\n "))
		buf_adds(error, xcat(g->file->name, ": classname contains invalid characters (", g->classname, ")\n"));
	if (jv_has_conflicting_class_name(g->file, g->classname, JV_EXACT_EQUAL)) {
		error->n = 0;
		buf_adds(error, xcat(g->file->name,
				     ": Cannot generate Java output because the file's outer class name, "
				     "\"",
				     g->classname,
				     "\", matches the name of one of the types declared inside it.  "
				     "Please either rename the type or use the java_outer_classname "
				     "option to specify a different outer class name for the .proto file."));
		return false;
	}
	/* EQUAL_IGNORE_CASE and optimize_for = LITE_RUNTIME only log warnings */
	visit_file(g->file, VISIT_ENUM_NESTED_NAME, error);
	visit_file(g->file, VISIT_FIELD, error);
	visit_file(g->file, VISIT_ENUM, error);
	visit_file(g->file, VISIT_MESSAGE, error);
	if (error->n)
		return false;
	/* not ported: GenerateLarge (internal_helpers.cc), the code of the large_enum feature */
	for (int pass = 0; pass < 1; pass++) {
		Vec todo = {0};
		for (int i = 0; i < g->file->n_enums; i++)
			vec_push(&todo, &g->file->enums[i]);
		Vec msgs = {0};
		for (int i = 0; i < g->file->n_messages; i++)
			vec_push(&msgs, &g->file->messages[i]);
		for (int i = 0; i < msgs.n; i++) {
			const LpMessage *m = VEC_AT(msgs, LpMessage, i);
			for (int j = 0; j < m->n_enums; j++)
				vec_push(&todo, &m->enums[j]);
			for (int j = 0; j < m->n_nested; j++)
				vec_push(&msgs, &m->nested[j]);
		}
		for (int i = 0; i < todo.n; i++)
			if (jv_check_large_enum(VEC_AT(todo, LpEnum, i))) {
				buf_adds(error, xcat("unsupported by protoc-gen-uni: feature pb.java.large_enum (",
						     VEC_AT(todo, LpEnum, i)->full_name, ")"));
				return false;
			}
	}
	return true;
}

static void generate_descriptor_initialization_code_for_immutable(JvFileGenerator *g, LpPrinter *printer);

static void jv_file_generator_generate(JvFileGenerator *g, LpPrinter *printer)
{
	const LpFile *file = g->file;
	bool descriptor_methods = jv_has_descriptor_methods(jv_context_enforce_lite(g->context));
	lp_print(printer,
		 "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
		 "// NO CHECKED-IN PROTOBUF "
		 "GENCODE\n"
		 "// source: $filename$\n",
		 "filename", file->name, NULL);
	lp_print(printer, "// Protobuf Java Version: $protobuf_java_version$\n", "protobuf_java_version", "4.36.2", NULL);
	JV_PRINT(printer, "\n");
	if (*g->java_package)
		lp_print(printer,
			 "package $package$;\n"
			 "\n",
			 "package", g->java_package, NULL);
	jv_print_generated_annotation(printer, '$', g->options.annotate_code ? xcat(g->classname, ".java.pb.meta") : "",
				      &g->options);
	lp_print(printer,
		 "$deprecation$public final class $classname$ $extends${\n"
		 "  private $ctor$() {}\n",
		 "deprecation", jv_file_deprecated(file) ? "@java.lang.Deprecated " : "", "classname", g->classname, "ctor",
		 g->classname, "extends", descriptor_methods ? "extends com.google.protobuf.GeneratedFile " : "", NULL);
	lp_indent(printer);

	if (!jv_context_enforce_lite(g->context)) {
		JV_PRINT(printer, "static {\n");
		lp_indent(printer);
		jv_print_gencode_version_validator(printer, true, g->classname);
		lp_outdent(printer);
		JV_PRINT(printer, "}\n");
	}

	JV_PRINT(printer, "public static void registerAllExtensions(\n"
			  "    com.google.protobuf.ExtensionRegistryLite registry) {\n");
	lp_indent(printer);
	for (int i = 0; i < file->n_extensions; i++)
		jv_extension_generator_generate_registration_code(g->generator_factory, g->extension_generators[i], printer);
	for (int i = 0; i < file->n_messages; i++)
		jv_message_generator_generate_extension_registration_code(g->message_generators[i], printer);
	lp_outdent(printer);
	JV_PRINT(printer, "}\n");
	if (descriptor_methods)
		JV_PRINT(printer, "\n"
				  "public static void registerAllExtensions(\n"
				  "    com.google.protobuf.ExtensionRegistry registry) {\n"
				  "  registerAllExtensions(\n"
				  "      (com.google.protobuf.ExtensionRegistryLite) registry);\n"
				  "}\n");

	for (int i = 0; i < file->n_enums; i++)
		if (jv_nested_in_file_class_enum(&file->enums[i], g->immutable_api))
			jv_enum_generator_generate(g->generator_factory, &file->enums[i], printer);
	for (int i = 0; i < file->n_messages; i++)
		if (jv_nested_in_file_class_message(&file->messages[i], g->immutable_api)) {
			jv_message_generator_generate_interface(g->message_generators[i], printer);
			jv_message_generator_generate(g->message_generators[i], printer);
		}
	if (jv_has_generic_services(file, jv_context_enforce_lite(g->context)))
		for (int i = 0; i < file->n_services; i++)
			if (jv_nested_in_file_class_service(&file->services[i], g->immutable_api))
				jv_service_generator_generate(g->generator_factory, &file->services[i], printer);

	for (int i = 0; i < file->n_extensions; i++)
		jv_extension_generator_generate(g->generator_factory, g->extension_generators[i], printer);

	int static_block_bytecode_estimate = 0;
	for (int i = 0; i < file->n_messages; i++)
		jv_message_generator_generate_static_variables(g->message_generators[i], printer,
							       &static_block_bytecode_estimate);

	JV_PRINT(printer, "\n");

	if (descriptor_methods) {
		if (g->immutable_api)
			generate_descriptor_initialization_code_for_immutable(g, printer);
	} else {
		JV_PRINT(printer, "static {\n");
		lp_indent(printer);
		int bytecode_estimate = 0;
		int method_num = 0;
		const char *method_name = "_clinit_autosplit";
		for (int i = 0; i < file->n_messages; i++) {
			bytecode_estimate +=
				jv_message_generator_generate_static_variable_initializers(g->message_generators[i], printer);
			maybe_restart_java_method(printer, &bytecode_estimate, method_name, &method_num,
						  "$method_name$_$method_num$();\n",
						  "private static void $method_name$_$method_num$() {\n");
		}
		lp_outdent(printer);
		JV_PRINT(printer, "}\n");
	}

	JV_PRINT(printer, "\n"
			  "// @@protoc_insertion_point(outer_class_scope)\n");
	lp_outdent(printer);
	JV_PRINT(printer, "}\n");
}

/* ShouldIncludeDependency */
static bool should_include_dependency(JvFileGenerator *g, const LpFile *descriptor, bool immutable_api)
{
	(void)immutable_api;
	/* IsKnownFeatureProto */
	if (g->options.strip_nonfunctional_codegen &&
	    (str_eq(descriptor->name, "google/protobuf/cpp_features.proto") ||
	     str_eq(descriptor->name, "google/protobuf/java_features.proto")))
		return false;
	return true;
}

static void generate_descriptor_initialization_code_for_immutable(JvFileGenerator *g, LpPrinter *printer)
{
	const LpFile *file = g->file;
	JV_PRINT(printer, "public static com.google.protobuf.Descriptors.FileDescriptor\n"
			  "    getDescriptor() {\n"
			  "  return descriptor;\n"
			  "}\n"
			  "private static final com.google.protobuf.Descriptors.FileDescriptor\n"
			  "    descriptor;\n"
			  "static {\n");
	lp_indent(printer);
	jv_shared_code_generator_generate_descriptors(file, &g->options, printer);

	const char *method_name = "_clinit_autosplit_dinit";
	int bytecode_estimate = 0;
	int method_num = 0;
	for (int i = 0; i < file->n_messages; i++) {
		bytecode_estimate +=
			jv_message_generator_generate_static_variable_initializers(g->message_generators[i], printer);
		maybe_restart_java_method(printer, &bytecode_estimate, method_name, &method_num,
					  "$method_name$_$method_num$();\n",
					  "private static void $method_name$_$method_num$() {\n");
	}
	for (int i = 0; i < file->n_extensions; i++) {
		bytecode_estimate += jv_extension_generator_generate_non_nested_initialization_code(
			g->generator_factory, g->extension_generators[i], printer);
		maybe_restart_java_method(printer, &bytecode_estimate, method_name, &method_num,
					  "$method_name$_$method_num$();\n",
					  "private static void $method_name$_$method_num$() {\n");
	}
	JV_PRINT(printer, "descriptor.resolveAllFeaturesImmutable();\n");

	Vec extensions = {0}, optional_extensions = {0};
	collect_extensions_file(file, &g->options, &extensions, &optional_extensions);

	for (int i = 0; i < file->n_dependencies; i++)
		if (should_include_dependency(g, file->dependencies[i], true))
			lp_print(printer, "$dependency$.getDescriptor();\n", "dependency",
				 jv_immutable_class_name(file->dependencies[i]), NULL);

	if (extensions.n || optional_extensions.n) {
		JV_PRINT(printer, "com.google.protobuf.ExtensionRegistry registry =\n"
				  "    com.google.protobuf.ExtensionRegistry.newInstance();\n");
		for (int i = 0; i < extensions.n; i++) {
			JvExtensionGen *eg = jv_new_extension_generator(g->generator_factory, VEC_AT(extensions, LpField, i));
			bytecode_estimate += jv_extension_generator_generate_registration_code(g->generator_factory, eg, printer);
			maybe_restart_java_method(printer, &bytecode_estimate, method_name, &method_num,
						  "$method_name$_$method_num$(registry);\n",
						  "private static void $method_name$_$method_num$(\n"
						  "    com.google.protobuf.ExtensionRegistry registry) {\n");
		}
		for (int i = 0; i < optional_extensions.n; i++) {
			const LpField *field = VEC_AT(optional_extensions, LpField, i);
			jv_new_extension_generator(g->generator_factory, field);
			LpSub subs[] = {
				LP_SUB("scope", field->extension_scope ? jv_immutable_class_name(field->extension_scope)
								       : jv_immutable_class_name(field->file)),
				LP_SUB("name", jv_underscores_to_camel_case_check_reserved(field)),
			};
			lp_emit(printer, subs, 2,
				"\n"
				"                      addOptionalExtension(registry, \"$scope$\", \"$name$\");\n"
				"                    ");
			bytecode_estimate += 8;
			maybe_restart_java_method(printer, &bytecode_estimate, method_name, &method_num,
						  "$method_name$_$method_num$(registry);\n",
						  "private static void $method_name$_$method_num$(\n"
						  "    com.google.protobuf.ExtensionRegistry registry) {\n");
		}
		JV_PRINT(printer, "com.google.protobuf.Descriptors.FileDescriptor\n"
				  "    .internalUpdateFileDescriptor(descriptor, registry);\n");
	}

	lp_outdent(printer);
	JV_PRINT(printer, "}\n");
}

/* GenerateSibling */
typedef enum { SIB_ENUM, SIB_INTERFACE, SIB_MESSAGE, SIB_SERVICE } SiblingKind;

static void generate_sibling(JvFileGenerator *g, const char *package_dir, const char *name, const char *name_suffix,
			     LpContext *context, Vec *file_list, SiblingKind kind, const void *descriptor,
			     JvMessageGen *mg)
{
	char *filename = xcat(package_dir, name, name_suffix, ".java");
	vec_push(file_list, filename);
	LpPrinter printer;
	lp_printer_init(&printer, lp_context_open(context, filename), '$');
	lp_print(&printer,
		 "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
		 "// NO CHECKED-IN PROTOBUF "
		 "GENCODE\n"
		 "// source: $filename$\n",
		 "filename", g->file->name, NULL);
	lp_print(&printer, "// Protobuf Java Version: $protobuf_java_version$\n", "protobuf_java_version", "4.36.2", NULL);
	JV_PRINT(&printer, "\n");
	if (*g->java_package)
		lp_print(&printer,
			 "package $package$;\n"
			 "\n",
			 "package", g->java_package, NULL);
	switch (kind) {
	case SIB_ENUM: jv_enum_generator_generate(g->generator_factory, descriptor, &printer); break;
	case SIB_INTERFACE: jv_message_generator_generate_interface(mg, &printer); break;
	case SIB_MESSAGE: jv_message_generator_generate(mg, &printer); break;
	case SIB_SERVICE: jv_service_generator_generate(g->generator_factory, descriptor, &printer); break;
	}
}

static void jv_file_generator_generate_siblings(JvFileGenerator *g, const char *package_dir, LpContext *context,
						Vec *file_list, Vec *annotation_list)
{
	(void)annotation_list;
	const LpFile *file = g->file;
	for (int i = 0; i < file->n_enums; i++) {
		if (jv_nested_in_file_class_enum(&file->enums[i], g->immutable_api))
			continue;
		generate_sibling(g, package_dir, file->enums[i].name, "", context, file_list, SIB_ENUM, &file->enums[i],
				 NULL);
	}
	for (int i = 0; i < file->n_messages; i++) {
		if (jv_nested_in_file_class_message(&file->messages[i], g->immutable_api))
			continue;
		if (g->immutable_api)
			generate_sibling(g, package_dir, file->messages[i].name, "OrBuilder", context, file_list,
					 SIB_INTERFACE, &file->messages[i], g->message_generators[i]);
		generate_sibling(g, package_dir, file->messages[i].name, "", context, file_list, SIB_MESSAGE,
				 &file->messages[i], g->message_generators[i]);
	}
	if (jv_has_generic_services(file, jv_context_enforce_lite(g->context)))
		for (int i = 0; i < file->n_services; i++) {
			if (jv_nested_in_file_class_service(&file->services[i], g->immutable_api))
				continue;
			generate_sibling(g, package_dir, file->services[i].name, "", context, file_list, SIB_SERVICE,
					 &file->services[i], NULL);
		}
}
