// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* Port of src/google/protobuf/compiler/objectivec/import_writer.{h,cc} (protobuf v36.2). The
 * framework mappings file (named_framework_to_proto_path_mappings_path) is not read: generator.c
 * rejects a non-empty path, so ModuleForFile always finds no module. */

typedef struct {
	const char *generate_for_named_framework;
	const char *runtime_import_prefix;
	bool for_bundled_proto;
	Vec protobuf_imports;        /* const char* */
	Vec other_framework_imports;
	Vec other_imports;
} ImportWriter;

static void import_writer_init(ImportWriter *w, const char *generate_for_named_framework,
			       const char *runtime_import_prefix, bool for_bundled_proto)
{
	*w = (ImportWriter){0};
	w->generate_for_named_framework = generate_for_named_framework;
	w->runtime_import_prefix = runtime_import_prefix;
	w->for_bundled_proto = for_bundled_proto;
}

static void import_writer_add_file(ImportWriter *w, const LpFile *file, const char *header_extension)
{
	if (lp_objc_is_protobuf_library_bundled_proto_file(file)) {
		/* The imports of the WKTs are only needed within the library itself; elsewhere the
		 * generated code imports GPBProtocolBuffers.h, which provides them. */
		if (w->for_bundled_proto)
			vec_push(&w->protobuf_imports, xcat("GPB", lp_objc_file_path_basename(file), header_extension));
		return;
	}
	/* (ModuleForFile: no mappings file, no module) */
	if (*w->generate_for_named_framework) {
		vec_push(&w->other_framework_imports, xcat(w->generate_for_named_framework, "/",
							    lp_objc_file_path_basename(file), header_extension));
		return;
	}
	vec_push(&w->other_imports, xcat(lp_objc_file_path(file), header_extension));
}

static void import_writer_add_runtime_import(ImportWriter *w, const char *header_name)
{
	vec_push(&w->protobuf_imports, (void *)header_name);
}

static void import_writer_print_file_imports(const ImportWriter *w, LpPrinter *p)
{
	for (int i = 0; i < w->other_framework_imports.n; i++) {
		LpSub subs[] = {LP_SUB("header", w->other_framework_imports.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" #import <$header$>\n");
	}
	for (int i = 0; i < w->other_imports.n; i++) {
		LpSub subs[] = {LP_SUB("header", w->other_imports.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" #import \"$header$\"\n");
	}
}

typedef struct {
	const ImportWriter *w;
	bool default_cpp_symbol;
} PrintRuntimeImports;

static void import_writer_maybe_default_cpp_symbol(LpPrinter *p, void *arg)
{
	const PrintRuntimeImports *a = arg;
	if (!a->default_cpp_symbol)
		return;
	lp_emit(p, NULL, 0,
		"\n"
		" // This CPP symbol can be defined to use imports that match up to the framework\n"
		" // imports needed when using CocoaPods.\n"
		" #if !defined($cpp_symbol$)\n"
		"  #define $cpp_symbol$ 0\n"
		" #endif\n");
}

static void import_writer_framework_imports(LpPrinter *p, void *arg)
{
	const PrintRuntimeImports *a = arg;
	for (int i = 0; i < a->w->protobuf_imports.n; i++) {
		LpSub subs[] = {LP_SUB("header", a->w->protobuf_imports.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" #import <$framework_name$/$header$>\n");
	}
}

static void import_writer_raw_imports(LpPrinter *p, void *arg)
{
	const PrintRuntimeImports *a = arg;
	for (int i = 0; i < a->w->protobuf_imports.n; i++) {
		LpSub subs[] = {LP_SUB("header", a->w->protobuf_imports.v[i])};
		lp_emit(p, subs, 1,
			"\n"
			" #import \"$header$\"\n");
	}
}

static void import_writer_print_runtime_imports(const ImportWriter *w, LpPrinter *p, bool default_cpp_symbol)
{
	/* given an override, use that */
	if (*w->runtime_import_prefix) {
		for (int i = 0; i < w->protobuf_imports.n; i++) {
			LpSub subs[] = {LP_SUB("import_prefix", w->runtime_import_prefix),
					LP_SUB("header", w->protobuf_imports.v[i])};
			lp_emit(p, subs, 2,
				"\n"
				" #import \"$import_prefix$/$header$\"\n");
		}
		return;
	}
	/* bundled: no framework support needed */
	if (w->for_bundled_proto) {
		for (int i = 0; i < w->protobuf_imports.n; i++) {
			LpSub subs[] = {LP_SUB("header", w->protobuf_imports.v[i])};
			lp_emit(p, subs, 1,
				"\n"
				" #import \"$header$\"\n");
		}
		return;
	}
	PrintRuntimeImports a = {w, default_cpp_symbol};
	LpSub subs[] = {
		LP_SUB("cpp_symbol", lp_objc_protobuf_framework_import_symbol(LP_OBJC_PROTOBUF_LIBRARY_FRAMEWORK_NAME)),
		LP_SUB_CB("maybe_default_cpp_symbol", import_writer_maybe_default_cpp_symbol, &a),
		LP_SUB("framework_name", LP_OBJC_PROTOBUF_LIBRARY_FRAMEWORK_NAME),
		LP_SUB_CB("framework_imports", import_writer_framework_imports, &a),
		LP_SUB_CB("raw_imports", import_writer_raw_imports, &a),
	};
	lp_emit(p, subs, 5,
		"\n"
		" $maybe_default_cpp_symbol$\n"
		"\n"
		" #if $cpp_symbol$\n"
		"  $framework_imports$\n"
		" #else\n"
		"  $raw_imports$\n"
		" #endif\n");
}
