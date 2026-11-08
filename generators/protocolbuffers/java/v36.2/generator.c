// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

/* protocolbuffers/java v36.2: a port of the Java generator of protoc v36.2
 * (buf.build/protocolbuffers/java, https://github.com/protocolbuffers/protobuf): the plugin main
 * of bufbuild/plugins (`java::JavaGenerator g; return PluginMain(argc, argv, &g);`) and
 * src/google/protobuf/compiler/java/{generator,file,shared_code_generator}.{h,cc} (this file,
 * file.c, shared_code_generator.c), java/full (full/) and java/lite (lite/). The shared core of
 * compiler/java (names, helpers, context, doc comments) is pkg/languages/cpp/libprotoc/compiler/java
 * and the libprotoc parts (plugin.cc, descriptor.cc, retention.cc, io::Printer, ...) are
 * pkg/languages/cpp/libprotoc.
 *
 * Unity build: this file includes the other .c files of the version directory, and everything
 * but the generator below is static. */
#include "internal.h"

/* generator_factory.h + make_field_gens.h of full/ and lite/ */
static JvMessageGen *jv_new_message_generator(const JvGeneratorFactory *f, const LpMessage *descriptor);
static void jv_message_generator_generate_static_variables(JvMessageGen *g, LpPrinter *printer, int *bytecode_estimate);
static int jv_message_generator_generate_static_variable_initializers(JvMessageGen *g, LpPrinter *printer);
static void jv_message_generator_generate(JvMessageGen *g, LpPrinter *printer);
static void jv_message_generator_generate_interface(JvMessageGen *g, LpPrinter *printer);
static void jv_message_generator_generate_extension_registration_code(JvMessageGen *g, LpPrinter *printer);
static void jv_enum_generator_generate(const JvGeneratorFactory *f, const LpEnum *descriptor, LpPrinter *printer);
static JvExtensionGen *jv_new_extension_generator(const JvGeneratorFactory *f, const LpField *descriptor);
static void jv_extension_generator_generate(const JvGeneratorFactory *f, JvExtensionGen *g, LpPrinter *printer);
static int jv_extension_generator_generate_non_nested_initialization_code(const JvGeneratorFactory *f, JvExtensionGen *g,
									  LpPrinter *printer);
static int jv_extension_generator_generate_registration_code(const JvGeneratorFactory *f, JvExtensionGen *g,
							     LpPrinter *printer);
static void jv_service_generator_generate(const JvGeneratorFactory *f, const LpService *descriptor, LpPrinter *printer);
static JvEnumGen *new_enum_gen(const LpEnum *descriptor, bool immutable_api, JvContext *context);
static JvExtensionGen *new_extension_gen(const LpField *descriptor, JvContext *context);
static JvMessageGen *new_message_gen(const LpMessage *descriptor, JvContext *context, bool lite);
static JvFieldGen *new_field_gen(const JvFieldGenOps *ops, void (*init)(JvFieldGen *), const LpField *field,
				 int message_bit_index, int builder_bit_index, JvContext *context);

#include "full/primitive_field.c"
#include "full/enum_field.c"
#include "full/string_field.c"
#include "full/message_field.c"
#include "full/map_field.c"
#include "full/make_field_gens.c"
#include "full/enum.c"
#include "full/extension.c"
#include "full/service.c"
#include "full/message_builder.c"
#include "full/message.c"
#include "lite/primitive_field.c"
#include "lite/enum_field.c"
#include "lite/string_field.c"
#include "lite/message_field.c"
#include "lite/map_field.c"
#include "lite/make_field_gens.c"
#include "lite/enum.c"
#include "lite/extension.c"
#include "lite/message_builder.c"
#include "lite/message.c"
#include "generator_factory.c"
#include "shared_code_generator.c"
#include "file.c"

/* ---- generator.cc: JavaGenerator ---- */

