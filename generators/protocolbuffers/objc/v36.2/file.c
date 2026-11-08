// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/file.{h,cc} (protobuf v36.2). */

/* also in GPBBootstrap.h */
#define GOOGLE_PROTOBUF_OBJC_VERSION "40311"

static const char kHeaderExtension[] = ".pbobjc.h";

static bool is_map_entry_message(const LpMessage *descriptor)
{
	return pb_int(descriptor->options, MSGOPT_MAP_ENTRY, 0) != 0;
}

/* MessageContainsExtensions: on the message or a nested one; include_custom_options decides
 * whether custom options count. */
static bool message_contains_extensions(const LpMessage *message, bool include_custom_options)
{
	if (message->n_extensions > 0) {
		if (include_custom_options)
			return true;
		for (int i = 0; i < message->n_extensions; i++)
			if (!extension_is_custom_option(&message->extensions[i]))
				return true;
	}
	for (int i = 0; i < message->n_nested; i++)
		if (message_contains_extensions(&message->nested[i], include_custom_options))
			return true;
	return false;
}

static bool file_contains_extensions(const LpFile *file, bool include_custom_options)
{
	if (file->n_extensions > 0) {
		if (include_custom_options)
			return true;
		for (int i = 0; i < file->n_extensions; i++)
			if (!extension_is_custom_option(&file->extensions[i]))
				return true;
	}
	for (int i = 0; i < file->n_messages; i++)
		if (message_contains_extensions(&file->messages[i], include_custom_options))
			return true;
	return false;
}

static bool is_direct_dependency(const LpFile *dep, const LpFile *file)
{
	for (int i = 0; i < file->n_dependencies; i++)
		if (dep == file->dependencies[i])
			return true;
	return false;
}

static void make_descriptors(const LpMessage *descriptor, const char *file_description_name, Vec *enum_generators,
			     Vec *extension_generators, Vec *message_generators,
			     const GenerationOptions *generation_options)
{
	for (int i = 0; i < descriptor->n_enums; i++)
		vec_push(enum_generators, enum_generator_new(&descriptor->enums[i], generation_options));
	for (int i = 0; i < descriptor->n_nested; i++) {
		const LpMessage *message_type = &descriptor->nested[i];
		/* map entries have no extensions or sub messages */
		if (is_map_entry_message(message_type))
			continue;
		MessageGenerator *m = message_generator_new(file_description_name, message_type, generation_options);
		vec_push(message_generators, m);
		message_add_extension_generators(m, extension_generators);
		make_descriptors(message_type, file_description_name, enum_generators, extension_generators,
				 message_generators, generation_options);
	}
}

static void emit_link_wkts(const char *name, LpPrinter *p)
{
	const char *slash = strrchr(name, '/');
	const char *basename = slash ? slash + 1 : name;
	LpSub subs[] = {LP_SUB("basename", lp_strip_proto(basename))};
	lp_emit(p, subs, 1,
		"\n"
		" // This is to help make sure that the GPBWellKnownTypes.* categories get linked and\n"
		" // developers do not have to use the `-ObjC` linker flag. More information\n"
		" // here: https://medium.com/ios-os-x-development/categories-in-static-libraries-78e41f8ddb96\n"
		" __attribute__((used)) static NSString* $basename$_importCategories(void) {\n"
		"   return GPBWellKnownTypesErrorDomain;\n"
		" }\n");
	lp_emit(p, NULL, 0, "\n");
}

static void emit_source_fwd_decls(StringSet *fwd_decls, LpPrinter *p)
{
	if (!fwd_decls->v.n)
		return;
	LpSub subs[] = {LP_SUB("fwd_decls", string_set_join(fwd_decls, "\n"))};
	lp_emit(p, subs, 1,
		"\n"
		" #pragma mark - Objective-C Class declarations\n"
		" // Forward declarations of Objective-C classes that we can use as\n"
		" // static values in struct initializers.\n"
		" // We don't use [Foo class] because it is not a static value.\n"
		" $fwd_decls$\n");
	lp_emit(p, NULL, 0, "\n");
}

/* ---- CommonState ---- */

typedef struct {
	bool has_extensions;
	FileSet min_deps;        /* the minimal dependencies that cover all those with extensions */
	FileSet transitive_deps;
} MinDepsEntry;

static void file_set_insert_all(FileSet *dst, const FileSet *src)
{
	for (int i = 0; i < src->v.n; i++)
		if (file_set_contains(src, src->v.v[i]))
			file_set_insert(dst, src->v.v[i]);
}

