// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/kotlin v36.2: a port of the Kotlin generator of protoc v36.2
 * (buf.build/protocolbuffers/kotlin, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (`kotlin::KotlinGenerator generator; return PluginMain(argc, argv,
 * &generator);`) and src/google/protobuf/compiler/kotlin/{generator,file,message,field}.{h,cc}
 * (this file, file.c, message.c, field.c). The shared core of compiler/java (names, helpers,
 * context, doc comments) is pkg/languages/cpp/libprotoc/compiler/java and the libprotoc parts
 * (plugin.cc, descriptor.cc, io::Printer, ...) are pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "libprotoc_java.h"
#include "uni.h"

#include <string.h>

#include "field.c"
#include "message.c"
#include "file.c"

/* ---- generator.cc: KotlinGenerator ---- */

static bool kotlin_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *context,
			    Buf *error)
{
	(void)gen;
	/* parse generator options */
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	JvOptions file_options = jv_default_options();
	for (int i = 0; i < n; i++) {
		const char *k = keys[i];
		if (str_eq(k, "output_list_file")) {
			file_options.output_list_file = values[i];
		} else if (str_eq(k, "immutable")) {
			/* Note: the option is considered always set regardless of the input. */
			file_options.generate_immutable_code = true;
		} else if (str_eq(k, "mutable")) {
			buf_adds(error, "Mutable not supported by Kotlin generator");
			return false;
		} else if (str_eq(k, "shared")) {
			/* Note: the option is considered always set regardless of the input. */
			file_options.generate_shared_code = true;
		} else if (str_eq(k, "lite")) {
			file_options.enforce_lite = true;
		} else if (str_eq(k, "annotate_code")) {
			file_options.annotate_code = true;
		} else if (str_eq(k, "annotation_list_file")) {
			file_options.annotation_list_file = values[i];
		} else if (str_eq(k, "experimental_strip_nonfunctional_codegen")) {
			file_options.strip_nonfunctional_codegen = true;
		} else if (str_eq(k, "no_jvm_dsl")) {
			file_options.jvm_dsl = false;
			file_options.dsl_use_concrete_types = true;
		} else {
			buf_adds(error, xcat("Unknown generator option: ", k));
			return false;
		}
	}

	/* We only support generation of immutable code so we do it. */
	file_options.generate_immutable_code = true;
	file_options.generate_shared_code = true;

	Vec all_files = {0};
	KtFileGenerator *file_generator = kt_file_generator_new(file, &file_options);

	char *package_dir = jv_java_package_to_dir(file_generator->java_package);
	char *kotlin_filename = xcat(package_dir, kt_file_generator_get_kotlin_classname(file_generator), ".proto.kt");
	vec_push(&all_files, kotlin_filename);

	/* Generate main kotlin file. */
	Buf annotations = {0};
	LpPrinter printer;
	lp_printer_init(&printer, lp_context_open(context, kotlin_filename), '$');
	if (file_options.annotate_code)
		printer.annotations = &annotations;

	kt_file_generator_generate(file_generator, &printer);
	kt_file_generator_generate_siblings(file_generator, package_dir, context, &all_files);

	if (file_options.annotate_code)
		kt_print_annotations(&printer, &annotations);

	/* Generate output list if requested. */
	if (*file_options.output_list_file) {
		LpPrinter srclist;
		lp_printer_init(&srclist, lp_context_open(context, file_options.output_list_file), '$');
		for (int i = 0; i < all_files.n; i++)
			lp_print(&srclist, "$filename$\n", "filename", VEC_AT(all_files, char, i), NULL);
	}
	return true;
}

static const char *const kotlin_feature_extensions[] = {"pb.java", NULL};

/* generator.h: KotlinGenerator */
static const LpGenerator kotlin_generator = {
	.plugin_name = "protoc-gen-kotlin",
	.generate = kotlin_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
	.feature_extensions = kotlin_feature_extensions,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &kotlin_generator, out);
}

const UniGenerator gen_protocolbuffers_kotlin_v36_2 = {
	.name = "protocolbuffers/kotlin",
	.version = "v36.2",
	.generate = generate,
};