static bool java_generate(const LpGenerator *gen, const LpFile *file, const char *parameter, LpContext *context,
			  Buf *error)
{
	(void)gen;
	char **keys, **values;
	int n = lp_parse_generator_parameter(parameter, &keys, &values);
	JvOptions file_options = jv_default_options();
	for (int i = 0; i < n; i++) {
		const char *k = keys[i];
		if (str_eq(k, "output_list_file")) {
			file_options.output_list_file = values[i];
		} else if (str_eq(k, "immutable")) {
			file_options.generate_immutable_code = true;
		} else if (str_eq(k, "mutable")) {
			file_options.generate_mutable_code = true;
		} else if (str_eq(k, "shared")) {
			file_options.generate_shared_code = true;
		} else if (str_eq(k, "lite")) {
			file_options.enforce_lite = true;
		} else if (str_eq(k, "annotate_code")) {
			file_options.annotate_code = true;
		} else if (str_eq(k, "annotation_list_file")) {
			file_options.annotation_list_file = values[i];
		} else if (str_eq(k, "experimental_strip_nonfunctional_codegen")) {
			file_options.strip_nonfunctional_codegen = true;
		} else if (str_eq(k, "bootstrap")) {
			file_options.bootstrap = true;
		} else {
			buf_adds(error, xcat("Unknown generator option: ", k));
			return false;
		}
	}

	if (file_options.enforce_lite && file_options.generate_mutable_code) {
		buf_adds(error, "lite runtime generator option cannot be used with mutable API.");
		return false;
	}
	if (file_options.annotate_code) {
		/* Not ported: the annotations of io::Printer (GeneratedCodeInfo .pb.meta files). */
		buf_adds(error, "unsupported by protoc-gen-uni: option annotate_code");
		return false;
	}

	if (!file_options.generate_immutable_code && !file_options.generate_mutable_code &&
	    !file_options.generate_shared_code) {
		file_options.generate_immutable_code = true;
		file_options.generate_shared_code = true;
	}

	Vec all_files = {0};
	Vec all_annotations = {0};
	JvFileGenerator *file_generators[2];
	int n_gen = 0;
	if (file_options.generate_immutable_code)
		file_generators[n_gen++] = jv_file_generator_new(file, &file_options, true);
	if (file_options.generate_mutable_code)
		file_generators[n_gen++] = jv_file_generator_new(file, &file_options, false);

	for (int i = 0; i < n_gen; i++)
		if (!jv_file_generator_validate(file_generators[i], error))
			return false;

	for (int i = 0; i < n_gen; i++) {
		JvFileGenerator *fg = file_generators[i];
		char *package_dir = jv_java_package_to_dir(fg->java_package);
		char *java_filename = xcat(package_dir, fg->classname, ".java");
		vec_push(&all_files, java_filename);

		LpPrinter printer;
		lp_printer_init(&printer, lp_context_open(context, java_filename), '$');
		jv_file_generator_generate(fg, &printer);
		jv_file_generator_generate_siblings(fg, package_dir, context, &all_files, &all_annotations);
	}

	if (*file_options.output_list_file) {
		LpPrinter srclist;
		lp_printer_init(&srclist, lp_context_open(context, file_options.output_list_file), '$');
		for (int i = 0; i < all_files.n; i++)
			lp_print(&srclist, "$filename$\n", "filename", VEC_AT(all_files, char, i), NULL);
	}
	if (*file_options.annotation_list_file) {
		LpPrinter list;
		lp_printer_init(&list, lp_context_open(context, file_options.annotation_list_file), '$');
		for (int i = 0; i < all_annotations.n; i++)
			lp_print(&list, "$filename$\n", "filename", VEC_AT(all_annotations, char, i), NULL);
	}
	return true;
}

static const char *const java_feature_extensions[] = {"pb.java", NULL};

/* generator.h: JavaGenerator */
static const LpGenerator java_generator = {
	.plugin_name = "protoc-gen-java",
	.generate = java_generate,
	.supported_features = 3, /* FEATURE_PROTO3_OPTIONAL | FEATURE_SUPPORTS_EDITIONS */
	.minimum_edition = LP_EDITION_PROTO2,
	.maximum_edition = LP_EDITION_2026,
	.feature_extensions = java_feature_extensions,
};

static COLD const char *generate(PbMsg *req, Str raw, Buf *out)
{
	return lp_plugin_main(raw, pb_bytes(req, CGR_PARAMETER), &java_generator, out);
}

const UniGenerator gen_protocolbuffers_java_v36_2 = {
	.name = "protocolbuffers/java",
	.version = "v36.2",
	.generate = generate,
};