/* CollectMinimalFileDepsContainingExtensionsInternal for a file whose dependencies are cached. */
static MinDepsEntry *collect_min_deps_entry(CommonState *state, const LpFile *file)
{
	FileSet min_deps_collector = {0}, transitive_deps_collector = {0}, to_prune = {0};
	for (int i = 0; i < file->n_dependencies; i++) {
		const LpFile *dep = file->dependencies[i];
		const MinDepsEntry *dep_info = map_get(&state->deps_info_cache, file_key(dep));
		/* everything the dep covered, this file covers too; and it is pruned from the min deps
		 * in case another dep lists it */
		file_set_insert_all(&transitive_deps_collector, &dep_info->transitive_deps);
		file_set_insert_all(&to_prune, &dep_info->transitive_deps);
		if (dep_info->has_extensions) {
			/* add this file, prune its min deps and add them to the covered ones */
			file_set_insert(&min_deps_collector, dep);
			file_set_insert_all(&to_prune, &dep_info->min_deps);
			file_set_insert_all(&transitive_deps_collector, &dep_info->min_deps);
		} else {
			file_set_insert_all(&min_deps_collector, &dep_info->min_deps);
		}
	}
	MinDepsEntry *e = xalloc(sizeof *e);
	e->has_extensions = file_contains_extensions(file, state->include_custom_options);
	e->transitive_deps = transitive_deps_collector;
	/* fast path: nothing to prune or a single dep */
	if (to_prune.size == 0 || file->n_dependencies == 1) {
		e->min_deps = min_deps_collector;
	} else {
		for (int i = 0; i < min_deps_collector.v.n; i++) {
			const LpFile *f = min_deps_collector.v.v[i];
			if (file_set_contains(&min_deps_collector, f) && !file_set_contains(&to_prune, f))
				file_set_insert(&e->min_deps, f);
		}
	}
	map_put(&state->deps_info_cache, file_key(file), e);
	return e;
}

/* CollectMinimalFileDepsContainingExtensionsInternal, the recursion over the dependencies done
 * with an explicit stack (dependency chains come from the input). */
static const MinDepsEntry *collect_min_deps_internal(CommonState *state, const LpFile *file)
{
	MinDepsEntry *cached = map_get(&state->deps_info_cache, file_key(file));
	if (cached)
		return cached;
	Vec stack = {0};
	Vec next_dep = {0}; /* intptr_t index of the next dependency to visit */
	vec_push(&stack, (void *)file);
	vec_push(&next_dep, (void *)(intptr_t)0);
	while (stack.n) {
		const LpFile *f = stack.v[stack.n - 1];
		intptr_t i = (intptr_t)next_dep.v[next_dep.n - 1];
		if (i < f->n_dependencies) {
			next_dep.v[next_dep.n - 1] = (void *)(i + 1);
			const LpFile *dep = f->dependencies[i];
			if (!map_get(&state->deps_info_cache, file_key(dep))) {
				vec_push(&stack, (void *)dep);
				vec_push(&next_dep, (void *)(intptr_t)0);
			}
			continue;
		}
		if (!map_get(&state->deps_info_cache, file_key(f)))
			collect_min_deps_entry(state, f);
		stack.n--;
		next_dep.n--;
	}
	return map_get(&state->deps_info_cache, file_key(file));
}

/* CollectMinimalFileDepsContainingExtensions: sorted by name (pointer order isn't stable). */
static Vec collect_minimal_file_deps_containing_extensions(CommonState *state, const LpFile *file)
{
	const MinDepsEntry *e = collect_min_deps_internal(state, file);
	Vec result = {0};
	for (int i = 0; i < e->min_deps.v.n; i++)
		if (file_set_contains(&e->min_deps, e->min_deps.v.v[i]))
			vec_push(&result, e->min_deps.v.v[i]);
	sort_files_by_name(&result);
	return result;
}

/* ---- FileGenerator ---- */

static bool file_headers_use_forward_declarations(const FileGenerator *g)
{
	/* the bundled protos (WKTs) don't use forward declarations */
	return !g->is_bundled_proto && g->generation_options->headers_use_forward_declarations;
}

static void file_generator_init(FileGenerator *g, int edition, const LpFile *file,
				const GenerationOptions *generation_options, CommonState *common_state)
{
	*g = (FileGenerator){0};
	g->edition = edition;
	g->file = file;
	g->generation_options = generation_options;
	g->common_state = common_state;
	g->root_class_name = lp_objc_file_class_name(file);
	g->file_unique_symbol_name = lp_objc_file_unique_symbol_name(file, "");
	g->file_registry_function_name = lp_objc_extension_registry_function_name(file);
	g->file_description_name = xcat(lp_objc_file_class_name(file), "_FileDescription");
	g->is_bundled_proto = lp_objc_is_protobuf_library_bundled_proto_file(file);
	for (int i = 0; i < file->n_enums; i++)
		vec_push(&g->enum_generators, enum_generator_new(&file->enums[i], generation_options));
	for (int i = 0; i < file->n_extensions; i++) {
		const LpField *extension = &file->extensions[i];
		if (!generation_options->strip_custom_options || !extension_is_custom_option(extension))
			vec_push(&g->extension_generators, extension_generator_new(extension, generation_options));
	}
	g->file_scoped_extension_count = (size_t)g->extension_generators.n;
	for (int i = 0; i < file->n_messages; i++) {
		const LpMessage *message_type = &file->messages[i];
		if (is_map_entry_message(message_type))
			continue;
		MessageGenerator *m = message_generator_new(g->file_description_name, message_type, generation_options);
		vec_push(&g->message_generators, m);
		message_add_extension_generators(m, &g->extension_generators);
		make_descriptors(message_type, g->file_description_name, &g->enum_generators, &g->extension_generators,
				 &g->message_generators, generation_options);
	}
}

/* PublicDepsHandling */
typedef enum { PUBLIC_DEPS_AS_USED, PUBLIC_DEPS_FORCE_INCLUDE, PUBLIC_DEPS_EXCLUDE } PublicDepsHandling;

/* DetermineNeededDeps: the deps needed for types (not those only defining custom options). */
static void file_determine_needed_deps(const FileGenerator *g, FileSet *deps, PublicDepsHandling public_deps_handling)
{
	const LpFile *file = g->file;
	if (public_deps_handling == PUBLIC_DEPS_FORCE_INCLUDE)
		for (int i = 0; i < file->n_public_dependencies; i++)
			file_set_insert(deps, file->dependencies[file->public_dependencies[i]]);
	for (int i = 0; i < g->message_generators.n; i++)
		message_determine_needed_files(g->message_generators.v[i], deps);
	for (int i = 0; i < g->extension_generators.n; i++)
		extension_determine_needed_files(g->extension_generators.v[i], deps);
	if (public_deps_handling == PUBLIC_DEPS_EXCLUDE)
		for (int i = 0; i < file->n_public_dependencies; i++)
			file_set_erase(deps, file); /* (upstream erases the file itself) */
}

/* GeneratedFileOptions */
typedef struct {
	Vec ignored_warnings;       /* const char* */
	Vec forced_files_to_import; /* const LpFile* */
	Vec extra_system_headers;   /* const char* */
} GeneratedFileOptions;

typedef void (*FileBodyFn)(const FileGenerator *g, LpPrinter *p, void *arg);

typedef struct {
	const FileGenerator *g;
	const ImportWriter *import_writer;
	const GeneratedFileOptions *file_options;
} FileHeaderArgs;

static void file_runtime_imports(LpPrinter *p, void *arg)
{
	const FileHeaderArgs *a = arg;
	import_writer_print_runtime_imports(a->import_writer, p, !a->g->is_bundled_proto);
}

static void file_extra_system_imports(LpPrinter *p, void *arg)
{
	const FileHeaderArgs *a = arg;
	if (!a->file_options->extra_system_headers.n)
		return;
	for (int i = 0; i < a->file_options->extra_system_headers.n; i++) {
		LpSub subs[] = {LP_SUB("header", a->file_options->extra_system_headers.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" #import <$header$>\n");
	}
	lp_emit(p, NULL, 0, "\n");
}

static void file_file_imports(LpPrinter *p, void *arg)
{
	const FileHeaderArgs *a = arg;
	import_writer_print_file_imports(a->import_writer, p);
}

static void file_extra_warnings(LpPrinter *p, void *arg)
{
	const FileHeaderArgs *a = arg;
	for (int i = 0; i < a->file_options->ignored_warnings.n; i++) {
		LpSub subs[] = {LP_SUB("warning", a->file_options->ignored_warnings.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" #pragma clang diagnostic ignored \"-W$warning$\"\n");
	}
}

typedef enum { GENERATED_FILE_HEADER, GENERATED_FILE_SOURCE } GeneratedFileType;

static void file_generate_file(const FileGenerator *g, LpPrinter *p, GeneratedFileType file_type,
			       const GeneratedFileOptions *file_options, FileBodyFn body, void *body_arg)
{
	const GenerationOptions *opts = g->generation_options;
	const LpFile *file = g->file;
	ImportWriter import_writer;
	import_writer_init(&import_writer, opts->generate_for_named_framework, opts->runtime_import_prefix,
			   g->is_bundled_proto);

	FileSet file_imports = {0};
	switch (file_type) {
	case GENERATED_FILE_HEADER:
		/* the bundled files get minimal imports, everything else the wrapper */
		if (g->is_bundled_proto) {
			import_writer_add_runtime_import(&import_writer, "GPBDescriptor.h");
			import_writer_add_runtime_import(&import_writer, "GPBMessage.h");
			import_writer_add_runtime_import(&import_writer, "GPBRootObject.h");
		} else {
			import_writer_add_runtime_import(&import_writer, "GPBProtocolBuffers.h");
		}
		if (file_headers_use_forward_declarations(g)) {
			/* the headers of the public imports */
			for (int i = 0; i < file->n_public_dependencies; i++)
				file_set_insert(&file_imports, file->dependencies[file->public_dependencies[i]]);
		} else if (opts->generate_minimal_imports) {
			file_determine_needed_deps(g, &file_imports, PUBLIC_DEPS_FORCE_INCLUDE);
		} else {
			for (int i = 0; i < file->n_dependencies; i++)
				file_set_insert(&file_imports, file->dependencies[i]);
		}
		break;
	case GENERATED_FILE_SOURCE:
		import_writer_add_runtime_import(&import_writer, "GPBProtocolBuffers_RuntimeSupport.h");
		if (g->is_bundled_proto && has_wkt_with_objc_category(file))
			import_writer_add_runtime_import(&import_writer, "GPBWellKnownTypes.h");
		import_writer_add_file(&import_writer, file, kHeaderExtension);
		if (file_headers_use_forward_declarations(g)) {
			if (opts->generate_minimal_imports) {
				file_determine_needed_deps(g, &file_imports, PUBLIC_DEPS_EXCLUDE);
			} else {
				/* the plain (not public) dependencies */
				Map public_import_names = {0};
				for (int i = 0; i < file->n_public_dependencies; i++)
					map_put(&public_import_names, file->dependencies[file->public_dependencies[i]]->name, "");
				for (int i = 0; i < file->n_dependencies; i++)
					if (!map_get(&public_import_names, file->dependencies[i]->name))
						file_set_insert(&file_imports, file->dependencies[i]);
			}
		}
		break;
	}

	/* a forced file that is a direct dep moves into file_imports */
	Vec extra_files_to_import = {0};
	for (int i = 0; i < file_options->forced_files_to_import.n; i++) {
		const LpFile *dep = file_options->forced_files_to_import.v[i];
		if (is_direct_dependency(dep, file))
			file_set_insert(&file_imports, dep);
		else
			vec_push(&extra_files_to_import, (void *)dep);
	}

	if (file_imports.size) {
		/* in the order they were listed as dependencies */
		for (int i = 0; i < file->n_dependencies; i++) {
			const LpFile *dep = file->dependencies[i];
			if (file_set_contains(&file_imports, dep)) {
				import_writer_add_file(&import_writer, dep, kHeaderExtension);
				file_set_erase(&file_imports, dep);
			}
		}
		if (file_imports.size) {
			/* public imports into the non public imports (minimal imports mode only), sorted */
			if (!opts->generate_minimal_imports)
				fail("Check failed: generation_options_.generate_minimal_imports");
			Vec still_needed = {0};
			for (int i = 0; i < file_imports.v.n; i++)
				if (file_set_contains(&file_imports, file_imports.v.v[i]))
					vec_push(&still_needed, file_imports.v.v[i]);
			sort_files_by_name(&still_needed);
			for (int i = 0; i < still_needed.n; i++)
				vec_push(&extra_files_to_import, still_needed.v[i]);
		}
	}
	for (int i = 0; i < extra_files_to_import.n; i++)
		import_writer_add_file(&import_writer, extra_files_to_import.v[i], kHeaderExtension);

	/* for all Emit() calls */
	LpSub vars[] = {
		/* not the directive itself in the template strings */
		LP_SUB("clangfmt", "clang-format"),
		LP_SUB("root_class_name", g->root_class_name),
		LP_SUB("file_unique_symbol_name", g->file_unique_symbol_name),
		LP_SUB("file_registry_function_name", g->file_registry_function_name),
		LP_SUB("google_protobuf_runtime_support",
		       "GOOGLE_PROTOBUF_OBJC_EXPECTED_GENCODE_VERSION_" GOOGLE_PROTOBUF_OBJC_VERSION),
	};
	lp_push_vars(p, vars, 5);

	FileHeaderArgs a = {g, &import_writer, file_options};
	LpSub subs[] = {
		LP_SUB("no_checked_in", "NO CHECKED-IN PROTOBUF GENCODE"),
		LP_SUB("filename", file->name),
		LP_SUB("google_protobuf_objc_version", GOOGLE_PROTOBUF_OBJC_VERSION),
		LP_SUB_CB("runtime_imports", file_runtime_imports, &a),
		LP_SUB_CB("extra_system_imports", file_extra_system_imports, &a),
		LP_SUB_CB("file_imports", file_file_imports, &a),
		LP_SUB_CB("extra_warnings", file_extra_warnings, &a),
	};
	lp_emit(p, subs, 7,
		"\n"
		" // Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
		" // $no_checked_in$\n"
		" // $clangfmt$ off\n"
		" // source: $filename$\n"
		"\n"
		" $runtime_imports$\n"
		"\n"
		" #if GOOGLE_PROTOBUF_OBJC_VERSION < $google_protobuf_objc_version$\n"
		" #error This file was generated by a newer version of protoc which is incompatible with your Protocol Buffer library sources.\n"
		" #endif\n"
		" #if $google_protobuf_objc_version$ < GOOGLE_PROTOBUF_OBJC_MIN_SUPPORTED_VERSION\n"
		" #error This file was generated by an older version of protoc which is incompatible with your Protocol Buffer library sources.\n"
		" #endif\n"
		"\n"
		" $extra_system_imports$\n"
		" $file_imports$\n"
		" // @@protoc_insertion_point(imports)\n"
		"\n"
		" #pragma clang diagnostic push\n"
		" #pragma clang diagnostic ignored \"-Wdeprecated-declarations\"\n"
		" $extra_warnings$\n");

	lp_emit(p, NULL, 0, "\n");
	body(g, p, body_arg);
	lp_emit(p, NULL, 0, "\n");
	lp_emit(p, NULL, 0,
		"\n"
		" #pragma clang diagnostic pop\n"
		"\n"
		" // @@protoc_insertion_point(global_scope)\n"
		"\n"
		" // $clangfmt$ on\n");
	lp_pop_vars(p);
}

/* ---- GenerateHeader ---- */

static void file_header_body(const FileGenerator *g, LpPrinter *p, void *arg)
{
	const char *info_path = arg;
	const GenerationOptions *opts = g->generation_options;
	StringSet fwd_decls = {0};
	for (int i = 0; i < g->message_generators.n; i++)
		message_determine_forward_declarations(g->message_generators.v[i], &fwd_decls,
						       file_headers_use_forward_declarations(g));
	string_set_finish(&fwd_decls);

	lp_emit(p, NULL, 0, "CF_EXTERN_C_BEGIN\n\n");
	if (fwd_decls.v.n) {
		LpSub subs[] = {LP_SUB("fwd_decls", string_set_join(&fwd_decls, "\n"))};
		lp_emit(p, subs, 1, "$fwd_decls$\n\n");
	}
	lp_emit(p, NULL, 0, "NS_ASSUME_NONNULL_BEGIN\n\n");

	if (*info_path) {
		LpSub subs[] = {
			LP_SUB("info_path", info_path),
			LP_SUB("guard", opts->annotation_guard_name),
			LP_SUB("pragma", opts->annotation_pragma_name),
		};
		lp_emit(p, subs, 3,
			"\n"
			" #ifdef $guard$\n"
			" #pragma $pragma$ \"$info_path$\"\n"
			" #endif  // $guard$\n");
		lp_emit(p, NULL, 0, "\n");
	}

	for (int i = 0; i < g->enum_generators.n; i++)
		enum_generate_header(g->enum_generators.v[i], p);

	if (emit_c_function_extensions(opts)) {
		/* the Root is created even without extensions, for chaining */
		lp_emit(p, NULL, 0,
			"\n"
			" #pragma mark - $file_unique_symbol_name$ extension registry\n"
			"\n"
			" /**\n"
			"  * An @c GPBExtensionRegistry that includes all the extensions defined by\n"
			"  * this file and all files that it depends on.\n"
			"  **/\n"
			" GPBExtensionRegistry *$file_registry_function_name$(void);\n");
		lp_emit(p, NULL, 0, "\n");
		for (size_t i = 0; i < g->file_scoped_extension_count; i++)
			extension_generate_functions_header(g->extension_generators.v[i], p);
	}

	if (emit_class_based_extensions(opts)) {
		/* the root class interface */
		lp_emit(p, NULL, 0,
			"\n"
			" #pragma mark - $root_class_name$\n"
			"\n"
			" /**\n"
			"  * Exposes the extension registry for this file.\n"
			"  *\n"
			"  * The base class provides:\n"
			"  * @code\n"
			"  *   + (GPBExtensionRegistry *)extensionRegistry;\n"
			"  * @endcode\n"
			"  * which is a @c GPBExtensionRegistry that includes all the extensions defined by\n"
			"  * this file and all files that it depends on.\n"
			"  **/\n"
			" GPB_FINAL @interface $root_class_name$ : GPBRootObject\n"
			" @end\n");
		lp_emit(p, NULL, 0, "\n");
		/* the dynamic methods of the file scoped extensions (the first ones) */
		if (g->file_scoped_extension_count) {
			lp_emit(p, NULL, 0, "@interface $root_class_name$ (DynamicMethods)\n");
			for (size_t i = 0; i < g->file_scoped_extension_count; i++)
				extension_generate_methods_header(g->extension_generators.v[i], p);
			lp_emit(p, NULL, 0, "@end\n\n");
		}
	}

	for (int i = 0; i < g->message_generators.n; i++)
		message_generate_message_header(g->message_generators.v[i], p);

	lp_emit(p, NULL, 0,
		"\n"
		" NS_ASSUME_NONNULL_END\n"
		"\n"
		" CF_EXTERN_C_END\n");
}

static void file_generate_header(const FileGenerator *g, LpPrinter *p, const char *info_path)
{
	GeneratedFileOptions file_options = {0};
	file_generate_file(g, p, GENERATED_FILE_HEADER, &file_options, file_header_body, (void *)info_path);
}

/* ---- the root and the extension registry ---- */

typedef struct {
	const FileGenerator *g;
	const Vec *deps_with_extensions;
} FileDepsArgs;

static void file_register_local_extensions_variable_blocks(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	for (int i = 0; i < a->g->extension_generators.n; i++)
		extension_generate_static_variables_initialization(a->g->extension_generators.v[i], p);
}

static void file_class_based_register_local_extensions(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	if (!a->g->extension_generators.n)
		return;
	LpSub subs[] = {LP_SUB_CB("register_local_extensions_variable_blocks",
				  file_register_local_extensions_variable_blocks, (void *)a)};
	lp_emit(p, subs, 1,
		"\n"
		" static GPBExtensionDescription descriptions[] = {\n"
		"   $register_local_extensions_variable_blocks$\n"
		" };\n"
		" for (size_t i = 0; i < sizeof(descriptions) / sizeof(descriptions[0]); ++i) {\n"
		"   GPBExtensionDescriptor *extension =\n"
		"       [[GPBExtensionDescriptor alloc] initWithExtensionDescription:&descriptions[i]\n"
		"                                                     runtimeSupport:&$google_protobuf_runtime_support$];\n"
		"   [registry addExtension:extension];\n"
		"   [self globallyRegisterExtension:extension];\n"
		"   [extension release];\n"
		" }\n");
}

static void file_class_based_register_imports(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	if (!a->deps_with_extensions->n) {
		lp_emit(p, NULL, 0,
			"\n"
			" // None of the imports (direct or indirect) defined extensions, so no need to add\n"
			" // them to this registry.\n");
		return;
	}
	lp_emit(p, NULL, 0,
		"\n"
		" // Merge in the imports (direct or indirect) that defined extensions.\n");
	for (int i = 0; i < a->deps_with_extensions->n; i++) {
		LpSub subs[] = {LP_SUB("dependency", lp_objc_file_class_name(a->deps_with_extensions->v[i]))};
		lp_emit(p, subs, 1,
			"\n"
			" [registry addExtensions:[$dependency$ extensionRegistry]];\n");
	}
}

static void file_emit_root_extension_registry_class_based_class_methods(const FileGenerator *g, LpPrinter *p,
									 const Vec *deps_with_extensions)
{
	FileDepsArgs a = {g, deps_with_extensions};
	LpSub subs[] = {
		LP_SUB_CB("register_local_extensions", file_class_based_register_local_extensions, &a),
		LP_SUB_CB("register_imports", file_class_based_register_imports, &a),
	};
	lp_emit(p, subs, 2,
		"\n"
		" + (GPBExtensionRegistry*)extensionRegistry {\n"
		"   // This is called by +initialize so there is no need to worry\n"
		"   // about thread safety and initialization of registry.\n"
		"   static GPBExtensionRegistry* registry = nil;\n"
		"   if (!registry) {\n"
		"     registry = [[GPBExtensionRegistry alloc] init];\n"
		"     $register_local_extensions$;\n"
		"     $register_imports$\n"
		"   }\n"
		"   return registry;\n"
		" }\n");
}

static void file_add_extension_to_registry_function_calls(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	for (int i = 0; i < a->g->extension_generators.n; i++)
		extension_generate_add_extension_to_registry_function_call(a->g->extension_generators.v[i], p);
}

static void file_add_extension_to_global_registry_function_calls(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	if (a->g->generation_options->extension_generation_mode != EXT_MODE_MIGRATION)
		return;
	for (int i = 0; i < a->g->extension_generators.n; i++)
		extension_generate_add_extension_to_global_registry_function_call(a->g->extension_generators.v[i], p);
}

static void file_c_function_register_local_extensions(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	if (!a->g->extension_generators.n)
		return;
	LpSub subs[] = {
		LP_SUB_CB("add_extension_to_registry_function_calls", file_add_extension_to_registry_function_calls,
			  (void *)a),
		LP_SUB_CB("add_extension_to_global_registry_function_calls",
			  file_add_extension_to_global_registry_function_calls, (void *)a),
	};
	lp_emit(p, subs, 2,
		"\n"
		" $add_extension_to_registry_function_calls$\n"
		" $add_extension_to_global_registry_function_calls$\n");
}

static void file_c_function_register_imports(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	if (!a->deps_with_extensions->n) {
		lp_emit(p, NULL, 0,
			"\n"
			" // None of the imports (direct or indirect) defined extensions, so no need to add\n"
			" // them to this registry.\n");
		return;
	}
	lp_emit(p, NULL, 0,
		"\n"
		" // Merge in the imports (direct or indirect) that defined extensions.\n");
	for (int i = 0; i < a->deps_with_extensions->n; i++) {
		LpSub subs[] = {LP_SUB("dependency_registry",
				       lp_objc_file_unique_symbol_name(a->deps_with_extensions->v[i], "Registry"))};
		lp_emit(p, subs, 1,
			"\n"
			" [registry addExtensions:$dependency_registry$()];\n");
	}
}

static void file_extension_descriptor_functions(LpPrinter *p, void *arg)
{
	const FileDepsArgs *a = arg;
	for (int i = 0; i < a->g->extension_generators.n; i++)
		extension_generate_descriptor_function(a->g->extension_generators.v[i], p);
}

static void file_emit_extension_registry_and_descriptor_functions(const FileGenerator *g, LpPrinter *p,
								   const Vec *deps_with_extensions)
{
	FileDepsArgs a = {g, deps_with_extensions};
	LpSub subs[] = {
		LP_SUB_CB("register_local_extensions", file_c_function_register_local_extensions, &a),
		LP_SUB_CB("register_imports", file_c_function_register_imports, &a),
		LP_SUB_CB("extension_descriptor_functions", file_extension_descriptor_functions, &a),
	};
	lp_emit(p, subs, 3,
		"\n"
		" GPBExtensionRegistry *$file_unique_symbol_name$_Registry(void) {\n"
		"   static GPBExtensionRegistry *registry = nil;\n"
		"   static dispatch_once_t onceToken;\n"
		"   dispatch_once(&onceToken, ^{\n"
		"     registry = [[GPBExtensionRegistry alloc] init];\n"
		"     $register_local_extensions$;\n"
		"     $register_imports$\n"
		"   });\n"
		"   return registry;\n"
		" }\n"
		"\n"
		" $extension_descriptor_functions$\n");
}

static void file_emit_root_implementation(const FileGenerator *g, LpPrinter *p, const Vec *deps_with_extensions)
{
	const GenerationOptions *opts = g->generation_options;
	if (emit_c_function_extensions(opts)) {
		lp_emit(p, NULL, 0,
			"\n"
			" #pragma mark - $file_unique_symbol_name$ extension registry\n");
		lp_emit(p, NULL, 0, "\n");
		file_emit_extension_registry_and_descriptor_functions(g, p, deps_with_extensions);
		lp_emit(p, NULL, 0, "\n");
	}
	if (emit_class_based_extensions(opts)) {
		lp_emit(p, NULL, 0,
			"\n"
			" #pragma mark - $root_class_name$\n"
			"\n"
			" @implementation $root_class_name$\n");
		lp_emit(p, NULL, 0, "\n");
		/* a registry to override when the file or its dependencies have extensions */
		if (!g->extension_generators.n && !deps_with_extensions->n) {
			lp_emit(p, NULL, 0,
				"\n"
				" // No extensions in the file and no imports or none of the imports (direct or\n"
				" // indirect) defined extensions, so no need to generate +extensionRegistry.\n");
		} else if (opts->extension_generation_mode == EXT_MODE_MIGRATION) {
			/* EmitRootExtensionRegistryMigrationClassMethods */
			lp_emit(p, NULL, 0,
				"\n"
				" + (GPBExtensionRegistry*)extensionRegistry {\n"
				"   return $file_unique_symbol_name$_Registry();\n"
				" }\n");
		} else {
			file_emit_root_extension_registry_class_based_class_methods(g, p, deps_with_extensions);
		}
		lp_emit(p, NULL, 0, "\n");
		lp_emit(p, NULL, 0, "@end\n");
	}
	lp_emit(p, NULL, 0, "\n");
}

static void file_emit_file_description(const FileGenerator *g, LpPrinter *p)
{
	/* only needed if messages use it */
	if (!g->message_generators.n)
		return;
	const LpFile *file = g->file;
	char *objc_prefix = lp_objc_file_class_prefix(file);
	/* (the syntax computed from the edition is not printed) */
	LpSub subs[] = {
		LP_SUB("file_description_name", g->file_description_name),
		LP_SUB("package_value", !*file->package ? "NULL" : xcat("\"", file->package, "\"")),
		LP_SUB("prefix_value", !*objc_prefix && !pb_has(file->options, FILEOPT_OBJC_CLASS_PREFIX)
					       ? "NULL"
					       : xcat("\"", objc_prefix, "\"")),
	};
	lp_emit(p, subs, 3,
		"\n"
		" static GPBFilePackageAndPrefix $file_description_name$ = {\n"
		"   .package = $package_value$,\n"
		"   .prefix = $prefix_value$\n"
		" };\n");
	lp_emit(p, NULL, 0, "\n");
}

/* ---- GenerateSource ---- */

typedef struct {
	StringSet *fwd_decls;
	const Vec *deps_with_extensions;
	int message_index; /* GenerateSourceForMessage */
} FileSourceArgs;

static void file_source_body(const FileGenerator *g, LpPrinter *p, void *arg)
{
	const FileSourceArgs *a = arg;
	emit_source_fwd_decls(a->fwd_decls, p);
	file_emit_root_implementation(g, p, a->deps_with_extensions);
	file_emit_file_description(g, p);
	if (g->is_bundled_proto && has_wkt_with_objc_category(g->file))
		emit_link_wkts(g->file->name, p);
	for (int i = 0; i < g->enum_generators.n; i++)
		enum_generate_source(g->enum_generators.v[i], p);
	for (int i = 0; i < g->message_generators.n; i++)
		message_generate_source(g->message_generators.v[i], p);
}

static void file_generate_source(const FileGenerator *g, LpPrinter *p)
{
	Vec deps_with_extensions = collect_minimal_file_deps_containing_extensions(g->common_state, g->file);
	GeneratedFileOptions file_options = {0};
	file_options.forced_files_to_import = deps_with_extensions;

	StringSet fwd_decls = {0};
	for (int i = 0; i < g->message_generators.n; i++)
		message_determine_objective_c_class_definitions(g->message_generators.v[i], &fwd_decls);
	for (int i = 0; i < g->extension_generators.n; i++)
		extension_determine_objective_c_class_definitions(g->extension_generators.v[i], &fwd_decls);
	string_set_finish(&fwd_decls);

	/* oneofs use direct ivar access */
	for (int i = 0; i < g->message_generators.n; i++) {
		if (message_includes_oneof_definition(g->message_generators.v[i])) {
			vec_push(&file_options.ignored_warnings, "direct-ivar-access");
			break;
		}
	}
	if (fwd_decls.v.n)
		vec_push(&file_options.ignored_warnings, "dollar-in-identifier-extension");
	/* enums use atomic */
	if (g->enum_generators.n)
		vec_push(&file_options.extra_system_headers, "stdatomic.h");

	FileSourceArgs a = {&fwd_decls, &deps_with_extensions, 0};
	file_generate_file(g, p, GENERATED_FILE_SOURCE, &file_options, file_source_body, &a);
}

/* ---- experimental_multi_source_generation ---- */

static void file_global_source_body(const FileGenerator *g, LpPrinter *p, void *arg)
{
	const FileSourceArgs *a = arg;
	emit_source_fwd_decls(a->fwd_decls, p);
	file_emit_root_implementation(g, p, a->deps_with_extensions);
}

static void file_check_not_bundled(const FileGenerator *g)
{
	if (g->is_bundled_proto)
		fail("Check failed: !is_bundled_proto_ Bundled protos aren't expected to use multi source generation.");
}

static void file_generate_global_source(const FileGenerator *g, LpPrinter *p)
{
	file_check_not_bundled(g);
	Vec deps_with_extensions = collect_minimal_file_deps_containing_extensions(g->common_state, g->file);
	GeneratedFileOptions file_options = {0};
	file_options.forced_files_to_import = deps_with_extensions;
	StringSet fwd_decls = {0};
	for (int i = 0; i < g->extension_generators.n; i++)
		extension_determine_objective_c_class_definitions(g->extension_generators.v[i], &fwd_decls);
	string_set_finish(&fwd_decls);
	if (fwd_decls.v.n)
		vec_push(&file_options.ignored_warnings, "dollar-in-identifier-extension");
	FileSourceArgs a = {&fwd_decls, &deps_with_extensions, 0};
	file_generate_file(g, p, GENERATED_FILE_SOURCE, &file_options, file_global_source_body, &a);
}

static void file_enums_source_body(const FileGenerator *g, LpPrinter *p, void *arg)
{
	for (int i = 0; i < g->enum_generators.n; i++)
		enum_generate_source(g->enum_generators.v[i], p);
}

static void file_generate_source_for_enums(const FileGenerator *g, LpPrinter *p)
{
	file_check_not_bundled(g);
	GeneratedFileOptions file_options = {0};
	vec_push(&file_options.extra_system_headers, "stdatomic.h");
	file_generate_file(g, p, GENERATED_FILE_SOURCE, &file_options, file_enums_source_body, NULL);
}

static void file_message_source_body(const FileGenerator *g, LpPrinter *p, void *arg)
{
	const FileSourceArgs *a = arg;
	emit_source_fwd_decls(a->fwd_decls, p);
	file_emit_file_description(g, p);
	message_generate_source(g->message_generators.v[a->message_index], p);
}

static void file_generate_source_for_message(const FileGenerator *g, int idx, LpPrinter *p)
{
	file_check_not_bundled(g);
	const MessageGenerator *generator = g->message_generators.v[idx];
	StringSet fwd_decls = {0};
	message_determine_objective_c_class_definitions(generator, &fwd_decls);
	string_set_finish(&fwd_decls);
	GeneratedFileOptions file_options = {0};
	if (message_includes_oneof_definition(generator))
		vec_push(&file_options.ignored_warnings, "direct-ivar-access");
	FileSourceArgs a = {&fwd_decls, NULL, idx};
	file_generate_file(g, p, GENERATED_FILE_SOURCE, &file_options, file_message_source_body, &a);
}
